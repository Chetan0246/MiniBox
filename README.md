# MiniBox TUI: An Educational Linux Container Runtime

**MiniBox TUI** is an educational, lightweight Linux container runtime designed to make operating system mechanisms—process management, namespace isolation, cgroup resource allocation, and `/proc` performance monitoring—transparent, observable, and interactive.

Built entirely from first principles using native Linux system calls, MiniBox rejects fake monitoring values, simulated isolation, and administrative (`sudo`) requirements. It empowers students and developers to inspect exactly how the Linux kernel isolates and controls running processes.

---

## Key Features

- **Native Linux Process Launcher**: Invokes real executables using `fork()`, error-pipe handshaking (`pipe2` with `O_CLOEXEC`), and `execvp()`. Harvests exit codes and signals via `waitpid()` with zero zombie processes.
- **Unprivileged Linux Namespace Isolation**:
  - **User Namespaces (`CLONE_NEWUSER`)**: Operates entirely rootless without `sudo`, mapping unprivileged user IDs to root inside the isolated namespace.
  - **UTS Namespaces (`CLONE_NEWUTS`)**: Changes container hostnames independently without affecting the host.
  - **PID Namespaces (`CLONE_NEWPID`)**: Isolates process trees, running the container entrypoint as **PID 1** (`init`).
  - **Mount Namespaces (`CLONE_NEWNS`)**: Remounts a private `/proc` filesystem so process inspection reflects only the container's process tree.
- **cgroups v2 Resource Throttling**:
  - **CFS CPU Bandwidth (`cpu.max`)**: Restricts CPU consumption to exact quotas (e.g., 50% of one core) over 100 ms scheduling periods.
  - **Hard Memory Ceilings (`memory.max`)**: Enforces physical page allocations with swap disabled (`memory.swap.max = 0`), triggering the kernel Out-Of-Memory (OOM) killer upon violation.
  - **Process-Tree Restrictions (`pids.max`)**: Defends against fork bombs by rejecting excess child creation with `errno == EAGAIN`.
  - **Safe Cleanup**: Signals `cgroup.kill` and drains kernel accounting before unlinking directories.
- **Dynamic Resource Telemetry**: Real-time rate-based CPU% calculation, Resident Set Size (`VmRSS`), peak memory, and CFS throttling statistics sampled from `/proc` and cgroups v2.
- **Crash-Resilient State Machine**: 7-state finite state machine with terminal-state invariants, serialized atomically via temporary files and POSIX `rename()`.
- **Interactive Terminal UI**: Full-screen terminal dashboard built with Textual, featuring live workload monitoring, one-click creation templates, inspection dialogs, log viewing, and a built-in benchmark runner.
- **Multi-Trial Empirical Benchmarks**: Rigorous performance evaluation suite measuring startup latency, CFS accuracy, and lifecycle latency across 10-trial distributions.

---

## Architecture Overview

```text
+-------------------------------------------------------------------------+
|                         MiniBox Terminal UI (Python)                    |
|   Dashboard | Workload Table | Resource Telemetry | Modals | Benchmark  |
+------------------------------------+------------------------------------+
                                     | JSON Lines / Subprocess IPC
                                     v
+-------------------------------------------------------------------------+
|                        Runtime Client (`tui/client.py`)                 |
|   Request Validation | Typed Deserialization | Error Handling           |
+------------------------------------+------------------------------------+
                                     | CLI Invocations (`--json`)
                                     v
+-------------------------------------------------------------------------+
|                       Core C Runtime (`bin/minibox`)                    |
|   +--------------------+  +--------------------+  +------------------+  |
|   | Process Manager    |  | Isolation Manager  |  | Resource Manager |  |
|   | fork/execvp/waitpid|  | User/UTS/PID/Mount |  | cgroups v2       |  |
|   +--------------------+  +--------------------+  +------------------+  |
|   +--------------------+  +--------------------+                        |
|   | Lifecycle Manager  |  | Resource Monitor   |                        |
|   | State Machine      |  | /proc & cgroup stat|                        |
|   +--------------------+  +--------------------+                        |
+------------------------------------+------------------------------------+
                                     | Native Linux System Calls
                                     v
+-------------------------------------------------------------------------+
|                           Linux Kernel (5.14+)                          |
|   clone() / unshare() | cgroups v2 (user.slice) | /proc | pipe2()       |
+-------------------------------------------------------------------------+
```

