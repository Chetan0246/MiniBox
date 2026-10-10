#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "monitor.h"
#include "workload.h"
#include "lifecycle.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t get_monotonic_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

typedef struct {
    unsigned long active_ticks;
    unsigned long total_ticks;
    uint64_t timestamp_usec;
    double cpu_percent;         /* Most recently computed host CPU utilization */
    bool have_percent;
} host_cpu_sample_t;

static host_cpu_sample_t g_last_host_cpu = {0};

/* Window used to obtain the first CPU reading, and to refresh a stale one. */
#define HOST_CPU_SAMPLE_USEC     50000ULL
/* Shortest delta window considered stable enough to report without re-sampling. */
#define HOST_CPU_MIN_WINDOW_USEC 20000ULL
/* Beyond this age the cached sample is refreshed with a fresh measurement window. */
#define HOST_CPU_MAX_WINDOW_USEC 10000000ULL

static int read_proc_stat_ticks(unsigned long *out_active, unsigned long *out_total) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    unsigned long u = 0, n = 0, s = 0, idle = 0, io = 0, irq = 0, sirq = 0, steal = 0;
    if (sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &u, &n, &s, &idle, &io, &irq, &sirq, &steal) < 4) {
        return -1;
    }
    *out_total = u + n + s + idle + io + irq + sirq + steal;
    *out_active = u + n + s + irq + sirq + steal;
    return 0;
}

int minibox_collect_host_metrics(minibox_host_metrics_t *out_host) {
    if (!out_host) {
        errno = EINVAL;
        return -1;
    }
    memset(out_host, 0, sizeof(*out_host));
    bool meminfo_ok = false;
    bool loadavg_ok = false;

    /* 1. Parse /proc/meminfo */
    FILE *f_mem = fopen("/proc/meminfo", "r");
    if (f_mem) {
        char line[256];
        uint64_t total_kb = 0, avail_kb = 0, free_kb = 0;
        while (fgets(line, sizeof(line), f_mem)) {
            if (sscanf(line, "MemTotal: %lu kB", &total_kb) == 1) continue;
            if (sscanf(line, "MemAvailable: %lu kB", &avail_kb) == 1) continue;
            if (sscanf(line, "MemFree: %lu kB", &free_kb) == 1) continue;
        }
        fclose(f_mem);

        out_host->mem_total_bytes = total_kb * 1024ULL;
        out_host->mem_avail_bytes = avail_kb ? (avail_kb * 1024ULL) : (free_kb * 1024ULL);
        if (out_host->mem_total_bytes >= out_host->mem_avail_bytes) {
            out_host->mem_used_bytes = out_host->mem_total_bytes - out_host->mem_avail_bytes;
        }
        meminfo_ok = (total_kb > 0);
    }

    /* 2. Parse /proc/loadavg */
    FILE *f_load = fopen("/proc/loadavg", "r");
    if (f_load) {
        double l1 = 0, l5 = 0, l15 = 0;
        if (fscanf(f_load, "%lf %lf %lf", &l1, &l5, &l15) >= 3) {
            out_host->load_1m = l1;
            out_host->load_5m = l5;
            out_host->load_15m = l15;
            loadavg_ok = true;
        }
        fclose(f_load);
    }

    /*
     * 3. Host CPU utilization from /proc/stat deltas.
     *
     * Re-sampling on every call costs a fixed sleep each time, which made one
     * `minibox metrics` invocation take ~50 ms per workload. The last measurement is
     * therefore cached: back-to-back calls in one process reuse it, and only a cold
     * start or a genuinely stale cache pays for a measurement window.
     */
    unsigned long active2 = 0, total2 = 0;
    if (read_proc_stat_ticks(&active2, &total2) == 0) {
        uint64_t now = get_monotonic_usec();
        bool have_cache = (g_last_host_cpu.total_ticks != 0);
        uint64_t dt_usec = 0;
        if (have_cache && now > g_last_host_cpu.timestamp_usec) {
            dt_usec = now - g_last_host_cpu.timestamp_usec;
        }

        bool take_fresh_window = false;
        if (!have_cache) {
            take_fresh_window = true;
        } else if (dt_usec >= HOST_CPU_MIN_WINDOW_USEC &&
                   dt_usec <= HOST_CPU_MAX_WINDOW_USEC &&
                   total2 > g_last_host_cpu.total_ticks) {
            /* Delta against the cached sample is wide enough to be meaningful. */
            unsigned long dt_total = total2 - g_last_host_cpu.total_ticks;
            unsigned long dt_active = (active2 >= g_last_host_cpu.active_ticks) ?
                                      (active2 - g_last_host_cpu.active_ticks) : 0;
            if (dt_total > 0) {
                g_last_host_cpu.cpu_percent = ((double)dt_active / (double)dt_total) * 100.0;
                g_last_host_cpu.have_percent = true;
            }
            g_last_host_cpu.active_ticks = active2;
            g_last_host_cpu.total_ticks = total2;
            g_last_host_cpu.timestamp_usec = now;
        } else if (dt_usec > HOST_CPU_MAX_WINDOW_USEC) {
            take_fresh_window = true;
        }
        /* Otherwise the cache is younger than the minimum window: reuse it as-is. */

        if (take_fresh_window) {
            usleep(HOST_CPU_SAMPLE_USEC);
            unsigned long active3 = 0, total3 = 0;
            if (read_proc_stat_ticks(&active3, &total3) == 0) {
                unsigned long base_active = have_cache ? g_last_host_cpu.active_ticks : 0;
                unsigned long base_total = have_cache ? g_last_host_cpu.total_ticks : 0;
                unsigned long dt_total = (total3 > base_total) ? (total3 - base_total) : 0;
                unsigned long dt_active = (active3 > base_active) ? (active3 - base_active) : 0;
                if (dt_total > 0) {
                    g_last_host_cpu.cpu_percent = ((double)dt_active / (double)dt_total) * 100.0;
                    g_last_host_cpu.have_percent = true;
                }
                g_last_host_cpu.active_ticks = active3;
                g_last_host_cpu.total_ticks = total3;
                g_last_host_cpu.timestamp_usec = get_monotonic_usec();
            }
        }
    }

    out_host->cpu_percent = g_last_host_cpu.cpu_percent;

    /* Only claim validity when something was actually measured. */
    out_host->valid = meminfo_ok || loadavg_ok || g_last_host_cpu.have_percent;
    return 0;
}

