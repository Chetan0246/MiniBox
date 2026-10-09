#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "resources.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char g_minibox_cgroup_base[600] = {0};

int minibox_parse_memory_size(const char *str, uint64_t *out_bytes) {
    if (!str || !out_bytes) return -1;

    /* Skip leading whitespace */
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '-' || *str == '+') return -1; /* Disallow signs */

    char *endptr = NULL;
    errno = 0;
    unsigned long long val = strtoull(str, &endptr, 10);
    if (errno != 0 || endptr == str || val == 0) {
        return -1;
    }

    /* Skip whitespace before unit */
    while (*endptr && isspace((unsigned char)*endptr)) {
        endptr++;
    }

    uint64_t multiplier = 1;
    if (*endptr == 0) {
        multiplier = 1; /* Raw bytes */
    } else if (strcasecmp(endptr, "k") == 0 || strcasecmp(endptr, "kb") == 0 || strcasecmp(endptr, "kib") == 0) {
        multiplier = 1024ULL;
    } else if (strcasecmp(endptr, "m") == 0 || strcasecmp(endptr, "mb") == 0 || strcasecmp(endptr, "mib") == 0) {
        multiplier = 1024ULL * 1024ULL;
    } else if (strcasecmp(endptr, "g") == 0 || strcasecmp(endptr, "gb") == 0 || strcasecmp(endptr, "gib") == 0) {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
    } else {
        return -1; /* Unknown unit or trailing characters */
    }

    /* Check for multiplication overflow */
    if (val > (UINT64_MAX / multiplier)) {
        return -1;
    }

    *out_bytes = (uint64_t)val * multiplier;
    return 0;
}

static bool check_subtree_control_available(const char *dir) {
    char ctrl_file[600];
    snprintf(ctrl_file, sizeof(ctrl_file), "%s/cgroup.subtree_control", dir);
    FILE *f = fopen(ctrl_file, "r");
    if (!f) return false;

    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;

    return (strstr(buf, "cpu") != NULL || strstr(buf, "memory") != NULL || strstr(buf, "pids") != NULL);
}

int minibox_cgroup_find_root(char *out_path, size_t max_len) {
    if (g_minibox_cgroup_base[0] != 0) {
        snprintf(out_path, max_len, "%s", g_minibox_cgroup_base);
        return 0;
    }

    char candidate[512] = {0};
    uid_t uid = getuid();

    /* 1. Prefer standard systemd user slice paths where controllers are delegated */
    char user_app_slice[512];
    snprintf(user_app_slice, sizeof(user_app_slice),
             "/sys/fs/cgroup/user.slice/user-%u.slice/user@%u.service/app.slice",
             (unsigned int)uid, (unsigned int)uid);
    char user_svc_slice[512];
    snprintf(user_svc_slice, sizeof(user_svc_slice),
             "/sys/fs/cgroup/user.slice/user-%u.slice/user@%u.service",
             (unsigned int)uid, (unsigned int)uid);

    if (access(user_app_slice, W_OK) == 0 && check_subtree_control_available(user_app_slice)) {
        snprintf(candidate, sizeof(candidate), "%s", user_app_slice);
    } else if (access(user_svc_slice, W_OK) == 0 && check_subtree_control_available(user_svc_slice)) {
        snprintf(candidate, sizeof(candidate), "%s", user_svc_slice);
    } else {
        /* 2. Walk up from /proc/self/cgroup */
        FILE *f = fopen("/proc/self/cgroup", "r");
        if (!f) {
            return -1;
        }

        char line[512] = {0};
        char rel_path[256] = {0};
        while (fgets(line, sizeof(line), f)) {
            char *colon = strrchr(line, ':');
            if (colon) {
                char *p = colon + 1;
                while (*p && (*p == ' ' || *p == '\t')) p++;
                size_t len = strlen(p);
                if (len > 0 && p[len - 1] == '\n') p[len - 1] = 0;
                snprintf(rel_path, sizeof(rel_path), "%s", p);
                break;
            }
        }
        fclose(f);

        if (rel_path[0] == 0) {
            return -1;
        }

        char current[512];
        snprintf(current, sizeof(current), "/sys/fs/cgroup%s", rel_path);

        bool found = false;
        while (strlen(current) > strlen("/sys/fs/cgroup")) {
            if (access(current, W_OK) == 0 && check_subtree_control_available(current)) {
                snprintf(candidate, sizeof(candidate), "%s", current);
                found = true;
                break;
            }
            char *last_slash = strrchr(current, '/');
            if (!last_slash || last_slash <= current + strlen("/sys/fs/cgroup")) {
                break;
            }
            *last_slash = 0;
        }

        if (!found) {
            /* Fall back to current directory */
            snprintf(candidate, sizeof(candidate), "/sys/fs/cgroup%s", rel_path);
        }
    }

    /* Form base minibox root: <candidate>/minibox */
    char base_dir[600];
    snprintf(base_dir, sizeof(base_dir), "%s/minibox", candidate);
    mkdir(base_dir, 0755);

    /* Enable controllers in base_dir/cgroup.subtree_control */
    char subtree_file[700];
    snprintf(subtree_file, sizeof(subtree_file), "%s/cgroup.subtree_control", base_dir);
    int fd = open(subtree_file, O_WRONLY);
    if (fd >= 0) {
        const char *ctrls = "+cpu +memory +pids";
        (void)write(fd, ctrls, strlen(ctrls));
        close(fd);
    }

    snprintf(g_minibox_cgroup_base, sizeof(g_minibox_cgroup_base), "%s", base_dir);
    snprintf(out_path, max_len, "%s", g_minibox_cgroup_base);
    return 0;
}

