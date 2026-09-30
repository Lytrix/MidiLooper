"""Ensure the device is in LOOP_EDIT (not NOTE_EDIT) before record/overdub HITL.

**The Teensy always boots into LOOP_EDIT** -- `main.cpp` startup policy runs
`editManager.sendEditSessionChange(EditSessionType::Loop, true)`. That is a boot-state fact, not a
guarantee: the slot select and long-press gestures a run performs (the pre-transport clear, a
rollback, a manual session) move the device out of LOOP_EDIT, and the record/overdub gestures mean
something else once it is gone (a loop-row select becomes geometry editing). So a run must
**establish** LOOP_EDIT before each record/overdub leg and prove it from the device's own acks.

The device announces a mode only when a session event happens, which is why the mode is *caused*
here and never sampled for:

* ``Edit session: <MODE> (Program N, Note M trigger)`` -- ``sendEditSessionMidi``, on session *open*;
* ``MIDI Encoder: Long press - exited edit mode`` -- the edit button's exit action, which always ends
  in LOOP_EDIT (``EditManager::exitEditMode`` -> ``sendEditSessionChange(EditSessionType::Loop)``).

A capture that attached after the boot's session-open event holds none of them, so when the mode is
unproven the run presses the edit button **long** (note 38, ``MidiConfig::NOTE_EDIT_MODE``; the
button is bound short = cycle LOOP_EDIT <-> NOTE_EDIT, double = load/save overlay, long = exit note
edit, in ``MidiButtonConfig::loadConfiguration``) and reads the ack that press leaves. The long press
is used rather than the short one on purpose: a short press *cycles*, so what it lands in depends on
the mode the run is trying to prove, while the exit always lands in LOOP_EDIT. It is never pressed
twice inside the double-tap window, so the load/save overlay cannot open.

Relying on the boot state is what produced `missing_phase_boundaries` /
`overdub start refused: no predecessor stop-completion receipt` runs where the record leg completed
but the successor leg could not be admitted.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    import mido
    from hitl.serial_collector import SerialCaptureCollector

LOOP_EDIT = "LOOP_EDIT"
NOTE_EDIT = "NOTE_EDIT"

#: Rows that mean "the device left an editing overlay and is in LOOP_EDIT".
_LOOP_EDIT_DEPARTURE_ACKS = (
    "MIDI Encoder: Long press - exited edit mode",
    "reverted to loop edit",
    "Reopened loop edit session",
)

#: Rows that mean "an extraction of the session ack above". Kept separate because only the
#: ``Edit session:`` rows name the *resulting* mode; the departure rows name the transition.
_LOOP_EDIT_OPEN_ACK = "Edit session: LOOP_EDIT"
_NOTE_EDIT_OPEN_ACK = "Edit session: NOTE_EDIT"


def last_edit_session_kind(lines: list[str]) -> str | None:
    """Mode named by the last session-open ack in ``lines``, or None when the capture has none."""
    last_kind: str | None = None
    for line in lines:
        if _NOTE_EDIT_OPEN_ACK in line:
            last_kind = NOTE_EDIT
        elif _LOOP_EDIT_OPEN_ACK in line:
            last_kind = LOOP_EDIT
    return last_kind


def loop_edit_confirmed_in_suffix(lines: list[str]) -> bool:
    """True when the suffix shows LOOP_EDIT: an open ack, or a departure from an overlay."""
    kind = last_edit_session_kind(lines)
    if kind == LOOP_EDIT:
        return True
    if kind == NOTE_EDIT:
        return False
    return any(ack in line for line in lines for ack in _LOOP_EDIT_DEPARTURE_ACKS)


def _edit_ack_kind(lines: list[str]) -> str | None:
    """Mode the acks in ``lines`` name, or None when they name none."""
    kind = last_edit_session_kind(lines)
    if kind is not None:
        return kind
    if any(ack in line for line in lines for ack in _LOOP_EDIT_DEPARTURE_ACKS):
        return LOOP_EDIT
    return None


def _wait_for_edit_ack(
    collector: SerialCaptureCollector, *, baseline_line_count: int, timeout_s: float
) -> str | None:
    """Wait for a mode ack after ``baseline_line_count`` and return the mode it names."""
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        kind = _edit_ack_kind(collector.snapshot()[baseline_line_count:])
        if kind is not None:
            return kind
        time.sleep(0.05)
    return None


def _wait_for_loop_edit(
    collector: SerialCaptureCollector,
    *,
    baseline_line_count: int,
    timeout_s: float,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if loop_edit_confirmed_in_suffix(collector.snapshot()[baseline_line_count:]):
            return True
        time.sleep(0.05)
    return False


def _refuse_unproven() -> bool:
    print(
        "[hitl] LOOP_EDIT not proven: the long edit press left no session ack. A capture that "
        "attached after boot holds none (the boot's `Edit session: LOOP_EDIT` row predates it), and "
        "this press produced none either, so the mode cannot be shown. Rerun with --boot-reset so "
        "the boot ack is in the capture; the record/overdub gestures must not be trusted in an "
        "unproven mode."
    )
    return False


def ensure_loop_edit_before_record(
    out_port: mido.ports.BaseOutput,
    serial_collector: Optional[SerialCaptureCollector],
    *,
    phase_wait_ms: int,
    exit_press_ms: int = 700,
    timeout_s: float = 2.0,
    skip: bool = False,
) -> bool:
    """Return True only when the device's acks show LOOP_EDIT.

    * the capture already names ``LOOP_EDIT`` -> proven, no gesture;
    * otherwise the edit button is held **long** (its exit action) and the ack that press leaves is
      read: either ``Edit session: LOOP_EDIT`` or the ``exited edit mode`` row, both of which mean
      the device is in LOOP_EDIT now.

    Fails closed (named refusal, with the ``--boot-reset`` remedy) when no ack arrives: an unproven
    mode must not be trusted, because the record/overdub gestures mean something else in NOTE_EDIT.
    """
    if skip:
        print("[hitl] skip LOOP_EDIT precondition (--no-loop-edit-precondition)")
        return True

    if serial_collector is None:
        return True

    if _edit_ack_kind(serial_collector.snapshot()) == LOOP_EDIT:
        print("[hitl] LOOP_EDIT already proven by the capture's ack")
        return True

    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_DEBOUNCE_MS,
        EDIT_BUTTON_NOTE,
    )
    from hitl.midi_io import _record_control_press, _send_long_press

    baseline = len(serial_collector.snapshot())
    print("[hitl] edit mode unproven in this capture; long edit press to land in LOOP_EDIT")
    _record_control_press(
        "long_press",
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        caller="ensure_loop_edit_before_record",
    )
    _send_long_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=exit_press_ms,
    )
    time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)

    if _wait_for_edit_ack(
        serial_collector, baseline_line_count=baseline, timeout_s=timeout_s
    ) == LOOP_EDIT:
        print("[hitl] LOOP_EDIT proven by the ack the long edit press left")
        return True
    return _refuse_unproven()
