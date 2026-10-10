# MiniBox Performance Evaluation & Benchmark Report

**Generated on:** 2026-10-10T12:06:04.128476  
**Runtime:** minibox version 0.8.0-phase8 (Phase 8: Final Integration & Educational Deliverables)  
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
| **Direct Host Execution** | 0.84 | 0.91 | 1.04 | 1.59 | 0.26 | Baseline (1.0x) |
| **MiniBox Foreground Baseline** | 2.60 | 2.94 | 3.09 | 3.78 | 0.40 | +2.04 ms (3.2x) |
| **MiniBox Isolated (UTS+PID+Mount)** | 2.66 | 3.33 | 3.43 | 4.46 | 0.59 | +2.42 ms (3.7x) |
| **MiniBox Full Isolation + cgroups v2** | 3.34 | 5.02 | 5.86 | 14.68 | 3.25 | +4.11 ms (5.5x) |

### Launch Latency Reconciliation & Analysis:
- **Direct Execution Baseline:** Direct execution takes ~0.5–1.5 ms depending on dynamic linker page-in and CPU frequency governor scaling (median: 0.91 ms).
- **Namespace Isolation Cost:** Running `unshare(CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS)` and establishing user/group ID mappings adds approximately 0.5–1.0 ms of kernel setup overhead (median: 3.33 ms).
- **cgroups v2 Cold vs Warm Variance:**
  - On the initial invocation (cold start), creating the cgroup directory under `/sys/fs/cgroup/user.slice/...`, writing controller configurations, and migrating the process into `cgroup.procs` takes ~10–18 ms (measured max: 14.68 ms) due to filesystem directory allocation and kernel cgroup tree synchronization.
  - On subsequent warm executions, the operation executes in 3.34–6.26 ms (measured min: 3.34 ms).
  - In small-trial runs ($N=2$), the cold-start outlier heavily skews the calculated median (e.g. [2.2 ms, 18.2 ms] yields a 10.22 ms median; [2.2 ms, 12.5 ms] yields 7.37 ms; [2.0 ms, 10.8 ms] yields 6.44 ms).
  - Across 10 repetitions, the median steady-state latency stabilizes at 5.02 ms (+4.11 ms overhead vs direct), with warm executions consistently completing in 3.34–6.26 ms.

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
| **Direct Host** | 100% (1 core) | 2.00s | 1.98s | 99.1% | 100% | None |
| **MiniBox Unconstrained** | 100% (1 core) | 2.00s | 1.99s | 99.3% | 100% | None |
| **MiniBox CFS 50% Quota** | 50% (50ms / 100ms) | 2.03s | 1.03s | 50.5% | 50.0% | Active (50.5%) |
| **MiniBox CFS 25% Quota** | 25% (25ms / 100ms) | 2.02s | 0.50s | 25.1% | 25.0% | Active (25.1%) |

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
| **Direct Host Execution** | None | 48 MiB | 564.92 | 566.03 | 567.22 | 2.52 |
| **MiniBox Unconstrained** | None | 48 MiB | 562.34 | 568.87 | 568.79 | 3.04 |
| **MiniBox Limited (64M Ceiling)** | 64 MiB | 48 MiB | 576.91 | 582.26 | 582.33 | 4.05 |

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
| **Direct Host** | 10 | Unlimited | All 10 forks succeeded | 2 | Unconstrained | 504.13 |
| **MiniBox Unconstrained** | 10 | Unlimited | All 10 forks succeeded | 2 | Unconstrained | 506.40 |
| **MiniBox Constrained (`--pids 5`)** | 10 | 5 PIDs | 5th fork rejected (`EAGAIN`) | 0 | **Kernel Enforced** | 515.96 |

### Genuine Kernel Enforcement Verification:
- With `pids.max = 5`, the cgroup accommodates 1 parent process and 4 child processes.
- The 5th child fork attempts to create a 6th process in the cgroup. The kernel checks `pids.current >= pids.max` and returns `-1` with `errno = EAGAIN`.
- The test workload detects `EAGAIN`, cleans up existing children with `waitpid()`, and exits with code 0.

---

## Experiment 5: Background Lifecycle & Supervision Latency

Measures individual supervisor lifecycle operations across 10 repetitions.

| Operation | Description | Median Latency (ms) | StdDev (ms) | Execution Semantics |
|---|---|---|---|---|
| **`start`** | Double-fork supervisor launch, isolation init, cgroup binding | 3.73 | 0.10 | Asynchronous daemonization |
| **`inspect`** | Atomic state file load via POSIX `rename()`, procfs check | 1.73 | 0.13 | Read-only inspect |
| **`stop`** | Graceful SIGTERM dispatch, supervisor waitpid reaping | 42.68 | 0.25 | Zero-zombie harvest |
| **`cleanup`** | Sequential cgroup `rmdir` and workload directory removal | 2.33 | 5.83 | Orderly teardown |

*Note on Atomicity:* The term "atomic" applies strictly to the state file persistence (`minibox_workload_save`), which writes to a temporary file and invokes POSIX `rename()` to guarantee atomic inode replacement. Workload teardown (`cleanup`) is a sequential cleanup procedure that safely reaps processes and removes directories.

---

## Measurement Boundaries & Limitations

1. **Unprivileged Operation:** All measurements operated without `root` or `sudo` privileges. Resource limits were managed through unprivileged cgroups v2 (`user.slice`).
2. **Timer Precision:** Wall-clock intervals were sampled using `time.perf_counter_ns()` with sub-microsecond resolution. Process CPU time utilized `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`.
3. **CFS Granularity:** Quota precision is governed by the kernel timer frequency (`CONFIG_HZ=1000` on Fedora Linux), resulting in ~1 ms scheduling slice resolution.
