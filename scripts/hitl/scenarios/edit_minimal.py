"""Fast edit smoke: add, delete, move, length, exit."""

from __future__ import annotations

import argparse
import time
from typing import Optional

from hitl.context import get_context


def _parse_common_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--track-number", "--track", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--record-bars", type=int, default=2)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    parser.add_argument("--start-transport", action="store_true", default=True)
    parser.add_argument("--no-start-transport", action="store_false", dest="start_transport")
    parser.add_argument("--clear-before-record", action="store_true", default=True)
    parser.add_argument("--no-clear-before-record", action="store_false", dest="clear_before_record")
    parser.add_argument("--clear-press-ms", type=int, default=900)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=8000)
    parser.add_argument("--stop-press-advance-clocks", type=int, default=0)
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
        TRACK_SELECT_NOTE_BASE,
        _count_capture_transitions,
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
        _wait_for_transition_count,
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
        _ensure_clear_to_empty,
        _ensure_recording_started,
        _ensure_transport_running,
        _fader1_select_empty_fixture_step,
        _fader1_select_sixteenth_step,
        _fader1_select_then_wait_for_fader2,
        _fader2_move_to_sixteenth_step,
        _send_long_press,
        _stop_transport_if_running,
        _stream_fixture_record,
        _toggle_length_edit_mode,
        _wait_for_revt_count,
    )

    log_prefix = "[edit-minimal-hitl]"
    track_index = max(0, ns.track_number - 1)
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    abort = RunAbort()

    def pause() -> None:
        time.sleep(max(ns.phase_wait_ms, 1) / 1000.0)

    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=115200)
            serial_collector.start()

        if ns.start_transport:
            _ensure_transport_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )

        print(f"{log_prefix} select track {ns.track_number}")
        _send_short_press(
            out_port,
            note=TRACK_SELECT_NOTE_BASE + track_index,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        pause()

        if ns.clear_before_record:
            if serial_collector is not None:
                _stop_transport_if_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                )
                if not _ensure_clear_to_empty(
                    out_port,
                    serial_collector,
                    clear_press_ms=ns.clear_press_ms,
                    state_sync_timeout_ms=ns.state_sync_timeout_ms,
                    abort=abort,
                ):
                    print(f"{log_prefix} clear failed")
                    return 1
            else:
                _send_long_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=ns.clear_press_ms,
                )
            if ns.start_transport:
                _ensure_transport_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                )

        print(f"{log_prefix} record {ns.record_bars} bars")
        if serial_collector is not None:
            if not _ensure_recording_started(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                state_sync_timeout_ms=ns.state_sync_timeout_ms,
                abort=abort,
            ):
                print(f"{log_prefix} failed to enter RECORDING")
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

        note_count, clocks = _stream_fixture_record(
            out_port,
            in_port,
            fixture=EDIT_RECORD_FIXTURE,
            target_bars=ns.record_bars,
            midi_channel_1based=midi_channel,
            stop_press_advance_clocks=ns.stop_press_advance_clocks,
            press_ms=ns.press_ms,
            abort=abort,
        )
        print(f"{log_prefix} fixture note-ons={note_count} clocks={clocks}")
        if note_count < len(EDIT_RECORD_FIXTURE):
            print(
                f"{log_prefix} fixture incomplete: sent {note_count}/{len(EDIT_RECORD_FIXTURE)} "
                f"note-ons with {clocks} clocks"
            )
            return 1

        pause()
        expected_play_count: Optional[int] = None
        if serial_collector is not None:
            counts = _count_capture_transitions(serial_collector.snapshot())
            expected_play_count = counts.get(("STOPPED_RECORDING", "PLAYING"), 0) + 1

        if ns.stop_press_advance_clocks <= 0:
            print(f"{log_prefix} record stop")
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )

        if serial_collector is not None:
            if expected_play_count is not None:
                reached_play = _wait_for_transition_count(
                    serial_collector,
                    from_state="STOPPED_RECORDING",
                    to_state="PLAYING",
                    target_count=expected_play_count,
                    timeout_s=ns.state_sync_timeout_ms / 1000.0,
                    abort=abort,
                )
                if not reached_play:
                    print(f"{log_prefix} timed out waiting for STOPPED_RECORDING->PLAYING")
                    return 1
            if not _wait_for_revt_count(
                serial_collector,
                min_count=len(EDIT_RECORD_FIXTURE),
                timeout_s=max(ns.final_wait_ms / 1000.0, 3.0),
                abort=abort,
            ):
                print(f"{log_prefix} timed out waiting for REVT fixture notes")
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

        print(f"{log_prefix} enter note edit")
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
        ctx.markers.append("position_edit_requested")
        pause()

        _fader1_select_sixteenth_step(out_port, layout=record_layout, fixture_step=0)
        pause()
        _toggle_length_edit_mode(out_port, press_ms=ns.press_ms)
        _fader2_move_to_sixteenth_step(out_port, layout=record_layout, fixture_step=4)
        pause()
        _toggle_length_edit_mode(out_port, press_ms=ns.press_ms)
        ctx.markers.append("length_edit_requested")
        pause()

        _send_long_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=700,
        )
        ctx.markers.append("exited edit mode")
        print(f"{log_prefix} exited note edit")
        time.sleep(ns.final_wait_ms / 1000.0)

        if serial_collector is not None:
            check = verify_edit_minimal_scenario(serial_collector.snapshot(), args)
            if check.get("ok"):
                print(f"{log_prefix} Result: PASS")
            else:
                print(f"{log_prefix} Result: FAIL — {check.get('issues', check)}")
                return 1
        else:
            print(f"{log_prefix} Result: PASS (no serial verification)")
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
    issues: list[str] = []
    if not any("entered note edit mode" in line for line in lines):
        issues.append("missing_enter_note_edit")
    if not any("exited edit mode" in line for line in lines):
        issues.append("missing_exit_edit")
    enter = _verify_session_state_enter(lines)
    if not enter.get("ok"):
        issues.extend(enter.get("issues", []))

    serial_markers = (
        "Created 32nd note",
        "Deleting note",
    )
    for marker in serial_markers:
        if not any(marker in line for line in lines):
            issues.append(f"missing_serial:{marker}")

    return {"ok": not issues, "issues": issues, "enter": enter}
