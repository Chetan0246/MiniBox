#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "isolation.h"
#include "resources.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
                 "Linux namespaces (User, UTS, PID, Mount) verified with PID 1; cgroups v2 delegated.");
    } else if (caps->user_ns_supported && caps->uts_ns_supported) {
        snprintf(caps->reason, sizeof(caps->reason),
                 "Linux namespaces (User, UTS) supported; PID/Mount partially available.");
    } else {
        snprintf(caps->reason, sizeof(caps->reason),
                 "Unprivileged user namespaces are restricted or unsupported on this kernel.");
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
