"""Transport and time-wait actions."""

from __future__ import annotations

import time

from hitl.context import ActionContext


def sleep_ms(action_ctx: ActionContext, ms: int) -> None:
    del action_ctx
    time.sleep(max(ms, 0) / 1000.0)


def phase_wait(action_ctx: ActionContext) -> None:
    sleep_ms(action_ctx, action_ctx.config.phase_wait_ms)
