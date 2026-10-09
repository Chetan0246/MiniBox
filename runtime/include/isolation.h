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

#endif /* MINIBOX_ISOLATION_H */
