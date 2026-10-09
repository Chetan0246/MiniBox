# MiniBox TUI — Project Implementation Plan

## 1. Project Overview

**Project Title:** MiniBox TUI: An Interactive Lightweight Linux Container Runtime for Process Isolation and Resource Management

**Domain:** Operating Systems, Linux System Programming, Process Management, Resource Allocation, Inter-Process Communication, System Monitoring.

### Problem Statement

Modern container platforms provide process isolation and resource management, but hide many of the underlying operating system mechanisms. Students often use containers without understanding how processes are created, isolated, restricted and monitored.

MiniBox is a lightweight educational Linux container runtime that exposes these mechanisms through an interactive terminal interface. It allows users to launch programs, configure resource limits, inspect their execution, monitor resource usage and examine the lifecycle of isolated processes.

### Primary Objective

Build a working Linux application that demonstrates practical operating system concepts through a C-based runtime and an interactive terminal user interface.

### Design Philosophy

- Simple enough for a student to understand, demonstrate and defend in a viva.
- Advanced enough to include genuine Linux system calls, isolation, resource controls, monitoring and performance evaluation.
- Interactive by default: the user should not need to type a separate Bash command for every operation.
- Modular, testable, documented and reproducible.
- Never simulate an OS feature and claim that it is implemented when it is not.

## 2. Technology Stack

Use the following stack unless a genuine compatibility issue requires a documented alternative.

| Component | Technology |
|---|---|
| Core runtime | C (C11 or later) |
| System interface | Linux system calls and Linux-specific APIs |
| Terminal UI | Python 3 + Textual |
| Runtime communication | JSON Lines over standard input/output or a documented local IPC mechanism |
| Process inspection | `/proc` and `waitpid()` |
| Resource controls | cgroups v2 where available |
| Isolation | Linux namespaces, using supported and permitted configurations |
| Build system | Make |
| C testing | CTest is not required; use a lightweight test runner or shell-based integration tests |
| Python testing | pytest |
| Data format | JSON for structured status and reports |
| Documentation | Markdown |

Keep the C runtime independent of the Textual interface. The runtime must remain testable even when the TUI is unavailable.

Use the Python standard library wherever possible. Textual may be installed in a virtual environment.

## 3. Project Scope

### Mandatory features

1. Interactive terminal dashboard.
2. Workload creation and execution.
3. Process creation using `fork()` and program execution using `execvp()` or a suitable `exec` variant.
4. Process waiting and exit-status collection using `waitpid()`.
5. Process lifecycle management and signal handling.
6. At least one working Linux namespace isolation mechanism.
7. CPU, memory and process-count limits using cgroups v2 where permissions and system configuration allow.
8. Process and resource monitoring.
9. Graceful termination and cleanup.
10. Structured logs and error messages.
11. Automated tests and performance evaluation.
12. Architecture diagrams, user documentation and a demonstration scenario.

### Optional features — only after mandatory features work

- Resource usage graphs in the terminal.
- A process tree view.
- Workload search and filtering.
- JSON export of benchmark results.
- A small educational explanation panel showing which OS mechanisms are active.
- A read-only system capability report.

### Explicitly out of scope

Do not implement the following unless every mandatory feature is complete:

- A production replacement for Docker or Podman.
- Kubernetes or container orchestration.
- Image registries or image downloading.
- A custom Linux kernel.
- A custom filesystem driver.
- Advanced container networking.
- Overlay filesystem layers.
- A web dashboard.
- AI features unrelated to the operating system project.
- Complex distributed or multi-host execution.

Do not sacrifice correctness and test coverage to add decorative features.

## 4. High-Level Architecture

The system consists of five main components.

```text
+---------------------------------------------------+
|                MiniBox Terminal UI                |
| Dashboard | Workloads | Resources | Logs | Tests  |
+-------------------------+-------------------------+
                          |
                          v
+---------------------------------------------------+
|               Runtime Client / Protocol           |
| Requests | Responses | Validation | Error Handling |
+-------------------------+-------------------------+
                          |
                          v
+---------------------------------------------------+
|                  MiniBox Core (C)                 |
|                                                   |
|  +-------------+  +-------------+                 |
|  | Process     |  | Isolation   |                 |
|  | Manager     |  | Manager     |                 |
|  +-------------+  +-------------+                 |
|                                                   |
|  +-------------+  +-------------+                 |
|  | Resource    |  | Lifecycle   |                 |
|  | Manager     |  | Manager     |                 |
|  +-------------+  +-------------+                 |
+-------------------------+-------------------------+
                          |
                          v
+---------------------------------------------------+
|                 Linux Operating System            |
| fork/exec/wait | namespaces | cgroups v2 | /proc   |
| signals | pipes | file descriptors | scheduling   |
+---------------------------------------------------+
```

