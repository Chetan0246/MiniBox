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


class MiniBoxClient:
    """Non-blocking asynchronous client for MiniBox C runtime."""

    def __init__(self, binary_path: Optional[str] = None):
        if binary_path:
            self.binary_path = binary_path
        else:
            repo_root = Path(__file__).resolve().parent.parent
            self.binary_path = str(repo_root / "bin" / "minibox")

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
            except Exception:
                pass
            return 124, "", "Command timed out after 10 seconds"

        stdout = stdout_bytes.decode("utf-8", errors="replace")
        stderr = stderr_bytes.decode("utf-8", errors="replace")
        return proc.returncode, stdout, stderr

    async def get_capabilities(self) -> Capabilities:
        """Detect Linux capabilities supported on host."""
        code, out, _ = await self._run_command("capabilities", "--json")
        if code == 0 and out.strip():
            try:
                data = json.loads(out)
                return Capabilities.from_dict(data)
            except Exception:
                pass
        return Capabilities(reason="Failed to detect capabilities")

    async def get_metrics(self) -> Tuple[HostMetrics, List[WorkloadSummary]]:
        """Collect host metrics and summary metrics for all workloads."""
        code, out, _ = await self._run_command("metrics", "--json")
        host = HostMetrics()
        workloads: List[WorkloadSummary] = []
        if code == 0 and out.strip():
            try:
                data = json.loads(out)
                if "host" in data:
                    host = HostMetrics.from_dict(data["host"])
                if "workloads" in data and isinstance(data["workloads"], list):
                    for w in data["workloads"]:
                        workloads.append(WorkloadSummary.from_dict(w))
            except Exception:
                pass
        return host, workloads

    async def get_workload_detail(self, workload_id: str) -> Optional[WorkloadDetail]:
        """Retrieve full workload inspection record."""
        code, out, _ = await self._run_command("inspect", "--json", workload_id)
        if code == 0 and out.strip():
            try:
                data = json.loads(out)
                if data.get("ok"):
                    return WorkloadDetail.from_dict(data)
            except Exception:
                pass
        return None

    async def get_workload_metrics(self, workload_id: str, sample_ms: int = 0) -> Optional[Dict[str, Any]]:
        """Get live metrics JSON for a specific workload."""
        args = ["metrics", "--json"]
        if sample_ms > 0:
            args.extend(["--sample-ms", str(sample_ms)])
        args.append(workload_id)
        code, out, _ = await self._run_command(*args)
        if code == 0 and out.strip():
            try:
                data = json.loads(out)
                if data.get("ok"):
                    return data
            except Exception:
                pass
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
            try:
                data = json.loads(out)
                if data.get("ok"):
                    return True, "Workload launched successfully", data
                else:
                    msg = data.get("error", err.strip() or f"Error (code {code})")
                    return False, msg, data
            except Exception:
                pass
        msg = err.strip() or out.strip() or f"Failed to start workload (exit code {code})"
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
            try:
                data = json.loads(out)
                if data.get("ok"):
                    return True, f"Workload '{workload_id}' stopped (state: {data.get('state')})"
            except Exception:
                pass
        return False, err.strip() or out.strip() or f"Failed to stop workload {workload_id}"

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
            try:
                data = json.loads(out)
                count = data.get("removed_count", 0)
                return True, f"Cleaned up {count} finished workload(s)"
            except Exception:
                pass
        return False, err.strip() or out.strip() or "Cleanup failed"

    async def get_workload_logs(self, workload_id: str) -> str:
        """Fetch stdout/stderr logs for a workload."""
        code, out, err = await self._run_command("logs", workload_id)
        if code == 0:
            return out
        return f"[Failed to read logs: {err.strip() or 'Unknown error'}]"
