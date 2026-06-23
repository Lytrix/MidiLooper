"""Fast edit smoke: add, delete, move, length, exit."""

from __future__ import annotations

import argparse
import time
from typing import Optional

from hitl.context import get_context


def _parse_common_args(args: object) -> argparse.Namespace:
    if isinstance(args, argparse.Namespace):
        return args
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--track-number", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--record-bars", type=int, default=2)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)


def run_edit_minimal_scenario(args: object) -> int:
    import host_midi_automation_edit_baseline as edit

    ns = _parse_common_args(args)
    ctx = get_context(args)
    ctx.record_bars = ns.record_bars

    import mido
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        RECORD_BUTTON_NOTE,
        RunAbort,
        SerialCaptureCollector,
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )
    from host_midi_automation_edit_baseline import (
        EDIT_BUTTON_NOTE,
        EDIT_RECORD_FIXTURE,
        TICKS_PER_BAR,
        RecordLayout,
        _build_fixture_step_to_tick,
        _build_select_navigation_slots,
        _create_note_at_bracket,
        _delete_selected_note,
        _ensure_recording_started,
        _fader1_select_empty_fixture_step,
        _fader1_select_sixteenth_step,
        _fader1_select_then_wait_for_fader2,
        _fader2_move_to_sixteenth_step,
        _send_long_press,
        _stream_fixture_record,
        _toggle_length_edit_mode,
    )

    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    abort = RunAbort()
    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=115200)
            serial_collector.start()

        if serial_collector is not None:
            if not _ensure_recording_started(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                state_sync_timeout_ms=8000,
                abort=abort,
            ):
                return 1
        else:
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            time.sleep(0.12)
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )

        note_count, _clocks = _stream_fixture_record(
            out_port,
            in_port,
            fixture=EDIT_RECORD_FIXTURE,
            target_bars=ns.record_bars,
            midi_channel_1based=midi_channel,
            stop_press_advance_clocks=0,
            press_ms=ns.press_ms,
            abort=abort,
        )
        if note_count < len(EDIT_RECORD_FIXTURE):
            return 1

        record_layout = RecordLayout(
            loop_start=0,
            loop_length=ns.record_bars * TICKS_PER_BAR,
            step_to_tick=_build_fixture_step_to_tick(
                [(n.step * 48, n.pitch) for n in EDIT_RECORD_FIXTURE],
                EDIT_RECORD_FIXTURE,
                loop_length=ns.record_bars * TICKS_PER_BAR,
            ),
            nav_slots=_build_select_navigation_slots(
                [(n.step * 48, n.pitch) for n in EDIT_RECORD_FIXTURE],
                loop_length=ns.record_bars * TICKS_PER_BAR,
                loop_start=0,
            ),
        )
        ctx.record_layout = record_layout
        ctx.record_fixture = EDIT_RECORD_FIXTURE
        ctx.midi_channel = midi_channel
        ctx.markers.append("entered note edit mode")

        pause = lambda: time.sleep(max(ns.phase_wait_ms, 1) / 1000.0)

        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        pause()

        if _fader1_select_empty_fixture_step(out_port, layout=record_layout, fixture_step=1):
            pause()
            _create_note_at_bracket(out_port, press_ms=ns.press_ms)
            ctx.markers.append("Created 32nd note")
            pause()
            _delete_selected_note(out_port, press_ms=ns.press_ms)
            ctx.markers.append("Deleting note")
            pause()

        _fader1_select_then_wait_for_fader2(out_port, layout=record_layout, fixture_step=0)
        pause()
        _fader2_move_to_sixteenth_step(out_port, layout=record_layout, fixture_step=4)
        ctx.markers.append("POSITION EDIT")
        pause()

        _fader1_select_sixteenth_step(out_port, layout=record_layout, fixture_step=0)
        pause()
        _toggle_length_edit_mode(out_port, press_ms=ns.press_ms)
        _fader2_move_to_sixteenth_step(out_port, layout=record_layout, fixture_step=4)
        pause()
        _toggle_length_edit_mode(out_port, press_ms=ns.press_ms)
        ctx.markers.append("LENGTH EDIT")
        pause()

        _send_long_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=700,
        )
        ctx.markers.append("exited edit mode")
        time.sleep(ns.final_wait_ms / 1000.0)
        return 0
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()
        _drain_input_messages(in_port)


def verify_edit_minimal_scenario(lines: list[str], args: object) -> dict[str, object]:
    from host_midi_automation_edit_baseline import _verify_session_state_enter

    ctx = getattr(args, "hitl_context", None)
    markers = ctx.markers if ctx else ["entered note edit mode", "exited edit mode"]
    ok = any("entered note edit mode" in line for line in lines)
    ok = ok and any("exited edit mode" in line for line in lines)
    enter = _verify_session_state_enter(lines)
    issues: list[str] = []
    if not ok:
        issues.append("missing_enter_or_exit")
    if not enter.get("ok"):
        issues.extend(enter.get("issues", []))
    for marker in markers:
        if not any(marker in line for line in lines):
            issues.append(f"missing_marker:{marker}")
    return {"ok": ok and enter.get("ok", False) and not issues, "issues": issues, "enter": enter}
