# MiniBox: Unprivileged Linux Container Runtime — Deep Dive

**Version:** 0.8.0-phase8  
**Runtime language:** C (C11, `_GNU_SOURCE`)  
**User interface:** Python 3 + Textual TUI  
**Build system:** GNU Make  
**Working directory:** `/home/chetan/MiniBox`

---

## 1. What MiniBox Is

MiniBox is an educational Linux container runtime. It lets a normal (non-root) user create lightweight “container” workloads using Linux namespaces for isolation and cgroups v2 for resource control.

Unlike Docker, MiniBox is designed to make the operating-system mechanisms visible:

- `fork()` and `execvp()` for process creation.
- user, UTS, PID, and mount namespaces.
- cgroups v2 (`cpu.max`, `memory.max`, `memory.swap.max`, `pids.max`).
- `/proc` based monitoring.
- A foreground/background supervisor model with zero-zombie cleanup.
- Mandatory kernel security hardening (`PR_SET_NO_NEW_PRIVS`, capability dropping, seccomp-BPF).

The core binary is `bin/minibox`. The interactive dashboard is `minibox-tui`, a Textual Python app in `tui/`.

---

## 2. High-Level Architecture

```text
+---------------------------+
| MiniBox TUI (Python)      |
|  - screens, modals        |
|  - client.py subprocess   |
+------------+--------------+
             |
             v
+---------------------------+
| minibox CLI (C runtime)   |
|  main.c commands          |
+------------+--------------+
             |
             v
+---------------------------+
| Kernel primitives         |
|  namespaces, cgroups v2,  |
|  /proc, seccomp, caps     |
+---------------------------+
```

Runtime persistence is file-based:

```text
$MINIBOX_STATE_DIR (default /tmp/minibox-<uid>/workloads)
└── <workload-id>/
    ├── state.json
    └── output.log
```

There is no daemon. State is saved and reloaded from `state.json`, which allows background workloads to outlive the command that started them.

---

## 3. Repository Structure

```text
MiniBox/
├── Makefile
├── runtime/
│   ├── include/        *.h public API/structs
│   └── src/            C implementation (main, process, workload, isolation, resources, monitor, lifecycle)
├── examples/           C programs used as workloads
├── tests/              shell, behavioral, regression, TUI tests
├── tui/                Python Textual dashboard
├── docs/               architecture, design, manual, reports, viva questions
├── benchmarks/         performance benchmark suite
└── demo/               scripted demonstration
```

---

## 4. Build and Run

```bash
make
./bin/minibox version
./bin/minibox capabilities
./bin/minibox run bin/examples/hello
```

Important Make targets:

- `make` / `make all` — build runtime + examples
- `make test` — all test suites
- `make test-phaseN` — phase test
- `make clean` — remove build artifacts
- `make benchmark`, `make demo`

C compiler flags include:

```make
-Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE
```

This makes the project compile cleanly with strict warnings.

---

## 5. Core Data Model

### 5.1 Workload Record

`runtime/include/workload.h` defines `minibox_workload_t`, the central entity:

- identity: `id`, `name`
- lifecycle: `state`, `created_at_ms`, `started_at_ms`, `finished_at_ms`
- process: `pid`, `supervisor_pid`, `executable`, `args`, `work_dir`
- result: `exit_code`, `term_signal`, `exec_errno`, `error_msg`
- output/log: `log_file`
- isolation: `iso_uts`, `iso_hostname`, `iso_pid`, `iso_mount`
- resources: `limits` + `cgroup_path`, `cgroup_active`, `oom_killed`
- PID safety: `pid_start_time`

### 5.2 Lifecycle States

Defined in `runtime/include/lifecycle.h`:

```text
CREATED -> STARTING -> RUNNING -> COMPLETED
                            -> TERMINATED
                            -> STOPPING -> TERMINATED
CREATED/STARTING can fail -> FAILED
```

Terminal states are `COMPLETED`, `TERMINATED`, and `FAILED`.

### 5.3 Resource Limits

```c
typedef struct {
    int cpu_percent;            /* 0 = unlimited */
    uint64_t memory_max_bytes;  /* 0 = unlimited */
    int pids_max;               /* 0 = unlimited */
} minibox_resource_limits_t;
```

---

## 6. Important C Modules and Functions

### 6.1 `main.c` — CLI Controller

Key commands:

