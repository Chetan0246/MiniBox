import shlex
from typing import Optional, Dict, Any
from textual.app import ComposeResult
from textual.containers import Vertical, Horizontal, VerticalScroll, Grid
from textual.screen import ModalScreen
from textual.widgets import Button, Label, Input, Checkbox, Static


class CreateWorkloadModal(ModalScreen[Optional[Dict[str, Any]]]):
    """Modal dialog with interactive form to launch a new container workload."""

    DEFAULT_CSS = """
    CreateWorkloadModal {
        align: center middle;
    }
    #create-dialog {
        width: 86;
        height: 90%;
        border: thick $primary;
        background: $surface;
        padding: 1 2;
    }
    #create-title {
        text-style: bold;
        color: $primary;
        margin-bottom: 1;
        text-align: center;
    }
    #create-scroll {
        height: 1fr;
        padding: 0 1;
        margin-bottom: 1;
    }
    .field-label {
        color: $accent;
        text-style: bold;
        margin-top: 1;
    }
    .hint-label {
        color: $text-muted;
    }
    .error-label {
        color: $error;
        text-style: bold;
        margin-top: 1;
    }
    #templates-bar {
        height: 3;
        margin-bottom: 1;
        align: center middle;
    }
    #templates-bar Button {
        margin: 0 1;
    }
    #create-buttons {
        align: right middle;
    }
    #create-buttons Button {
        margin-left: 2;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical(id="create-dialog"):
            yield Label("Launch New Container Workload", id="create-title")

            yield Label("Quick Templates:", classes="hint-label")
            with Horizontal(id="templates-bar"):
                yield Button("Sleeper", id="tpl-sleeper", variant="default")
                yield Button("CPU Hog", id="tpl-cpuhog", variant="default")
                yield Button("Memory Hog", id="tpl-memhog", variant="default")
                yield Button("Fork Test", id="tpl-fork", variant="default")
                yield Button("Hello", id="tpl-hello", variant="default")

            with VerticalScroll(id="create-scroll"):
                yield Label("Executable Path (*required)", classes="field-label")
                yield Input(placeholder="./bin/examples/sleeper", id="input-exec")

                yield Label("Arguments", classes="field-label")
                yield Input(placeholder="10", id="input-args")

                yield Label("Workload Name (optional)", classes="field-label")
                yield Input(placeholder="my-sleeper-box", id="input-name")

                yield Label("Working Directory (optional)", classes="field-label")
                yield Input(placeholder="/tmp", id="input-workdir")

                yield Label("Resource Limits (cgroups v2)", classes="field-label")
                yield Label("CPU Quota % (e.g. 50 for 50% CPU limit, 0 for unlimited):", classes="hint-label")
                yield Input(placeholder="0", id="input-cpu")

                yield Label("Memory Limit (e.g. 64M, 256M, 1G, or empty for unlimited):", classes="hint-label")
                yield Input(placeholder="", id="input-mem")

                yield Label("Max Process Count (e.g. 10, or 0 for unlimited):", classes="hint-label")
                yield Input(placeholder="0", id="input-pids")

                yield Label("Linux Namespace Isolation", classes="field-label")
                yield Checkbox("Enable Full Isolation (UTS, PID, and Mount namespaces)", id="chk-isolate")
                yield Checkbox("Isolate UTS Namespace (hostname)", id="chk-uts")
                yield Label("Custom Hostname (requires UTS namespace):", classes="hint-label")
                yield Input(placeholder="minibox-host", id="input-hostname")
                yield Checkbox("Isolate PID Namespace (workload sees PID 1)", id="chk-pidns")
                yield Checkbox("Isolate Mount Namespace", id="chk-mountns")

                yield Label("", id="lbl-validation-error", classes="error-label")

            with Horizontal(id="create-buttons"):
                yield Button("Cancel (Esc)", variant="default", id="btn-cancel")
                yield Button("Launch Workload", variant="success", id="btn-submit")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        bid = event.button.id
        if bid == "btn-cancel":
            self.dismiss(None)
            return

        # Template handling
        if bid == "tpl-sleeper":
            self.query_one("#input-exec", Input).value = "./bin/examples/sleeper"
            self.query_one("#input-args", Input).value = "15"
            self.query_one("#input-name", Input).value = "sleeper-demo"
            return
        elif bid == "tpl-cpuhog":
            self.query_one("#input-exec", Input).value = "./bin/examples/cpu_hog"
            self.query_one("#input-args", Input).value = "10"
            self.query_one("#input-name", Input).value = "throttled-cpu"
            self.query_one("#input-cpu", Input).value = "40"
            return
        elif bid == "tpl-memhog":
            self.query_one("#input-exec", Input).value = "./bin/examples/memory_hog"
            self.query_one("#input-args", Input).value = "30 10"
            self.query_one("#input-name", Input).value = "mem-test"
            self.query_one("#input-mem", Input).value = "64M"
            return
        elif bid == "tpl-fork":
            self.query_one("#input-exec", Input).value = "./bin/examples/fork_test"
            self.query_one("#input-args", Input).value = "10"
            self.query_one("#input-name", Input).value = "fork-limit-test"
            self.query_one("#input-pids", Input).value = "8"
            return
        elif bid == "tpl-hello":
            self.query_one("#input-exec", Input).value = "./bin/examples/hello"
            self.query_one("#input-args", Input).value = ""
            self.query_one("#input-name", Input).value = "hello-box"
            return

        if bid == "btn-submit":
            self._validate_and_submit()

    def _validate_and_submit(self) -> None:
        err_lbl = self.query_one("#lbl-validation-error", Label)
        executable = self.query_one("#input-exec", Input).value.strip()
        if not executable:
            err_lbl.update("Error: Executable path is required.")
            return

        args_str = self.query_one("#input-args", Input).value.strip()
        args = []
        if args_str:
            try:
                args = shlex.split(args_str)
            except ValueError as e:
                err_lbl.update(f"Error: Malformed arguments quoting ({e}).")
                return

        name = self.query_one("#input-name", Input).value.strip() or None
        workdir = self.query_one("#input-workdir", Input).value.strip() or None

        # Parse CPU
        cpu_val = self.query_one("#input-cpu", Input).value.strip()
        cpu_pct = 0
        if cpu_val:
            try:
                cpu_pct = int(cpu_val)
                if cpu_pct < 0 or cpu_pct > 10000:
                    err_lbl.update("Error: CPU quota percentage must be between 1 and 10000.")
                    return
            except ValueError:
                err_lbl.update("Error: CPU quota must be an integer.")
                return

        # Parse Memory
        mem_str = self.query_one("#input-mem", Input).value.strip() or None

        # Parse PIDs
        pids_val = self.query_one("#input-pids", Input).value.strip()
        pids_max = 0
        if pids_val:
            try:
                pids_max = int(pids_val)
                if pids_max < 0 or pids_max > 4194304:
                    err_lbl.update("Error: Max PIDs must be positive integer up to 4194304.")
                    return
            except ValueError:
                err_lbl.update("Error: Max PIDs must be an integer.")
                return

        # Isolation options
        isolate = self.query_one("#chk-isolate", Checkbox).value
        uts = self.query_one("#chk-uts", Checkbox).value
        hostname = self.query_one("#input-hostname", Input).value.strip() or None
        pid_ns = self.query_one("#chk-pidns", Checkbox).value
        mount_ns = self.query_one("#chk-mountns", Checkbox).value

        result = {
            "executable": executable,
            "args": args,
            "name": name,
            "workdir": workdir,
            "cpu_pct": cpu_pct,
            "memory_str": mem_str,
            "pids_max": pids_max,
            "isolate": isolate,
            "uts": uts or bool(hostname),
            "hostname": hostname,
            "pid_ns": pid_ns,
            "mount_ns": mount_ns,
        }
        self.dismiss(result)

    def on_key(self, event) -> None:
        if event.key == "escape":
            self.dismiss(None)