static int read_file_string(const char *path, char *out, size_t max_len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[128] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = 0;
        snprintf(out, max_len, "%s", buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    return -1;
}

int minibox_cgroup_setup_workload(const char *workload_id,
                                  const minibox_resource_limits_t *limits,
                                  char *out_cgroup_path,
                                  size_t max_len,
                                  minibox_resource_status_t *status) {
    if (!workload_id || !limits || !out_cgroup_path || !status) {
        errno = EINVAL;
        return -1;
    }
    memset(status, 0, sizeof(*status));

    char base_root[600];
    if (minibox_cgroup_find_root(base_root, sizeof(base_root)) != 0) {
        snprintf(status->error_msg, sizeof(status->error_msg),
                 "cgroup v2 root not found or unprivileged delegation unavailable");
        return -1;
    }

    char wl_cg[700];
    snprintf(wl_cg, sizeof(wl_cg), "%s/%s", base_root, workload_id);
    if (mkdir(wl_cg, 0755) != 0 && errno != EEXIST) {
        snprintf(status->error_msg, sizeof(status->error_msg),
                 "Failed to create workload cgroup: %s", strerror(errno));
        return -1;
    }
    status->cgroup_created = true;
    snprintf(out_cgroup_path, max_len, "%s", wl_cg);

    /* 1. Configure pids.max */
    if (limits->pids_max > 0) {
        char path[800];
        snprintf(path, sizeof(path), "%s/pids.max", wl_cg);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%d\n", limits->pids_max);
            fclose(f);
            if (read_file_string(path, status->pids_max_readback, sizeof(status->pids_max_readback)) == 0) {
                char *end_p = NULL;
                long read_pids = strtol(status->pids_max_readback, &end_p, 10);
                if (read_pids == limits->pids_max) {
                    status->pids_applied = true;
                }
            }
        }
        if (!status->pids_applied) {
            snprintf(status->error_msg, sizeof(status->error_msg),
                     "Failed to apply pids limit: readback ('%s') did not match requested %d",
                     status->pids_max_readback[0] ? status->pids_max_readback : strerror(errno), limits->pids_max);
            return -1;
        }
    }

    /* 2. Configure memory.max and disable swap bypass */
    if (limits->memory_max_bytes > 0) {
        char path[800];
        snprintf(path, sizeof(path), "%s/memory.max", wl_cg);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%lu\n", (unsigned long)limits->memory_max_bytes);
            fclose(f);
            if (read_file_string(path, status->memory_max_readback, sizeof(status->memory_max_readback)) == 0) {
                char *end_m = NULL;
                unsigned long long read_mem = strtoull(status->memory_max_readback, &end_m, 10);
                if (read_mem == limits->memory_max_bytes) {
                    status->memory_applied = true;
                }
            }
        }
        if (!status->memory_applied) {
            snprintf(status->error_msg, sizeof(status->error_msg),
                     "Failed to apply memory limit: readback ('%s') did not match requested %lu bytes",
                     status->memory_max_readback[0] ? status->memory_max_readback : strerror(errno), (unsigned long)limits->memory_max_bytes);
            return -1;
        }

        /* Prevent swap from bypassing container memory ceiling */
        char swap_path[800];
        snprintf(swap_path, sizeof(swap_path), "%s/memory.swap.max", wl_cg);
        FILE *f_swap = fopen(swap_path, "w");
        if (f_swap) {
            fprintf(f_swap, "0\n");
            fclose(f_swap);
        }
    }

    /* 3. Configure cpu.max (quota us / period us) */
    if (limits->cpu_percent > 0) {
        char path[800];
        snprintf(path, sizeof(path), "%s/cpu.max", wl_cg);
        uint64_t quota_us = ((uint64_t)MINIBOX_DEFAULT_CPU_PERIOD_US * (uint64_t)limits->cpu_percent) / 100ULL;
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%lu %d\n", (unsigned long)quota_us, MINIBOX_DEFAULT_CPU_PERIOD_US);
            fclose(f);
            if (read_file_string(path, status->cpu_max_readback, sizeof(status->cpu_max_readback)) == 0) {
                unsigned long read_quota = 0;
                unsigned long read_period = 0;
                if (sscanf(status->cpu_max_readback, "%lu %lu", &read_quota, &read_period) == 2) {
                    if (read_quota == quota_us && read_period == (unsigned long)MINIBOX_DEFAULT_CPU_PERIOD_US) {
                        status->cpu_applied = true;
                    }
                }
            }
        }
        if (!status->cpu_applied) {
            snprintf(status->error_msg, sizeof(status->error_msg),
                     "Failed to apply cpu limit: readback ('%s') did not match requested quota %lu/%d",
                     status->cpu_max_readback[0] ? status->cpu_max_readback : strerror(errno),
                     (unsigned long)quota_us, MINIBOX_DEFAULT_CPU_PERIOD_US);
            return -1;
        }
    }

    return 0;
}

int minibox_cgroup_attach_pid(const char *cgroup_path, pid_t pid) {
    if (!cgroup_path || pid <= 0) return -1;

    char path[800];
    snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "%d\n", pid);
    fclose(f);
    return 0;
}

int minibox_cgroup_cleanup(const char *cgroup_path) {
    if (!cgroup_path || cgroup_path[0] == 0) return 0;

    /* Signal kernel to terminate any lingering processes in this cgroup */
    char kill_path[800];
    snprintf(kill_path, sizeof(kill_path), "%s/cgroup.kill", cgroup_path);
    FILE *f_kill = fopen(kill_path, "w");
    if (f_kill) {
        fprintf(f_kill, "1\n");
        fclose(f_kill);
    }

    /* Wait briefly for processes to terminate and kernel accounting to drain */
    for (int i = 0; i < 15; i++) {
        if (rmdir(cgroup_path) == 0 || errno == ENOENT) {
            return 0;
        }
        usleep(20000); /* 20ms */
    }
    return (rmdir(cgroup_path) == 0 || errno == ENOENT) ? 0 : -1;
}