- `capabilities`
- `run`
- `start`
- `list`
- `inspect`
- `metrics` / `stats`
- `stop`
- `logs`
- `delete`
- `cleanup`
- `version`, `help`

Important helpers:

- `parse_strict_positive_int()` — safe integer parsing with bounds.
- `collect_positional_arg()` — prevents multiple workload IDs being passed as one command argument.
- `print_json_string_field()` — emits properly escaped JSON strings.
- `cmd_run()` / `cmd_start()` — parse flags, initialize workload, invoke foreground/background launcher.
- `cmd_metrics()` — collects host + per-workload metrics.
- `cmd_stop()` — dispatches termination and reclaims resources.
- `cmd_delete()` / `cmd_cleanup()` — remove terminal records.

Notable design: foreground vs background:

- `run` uses `minibox_run_workload_foreground()`; the CLI waits for completion.
- `start` uses `minibox_start_workload_background()`; a supervisor process remains alive and reaps the child.

### 6.2 `process.c` — Process Launch and Supervision

#### `minibox_run_process()`

Basic fork/exec wrapper using an `O_CLOEXEC` pipe to reliably detect child `execvp()` failure.

#### `minibox_run_workload_foreground()`

1. Applies cgroup limits (if configured).
2. Forks child.
3. In child:
   - `minibox_prepare_isolation()`
   - cgroup sync
   - PID namespace handling (`fork()` PID 1)
   - mount `/proc`
   - chdir
   - stdout redirect if requested
   - `minibox_apply_runtime_hardening()`
   - `execvp()`
4. In parent:
   - attaches PID to cgroup
   - sync handshake
   - reads exec-failure pipe
   - waits for child
   - checks OOM event
   - cleans cgroup
   - saves state

Important global:

```c
static volatile sig_atomic_t g_foreground_child_pid = 0;
```

Signal handler forwards `SIGINT`/`SIGTERM` to the foreground workload.

#### `minibox_start_workload_background()`

1. Sets up cgroups.
2. Forks supervisor.
3. Supervisor:
   - `setsid()`
   - redirects stdio
   - opens log file
   - forks workload child
   - attaches cgroup
   - handles PID namespace
   - applies hardening
   - `execvp()`
   - waits for child
   - OOM detection
   - cgroup cleanup
   - state persistence
   - `_exit()`

The parent returns a launch code to the CLI over a pipe.

#### `minibox_stop_workload()`

- Refreshes state.
- Refuses unknown/recycled PIDs via PID start-time comparison.
- Publishes `STOPPING`.
- Sends signal.
- Waits/polls.
- Escalates to `SIGKILL` if `--force`.
- Prefers supervisor’s authoritative terminal result.
- Cleans cgroup.

### 6.3 `workload.c` — Record Persistence and Safety

Key functions:

- `minibox_get_state_dir()` / `minibox_get_state_dir_buf()`
- `minibox_generate_workload_id()`
- `minibox_workload_init()`
- `minibox_workload_save()`
- `minibox_workload_load()`
- `minibox_workload_refresh_state()`
- `minibox_workload_list()`
- `minibox_workload_delete()`
- `minibox_workload_cleanup_completed()`

Safety features:

- Safe workload ID validation (`is_safe_workload_id()`), blocks path traversal and many special characters.
- Atomic-ish writes using `mkstemp()` + `rename()`.
- Unique temporary file per writer.
- `fsync()` before rename.
- JSON escaping for quote, backslash, control characters, and Unicode.
- PID reuse protection using `/proc/<pid>/stat` start time.
- Process liveness checks distinguish `live`, `gone`, `matches`.

### 6.4 `isolation.c` — Linux Namespaces and Hardening

#### `minibox_detect_capabilities()`

Forks a probe process and tests:

- user namespaces
- UTS namespaces
- PID namespaces and whether child sees PID 1
- mount namespaces
- cgroups v2 kernel/delegated controllers
- writable controllers

#### `minibox_prepare_isolation()`

Steps:

1. Create user namespace.
2. Write `deny` to `/proc/self/setgroups`.
3. Map UID/GID 0 -> real user.
4. Create mount namespace if mount or PID isolation requested.
5. Make root mount private (`MS_REC | MS_PRIVATE`).
6. Create UTS namespace and set hostname.
7. Create PID namespace (`CLONE_NEWPID`) so children become PID 1.

#### `minibox_apply_runtime_hardening()`

Order matters:

