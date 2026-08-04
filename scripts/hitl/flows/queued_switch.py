"""Queued slot switch flow — Phase 1 skeleton."""

from __future__ import annotations

from hitl.actions import transport
from hitl.context import ActionContext


def queued_switch(action_ctx: ActionContext) -> None:
    transport.phase_wait(action_ctx)
    action_ctx.markers.append("flow:queued_switch:skeleton")