---

## Terminal UI Preview

```text
========================================================================================
MiniBox TUI: Container Runtime Dashboard                     [USER-NS] [UTS-NS] [CGROUP-V2]
========================================================================================
Host Overview (AMD Ryzen 7, 16 cores)  | Active Workloads
---------------------------------------|------------------------------------------------
CPU Usage:       4.2%                  | ID           Name       PID   State    CPU%  Mem
RAM Used:        3.2 / 13.3 GiB        | -----------------------------------------------
Load Avg:        0.18, 0.22, 0.19      | wl-a1b2c3d4  my-sleeper 8120  RUNNING  0.0%  4.2M
Total Workloads: 1                     |
Active:          1                     |
Completed:       0                     |
---------------------------------------+------------------------------------------------
Workload Details / Logs / Metrics
[Tab: Inspector]  [Tab: Live Metrics]  [Tab: Workload Logs]
----------------------------------------------------------------------------------------
Workload ID:    wl-a1b2c3d4        State: RUNNING          PID: 8120
Executable:     ./bin/examples/sleeper 100                 Log: /tmp/minibox_1000/...
Isolation:      UTS: ON (box-1)  PID: ON  MNT: ON          CPU Limit: 50%  RAM Limit: 64M
========================================================================================
Keys: [N] New  [S] Stop  [I] Inspect  [L] Logs  [C] Cleanup  [B] Benchmark  [H] Help  [Q] Quit
========================================================================================
```

---

## Supported Environment

- **Operating System**: Linux (Fedora 38+, Ubuntu 22.04+, Debian 12+, Arch Linux).
- **Kernel Version**: Linux 5.14 or later (tested on 7.1.13-200.fc44.x86_64).
- **cgroups Configuration**: cgroups v2 enabled and delegated to unprivileged users (`user.slice`).
- **Dependencies**:
  - GCC 9+ or Clang (supporting C11).
  - Python 3.9+ with `python3-venv`.
  - GNU Make.

---

## Installation & Launch

```bash
# 1. Clone the repository
git clone https://github.com/example/MiniBox.git
cd MiniBox

# 2. Compile the C runtime and test workload binaries
make

# 3. Create and activate a Python virtual environment
python3 -m venv .venv
source .venv/bin/activate

# 4. Install Textual UI dependencies
pip install -r tui/requirements.txt

# 5. Launch the interactive dashboard
./minibox-tui
```

### Dashboard Keybindings

| Key | Action | Description |
|---|---|---|
| `n` | **New Workload** | Opens workload creation dialog with pre-filled templates |
| `s` | **Stop Workload** | Dispatches graceful `SIGTERM` to the selected container |
| `i` | **Inspect Workload** | Displays container PID, namespaces, cgroup paths, and status |
| `l` | **View Logs** | Switches focus to the real-time container log viewer |
| `c` | **Bulk Cleanup** | Cleans up all finished, terminated, and failed workloads |
| `b` | **Benchmark Runner** | Opens the interactive performance benchmark modal |
| `r` | **Refresh** | Immediately polls the runtime for live metrics |
| `h` | **Help** | Displays keybindings and system capabilities |
| `q` | **Quit** | Cleanly exits the dashboard |

---

## Command-Line Interface (CLI) Usage

MiniBox functions as a complete standalone CLI tool independent of the TUI:

```bash
# Display system capabilities (namespaces & cgroups)
./bin/minibox capabilities

# Run an isolated container in foreground with CPU & memory limits
./bin/minibox run --isolate --cpu 50 --memory 64M /bin/sh -c "hostname; sleep 1"

# Launch a background container
./bin/minibox start --name my-worker ./bin/examples/sleeper 30

# List all workloads
./bin/minibox list

# Inspect container state and resource limits
./bin/minibox inspect <workload_id>

# Display live workload and host telemetry
./bin/minibox metrics <workload_id>

# Stop an active container
./bin/minibox stop <workload_id>

# Remove finished containers and clean up disk state
./bin/minibox cleanup
```

