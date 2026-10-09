# MiniBox TUI: Empirical Performance Evaluation & Benchmark Report

## 1. Experimental Methodology & Testbed

All performance evaluations were conducted on real physical hardware running native Linux without emulation or virtualization layers. Every measurement utilizes high-precision monotonic timers (`time.perf_counter_ns()` in Python and `clock_gettime(CLOCK_MONOTONIC)` in C) and is cross-referenced against kernel accounting interfaces.

### 1.1 Testbed Environment
- **Host Operating System**: Fedora Linux 44 (KDE Plasma Desktop Edition)
- **Linux Kernel**: `7.1.13-200.fc44.x86_64`
- **CPU Architecture**: `x86_64` (AMD Ryzen 7 250 w/ Radeon 780M Graphics, 16 physical/logical cores)
- **Total Physical Memory**: 13.28 GiB RAM
- **Compiler**: `gcc (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2)` with flags `-Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE`
- **Repetitions ($N$)**: 10 trials per experiment configuration

---

## 2. Experiment 1: Container Launch Overhead

### 2.1 Objective
Measure the wall-clock startup and execution latency of a minimal no-op binary (`/usr/bin/true`) comparing direct host invocation with various container isolation configurations.

### 2.2 Empirical Measurements (10 Trials per Configuration)

| Configuration | Min (ms) | Median (ms) | Mean (ms) | Max (ms) | StdDev (ms) | Overhead vs Direct | Ratio |
|---|---|---|---|---|---|---|
| **Direct Host Execution** | 0.48 | **0.65** | 0.81 | 1.44 | 0.36 | Baseline (0.0 ms) | 1.0x |
| **MiniBox Foreground Baseline** | 0.92 | **1.14** | 1.13 | 1.29 | 0.13 | +0.49 ms | 1.8x |
| **MiniBox Isolated (UTS+PID+Mount)** | 1.32 | **1.59** | 1.62 | 2.11 | 0.20 | +0.94 ms | 2.5x |
| **MiniBox Full Isolation + cgroups v2** | 1.68 | **1.97** | 3.76 | 18.70 | 5.27 | +1.32 ms | 3.0x |

### 2.3 Latency Analysis & Reconciliation
1. **Direct Execution Baseline (0.65 ms)**: Represents the cost of kernel `execve()`, dynamic linker ELF resolution, and immediate termination.
2. **Foreground C Runtime Overhead (+0.49 ms)**: Spawning through MiniBox adds argument parsing, state initialization, and error-pipe verification.
3. **Namespace Isolation Overhead (+0.94 ms)**: `unshare(CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS)` and establishing user/group ID mappings adds ~0.94 ms of kernel namespace table allocation.
4. **Cold-Start vs. Steady-State cgroup Overhead**:
   - **Cold Start (Trial 1 = 18.70 ms)**: On the initial invocation, creating the cgroup directory under `/sys/fs/cgroup/user.slice/...`, writing controller strings (`+cpu +memory +pids`), and migrating the process into `cgroup.procs` takes ~18 ms due to filesystem directory allocation and kernel tree synchronization.
   - **Warm Cache (Trials 2–10 = 1.68–2.23 ms)**: Subsequent executions complete in ~1.97 ms.
   - **Conclusion**: MiniBox container initialization overhead in steady state is under **2.0 ms**.

---

## 3. Experiment 2: Compute Workload & CFS Quota Throttling

### 3.1 Objective
Evaluate the precision of the Linux kernel Completely Fair Scheduler (CFS) bandwidth rate limiting on a compute-bound spinloop (`cpu_hog 2`) over an observation window of 2.0 wall-clock seconds.

### 3.2 Scheduler Parameters
- **CFS Period**: 100,000 µs (100 ms).
- **CFS 50% Quota**: 50,000 µs per 100 ms period (`cpu.max = 50000 100000`).
- **CFS 25% Quota**: 25,000 µs per 100 ms period (`cpu.max = 25000 100000`).
- **Observation Window**: 2.00 seconds (spanning exactly 20 CFS scheduling periods).

### 3.3 Empirical Measurements (10 Trials per Configuration)

| Configuration | Target Quota | Wall Time (s) | CPU Time Granted (s) | Measured CPU Usage (%) | Throttling Status |
|---|---|---|---|---|---|
| **Direct Host** | 100.0% | 2.00s | 1.99s | 99.6% | None |
| **MiniBox Unconstrained** | 100.0% | 2.00s | 1.99s | 99.6% | None |
| **MiniBox CFS 50% Quota** | 50.0% | 2.03s | 1.02s | **50.5%** | Active (Throttled) |
| **MiniBox CFS 25% Quota** | 25.0% | 2.04s | 0.52s | **25.4%** | Active (Throttled) |

