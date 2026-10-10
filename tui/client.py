import asyncio
import json
import os
from pathlib import Path
from typing import Optional, List, Tuple, Dict, Any

from tui.models import (
    Capabilities,
    HostMetrics,
    WorkloadSummary,
    WorkloadDetail,
)


class MiniBoxCommandError(RuntimeError):
    """Raised when the minibox runtime returns output that cannot be interpreted."""


class MiniBoxClient:
    """Non-blocking asynchronous client for MiniBox C runtime."""

    def __init__(self, binary_path: Optional[str] = None):
        if binary_path:
            self.binary_path = binary_path
        else:
            repo_root = Path(__file__).resolve().parent.parent
            self.binary_path = str(repo_root / "bin" / "minibox")
        # Last decoding/lookup failure, retained so the UI can report it instead of
        # silently degrading to empty data.
        self.last_error: Optional[str] = None

    def _decode_json(self, out: str, context: str) -> Optional[Dict[str, Any]]:
        """Parse runtime JSON output, recording a diagnostic on failure."""
        try:
            data = json.loads(out)
        except json.JSONDecodeError as exc:
            self.last_error = f"{context}: invalid JSON from runtime ({exc})"
            return None
        if not isinstance(data, dict):
            self.last_error = f"{context}: expected a JSON object, got {type(data).__name__}"
            return None
        return data

    async def _run_command(self, *args: str) -> Tuple[int, str, str]:
        """Execute a minibox command asynchronously and return (exit_code, stdout, stderr)."""
        if not os.path.isfile(self.binary_path) or not os.access(self.binary_path, os.X_OK):
            return 127, "", f"Binary '{self.binary_path}' not found or not executable. Run 'make' first."

        proc = await asyncio.create_subprocess_exec(
            self.binary_path,
            *args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        try:
            stdout_bytes, stderr_bytes = await asyncio.wait_for(proc.communicate(), timeout=10.0)
        except asyncio.TimeoutError:
            try:
                proc.kill()
                await proc.wait()
            except (ProcessLookupError, OSError):
                # Already reaped or gone; nothing further to do while unwinding.
                pass
            return 124, "", "Command timed out after 10 seconds"

        stdout = stdout_bytes.decode("utf-8", errors="replace")
        stderr = stderr_bytes.decode("utf-8", errors="replace")
        return proc.returncode, stdout, stderr

    async def get_capabilities(self) -> Capabilities:
        """Detect Linux capabilities supported on host."""
        code, out, err = await self._run_command("capabilities", "--json")
        if code == 0 and out.strip():
            data = self._decode_json(out, "capabilities")
            if data is not None:
                try:
                    return Capabilities.from_dict(data)
                except (TypeError, ValueError) as exc:
                    self.last_error = f"capabilities: malformed fields ({exc})"
        self.last_error = self.last_error or (
            f"capabilities: runtime exited {code}" + (f" ({err.strip()})" if err.strip() else "")
        )
        return Capabilities(reason=f"Failed to detect capabilities: {self.last_error}")

    async def get_metrics(self) -> Tuple[HostMetrics, List[WorkloadSummary]]:
        """Collect host metrics and summary metrics for all workloads."""
        code, out, err = await self._run_command("metrics", "--json")
        host = HostMetrics()
        workloads: List[WorkloadSummary] = []
        if code == 0 and out.strip():
            data = self._decode_json(out, "metrics")
            if data is not None:
                raw_host = data.get("host")
                if isinstance(raw_host, dict):
                    try:
                        host = HostMetrics.from_dict(raw_host)
                    except (TypeError, ValueError) as exc:
                        self.last_error = f"metrics: malformed host fields ({exc})"
                raw_workloads = data.get("workloads")
                if isinstance(raw_workloads, list):
                    for w in raw_workloads:
                        if not isinstance(w, dict):
                            self.last_error = "metrics: skipping non-object workload entry"
                            continue
                        try:
                            workloads.append(WorkloadSummary.from_dict(w))
                        except (TypeError, ValueError) as exc:
                            self.last_error = f"metrics: malformed workload entry ({exc})"
        elif not self.last_error:
            self.last_error = (
                f"metrics: runtime exited {code}" + (f" ({err.strip()})" if err.strip() else "")
            )
        return host, workloads

    async def get_workload_detail(self, workload_id: str) -> Optional[WorkloadDetail]:
        """Retrieve full workload inspection record."""
        code, out, err = await self._run_command("inspect", "--json", workload_id)
        if code == 0 and out.strip():
            data = self._decode_json(out, f"inspect {workload_id}")
            if data is not None:
                if data.get("ok"):
                    try:
                        return WorkloadDetail.from_dict(data)
                    except (TypeError, ValueError) as exc:
                        self.last_error = f"inspect {workload_id}: malformed fields ({exc})"
                else:
                    self.last_error = str(data.get("error") or "workload not found")
        elif not self.last_error:
            self.last_error = (
                f"inspect {workload_id}: runtime exited {code}"
                + (f" ({err.strip()})" if err.strip() else "")
            )
        return None

    async def get_workload_metrics(self, workload_id: str, sample_ms: int = 0) -> Optional[Dict[str, Any]]:
        """Get live metrics JSON for a specific workload."""
        args = ["metrics", "--json"]
        if sample_ms > 0:
            args.extend(["--sample-ms", str(sample_ms)])
        args.append(workload_id)
        code, out, err = await self._run_command(*args)
        if code == 0 and out.strip():
            data = self._decode_json(out, f"metrics {workload_id}")
            if data is not None and data.get("ok"):
                return data
            elif data is not None:
                self.last_error = str(data.get("error") or "metrics unavailable")
        elif not self.last_error:
            self.last_error = (
                f"metrics {workload_id}: runtime exited {code}"
                + (f" ({err.strip()})" if err.strip() else "")
            )
        return None

    async def start_workload(
        self,
        executable: str,
        args: Optional[List[str]] = None,
        name: Optional[str] = None,
        workdir: Optional[str] = None,
        cpu_pct: int = 0,
        memory_str: Optional[str] = None,
        pids_max: int = 0,
        isolate: bool = False,
        uts: bool = False,
        hostname: Optional[str] = None,
        pid_ns: bool = False,
        mount_ns: bool = False,
    ) -> Tuple[bool, str, Dict[str, Any]]:
        """Start a workload asynchronously in background."""
        cmd = ["start", "--json"]
        if name:
            cmd.extend(["--name", name])
        if workdir:
            cmd.extend(["--workdir", workdir])
        if cpu_pct > 0:
            cmd.extend(["--cpu", str(cpu_pct)])
        if memory_str:
            cmd.extend(["--memory", memory_str])
        if pids_max > 0:
            cmd.extend(["--pids", str(pids_max)])
        if isolate:
            cmd.append("--isolate")
        if uts:
            cmd.append("--uts")
        if hostname:
            cmd.extend(["--hostname", hostname])
        if pid_ns:
            cmd.append("--pid-ns")
        if mount_ns:
            cmd.append("--mount-ns")

        cmd.append(executable)
        if args:
            cmd.extend(args)

        code, out, err = await self._run_command(*cmd)
        if out.strip():
            data = self._decode_json(out, "start")
            if data is not None:
                if data.get("ok"):
                    return True, "Workload launched successfully", data
                msg = data.get("error") or err.strip() or f"Error (code {code})"
                return False, msg, data
        msg = err.strip() or out.strip() or self.last_error or f"Failed to start workload (exit code {code})"
        return False, msg, {}

    async def stop_workload(self, workload_id: str, force: bool = False, signal_num: int = 15) -> Tuple[bool, str]:
        """Stop a running workload."""
        cmd = ["stop", "--json"]
        if force:
            cmd.append("--force")
        if signal_num != 15:
            cmd.extend(["--signal", str(signal_num)])
        cmd.append(workload_id)

        code, out, err = await self._run_command(*cmd)
        if out.strip():
            data = self._decode_json(out, f"stop {workload_id}")
            if data is not None and data.get("ok"):
                return True, f"Workload '{workload_id}' stopped (state: {data.get('state')})"
        return False, (
            err.strip() or out.strip() or self.last_error or f"Failed to stop workload {workload_id}"
        )

    async def delete_workload(self, workload_id: str, force: bool = False) -> Tuple[bool, str]:
        """Delete a workload record."""
        cmd = ["delete", "--json"]
        if force:
            cmd.append("--force")
        cmd.append(workload_id)

        code, out, err = await self._run_command(*cmd)
        if code == 0:
            return True, f"Workload '{workload_id}' deleted"
        return False, err.strip() or out.strip() or f"Failed to delete workload {workload_id}"

    async def cleanup_workloads(self) -> Tuple[bool, str]:
        """Remove all completed/terminated workloads."""
        code, out, err = await self._run_command("cleanup", "--json")
        if code == 0 and out.strip():
            data = self._decode_json(out, "cleanup")
            if data is not None:
                count = data.get("removed_count", 0)
                return True, f"Cleaned up {count} finished workload(s)"
        return False, err.strip() or out.strip() or self.last_error or "Cleanup failed"

    async def get_workload_logs(self, workload_id: str) -> str:
        """Fetch stdout/stderr logs for a workload."""
        code, out, err = await self._run_command("logs", workload_id)
        if code == 0:
            return out
        return f"[Failed to read logs: {err.strip() or 'Unknown error'}]"