---

## Performance Summary

Measurements recorded across 10 empirical trials on native hardware (AMD Ryzen 7, 16 cores, Fedora 44, Linux Kernel 7.1.13):

| Experiment | Metric | Direct Host | MiniBox Container | Measured Overhead |
|---|---|---|---|---|
| **Startup Overhead** | Minimal binary latency | 0.65 ms (median) / 0.81 ms (mean) | 1.97 ms (median) / 3.76 ms (mean) | **+1.32 ms** (steady-state median; cold start: 18.7 ms) |
| **CFS CPU Quota** | 50% target over 2.0s | 99.6% (median) | 50.55% (median) / 50.58% (mean) | $\pm 0.8\%$ quantization jitter |
| **Memory Ceiling** | 64 MiB dirty under 16 MiB | N/A | SIGKILL (137) | **100% OOM Killed** |
| **PID Fork Bomb** | 10 forks under `pids.max=5` | 10 forks (2) | 5th fork rejected (0) | **`errno == EAGAIN`** |
| **Supervisor Start** | Background launch latency | N/A | 1.44 ms | Sub-2 ms daemonization |

*See [`docs/performance_report.md`](docs/performance_report.md) for full statistical breakdown.*

---

## Running the Automated Test Suite

MiniBox includes comprehensive test suites covering all 8 phases of development plus behavioral hardening (89 passing tests):

```bash
# Run all automated test suites (Phases 1 through 8 + Behavioral Verification)
make test

# Run individual test suites
make test-phase1       # Process launcher and exit harvesting
make test-phase2       # Lifecycle state machine and background supervisor
make test-phase3       # Linux namespace isolation (UTS, User, PID, Mount)
make test-phase4       # cgroups v2 resource limits (CPU, memory, PIDs)
make test-phase5       # Telemetry and /proc metrics collection
make test-phase6       # Terminal UI and Textual headless pilot tests
make test-phase7       # Performance benchmarks and behavioral assertions
make test-phase8       # Final integration, documentation, and demo verification
make test-behavioral   # Kernel isolation, strict validation, and security hardening

# Execute the complete 13-step demonstration scenario
make demo
```

---

## Documentation Index

- [Architecture Specification (`docs/architecture.md`)](docs/architecture.md): UI, protocol, runtime modules, and Linux interfaces.
- [Detailed Design (`docs/design.md`)](docs/design.md): State machine, process launch sequence, namespace init, and error handling.
- [User Manual (`docs/user_manual.md`)](docs/user_manual.md): Step-by-step navigation, workload creation, and troubleshooting.
- [Testing Strategy & Report (`docs/testing.md`)](docs/testing.md): Test matrix (T01–T15), expected vs. actual results, and verification evidence.
- [Performance Evaluation (`docs/performance_report.md`)](docs/performance_report.md): Methodology, empirical data tables, and analysis.
- [Viva Voce Preparation Guide (`docs/viva_questions.md`)](docs/viva_questions.md): Oral exam questions and answers on core operating system concepts.
- [Final Technical Report (`docs/final_report.md`)](docs/final_report.md): Formal course project report with abstract, background, and findings.

---

## Limitations

1. **Unprivileged Rootless Mounts**: Private `/proc` isolation requires `CLONE_NEWNS` + `CLONE_NEWUSER` + `CLONE_NEWPID`; root filesystem mutation is intentionally restricted without host `root` privileges.
2. **Cold-Start Latency**: The very first container invocation incurs an initial ~10–18 ms operating system penalty for cgroup tree synchronization; subsequent warm executions complete in ~1.97 ms.
3. **CFS Granularity**: CFS throttles over 100 ms periods; burst compute workloads executing in under 100 ms cannot be throttled.

---

## License

MiniBox is released under the MIT License for educational and research purposes.
