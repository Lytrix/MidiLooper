"""Long loop display flow — Phase 1 skeleton."""

from __future__ import annotations

from hitl.actions import transport
from hitl.context import ActionContext


def long_loop_display(action_ctx: ActionContext) -> None:
    transport.phase_wait(action_ctx)
    action_ctx.markers.append("flow:long_loop_display:skeleton")
