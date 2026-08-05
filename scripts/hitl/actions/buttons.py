"""Button press actions."""

from __future__ import annotations

from hitl.context import ActionContext
from hitl.control_constants import (
    CONTROL_CHANNEL_1BASED,
    EDIT_BUTTON_NOTE,
    PLAY_STOP_BUTTON_NOTE,
    RECORD_BUTTON_NOTE,
)
from hitl.midi_io import _send_multi_short_press, _send_short_press


def press_record(action_ctx: ActionContext, *, press_ms: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(
        action_ctx.out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
    )


def press_play_stop(action_ctx: ActionContext, *, press_ms: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(
        action_ctx.out_port,
        note=PLAY_STOP_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
    )


def press_edit(action_ctx: ActionContext, *, press_ms: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(
        action_ctx.out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
    )


def press_undo(action_ctx: ActionContext, *, count: int = 1, press_ms: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_multi_short_press(
        action_ctx.out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
        count=count,
    )