### Component responsibilities

**Terminal UI**
- Display active workloads, their states, PIDs, runtime and resource usage.
- Provide forms and keyboard actions to launch and stop workloads.
- Show logs, errors, configuration and system capabilities.
- Refresh monitoring data periodically without freezing the interface.

**Runtime client**
- Communicate with the core using a defined protocol.
- Validate inputs before submitting requests.
- Display runtime errors without crashing the interface.
- Never parse human-readable log messages as protocol responses.

**Process manager**
- Create child processes.
- Execute programs.
- collect exit status.
- Track process IDs and lifecycle transitions.
- Handle signals and failed executions.

**Isolation manager**
- Set up supported namespaces.
- Configure an isolated hostname where applicable.
- Set up a restricted working directory or filesystem view only where implemented and tested.
- Report unsupported or unavailable isolation features accurately.

**Resource manager**
- Apply cgroup v2 CPU, memory and process-count controls when supported.
- Read resource statistics.
- Report permission failures and unsupported controllers.

**Lifecycle manager**
- Coordinate launch, readiness, running, stopping, completion, failure and cleanup.
- Ensure resources are released after normal exit and errors.

## 5. Terminal Interface Specification

The main command to launch the application must be:

```bash
./minibox-tui
```

The user should land in an interactive full-screen terminal interface.

### Main dashboard

Design a polished, dark terminal dashboard with clear sections, compact tables, status indicators and a consistent visual hierarchy.

Suggested layout:

```text
┌────────────────────── MINIBOX TUI ──────────────────────┐
│ Lightweight Linux Container Runtime                     │
├─────────────────────────────────────────────────────────┤
│ Runtime: READY     Isolation: AVAILABLE    cgroups: OK  │
├───────────────────┬─────────────────────────────────────┤
│ SYSTEM             │ ACTIVE WORKLOADS                   │
│ CPU usage          │ ID   NAME       PID   STATE        │
│ Memory usage       │ 01   demo       4210  RUNNING      │
│ Process count      │ 02   cpu-test   4217  RUNNING      │
│                    │                                     │
├───────────────────┴─────────────────────────────────────┤
│ RECENT EVENTS                                           │
│ [INFO] Workload demo started                            │
│ [INFO] Resource monitoring initialized                  │
├─────────────────────────────────────────────────────────┤
│ [N] New  [Enter] Inspect  [S] Stop  [L] Logs  [Q] Quit  │
└─────────────────────────────────────────────────────────┘
```

This is a design reference, not a requirement to reproduce every pixel.

### Required screens

**1. Dashboard**
- System capability status.
- Active workload table.
- Current resource statistics.
- Recent event log.
- Keyboard shortcut help.

**2. Create Workload**
- Program path or executable.
- Optional arguments.
- Workload name.
- Working directory.
- CPU limit.
- Memory limit.
- Maximum process count.
- Isolation options supported by the runtime.
- Configuration validation before launch.

**3. Workload Details**
- Workload ID and PID.
- Lifecycle state.
- Start time and elapsed runtime.
- Configured limits.
- Current resource usage.
- Exit code or termination signal, when available.
- Active isolation features.
- Actions to stop or inspect the workload.

**4. Monitoring**
- CPU usage.
- Memory consumption.
- Process count.
- Runtime history or a compact text graph.
- Refresh indicator and last update time.
- Clear distinction between host-wide and workload-specific statistics.

**5. Logs and Events**
- Timestamped events.
- Runtime errors.
- Workload creation and termination.
- Resource-limit setup results.
- Cleanup results.
- Filters by workload and severity where practical.

