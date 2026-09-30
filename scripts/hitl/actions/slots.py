"""Slot and track press actions (channel 16).

The loop buttons and track buttons were reachable from the canonical args but not from the composable
action layer, so no flow could perform a *per-slot* gesture. That is what blocked the S4f.3 retention
stimulus: the sequence needs slot-level record / overdub / switch presses and a track selection.

Each helper is the **single owner of the SEND stage** of its gesture: when a caller passes `correlate`, the
stage is recorded by the code that put the note on the wire, together with the slot identity the caller can then
require its evidence to carry (task 3.1). `before_device_us` is the device clock the caller observed *before* the
press, so the wait can reject drained history.

Firmware mapping (`MidiButtonConfig.cpp`, channel 16 = `CONTROL_CHANNEL_1BASED`):

    loop buttons   notes LOOP_SELECT_NOTE_BASE + i   short  -> TOGGLE_RECORD_FOR_SLOT(i)
                                                     double -> OVERDUB_FOR_SLOT(i)
                                                     long   -> CLEAR_TRACK_FOR_SLOT(i)
    track buttons  notes TRACK_SELECT_NOTE_BASE + i  short  -> SELECT_TRACK(i)

Slot and track numbers here are 1-based, matching the CLI and the device's own labels; the note offsets
convert to the firmware's 0-based parameter.
"""

from __future__ import annotations

from hitl.actions.correlation import CorrelatedGesture, record_send
from hitl.context import ActionContext
from hitl.control_constants import (
    CONTROL_CHANNEL_1BASED,
    LOOP_SELECT_NOTE_BASE,
    TRACK_SELECT_NOTE_BASE,
)
from hitl.midi_io import _send_long_press, _send_multi_short_press, _send_short_press

# A long press must exceed the firmware's short/long discriminator; the canonical HITL clear uses this.
DEFAULT_LONG_PRESS_MS = 900
# Two presses inside the firmware's double-tap window, with enough separation to register as two.
DEFAULT_DOUBLE_GAP_MS = 80


def _slot_note(slot_number: int) -> int:
    if not 1 <= slot_number <= 8:
        raise ValueError(f"slot number must be 1-8, got {slot_number}")
    return LOOP_SELECT_NOTE_BASE + (slot_number - 1)


def _track_note(track_number: int) -> int:
    if not 1 <= track_number <= 8:
        raise ValueError(f"track number must be 1-8, got {track_number}")
    return TRACK_SELECT_NOTE_BASE + (track_number - 1)


def press_track(action_ctx: ActionContext, track_number: int, *, press_ms: int | None = None,
                correlate: CorrelatedGesture | None = None,
                before_device_us: int | None = None) -> None:
    """Short-press a track button: select that track row."""
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(action_ctx.out_port, note=_track_note(track_number),
                      channel_1based=CONTROL_CHANNEL_1BASED, press_ms=duration)
    record_send(correlate, note=_track_note(track_number), kind="short",
                before_device_us=before_device_us)


def press_slot_record(action_ctx: ActionContext, slot_number: int, *,
                      press_ms: int | None = None,
                      correlate: CorrelatedGesture | None = None,
                      before_device_us: int | None = None) -> None:
    """Short-press a loop button.

    This is the gesture the S4f.3 handoff depends on: while the track is capturing on another slot it
    routes to the capture-switch branch (`finalizeCaptureAndSelectSlot`), and while capturing on *this*
    slot it requests that interval's stop. On an empty slot under a running transport it queues a record.
    """
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_short_press(action_ctx.out_port, note=_slot_note(slot_number),
                      channel_1based=CONTROL_CHANNEL_1BASED, press_ms=duration)
    record_send(correlate, note=_slot_note(slot_number), kind="short",
                before_device_us=before_device_us)


def press_slot_overdub(action_ctx: ActionContext, slot_number: int, *,
                       press_ms: int | None = None, gap_ms: int = DEFAULT_DOUBLE_GAP_MS,
                       correlate: CorrelatedGesture | None = None,
                       before_device_us: int | None = None) -> None:
    """Double-press a loop button: `OVERDUB_FOR_SLOT` for that slot."""
    duration = action_ctx.config.press_ms if press_ms is None else press_ms
    _send_multi_short_press(action_ctx.out_port, note=_slot_note(slot_number),
                            channel_1based=CONTROL_CHANNEL_1BASED, press_ms=duration,
                            count=2, gap_ms=gap_ms)
    record_send(correlate, note=_slot_note(slot_number), kind="double",
                before_device_us=before_device_us)


def press_slot_clear(action_ctx: ActionContext, slot_number: int, *,
                     press_ms: int = DEFAULT_LONG_PRESS_MS,
                     correlate: CorrelatedGesture | None = None,
                     before_device_us: int | None = None) -> None:
    """Long-press a loop button: clear that slot. Destructive -- only for an authorized scratch slot."""
    _send_long_press(action_ctx.out_port, note=_slot_note(slot_number),
                     channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
    record_send(correlate, note=_slot_note(slot_number), kind="long",
                before_device_us=before_device_us)
