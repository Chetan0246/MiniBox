#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "isolation.h"
#include "resources.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__x86_64__)
#define MINIBOX_AUDIT_ARCH AUDIT_ARCH_X86_64
#define MINIBOX_X32_SYSCALL_BIT 0x40000000u
#elif defined(__aarch64__)
#define MINIBOX_AUDIT_ARCH AUDIT_ARCH_AARCH64
#define MINIBOX_X32_SYSCALL_BIT 0u
#else
#define MINIBOX_AUDIT_ARCH 0
#define MINIBOX_X32_SYSCALL_BIT 0u
#endif

int minibox_detect_capabilities(minibox_capabilities_t *caps) {
    if (!caps) {
        errno = EINVAL;
        return -1;
    }
    memset(caps, 0, sizeof(*caps));

    /* 1. Kernel-level namespace support */
    struct stat st;
    if (stat("/proc/self/ns/user", &st) == 0) {
        FILE *f_userns = fopen("/proc/sys/user/max_user_namespaces", "r");
        if (f_userns) {
            long max_user = 0;
            if (fscanf(f_userns, "%ld", &max_user) == 1 && max_user > 0) {
                caps->kernel_user_ns = true;
            }
            fclose(f_userns);
        } else {
            caps->kernel_user_ns = true;
        }
    }

    /*
     * 2. Probe unprivileged namespace operation and child initialization.
     * Run in a separate probe process so the runtime state remains untouched.
     */
    pid_t probe_pid = fork();
    if (probe_pid == 0) {
        int mask = 0;
        uid_t uid = getuid();
        gid_t gid = getgid();

        if (unshare(CLONE_NEWUSER) == 0) {
            mask |= 1; /* Unprivileged user namespace permitted */

            int fd = open("/proc/self/setgroups", O_WRONLY);
            if (fd >= 0) {
                (void)write(fd, "deny", 4);
                close(fd);
            }

            char map[64];
            fd = open("/proc/self/uid_map", O_WRONLY);
            if (fd >= 0) {
                snprintf(map, sizeof(map), "0 %u 1\n", uid);
                if (write(fd, map, strlen(map)) > 0) {
                    /* UID map OK */
                }
                close(fd);
            }

            fd = open("/proc/self/gid_map", O_WRONLY);
            if (fd >= 0) {
                snprintf(map, sizeof(map), "0 %u 1\n", gid);
                if (write(fd, map, strlen(map)) > 0) {
                    /* GID map OK */
                }
                close(fd);
            }

            if (unshare(CLONE_NEWUTS) == 0) {
                mask |= 2; /* UTS namespace supported */
            }
            if (unshare(CLONE_NEWPID) == 0) {
                mask |= 4; /* PID namespace supported */

                /* Verify child process initialization and PID 1 */
                pid_t child_pid = fork();
                if (child_pid == 0) {
                    if (getpid() == 1) {
                        _exit(42);
                    }
                    _exit(1);
                } else if (child_pid > 0) {
                    int cstat = 0;
                    while (waitpid(child_pid, &cstat, 0) < 0 && errno == EINTR);
                    if (WIFEXITED(cstat) && WEXITSTATUS(cstat) == 42) {
                        mask |= 16; /* PID 1 verified */
                    }
                }
            }
            if (unshare(CLONE_NEWNS) == 0) {
                mask |= 8; /* Mount namespace supported */
            }
        }
        _exit(mask);
    }

    if (probe_pid > 0) {
        int status = 0;
        while (waitpid(probe_pid, &status, 0) < 0 && errno == EINTR);
        if (WIFEXITED(status)) {
            int mask = WEXITSTATUS(status);
            caps->unprivileged_user_ns = (mask & 1) != 0;
            caps->user_ns_supported = caps->unprivileged_user_ns;
            caps->uts_ns_supported = (mask & 2) != 0;
            caps->pid_ns_supported = (mask & 4) != 0;
            caps->mount_ns_supported = (mask & 8) != 0;
            caps->pid_ns_child_tested = (mask & 16) != 0;
        }
    }

    /* 3. Probe cgroup v2 kernel support */
    if (stat("/sys/fs/cgroup/cgroup.controllers", &st) == 0 ||
        stat("/sys/fs/cgroup/cgroup.procs", &st) == 0) {
        caps->cgroup_v2_kernel = true;
    }

    /* 4. Probe cgroup v2 delegated subtree and controller availability/writability (read-only) */
    char cg_cand[600] = {0};
    if (minibox_cgroup_find_candidate(cg_cand, sizeof(cg_cand)) == 0) {
        caps->cgroup_v2_delegated = true;

        /* Inspect controllers listed in delegated candidate base */
        char ctrl_path[700];
        snprintf(ctrl_path, sizeof(ctrl_path), "%s/cgroup.controllers", cg_cand);
        FILE *fc = fopen(ctrl_path, "r");
        if (fc) {
            char buf[256] = {0};
            if (fgets(buf, sizeof(buf), fc)) {
                caps->cgroup_cpu_controller = (strstr(buf, "cpu") != NULL);
                caps->cgroup_memory_controller = (strstr(buf, "memory") != NULL);
                caps->cgroup_pids_controller = (strstr(buf, "pids") != NULL);
            }
            fclose(fc);
        }

        /*
         * Since find_candidate verified that cg_cand/cgroup.subtree_control is writable,
         * all controllers available in cgroup.controllers are writable by the unprivileged user.
         */
        caps->cgroup_cpu_writable = caps->cgroup_cpu_controller;
        caps->cgroup_memory_writable = caps->cgroup_memory_controller;
        caps->cgroup_pids_writable = caps->cgroup_pids_controller;
    }

    caps->cgroups_v2_supported = caps->cgroup_v2_kernel && caps->cgroup_v2_delegated;

    if (caps->user_ns_supported && caps->uts_ns_supported && caps->pid_ns_child_tested) {
        snprintf(caps->reason, sizeof(caps->reason),
                 "Static capability probe: namespaces verified with PID 1; delegated cgroup candidate detected (dynamic enforcement verified at launch).");
    } else if (caps->user_ns_supported && caps->uts_ns_supported) {
        snprintf(caps->reason, sizeof(caps->reason),
                 "Static capability probe: namespaces (User, UTS) supported; PID/Mount partially available; delegated cgroup candidate detected.");
    } else {
        snprintf(caps->reason, sizeof(caps->reason),
                 "Static capability probe: unprivileged user namespaces are restricted or unsupported on this kernel.");
    }

    return 0;
}