**6. Experiments and Benchmarks**
- Select predefined test programs.
- Run normal versus MiniBox execution.
- Display elapsed time and resource measurements.
- Export results as JSON or CSV.
- Clearly label failed or incomplete experiments.

### Interaction requirements

- Support keyboard navigation and visible shortcut hints.
- Do not require the user to manually type a shell command for each workload operation.
- Use forms, selection lists, tables and confirmation prompts.
- Show a progress or status indicator during lengthy operations.
- Never block the UI while waiting for a long-running workload.
- Require confirmation before terminating a workload.
- Show helpful errors instead of raw stack traces.
- Handle terminal resize and application shutdown gracefully.

The TUI may internally call the runtime executable. That is acceptable: the interface must make these operations available interactively.

## 6. Core Runtime Requirements

### 6.1 Process Management

Implement:

- Child process creation with `fork()`.
- Program execution with `execvp()` or another suitable `exec` function.
- Parent-child synchronization using `waitpid()`.
- Controlled termination using signals.
- Exit-code collection.
- Handling for nonexistent executables.
- Handling for fork failures and interrupted system calls.
- Separation of the runtime manager from workload output.

A failed `exec` must be reported to the parent through a dedicated error pipe or another reliable mechanism. Do not assume that a child successfully started merely because `fork()` returned successfully.

Demonstrate these concepts using actual executable programs.

### 6.2 Process Lifecycle

Define and document a state machine. A suggested lifecycle is:

```text
CREATED
   |
   v
STARTING
   |
   +------ failure ------> FAILED
   |
   v
RUNNING
   |
   +------ stop request -> STOPPING
   |                         |
   v                         v
COMPLETED                 STOPPING
                              |
                              v
                         TERMINATED
                              |
                              v
                           CLEANUP
```

Implement clear, testable transitions. The runtime must not claim a workload is running when it has already exited.

Track exit status, termination signal and failure reason where available.

### 6.3 Namespace Isolation

Implement a carefully scoped subset of Linux namespaces.

Prioritize:

1. UTS namespace for an isolated hostname.
2. PID namespace if the supported launch path and privileges permit it.
3. Mount namespace only with a safe and documented mount setup.

Use Linux APIs such as `clone()` or `unshare()` as appropriate for the selected architecture. The agent must choose and document a correct initialization sequence, including PID namespace child-reaping requirements.

Do not claim full container isolation. Report exactly which namespaces are enabled.

Important constraints:
- Namespace availability depends on kernel configuration and user permissions.
- Do not assume unprivileged namespaces are enabled.
- Do not silently fall back to a non-isolated workload while displaying an "isolated" status.
- Do not remount or alter host filesystems unnecessarily.
- Do not make host directories writable from the workload.
- Do not run arbitrary user workloads as root merely to bypass permission errors.

If a feature cannot be safely supported on the current machine, show it as unavailable and document the reason.

### 6.4 Filesystem and Working Directory

Start with a configurable working directory and controlled file-access behavior.

Only implement a restricted filesystem view if it can be set up and verified correctly. A changed working directory alone is not filesystem isolation and must not be labelled as such.

If a mount namespace is implemented, document its mount setup and ensure that cleanup cannot accidentally modify the host filesystem.

Avoid implementing a custom filesystem or complex overlay support.

### 6.5 Resource Management

Use cgroups v2 when available and writable.

Support the following resource controls where available:

- CPU quota or relative CPU allocation through the appropriate cgroup controller.
- Memory limit using `memory.max`.
- Process-count limit using `pids.max`.

For example, a user should be able to configure a workload with:
- CPU limit: 30% of one CPU's quota.
- Memory limit: 256 MiB.
- Maximum process count: 20.

Document exactly how the CPU value is interpreted. Do not confuse CPU quota with a guarantee of CPU time.

The runtime must:
- Detect cgroup v2.
- Detect controller availability and permissions.
- Validate limit values.
- Apply limits before starting the workload whenever the implementation permits.
- Read back configured limits to verify successful application.
- Report unsupported controls explicitly.
- Remove the workload's cgroup after all relevant processes have exited.

Do not write arbitrary cgroup settings or modify system-wide configurations automatically. Never disable host protections to make a demo work.

If cgroups cannot be used, retain the rest of the application and show the limitation. Do not simulate enforcement.

### 6.6 Monitoring

