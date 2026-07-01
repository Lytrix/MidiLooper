"""Ensure LOOP_EDIT (not NOTE_EDIT) before record/overdub HITL."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    import mido
    from host_midi_automation_baseline import SerialCaptureCollector


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


def ensure_loop_edit_before_record(
    out_port: mido.ports.BaseOutput,
    serial_collector: Optional[SerialCaptureCollector],
    *,
    press_ms: int,
    phase_wait_ms: int,
    exit_press_ms: int = 700,
    timeout_s: float = 4.0,
) -> bool:
    """Leave NOTE_EDIT overlay and confirm LOOP_EDIT before capture HITL."""
    from host_midi_automation_baseline import CONTROL_CHANNEL_1BASED, _send_short_press
    from host_midi_automation_edit_baseline import (
        EDIT_BUTTON_DEBOUNCE_MS,
        EDIT_BUTTON_NOTE,
        _send_long_press,
    )

    if serial_collector is not None:
        if last_edit_session_kind(serial_collector.snapshot()) == "LOOP_EDIT":
            print("[hitl] LOOP_EDIT already active; skipping edit precondition")
            return True

    print("[hitl] ensure LOOP_EDIT before record (exit NOTE_EDIT if needed)")

    if serial_collector is not None:
        baseline = len(serial_collector.snapshot())
    else:
        baseline = 0

    _send_long_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=exit_press_ms,
    )
    time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)

    if serial_collector is not None:
        if _wait_for_loop_edit(
            serial_collector, baseline_line_count=baseline, timeout_s=timeout_s
        ):
            print("[hitl] LOOP_EDIT confirmed after edit long press")
            return True

        tail = serial_collector.snapshot()[baseline:]
        if last_edit_session_kind(tail) == "NOTE_EDIT":
            print("[hitl] still NOTE_EDIT; cycling edit session with short press")
            _send_short_press(
                out_port,
                note=EDIT_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=press_ms,
            )
            time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)
            if _wait_for_loop_edit(
                serial_collector, baseline_line_count=baseline, timeout_s=timeout_s
            ):
                print("[hitl] LOOP_EDIT confirmed after edit short press")
                return True

        print("[hitl] WARN: could not confirm LOOP_EDIT in serial; continuing")
        return False

    time.sleep(phase_wait_ms / 1000.0)
    return True
