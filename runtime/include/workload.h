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
} minibox_workload_t;

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
