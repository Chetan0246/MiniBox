from textual.app import ComposeResult
from textual.containers import Vertical, Horizontal, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Button, Label, Static


class HelpModal(ModalScreen[None]):
    """Help dialog and educational guide explaining OS concepts in MiniBox."""

    DEFAULT_CSS = """
    HelpModal {
        align: center middle;
    }
    #help-dialog {
        width: 82;
        height: 85%;
        border: thick $primary;
        background: $surface;
        padding: 1 2;
    }
    #help-title {
        text-style: bold;
        color: $primary;
        margin-bottom: 1;
        text-align: center;
    }
    #help-scroll {
        height: 1fr;
        border: solid $secondary;
        padding: 1;
        margin-bottom: 1;
    }
    .topic-title {
        text-style: bold;
        color: $accent;
        margin-top: 1;
    }
    .body-text {
        margin-left: 2;
    }
    #help-buttons {
        align: right middle;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical(id="help-dialog"):
            yield Label("MiniBox TUI: Help & Operating System Guide", id="help-title")
            with VerticalScroll(id="help-scroll"):
                yield Label("Keyboard Navigation", classes="topic-title")
                yield Static("• [N] : Launch new container workload via interactive form", classes="body-text")
                yield Static("• [Enter] or [I] : Inspect selected workload metadata and cgroups", classes="body-text")
                yield Static("• [S] : Stop running workload (SIGTERM / SIGKILL)", classes="body-text")
                yield Static("• [L] : Switch to Workload Output Logs tab", classes="body-text")
                yield Static("• [D] : Delete selected workload metadata and logs", classes="body-text")
                yield Static("• [C] : Clean up all finished/terminated workloads", classes="body-text")
                yield Static("• [R] : Immediate refresh of metrics and processes", classes="body-text")
                yield Static("• [H] or [?] : Open this help dialog", classes="body-text")
                yield Static("• [Q] : Gracefully shut down MiniBox TUI", classes="body-text")

                yield Label("Educational Linux Concepts: Namespaces", classes="topic-title")
                yield Static("Linux namespaces provide virtualized isolation boundaries for processes:", classes="body-text")
                yield Static("• UTS Namespace: Isolates nodename/hostname. The container sees a custom", classes="body-text")
                yield Static("  hostname without modifying the host system's actual hostname.", classes="body-text")
                yield Static("• PID Namespace: Virtualizes process IDs. Inside the container, the initial", classes="body-text")
                yield Static("  process sees itself as PID 1, with an independent process tree.", classes="body-text")
                yield Static("• Mount Namespace: Isolates mount tables, preventing host mount changes.", classes="body-text")
                yield Static("• User Namespace: Maps container root (UID 0) to unprivileged host UID,", classes="body-text")
                yield Static("  allowing safe unprivileged namespace configuration without root/sudo.", classes="body-text")

                yield Label("Educational Linux Concepts: cgroups v2", classes="topic-title")
                yield Static("Control Groups (cgroups v2) manage and enforce hardware resource limits:", classes="body-text")
                yield Static("• cpu.max: Completely controls CFS bandwidth quota. Overuse results in scheduler", classes="body-text")
                yield Static("  throttling, recorded under nr_throttled in cpu.stat.", classes="body-text")
                yield Static("• memory.max: Hard memory ceiling. Exceeding limits triggers kernel OOM-killer", classes="body-text")
                yield Static("  sending SIGKILL (exit status 137).", classes="body-text")
                yield Static("• pids.max: Restricts max process/thread creation, mitigating fork bombs with EAGAIN.", classes="body-text")

                yield Label("Process Supervision & Zero-Zombie Guarantees", classes="topic-title")
                yield Static("MiniBox uses double-fork background supervisors. The supervisor process stays", classes="body-text")
                yield Static("attached via waitpid() to harvest the workload's exit status immediately,", classes="body-text")
                yield Static("preventing defunct/zombie processes in the operating system.", classes="body-text")

            with Horizontal(id="help-buttons"):
                yield Button("Close (Esc)", variant="primary", id="btn-close-help")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(None)

    def on_key(self, event) -> None:
        if event.key in ("escape", "enter"):
            self.dismiss(None)