static int read_cgroup_uint64(const char *cgroup_path, const char *file, uint64_t *out_val) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", cgroup_path, file);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[64] = {0};
    bool got = false;
    if (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "max", 3) == 0) {
            *out_val = 0;
            got = true;
        } else {
            char *endptr = NULL;
            errno = 0;
            unsigned long long v = strtoull(buf, &endptr, 10);
            if (endptr != buf && errno == 0) {
                *out_val = (uint64_t)v;
                got = true;
            }
        }
    }
    fclose(f);
    return got ? 0 : -1;
}

static int read_cgroup_cpu_stat(const char *cgroup_path, minibox_cpu_metrics_t *cpu) {
    char path[800];
    snprintf(path, sizeof(path), "%s/cpu.stat", cgroup_path);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64] = {0};
        unsigned long long val = 0;
        if (sscanf(line, "%63s %llu", key, &val) == 2) {
            if (strcmp(key, "usage_usec") == 0) cpu->usage_usec = val;
            else if (strcmp(key, "user_usec") == 0) cpu->user_usec = val;
            else if (strcmp(key, "system_usec") == 0) cpu->system_usec = val;
            else if (strcmp(key, "nr_periods") == 0) cpu->nr_periods = val;
            else if (strcmp(key, "nr_throttled") == 0) cpu->nr_throttled = val;
            else if (strcmp(key, "throttled_usec") == 0) cpu->throttled_usec = val;
        }
    }
    fclose(f);
    cpu->valid = true;
    return 0;
}

