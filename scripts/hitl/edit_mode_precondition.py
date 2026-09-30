"""Ensure the device is in LOOP_EDIT (not NOTE_EDIT) before record/overdub HITL.

**The Teensy always boots into LOOP_EDIT.** That is a boot-state fact, not a guarantee: the slot
select and long-press gestures a run performs (the pre-transport clear, a rollback, a manual
session) move the device out of LOOP_EDIT, and the record/overdub gestures mean something else
once it is gone (a loop-row select becomes geometry editing). So a run must **establish**
LOOP_EDIT explicitly before each record/overdub leg and prove it from the device's own serial
`Edit session: LOOP_EDIT` line -- never infer it from "we rebooted, so it must be LOOP_EDIT".

Relying on the boot state is what produced `missing_phase_boundaries` /
`overdub start refused: no predecessor stop-completion receipt` runs where the record leg
completed but the successor leg could not be admitted.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    import mido
    from hitl.serial_collector import SerialCaptureCollector


def last_edit_session_kind(lines: list[str]) -> str | None:
    last_kind: str | None = None
    for line in lines:
        if "Edit session: NOTE_EDIT" in line:
            last_kind = "NOTE_EDIT"
        elif "Edit session: LOOP_EDIT" in line:
            last_kind = "LOOP_EDIT"
    return last_kind


def loop_edit_confirmed_in_suffix(lines: list[str]) -> bool:
    kind = last_edit_session_kind(lines)
    if kind == "LOOP_EDIT":
        return True
    if kind == "NOTE_EDIT":
        return False
    return any("exited edit mode" in line for line in lines)


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


def _select_loop_row_to_enter_loop_edit(
    out_port: mido.ports.BaseOutput,
    *,
    track_number: int,
    loop_slot: int,
    press_ms: int,
    settle_ms: int,
) -> None:
    """Select the track, then its loop row: that selection is what enters LOOP_EDIT."""
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        LOOP_SELECT_NOTE_BASE,
        TRACK_SELECT_NOTE_BASE,
    )
    from hitl.midi_io import _send_short_press

    print(f"[hitl] select track {track_number} to enter LOOP_EDIT")
    _send_short_press(
        out_port,
        note=TRACK_SELECT_NOTE_BASE + track_number - 1,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(max(settle_ms, 1) / 1000.0)
    print(f"[hitl] select loop slot {loop_slot} to enter LOOP_EDIT")
    _send_short_press(
        out_port,
        note=LOOP_SELECT_NOTE_BASE + loop_slot - 1,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(max(settle_ms, 1) / 1000.0)


def ensure_loop_edit_before_record(
    out_port: mido.ports.BaseOutput,
    serial_collector: Optional[SerialCaptureCollector],
    *,
    press_ms: int,
    phase_wait_ms: int,
    track_number: int = 0,
    loop_slot: int = 0,
    exit_press_ms: int = 700,
    timeout_s: float = 2.0,
    skip: bool = False,
) -> bool:
    """Return True only when the device's serial evidence shows LOOP_EDIT.

    Establishes LOOP_EDIT rather than trusting the boot state:

    * serial shows ``LOOP_EDIT`` -> nothing to do (already proven);
    * serial shows ``NOTE_EDIT`` -> press the edit button to leave it, then require LOOP_EDIT;
    * serial shows neither (no edit session yet, or a mode this helper cannot name) -> **select
      the track's loop row explicitly**, which is what enters LOOP_EDIT, and require the
      confirmation. An unknown mode is not accepted: that silent pass is what let a run proceed
      with the record/overdub gestures meaning something else.

    Fails closed (named refusal) when LOOP_EDIT cannot be proven.
    """
    if skip:
        print("[hitl] skip LOOP_EDIT precondition (--no-loop-edit-precondition)")
        return True

    if serial_collector is None:
        return True

    snapshot = serial_collector.snapshot()
    kind = last_edit_session_kind(snapshot)

    if kind == "LOOP_EDIT":
        print("[hitl] LOOP_EDIT already active (device evidence)")
        return True

    if kind != "NOTE_EDIT":
        # The boot state is LOOP_EDIT, but this run may have left it (the pre-transport clear
        # selects rows and long-presses). Prove it instead of assuming it.
        if track_number <= 0 or loop_slot <= 0:
            print(
                "[hitl] edit mode is neither LOOP_EDIT nor NOTE_EDIT and no track/loop target was "
                "given; refusing rather than running with an unproven edit mode"
            )
            return False
        baseline = len(snapshot)
        _select_loop_row_to_enter_loop_edit(
            out_port,
            track_number=track_number,
            loop_slot=loop_slot,
            press_ms=press_ms,
            settle_ms=max(phase_wait_ms, 250),
        )
        if _wait_for_loop_edit(
            serial_collector, baseline_line_count=baseline, timeout_s=timeout_s
        ):
            print("[hitl] LOOP_EDIT established by selecting the loop row")
            return True
        print(
            "[hitl] LOOP_EDIT not established after selecting the loop row; refusing "
            "(the record/overdub gestures would mean something else)"
        )
        return False

    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_DEBOUNCE_MS,
        EDIT_BUTTON_NOTE,
    )
    from hitl.midi_io import _send_short_press

    print("[hitl] exit NOTE_EDIT before record")
    from hitl.recovery import log_action_outcome, request_state_transition

    def _send_exit() -> None:
        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)

    def _confirmed(lines: list[str], after: int) -> bool:
        return loop_edit_confirmed_in_suffix(lines[after:])

    def _still_note_edit(lines: list[str], after: int) -> bool:
        return last_edit_session_kind(lines[after:]) == "NOTE_EDIT"

    outcome = request_state_transition(
        snapshot=serial_collector.snapshot,
        requested="LOOP_EDIT",
        already_satisfied=lambda _lines, _baseline: False,
        confirmed=_confirmed,
        disproved=_still_note_edit,
        send_request=_send_exit,
        timeout_s=timeout_s,
        max_retries=1,
    )
    log_action_outcome(outcome)
    if outcome.ok:
        print("[hitl] left NOTE_EDIT after edit short press")
        return True
    print("[hitl] NOTE_EDIT exit not confirmed in serial; aborting")
    return False
