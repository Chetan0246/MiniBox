import asyncio
import json
import os
import subprocess
from pathlib import Path
from typing import Optional
from textual.app import ComposeResult
from textual.containers import Vertical, Horizontal, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Button, Label, Static


class BenchmarkModal(ModalScreen[None]):
    """Interactive modal dialog to execute and view benchmarks from MiniBox TUI."""

    DEFAULT_CSS = """
    BenchmarkModal {
        align: center middle;
    }
    #bench-dialog {
        width: 86;
        height: 90%;
        border: thick $primary;
        background: $surface;
        padding: 1 2;
    }
    #bench-title {
        text-style: bold;
        color: $primary;
        margin-bottom: 1;
        text-align: center;
    }
    #bench-scroll {
        height: 1fr;
        border: solid $secondary;
        padding: 1;
        margin-bottom: 1;
    }
    .bench-sec-hdr {
        text-style: bold;
        color: $accent;
        margin-top: 1;
    }
    .bench-data-line {
        margin-left: 2;
    }
    #bench-status {
        color: $warning;
        text-style: bold;
        margin-bottom: 1;
    }
    #bench-buttons {
        align: right middle;
    }
    #bench-buttons Button {
        margin-left: 1;
    }
    """

    def __init__(self):
        super().__init__()
        self.results_path = Path(__file__).resolve().parent.parent.parent / "benchmarks" / "results.json"
        self._running = False

    def compose(self) -> ComposeResult:
        with Vertical(id="bench-dialog"):
            yield Label("MiniBox Performance Evaluation & Benchmarks", id="bench-title")
            yield Label("Status: Ready", id="bench-status")
            with VerticalScroll(id="bench-scroll"):
                yield Static(self._format_results_text(), id="bench-content-static")
            with Horizontal(id="bench-buttons"):
                yield Button("Run Benchmark (Quick)", variant="success", id="btn-run-bench")
                yield Button("Close (Esc)", variant="default", id="btn-close-bench")

    def _format_results_text(self) -> str:
        """Format existing results.json into terminal readable text."""
        if not self.results_path.exists():
            return "No previous benchmark run found.\nClick 'Run Benchmark (Quick)' below to execute the evaluation suite."

        try:
            with open(self.results_path) as f:
                data = json.load(f)

            sys_p = data.get("system", {})
            e1 = data.get("launch_overhead", {})
            e2 = data.get("cpu_throttling", {})
            e3 = data.get("memory", {}) or data.get("memory_allocation", {})
            e4 = data.get("fork_spawning", {})
            e5 = data.get("lifecycle_latency", {})

            # Memory formatting (Part A performance & Part B enforcement)
            if "performance_subceiling" in e3:
                mem_p = e3.get("performance_subceiling", {})
                mem_e = e3.get("enforcement_overceiling", {})
                mem_direct = mem_p.get("direct", {}).get("stats_ms", {}).get("median", 0)
                mem_unconst = mem_p.get("minibox_unconstrained", {}).get("stats_ms", {}).get("median", 0)
                mem_lim = mem_p.get("minibox_limited_64M", {}).get("stats_ms", {}).get("median", 0)
                enf_status = "VERIFIED (SIGKILL 9)" if mem_e.get("enforcement_confirmed") else "UNVERIFIED"
            else:
                mem_direct = e3.get("direct", {}).get("stats_ms", {}).get("median", 0)
                mem_unconst = e3.get("minibox_unconstrained", {}).get("stats_ms", {}).get("median", 0)
                mem_lim = e3.get("minibox_limited_64M", {}).get("stats_ms", {}).get("median", 0)
                enf_status = "Tested in test_phase4"

            txt = (
                f"[bold yellow]Benchmark Timestamp:[/bold yellow] {sys_p.get('timestamp', 'Unknown')}\n"
                f"[bold cyan]Environment Profile:[/bold cyan]\n"
                f"• Host: {sys_p.get('os')} ({sys_p.get('arch')}), Kernel: {sys_p.get('kernel')}\n"
                f"• CPU:  {sys_p.get('cpu_model')} ({sys_p.get('cpu_cores')} cores), RAM: {sys_p.get('ram_total_gb')} GiB\n\n"
                f"[bold cyan]Experiment 1: Container Launch Overhead (Microbenchmark)[/bold cyan]\n"
                f"• Direct Host Execution:           {e1.get('direct', {}).get('stats_ms', {}).get('median', 0):.2f} ms\n"
                f"• MiniBox Baseline:                {e1.get('minibox_baseline', {}).get('stats_ms', {}).get('median', 0):.2f} ms (+{e1.get('minibox_baseline', {}).get('overhead_ms', 0):.2f} ms)\n"
                f"• MiniBox Namespace Isolated:      {e1.get('minibox_isolated', {}).get('stats_ms', {}).get('median', 0):.2f} ms (+{e1.get('minibox_isolated', {}).get('overhead_ms', 0):.2f} ms)\n"
                f"• MiniBox Full Isolation + cgroups:{e1.get('minibox_cgroups', {}).get('stats_ms', {}).get('median', 0):.2f} ms (+{e1.get('minibox_cgroups', {}).get('overhead_ms', 0):.2f} ms)\n\n"
                f"[bold cyan]Experiment 2: CFS Bandwidth Quota Precision[/bold cyan]\n"
                f"• Direct Host (100%):              {e2.get('direct_100pct', {}).get('stats_usage_pct', {}).get('median', 0):.1f}% CPU ({e2.get('direct_100pct', {}).get('stats_cpu_sec', {}).get('median', 0):.2f}s granted)\n"
                f"• MiniBox Unconstrained:           {e2.get('minibox_unconstrained', {}).get('stats_usage_pct', {}).get('median', 0):.1f}% CPU ({e2.get('minibox_unconstrained', {}).get('stats_cpu_sec', {}).get('median', 0):.2f}s granted)\n"
                f"• MiniBox 50% Quota:               {e2.get('minibox_quota_50pct', {}).get('stats_usage_pct', {}).get('median', 0):.1f}% CPU ({e2.get('minibox_quota_50pct', {}).get('stats_cpu_sec', {}).get('median', 0):.2f}s granted)\n"
                f"• MiniBox 25% Quota:               {e2.get('minibox_quota_25pct', {}).get('stats_usage_pct', {}).get('median', 0):.1f}% CPU ({e2.get('minibox_quota_25pct', {}).get('stats_cpu_sec', {}).get('median', 0):.2f}s granted)\n\n"
                f"[bold cyan]Experiment 3: Memory Allocation & Enforcement[/bold cyan]\n"
                f"• Direct Page Allocation (48M):    {mem_direct:.2f} ms\n"
                f"• MiniBox Unconstrained (48M):     {mem_unconst:.2f} ms\n"
                f"• MiniBox Sub-Ceiling (64M Limit): {mem_lim:.2f} ms\n"
                f"• Hard Ceiling (16M Limit / 64M):  {enf_status}\n\n"
                f"[bold cyan]Experiment 4: Process Spawning & PID Limit Enforcement[/bold cyan]\n"
                f"• Direct:                          {e4.get('direct_10_forks', {}).get('stats_ms', {}).get('median', 0):.2f} ms (exit code: {e4.get('direct_10_forks', {}).get('exit_codes', [2])[0]})\n"
                f"• MiniBox Constrained (pids.max=5):{e4.get('minibox_constrained_pids5', {}).get('stats_ms', {}).get('median', 0):.2f} ms (exit code: {e4.get('minibox_constrained_pids5', {}).get('exit_codes', [0])[0]})\n\n"
                f"[bold cyan]Experiment 5: Background Supervisor Lifecycle Latency[/bold cyan]\n"
                f"• Start Background Workload:       {e5.get('start_latency_ms', {}).get('median', 0):.2f} ms\n"
                f"• Inspect Workload Record:         {e5.get('inspect_latency_ms', {}).get('median', 0):.2f} ms\n"
                f"• Stop Workload (SIGTERM Reaping): {e5.get('stop_latency_ms', {}).get('median', 0):.2f} ms\n"
                f"• Cleanup Workload (Orderly rmdir):{e5.get('cleanup_latency_ms', {}).get('median', 0):.2f} ms\n"
            )
            return txt
        except Exception as e:
            return f"Error loading results: {e}"

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "btn-close-bench":
            self.dismiss(None)
        elif event.button.id == "btn-run-bench":
            if not self._running:
                asyncio.create_task(self._run_benchmark_task())

    async def _run_benchmark_task(self) -> None:
        """Run benchmark script asynchronously in background."""
        self._running = True
        status_lbl = self.query_one("#bench-status", Label)
        btn = self.query_one("#btn-run-bench", Button)
        btn.disabled = True
        status_lbl.update("[yellow]Running benchmark experiments (quick mode)... please wait...[/yellow]")

        bench_script = Path(__file__).resolve().parent.parent.parent / "benchmarks" / "benchmark.py"
        venv_py = Path(__file__).resolve().parent.parent.parent / ".venv" / "bin" / "python3"
        py_bin = str(venv_py) if venv_py.exists() else "python3"

        proc = await asyncio.create_subprocess_exec(
            py_bin,
            str(bench_script),
            "--quick",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        _, stderr_b = await proc.communicate()

        if proc.returncode == 0:
            status_lbl.update("[green]Benchmark completed! Results updated below.[/green]")
        else:
            err_msg = stderr_b.decode("utf-8", errors="replace").strip() or f"Process exited with code {proc.returncode}"
            status_lbl.update(f"[red]Benchmark failed: {err_msg}[/red]")

        btn.disabled = False
        self._running = False

        static_view = self.query_one("#bench-content-static", Static)
        static_view.update(self._format_results_text())

    def on_key(self, event) -> None:
        if event.key in ("escape", "q"):
            self.dismiss(None)