static int read_proc_pid_stat(pid_t pid, uint64_t *out_user_usec, uint64_t *out_sys_usec, int *out_threads) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[1024];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Locate the closing parenthesis of the comm field to safely skip executable name */
    char *close_paren = strrchr(buf, ')');
    if (!close_paren) return -1;

    char state_ch = 0;
    int ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
    unsigned int flags = 0;
    unsigned long minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0;
    unsigned long utime = 0, stime = 0;
    long cutime = 0, cstime = 0, priority = 0, nice_val = 0, num_threads = 0;

    int matched = sscanf(close_paren + 1,
                         " %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld",
                         &state_ch, &ppid, &pgrp, &session, &tty_nr, &tpgid,
                         &flags, &minflt, &cminflt, &majflt, &cmajflt,
                         &utime, &stime, &cutime, &cstime, &priority, &nice_val,
                         &num_threads);

    if (matched < 13) return -1;

    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) ticks_per_sec = 100;

    if (out_user_usec) *out_user_usec = ((uint64_t)utime * 1000000ULL) / (uint64_t)ticks_per_sec;
    if (out_sys_usec) *out_sys_usec = ((uint64_t)stime * 1000000ULL) / (uint64_t)ticks_per_sec;
    if (out_threads) *out_threads = (int)num_threads;

    return 0;
}

static int read_proc_pid_status(pid_t pid, uint64_t *out_rss_bytes, uint64_t *out_peak_bytes, int *out_threads) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    unsigned long long rss_kb = 0, peak_kb = 0;
    int threads = 0;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %llu kB", &rss_kb) == 1) continue;
        if (sscanf(line, "VmHWM: %llu kB", &peak_kb) == 1) continue;
        if (sscanf(line, "Threads: %d", &threads) == 1) continue;
    }
    fclose(f);

    if (out_rss_bytes) *out_rss_bytes = rss_kb * 1024ULL;
    if (out_peak_bytes) *out_peak_bytes = peak_kb * 1024ULL;
    if (out_threads && threads > 0) *out_threads = threads;

    return 0;
}

