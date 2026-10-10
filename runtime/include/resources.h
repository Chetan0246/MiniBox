#ifndef MINIBOX_RESOURCES_H
#define MINIBOX_RESOURCES_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define MINIBOX_DEFAULT_CPU_PERIOD_US 100000 /* 100 ms period */

/**
 * Resource limits configured for a workload.
 */
typedef struct {
    int cpu_percent;            /* 1 to 100+ percent of 1 CPU (e.g. 30 -> 30,000 / 100,000 us). 0 = unlimited */
    uint64_t memory_max_bytes;  /* Max memory in bytes. 0 = unlimited */
    int pids_max;               /* Max processes. 0 = unlimited */
} minibox_resource_limits_t;

/**
 * Status of resource limit verification against kernel cgroup v2.
 */
typedef struct {
    bool cgroup_created;
    bool cpu_applied;
    bool memory_applied;
    bool pids_applied;
    char cpu_max_readback[64];
    char memory_max_readback[64];
    char pids_max_readback[64];
    char error_msg[256];
} minibox_resource_status_t;

/**
 * Parse human memory size string (e.g. "256M", "1G", "64K", or raw bytes) into bytes.
 */
int minibox_parse_memory_size(const char *str, uint64_t *out_bytes);

/**
 * Probe writable delegated cgroup v2 candidate path without side-effects.
 */
int minibox_cgroup_find_candidate(char *out_path, size_t max_len);

/**
 * Detect writable delegated cgroup v2 root path, creating minibox base directory.
 */
int minibox_cgroup_find_root(char *out_path, size_t max_len);

/**
 * Create a dedicated cgroup v2 for a workload and apply configured resource limits.
 * Reads back configured values from kernel to verify successful application.
 */
int minibox_cgroup_setup_workload(const char *workload_id,
                                  const minibox_resource_limits_t *limits,
                                  char *out_cgroup_path,
                                  size_t max_len,
                                  minibox_resource_status_t *status);

/**
 * Attach a process (PID) to a workload's cgroup v2.
 */
int minibox_cgroup_attach_pid(const char *cgroup_path, pid_t pid);

/**
 * Clean up and remove a workload's cgroup v2 directory.
 */
int minibox_cgroup_cleanup(const char *cgroup_path);

#endif /* MINIBOX_RESOURCES_H */