1. `PR_SET_NO_NEW_PRIVS`
2. Drop disallowed capabilities from bounding set.
3. Install seccomp-BPF deny list.

Denied syscalls include module loading, reboot, ptrace, mount, pivot_root, BPF, performance events, keyctl, and handle resolution.

Important constant sets:

- `g_denied_syscalls[]`
- `g_allowed_caps[]`

### 6.5 `resources.c` — cgroups v2 Management

Key functions:

- `minibox_parse_memory_size()` — parses `K/M/G/KB/MB/GB/KiB/MiB/GiB` and raw bytes.
- `minibox_cgroup_find_candidate()` — finds delegated cgroup subtree.
- `minibox_cgroup_find_root()` — creates `<candidate>/minibox`, enables controllers.
- `minibox_cgroup_setup_workload()` — creates `<base>/<workload-id>` and applies limits.
- `minibox_cgroup_attach_pid()` — writes PID to `cgroup.procs` and verifies readback.
- `minibox_cgroup_cleanup()` — `cgroup.kill`, waits, `rmdir`.

Enforcement strictness:

- CPU: writes `cpu.max = quota period`
- Memory: writes `memory.max`, then enforces `memory.swap.max = 0`
- PIDs: writes `pids.max`
- Failed readback is treated as failure and cgroup is cleaned up (fail-closed).

### 6.6 `monitor.c` — Metrics Collection

Structures:

- `minibox_cpu_metrics_t`
- `minibox_memory_metrics_t`
- `minibox_pids_metrics_t`
- `minibox_host_metrics_t`
- `minibox_workload_metrics_t`

Key functions:

- `minibox_collect_host_metrics()`
- `minibox_collect_workload_metrics()`
- `minibox_collect_workload_metrics_with_host()`

Data sources:

- Host CPU: `/proc/stat`, cached sample with safe time window.
- Host memory: `/proc/meminfo`
- Load average: `/proc/loadavg`
- Workload CPU: cgroup `cpu.stat` or `/proc/<pid>/stat`
- Workload memory: cgroup `memory.current`, `memory.peak`, or `/proc/<pid>/status`
- Process count: cgroup `pids.current` or thread count
- OOM: `memory.events`

### 6.7 `lifecycle.c` — State Transition Validation

Functions:

- `minibox_state_to_string()`
- `minibox_state_from_string()`
- `minibox_is_valid_transition()`
- `minibox_state_is_terminal()`

This enforces the lifecycle model and prevents invalid state changes.

---

## 7. CLI Commands Reference

```text
capabilities [--json]
run [options] <executable> [args...]
start [options] <executable> [args...]
list [--json]
inspect [--json] <workload_id>
metrics [--json] [--sample-ms <ms>] [<workload_id>]
stop [--json] [--force] [--signal <num>] <id>
logs <workload_id>
delete [--json] [--force] <workload_id>
cleanup [--json]
version
help
```

Common options:

```text
--id <id>
--name <name>
--workdir <dir>
--cpu <pct>
--memory <size>
--pids <max>
--isolate
--uts
--hostname <name>
--pid-ns
--mount-ns
--json
--
```

Exit code behavior:

- normal completion returns child exit code
- `FAILED` returns 127
- signal termination returns `128 + signal`

---

## 8. Textual TUI Architecture

Files:

- `tui/app.py` — main dashboard
- `tui/client.py` — async bridge to `minibox`
- `tui/models.py` — dataclasses for runtime JSON
- `tui/screens/*.py` — modals

Features:

- workload table
- host resource panel
- workload summary panel
- events log
- log viewer
- capability view
- create workload form
- inspect modal
- stop/delete confirmations
- benchmark modal
- help/education modal

Client uses `asyncio.create_subprocess_exec()` with a 10-second timeout. Periodic refresh runs every 1.5 seconds, protected by `_refreshing`.

Key TUI models:

- `Capabilities`
- `HostMetrics`
- `WorkloadSummary`
- `WorkloadDetail`
- `ResourceLimits`
- `EventItem`

---

## 9. End-to-End Workload Flow

### Foreground `run`

1. Parse CLI options.
2. `minibox_workload_init()` creates record and directory.
3. Setup cgroup if limits exist.
4. Fork child.
5. Child performs isolation and hardening, then execs target.
6. Parent waits, detects result, checks OOM, cleans cgroup, saves state.
7. CLI optionally emits JSON, returns exit code.

