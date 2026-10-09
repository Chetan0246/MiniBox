# MiniBox TUI: Testing Strategy & Verification Report

## 1. Testing Philosophy & Verification Strategy

MiniBox employs a multi-tiered testing strategy ensuring that every layer—from raw kernel system calls up to interactive Textual pilot actions—is rigorously validated without simulated or synthetic mocks.

1. **System Call Integrity**: Every process launch verifies native `fork()`, error-pipe synchronization, and `waitpid()` exit status harvesting.
2. **Kernel Enforcement Verification**: Resource limits are asserted against actual Linux kernel responses:
   - `cpu.max`: Asserted via monotonic clock CPU time calculations against configured CFS quotas.
   - `memory.max`: Asserted via real kernel OOM killer termination (`SIGKILL` / status code 137).
   - `pids.max`: Asserted via genuine `fork()` returning `-1` with `errno == EAGAIN`.
3. **Zero-Resource Leakage**: All tests rigorously verify that zero zombie processes, lingering background supervisors, or orphaned cgroup directories remain after execution.
4. **End-to-End Headless TUI Testing**: The interactive interface is exercised via pytest using Textual's asynchronous headless pilot runner.

---

## 2. Test Plan Matrix (T01 – T15)

The following table documents the test scenarios defined in Section 10 of `plan.md` alongside their implementation, expected outcomes, and actual verified results:

| ID | Test Scenario | Implementation / Command | Expected Result | Actual Result & Evidence | Status |
|---|---|---|---|---|---|
| **T01** | Valid short program execution | `minibox run /usr/bin/true` | Program exits with code 0; exit status cleanly collected | Exit code 0 recorded; state `COMPLETED` | **PASS** |
| **T02** | Nonexistent executable | `minibox run /nonexistent/binary` | Immediate error; state transitions to `FAILED`; no false `RUNNING` | Returned exit code 1; `exec_errno=2` (`ENOENT`); state `FAILED` | **PASS** |
| **T03** | Nonzero exit code collection | `minibox run ./bin/examples/exit_code 42` | Status 42 recorded in workload record | `exit_code: 42`; state `COMPLETED` | **PASS** |
| **T04** | Stop long-running workload | `minibox start sleeper 100` followed by `minibox stop <id>` | Graceful `SIGTERM` sent; process terminated and reaped | Signal 15 dispatched; reaped cleanly; zero zombies | **PASS** |
| **T05** | Multiple independent workloads | Launch 3 background workloads simultaneously | Each workload receives isolated ID, state file, and log | Separate UUIDs generated; state files distinct; zero collision | **PASS** |
| **T06** | UTS namespace isolation | `minibox run --uts testbox /bin/hostname` | Workload reports `testbox`; host hostname unchanged | Workload saw `testbox`; host remained `fedora` | **PASS** |
| **T07** | PID namespace isolation | `minibox run --pid-ns check_isolation` | Process executes as PID 1 in container namespace | Confirmed process PID == 1 in child namespace | **PASS** |
| **T08** | Memory limit enforcement | `minibox run --memory 16M memory_hog 64` | Linux kernel OOM killer terminates process via `SIGKILL` | Terminated by `SIGKILL` (signal 9, exit code 137) | **PASS** |
| **T09** | CPU CFS bandwidth quota | `minibox run --cpu 50 cpu_hog 2` | Single core throttled to ~50% utilization over 2.0s | Measured 50.5% CPU utilization (1.02s CPU / 2.03s wall) | **PASS** |
| **T10** | Process count limit (`pids.max`) | `minibox run --pids 5 fork_test 10` | 5th fork fails with `EAGAIN`; workload exits code 0 | 5th fork rejected by kernel (`EAGAIN`); exit code 0 | **PASS** |
| **T11** | Unprivileged capability check | `minibox capabilities --json` | Accurately reports available user namespaces and cgroup paths | Reported `user_namespaces: true`, `cgroups_v2: true` | **PASS** |
| **T12** | Background supervision resilience | Start background container, exit CLI launcher | Supervisor process continues independently | Workload continued running until normal completion | **PASS** |
| **T13** | Repeated launch & stop cycles | 10 rapid iterations of start $\rightarrow$ stop $\rightarrow$ cleanup | Zero leaked processes, descriptors, or cgroup directories | Cleaned up 10/10 workloads; zero leftover cgroups | **PASS** |
| **T14** | TUI Responsiveness & Layout | Headless Textual pilot testing via pytest | Modals open and close cleanly without interface lockup | 7/7 Textual pilot tests passed with 100% success | **PASS** |
| **T15** | Performance benchmark suite | `python3 benchmarks/benchmark.py --trials 10` | Full report and empirical results generated and preserved | Generated authoritative `results.json` and `PERFORMANCE_REPORT.md` | **PASS** |

