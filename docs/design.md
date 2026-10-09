# MiniBox TUI: Detailed System Design

## 1. Requirements

### 1.1 Functional Requirements
- **FR1 (Process Launching)**: Launch arbitrary binary executables with arguments via native `fork()` and `execvp()`.
- **FR2 (Exit Harvesting)**: Collect exact termination status (`exit_code` or `term_signal`) using `waitpid()` with zero zombie processes.
- **FR3 (Lifecycle Tracking)**: Track container workloads across background lifecycles through distinct operational states.
- **FR4 (Namespace Isolation)**: Provide unprivileged UTS, PID, User, and Mount namespace isolation without requiring `sudo` or host root privileges.
- **FR5 (Resource Throttling)**: Enforce CPU bandwidth quotas via CFS (`cpu.max`), physical memory limits (`memory.max`), and process-tree limits (`pids.max`).
- **FR6 (Monitoring & Telemetry)**: Provide live CPU %, memory RSS/peak, and CFS throttle statistics sampled from `/proc` and cgroups v2.
- **FR7 (Interactive Terminal UI)**: Enable user-friendly control, inspection, live monitoring, log viewing, and benchmarking through Textual.

### 1.2 Non-Functional Requirements
- **NFR1 (Unprivileged Security)**: Never require or invoke `sudo`. Operate entirely within the user's unprivileged namespace and delegated user slice.
- **NFR2 (Atomicity & Crash Safety)**: Workload state file writes must be atomic via POSIX `rename()` to prevent corrupt reads by external tools or the TUI.
- **NFR3 (Fail-Safe Cleanup)**: Ensure cgroups and processes are safely terminated (via `cgroup.kill` and SIGTERM/SIGKILL) with zero orphaned resources.
- **NFR4 (Minimal Overhead)**: Steady-state container startup latency must remain under 3.0 ms.

---

## 2. Module Responsibilities

| Module | Primary Header | Core Functions | Architectural Invariant |
|---|---|---|---|
| **Process Manager** | `runtime/include/process.h` | `minibox_run_workload_foreground`<br>`minibox_start_workload_background`<br>`minibox_stop_workload` | No zombie processes; error pipe is always drained before concluding startup. |
| **Lifecycle Manager** | `runtime/include/lifecycle.h` | `minibox_state_to_string`<br>`minibox_state_from_string`<br>`minibox_is_valid_transition`<br>`minibox_state_is_terminal` | Terminal states (`COMPLETED`, `TERMINATED`, `FAILED`) cannot transition to any other state. |
| **Isolation Manager** | `runtime/include/isolation.h` | `minibox_detect_capabilities`<br>`minibox_prepare_isolation`<br>`minibox_setup_user_ns_mappings` | If any namespace setup step fails, the child immediately aborts before `execvp()`. |
| **Resource Manager** | `runtime/include/resources.h` | `minibox_cgroup_find_root`<br>`minibox_cgroup_setup_workload`<br>`minibox_cgroup_attach_pid`<br>`minibox_cgroup_cleanup` | Workload cgroup directories are always created under delegated user slices and unlinked upon process termination. |
| **Resource Monitor** | `runtime/include/monitor.h` | `minibox_collect_workload_metrics`<br>`minibox_collect_host_metrics`<br>`minibox_calculate_cpu_percent` | Non-blocking telemetry; if process has exited, dynamic metrics are reported as invalid rather than synthesized. |
| **Workload Store** | `runtime/include/workload.h` | `minibox_workload_init`<br>`minibox_workload_save`<br>`minibox_workload_load`<br>`minibox_workload_delete` | State files are written to a `.tmp` file and atomically renamed to `state.json`. |

---

## 3. Workload Lifecycle State Machine

MiniBox enforces a 7-state finite state machine with deterministic transitions:

```text
                      +-------------------+
                      |      CREATED      |
                      +---------+---------+
                                |
                                v
                      +-------------------+
       +------------> |     STARTING      | <------------+
       |              +---------+---------+              |
       |                        |                        |
       | Error on setup         v          execvp error  |
       |              +-------------------+              |
       +------------> |      RUNNING      | -------------+
                      +----+---------+----+
                           |         |
             SIGTERM / 's' |         | Process naturally exits
                           v         |
                      +----+-----+   |
                      | STOPPING |   |
                      +----+-----+   |
                           |         |
          +----------------+         +----------------+
          |                                           |
          v                                           v
+-------------------+                       +-------------------+
|    TERMINATED     |                       |     COMPLETED     |
| (Signal/SIGKILL)  |                       |  (Normal Exit 0)  |
+-------------------+                       +-------------------+
          |                                           |
          +--------------------+----------------------+
                               |
                               | Handled in error branches
                               v
                      +-------------------+
                      |      FAILED       |
                      |  (Exec/Pipe Fail) |
                      +-------------------+
```

### 3.1 Transition Rules
1. `CREATED` $\rightarrow$ `STARTING` or `FAILED`.
2. `STARTING` $\rightarrow$ `RUNNING` or `FAILED`.
3. `RUNNING` $\rightarrow$ `STOPPING` (via signal), `COMPLETED` (normal termination), or `TERMINATED` (unhandled signal).
4. `STOPPING` $\rightarrow$ `TERMINATED` or `COMPLETED`.
5. `COMPLETED`, `TERMINATED`, `FAILED` are terminal states; no further transitions are permitted.

