# MiniBox Performance Evaluation & Benchmark Report

**Generated on:** 2026-10-10T00:02:39.890880  
**Runtime:** minibox version 0.5.0-phase5 (Phase 5: Resource Monitoring & Metrics)  
**Operating System:** Fedora Linux 44 (KDE Plasma Desktop Edition) (x86_64)  
**Kernel:** `7.1.13-200.fc44.x86_64`  
**Hardware Profile:** AMD Ryzen 7 250 w/ Radeon 780M Graphics (16 cores), 13.28 GiB RAM  
**Compiler:** `gcc (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2)`  

---

## Executive Summary

MiniBox introduces Linux namespace isolation and cgroups v2 resource control with measurable, lightweight performance characteristics. This report documents empirical measurements across five evaluation experiments. All figures represent genuine operating system measurements obtained via monotonic clocks and verified against kernel accounting interfaces.

---

## Experiment 1: Container Launch Overhead

Microbenchmark measuring wall-clock execution latency for a minimal binary (`/usr/bin/true`) across four configurations (10 trials per configuration).

| Execution Mode | Min (ms) | Median (ms) | Mean (ms) | Max (ms) | StdDev (ms) | Overhead vs Direct |
|---|---|---|---|---|---|---|
| **Direct Host Execution** | 0.48 | 0.65 | 0.81 | 1.44 | 0.36 | Baseline (1.0x) |
| **MiniBox Foreground Baseline** | 0.92 | 1.14 | 1.13 | 1.29 | 0.13 | +0.49 ms (1.8x) |
| **MiniBox Isolated (UTS+PID+Mount)** | 1.32 | 1.59 | 1.62 | 2.11 | 0.20 | +0.94 ms (2.5x) |
| **MiniBox Full Isolation + cgroups v2** | 1.68 | 1.97 | 3.76 | 18.70 | 5.27 | +1.32 ms (3.0x) |

### Launch Latency Reconciliation & Analysis:
- **Direct Execution Baseline:** Direct execution takes ~0.5–1.5 ms depending on dynamic linker page-in and CPU frequency governor scaling (median: 0.65 ms).
- **Namespace Isolation Cost:** Running `unshare(CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS)` and establishing user/group ID mappings adds approximately 0.5–1.0 ms of kernel setup overhead (median: 1.59 ms).
- **cgroups v2 Cold vs Warm Variance:**
  - On the initial invocation (cold start), creating the cgroup directory under `/sys/fs/cgroup/user.slice/...`, writing controller configurations, and migrating the process into `cgroup.procs` takes ~10–18 ms (measured max: 18.70 ms) due to filesystem directory allocation and kernel cgroup tree synchronization.
  - On subsequent warm executions, the operation executes in ~1.6–2.5 ms (measured min: 1.68 ms).
  - In small-trial runs ($N=2$), the cold-start outlier heavily skews the calculated median (e.g. [2.2 ms, 18.2 ms] yields a 10.22 ms median; [2.2 ms, 12.5 ms] yields 7.37 ms; [2.0 ms, 10.8 ms] yields 6.44 ms).
  - Across 10 repetitions, the median steady-state latency stabilizes at 1.97 ms (+1.32 ms overhead vs direct), confirming that container startup overhead in steady state is under 2 ms.

---

## Experiment 2: Compute Workload & CFS Quota Accuracy

Evaluates Completely Fair Scheduler (CFS) bandwidth rate limiting on a single-core spinloop workload (`cpu_hog 2`) over an observation window of 2.0 wall-clock seconds (10 trials).

### Scheduler Configuration:
- **CFS Period:** 100,000 µs (100 ms).
- **CFS 50% Quota:** 50,000 µs per 100 ms period (`cpu.max = 50000 100000`).
- **CFS 25% Quota:** 25,000 µs per 100 ms period (`cpu.max = 25000 100000`).
- **Observation Window:** 2.0 seconds (spanning exactly 20 CFS periods).

| Configuration | Configured Quota | Wall-Clock Time (s) | CPU Time Granted (s) | Measured CPU Usage (%) | Target Quota | CFS Throttling Status |
|---|---|---|---|---|---|---|
| **Direct Host** | 100% (1 core) | 2.00s | 1.99s | 99.6% | 100% | None |
| **MiniBox Unconstrained** | 100% (1 core) | 2.00s | 1.99s | 99.6% | 100% | None |
| **MiniBox CFS 50% Quota** | 50% (50ms / 100ms) | 2.03s | 1.02s | 50.5% | 50.0% | Active (50.5%) |
| **MiniBox CFS 25% Quota** | 25% (25ms / 100ms) | 2.04s | 0.52s | 25.4% | 25.0% | Active (25.4%) |

### CPU Quota Reconciliation & Boundary Analysis:
- In repeated empirical runs, 50% quota measurements vary slightly between **50.5% and 51.6%** (median: ~50.5%).
- **Reason for Boundary Variation:** The workload loops until `CLOCK_MONOTONIC` reaches 2.000s. Across 20 scheduling periods of 100 ms, if the process begins slightly off-phase relative to the kernel's period timer, the kernel allows between 20 and 21 quota slices, resulting in approximately 1.01s to 1.04s of CPU time. This represents normal kernel scheduler quantization jitter (±0.8%), rather than a measurement error or different calculation formula.