int minibox_prepare_isolation(const minibox_isolation_config_t *iso, const char *workload_id) {
    if (!iso) {
        return 0;
    }

    if (!iso->enable_uts && !iso->enable_pid && !iso->enable_mount) {
        return 0;
    }

    /*
     * Step 1: Create a new user namespace to gain local unprivileged capabilities.
     * Record the real host UID/GID before unsharing so they can be mapped to root (0).
     */
    uid_t real_uid = getuid();
    gid_t real_gid = getgid();

    if (unshare(CLONE_NEWUSER) != 0) {
        return -1;
    }

    int fd = open("/proc/self/setgroups", O_WRONLY);
    if (fd >= 0) {
        (void)write(fd, "deny", 4);
        close(fd);
    }

    char map[64];
    fd = open("/proc/self/uid_map", O_WRONLY);
    if (fd >= 0) {
        snprintf(map, sizeof(map), "0 %u 1\n", real_uid);
        if (write(fd, map, strlen(map)) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    } else {
        return -1;
    }

    fd = open("/proc/self/gid_map", O_WRONLY);
    if (fd >= 0) {
        snprintf(map, sizeof(map), "0 %u 1\n", real_gid);
        if (write(fd, map, strlen(map)) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    } else {
        return -1;
    }

    /*
     * Step 2: Mount namespace setup (with private mount propagation).
     */
    if (iso->enable_mount || iso->enable_pid) {
        if (unshare(CLONE_NEWNS) != 0) {
            return -1;
        }
        if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
            return -1;
        }
    }

    /*
     * Step 3: UTS namespace for isolated hostname.
     */
    if (iso->enable_uts) {
        if (unshare(CLONE_NEWUTS) != 0) {
            return -1;
        }
        const char *h = iso->hostname[0] ? iso->hostname : workload_id;
        if (sethostname(h, strlen(h)) != 0) {
            return -1;
        }
    }

    /*
     * Step 4: PID namespace unshare.
     * Note: CLONE_NEWPID affects the *children* of the calling process.
     */
    if (iso->enable_pid) {
        if (unshare(CLONE_NEWPID) != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Syscalls a MiniBox workload must never make. None of these are usable by ordinary
 * user-space programs, so denying them costs nothing while removing the syscalls
 * most often reached for after an isolation escape or to tamper with the host.
 */
static const int g_denied_syscalls[] = {
    __NR_init_module,
    __NR_finit_module,
    __NR_delete_module,
    __NR_kexec_load,
    __NR_kexec_file_load,
    __NR_reboot,
    __NR_swapon,
    __NR_swapoff,
    __NR_syslog,
    __NR_ptrace,
    __NR_bpf,
    __NR_perf_event_open,
    __NR_process_vm_readv,
    __NR_process_vm_writev,
    __NR_keyctl,
    __NR_add_key,
    __NR_request_key,
    __NR_open_by_handle_at,
    __NR_mount,
    __NR_umount2,
    __NR_pivot_root,
    __NR_name_to_handle_at,
};

/* Capability bounding set retained for workloads (matches the common container default). */
static const int g_allowed_caps[] = {
    CAP_CHOWN,
    CAP_DAC_OVERRIDE,
    CAP_FOWNER,
    CAP_FSETID,
    CAP_KILL,
    CAP_SETGID,
    CAP_SETUID,
    CAP_SETPCAP,
    CAP_NET_BIND_SERVICE,
    CAP_NET_RAW,
    CAP_SYS_CHROOT,
    CAP_MKNOD,
    CAP_AUDIT_WRITE,
    CAP_SETFCAP,
};

static bool cap_is_allowed(int cap) {
    for (size_t i = 0; i < sizeof(g_allowed_caps) / sizeof(g_allowed_caps[0]); i++) {
        if (g_allowed_caps[i] == cap) return true;
    }
    return false;
}

/*
 * Install a seccomp-BPF filter that returns EPERM for the denied syscall set.
 * Must run after PR_SET_NO_NEW_PRIVS, which the kernel requires for an
 * unprivileged filter installation. Returns 0 on success, -1 with errno set otherwise.
 */
static int install_seccomp_filter(void) {
    const size_t denied = sizeof(g_denied_syscalls) / sizeof(g_denied_syscalls[0]);
    const size_t max_insns = denied * 2 + 16;
    struct sock_filter *insns = calloc(max_insns, sizeof(*insns));
    if (!insns) {
        errno = ENOMEM;
        return -1;
    }

    size_t n = 0;
#if MINIBOX_AUDIT_ARCH != 0
    /* Reject any ABI other than the one these syscall numbers were compiled for. */
    insns[n++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                             offsetof(struct seccomp_data, arch));
    insns[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                             MINIBOX_AUDIT_ARCH, 1, 0);
    insns[n++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif
#if MINIBOX_X32_SYSCALL_BIT != 0
    /* Close the x32 ABI escape hatch, which would otherwise re-number every syscall. */
    insns[n++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                             offsetof(struct seccomp_data, nr));
    insns[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K,
                                             MINIBOX_X32_SYSCALL_BIT, 0, 1);
    insns[n++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#else
    insns[n++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                             offsetof(struct seccomp_data, nr));
#endif

    const size_t deny_slot = n + denied + 1;   /* index of the shared deny return */
    for (size_t i = 0; i < denied; i++) {
        size_t at = n + i;
        insns[at] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                (unsigned int)g_denied_syscalls[i],
                                                (unsigned char)(deny_slot - at - 1), 0);
    }
    insns[n + denied] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    insns[deny_slot] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K,
                                                    SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));

    struct sock_fprog prog = {
        .len = (unsigned short)(deny_slot + 1),
        .filter = insns,
    };

    long rc = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0u, &prog);
    int saved = (rc < 0) ? errno : 0;
    free(insns);
    if (rc < 0) {
        errno = saved;
        return -1;
    }
    return 0;
}

/*
 * Apply runtime hardening in the workload process, immediately before execvp().
 *
 * MiniBox provides namespace isolation, not security isolation: the workload keeps
 * the invoking user's privileges. These steps narrow what a workload can do to the
 * host, and must run last, after the /proc mount and any capability-dependent setup.
 *
 * Returns 0 when hardening was applied, or -1 with errno set on a hard failure.
 * When seccomp is unavailable the earlier steps still stand and -1 is returned so the
 * caller can decide whether to continue.
 */
int minibox_apply_runtime_hardening(bool strict) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -1;
    }

    for (int cap = 0; cap <= CAP_LAST_CAP; cap++) {
        if (cap_is_allowed(cap)) continue;
        /* Dropping a capability we do not hold is not an error worth reporting. */
        (void)prctl(PR_CAPBSET_DROP, (unsigned long)cap, 0, 0, 0);
    }

    if (install_seccomp_filter() != 0) {
        if (strict) return -1;
    }
    return 0;
}
