"""Record seed flow — Phase 1 skeleton (device behaviour in Phase 2)."""

from __future__ import annotations

from hitl.actions import buttons, transport
from hitl.context import ActionContext


def record_seed(action_ctx: ActionContext) -> None:
    """Clear slot, arm record, stream notes, stop — skeleton only in Phase 1."""
    transport.phase_wait(action_ctx)
    buttons.press_record(action_ctx)
    transport.phase_wait(action_ctx)
    action_ctx.markers.append("flow:record_seed:skeleton")


def clear_slot(action_ctx: ActionContext) -> None:
    """Placeholder for clear-to-empty precondition."""
    transport.phase_wait(action_ctx)
    action_ctx.markers.append("flow:clear_slot:skeleton")
