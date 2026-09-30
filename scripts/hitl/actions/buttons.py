"""Button press actions.

Each helper is the **single owner of the SEND stage** of its gesture: when a caller passes `correlate`, the
stage is recorded by the code that put the note on the wire, so no flow has to declare what it sent (task 3.1 of
`response-driven-hitl-acceptance`). `before_device_us` is the device clock the caller observed *before* the
press; pass it whenever the caller can read it, so the wait can reject drained history.
"""

from __future__ import annotations

from hitl.actions.correlation import CorrelatedGesture, record_send
from hitl.context import ActionContext
from hitl.control_constants import (
    CONTROL_CHANNEL_1BASED,
    EDIT_BUTTON_NOTE,
    PLAY_STOP_BUTTON_NOTE,
    RECORD_BUTTON_NOTE,
)
from hitl.midi_io import _send_multi_short_press, _send_short_press


def _press_kind(count: int) -> str:
    """The firmware's own classification name for a 1/2/3 short-press burst.

    The names are the ones `MidiButtonManager` prints (`short` / `double` / `triple`), so the SEND record speaks
    the device's vocabulary. The classification contract itself is the firmware's; a burst longer than the
    device distinguishes is named for what the host sent.
    """
    return {1: "short", 2: "double", 3: "triple"}.get(count, f"multi{count}")


def press_record(action_ctx: ActionContext, *, press_ms: int | None = None,
                 correlate: CorrelatedGesture | None = None,
                 before_device_us: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(
        action_ctx.out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
    )
    record_send(correlate, note=RECORD_BUTTON_NOTE, kind="short",
                before_device_us=before_device_us)


def press_play_stop(action_ctx: ActionContext, *, press_ms: int | None = None,
                    correlate: CorrelatedGesture | None = None,
                    before_device_us: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(
        action_ctx.out_port,
        note=PLAY_STOP_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
    )
    record_send(correlate, note=PLAY_STOP_BUTTON_NOTE, kind="short",
                before_device_us=before_device_us)


def press_edit(action_ctx: ActionContext, *, press_ms: int | None = None,
               correlate: CorrelatedGesture | None = None,
               before_device_us: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(
        action_ctx.out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
    )
    record_send(correlate, note=EDIT_BUTTON_NOTE, kind="short",
                before_device_us=before_device_us)


def press_undo(action_ctx: ActionContext, *, count: int = 1, press_ms: int | None = None,
               correlate: CorrelatedGesture | None = None,
               before_device_us: int | None = None) -> None:
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_multi_short_press(
        action_ctx.out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=duration,
        count=count,
    )
    record_send(correlate, note=RECORD_BUTTON_NOTE, kind=_press_kind(count),
                before_device_us=before_device_us)
