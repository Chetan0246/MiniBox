import asyncio
import os
from typing import Optional, List, Dict, Any
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Vertical, Horizontal, VerticalScroll
from textual.widgets import (
    Header,
    Footer,
    DataTable,
    Static,
    Label,
    TabbedContent,
    TabPane,
    RichLog,
)

from tui.client import MiniBoxClient
from tui.models import (
    Capabilities,
    HostMetrics,
    WorkloadSummary,
    EventItem,
)
from tui.screens.create_modal import CreateWorkloadModal
from tui.screens.inspect_modal import InspectWorkloadModal
from tui.screens.confirm_modal import ConfirmModal
from tui.screens.help_modal import HelpModal
from tui.screens.benchmark_modal import BenchmarkModal


class MiniBoxApp(App[None]):
    """MiniBox Interactive Terminal Container Runtime Dashboard."""

    CSS_PATH = "styles.tcss"
    TITLE = "MiniBox Container Runtime"

    BINDINGS = [
        Binding("n", "new_workload", "New Workload", priority=True),
        Binding("enter", "inspect_workload", "Inspect", priority=True),
        Binding("i", "inspect_workload", "Inspect"),
        Binding("s", "stop_workload", "Stop"),
        Binding("l", "view_logs", "Logs"),
        Binding("b", "benchmark", "Benchmark"),
        Binding("d", "delete_workload", "Delete"),
        Binding("c", "cleanup_workloads", "Cleanup"),
        Binding("r", "refresh_data", "Refresh"),
        Binding("h", "help", "Help"),
        Binding("question_mark", "help", "Help"),
        Binding("q", "quit", "Quit"),
    ]

    def __init__(self, binary_path: Optional[str] = None):
        super().__init__()
        self.client = MiniBoxClient(binary_path=binary_path)
        self.capabilities = Capabilities()
        self.host_metrics = HostMetrics()
        self.workloads: List[WorkloadSummary] = []
        self.selected_workload_id: Optional[str] = None
        self._refreshing = False

    def compose(self) -> ComposeResult:
        with Horizontal(id="top-header"):
            yield Label("MINIBOX TUI: Container Runtime", id="header-title")
            yield Label("Detecting capabilities...", id="header-caps")

        with Horizontal(id="main-content"):
            # Left sidebar: System & Resource overview
            with Vertical(id="left-panel"):
                yield Label("HOST RESOURCES", classes="panel-title")
                yield Label("CPU Utilization:", classes="metric-label")
                yield Static("0.0%", id="stat-host-cpu", classes="metric-value")

                yield Label("Memory Usage:", classes="metric-label")
                yield Static("0.00 / 0.00 GiB", id="stat-host-mem", classes="metric-value")
                yield Static("Available: 0.00 GiB", id="stat-host-avail", classes="metric-label")

                yield Label("System Load Averages:", classes="metric-label")
                yield Static("0.00, 0.00, 0.00", id="stat-host-load", classes="metric-value")

                yield Label("WORKLOAD SUMMARY", classes="panel-title")
                yield Label("Total Workloads:", classes="metric-label")
                yield Static("0", id="stat-total-wl", classes="metric-value")
                yield Label("Active Running:", classes="metric-label")
                yield Static("0", id="stat-running-wl", classes="metric-value")
                yield Label("Completed / Stopped:", classes="metric-label")
                yield Static("0", id="stat-completed-wl", classes="metric-value")

            # Right main section: Workloads table & multi-tab details
            with Vertical(id="right-panel"):
                with Vertical(id="workload-table-container"):
                    yield Label("ACTIVE & RECENT WORKLOADS", classes="panel-title")
                    yield DataTable(id="workloads-table")

                with Vertical(id="bottom-tabs-container"):
                    with TabbedContent(initial="tab-events", id="tabs-details"):
                        with TabPane("Recent Activity & Events", id="tab-events"):
                            yield RichLog(id="events-log", highlight=True, markup=True)

                        with TabPane("Workload Output Logs", id="tab-logs"):
                            with VerticalScroll(id="workload-logs-view"):
                                yield Static("Select a workload and press [L] to view logs.", id="log-content-static")

                        with TabPane("Kernel Capabilities", id="tab-caps"):
                            with VerticalScroll(id="capabilities-view"):
                                yield Static("Detecting system isolation capabilities...", id="caps-content-static")

        yield Static("[N] New  [Enter/I] Inspect  [S] Stop  [L] Logs  [B] Benchmark  [D] Delete  [C] Cleanup  [R] Refresh  [H] Help  [Q] Quit", id="footer-bar")

    async def on_mount(self) -> None:
        """Initialize table columns, detect capabilities, and start polling."""
        table = self.query_one("#workloads-table", DataTable)
        table.cursor_type = "row"
        table.add_column("WORKLOAD ID", key="id")
        table.add_column("NAME", key="name")
        table.add_column("PID", key="pid")
        table.add_column("STATE", key="state")
        table.add_column("CPU %", key="cpu")
        table.add_column("MEMORY", key="memory")
        table.add_column("PIDS", key="pids")

        self.log_event("INFO", "MiniBox TUI initialized. Connecting to core runtime...")

        # Detect capabilities
        await self._detect_capabilities()

        # Initial metrics & workload fetch
        await self.refresh_data()

        # Set periodic refresh interval (1.5 seconds)
        self.set_interval(1.5, self.refresh_data)

    def log_event(self, level: str, message: str) -> None:
        """Append an event to the events log panel with formatting."""
        item = EventItem.create(level, message)
        color = "green"
        if level == "WARN":
            color = "yellow"
        elif level == "ERROR":
            color = "red"
        elif level == "SUCCESS":
            color = "bold green"
        try:
            log_widget = self.query_one("#events-log", RichLog)
            log_widget.write(f"[{item.timestamp}] [[{color}]{item.level}[/{color}]] {item.message}")
        except Exception:
            pass

    async def _detect_capabilities(self) -> None:
        """Query kernel capabilities and update header and capabilities tab."""
        self.capabilities = await self.client.get_capabilities()

        user_tag = "[green]USER-NS[/green]" if self.capabilities.user_namespaces else "[red]NO-USER-NS[/red]"
        uts_tag = "[green]UTS[/green]" if self.capabilities.uts_namespace else "[yellow]NO-UTS[/yellow]"
        pid_tag = "[green]PID-NS[/green]" if self.capabilities.pid_namespace else "[yellow]NO-PID[/yellow]"
        cg_tag = "[green]CGROUPS-V2[/green]" if self.capabilities.cgroups_v2 else "[yellow]NO-CGROUPS[/yellow]"

        header_lbl = self.query_one("#header-caps", Label)
        header_lbl.update(f"Isolation: {user_tag} | {uts_tag} | {pid_tag} | {cg_tag}")

        caps_text = (
            f"[bold cyan]Detected Linux Kernel Isolation Capabilities:[/bold cyan]\n\n"
            f"• [bold]User Namespaces:[/bold]  {'AVAILABLE' if self.capabilities.user_namespaces else 'UNAVAILABLE'}\n"
            f"• [bold]UTS Namespace:[/bold]    {'AVAILABLE (Isolated Hostname)' if self.capabilities.uts_namespace else 'UNAVAILABLE'}\n"
            f"• [bold]PID Namespace:[/bold]    {'AVAILABLE (Private PID 1 tree)' if self.capabilities.pid_namespace else 'UNAVAILABLE'}\n"
            f"• [bold]Mount Namespace:[/bold]  {'AVAILABLE' if self.capabilities.mount_namespace else 'UNAVAILABLE'}\n"
            f"• [bold]cgroups v2:[/bold]       {'AVAILABLE (Resource Quotas)' if self.capabilities.cgroups_v2 else 'UNAVAILABLE'}\n\n"
            f"[bold]Capability Summary:[/bold] {self.capabilities.reason or 'Normal operation'}\n"
        )
        try:
            self.query_one("#caps-content-static", Static).update(caps_text)
        except Exception:
            pass

        self.log_event("INFO", f"Capabilities verified: {self.capabilities.reason or 'Linux environment ready'}")

    async def refresh_data(self) -> None:
        """Asynchronously refresh host metrics and workloads table."""
        if self._refreshing:
            return
        self._refreshing = True
        try:
            host, workloads = await self.client.get_metrics()
            self.host_metrics = host
            self.workloads = workloads

            # Update Host Metrics widgets
            if host.valid:
                self.query_one("#stat-host-cpu", Static).update(f"{host.cpu_percent:.1f}%")
                used_gib = host.mem_used_bytes / (1024.0 ** 3)
                tot_gib = host.mem_total_bytes / (1024.0 ** 3)
                avail_gib = host.mem_avail_bytes / (1024.0 ** 3)
                self.query_one("#stat-host-mem", Static).update(f"{used_gib:.2f} / {tot_gib:.2f} GiB")
                self.query_one("#stat-host-avail", Static).update(f"Available: {avail_gib:.2f} GiB")
                self.query_one("#stat-host-load", Static).update(f"{host.load_1m:.2f}, {host.load_5m:.2f}, {host.load_15m:.2f}")

            # Update summary counts
            total = len(workloads)
            running = sum(1 for w in workloads if w.state == "RUNNING")
            completed = sum(1 for w in workloads if w.state in ("COMPLETED", "TERMINATED", "FAILED"))

            self.query_one("#stat-total-wl", Static).update(str(total))
            self.query_one("#stat-running-wl", Static).update(str(running))
            self.query_one("#stat-completed-wl", Static).update(str(completed))

            # Update DataTable
            table = self.query_one("#workloads-table", DataTable)
            curr_cursor_row = table.cursor_row

            # Re-populate rows
            table.clear()
            for w in workloads:
                cpu_str = f"{w.cpu_percent:.1f}%" if (w.active and w.cpu_percent >= 0) else "-"
                mem_str = f"{w.memory_bytes / (1024.0*1024.0):.1f} MB" if (w.active and w.memory_bytes > 0) else "-"
                pids_str = str(w.pids_count) if (w.active and w.pids_count > 0) else "-"
                pid_display = str(w.pid) if w.pid > 0 else "-"

                # Colorize state
                state_styled = w.state
                if w.state == "RUNNING":
                    state_styled = f"[green]{w.state}[/green]"
                elif w.state == "COMPLETED":
                    state_styled = f"[blue]{w.state}[/blue]"
                elif w.state == "TERMINATED":
                    state_styled = f"[yellow]{w.state}[/yellow]"
                elif w.state == "FAILED":
                    state_styled = f"[red]{w.state}[/red]"

                table.add_row(
                    w.id,
                    w.name,
                    pid_display,
                    state_styled,
                    cpu_str,
                    mem_str,
                    pids_str,
                    key=w.id,
                )

            # Preserve selection
            if curr_cursor_row is not None and curr_cursor_row < table.row_count:
                table.cursor_coordinate = (curr_cursor_row, 0)

            # If workload log view is currently active and selected, auto-update
            selected_id = self._get_selected_workload_id()
            if selected_id and self.query_one("#tabs-details", TabbedContent).active == "tab-logs":
                await self._update_logs_view(selected_id)

        except Exception as e:
            self.log_event("ERROR", f"Refresh failed: {e}")
        finally:
            self._refreshing = False

    def _get_selected_workload_id(self) -> Optional[str]:
        """Return the workload ID of the currently highlighted DataTable row."""
        try:
            table = self.query_one("#workloads-table", DataTable)
            if table.row_count == 0:
                return None
            row_idx = table.cursor_row
            row_key, _ = table.coordinate_to_cell_key((row_idx, 0))
            if row_key and row_key.value:
                return str(row_key.value)
        except Exception:
            pass
        return None

    async def _update_logs_view(self, workload_id: str) -> None:
        """Fetch and display workload log file content."""
        logs = await self.client.get_workload_logs(workload_id)
        try:
            log_view = self.query_one("#log-content-static", Static)
            log_view.update(f"[bold cyan]Logs for Workload: {workload_id}[/bold cyan]\n\n{logs or '(Log file is currently empty)'}")
        except Exception:
            pass

    # ================= Action Handlers =================

    def action_new_workload(self) -> None:
        """Open the interactive workload creation modal."""
        self.push_screen(CreateWorkloadModal(), self._on_workload_created)

    async def _on_workload_created(self, params: Optional[Dict[str, Any]]) -> None:
        """Callback when the workload creation modal is submitted."""
        if not params:
            return

        self.log_event("INFO", f"Launching workload: {params['executable']}...")
        ok, msg, data = await self.client.start_workload(
            executable=params["executable"],
            args=params["args"],
            name=params["name"],
            workdir=params["workdir"],
            cpu_pct=params["cpu_pct"],
            memory_str=params["memory_str"],
            pids_max=params["pids_max"],
            isolate=params["isolate"],
            uts=params["uts"],
            hostname=params["hostname"],
            pid_ns=params["pid_ns"],
            mount_ns=params["mount_ns"],
        )
        if ok:
            wl_id = data.get("workload_id", "unknown")
            pid = data.get("pid", 0)
            self.log_event("SUCCESS", f"Workload '{wl_id}' (PID {pid}) started successfully.")
            self.notify(f"Workload {wl_id} started!", title="Success", severity="information")
        else:
            self.log_event("ERROR", f"Failed to start workload: {msg}")
            self.notify(f"Launch failed: {msg}", title="Error", severity="error")

        await self.refresh_data()

    async def action_inspect_workload(self) -> None:
        """Inspect the currently highlighted container."""
        wl_id = self._get_selected_workload_id()
        if not wl_id:
            self.notify("Please select a workload from the table first.", severity="warning")
            return

        detail = await self.client.get_workload_detail(wl_id)
        if not detail:
            self.notify(f"Workload '{wl_id}' not found.", severity="error")
            return

        metrics = await self.client.get_workload_metrics(wl_id, sample_ms=100)
        self.push_screen(InspectWorkloadModal(detail, metrics))

    def action_stop_workload(self) -> None:
        """Prompt to stop the highlighted container."""
        wl_id = self._get_selected_workload_id()
        if not wl_id:
            self.notify("Please select a workload to stop.", severity="warning")
            return

        self.push_screen(
            ConfirmModal("Stop Workload", f"Stop workload '{wl_id}'?"),
            lambda res: asyncio.create_task(self._on_confirm_stop(wl_id, res)),
        )

    async def _on_confirm_stop(self, wl_id: str, result: tuple[bool, bool]) -> None:
        confirmed, force = result
        if not confirmed:
            return

        self.log_event("INFO", f"Stopping workload '{wl_id}' (force={force})...")
        ok, msg = await self.client.stop_workload(wl_id, force=force)
        if ok:
            self.log_event("SUCCESS", msg)
            self.notify(msg, severity="information")
        else:
            self.log_event("ERROR", msg)
            self.notify(msg, severity="error")

        await self.refresh_data()

    async def action_view_logs(self) -> None:
        """Switch to logs tab and load output for selected container."""
        wl_id = self._get_selected_workload_id()
        if not wl_id:
            self.notify("Please select a workload to view logs.", severity="warning")
            return

        tabs = self.query_one("#tabs-details", TabbedContent)
        tabs.active = "tab-logs"
        await self._update_logs_view(wl_id)

    def action_delete_workload(self) -> None:
        """Prompt to delete the highlighted container record."""
        wl_id = self._get_selected_workload_id()
        if not wl_id:
            self.notify("Please select a workload to delete.", severity="warning")
            return

        self.push_screen(
            ConfirmModal("Delete Workload", f"Permanently delete workload '{wl_id}'?"),
            lambda res: asyncio.create_task(self._on_confirm_delete(wl_id, res)),
        )

    async def _on_confirm_delete(self, wl_id: str, result: tuple[bool, bool]) -> None:
        confirmed, force = result
        if not confirmed:
            return

        self.log_event("INFO", f"Deleting workload '{wl_id}' (force={force})...")
        ok, msg = await self.client.delete_workload(wl_id, force=force)
        if ok:
            self.log_event("SUCCESS", msg)
            self.notify(msg, severity="information")
        else:
            self.log_event("ERROR", msg)
            self.notify(msg, severity="error")

        await self.refresh_data()

    async def action_cleanup_workloads(self) -> None:
        """Bulk remove all finished workloads."""
        self.log_event("INFO", "Performing bulk cleanup of finished workloads...")
        ok, msg = await self.client.cleanup_workloads()
        if ok:
            self.log_event("SUCCESS", msg)
            self.notify(msg, severity="information")
        else:
            self.log_event("ERROR", msg)
            self.notify(msg, severity="error")

        await self.refresh_data()

    async def action_refresh_data(self) -> None:
        """Trigger an immediate data refresh."""
        self.log_event("INFO", "Manual refresh requested.")
        await self.refresh_data()
        self.notify("Dashboard refreshed", severity="information")

    def action_help(self) -> None:
        """Show the help and educational guide dialog."""
        self.push_screen(HelpModal())

    def action_benchmark(self) -> None:
        """Show the benchmark evaluation and results dialog."""
        self.push_screen(BenchmarkModal())


def main():
    """Main CLI entrypoint for launching MiniBox TUI."""
    import sys

    if "--help" in sys.argv or "-h" in sys.argv:
        print("MiniBox TUI: Interactive Lightweight Linux Container Runtime Dashboard")
        print("\nUsage:\n  ./minibox-tui [options]\n")
        print("Options:")
        print("  --binary <path>    Path to minibox C runtime binary (default: ./bin/minibox)")
        print("  -h, --help         Show this help message")
        print("  -v, --version      Show version information")
        return

    if "--version" in sys.argv or "-v" in sys.argv:
        from tui import __version__
        print(f"MiniBox TUI version {__version__}")
        return

    bin_path = None
    if "--binary" in sys.argv:
        idx = sys.argv.index("--binary")
        if idx + 1 < len(sys.argv):
            bin_path = sys.argv[idx + 1]

    app = MiniBoxApp(binary_path=bin_path)
    app.run()


if __name__ == "__main__":
    main()