---

## 3. Automated Test Suites Summary

The project includes dedicated automated bash test suites executed via `make test`:

### Phase 1: Core Process Launcher (`tests/test_phase1.sh`)
- Tests basic execution, CLI version, argument passing, error pipes, nonzero exit codes, invalid binaries, crash handling, and zero zombie guarantees.
- **Result**: 12/12 passed (100%).

### Phase 2: State Machine & Background Supervisor (`tests/test_phase2.sh`)
- Tests workload initialization, atomic state file saves, background daemonization, log redirection, `inspect`, `stop`, `delete`, and bulk `cleanup`.
- **Result**: 12/12 passed (100%).

### Phase 3: Linux Namespaces (`tests/test_phase3.sh`)
- Tests capability detection, UTS isolation (hostname mutation), unprivileged User namespaces, PID namespaces (PID 1 verification), and Mount namespaces (`/proc` remounting).
- **Result**: 9/9 passed (100%).

### Phase 4: cgroups v2 Resource Controls (`tests/test_phase4.sh`)
- Tests cgroups v2 controller detection, subtree delegation, CFS `cpu.max` throttling, `memory.max` OOM killing, and `pids.max` fork restriction.
- **Result**: 14/14 passed (100%).

### Phase 5: Resource Monitoring & Telemetry (`tests/test_phase5.sh`)
- Tests host resource telemetry (`/proc/meminfo`, `/proc/loadavg`), workload CPU % rate calculation, memory RSS/peak tracking, CFS throttle statistics, and strict JSON output parsing.
- **Result**: 16/16 passed (100%).

### Phase 6: Terminal User Interface (`tests/test_phase6.sh`)
- Tests launcher script, CLI version/help, end-to-end client communication, and 7 headless Textual pilot tests (`tests/test_tui.py`).
- **Result**: 6/6 test groups passed (100%).

### Phase 7: Testing & Performance Evaluation (`tests/test_phase7.sh`)
- Validates benchmark runner, schema compliance, behavioral enforcement assertions (CFS quota boundaries, memory OOM kill, PID limits), and zero leftover resources.
- **Result**: 5/5 passed (100%).

### Phase 8: Final Integration & Documentation (`tests/test_phase8.sh`)
- Verifies documentation completeness, user manual, architecture specs, viva preparation notes, demonstration scripts, clean builds, and acceptance criteria.
- **Result**: 5/5 passed (100%).

### Behavioral Verification Suite (`tests/test_behavioral.sh`)
- Directly verifies PID 1 execution in PID namespaces, isolated CFS 50% CPU quota throttling without namespace race escapes, strict malformed/negative/zero/overflow input validation, kernel OOM memory killer termination, kernel PID fork bomb restriction (`EAGAIN`), JSON serialization resilience with special characters (quotes, backslashes, spaces), directory traversal rejection on deletion, live host CPU interval delta rate measurement, granular capability detection, and TUI quoting-aware argument splitting (`shlex.split`).
- **Result**: 10/10 passed (100%).

---

## 4. Execution Evidence & Commands

To execute the complete test suite:
```bash
make clean && make && make test
```

All 9 test suites execute sequentially and report unambiguous PASS/FAIL status across 89 verified test assertions. Zero tests are mocked or bypassed.
