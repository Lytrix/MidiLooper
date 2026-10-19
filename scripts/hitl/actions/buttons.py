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
    GLOBAL_TRANSPORT_NOTE,
    TRACK_BUTTON_NOTE,
)
from hitl.midi_io import _send_long_press, _send_multi_short_press, _send_short_press


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


def press_edit_double(action_ctx: ActionContext) -> None:
    """Double EDIT: toggle load/save overlay; caller proves the precondition."""
    _send_multi_short_press(action_ctx.out_port, note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED, press_ms=action_ctx.config.press_ms,
        count=2, gap_ms=80)


def press_overlay_up(action_ctx: ActionContext) -> None:
    """Global TRACK short: scroll overlay upward, clamped at row zero."""
    _send_short_press(action_ctx.out_port, note=TRACK_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED, press_ms=action_ctx.config.press_ms)


def press_transport(action_ctx: ActionContext) -> None:
    """Global transport toggle; caller must prove current running state first."""
    _send_short_press(action_ctx.out_port, note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED, press_ms=action_ctx.config.press_ms)


def press_edit_long(action_ctx: ActionContext) -> None:
    """Global EDIT long: exit note edit if closed, or overlay history/back; caller proves context."""
    from hitl.actions.slots import DEFAULT_LONG_PRESS_MS
    _send_long_press(action_ctx.out_port, note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED, press_ms=DEFAULT_LONG_PRESS_MS)
