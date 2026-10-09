# MiniBox TUI: Architecture Specification

## 1. System Overview

MiniBox is an educational, lightweight Linux container runtime designed to make operating system mechanisms—process management, namespace isolation, cgroup resource allocation, and `/proc` performance monitoring—transparent, observable, and interactive.

The system is architecturally split into three distinct layers:
1. **Interactive Terminal User Interface (TUI)**: A Python 3 Textual application providing full-screen monitoring, workload creation, inspection, and lifecycle actions.
2. **Runtime Client & IPC Protocol**: An asynchronous client interfacing with the core runtime through structured JSON Lines and CLI invocations.
3. **Core C Runtime (`minibox`)**: A high-performance, modular C11 binary interacting directly with the Linux kernel through native system calls.

---

## 2. High-Level Architecture Diagram

```text
+---------------------------------------------------------------------------------+
|                               MiniBox Terminal UI                               |
|   +-----------------------+ +-----------------------+ +---------------------+   |
|   | Dashboard & Workloads | | Resource Stats (Host) | | Inspection & Modals |   |
|   +-----------------------+ +-----------------------+ +---------------------+   |
|   | Log Viewer Tab        | | Benchmark Runner      | | System Capabilities |   |
|   +-----------------------+ +-----------------------+ +---------------------+   |
+----------------------------------------+----------------------------------------+
                                         |
                                         | JSON / Subprocess IPC
                                         v
+---------------------------------------------------------------------------------+
|                       Runtime Client (`tui/client.py`)                          |
|   - Request Validation (executable path, CPU %, Memory bytes, PID limits)       |
|   - Subprocess Spawner & Protocol Serializer (`--json` output mode)             |
|   - Asynchronous Result Deserialization & Typed Data Models                     |
+----------------------------------------+----------------------------------------+
                                         |
                                         | CLI / Argument Vector
                                         v
+---------------------------------------------------------------------------------+
|                             Core C Runtime (`minibox`)                          |
|                                                                                 |
|   +------------------------------------+  +---------------------------------+   |
|   | Process Manager (`process.c`)      |  | Isolation Manager (`isolation.c`)|  |
|   | - fork() / execvp() / waitpid()    |  | - CLONE_NEWUSER (UID/GID maps)  |   |
|   | - Pipe-based errno synchronization |  | - CLONE_NEWUTS (hostname)       |   |
|   | - Signal propagation (SIGINT/TERM) |  | - CLONE_NEWPID (PID 1 init)     |   |
|   | - Zero-zombie reaping loop         |  | - CLONE_NEWNS (procfs remount)  |   |
|   +------------------------------------+  +---------------------------------+   |
|                                                                                 |
|   +------------------------------------+  +---------------------------------+   |
|   | Lifecycle Manager (`lifecycle.c`)  |  | Resource Manager (`resources.c`)|   |
|   | - 7-state finite state machine     |  | - cgroups v2 controller setup   |   |
|   | - State transition guards          |  | - cpu.max (CFS bandwidth quota) |   |
|   | - Atomic POSIX rename() persistence|  | - memory.max & memory.swap.max  |   |
|   | - Background supervisor daemon     |  | - pids.max (fork limit)         |   |
|   +------------------------------------+  | - Safe cgroup.kill & rmdir drain|   |
|                                           +---------------------------------+   |
|   +------------------------------------+                                        |
|   | Resource Monitor (`monitor.c`)     |                                        |
|   | - /proc/[pid]/stat CPU tick delta  |                                        |
|   | - /proc/[pid]/status RSS memory    |                                        |
|   | - /proc/loadavg & /proc/meminfo    |                                        |
|   | - cgroup.stat & cpu.stat throttles |                                        |
|   +------------------------------------+                                        |
+----------------------------------------+----------------------------------------+
                                         |
                                         | Native Linux System Calls
                                         v
+---------------------------------------------------------------------------------+
|                             Linux Kernel (v7.1+)                                |
|   - clone() / unshare()        - cgroups v2 (user.slice)     - kill() / sigaction|
|   - mount() / umount2()        - /proc filesystem            - pipe2() (O_CLOEXEC|
+---------------------------------------------------------------------------------+
```

---

## 3. Component Details & Responsibilities

