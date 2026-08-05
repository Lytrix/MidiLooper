"""Note edit smoke flow — Phase 1 skeleton."""

from __future__ import annotations

from hitl.actions import buttons, transport
from hitl.context import ActionContext


def note_edit_smoke(action_ctx: ActionContext) -> None:
    buttons.press_edit(action_ctx)
    transport.phase_wait(action_ctx)
    action_ctx.markers.append("flow:note_edit_smoke:skeleton")