---

## Experiment 3: Memory Allocation Latency vs Limit Enforcement

To maintain scientific validity, memory measurements are strictly partitioned into **Performance Measurement** (sub-ceiling allocation) and **Enforcement Verification** (over-ceiling allocation triggering kernel termination).

### Part A: Page Allocation Latency (Sub-Ceiling: 48 MiB Allocated)
Measures physical page dirtying latency (`memory_hog 48` allocating and writing 48 MiB of RAM).

| Execution Mode | Memory Limit | Target Allocation | Min Latency (ms) | Median Latency (ms) | Mean Latency (ms) | StdDev (ms) |
|---|---|---|---|---|---|---|
| **Direct Host Execution** | None | 48 MiB | 516.06 | 520.19 | 520.03 | 1.81 |
| **MiniBox Unconstrained** | None | 48 MiB | 517.99 | 519.98 | 520.13 | 1.22 |
| **MiniBox Limited (64M Ceiling)** | 64 MiB | 48 MiB | 525.94 | 528.41 | 530.28 | 4.36 |

### Part B: Kernel Memory Limit Enforcement (Over-Ceiling: 64 MiB Requested with 16 MiB Limit)
Tests kernel Out-Of-Memory (OOM) killer enforcement with swap disabled (`memory.swap.max = 0`).

- **Configured `memory.max`:** 16 MiB (16,777,216 bytes).
- **Target Allocation:** 64 MiB.
- **Enforcement Result:** Confirmed across all trials. The Linux kernel OOM killer terminates the process via `SIGKILL` (signal 9, status code 137).
- **Enforcement Status:** `VERIFIED (SIGKILL 9)`.

---

## Experiment 4: Process Spawning & PID Limit Enforcement

Evaluates process tree restriction and fork bomb protection (`fork_test 10`).

### Workload Exit Code Semantics:
- **Exit Code 2 (Unconstrained):** The test process spawned all 10 child processes successfully without encountering limits.
- **Exit Code 0 (Enforced):** A child `fork()` failed with `errno == EAGAIN` (Resource temporarily unavailable), confirming kernel cgroups v2 `pids.max` limit enforcement.

| Configuration | Target Forks | Configured `pids.max` | Kernel Behavior | Exit Code | Enforcement Semantics | Median Latency (ms) |
|---|---|---|---|---|---|---|
| **Direct Host** | 10 | Unlimited | All 10 forks succeeded | 2 | Unconstrained | 501.60 |
| **MiniBox Unconstrained** | 10 | Unlimited | All 10 forks succeeded | 2 | Unconstrained | 502.55 |
| **MiniBox Constrained (`--pids 5`)** | 10 | 5 PIDs | 5th fork rejected (`EAGAIN`) | 0 | **Kernel Enforced** | 511.51 |

### Genuine Kernel Enforcement Verification:
- With `pids.max = 5`, the cgroup accommodates 1 parent process and 4 child processes.
- The 5th child fork attempts to create a 6th process in the cgroup. The kernel checks `pids.current >= pids.max` and returns `-1` with `errno = EAGAIN`.
- The test workload detects `EAGAIN`, cleans up existing children with `waitpid()`, and exits with code 0.

---

## Experiment 5: Background Lifecycle & Supervision Latency

Measures individual supervisor lifecycle operations across 10 repetitions.

| Operation | Description | Median Latency (ms) | StdDev (ms) | Execution Semantics |
|---|---|---|---|---|
| **`start`** | Double-fork supervisor launch, isolation init, cgroup binding | 1.44 | 0.11 | Asynchronous daemonization |
| **`inspect`** | Atomic state file load via POSIX `rename()`, procfs check | 0.65 | 0.09 | Read-only inspect |
| **`stop`** | Graceful SIGTERM dispatch, supervisor waitpid reaping | 40.97 | 0.11 | Zero-zombie harvest |
| **`cleanup`** | Sequential cgroup `rmdir` and workload directory removal | 0.90 | 1.56 | Orderly teardown |

*Note on Atomicity:* The term "atomic" applies strictly to the state file persistence (`minibox_workload_save`), which writes to a temporary file and invokes POSIX `rename()` to guarantee atomic inode replacement. Workload teardown (`cleanup`) is a sequential cleanup procedure that safely reaps processes and removes directories.

---

## Measurement Boundaries & Limitations

1. **Unprivileged Operation:** All measurements operated without `root` or `sudo` privileges. Resource limits were managed through unprivileged cgroups v2 (`user.slice`).
2. **Timer Precision:** Wall-clock intervals were sampled using `time.perf_counter_ns()` with sub-microsecond resolution. Process CPU time utilized `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`.
3. **CFS Granularity:** Quota precision is governed by the kernel timer frequency (`CONFIG_HZ=1000` on Fedora Linux), resulting in ~1 ms scheduling slice resolution.
