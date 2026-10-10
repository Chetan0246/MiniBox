#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include "workload.h"
#include "lifecycle.h"
#include "isolation.h"
#include "resources.h"

int minibox_run_process(const minibox_process_config_t *config, minibox_process_result_t *result) {
    if (!config || !config->executable || !result) {
        errno = EINVAL;
        return -1;
    }

    memset(result, 0, sizeof(*result));
    result->status = MINIBOX_STATUS_UNKNOWN;

    /*
     * We create a pipe with O_CLOEXEC to synchronize and pass exec failures
     * from the child to the parent. If execvp succeeds, the descriptor is
     * closed automatically by the kernel on exec, yielding EOF (0 bytes) to
     * the parent read. If execvp fails, the child writes its errno to the pipe.
     */
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        result->status = MINIBOX_STATUS_FORK_FAILED;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        int saved_errno = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        result->status = MINIBOX_STATUS_FORK_FAILED;
        errno = saved_errno;
        return -1;
    }

    if (pid == 0) {
        /*
         * Child process execution.
         * Close the read end of the error pipe.
         */
        close(pipefd[0]);

        /* Optionally switch working directory if requested */
        if (config->work_dir != NULL && strlen(config->work_dir) > 0) {
            if (chdir(config->work_dir) != 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
        }

        /*
         * Separate runtime protocol stdout from child stdout if requested.
         * Redirect child stdout to stderr so protocol channel on stdout remains uncorrupted.
         */
        if (config->redirect_child_stdout_to_stderr) {
            if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
        }

        /*
         * Execute target program. argv must be NULL-terminated.
         * If config->argv is NULL, construct a 1-element argv with just the executable.
         */
        char *const default_argv[] = {(char *)(uintptr_t)config->executable, NULL};
        char *const *exec_argv = config->argv ? config->argv : default_argv;

        execvp(config->executable, exec_argv);

        /*
         * If execvp returns, an error occurred (e.g. ENOENT, EACCES).
         * Report the errno to the parent via the pipe and exit immediately.
         */
        int err = errno;
        (void)write(pipefd[1], &err, sizeof(err));
        close(pipefd[1]);
        _exit(127);
    }

    /*
     * Parent process execution.
     * Close the write end of the error pipe so EOF can be received.
     */
    result->pid = pid;
    close(pipefd[1]);

    int child_errno = 0;
    size_t bytes_read = 0;

    while (bytes_read < sizeof(child_errno)) {
        ssize_t n = read(pipefd[0], ((char *)&child_errno) + bytes_read, sizeof(child_errno) - bytes_read);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            /* EOF: execvp succeeded and closed pipefd[1] via O_CLOEXEC */
            break;
        }
        bytes_read += (size_t)n;
    }
    close(pipefd[0]);

    if (bytes_read == sizeof(child_errno)) {
        /* Exec failed in child */
        result->status = MINIBOX_STATUS_EXEC_FAILED;
        result->exec_errno = child_errno;

        /* Reap the child immediately to ensure no zombie is left */
        int wait_status = 0;
        while (waitpid(pid, &wait_status, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (WIFEXITED(wait_status)) {
            result->exited_normally = true;
            result->exit_code = WEXITSTATUS(wait_status);
        }
        return -1;
    }

    /*
     * Exec succeeded in child. Wait for process completion using waitpid().
     */
    int status = 0;
    while (1) {
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            result->status = MINIBOX_STATUS_WAIT_FAILED;
            return -1;
        }
        break;
    }

    result->status = MINIBOX_STATUS_SUCCESS;
    if (WIFEXITED(status)) {
        result->exited_normally = true;
        result->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result->signaled = true;
        result->term_signal = WTERMSIG(status);
    }

    return 0;
}

static volatile sig_atomic_t g_foreground_child_pid = 0;
static volatile sig_atomic_t g_pid1_child = 0;

static void forward_signal_to_pid1(int sig) {
    if (g_pid1_child > 0) {
        kill(g_pid1_child, sig);
    }
}