### Background `start`

1. Parse CLI options.
2. `minibox_workload_init()`.
3. Setup cgroup.
4. Fork supervisor.
5. Parent reads launch code and returns.
6. Supervisor forks workload child.
7. Supervisor waits and publishes final state.

### `stop`

1. Load state and refresh.
2. Verify PID is the original process (PID start time).
3. Publish `STOPPING`.
4. Signal process.
5. Wait/escalate.
6. Prefer supervisor authoritative result.
7. Clean cgroup.

---

## 10. Security Model

MiniBox targets unprivileged user namespaces. Security measures:

- UID/GID mapped-only to root inside user namespace.
- `setgroups` deny.
- mount namespace root made private.
- fresh `/proc` mounted only inside the new mount/PID namespace.
- PID namespace gives workload PID 1.
- UTS namespace isolates hostname.
- `PR_SET_NO_NEW_PRIVS`.
- Capability bounding set reduction.
- seccomp-BPF deny list for dangerous kernel operations.
- Strict workload ID/path validation.
- Runtime path containment checks before recursive deletion.
- PID reuse protection before any `kill()`.
- Fail-closed cgroup limit application.
- Fixed buffers and careful JSON escaping.

---

## 11. Testing and Validation

Test categories:

- Phase 1–8 tests
- behavioral tests
- regression tests
- TUI pilot tests

Covered scenarios:

- normal execution and argument forwarding
- exit codes
- signal termination
- invalid executable handling
- JSON output
- working directory
- namespaces and isolation
- cgroups CPU/memory/PID limits
- OOM enforcement
- dynamic metrics
- path traversal rejection
- Unicode JSON decoding
- concurrent writes
- PID safety
- seccomp behavior
- TUI interactions

Build/test command:

```bash
make test
.venv/bin/python -m pytest tests/test_tui.py
```

---

## 12. Audit Fixes Applied

Final polished issues addressed:

1. `read_whole_file()` now truly grows dynamically and checks cap safely.
2. `delete` handling made safer for path validation/error paths.
3. `--hostname` length is validated.
4. Hardening documentation matched implementation behavior.
5. TUI delete now checks runtime JSON `ok`.

The project builds cleanly and all provided tests pass.

---

## 13. Demos

`demo/demo_script.sh` demonstrates:

1. capability detection
2. launching a workload with CPU + memory limits
3. live metrics inspection
4. second workload tracking
5. graceful stop
6. OOM killer enforcement

Benchmarks are in `benchmarks/benchmark.py`, with results in `benchmarks/results.json` and `docs/performance_report.md`.

---

## 14. Likely Viva Questions

### Why use `/proc/<pid>/stat` start time?

To ensure a PID has not been recycled by the kernel. MiniBox never signals a PID unless the recorded start time matches.

### Why use an error pipe in `fork/exec`?

Because `fork()` returning success does not prove `execvp()` succeeded. The child writes errno to a pipe if exec fails; EOF means success.

### Why are workloads in PID namespaces hard to stop?

The host sees the wrapper PID, while the workload sees PID 1 internally. The wrapper forwards signals and reaps the inner process.

### Why apply hardening last?

Because mount, `/proc`, and namespace setup may require privileges and capabilities; hardening must not block setup.

### What makes state persistence safe?

Atomic rename, unique temp files, fsync, chmod, and safe workload ID validation.

---

## 15. Key Code Pointers (Quick Index)

- `runtime/src/main.c` — CLI commands
- `runtime/src/process.c` — fork/exec, foreground/background, stop
- `runtime/src/workload.c` — state JSON, list, delete, PID safety
- `runtime/src/isolation.c` — namespaces, capabilities, seccomp
- `runtime/src/resources.c` — cgroups v2 limits
- `runtime/src/monitor.c` — host/workload metrics
- `runtime/src/lifecycle.c` — lifecycle states/transitions
- `tui/app.py` — dashboard
- `tui/client.py` — async runtime bridge
- `tui/models.py` — data models

---

## 16. Current Limitations

- File-based state instead of a daemon or database.
- Full nested process management complexity.
- Some cgroup paths depend on system delegation.
- No networking namespace isolation in this educational implementation.
- seccomp policy is a deny-list, not a full allow-list profile.
- User namespace availability depends on kernel configuration.

These limitations are intentional and appropriate for an educational runtime.

---

*Generated for project review and viva preparation.*
