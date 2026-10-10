#ifndef MINIBOX_ISOLATION_H
#define MINIBOX_ISOLATION_H

#include <stdbool.h>

/**
 * Detected Linux namespace and cgroup capabilities.
 */
typedef struct {
    bool user_ns_supported;
    bool uts_ns_supported;
    bool pid_ns_supported;
    bool mount_ns_supported;
    bool cgroups_v2_supported;
    /* Granular capability inspection */
    bool kernel_user_ns;
    bool unprivileged_user_ns;
    bool pid_ns_child_tested;
    bool cgroup_v2_kernel;
    bool cgroup_v2_delegated;
    bool cgroup_cpu_controller;
    bool cgroup_cpu_writable;
    bool cgroup_memory_controller;
    bool cgroup_memory_writable;
    bool cgroup_pids_controller;
    bool cgroup_pids_writable;
    char reason[256];
} minibox_capabilities_t;

/**
 * Configuration for workload isolation.
 */
typedef struct {
    bool enable_uts;
    char hostname[64];
    bool enable_pid;
    bool enable_mount;
} minibox_isolation_config_t;

/**
 * Detect available Linux isolation features.
 */
int minibox_detect_capabilities(minibox_capabilities_t *caps);

/**
 * Prepare and configure isolation in the child process before execvp.
 * Sets up user, mount, UTS, and PID namespaces as requested.
 * Returns 0 on success, or -1 with errno set on failure.
 */
int minibox_prepare_isolation(const minibox_isolation_config_t *iso, const char *workload_id);

/**
 * Apply runtime hardening to the current (post-fork, pre-exec) process:
 * PR_SET_NO_NEW_PRIVS, a reduced capability bounding set, and a seccomp-BPF filter
 * denying kernel-module, tracing, reboot, mount and handle-resolution syscalls.
 *
 * Must be called after all namespace and /proc setup is finished. When `strict` is
 * true a seccomp installation failure is reported as an error; otherwise the earlier
 * steps still apply and the function returns 0.
 */
int minibox_apply_runtime_hardening(bool strict);

#endif /* MINIBOX_ISOLATION_H */