static void sigint_handler(int sig) {
    (void)sig;
    if (g_foreground_child_pid > 0) {
        kill(g_foreground_child_pid, SIGTERM);
    }
}

int minibox_run_workload_foreground(minibox_workload_t *wl,
                                    char *const *argv,
                                    bool redirect_child_stdout_to_stderr,
                                    const minibox_isolation_config_t *iso) {
    if (!wl || !wl->executable[0]) {
        errno = EINVAL;
        return -1;
    }

    if (iso) {
        wl->iso_uts = iso->enable_uts;
        snprintf(wl->iso_hostname, sizeof(wl->iso_hostname), "%s",
                 iso->hostname[0] ? iso->hostname : wl->id);
        wl->iso_pid = iso->enable_pid;
        wl->iso_mount = iso->enable_mount;
    }

    minibox_resource_status_t res_status;
    bool has_limits = (wl->limits.cpu_percent > 0 || wl->limits.memory_max_bytes > 0 || wl->limits.pids_max > 0);
    if (has_limits) {
        if (minibox_cgroup_setup_workload(wl->id, &wl->limits, wl->cgroup_path, sizeof(wl->cgroup_path), &res_status) == 0) {
            wl->cgroup_active = true;
        } else {
            wl->state = MINIBOX_STATE_FAILED;
            snprintf(wl->error_msg, sizeof(wl->error_msg), "cgroup limit setup failed: %.200s",
                     res_status.error_msg[0] ? res_status.error_msg : "unknown error");
            wl->finished_at_ms = minibox_current_time_ms();
            minibox_workload_save(wl);
            return -1;
        }
    }

    wl->state = MINIBOX_STATE_STARTING;
    minibox_workload_save(wl);

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = errno;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "%s", strerror(errno));
        wl->finished_at_ms = minibox_current_time_ms();
        minibox_workload_save(wl);
        return -1;
    }

    int cg_sync_pipe[2] = {-1, -1};
    if (wl->cgroup_active) {
        if (pipe2(cg_sync_pipe, O_CLOEXEC) < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            minibox_cgroup_cleanup(wl->cgroup_path);
            wl->state = MINIBOX_STATE_FAILED;
            wl->exec_errno = errno;
            snprintf(wl->error_msg, sizeof(wl->error_msg), "pipe error: %s", strerror(errno));
            wl->finished_at_ms = minibox_current_time_ms();
            minibox_workload_save(wl);
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        int saved_errno = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        if (cg_sync_pipe[0] >= 0) {
            close(cg_sync_pipe[0]);
            close(cg_sync_pipe[1]);
        }
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = saved_errno;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "%s", strerror(saved_errno));
        wl->finished_at_ms = minibox_current_time_ms();
        minibox_workload_save(wl);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (cg_sync_pipe[1] >= 0) close(cg_sync_pipe[1]);

        if (iso) {
            if (minibox_prepare_isolation(iso, wl->id) != 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                if (cg_sync_pipe[0] >= 0) close(cg_sync_pipe[0]);
                _exit(127);
            }
        }

        /*
         * Synchronize with parent launcher so this process is attached to the cgroup
         * BEFORE creating child processes in the new PID namespace.
         * In Linux, cgroup membership is inherited across fork(), ensuring that
         * the new PID 1 process operates under the requested resource limits!
         */
        if (cg_sync_pipe[0] >= 0) {
            char sync_byte = 0;
            ssize_t sn = read(cg_sync_pipe[0], &sync_byte, 1);
            close(cg_sync_pipe[0]);
            cg_sync_pipe[0] = -1;
            if (sn != 1 || sync_byte != '1') {
                int err = (sn < 0) ? errno : ((sync_byte == '0') ? EACCES : EIO);
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
        }

        if (iso && iso->enable_pid) {
            pid_t pid1 = fork();
            if (pid1 < 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
            if (pid1 > 0) {
                g_pid1_child = pid1;
                struct sigaction sa;
                memset(&sa, 0, sizeof(sa));
                sa.sa_handler = forward_signal_to_pid1;
                sigaction(SIGTERM, &sa, NULL);
                sigaction(SIGINT, &sa, NULL);
                sigaction(SIGHUP, &sa, NULL);

                int init_status = 0;
                while (waitpid(pid1, &init_status, 0) < 0 && errno == EINTR);
                g_pid1_child = 0;
                if (WIFEXITED(init_status)) _exit(WEXITSTATUS(init_status));
                if (WIFSIGNALED(init_status)) _exit(128 + WTERMSIG(init_status));
                _exit(0);
            }

            /* In child: this process is PID 1 in the new PID namespace.
             * Mount new /proc filesystem as a mandatory step for PID isolation. */
            if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
        } else if (iso && iso->enable_mount) {
            if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
        }

        if (wl->work_dir[0] != 0) {
            if (chdir(wl->work_dir) != 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
        }

        if (redirect_child_stdout_to_stderr) {
            if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                close(pipefd[1]);
                _exit(127);
            }
        }


        char *const default_argv[] = {wl->executable, NULL};
        char *const *exec_argv = argv ? argv : default_argv;

        execvp(wl->executable, exec_argv);

        int err = errno;
        (void)write(pipefd[1], &err, sizeof(err));
        close(pipefd[1]);
        _exit(127);
    }

    wl->pid = pid;
    g_foreground_child_pid = pid;
    close(pipefd[1]);

    if (wl->cgroup_active) {
        int attach_res = minibox_cgroup_attach_pid(wl->cgroup_path, pid);
        char sync_cmd = (attach_res == 0) ? '1' : '0';
        ssize_t wn = -1;
        if (cg_sync_pipe[1] >= 0) {
            wn = write(cg_sync_pipe[1], &sync_cmd, 1);
            int close_err = close(cg_sync_pipe[1]);
            cg_sync_pipe[1] = -1;
            if (close_err != 0 && attach_res == 0) {
                attach_res = -1;
            }
        }

        if (attach_res != 0 || wn != 1) {
            int saved_err = (attach_res != 0) ? ((errno != 0) ? errno : EACCES) : EIO;
            if (cg_sync_pipe[0] >= 0) {
                close(cg_sync_pipe[0]);
                cg_sync_pipe[0] = -1;
            }
            close(pipefd[0]);
            kill(pid, SIGKILL);
            int wait_status = 0;
            while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR);
            minibox_cgroup_cleanup(wl->cgroup_path);
            wl->state = MINIBOX_STATE_FAILED;
            wl->exec_errno = saved_err;
            snprintf(wl->error_msg, sizeof(wl->error_msg), "cgroup attach failed: %s", strerror(saved_err));
            wl->finished_at_ms = minibox_current_time_ms();
            minibox_workload_save(wl);
            g_foreground_child_pid = 0;
            return -1;
        }
    }
    if (cg_sync_pipe[0] >= 0) {
        close(cg_sync_pipe[0]);
        cg_sync_pipe[0] = -1;
    }

    struct sigaction sa_new, sa_old_int, sa_old_term;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = sigint_handler;
    sigemptyset(&sa_new.sa_mask);
    sigaction(SIGINT, &sa_new, &sa_old_int);
    sigaction(SIGTERM, &sa_new, &sa_old_term);

    int child_errno = 0;
    size_t bytes_read = 0;
    while (bytes_read < sizeof(child_errno)) {
        ssize_t n = read(pipefd[0], ((char *)&child_errno) + bytes_read, sizeof(child_errno) - bytes_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        bytes_read += (size_t)n;
    }
    close(pipefd[0]);

    if (bytes_read == sizeof(child_errno)) {
        if (wl->cgroup_active) {
            minibox_cgroup_cleanup(wl->cgroup_path);
        }
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = child_errno;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "%s", strerror(child_errno));
        wl->finished_at_ms = minibox_current_time_ms();

        int wait_status = 0;
        while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR);
        minibox_workload_save(wl);

        sigaction(SIGINT, &sa_old_int, NULL);
        sigaction(SIGTERM, &sa_old_term, NULL);
        g_foreground_child_pid = 0;
        return -1;
    }

    wl->state = MINIBOX_STATE_RUNNING;
    wl->started_at_ms = minibox_current_time_ms();
    minibox_workload_save(wl);

    int status = 0;
    while (1) {
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        break;
    }

    sigaction(SIGINT, &sa_old_int, NULL);
    sigaction(SIGTERM, &sa_old_term, NULL);
    g_foreground_child_pid = 0;

    if (wl->cgroup_active) {
        char events_path[1100];
        snprintf(events_path, sizeof(events_path), "%s/memory.events", wl->cgroup_path);
        FILE *f_ev = fopen(events_path, "r");
        if (f_ev) {
            char eline[128];
            while (fgets(eline, sizeof(eline), f_ev)) {
                int count = 0;
                if ((sscanf(eline, "oom_kill %d", &count) == 1 ||
                     sscanf(eline, "oom %d", &count) == 1) && count > 0) {
                    wl->oom_killed = true;
                    break;
                }
            }
            fclose(f_ev);
        }
        minibox_cgroup_cleanup(wl->cgroup_path);
    }

    wl->finished_at_ms = minibox_current_time_ms();
    if (WIFEXITED(status)) {
        wl->state = MINIBOX_STATE_COMPLETED;
        wl->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        wl->state = MINIBOX_STATE_TERMINATED;
        wl->term_signal = WTERMSIG(status);
    }

    minibox_workload_save(wl);
    return 0;
}

