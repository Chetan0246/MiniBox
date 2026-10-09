# MiniBox TUI: User Manual & Operator Guide

## 1. Introduction

MiniBox TUI is an educational, interactive container runtime and process isolation dashboard for Linux. It allows you to launch, monitor, restrict, inspect, and benchmark containerized processes directly from your terminal.

---

## 2. Installation & Quickstart

### 2.1 System Requirements
- **Operating System**: Modern Linux distribution (Fedora 38+, Ubuntu 22.04+, Debian 12+, Arch Linux).
- **Kernel**: Linux 5.14 or later with unprivileged user namespaces and cgroups v2 enabled.
- **Compiler**: GCC 9+ or Clang supporting C11.
- **Python**: Python 3.9+ with `venv`.

### 2.2 Building and Launching

```bash
# 1. Clone the repository and enter the workspace
git clone https://github.com/example/MiniBox.git
cd MiniBox

# 2. Build the C runtime and test workload binaries
make

# 3. Create and activate a Python virtual environment
python3 -m venv .venv
source .venv/bin/activate

# 4. Install Textual and UI dependencies
pip install -r tui/requirements.txt

# 5. Launch the MiniBox Terminal UI
./minibox-tui
```

---

## 3. Terminal Interface Navigation

The dashboard provides a full-screen layout designed for fast, single-key control:

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

### 3.1 Keyboard Controls

| Key | Action | Description |
|---|---|---|
| `n` | **New Workload** | Opens the workload creation modal dialog |
| `s` | **Stop Workload** | Sends graceful `SIGTERM` to the selected container |
| `i` | **Inspect Workload** | Displays detailed namespace, cgroup, and process status |
| `l` | **View Logs** | Switches focus to the Workload Logs tab |
| `c` | **Bulk Cleanup** | Cleans up and deletes all finished/terminated workloads |
| `b` | **Benchmark Suite** | Opens the interactive performance benchmark runner |
| `r` | **Refresh** | Immediately polls the runtime for fresh telemetry |
| `h` | **Help** | Displays keybindings and system capabilities overview |
| `q` | **Quit** | Closes the MiniBox TUI dashboard cleanly |
| `Esc` | **Close Dialog** | Dismisses any active modal dialog |
| `Up` / `Down` | **Select Row** | Navigates between workloads in the active table |

---

## 4. Workload Management

### 4.1 Creating a Workload (Key: `n`)
Pressing `n` opens the **Create Workload** dialog. You can either type custom commands or click one of the quick-load template buttons:
- **Hello World**: Quick-terminating `/bin/echo` demonstration.
- **Sleeper**: Long-running background workload (`./bin/examples/sleeper 60`).
- **CPU Throttling**: Compute-bound single-core spinloop (`./bin/examples/cpu_hog 10`).
- **Memory Allocation**: Physical page dirtying workload (`./bin/examples/memory_hog 48`).
- **Process Bomb**: Process fork stress test (`./bin/examples/fork_test 10`).

#### Configuring Resource Limits:
- **CPU Limit (%)**: Configure CFS bandwidth throttling (e.g. `50` for 50% of one core).
- **Memory Limit**: Configure maximum RAM usage (e.g. `64M`, `256M`, `1G`).
- **PID Limit**: Configure maximum allowable process count (e.g. `10`).
- **Namespaces**: Toggle UTS isolation (custom hostname), PID isolation, and mount isolation.

### 4.2 Inspecting a Workload (Key: `i`)
Highlight a workload using the arrow keys and press `i`. The inspector modal displays:
- Supervisor PID and Child PID.
- Exact command arguments and working directory.
- Namespace flags (`UTS`, `PID`, `Mount`).
- Configured cgroups v2 limits and active cgroup directory.
- Execution exit code or termination signal.

### 4.3 Stopping a Workload (Key: `s`)
Select an active workload and press `s`. A confirmation modal will appear. Confirming sends `SIGTERM` to the workload. The supervisor catches the exit event, transitions the workload to `COMPLETED` or `TERMINATED`, and cleans up cgroups safely.

### 4.4 Bulk Cleanup (Key: `c`)
Pressing `c` removes all inactive (completed, terminated, or failed) container records from disk and reclaims all associated state directories.

---

## 5. Benchmarking from the Interface (Key: `b`)

Pressing `b` opens the **Performance Benchmark** modal.
1. The modal displays existing benchmark data along with the exact timestamp of execution.
2. Clicking **Run Benchmark (Quick)** launches genuine performance experiments in the background:
   - Microbenchmark launch latency across configurations.
   - CFS CPU throttling precision over a 2.0s observation window.
   - Memory page dirtying latency and OOM killer verification.
   - Process tree fork bomb restriction (`pids.max`).
   - Supervisor lifecycle latencies.
3. The interface updates dynamically as results are computed.

---

## 6. Command-Line Interface (CLI)

MiniBox also functions as a standalone command-line tool without the TUI:

```bash
# 1. Print system capabilities
./bin/minibox capabilities

# 2. Run an isolated container in the foreground
./bin/minibox run --isolate --cpu 50 --memory 64M /bin/sh -c "hostname; sleep 1"

# 3. Start a background container
./bin/minibox start --name my-worker ./bin/examples/sleeper 30

# 4. List all container workloads
./bin/minibox list

# 5. Inspect container status
./bin/minibox inspect <workload_id>

# 6. Stream live container resource metrics
./bin/minibox metrics <workload_id>

# 7. Stop an active container
./bin/minibox stop <workload_id>

# 8. Clean up terminated containers
./bin/minibox cleanup
```

---

## 7. Troubleshooting & FAQ

### Q: Why does CPU usage slightly exceed the quota (e.g., 50.8% instead of 50.0%)?
The Linux Completely Fair Scheduler (CFS) throttles workloads across 100 ms periods. Over small observation windows (e.g. 2 seconds), scheduler boundary quantization causes $\pm 0.8\%$ natural variation depending on slice phase alignment.

### Q: Why does memory limit test exit with code 137?
Exit code 137 corresponds to $128 + 9$ (`SIGKILL`). When a process exceeds `memory.max` with swap disabled, the Linux kernel Out-Of-Memory (OOM) killer immediately terminates the process with `SIGKILL`.

### Q: Can MiniBox run without `root` or `sudo`?
Yes! MiniBox is specifically engineered to operate completely in unprivileged mode using user namespaces (`CLONE_NEWUSER`) and delegated cgroups v2 (`user.slice`).
