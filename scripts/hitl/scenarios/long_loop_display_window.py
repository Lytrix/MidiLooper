"""Long-loop bounded window: NOTE_EDIT freeze, play/stop long-press snap, hold-to-track."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context

def _track_index(track_number_1based: int) -> int:
    """0-based track index — matches host_midi_automation_baseline first_track_index."""
    return track_number_1based - 1

def _effective_sync_timeout_s(record_bars: int, configured_ms: int) -> float:
    """Scale state-sync waits for long-loop stop finalize + deferred save."""
    scaled_ms = max(configured_ms, record_bars * 1000)
    return min(scaled_ms, 120_000) / 1000.0

def _track_select_note(track_number_1based: int) -> int:
    from hitl.control_constants import TRACK_SELECT_NOTE_BASE

    return TRACK_SELECT_NOTE_BASE + _track_index(track_number_1based)

def _ensure_note_edit_entered(
    out_port,
    serial_collector,
    *,
    press_ms: int,
    phase_wait_ms: int,
    timeout_s: float = 12.0,
) -> bool:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_DEBOUNCE_MS,
        EDIT_BUTTON_NOTE,
    )
    from hitl.midi_io import _send_short_press

    def _note_edit_active(lines: list[str]) -> bool:
        last_kind = ""
        for line in lines:
            if "Edit session: NOTE_EDIT" in line:
                last_kind = "NOTE_EDIT"
            elif "Edit session: LOOP_EDIT" in line:
                last_kind = "LOOP_EDIT"
        if last_kind == "NOTE_EDIT":
            return True
        return any("entered note edit mode" in line for line in lines)

    def _note_edit_entered_in_suffix(suffix: list[str]) -> bool:
        if any("Edit session: NOTE_EDIT" in line for line in suffix):
            return True
        if any("entered note edit mode" in line for line in suffix):
            return True
        if any("EditSession opened editPass=0" in line for line in suffix):
            return True
        if any("MIDI Edit Mode: Short press" in line for line in suffix):
            return True
        return False

    snapshot = serial_collector.snapshot()
    if _note_edit_active(snapshot):
        print("[long-loop-display-hitl] NOTE_EDIT already active")
        return True

    for attempt in range(1, 3):
        baseline = len(serial_collector.snapshot())
        print(f"[long-loop-display-hitl] enter NOTE_EDIT attempt {attempt}/2")
        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)
        deadline = time.monotonic() + max(timeout_s, 0.0)
        last_press_at = time.monotonic()
        while time.monotonic() < deadline:
            suffix = serial_collector.snapshot()[baseline:]
            if any("exited edit mode" in line for line in suffix):
                break
            if _note_edit_entered_in_suffix(suffix):
                print("[long-loop-display-hitl] NOTE_EDIT enter confirmed")
                return True
            if time.monotonic() - last_press_at >= 5.0:
                print("[long-loop-display-hitl] re-press edit (no enter confirmation yet)")
                _send_short_press(
                    out_port,
                    note=EDIT_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=press_ms,
                )
                last_press_at = time.monotonic()
                time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)
            time.sleep(0.05)
    return False

def _parse_common_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", type=int, default=5)
    parser.add_argument("--track", type=int, dest="track_number")
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--record-bars", type=int, default=24)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--long-press-ms", type=int, default=700)
    parser.add_argument(
        "--play-stop-long-press-ms",
        type=int,
        default=3100,
        help="Play/stop (note 40) long-press for window snap — must exceed firmware 3000ms threshold",
    )
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--freeze-wait-ms", type=int, default=8000)
    parser.add_argument("--hold-track-ms", type=int, default=4000)
    parser.add_argument("--start-transport", action="store_true", default=True)
    parser.add_argument("--clear-before-record", action="store_true", default=True)
    parser.add_argument("--clear-press-ms", type=int, default=900)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=8000)
    parser.add_argument("--boot-settle-ms", type=int, default=10000)
    parser.add_argument("--serial-grace-ms", type=int, default=3000)
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)

def run_long_loop_display_window(args: object) -> int:
    import mido
    from hitl.serial_transport import resolve_wall_tempo_for_proxy
    from hitl.transport_clock import ensure_transport_clock
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
        GLOBAL_TRANSPORT_NOTE,
        PLAY_STOP_BUTTON_NOTE,
        RECORD_BUTTON_NOTE,
        RECORD_GRID_STEP_CLOCKS,
    )
    from hitl.serial_collector import (
        RunAbort,
        SerialCaptureCollector,
    )
    from hitl.midi_io import (
        _find_midi_port,
        _send_short_press,
    )
    from hitl.capture_transitions import (
        _latest_track_state,
        _wait_for_transition_count,
    )
    from host_midi_automation_baseline import (
        _count_reca_markers,
        _recording_transition_baseline,
        _stream_pattern_for_bars,
        _wait_for_recording_started,
    )
    from hitl.edit_controls import (
        _ensure_transport_running,
        _send_long_press,
        _stop_transport_if_running,
    )
    from host_midi_automation_edit_baseline import _ensure_clear_to_empty

    ns = _parse_common_args(args)
    ctx = get_context(args)
    track_index = _track_index(ns.track_number)
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    ctx.record_bars = ns.record_bars
    setattr(args, "record_bars", ns.record_bars)

    if ns.record_bars <= 16:
        print("[long-loop-display-hitl] error: --record-bars must be > 16 for bounded window")
        return 2

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    abort = RunAbort()
    exit_code = 0

    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
            serial_collector.start()
            if ns.boot_settle_ms > 0:
                print(
                    f"[long-loop-display-hitl] boot settle {ns.boot_settle_ms}ms "
                    "(Teensy resets when serial opens)"
                )
                time.sleep(ns.boot_settle_ms / 1000.0)

        if ns.start_transport:
            _ensure_transport_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
                serial_collector=serial_collector,
                transport_args=ns,
            )

        print(
            f"[long-loop-display-hitl] select track {ns.track_number} "
            f"(index {track_index}, MIDI ch {midi_channel})"
        )
        _send_short_press(
            out_port,
            note=_track_select_note(ns.track_number),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        if serial_collector is not None:
            from hitl.edit_mode_precondition import ensure_loop_edit_before_record

            ensure_loop_edit_before_record(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )

        if ns.clear_before_record:
            if serial_collector is not None:
                # Recover transport clock after serial boot / prior STOPPED (same as base preset).
                if ns.start_transport:
                    _send_short_press(
                        out_port,
                        note=GLOBAL_TRANSPORT_NOTE,
                        channel_1based=CONTROL_CHANNEL_1BASED,
                        press_ms=ns.press_ms,
                    )
                    time.sleep(ns.phase_wait_ms / 1000.0)
                    clock_ok, _using_proxy = ensure_transport_clock(
                        in_port,
                        out_port,
                        serial_collector,
                        ns,
                        min_clocks=24,
                        timeout_s=2.0,
                        abort=abort,
                    )
                    if not clock_ok:
                        print(
                            "[long-loop-display-hitl] MIDI clock missing after transport "
                            "start; retrying transport"
                        )
                        _send_short_press(
                            out_port,
                            note=GLOBAL_TRANSPORT_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=ns.press_ms,
                        )
                        time.sleep(ns.phase_wait_ms / 1000.0)
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
                    print("[long-loop-display-hitl] clear failed")
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
                    serial_collector=serial_collector,
                    transport_args=ns,
                )

        print(f"[track {track_index}] record {ns.record_bars} bars")
        record_baseline_len = len(serial_collector.snapshot()) if serial_collector is not None else 0
        baseline_reca = (
            _count_reca_markers(serial_collector.snapshot()) if serial_collector is not None else 0
        )
        baseline_recording_transitions = (
            _recording_transition_baseline(serial_collector.snapshot())
            if serial_collector is not None
            else 0
        )
        baseline_latest_state = (
            _latest_track_state(serial_collector.snapshot()) if serial_collector is not None else None
        )
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        reached_recording = False
        if serial_collector is not None:
            reached_recording = _wait_for_recording_started(
                serial_collector,
                baseline_len=record_baseline_len,
                baseline_reca=baseline_reca,
                baseline_recording_transitions=baseline_recording_transitions,
                baseline_latest_state=baseline_latest_state,
                timeout_s=ns.state_sync_timeout_ms / 1000.0,
                serial_grace_s=ns.serial_grace_ms / 1000.0,
                abort=abort,
            )
            if not reached_recording:
                print("[long-loop-display-hitl] warn: RECORDING not confirmed; retrying record press")
                _send_short_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=ns.press_ms,
                )
                reached_recording = _wait_for_recording_started(
                    serial_collector,
                    baseline_len=record_baseline_len,
                    baseline_reca=baseline_reca,
                    baseline_recording_transitions=baseline_recording_transitions,
                    baseline_latest_state=baseline_latest_state,
                    timeout_s=ns.state_sync_timeout_ms / 1000.0,
                    serial_grace_s=ns.serial_grace_ms / 1000.0,
                    abort=abort,
                )
        else:
            time.sleep(ns.phase_wait_ms / 1000.0)

        if serial_collector is not None and not reached_recording:
            print("[long-loop-display-hitl] error: record did not reach RECORDING")
            return 1

        if not hasattr(ns, "tempo_bpm"):
            ns.tempo_bpm = 120.0
        clock_ok, using_serial_proxy = ensure_transport_clock(
            in_port,
            out_port,
            serial_collector,
            ns,
            min_clocks=24,
            timeout_s=3.0,
            abort=abort,
        )
        if not clock_ok:
            print("[long-loop-display-hitl] MIDI clock missing before record stream")
            return 1
        wall_clock_tempo = resolve_wall_tempo_for_proxy(
            serial_collector, ns, using_serial_proxy=using_serial_proxy
        )

        seconds_per_bar = 2.0
        guard = max(30.0, ns.record_bars * seconds_per_bar * 2.5)
        rec_notes, _rec_cc, rec_clock_count, _timing = _stream_pattern_for_bars(
            out_port,
            in_port,
            midi_channel_1based=midi_channel,
            low_note=36,
            high_note=51,
            step_clocks=RECORD_GRID_STEP_CLOCKS,
            gate_clocks=RECORD_GRID_STEP_CLOCKS,
            target_bars=ns.record_bars,
            cc_number=74,
            cc_step=9,
            pitch_cycle_bars=2,
            phase_start_delay_bars=0,
            phase_start_delay_beats=0,
            max_seconds_guard=guard,
            fixed_note=None,
            stop_press_advance_clocks=0,
            abort=abort,
            emit_immediate_first_step=True,
            wall_clock_tempo_bpm=wall_clock_tempo,
        )
        print(
            f"[long-loop-display-hitl] record stream notes={rec_notes} clocks={rec_clock_count}"
        )
        if rec_notes == 0 or rec_clock_count == 0:
            print(
                "[long-loop-display-hitl] error: no MIDI notes/clocks streamed during record "
                "(transport or RECORDING sync failed)"
            )
            return 1

        sync_timeout_s = _effective_sync_timeout_s(ns.record_bars, ns.state_sync_timeout_ms)
        time.sleep(0.5)

        expected_play = None
        stop_baseline_len = 0
        if serial_collector is not None:
            from hitl.capture_transitions import _count_capture_transitions

            counts = _count_capture_transitions(serial_collector.snapshot())
            expected_play = counts.get(("STOPPED_RECORDING", "PLAYING"), 0) + 1
            stop_baseline_len = len(serial_collector.snapshot())

        print("[long-loop-display-hitl] record stop (returns to play)")
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )

        if serial_collector is not None and expected_play is not None:
            reached_play = _wait_for_transition_count(
                serial_collector,
                from_state="STOPPED_RECORDING",
                to_state="PLAYING",
                target_count=expected_play,
                timeout_s=sync_timeout_s,
                abort=abort,
            )
            if not reached_play:
                print(
                    "[long-loop-display-hitl] warn: STOPPED_RECORDING->PLAYING not confirmed; "
                    "retrying record stop press"
                )
                _send_short_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=ns.press_ms,
                )
                reached_play = _wait_for_transition_count(
                    serial_collector,
                    from_state="STOPPED_RECORDING",
                    to_state="PLAYING",
                    target_count=expected_play,
                    timeout_s=sync_timeout_s,
                    abort=abort,
                )
            if not reached_play:
                print("[long-loop-display-hitl] warn: did not confirm STOPPED_RECORDING->PLAYING")

            if ns.record_bars >= 48:
                from hitl.deferred_save_idle import wait_for_deferred_save_idle

                print(
                    f"[long-loop-display-hitl] waiting for deferred save idle "
                    f"(timeout {sync_timeout_s:.0f}s)"
                )
                if not wait_for_deferred_save_idle(
                    serial_collector,
                    after_line_index=stop_baseline_len,
                    timeout_s=sync_timeout_s,
                    log_prefix="[long-loop-display-hitl]",
                ):
                    print("[long-loop-display-hitl] warn: deferred save idle not confirmed")

            playing_deadline = time.monotonic() + sync_timeout_s
            while time.monotonic() < playing_deadline:
                if _latest_track_state(serial_collector.snapshot()) == "PLAYING":
                    break
                time.sleep(0.05)
            else:
                print("[long-loop-display-hitl] warn: latest track state is not PLAYING before NOTE_EDIT")

            from hitl.scenarios.load_save_overlay_helpers import recover_load_save_overlay
            from hitl.edit_controls import _send_double_press

            recover_load_save_overlay(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                gap_ms=80,
                gesture_settle_ms=600,
                send_double_press=_send_double_press,
                log_prefix="[long-loop-display-hitl]",
            )

            time.sleep(2.0)

        time.sleep(ns.phase_wait_ms / 1000.0)

        print("[long-loop-display-hitl] enter NOTE_EDIT while playing")
        if serial_collector is not None:
            if not _ensure_note_edit_entered(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
                timeout_s=sync_timeout_s,
            ):
                lines = serial_collector.snapshot()
                out_dir = Path(getattr(args, "out_dir", Path("captures")))
                out_dir.mkdir(parents=True, exist_ok=True)
                stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                fail_path = out_dir / f"host_midi_hitl_long_loop_display_fail_{stamp}.log"
                fail_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                edit_tail = [
                    line
                    for line in lines[-200:]
                    if any(
                        token in line
                        for token in (
                            "Edit session",
                            "edit mode",
                            "Edit Mode",
                            "EditSession",
                            "LDSV",
                            "NOTE_EDIT",
                        )
                    )
                ]
                print(f"[long-loop-display-hitl] serial fail log: {fail_path}")
                if edit_tail:
                    print("[long-loop-display-hitl] recent edit-related serial:")
                    for line in edit_tail[-15:]:
                        print(f"  {line}")
                print("[long-loop-display-hitl] error: NOTE_EDIT enter not confirmed")
                return 1
        else:
            _send_short_press(
                out_port,
                note=EDIT_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            time.sleep(ns.phase_wait_ms / 1000.0)
        ctx.markers.append("entered note edit mode")
        time.sleep(ns.phase_wait_ms / 1000.0)

        print(f"[long-loop-display-hitl] freeze wait {ns.freeze_wait_ms}ms (window should stay fixed)")
        time.sleep(ns.freeze_wait_ms / 1000.0)
        ctx.markers.append("phase:freeze_wait_end")

        from hitl.control_constants import DISPLAY_SETTLE_MS

        print(
            f"[long-loop-display-hitl] play/stop long press ({ns.play_stop_long_press_ms}ms) "
            "— snap window to playhead"
        )
        _send_long_press(
            out_port,
            note=PLAY_STOP_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.play_stop_long_press_ms,
        )
        ctx.markers.append("phase:long_press_snap")
        time.sleep(max(DISPLAY_SETTLE_MS, ns.phase_wait_ms) / 1000.0)

        print(f"[long-loop-display-hitl] play/stop hold {ns.hold_track_ms}ms (window should track)")
        ch = CONTROL_CHANNEL_1BASED - 1
        out_port.send(
            mido.Message("note_on", channel=ch, note=PLAY_STOP_BUTTON_NOTE, velocity=127)
        )
        ctx.markers.append("phase:hold_track_start")
        time.sleep(ns.hold_track_ms / 1000.0)
        out_port.send(
            mido.Message("note_off", channel=ch, note=PLAY_STOP_BUTTON_NOTE, velocity=0)
        )
        ctx.markers.append("phase:hold_track_end")
        time.sleep(max(DISPLAY_SETTLE_MS, ns.phase_wait_ms) / 1000.0)

        print("[long-loop-display-hitl] exit NOTE_EDIT")
        _send_long_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.long_press_ms,
        )
        ctx.markers.append("exited edit mode")

        if serial_collector is not None:
            setattr(args, "phase_markers", list(ctx.markers))
            setattr(args, "hold_track_ms", ns.hold_track_ms)
            setattr(args, "hitl_context", ctx)
            if ns.serial_grace_ms > 0:
                time.sleep(ns.serial_grace_ms / 1000.0)
            lines = serial_collector.snapshot()
            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            serial_path = out_dir / f"host_midi_hitl_long_loop_display_serial_{stamp}.log"
            serial_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"[long-loop-display-hitl] serial log: {serial_path}")
            check = verify_long_loop_display_window(lines, args)
            print(f"[long-loop-display-hitl] serial verification ok={check.get('ok')}")
            for issue in check.get("issues", []):
                print(f"  issue: {issue}")
            for warning in check.get("warnings", []):
                print(f"  warn: {warning}")

            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            report_path = out_dir / f"host_midi_hitl_long_loop_display_window_{stamp}.json"
            report_path.write_text(
                json.dumps(
                    {
                        "scenario": "long_loop_display_window",
                        "track_number": ns.track_number,
                        "record_bars": ns.record_bars,
                        "freeze_wait_ms": ns.freeze_wait_ms,
                        "hold_track_ms": ns.hold_track_ms,
                        "serial_verification": check,
                        "markers": ctx.markers,
                    },
                    indent=2,
                )
            )
            print(f"[long-loop-display-hitl] report: {report_path}")
            if not check.get("ok", False):
                exit_code = 2
        else:
            print("[long-loop-display-hitl] warn: no --serial-port; skipping verification")
            exit_code = 1

        return exit_code
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()
        from hitl.midi_io import _drain_input_messages

        _drain_input_messages(in_port)

def verify_long_loop_display_window(lines: list[str], args: object) -> dict[str, object]:
    from hitl.verify.display_window import verify_long_loop_display_window as _verify

    return _verify(lines, args)