Collect measurements using appropriate Linux interfaces, including `/proc` and cgroup v2 files.

Show:
- PID or workload identifier.
- Workload state.
- Elapsed runtime.
- Process count.
- Memory usage.
- CPU usage or CPU time.
- Configured limits.
- Recent lifecycle events.

Use monotonic time for duration measurements.

Calculate CPU utilization carefully using CPU-time deltas and elapsed wall-clock time. Handle counter resets, exited processes and missing metrics.

Label host-wide metrics separately from workload-specific metrics. Do not fabricate values for unsupported metrics.

### 6.7 Inter-Process Communication

Use a pipe or another documented IPC mechanism to demonstrate communication between the manager and the workload launcher.

Use IPC for a specific purpose, such as:
- Reporting initialization failures.
- Reporting readiness.
- Sending structured lifecycle information.

Do not add IPC merely as a decorative feature. Explain its purpose in the report and viva.

### 6.8 Cleanup and Error Handling

Cleanup is a core requirement, not an optional feature.

Handle:
- Invalid executable paths.
- Invalid arguments and limits.
- Child process crashes.
- Normal completion.
- User-requested termination.
- Interrupted system calls.
- Namespace initialization failure.
- Resource-controller permission failures.
- UI disconnection or exit.
- Partial initialization.

Ensure that:
- No unnecessary child processes remain.
- Open file descriptors are closed.
- Temporary files and pipes are cleaned up.
- Created cgroups are removed when safe.
- A failed initialization does not leave an apparently running workload.
- The runtime does not kill unrelated host processes.

## 7. Runtime Protocol

Define a small, documented protocol between the TUI and C runtime.

Use JSON Lines or another explicit structured format.

Example request:

```json
{
  "action": "run",
  "name": "demo",
  "program": "/usr/bin/sleep",
  "args": ["30"],
  "limits": {
    "cpu_percent": 30,
    "memory_mib": 256,
    "pids_max": 20
  }
}
```

Example response:

```json
{
  "ok": true,
  "workload_id": "demo-001",
  "state": "RUNNING",
  "pid": 4210
}
```

These examples define the protocol shape, not a promise that the values will work on every machine.

Implement and document operations for:
- Capability detection.
- Workload creation.
- Workload listing.
- Workload inspection.
- Workload termination.
- Statistics retrieval.
- Log retrieval.
- Benchmark execution.

Validate all inputs and return structured errors. Keep protocol output separate from diagnostic output so that compiler messages and debug logs cannot corrupt JSON responses.

## 8. Suggested Repository Structure

```text
minibox/
├── plan.md
├── README.md
├── Makefile
├── .gitignore
├── LICENSE
│
├── runtime/
│   ├── include/
│   │   ├── minibox.h
│   │   ├── process.h
│   │   ├── isolation.h
│   │   ├── resources.h
│   │   ├── monitor.h
│   │   └── lifecycle.h
│   ├── src/
│   │   ├── main.c
│   │   ├── process.c
│   │   ├── isolation.c
│   │   ├── resources.c
│   │   ├── monitor.c
│   │   ├── lifecycle.c
│   │   ├── protocol.c
│   │   └── cleanup.c
│   └── tests/
│
├── tui/
│   ├── app.py
│   ├── screens/
│   ├── widgets/
│   ├── services/
│   ├── models/
│   ├── requirements.txt
│   └── tests/
│
├── examples/
│   ├── hello.c
│   ├── cpu_hog.c
│   ├── memory_hog.c
│   ├── fork_test.c
│   └── crash_test.c
│
├── scripts/
│   ├── setup_dev.sh
│   ├── run_tests.sh
│   └── benchmark.sh
│
├── docs/
│   ├── architecture.md
│   ├── design.md
│   ├── user_manual.md
│   ├── testing.md
│   ├── performance_report.md
│   ├── viva_questions.md
│   └── diagrams/
│
├── results/
│   ├── benchmark_results.json
│   └── test_summary.md
│
└── build/
```

Adapt the structure when necessary, but maintain clear separation between the core runtime, UI, tests and documentation.

Do not create empty modules just to match the directory tree. Each module must have a defined responsibility.

## 9. Development Phases

Implement the project in the following order. Do not generate the entire project in one untested step.