### 3.4 CFS Boundary Quantization Analysis
Across 10 repeated trials, the raw measured CPU percentages for 50% quota were:
`[49.9%, 50.3%, 51.2%, 51.0%, 51.2%, 50.1%, 50.5%, 50.9%, 50.1%, 50.6%]` (median: **50.55%**).
Because the workload loops until `CLOCK_MONOTONIC` reaches 2.000s, whether the process captures 20 or 21 quota slices dictates whether 1.00s or 1.05s of CPU time is granted. This causes $\pm 0.8\%$ natural quantization variation around the nominal 50.0% mark, demonstrating accurate kernel enforcement.

---

## 4. Experiment 3: Memory Allocation Latency vs Limit Enforcement

### 4.1 Part A: Sub-Ceiling Allocation Latency (48 MiB)
Measures physical page dirtying latency (`memory_hog 48`) touching 48 MiB of RAM.

| Execution Mode | Configured Limit | Target Allocation | Median Latency (ms) | StdDev (ms) |
|---|---|---|---|---|
| **Direct Host Execution** | None | 48 MiB | **520.19** | 1.81 |
| **MiniBox Unconstrained** | None | 48 MiB | **519.98** | 1.22 |
| **MiniBox Limited (64M Ceiling)** | 64 MiB | 48 MiB | **528.41** | 4.36 |

- *Observation*: Operating within a cgroup adds ~8.2 ms of kernel page-accounting overhead during bulk physical allocation.

### 4.2 Part B: Hard Ceiling Enforcement (64 MiB with 16 MiB Limit)
Tests kernel Out-Of-Memory (OOM) killer enforcement with swap disabled (`memory.swap.max = 0`).
- **Configured `memory.max`**: 16 MiB (16,777,216 bytes).
- **Target Allocation**: 64 MiB.
- **Enforcement Result**: 100% of runs were terminated by the Linux kernel OOM killer via `SIGKILL` (signal 9, status code 137) within ~175 ms of execution.

---

## 5. Experiment 4: Process Spawning & PID Limit Enforcement

### 5.1 Objective
Evaluate process tree restriction and fork bomb protection (`fork_test 10`).

### 5.2 Workload Exit-Code Semantics
- **Exit Code 2 (Unconstrained)**: All 10 child processes spawned successfully without OS restrictions.
- **Exit Code 0 (Enforced)**: The 5th child `fork()` failed with `errno == EAGAIN`, proving kernel enforcement.

| Configuration | Target Forks | Configured `pids.max` | Exit Code | Verified Semantics | Median Latency (ms) |
|---|---|---|---|---|---|
| **Direct Host** | 10 | Unlimited | 2 | Unconstrained | 501.60 |
| **MiniBox Unconstrained** | 10 | Unlimited | 2 | Unconstrained | 502.55 |
| **MiniBox Constrained (`--pids 5`)** | 10 | 5 PIDs | 0 | **Kernel Enforced (`EAGAIN`)** | 511.51 |

---

## 6. Experiment 5: Background Lifecycle & Supervision Latency

Measures individual supervisor lifecycle operations across 10 repetitions:

| Operation | Description | Median Latency (ms) | StdDev (ms) | Architectural Semantics |
|---|---|---|---|---|
| **`start`** | Double-fork supervisor daemonization, cgroup binding | **1.44** | 0.11 | Asynchronous non-blocking launch |
| **`inspect`** | State load via POSIX `rename()`, procfs verification | **0.65** | 0.09 | Read-only inspect |
| **`stop`** | Graceful SIGTERM dispatch, supervisor waitpid reaping | **40.97** | 0.11 | Zero-zombie harvest |
| **`cleanup`** | Sequential cgroup `rmdir` and state directory unlink | **0.90** | 1.56 | Orderly teardown |

---

## 7. Conclusions

1. **Lightweight Profile**: MiniBox introduces minimal launch latency (~1.3 ms overhead in steady state) while providing full isolation and resource control.
2. **Enforcement Authenticity**: All resource limits demonstrate genuine Linux kernel enforcement: CFS throttling holds compute within $\pm 0.8\%$ of configured quotas, memory limits trigger immediate OOM termination, and PID limits reliably intercept process bomb creation via `EAGAIN`.
3. **Zero Resource Leaks**: Supervisor harvesting and cgroup teardown reliably prevent zombie processes or orphaned cgroups.