### 3.1 Terminal User Interface (`tui/`)
- **`app.py`**: The application driver implementing `MiniBoxApp`. Manages screen rendering, keyboard shortcuts (`n` for new, `i` for inspect, `s` for stop, `l` for logs, `c` for cleanup, `b` for benchmark, `r` for refresh, `q` for quit), and periodic timer-based data polling (every 1.5 seconds).
- **`client.py`**: Low-level protocol abstraction. Executes `./bin/minibox` with JSON flags, verifies input bounds, and parses structured output into dataclasses (`WorkloadMetrics`, `HostMetrics`, `CapabilityReport`).
- **Modals (`tui/screens/`)**:
  - `create_modal.py`: Validated input form with preset quick-load buttons (`hello`, `sleeper`, `cpu_hog`, `memory_hog`, `fork_test`).
  - `inspect_modal.py`: Read-only property inspector displaying container namespaces, cgroup directories, limits, and exit status.
  - `confirm_modal.py`: Safety prompt for stopping or deleting active workloads.
  - `help_modal.py`: Keybinding reference and educational system overview.
  - `benchmark_modal.py`: Real-time benchmark runner and report viewer with timestamp verification.

### 3.2 Core Runtime Modules (`runtime/src/`)
- **`main.c`**: Command-line entry point. Parses subcommands (`run`, `start`, `stop`, `list`, `inspect`, `delete`, `cleanup`, `metrics`, `capabilities`, `version`) and sets output formatting (human-readable tables vs strict JSON).
- **`process.c`**:
  - **Foreground Execution (`minibox_run_workload_foreground`)**: Creates error and synchronization pipes, forks the child, applies namespaces and cgroups, synchronizes child startup, waits for completion via `waitpid()`, and safely unlinks cgroups.
  - **Background Supervision (`minibox_start_workload_background`)**: Uses a double-fork daemonization pattern. The supervisor process stays alive in the background to reap child exits, record final exit status, and cleanly flush logs.
- **`isolation.c`**:
  - Unshares namespaces using `unshare()`.
  - Configures unprivileged user namespace mappings (`/proc/self/uid_map` and `/proc/self/gid_map`) with `setgroups` denial.
  - Remounts an isolated `/proc` instance so `ps` inside the container reflects only isolated PID tree.
- **`resources.c`**:
  - Discovers unprivileged cgroups v2 delegated slice (`user.slice`).
  - Creates workload-specific subdirectories under `.../user@<uid>.service/minibox/<id>`.
  - Applies `cpu.max`, `memory.max`, `memory.swap.max`, and `pids.max`.
  - Attaches processes by writing PID to `cgroup.procs`.
  - Safely destroys cgroups by signaling `cgroup.kill` and draining `rmdir()`.
- **`lifecycle.c`**:
  - Enforces the 7-state lifecycle state machine.
  - Validates state transitions to prevent invalid jumps (e.g. `COMPLETED` -> `RUNNING`).
- **`workload.c`**:
  - Manages workload persistence under `/tmp/minibox_<uid>/<workload_id>/state.json`.
  - Implements crash-resilient atomic updates using temporary files and POSIX `rename()`.
- **`monitor.c`**:
  - Samples CPU ticks from `/proc/[pid]/stat` over delta intervals to calculate instantaneous CPU %.
  - Gathers memory RSS, peak memory, and CFS throttling statistics from kernel accounting interfaces.

---

## 4. Communication & IPC Protocol

The interface between the Python TUI and the C runtime uses standard UNIX process execution with structured JSON over stdout.

### 4.1 CLI Protocol Conventions
1. **Flag `--json`**: Instructs `minibox` to format all stdout responses as strict JSON.
2. **Status Field `ok`**: Every JSON payload includes an `"ok": true|false` boolean flag.
3. **Separate Error Channels**: Operational failure details are captured in `"error": "..."` in the JSON payload, while low-level process faults write to stderr.
4. **Log Redirection**: Workload standard output and standard error are captured into dedicated per-workload log files (`/tmp/minibox_<uid>/<id>/workload.log`) ensuring protocol isolation.

---

## 5. Linux Kernel System Call Footprint

| Subsystem | Linux System Calls Employed | Architectural Purpose |
|---|---|---|
| **Process Control** | `fork()`, `execvp()`, `waitpid()`, `kill()`, `getpid()`, `setsid()` | Child process spawning, executable invocation, exit reaping, background daemonization |
| **Namespaces** | `unshare()`, `mount()`, `umount2()` | UTS hostname isolation, PID 1 isolation, independent `/proc` mounting |
| **Inter-Process Comm.** | `pipe2(..., O_CLOEXEC)`, `read()`, `write()`, `close()`, `dup2()` | Atomic `execvp` errno propagation, cgroup attachment handshaking, log redirection |
| **Filesystem & State** | `mkdir()`, `rmdir()`, `unlink()`, `rename()`, `opendir()`, `readdir()` | Atomic state persistence via POSIX `rename`, cgroups v2 directory management |
| **Timing & Clocks** | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` | High-precision rate calculation, execution latency measurement, CFS tracking |
