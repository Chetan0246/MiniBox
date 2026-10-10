#ifndef MINIBOX_MONITOR_H
#define MINIBOX_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "lifecycle.h"

/**
 * CPU metrics collected from cgroup v2 cpu.stat and /proc/<pid>/stat.
 */
typedef struct {
    bool valid;
    uint64_t usage_usec;        /* Total CPU time consumed in microseconds */
    uint64_t user_usec;         /* User-space CPU time in microseconds */
    uint64_t system_usec;       /* Kernel-space CPU time in microseconds */
    double usage_percent;       /* Computed CPU utilization percentage (0.0 to 100.0+) */
    uint64_t nr_periods;        /* CFS periods elapsed */
    uint64_t nr_throttled;      /* CFS periods throttled */
    uint64_t throttled_usec;    /* Microseconds throttled by CPU quota */
} minibox_cpu_metrics_t;

/**
 * Memory metrics collected from cgroup v2 memory.current / memory.peak and /proc/<pid>/status.
 */
typedef struct {
    bool valid;
    uint64_t current_bytes;     /* Current resident memory in bytes */
    uint64_t peak_bytes;        /* Peak memory in bytes */
    uint64_t limit_bytes;       /* Configured memory ceiling (0 if unlimited) */
    uint64_t rss_bytes;         /* Resident Set Size from /proc/<pid>/status */
} minibox_memory_metrics_t;

/**
 * Process count metrics collected from cgroup v2 pids.current and /proc.
 */
typedef struct {
    bool valid;
    int current_pids;           /* Current number of active processes/threads in cgroup */
    int max_pids;               /* Configured pids.max (0 if unlimited) */
} minibox_pids_metrics_t;

/**
 * Host-wide resource metrics collected from /proc/meminfo, /proc/stat, /proc/loadavg.
 */
typedef struct {
    bool valid;
    double cpu_percent;         /* Host CPU utilization percentage */
    uint64_t mem_total_bytes;   /* Host total physical memory */
    uint64_t mem_avail_bytes;   /* Host available physical memory */
    uint64_t mem_used_bytes;    /* Host used physical memory */
    double load_1m;             /* 1-minute load average */
    double load_5m;             /* 5-minute load average */
    double load_15m;            /* 15-minute load average */
} minibox_host_metrics_t;

/**
 * Comprehensive workload monitoring snapshot.
 */
typedef struct {
    char workload_id[64];
    char name[64];
    pid_t pid;
    pid_t supervisor_pid;
    minibox_state_t state;
    bool active;                /* True if process is currently running in kernel */
    uint64_t elapsed_ms;        /* Elapsed runtime in milliseconds */
    int exit_code;
    int term_signal;
    minibox_cpu_metrics_t cpu;
    minibox_memory_metrics_t memory;
    minibox_pids_metrics_t pids;
    minibox_host_metrics_t host;
} minibox_workload_metrics_t;

/**
 * Collect live host-wide metrics from /proc/meminfo, /proc/stat, and /proc/loadavg.
 */
int minibox_collect_host_metrics(minibox_host_metrics_t *out_host);

/**
 * Collect metrics for a specific workload.
 * If sample_window_ms > 0, samples CPU over the specified window to compute instantaneous CPU %.
 * If sample_window_ms <= 0, computes lifetime average CPU %.
 */
int minibox_collect_workload_metrics(const char *workload_id,
                                     int sample_window_ms,
                                     minibox_workload_metrics_t *out_metrics);

/**
 * As minibox_collect_workload_metrics(), but reuses a caller-supplied host snapshot
 * when `host` is non-NULL. Listing many workloads with one host sample avoids
 * re-measuring (and re-sleeping on) host CPU utilization once per workload.
 */
int minibox_collect_workload_metrics_with_host(const char *workload_id,
                                               int sample_window_ms,
                                               const minibox_host_metrics_t *host,
                                               minibox_workload_metrics_t *out_metrics);

/**
 * Largest CPU sampling window accepted by the metrics commands, in milliseconds.
 * Larger values are rejected so a sampling window can never turn into an
 * effectively unbounded (or overflowing) sleep.
 */
#define MINIBOX_MAX_SAMPLE_MS 5000

#endif /* MINIBOX_MONITOR_H */
