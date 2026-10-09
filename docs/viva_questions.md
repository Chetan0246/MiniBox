# MiniBox TUI: Oral Examination (Viva Voce) Preparation Guide

This document contains essential operating systems theory, system programming concepts, and runtime implementation questions with concise, technically rigorous answers.

---

## 1. Process Management & System Calls

### Q1: What happens under the hood during `fork()`?
**Answer**: `fork()` invokes the Linux `clone()` system call. The kernel creates an exact duplicate of the calling process, allocating a new Task Control Block (`task_struct`), assigning a unique Process ID (PID), and copying the parent's memory page tables. Using **Copy-On-Write (COW)**, the physical pages are marked read-only and shared until either the parent or child attempts to write, at which point the kernel duplicates only that page.

### Q2: Why is `execvp()` used instead of `system()`?
**Answer**: `system()` invokes `/bin/sh -c`, which creates a subshell, introduces shell-injection vulnerabilities, alters signal handling, and adds unnecessary process overhead. `execvp()` directly replaces the current process image with the target executable ELF binary, preserving open file descriptors (unless `O_CLOEXEC` is set) without spawning an intermediate shell.

### Q3: How does MiniBox prevent zombie processes?
**Answer**: A zombie process occurs when a child terminates but its parent has not read its exit status via `wait()` or `waitpid()`. In MiniBox:
- In foreground execution, the launcher calls `waitpid(pid, &status, 0)` synchronously until the child exits.
- In background mode, a dedicated supervisor daemon stays alive, waits on the child, collects the exit status, saves the state file, and then terminates cleanly.

### Q4: How is an `execvp()` failure communicated back to the parent?
**Answer**: MiniBox creates an anonymous UNIX pipe with `pipe2(pipefd, O_CLOEXEC)`. The child inherits the write end.
- If `execvp()` succeeds, the file descriptor automatically closes on `exec` due to `O_CLOEXEC`, and the parent reading the pipe receives EOF (`0` bytes).
- If `execvp()` fails, the child writes the system `errno` (4 bytes) into the pipe before calling `_exit(127)`. The parent reads `errno` and immediately transitions the workload to `FAILED`.

---

## 2. Linux Namespaces & Isolation

### Q5: What is the difference between Linux namespaces and cgroups?
**Answer**:
- **Namespaces** govern **visibility** (what a process can see): they partition system resources (hostnames, PIDs, network interfaces, mount points, user IDs).
- **cgroups (Control Groups)** govern **resource utilization** (how much a process can use): they meter and restrict physical resources (CPU bandwidth, memory bytes, I/O bandwidth, PID count).

### Q6: How does MiniBox achieve unprivileged namespace isolation without `sudo`?
**Answer**: MiniBox utilizes **User Namespaces** (`CLONE_NEWUSER`). By invoking `unshare(CLONE_NEWUSER)`, an unprivileged user creates a new user namespace where they possess full root capabilities (`CAP_SYS_ADMIN`) *locally within that namespace*. MiniBox writes UID/GID mappings into `/proc/self/uid_map` and `/proc/self/gid_map`, enabling subsequent creation of UTS (`CLONE_NEWUTS`), PID (`CLONE_NEWPID`), and Mount (`CLONE_NEWNS`) namespaces without root privileges on the host.

### Q7: Why does PID namespace isolation require an isolated `/proc` mount?
**Answer**: Tools like `ps` and `top` do not query the kernel directly; they inspect the pseudo-filesystem `/proc`. Even if a process has PID 1 in a new PID namespace, reading the host's `/proc` would still expose all host processes. MiniBox creates a mount namespace (`CLONE_NEWNS`) and remounts `/proc` (`mount("proc", "/proc", "proc", 0, NULL)`), ensuring only processes inside the container's PID tree are visible.

---

## 3. Resource Management & cgroups v2

### Q8: How does the Linux Completely Fair Scheduler (CFS) enforce CPU quotas?
**Answer**: CFS tracks CPU bandwidth using two parameters:
- `period` (typically 100,000 µs = 100 ms).
- `quota` (the allowable CPU time within that period, e.g. 50,000 µs for 50%).
When a process executes, its runtime is deducted from the quota. Once the quota is exhausted within a 100 ms window, the kernel deschedules the process until the next period begins, generating throttle events recorded in `cpu.stat` (`nr_throttled`).

### Q9: What happens when a container exceeds its `memory.max` limit?
**Answer**: If a workload allocates and dirties physical memory beyond `memory.max` (with swap disabled via `memory.swap.max = 0`), the Linux kernel cannot fulfill the page fault. The kernel triggers the **Out-Of-Memory (OOM) killer**, which selects the memory-consuming process in the cgroup and immediately terminates it via `SIGKILL` (signal 9, exit code 137).

### Q10: How does `pids.max` defend against fork bombs?
**Answer**: The `pids.max` controller maintains an atomic counter (`pids.current`) of all thread and process descriptors within the cgroup. When a process invokes `fork()` and `pids.current >= pids.max`, the kernel rejects process creation and returns `-1` with `errno = EAGAIN` (`Resource temporarily unavailable`), containing the bomb within that slice.

### Q11: How does MiniBox safely clean up cgroups when processes are still running?
**Answer**: In Linux cgroups v2, calling `rmdir()` on a non-empty cgroup fails with `EBUSY`. MiniBox writes `"1\n"` to `<cgroup>/cgroup.kill` (Linux 5.14+ feature), which atomically dispatches `SIGKILL` to all processes in that cgroup. MiniBox then retries `rmdir()` across an interval to allow kernel task structures to flush cleanly.

---

## 4. State Management, IPC, and Telemetry

### Q12: Why is workload state updated using temporary files and POSIX `rename()`?
**Answer**: If MiniBox wrote directly to `state.json` and crashed or was killed midway through writing, the file would be truncated or corrupted. Under POSIX specifications, `rename(oldpath, newpath)` is an **atomic directory operation**: the destination inode is atomically swapped in directory entries, ensuring that external readers (such as the TUI or CLI) always read either the complete old state or the complete new state, never a partially written file.

### Q13: How does MiniBox calculate instantaneous CPU utilization from `/proc/[pid]/stat`?
**Answer**: `/proc/[pid]/stat` provides cumulative CPU ticks in user space (`utime`) and system space (`stime`). MiniBox takes two samples over a known time interval ($\Delta t$):
$$\text{CPU \%} = \frac{(\Delta\text{utime} + \Delta\text{stime}) / \text{CLK\_TCK}}{\Delta t} \times 100\%$$
Where `CLK_TCK` is obtained via `sysconf(_SC_CLK_TCK)` (usually 100 on Linux).

### Q14: What is the cold-start penalty observed in container launch microbenchmarks?
**Answer**: On the initial execution of a cgroup-isolated container, the kernel must allocate inode and directory structures in the cgroup v2 filesystem (`mkdir`), verify and enable subtree controllers (`+cpu +memory +pids`), and synchronize kernel accounting trees (~10–18 ms). On subsequent executions, memory structures and directory caches are warm, reducing steady-state overhead to under 2 ms.
