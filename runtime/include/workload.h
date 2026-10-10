#ifndef MINIBOX_WORKLOAD_H
#define MINIBOX_WORKLOAD_H

#include "lifecycle.h"
#include "resources.h"
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MINIBOX_MAX_ID_LEN 64
#define MINIBOX_MAX_NAME_LEN 64
#define MINIBOX_MAX_PATH_LEN 1024
#define MINIBOX_MAX_ARGS_LEN 2048
#define MINIBOX_MAX_ERR_LEN 256

/* Longest path built from a base directory plus a workload id and a fixed suffix. */
#define MINIBOX_FULL_PATH_MAX (MINIBOX_MAX_PATH_LEN + MINIBOX_MAX_ID_LEN + 32)

/* Capacity of the runtime state directory path, including the NUL terminator. */
#define MINIBOX_STATE_DIR_MAX 512

/* "<state dir>/<workload id>" - the directory that holds a workload's files. */
#define MINIBOX_WL_DIR_MAX (MINIBOX_STATE_DIR_MAX + MINIBOX_MAX_ID_LEN + 2)

/**
 * Record representing a managed workload.
 */
typedef struct {
    char id[MINIBOX_MAX_ID_LEN];
    char name[MINIBOX_MAX_NAME_LEN];
    minibox_state_t state;
    pid_t pid;
    pid_t supervisor_pid;
    char executable[MINIBOX_MAX_PATH_LEN];
    char args[MINIBOX_MAX_ARGS_LEN];
    char work_dir[MINIBOX_MAX_PATH_LEN];
    uint64_t created_at_ms;
    uint64_t started_at_ms;
    uint64_t finished_at_ms;
    int exit_code;
    int term_signal;
    int exec_errno;
    char error_msg[MINIBOX_MAX_ERR_LEN];
    char log_file[MINIBOX_MAX_PATH_LEN];
    bool iso_uts;
    char iso_hostname[64];
    bool iso_pid;
    bool iso_mount;
    minibox_resource_limits_t limits;
    char cgroup_path[MINIBOX_MAX_PATH_LEN];
    bool cgroup_active;
    bool oom_killed;
    uint64_t pid_start_time;   /* Kernel start time of `pid`, guards against PID reuse */
} minibox_workload_t;

/**
 * Returns the kernel process start time (jiffies since boot) for a PID, or 0 if
 * the process does not exist. Used to detect PID reuse before signalling.
 */
uint64_t minibox_pid_start_time(pid_t pid);

/**
 * Returns true only if the PID refers to a live, non-zombie process.
 */
bool minibox_pid_is_live(pid_t pid);

/**
 * Returns true once the PID no longer exists in the kernel at all (i.e. it has been
 * reaped by its parent). Unlike minibox_pid_is_live() this stays false while the
 * process is still a zombie, so callers cannot outrun a parent that is about to
 * publish the child's real exit status.
 */
bool minibox_pid_is_gone(pid_t pid);

/**
 * Returns true if the PID is still the exact process MiniBox launched.
 * A recorded start time of 0 falls back to a liveness-only check.
 */
bool minibox_pid_matches(pid_t pid, uint64_t recorded_start_time);

/**
 * Returns current real-time clock timestamp in milliseconds.
 */
uint64_t minibox_current_time_ms(void);

/**
 * Returns the base runtime storage directory (e.g. /tmp/minibox-<uid>/workloads).
 * Creates directory hierarchy if not already present.
 */
const char *minibox_get_state_dir(void);

/**
 * Copy the base runtime storage directory into a caller-supplied buffer.
 * Using a fixed-size buffer keeps derived path lengths statically provable.
 * Returns 0 on success, or -1 with errno set if the path does not fit.
 */
int minibox_get_state_dir_buf(char *out, size_t out_size);

/**
 * Generate a unique workload identifier.
 */
void minibox_generate_workload_id(char *out_id, size_t max_len);

/**
 * Initialize a new workload record with CREATED state and unique ID.
 */
int minibox_workload_init(minibox_workload_t *wl,
                          const char *requested_id,
                          const char *name,
                          const char *executable,
                          char *const *argv,
                          const char *work_dir);

/**
 * Save workload record as state.json inside its dedicated directory.
 */
int minibox_workload_save(const minibox_workload_t *wl);

/**
 * Load workload record by ID from its directory.
 */
int minibox_workload_load(const char *id, minibox_workload_t *wl);

/**
 * Refresh workload state by checking state.json and OS process liveness.
 */
int minibox_workload_refresh_state(minibox_workload_t *wl);

/**
 * List all existing workloads.
 * Allocates an array of minibox_workload_t; caller must free using minibox_workload_free_list().
 */
int minibox_workload_list(minibox_workload_t **out_workloads, size_t *out_count);

/**
 * Free memory allocated by minibox_workload_list.
 */
void minibox_workload_free_list(minibox_workload_t *workloads);

/**
 * Delete a workload record and its logs.
 * If workload is still running, fails unless force is true.
 */
int minibox_workload_delete(const char *id, bool force);

/**
 * Remove all workloads in terminal states (COMPLETED, TERMINATED, FAILED).
 */
int minibox_workload_cleanup_completed(size_t *removed_count);

#endif /* MINIBOX_WORKLOAD_H */