### Phase 1 — Environment and Minimal Runtime

Tasks:
1. Inspect the operating system and compiler.
2. Check required development tools.
3. Create the repository structure.
4. Implement the Makefile.
5. Build a minimal C program launcher using `fork()`, `exec()` and `waitpid()`.
6. Implement exit-status reporting and error handling.
7. Create a small example executable.
8. Write initial tests.

Acceptance criteria:
- The project compiles successfully.
- A valid program starts and exits.
- An invalid executable generates a useful error.
- The parent collects the child exit status.
- No zombie process remains after a completed workload.

### Phase 2 — Lifecycle and Process Management

Tasks:
1. Introduce workload records and unique identifiers.
2. Implement lifecycle states.
3. Implement status reporting and controlled termination.
4. Add signal handling.
5. Implement robust cleanup.
6. Add tests for success, failure, interruption and termination.

Acceptance criteria:
- Workloads have traceable states.
- The runtime can report completion and failure.
- Stopping a workload does not terminate unrelated processes.
- Cleanup works after normal exit and initialization failure.

### Phase 3 — Linux Isolation

Tasks:
1. Implement capability detection.
2. Implement UTS namespace isolation.
3. Implement PID namespace isolation where supported.
4. Implement a safe mount namespace configuration if feasible.
5. Add verification that each enabled feature is actually active.
6. Document permission and kernel limitations.

Acceptance criteria:
- The application reports active and unavailable isolation features correctly.
- Tests verify the observable effect of each supported namespace.
- Failed isolation setup is not reported as successful isolation.
- The ordinary workload path still works when optional isolation is unavailable.

### Phase 4 — Resource Controls

Tasks:
1. Detect cgroup v2.
2. Detect controller availability and write permissions.
3. Implement CPU, memory and process-count limit configuration.
4. Read configured values back.
5. Capture resource-limit failures.
6. Clean up per-workload cgroups safely.
7. Add integration tests.

Acceptance criteria:
- Configured values are verified against the kernel interface.
- Unsupported controls are reported accurately.
- At least one resource-limit experiment demonstrates genuine kernel enforcement on a compatible environment.
- No system-wide cgroup configuration is changed automatically.

### Phase 5 — Monitoring

Tasks:
1. Read process statistics from `/proc`.
2. Read cgroup statistics where available.
3. Calculate elapsed runtime and CPU usage.
4. Collect process-count and memory measurements.
5. Expose structured monitoring data to the UI.
6. Test exited processes and unavailable metrics.

Acceptance criteria:
- Measurements update while workloads are running.
- The UI distinguishes host and workload metrics.
- Missing values are shown as unavailable, not invented.
- Monitoring does not block workload execution.

### Phase 6 — Interactive Terminal UI

Tasks:
1. Create the Textual application.
2. Implement the main dashboard.
3. Add the workload creation form.
4. Add workload inspection and stop actions.
5. Add live monitoring.
6. Add logs and event history.
7. Add capability status and helpful error displays.
8. Add keyboard navigation, help and confirmation prompts.
9. Add graceful UI shutdown.

Acceptance criteria:
- Running `./minibox-tui` launches the full-screen interface.
- A user can create, inspect and stop a workload without manually typing a separate Bash command for each operation.
- The UI remains responsive while workloads run.
- Errors are understandable.
- Workload state in the UI agrees with the core runtime.

### Phase 7 — Testing and Performance Evaluation

Tasks:
1. Create repeatable workload tests.
2. Compare direct execution with MiniBox execution.
3. Test resource limits on compatible systems.
4. Record measurements.
5. Generate tables and graphs where useful.
6. Document the test environment and limitations.

Acceptance criteria:
- Tests are repeatable.
- Results come from actual measurements.
- Failures are included in the test summary.
- Performance conclusions are supported by the recorded data.

### Phase 8 — Final Integration and Submission

Tasks:
1. Perform a clean build.
2. Run all automated tests.
3. Fix major warnings and errors.
4. Test startup, workload launch, monitoring and shutdown.
5. Prepare architecture and lifecycle diagrams.
6. Write the user manual and technical report.
7. Prepare a short demonstration.
8. Prepare viva questions and answers.