---

## 4. Process Launch Sequence

To prevent race conditions between namespace establishment, cgroup association, and executable invocation, MiniBox uses a **dual-pipe handshake**:

```text
Parent (minibox)                                Child (workload)
       |                                               |
       |-- 1. pipe2(err_pipe, O_CLOEXEC) ------------->|
       |-- 2. pipe2(cg_sync_pipe, O_CLOEXEC) --------->|
       |-- 3. fork() --------------------------------->|
       |                                               |
       | (Child enters)                                |-- 4. unshare(USER|UTS|PID|NS)
       |                                               |-- 5. configure uid_map & gid_map
       |                                               |-- 6. read(cg_sync_pipe) [BLOCKS]
       |                                               |      (Awaits cgroup attachment)
       |-- 7. Write Child PID to cgroup.procs -------->|
       |-- 8. write(cg_sync_pipe, "1") [UNBLOCK] ----->|
       |                                               |-- 9. dup2(log_fd, STDOUT/ERR)
       |                                               |-- 10. execvp(executable, argv)
       |                                               |
       |                                               | (If execvp fails:)
       |                                               |-- 11. write(err_pipe, &errno)
       |                                               |-- 12. _exit(127)
       |
       |-- 13. read(err_pipe) [Check for exec errors]
       |       - If bytes read: mark FAILED, reap child.
       |       - If EOF (0 bytes): execvp succeeded!
       |
       |-- 14. waitpid(child_pid, &status, 0)
       |-- 15. Record exit code and cleanup cgroup
```

---

## 5. Namespace Isolation Sequence

When `--isolate` is specified (or individual namespace flags `--uts`, `--pid-ns`, `--mount-ns`):

1. **`CLONE_NEWUSER`**:
   - `unshare(CLONE_NEWUSER)` creates a new user namespace where the unprivileged caller gains `CAP_SYS_ADMIN` inside that namespace.
   - The runtime opens `/proc/self/setgroups` and writes `"deny\n"`.
   - The runtime writes `"0 <host_uid> 1\n"` to `/proc/self/uid_map` and `"0 <host_gid> 1\n"` to `/proc/self/gid_map`.
2. **`CLONE_NEWUTS`**:
   - `unshare(CLONE_NEWUTS)` decouples the hostname table.
   - `sethostname(new_name, len)` updates the container hostname without modifying the host system.
3. **`CLONE_NEWPID`**:
   - `unshare(CLONE_NEWPID)` isolates process hierarchy.
   - A subsequent child `fork()` inside the namespace becomes **PID 1** (`init`).
4. **`CLONE_NEWNS` (Mount)**:
   - `unshare(CLONE_NEWNS)` decouples filesystem mount points.
   - Remounts `/proc` using `mount("proc", "/proc", "proc", 0, NULL)` so process inspection commands inside the container see only isolated processes.

---

## 6. Resource Control Sequence (cgroups v2)

1. **Hierarchy Discovery**:
   - Discovers delegated unprivileged controller mount: `/sys/fs/cgroup/user.slice/user-<uid>.slice/user@<uid>.service/app.slice`.
2. **Subtree Controller Enablement**:
   - Writes `"+cpu +memory +pids\n"` to `cgroup.subtree_control`.
3. **Workload Subgroup Creation**:
   - Creates directory `/sys/fs/cgroup/.../minibox/<workload_id>`.
4. **Parameter Application**:
   - **CPU**: Writes `"<quota_us> <period_us>\n"` to `cpu.max`.
   - **Memory**: Writes `"<bytes>\n"` to `memory.max`, and `"0\n"` to `memory.swap.max` to guarantee hard ceiling enforcement without swap spilling.
   - **PIDs**: Writes `"<count>\n"` to `pids.max`.
5. **Teardown & Drain**:
   - Writes `"1\n"` to `cgroup.kill` to atomically terminate all processes in the slice.
   - Retries `rmdir()` up to 15 times with 20ms sleeps to allow kernel accounting structures to flush cleanly.

---

## 7. Error Handling & Resilience

1. **Atomic File Persistence**:
   - Writing directly to `state.json` risks partial-read corruption during supervisor crashes.
   - MiniBox writes state to `/tmp/minibox_<uid>/<id>/state.json.tmp` and executes POSIX `rename()` to achieve atomic inode substitution.
2. **Invalid Executable Handling**:
   - The execution error pipe (`pipefd`) is configured with `O_CLOEXEC`.
   - If `execvp()` succeeds, the descriptor is automatically closed, and the parent reads EOF (`0` bytes).
   - If `execvp()` fails, the child writes `errno` into the pipe before calling `_exit(127)`. The parent receives the exact system error code (e.g., `ENOENT` 2, `EACCES` 13).
3. **Signal Forwarding**:
   - In foreground mode, the launcher installs signal handlers for `SIGINT` and `SIGTERM`.
   - When the user presses `Ctrl+C`, the launcher relays `SIGINT` directly to the child process group and awaits clean termination via `waitpid()`.
