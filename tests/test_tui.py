import asyncio
import pytest
from pathlib import Path
from textual.widgets import DataTable, Static, Label, TabbedContent, Input, Button

from tui.app import MiniBoxApp
from tui.screens.create_modal import CreateWorkloadModal
from tui.screens.inspect_modal import InspectWorkloadModal
from tui.screens.confirm_modal import ConfirmModal
from tui.screens.help_modal import HelpModal
from tui.screens.benchmark_modal import BenchmarkModal
from tui.client import MiniBoxClient


@pytest.fixture
def repo_root():
    return Path(__file__).resolve().parent.parent


@pytest.fixture
def binary_path(repo_root):
    return str(repo_root / "bin" / "minibox")


@pytest.mark.asyncio
async def test_client_capabilities_and_metrics(binary_path):
    """Test client low-level API against real C runtime."""
    client = MiniBoxClient(binary_path=binary_path)

    # Capabilities
    caps = await client.get_capabilities()
    assert caps.user_namespaces is True
    assert caps.uts_namespace is True

    # Host metrics
    host, workloads = await client.get_metrics()
    assert host.valid is True
    assert host.mem_total_bytes > 0
    assert isinstance(workloads, list)


@pytest.mark.asyncio
async def test_app_startup_and_dashboard_widgets(binary_path):
    """Test Textual app startup, capability detection, and host metrics."""
    app = MiniBoxApp(binary_path=binary_path)
    async with app.run_test() as pilot:
        await pilot.pause(0.5)

        # Header and capabilities
        header_caps = app.query_one("#header-caps", Label)
        assert "USER-NS" in str(header_caps.render())

        # Left panel host metrics
        cpu_stat = app.query_one("#stat-host-cpu", Static)
        assert "%" in str(cpu_stat.render())

        # Workload table
        table = app.query_one("#workloads-table", DataTable)
        assert table is not None
        assert len(table.columns) == 7


@pytest.mark.asyncio
async def test_help_modal_navigation(binary_path):
    """Test opening and dismissing HelpModal."""
    app = MiniBoxApp(binary_path=binary_path)
    async with app.run_test() as pilot:
        await pilot.press("h")
        await pilot.pause(0.2)
        assert isinstance(app.screen, HelpModal)

        await pilot.press("escape")
        await pilot.pause(0.2)
        assert not isinstance(app.screen, HelpModal)


@pytest.mark.asyncio
async def test_create_workload_validation_and_launch(binary_path):
    """Test CreateWorkloadModal template pre-fill, validation, and real execution."""
    app = MiniBoxApp(binary_path=binary_path)
    async with app.run_test() as pilot:
        # Open Create modal
        await pilot.press("n")
        await pilot.pause(0.3)
        assert isinstance(app.screen, CreateWorkloadModal)
        modal = app.screen

        # Test validation failure with empty executable
        submit_btn = modal.query_one("#btn-submit", Button)
        await pilot.click(submit_btn)
        await pilot.pause(0.2)
        err_lbl = modal.query_one("#lbl-validation-error", Label)
        assert "Executable path is required" in str(err_lbl.render())

        # Click Sleeper template
        tpl_sleeper = modal.query_one("#tpl-sleeper", Button)
        await pilot.click(tpl_sleeper)
        await pilot.pause(0.2)

        input_exec = modal.query_one("#input-exec", Input)
        assert "./bin/examples/sleeper" in input_exec.value

        # Submit valid form
        await pilot.click(submit_btn)
        await pilot.pause(1.0)

        # Modal should be dismissed and workload should appear
        assert not isinstance(app.screen, CreateWorkloadModal)
        table = app.query_one("#workloads-table", DataTable)
        assert table.row_count >= 1

        # Check total workload counter updated
        total_wl = app.query_one("#stat-total-wl", Static)
        assert int(str(total_wl.render())) >= 1

        # Clean up started workload
        selected_id = app._get_selected_workload_id()
        if selected_id:
            await app.client.stop_workload(selected_id, force=True)
            await app.client.delete_workload(selected_id, force=True)


@pytest.mark.asyncio
async def test_inspect_and_logs_and_cleanup(binary_path):
    """Test workload inspection modal, logs view tab, and bulk cleanup."""
    app = MiniBoxApp(binary_path=binary_path)
    async with app.run_test() as pilot:
        # Start a quick hello workload
        ok, msg, data = await app.client.start_workload(
            executable="./bin/examples/hello",
            name="test-tui-hello"
        )
        assert ok is True
        wl_id = data["workload_id"]

        # Refresh table
        await app.refresh_data()
        await pilot.pause(0.5)

        table = app.query_one("#workloads-table", DataTable)
        assert table.row_count >= 1

        # Inspect workload ('i')
        await pilot.press("i")
        await pilot.pause(0.3)
        assert isinstance(app.screen, InspectWorkloadModal)

        # Close inspect modal
        await pilot.press("escape")
        await pilot.pause(0.2)
        assert not isinstance(app.screen, InspectWorkloadModal)

        # View logs ('l')
        await pilot.press("l")
        await pilot.pause(0.5)
        tabs = app.query_one("#tabs-details", TabbedContent)
        assert tabs.active == "tab-logs"

        log_static = app.query_one("#log-content-static", Static)
        assert "Hello from MiniBox example workload" in str(log_static.render())

        # Bulk cleanup ('c')
        await pilot.press("c")
        await pilot.pause(0.5)
        assert table.row_count == 0


@pytest.mark.asyncio
async def test_benchmark_modal_navigation(binary_path):
    """Test opening and closing BenchmarkModal ('b')."""
    app = MiniBoxApp(binary_path=binary_path)
    async with app.run_test() as pilot:
        await pilot.press("b")
        await pilot.pause(0.2)
        assert isinstance(app.screen, BenchmarkModal)

        await pilot.press("escape")
        await pilot.pause(0.2)
        assert not isinstance(app.screen, BenchmarkModal)


@pytest.mark.asyncio
async def test_benchmark_modal_content_rendering(binary_path):
    """Test BenchmarkModal renders empirical metrics, timestamp, and enforcement status."""
    app = MiniBoxApp(binary_path=binary_path)
    async with app.run_test() as pilot:
        await pilot.press("b")
        await pilot.pause(0.2)
        assert isinstance(app.screen, BenchmarkModal)
        modal = app.screen
        content = modal.query_one("#bench-content-static", Static)
        rendered_text = str(content.render())
        assert "Benchmark Timestamp:" in rendered_text
        assert "Experiment 1: Container Launch Overhead" in rendered_text
        assert "Experiment 2: CFS Bandwidth Quota Precision" in rendered_text
        assert "Experiment 3: Memory Allocation & Enforcement" in rendered_text
        assert "VERIFIED (SIGKILL 9)" in rendered_text
        assert "Experiment 4: Process Spawning & PID Limit Enforcement" in rendered_text

        await pilot.press("escape")
        await pilot.pause(0.2)
        assert not isinstance(app.screen, BenchmarkModal)