Acceptance criteria:
- A new user can follow the README to install and run the project.
- The demo works on the documented supported environment.
- The documentation accurately describes implemented features and limitations.
- No major feature is represented as complete without a test or demonstration.

## 10. Test Plan

Create test cases for at least the following scenarios.

| ID | Test | Expected result |
|---|---|---|
| T01 | Run a valid short program | Program completes and exit status is collected |
| T02 | Run a nonexistent executable | Clear error; no false RUNNING state |
| T03 | Run a program that exits nonzero | Nonzero status is recorded |
| T04 | Stop a long-running workload | Workload terminates and cleanup occurs |
| T05 | Launch multiple workloads | Each workload has a separate record |
| T06 | Test UTS namespace | Hostname isolation is verified when supported |
| T07 | Test PID namespace | Process visibility is verified when supported |
| T08 | Apply memory limit | Limit is verified and behavior is recorded |
| T09 | Apply CPU quota | Configuration and observed behavior are recorded |
| T10 | Apply process-count limit | Excess process creation is restricted when supported |
| T11 | Run without cgroup permissions | Clear capability warning; no fake enforcement |
| T12 | Exit the UI during execution | Runtime behavior is documented and managed safely |
| T13 | Test repeated launch/stop cycles | No accumulating leaked processes or resources |
| T14 | Resize terminal | Interface remains usable |
| T15 | Run the benchmark | Results and environment information are saved |

Some tests require a compatible kernel and appropriate permissions. Mark such tests as blocked or unsupported when necessary, with the exact reason.

## 11. Performance Evaluation

Compare direct execution and MiniBox execution for selected workloads.

Measure:
- Startup latency.
- Wall-clock completion time.
- CPU time or utilization.
- Peak or sampled memory usage.
- Process count.
- Resource-control behavior.
- Cleanup time where meaningful.

Use multiple trials and report the median and, where useful, spread or variability.

For every experiment record:
- CPU and RAM configuration.
- Linux kernel version.
- Compiler and build mode.
- Workload parameters.
- Resource limits.
- Number of trials.
- Measurement method.
- Results and limitations.

Do not invent measurements or promise that MiniBox is faster than direct execution. Isolation and monitoring may introduce overhead. Explain that overhead and the value of resource control.

## 12. Safety and Reliability Requirements

- Never run commands with `sudo` automatically.
- Never alter host security settings to bypass restrictions.
- Never kill processes by matching broad names.
- Track the PIDs and process groups or other identifiers owned by MiniBox.
- Validate paths and resource values.
- Avoid shell-string construction for launching user programs; use argument arrays and `exec` functions.
- Avoid arbitrary recursive deletion.
- Use dedicated temporary locations and verify ownership before cleanup.
- Do not expose a writable host filesystem to workloads without an explicit, documented requirement.
- Keep logs and protocol output separate.
- Handle signals and partial initialization.
- Never claim isolation or resource enforcement unless verified.
- Document permission requirements and safe test environments.

## 13. Build and Run Experience

Provide a simple setup experience.

The intended user workflow is:

```bash
# Build the C runtime
make

# Set up the Python UI environment
python3 -m venv .venv
. .venv/bin/activate
pip install -r tui/requirements.txt

# Launch the interactive interface
./minibox-tui
```

The final launcher may be a small executable shell script or Python entry point. It must locate the built runtime reliably and display helpful errors when dependencies are missing.

The exact setup commands may be adjusted for the target Linux distribution. Document any required development packages and permissions.

Inside the interface, the user should be able to:
1. Inspect runtime capabilities.
2. Create a workload.
3. Configure its supported limits.
4. Start it.
5. Observe status and statistics.
6. Stop it.
7. Inspect the exit result and logs.
8. Run a benchmark.
9. Export results.
10. Quit cleanly.

## 14. Documentation Deliverables

Create the following deliverables.

### README.md
Include:
- Project overview.
- Problem statement.
- Features.
- Screenshots or terminal captures.
- Architecture.
- Supported Linux environment.
- Installation and launch instructions.
- Limitations.
- Test instructions.

### docs/architecture.md
Explain the UI, protocol, runtime modules and Linux interfaces.

### docs/design.md
Include:
- Requirements.
- Module responsibilities.
- Lifecycle state machine.
- Process-launch sequence.
- Namespace initialization sequence.
- Resource-control sequence.
- Error-handling design.

