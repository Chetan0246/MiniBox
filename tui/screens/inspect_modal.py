from typing import Optional
from textual.app import ComposeResult
from textual.containers import Vertical, Horizontal, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Button, Label, Static

from tui.models import WorkloadDetail


class InspectWorkloadModal(ModalScreen[None]):
    """Modal dialog displaying comprehensive container details."""

    DEFAULT_CSS = """
    InspectWorkloadModal {
        align: center middle;
    }
    #inspect-dialog {
        width: 80;
        height: 85%;
        border: thick $accent;
        background: $surface;
        padding: 1 2;
    }
    #inspect-title {
        text-style: bold;
        color: $accent;
        margin-bottom: 1;
        text-align: center;
    }
    #inspect-scroll {
        height: 1fr;
        border: solid $secondary;
        padding: 1;
        margin-bottom: 1;
    }
    .section-header {
        text-style: bold;
        color: $warning;
        margin-top: 1;
        margin-bottom: 0;
    }
    .detail-line {
        margin-left: 2;
    }
    #inspect-buttons {
        align: right middle;
    }
    """

    def __init__(self, detail: WorkloadDetail, metrics: Optional[dict] = None):
        super().__init__()
        self.detail = detail
        self.metrics = metrics or {}

    def compose(self) -> ComposeResult:
        with Vertical(id="inspect-dialog"):
            yield Label(f"Container Inspection: {self.detail.name} ({self.detail.id})", id="inspect-title")
            with VerticalScroll(id="inspect-scroll"):
                yield Label("Core Process Attributes", classes="section-header")
                yield Static(f"• Workload ID:     {self.detail.id}", classes="detail-line")
                yield Static(f"• Friendly Name:   {self.detail.name}", classes="detail-line")
                yield Static(f"• Lifecycle State: {self.detail.state}", classes="detail-line")
                yield Static(f"• Process PID:     {self.detail.pid}", classes="detail-line")
                yield Static(f"• Supervisor PID:  {self.detail.supervisor_pid}", classes="detail-line")
                yield Static(f"• Executable:      {self.detail.executable}", classes="detail-line")
                yield Static(f"• Arguments:       {self.detail.args or '(none)'}", classes="detail-line")
                yield Static(f"• Work Directory:  {self.detail.work_dir or '.'}", classes="detail-line")
                yield Static(f"• Log File:        {self.detail.log_file}", classes="detail-line")

                yield Label("Lifecycle & Termination", classes="section-header")
                if self.detail.started_at_ms > 0:
                    dur_s = (self.detail.finished_at_ms - self.detail.started_at_ms) / 1000.0 if self.detail.finished_at_ms > 0 else 0.0
                    yield Static(f"• Elapsed Runtime: {dur_s:.2f}s", classes="detail-line")
                if self.detail.state == "COMPLETED":
                    yield Static(f"• Exit Code:       {self.detail.exit_code}", classes="detail-line")
                elif self.detail.state == "TERMINATED":
                    yield Static(f"• Signal:          {self.detail.term_signal}", classes="detail-line")
                elif self.detail.state == "FAILED":
                    yield Static(f"• Error Message:   {self.detail.error_msg} (errno: {self.detail.exec_errno})", classes="detail-line")

                yield Label("Linux Namespace Isolation", classes="section-header")
                uts_status = f"ENABLED (hostname: '{self.detail.iso_hostname}')" if self.detail.iso_uts else "DISABLED"
                pid_status = "ENABLED (isolated PID 1)" if self.detail.iso_pid else "DISABLED"
                mnt_status = "ENABLED" if self.detail.iso_mount else "DISABLED"
                yield Static(f"• UTS Namespace:   {uts_status}", classes="detail-line")
                yield Static(f"• PID Namespace:   {pid_status}", classes="detail-line")
                yield Static(f"• Mount Namespace: {mnt_status}", classes="detail-line")

                yield Label("cgroups v2 Resource Controls", classes="section-header")
                cpu_lim = f"{self.detail.limits.cpu_percent}% quota" if self.detail.limits.cpu_percent > 0 else "unlimited"
                mem_lim = f"{self.detail.limits.memory_max_bytes / (1024*1024):.1f} MiB" if self.detail.limits.memory_max_bytes > 0 else "unlimited"
                pids_lim = f"{self.detail.limits.pids_max} max" if self.detail.limits.pids_max > 0 else "unlimited"
                yield Static(f"• CPU Limit:       {cpu_lim}", classes="detail-line")
                yield Static(f"• Memory Limit:    {mem_lim}", classes="detail-line")
                yield Static(f"• PIDs Limit:      {pids_lim}", classes="detail-line")
                yield Static(f"• cgroup Path:     {self.detail.cgroup_path or '(none)'}", classes="detail-line")
                yield Static(f"• cgroup Active:   {'YES' if self.detail.cgroup_active else 'NO'}", classes="detail-line")

                # Metrics section if available
                if self.metrics and self.metrics.get("ok"):
                    yield Label("Live Metrics Snapshot", classes="section-header")
                    cpu_m = self.metrics.get("cpu", {})
                    if cpu_m.get("valid"):
                        yield Static(f"• CPU Usage:       {cpu_m.get('usage_percent', 0.0):.1f}%", classes="detail-line")
                        yield Static(f"• User / Sys Time: {cpu_m.get('user_usec', 0)/1e6:.3f}s / {cpu_m.get('system_usec', 0)/1e6:.3f}s", classes="detail-line")
                        if cpu_m.get("nr_periods", 0) > 0:
                            yield Static(f"• CFS Throttling:  {cpu_m.get('nr_throttled', 0)} / {cpu_m.get('nr_periods', 0)} periods ({cpu_m.get('throttled_usec', 0)} us)", classes="detail-line")
                    mem_m = self.metrics.get("memory", {})
                    if mem_m.get("valid"):
                        cur_mb = mem_m.get("current_bytes", 0) / (1024.0 * 1024.0)
                        peak_mb = mem_m.get("peak_bytes", 0) / (1024.0 * 1024.0)
                        yield Static(f"• Memory Current:  {cur_mb:.2f} MiB (peak: {peak_mb:.2f} MiB)", classes="detail-line")
                    pids_m = self.metrics.get("pids", {})
                    if pids_m.get("valid"):
                        yield Static(f"• Active PIDs:     {pids_m.get('current', 1)}", classes="detail-line")

            with Horizontal(id="inspect-buttons"):
                yield Button("Close (Esc)", variant="primary", id="btn-close")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(None)

    def on_key(self, event) -> None:
        if event.key in ("escape", "enter"):
            self.dismiss(None)