int minibox_collect_workload_metrics_with_host(const char *workload_id,
                                               int sample_window_ms,
                                               const minibox_host_metrics_t *host,
                                               minibox_workload_metrics_t *out_metrics) {
    if (!workload_id || !out_metrics) {
        errno = EINVAL;
        return -1;
    }
    if (sample_window_ms < 0) sample_window_ms = 0;
    memset(out_metrics, 0, sizeof(*out_metrics));

    minibox_workload_t wl;
    if (minibox_workload_load(workload_id, &wl) != 0) {
        errno = ENOENT;
        return -1;
    }
    minibox_workload_refresh_state(&wl);

    snprintf(out_metrics->workload_id, sizeof(out_metrics->workload_id), "%s", wl.id);
    snprintf(out_metrics->name, sizeof(out_metrics->name), "%s", wl.name);
    out_metrics->pid = wl.pid;
    out_metrics->supervisor_pid = wl.supervisor_pid;
    out_metrics->state = wl.state;
    out_metrics->exit_code = wl.exit_code;
    out_metrics->term_signal = wl.term_signal;

    uint64_t now_ms = minibox_current_time_ms();
    if (wl.started_at_ms > 0) {
        if (minibox_state_is_terminal(wl.state)) {
            out_metrics->elapsed_ms = (wl.finished_at_ms >= wl.started_at_ms) ?
                                      (wl.finished_at_ms - wl.started_at_ms) : 0;
        } else {
            out_metrics->elapsed_ms = (now_ms >= wl.started_at_ms) ? (now_ms - wl.started_at_ms) : 0;
        }
    }

    /* Check if process is actively running (zombies and recycled PIDs do not count) */
    bool is_live = false;
    if (!minibox_state_is_terminal(wl.state) && wl.pid > 1) {
        is_live = minibox_pid_is_live(wl.pid);
    }
    out_metrics->active = is_live;

    /* Attach the host snapshot. Reuse the caller's when supplied so that listing
     * many workloads does not pay for a fresh host CPU sample each time. */
    if (host) {
        out_metrics->host = *host;
    } else {
        minibox_collect_host_metrics(&out_metrics->host);
    }

    /* If workload is not live, dynamic metrics are unavailable */
    if (!is_live) {
        return 0;
    }

    /* 1. Memory metrics */
    uint64_t cg_mem = 0, cg_peak = 0;
    bool has_cg_mem = false;
    if (wl.cgroup_active && wl.cgroup_path[0]) {
        if (read_cgroup_uint64(wl.cgroup_path, "memory.current", &cg_mem) == 0) {
            has_cg_mem = true;
            (void)read_cgroup_uint64(wl.cgroup_path, "memory.peak", &cg_peak);
        }
    }

    uint64_t proc_rss = 0, proc_peak = 0;
    int proc_threads = 0;
    read_proc_pid_status(wl.pid, &proc_rss, &proc_peak, &proc_threads);

    out_metrics->memory.valid = true;
    out_metrics->memory.current_bytes = has_cg_mem ? cg_mem : proc_rss;
    out_metrics->memory.peak_bytes = cg_peak ? cg_peak : proc_peak;
    out_metrics->memory.rss_bytes = proc_rss;
    out_metrics->memory.limit_bytes = wl.limits.memory_max_bytes;

    /* 2. Process count metrics */
    uint64_t cg_pids = 0;
    bool has_cg_pids = false;
    if (wl.cgroup_active && wl.cgroup_path[0]) {
        if (read_cgroup_uint64(wl.cgroup_path, "pids.current", &cg_pids) == 0) {
            has_cg_pids = true;
        }
    }

    out_metrics->pids.valid = true;
    out_metrics->pids.current_pids = has_cg_pids ? (int)cg_pids : (proc_threads ? proc_threads : 1);
    out_metrics->pids.max_pids = wl.limits.pids_max;

    /* 3. CPU metrics */
    bool has_cg_cpu = false;
    if (wl.cgroup_active && wl.cgroup_path[0]) {
        if (read_cgroup_cpu_stat(wl.cgroup_path, &out_metrics->cpu) == 0) {
            has_cg_cpu = true;
        }
    }

    if (!has_cg_cpu) {
        uint64_t u_usec = 0, s_usec = 0;
        int th = 0;
        if (read_proc_pid_stat(wl.pid, &u_usec, &s_usec, &th) == 0) {
            out_metrics->cpu.user_usec = u_usec;
            out_metrics->cpu.system_usec = s_usec;
            out_metrics->cpu.usage_usec = u_usec + s_usec;
            out_metrics->cpu.valid = true;
        }
    }

    /* Compute CPU % */
    if (out_metrics->cpu.valid) {
        if (sample_window_ms > 0) {
            /* Instantaneous rate sampling */
            uint64_t t1 = get_monotonic_usec();
            uint64_t cpu1 = out_metrics->cpu.usage_usec;

            usleep((useconds_t)sample_window_ms * 1000u);

            minibox_cpu_metrics_t cpu_sample2 = {0};
            if (has_cg_cpu) {
                read_cgroup_cpu_stat(wl.cgroup_path, &cpu_sample2);
            } else {
                uint64_t u2 = 0, s2 = 0;
                int th2 = 0;
                read_proc_pid_stat(wl.pid, &u2, &s2, &th2);
                cpu_sample2.usage_usec = u2 + s2;
                cpu_sample2.user_usec = u2;
                cpu_sample2.system_usec = s2;
                cpu_sample2.valid = true;
            }

            uint64_t t2 = get_monotonic_usec();
            uint64_t cpu2 = cpu_sample2.usage_usec;

            if (t2 > t1 && cpu2 >= cpu1) {
                uint64_t dt = t2 - t1;
                uint64_t dcpu = cpu2 - cpu1;
                out_metrics->cpu.usage_percent = ((double)dcpu / (double)dt) * 100.0;
            }
            /* Update to latest absolute totals */
            out_metrics->cpu.usage_usec = cpu2;
            out_metrics->cpu.user_usec = cpu_sample2.user_usec;
            out_metrics->cpu.system_usec = cpu_sample2.system_usec;
            out_metrics->cpu.nr_periods = cpu_sample2.nr_periods;
            out_metrics->cpu.nr_throttled = cpu_sample2.nr_throttled;
            out_metrics->cpu.throttled_usec = cpu_sample2.throttled_usec;
        } else {
            /* Lifetime average CPU rate */
            uint64_t elapsed_us = out_metrics->elapsed_ms * 1000ULL;
            if (elapsed_us > 0) {
                out_metrics->cpu.usage_percent = ((double)out_metrics->cpu.usage_usec / (double)elapsed_us) * 100.0;
            }
        }
    }

    return 0;
}

int minibox_collect_workload_metrics(const char *workload_id,
                                     int sample_window_ms,
                                     minibox_workload_metrics_t *out_metrics) {
    return minibox_collect_workload_metrics_with_host(workload_id, sample_window_ms, NULL, out_metrics);
}
