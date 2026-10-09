#ifndef MINIBOX_PROCESS_H
#define MINIBOX_PROCESS_H

#include <sys/types.h>
#include <stdbool.h>

/**
 * Configuration for launching a process.
 */
typedef struct {
    const char *executable;                 /* Path or command name to execute */
    char *const *argv;                      /* NULL-terminated argument vector */
    const char *work_dir;                   /* Optional working directory (NULL for current) */
    bool redirect_child_stdout_to_stderr;   /* Redirect child stdout to stderr to isolate runtime protocol */
} minibox_process_config_t;

/**
 * Status of the launch attempt.
 */
typedef enum {
    MINIBOX_STATUS_UNKNOWN = 0,
    MINIBOX_STATUS_SUCCESS,         /* Program was launched successfully and completed */
    MINIBOX_STATUS_EXEC_FAILED,     /* execvp failed (e.g. file not found, permission denied) */
    MINIBOX_STATUS_FORK_FAILED,     /* fork syscall failed */
    MINIBOX_STATUS_WAIT_FAILED      /* waitpid syscall failed */
} minibox_launch_status_t;

/**
 * Result of the process execution.
 */
typedef struct {
    pid_t pid;                      /* Child process ID */
    minibox_launch_status_t status; /* Launch outcome */
    int exec_errno;                 /* errno if execvp failed */
    bool exited_normally;           /* True if process terminated via exit() or return */
    int exit_code;                  /* Exit status code if exited_normally is true */
    bool signaled;                  /* True if process was terminated by an unhandled signal */
    int term_signal;                /* Signal number if signaled is true */
} minibox_process_result_t;

#include "workload.h"
#include "isolation.h"

/**
 * Launch an executable using fork() and execvp(), synchronize with waitpid(),
 * and reliably detect whether execution actually succeeded or failed using
 * an error notification pipe with O_CLOEXEC.
 *
 * @param config Configuration specifying executable and arguments.
 * @param result Pointer to result structure to populate.
 * @return 0 on successful process execution (even if process returned non-zero),
 *         -1 if process could not be launched or waitpid failed.
 */
int minibox_run_process(const minibox_process_config_t *config, minibox_process_result_t *result);

/**
 * Run a workload synchronously in the foreground while tracking its lifecycle
 * states (CREATED -> STARTING -> RUNNING -> COMPLETED/TERMINATED/FAILED),
 * applying requested isolation, trapping signals, and recording state.
 */
int minibox_run_workload_foreground(minibox_workload_t *wl,
                                    char *const *argv,
                                    bool redirect_child_stdout_to_stderr,
                                    const minibox_isolation_config_t *iso);

/**
 * Start a workload asynchronously in the background. A lightweight supervisor
 * process supervises the child, applies requested isolation, redirects output
 * to the workload log file, waits for termination, and updates state.json when completed.
 */
int minibox_start_workload_background(minibox_workload_t *wl,
                                      char *const *argv,
                                      const minibox_isolation_config_t *iso);

/**
 * Controlled termination of a workload.
 * Sends signal (default SIGTERM) to the tracked process, updates state to STOPPING,
 * and escalates to SIGKILL if force is true and process does not exit in time.
 */
int minibox_stop_workload(const char *id, int signal_num, bool force);

#endif /* MINIBOX_PROCESS_H */