### docs/user_manual.md
Explain installation, dashboard navigation, workload creation, monitoring, stopping and troubleshooting.

### docs/testing.md
Include test cases, expected results, actual results and evidence.

### docs/performance_report.md
Include experimental setup, methodology, actual measurements, tables or graphs and interpretation.

### docs/viva_questions.md
Include questions and concise answers about `fork()`, `exec()`, `waitpid()`, namespaces, cgroups, IPC, scheduling, memory, process states, cleanup and performance overhead.

### Final report
Prepare a report aligned with the course requirements, including:
- Abstract.
- Introduction and problem statement.
- Objectives and scope.
- Background and OS concepts.
- System architecture.
- Implementation.
- Test results.
- Performance analysis.
- Limitations.
- Conclusion.
- References.

Do not fabricate literature, benchmark results, screenshots or implementation details.

## 15. AI Coding Agent Working Rules

Follow these instructions throughout development.

1. Inspect the repository before changing files.
2. Read this plan and create a checklist of requirements.
3. Check the environment before assuming dependencies or kernel features exist.
4. Implement one phase at a time.
5. Before each phase, describe the intended changes and files.
6. Write the smallest complete implementation for that phase.
7. Compile and run relevant tests immediately.
8. Fix errors before proceeding.
9. Do not replace working code with a large rewrite without justification.
10. Do not leave placeholder functions or mock OS operations in mandatory features.
11. Do not report tests as passing unless they were actually executed.
12. Clearly distinguish implemented, partially implemented and unsupported features.
13. Keep dependencies minimal.
14. Keep functions small and add comments explaining important system calls and non-obvious logic.
15. Update documentation as implementation changes.
16. Do not start optional polish while mandatory features are broken.
17. After each phase, summarize changed files, tests run, results, limitations and the next step.
18. Preserve existing work and avoid destructive repository operations.

When a permission or kernel limitation blocks a feature, diagnose it and propose a safe alternative. Do not silently bypass the limitation.

## 16. Final Acceptance Criteria

The project is ready for submission only when:

- [ ] The C runtime builds cleanly.
- [ ] Real workloads can be started and their exit status collected.
- [ ] Process lifecycle management works.
- [ ] At least one namespace mechanism is implemented and verified on the supported environment.
- [ ] cgroup-based resource control is implemented and verified where supported.
- [ ] Monitoring shows real measurements.
- [ ] The TUI provides workload creation, inspection and stopping.
- [ ] The interface remains responsive during workload execution.
- [ ] Errors and unsupported features are reported honestly.
- [ ] Cleanup and repeated execution tests pass.
- [ ] Automated tests are documented.
- [ ] Performance experiments contain real measurements.
- [ ] The README and user manual enable reproducible execution.
- [ ] Architecture diagrams and viva notes are complete.
- [ ] The final demonstration covers both the interface and underlying OS mechanisms.

## 17. Final Demonstration Scenario

Prepare one reliable demonstration that follows this sequence:

1. Launch MiniBox TUI.
2. Show detected Linux capabilities.
3. Create a workload named `demo`.
4. Configure a CPU quota and memory limit supported by the environment.
5. Launch a long-running test program.
6. Show its PID, state, runtime and resource measurements.
7. Inspect the workload's isolation and configured limits.
8. Launch a second workload to demonstrate independent tracking.
9. Stop one workload and show the lifecycle transition.
10. Run a resource-limit or process-management test.
11. Open the benchmark screen and show measured results.
12. Show the test summary and explain one observed result.
13. Explain how `fork()`, `exec()`, namespaces, cgroups and `/proc` contribute to the implementation.

The demonstration must use actual runtime behavior. If a particular isolation or resource-control feature is unavailable on the presentation machine, explain the limitation and demonstrate it in a compatible test environment instead.

## 18. Definition of Success

MiniBox is successful when a student can use one interactive terminal application to launch and inspect workloads while clearly demonstrating how Linux process management, isolation, resource allocation, IPC, monitoring and cleanup work underneath.

The goal is not to reproduce every feature of Docker. The goal is to build a small, real and well-tested system that makes operating system mechanisms visible, measurable and understandable.
