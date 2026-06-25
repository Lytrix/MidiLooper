"""Record + two overdub passes + global undo to EMPTY + global redo restore."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context
from hitl.verify.two_overdub_undo_redo import (
    REQUIRED_OVERDUB_PASSES,
    REQUIRED_REDO_STEPS,
    REQUIRED_UNDO_STEPS,
    verify_two_overdub_undo_redo,
)

# Baseline-aligned pitch bands (distinct per overdub pass).
OVERDUB_1_LOW = 24  # C1
OVERDUB_1_HIGH = 39  # D#2
OVERDUB_2_LOW = 12  # C0
OVERDUB_2_HIGH = 35  # B1

OVERDUB_PASSES: tuple[dict[str, object], ...] = (
    {
        "label": "overdub 1",
        "low_note": OVERDUB_1_LOW,
        "high_note": OVERDUB_1_HIGH,
        "step_clocks": 12,
        "gate_clocks": 12,
        "delay_bars": 0,
        "delay_beats": 1,
    },
    {
        "label": "overdub 2",
        "low_note": OVERDUB_2_LOW,
        "high_note": OVERDUB_2_HIGH,
        "step_clocks": 24,
        "gate_clocks": 24,
        "delay_bars": 0,
        "delay_beats": 1,
    },
)


def _parse_common_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--record-bars", type=int, default=2)
    parser.add_argument("--overdub-bars", type=int, default=2)
    parser.add_argument("--overdub-passes", type=int, default=REQUIRED_OVERDUB_PASSES)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    parser.add_argument("--undo-redo-delay-ms", type=int, default=3000)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=12000)
    parser.add_argument("--undo-steps", type=int, default=REQUIRED_UNDO_STEPS)
    parser.add_argument("--redo-steps", type=int, default=REQUIRED_REDO_STEPS)
    parser.add_argument("--start-transport", action="store_true", default=True)
    parser.add_argument("--clear-before-record", action="store_true", default=True)
    parser.add_argument("--clear-press-ms", type=int, default=900)
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)


def _build_shim(ns: argparse.Namespace, midi_channel: int) -> object:
    from host_midi_automation_baseline import OVERDUB_GRID_STEP_CLOCKS

    class _ArgsShim:
        pass

    shim = _ArgsShim()
    shim.press_ms = ns.press_ms
    shim.phase_wait_ms = ns.phase_wait_ms
    shim.state_sync_timeout_ms = ns.state_sync_timeout_ms
    shim.stop_press_advance_clocks = 0
    shim.overdub_bars = ns.overdub_bars
    shim.record_bars = ns.record_bars
    shim.tempo_bpm = 120.0
    shim.bar_sync_from_midi_clock = True
    shim.midi_channel = midi_channel
    shim.overdub_wrap_note_off_test = False
    shim.overdub_fixed_note = 36
    shim.fixed_grid_notes = False
    shim.cc_number = 74
    shim.cc_step = 9
    shim.pitch_cycle_bars = 2
    shim.post_stream_settle_ms = 120
    shim.overdub_seconds = 2.0
    shim.root_note = 60
    shim.semitone_span = 12
    shim.note_gap_ms = 20
    shim.gate_ms = 80
    shim.record_low_note = 48
    shim.record_high_note = 79
    shim.overdub_low_note = OVERDUB_1_LOW
    shim.overdub_high_note = OVERDUB_1_HIGH
    shim.overdub_start_delay_bars = 0
    shim.overdub_start_delay_beats = 1
    shim.second_overdub_bars = 0
    shim.overdub_step_clocks = OVERDUB_GRID_STEP_CLOCKS
    return shim


def run_two_overdub_undo_redo(args: object) -> int:
    import mido
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        RECORD_BUTTON_NOTE,
        RECORD_GRID_STEP_CLOCKS,
        TRACK_SELECT_NOTE_BASE,
        RunAbort,
        SerialCaptureCollector,
        _count_capture_transitions,
        _ensure_midi_clock,
        _find_midi_port,
        _run_overdub_pass,
        _send_short_press,
        _stream_pattern_for_bars,
        _wait_for_transition_count,
    )
    from host_midi_automation_edit_baseline import (
        _ensure_clear_to_empty,
        _ensure_recording_started,
        _ensure_transport_running,
        _latest_track_state,
        _send_global_redo,
        _send_global_undo,
        _stop_transport_if_running,
    )

    ns = _parse_common_args(args)
    setattr(args, "overdub_passes", ns.overdub_passes)
    setattr(args, "undo_steps", ns.undo_steps)
    setattr(args, "redo_steps", ns.redo_steps)
    ctx = get_context(args)
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    ctx.midi_channel = midi_channel
    ctx.record_bars = ns.record_bars
    shim = _build_shim(ns, midi_channel)
    track_index = ns.track_number - 1
    seconds_per_bar = 60.0 / (shim.tempo_bpm * 4.0)
    log_prefix = "[two-overdub-undo-redo-hitl]"

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    abort = RunAbort()
    exit_code = 0

    def pause() -> None:
        time.sleep(max(ns.phase_wait_ms, 1) / 1000.0)

    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
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
                from host_midi_automation_edit_baseline import _send_long_press

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
            pause()

        if not _ensure_midi_clock(in_port, out_port, min_clocks=24, timeout_s=3.0, abort=abort):
            print(f"{log_prefix} MIDI clock missing before record stream")
            return 1

        guard = max(10.0, ns.record_bars * seconds_per_bar * 3.0)
        rec_notes, _rec_cc, rec_clock_count, _rec_timing = _stream_pattern_for_bars(
            out_port,
            in_port,
            midi_channel_1based=midi_channel,
            low_note=shim.record_low_note,
            high_note=shim.record_high_note,
            step_clocks=RECORD_GRID_STEP_CLOCKS,
            gate_clocks=RECORD_GRID_STEP_CLOCKS,
            target_bars=ns.record_bars,
            cc_number=shim.cc_number,
            cc_step=shim.cc_step,
            pitch_cycle_bars=shim.pitch_cycle_bars,
            phase_start_delay_bars=0,
            phase_start_delay_beats=0,
            max_seconds_guard=guard,
            fixed_note=None,
            stop_press_advance_clocks=0,
            abort=abort,
            emit_immediate_first_step=True,
        )
        print(f"{log_prefix} record stream notes={rec_notes} clocks={rec_clock_count}")
        if rec_clock_count <= 0:
            print(f"{log_prefix} record saw 0 MIDI clock pulses")
            return 1

        expected_play = None
        if serial_collector is not None:
            counts = _count_capture_transitions(serial_collector.snapshot())
            expected_play = counts.get(("STOPPED_RECORDING", "PLAYING"), 0) + 1

        print(f"{log_prefix} record stop (returns to play)")
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        if serial_collector is not None and expected_play is not None:
            if not _wait_for_transition_count(
                serial_collector,
                from_state="STOPPED_RECORDING",
                to_state="PLAYING",
                target_count=expected_play,
                timeout_s=ns.state_sync_timeout_ms / 1000.0,
                abort=abort,
            ):
                latest = _latest_track_state(serial_collector.snapshot())
                print(f"{log_prefix} warn: did not confirm STOPPED_RECORDING->PLAYING (latest={latest})")
                return 1
        pause()

        for spec in OVERDUB_PASSES[: ns.overdub_passes]:
            reason, od_notes, _od_cc, od_clocks, _timing, _fallback = _run_overdub_pass(
                track_index,
                out_port,
                in_port,
                serial_collector,
                abort,
                shim,
                overdub_bars=ns.overdub_bars,
                low_note=int(spec["low_note"]),
                high_note=int(spec["high_note"]),
                step_clocks=int(spec["step_clocks"]),
                gate_clocks=int(spec["gate_clocks"]),
                phase_start_delay_bars=int(spec["delay_bars"]),
                phase_start_delay_beats=int(spec["delay_beats"]),
                pass_label=str(spec["label"]),
                seconds_per_bar=seconds_per_bar,
            )
            if reason:
                print(f"{log_prefix} {spec['label']} failed: {reason}")
                return 1
            if od_clocks <= 0:
                print(f"{log_prefix} {spec['label']} saw 0 MIDI clock pulses")
                return 1
            print(f"{log_prefix} {spec['label']} notes={od_notes} clocks={od_clocks}")
            ctx.markers.append(f"overdub_pass:{spec['label']}")

        gap_s = max(ns.undo_redo_delay_ms, 0) / 1000.0
        print(f"{log_prefix} wait {ns.undo_redo_delay_ms}ms before undo chain")
        time.sleep(gap_s)

        for step in range(ns.undo_steps):
            print(f"{log_prefix} global undo {step + 1}/{ns.undo_steps}")
            _send_global_undo(
                out_port,
                press_ms=ns.press_ms,
                label=f"undo {step + 1}/{ns.undo_steps}",
            )
            ctx.markers.append("Overdub undone")
            if step + 1 < ns.undo_steps:
                time.sleep(gap_s)

        print(f"{log_prefix} wait {ns.undo_redo_delay_ms}ms before redo chain")
        time.sleep(gap_s)

        for step in range(ns.redo_steps):
            print(f"{log_prefix} global redo {step + 1}/{ns.redo_steps}")
            _send_global_redo(
                out_port,
                press_ms=ns.press_ms,
                label=f"redo {step + 1}/{ns.redo_steps}",
            )
            ctx.markers.append("Overdub redone")
            if step + 1 < ns.redo_steps:
                time.sleep(gap_s)

        time.sleep(max(ns.final_wait_ms, 1) / 1000.0)

        if serial_collector is not None:
            lines = serial_collector.snapshot()
            check = verify_two_overdub_undo_redo(lines, args)
            print(f"{log_prefix} serial verification ok={check.get('ok')}")
            for issue in check.get("issues", []):
                print(f"  issue: {issue}")

            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            report_path = out_dir / f"host_midi_hitl_two_overdub_undo_redo_{stamp}.json"
            serial_log_path = out_dir / f"host_midi_hitl_two_overdub_undo_redo_serial_{stamp}.log"
            serial_log_path.write_text(
                "\n".join(lines) + ("\n" if lines else ""),
                encoding="utf-8",
            )
            report_path.write_text(
                json.dumps(
                    {
                        "scenario": "two_overdub_undo_redo",
                        "track_number": ns.track_number,
                        "record_bars": ns.record_bars,
                        "overdub_bars": ns.overdub_bars,
                        "overdub_passes": ns.overdub_passes,
                        "undo_steps": ns.undo_steps,
                        "redo_steps": ns.redo_steps,
                        "serial_verification": check,
                        "serial_log_path": str(serial_log_path),
                        "markers": ctx.markers,
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )
            print(f"{log_prefix} report: {report_path}")
            if not check.get("ok", False):
                exit_code = 2
        else:
            print(f"{log_prefix} warn: no --serial-port; skipping serial verification")
            exit_code = 2

    finally:
        out_port.close()
        in_port.close()
        if serial_collector is not None:
            serial_collector.stop()

    return exit_code
