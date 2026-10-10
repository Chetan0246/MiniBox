from textual.app import ComposeResult
from textual.containers import Grid, Vertical, Horizontal
from textual.css.query import NoMatches
from textual.screen import ModalScreen
from textual.widgets import Button, Label, Checkbox


class ConfirmModal(ModalScreen[tuple[bool, bool]]):
    """Modal dialog to confirm actions like stopping or deleting a workload."""

    DEFAULT_CSS = """
    ConfirmModal {
        align: center middle;
    }
    #confirm-dialog {
        width: 60;
        height: auto;
        border: thick $primary;
        background: $surface;
        padding: 1 2;
    }
    #confirm-title {
        text-style: bold;
        color: $accent;
        margin-bottom: 1;
    }
    #confirm-msg {
        margin-bottom: 1;
    }
    #confirm-buttons {
        margin-top: 1;
        align: right middle;
    }
    #confirm-buttons Button {
        margin-left: 2;
    }
    """

    def __init__(self, title: str, message: str, show_force: bool = True):
        super().__init__()
        self.dialog_title = title
        self.message = message
        self.show_force = show_force

    def compose(self) -> ComposeResult:
        with Vertical(id="confirm-dialog"):
            yield Label(self.dialog_title, id="confirm-title")
            yield Label(self.message, id="confirm-msg")
            if self.show_force:
                yield Checkbox("Force action (SIGKILL / force delete)", id="force-checkbox")
            with Horizontal(id="confirm-buttons"):
                yield Button("Cancel", variant="default", id="btn-cancel")
                yield Button("Confirm", variant="error", id="btn-confirm")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "btn-confirm":
            force = False
            if self.show_force:
                try:
                    force = self.query_one("#force-checkbox", Checkbox).value
                except NoMatches:
                    force = False
            self.dismiss((True, force))
        else:
            self.dismiss((False, False))

    def on_key(self, event) -> None:
        if event.key == "escape":
            self.dismiss((False, False))
