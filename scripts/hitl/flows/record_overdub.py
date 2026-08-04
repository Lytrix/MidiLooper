"""Record overdub flow — composes record_seed (Phase 1 skeleton)."""

from __future__ import annotations

from hitl.context import ActionContext
from hitl.flows import record_seed


def record_overdub(action_ctx: ActionContext) -> None:
    record_seed.record_seed(action_ctx)
    action_ctx.markers.append("flow:record_overdub:skeleton")