int minibox_start_workload_background(minibox_workload_t *wl,
                                      char *const *argv,
                                      const minibox_isolation_config_t *iso) {
    if (!wl || !wl->executable[0]) {
        errno = EINVAL;
        return -1;
    }

    if (iso) {
        wl->iso_uts = iso->enable_uts;
        snprintf(wl->iso_hostname, sizeof(wl->iso_hostname), "%s",
                 iso->hostname[0] ? iso->hostname : wl->id);
        wl->iso_pid = iso->enable_pid;
        wl->iso_mount = iso->enable_mount;
    }

    minibox_resource_status_t res_status;
    bool has_limits = (wl->limits.cpu_percent > 0 || wl->limits.memory_max_bytes > 0 || wl->limits.pids_max > 0);
    if (has_limits) {
        if (minibox_cgroup_setup_workload(wl->id, &wl->limits, wl->cgroup_path, sizeof(wl->cgroup_path), &res_status) == 0) {
            wl->cgroup_active = true;
        } else {
            wl->state = MINIBOX_STATE_FAILED;
            snprintf(wl->error_msg, sizeof(wl->error_msg), "cgroup limit setup failed: %.200s",
                     res_status.error_msg[0] ? res_status.error_msg : "unknown error");
            wl->finished_at_ms = minibox_current_time_ms();
            minibox_workload_save(wl);
            return -1;
        }
    }

    wl->state = MINIBOX_STATE_STARTING;
    minibox_workload_save(wl);

    int sync_pipe[2];
    if (pipe2(sync_pipe, O_CLOEXEC) < 0) {
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = errno;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "%s", strerror(errno));
        wl->finished_at_ms = minibox_current_time_ms();
        minibox_workload_save(wl);
        return -1;
    }

    pid_t supervisor_pid = fork();
    if (supervisor_pid < 0) {
        int saved_errno = errno;
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = saved_errno;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "%s", strerror(saved_errno));
        wl->finished_at_ms = minibox_current_time_ms();
        minibox_workload_save(wl);
        return -1;
    }

    if (supervisor_pid > 0) {
        close(sync_pipe[1]);
        int start_result = -1;
        ssize_t n = read(sync_pipe[0], &start_result, sizeof(start_result));
        close(sync_pipe[0]);

        if (n == sizeof(start_result) && start_result == 0) {
            minibox_workload_load(wl->id, wl);
            return 0;
        } else {
            minibox_workload_load(wl->id, wl);
            return -1;
        }
    }

    /* Supervisor process */
    close(sync_pipe[0]);
    setsid();

    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) {
            close(null_fd);
        }
    }

    int log_fd = open(wl->log_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (log_fd < 0) {
        int err = errno;
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = err;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "Failed to open log file: %s", strerror(err));
        wl->finished_at_ms = minibox_current_time_ms();
        minibox_workload_save(wl);

        int code = -1;
        (void)write(sync_pipe[1], &code, sizeof(code));
        close(sync_pipe[1]);
        _exit(1);
    }

    int err_pipe[2];
    if (pipe2(err_pipe, O_CLOEXEC) < 0) {
        int err = errno;
        close(log_fd);
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = err;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "Pipe error: %s", strerror(err));
        wl->finished_at_ms = minibox_current_time_ms();
        minibox_workload_save(wl);

        int code = -1;
        (void)write(sync_pipe[1], &code, sizeof(code));
        close(sync_pipe[1]);
        _exit(1);
    }

    int cg_sync_pipe[2] = {-1, -1};
    if (wl->cgroup_active) {
        if (pipe2(cg_sync_pipe, O_CLOEXEC) < 0) {
            int err = errno;
            close(log_fd);
            close(err_pipe[0]);
            close(err_pipe[1]);
            minibox_cgroup_cleanup(wl->cgroup_path);
            wl->state = MINIBOX_STATE_FAILED;
            wl->exec_errno = err;
            snprintf(wl->error_msg, sizeof(wl->error_msg), "Pipe error: %s", strerror(err));
            wl->finished_at_ms = minibox_current_time_ms();
            minibox_workload_save(wl);

            int code = -1;
            (void)write(sync_pipe[1], &code, sizeof(code));
            close(sync_pipe[1]);
            _exit(1);
        }
    }

    pid_t child_pid = fork();
    if (child_pid < 0) {
        int err = errno;
        close(log_fd);
        close(err_pipe[0]);
        close(err_pipe[1]);
        if (cg_sync_pipe[0] >= 0) {
            close(cg_sync_pipe[0]);
            close(cg_sync_pipe[1]);
        }
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = err;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "Child fork error: %s", strerror(err));
        wl->finished_at_ms = minibox_current_time_ms();
        minibox_workload_save(wl);

        int code = -1;
        (void)write(sync_pipe[1], &code, sizeof(code));
        close(sync_pipe[1]);
        _exit(1);
    }

    if (child_pid == 0) {
        close(sync_pipe[1]);
        close(err_pipe[0]);
        if (cg_sync_pipe[1] >= 0) close(cg_sync_pipe[1]);

        if (iso) {
            if (minibox_prepare_isolation(iso, wl->id) != 0) {
                int err = errno;
                (void)write(err_pipe[1], &err, sizeof(err));
                close(err_pipe[1]);
                if (cg_sync_pipe[0] >= 0) close(cg_sync_pipe[0]);
                _exit(127);
            }
        }

        /*
         * Synchronize with parent supervisor so this process is attached to the cgroup
         * BEFORE creating child processes in the new PID namespace.
         */
        if (cg_sync_pipe[0] >= 0) {
            char sync_byte = 0;
            ssize_t sn = read(cg_sync_pipe[0], &sync_byte, 1);
            close(cg_sync_pipe[0]);
            cg_sync_pipe[0] = -1;
            if (sn != 1 || sync_byte != '1') {
                int err = (sn < 0) ? errno : ((sync_byte == '0') ? EACCES : EIO);
                (void)write(err_pipe[1], &err, sizeof(err));
                close(err_pipe[1]);
                _exit(127);
            }
        }

        if (iso && iso->enable_pid) {
            pid_t pid1 = fork();
            if (pid1 < 0) {
                int err = errno;
                (void)write(err_pipe[1], &err, sizeof(err));
                close(err_pipe[1]);
                _exit(127);
            }
            if (pid1 > 0) {
                g_pid1_child = pid1;
                struct sigaction sa;
                memset(&sa, 0, sizeof(sa));
                sa.sa_handler = forward_signal_to_pid1;
                sigaction(SIGTERM, &sa, NULL);
                sigaction(SIGINT, &sa, NULL);
                sigaction(SIGHUP, &sa, NULL);

                int init_status = 0;
                while (waitpid(pid1, &init_status, 0) < 0 && errno == EINTR);
                g_pid1_child = 0;
                if (WIFEXITED(init_status)) _exit(WEXITSTATUS(init_status));
                if (WIFSIGNALED(init_status)) _exit(128 + WTERMSIG(init_status));
                _exit(0);
            }

            /* In child: this process is PID 1 in the new PID namespace.
             * Mount new /proc filesystem as a mandatory step for PID isolation. */
            if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
                int err = errno;
                (void)write(err_pipe[1], &err, sizeof(err));
                close(err_pipe[1]);
                _exit(127);
            }
        } else if (iso && iso->enable_mount) {
            if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
                int err = errno;
                (void)write(err_pipe[1], &err, sizeof(err));
                close(err_pipe[1]);
                _exit(127);
            }
        }

        if (wl->work_dir[0] != 0) {
            if (chdir(wl->work_dir) != 0) {
                int err = errno;
                (void)write(err_pipe[1], &err, sizeof(err));
                close(err_pipe[1]);
                _exit(127);
            }
        }

        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);


        char *const default_argv[] = {wl->executable, NULL};
        char *const *exec_argv = argv ? argv : default_argv;

        execvp(wl->executable, exec_argv);

        int err = errno;
        (void)write(err_pipe[1], &err, sizeof(err));
        close(err_pipe[1]);
        _exit(127);
    }

    close(log_fd);
    close(err_pipe[1]);

    if (wl->cgroup_active) {
        int attach_res = minibox_cgroup_attach_pid(wl->cgroup_path, child_pid);
        char sync_cmd = (attach_res == 0) ? '1' : '0';
        ssize_t wn = -1;
        if (cg_sync_pipe[1] >= 0) {
            wn = write(cg_sync_pipe[1], &sync_cmd, 1);
            int close_err = close(cg_sync_pipe[1]);
            cg_sync_pipe[1] = -1;
            if (close_err != 0 && attach_res == 0) {
                attach_res = -1;
            }
        }

        if (attach_res != 0 || wn != 1) {
            int saved_err = (attach_res != 0) ? ((errno != 0) ? errno : EACCES) : EIO;
            if (cg_sync_pipe[0] >= 0) {
                close(cg_sync_pipe[0]);
                cg_sync_pipe[0] = -1;
            }
            close(err_pipe[0]);
            kill(child_pid, SIGKILL);
            int wait_status = 0;
            while (waitpid(child_pid, &wait_status, 0) < 0 && errno == EINTR);
            minibox_cgroup_cleanup(wl->cgroup_path);
            wl->state = MINIBOX_STATE_FAILED;
            wl->exec_errno = saved_err;
            snprintf(wl->error_msg, sizeof(wl->error_msg), "cgroup attach failed: %s", strerror(saved_err));
            wl->finished_at_ms = minibox_current_time_ms();
            minibox_workload_save(wl);

            int code = -1;
            (void)write(sync_pipe[1], &code, sizeof(code));
            close(sync_pipe[1]);
            _exit(1);
        }
    }
    if (cg_sync_pipe[0] >= 0) {
        close(cg_sync_pipe[0]);
        cg_sync_pipe[0] = -1;
    }

    int child_errno = 0;
    size_t bytes_read = 0;
    while (bytes_read < sizeof(child_errno)) {
        ssize_t n = read(err_pipe[0], ((char *)&child_errno) + bytes_read, sizeof(child_errno) - bytes_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        bytes_read += (size_t)n;
    }
    close(err_pipe[0]);

    if (bytes_read == sizeof(child_errno)) {
        if (wl->cgroup_active) minibox_cgroup_cleanup(wl->cgroup_path);
        wl->state = MINIBOX_STATE_FAILED;
        wl->exec_errno = child_errno;
        snprintf(wl->error_msg, sizeof(wl->error_msg), "%s", strerror(child_errno));
        wl->finished_at_ms = minibox_current_time_ms();

        int wait_status = 0;
        while (waitpid(child_pid, &wait_status, 0) < 0 && errno == EINTR);
        minibox_workload_save(wl);

        int code = -1;
        (void)write(sync_pipe[1], &code, sizeof(code));
        close(sync_pipe[1]);
        _exit(1);
    }

    wl->pid = child_pid;
    wl->supervisor_pid = getpid();
    wl->state = MINIBOX_STATE_RUNNING;
    wl->started_at_ms = minibox_current_time_ms();
    minibox_workload_save(wl);

    int code = 0;
    (void)write(sync_pipe[1], &code, sizeof(code));
    close(sync_pipe[1]);

    int status = 0;
    while (1) {
        pid_t w = waitpid(child_pid, &status, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        break;
    }

    if (wl->cgroup_active) {
        char events_path[1100];
        snprintf(events_path, sizeof(events_path), "%s/memory.events", wl->cgroup_path);
        FILE *f_ev = fopen(events_path, "r");
        if (f_ev) {
            char eline[128];
            while (fgets(eline, sizeof(eline), f_ev)) {
                int count = 0;
                if ((sscanf(eline, "oom_kill %d", &count) == 1 ||
                     sscanf(eline, "oom %d", &count) == 1) && count > 0) {
                    wl->oom_killed = true;
                    break;
                }
            }
            fclose(f_ev);
        }
        minibox_cgroup_cleanup(wl->cgroup_path);
    }

    wl->finished_at_ms = minibox_current_time_ms();
    if (WIFEXITED(status)) {
        wl->state = MINIBOX_STATE_COMPLETED;
        wl->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        wl->state = MINIBOX_STATE_TERMINATED;
        wl->term_signal = WTERMSIG(status);
    }
    minibox_workload_save(wl);
    _exit(0);
}

int minibox_stop_workload(const char *id, int signal_num, bool force) {
    if (!id) {
        errno = EINVAL;
        return -1;
    }

    minibox_workload_t wl;
    if (minibox_workload_load(id, &wl) != 0) {
        errno = ENOENT;
        return -1;
    }
    minibox_workload_refresh_state(&wl);

    if (minibox_state_is_terminal(wl.state)) {
        return 0;
    }

    if (wl.pid <= 1) {
        errno = ESRCH;
        return -1;
    }

    wl.state = MINIBOX_STATE_STOPPING;
    minibox_workload_save(&wl);

    int sig = signal_num > 0 ? signal_num : SIGTERM;
    if (kill(wl.pid, sig) != 0) {
        if (errno == ESRCH) {
            minibox_workload_refresh_state(&wl);
            return 0;
        }
        return -1;
    }

    bool exited = false;
    for (int i = 0; i < 50; i++) {
        if (kill(wl.pid, 0) != 0 && errno == ESRCH) {
            exited = true;
            break;
        }
        usleep(20000);
    }

    if (!exited && force) {
        kill(wl.pid, SIGKILL);
        for (int i = 0; i < 25; i++) {
            if (kill(wl.pid, 0) != 0 && errno == ESRCH) {
                exited = true;
                break;
            }
            usleep(20000);
        }
    }

    usleep(20000);
    minibox_workload_refresh_state(&wl);

    if (!minibox_state_is_terminal(wl.state)) {
        wl.state = MINIBOX_STATE_TERMINATED;
        wl.term_signal = force ? SIGKILL : sig;
        if (wl.finished_at_ms == 0) {
            wl.finished_at_ms = minibox_current_time_ms();
        }
        if (wl.cgroup_active) {
            minibox_cgroup_cleanup(wl.cgroup_path);
        }
        minibox_workload_save(&wl);
    }

    return 0;
}

