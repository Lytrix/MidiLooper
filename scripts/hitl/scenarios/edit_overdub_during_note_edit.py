"""Edit + overdub during note edit integration scenario."""

from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Optional

from hitl.context import get_context

# Baseline-aligned overdub pitch bands (host_midi_automation_baseline defaults).
PRE_EDIT_OVERDUB_LOW = 24  # C1
PRE_EDIT_OVERDUB_HIGH = 39  # D#2
IN_EDIT_OVERDUB_LOW = 12  # C0
IN_EDIT_OVERDUB_HIGH = 35  # B1

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
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    parser.add_argument("--undo-redo-delay-ms", type=int, default=3000)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=8000)
    parser.add_argument(
        "--post-seed-settle-ms",
        type=int,
        default=3500,
        help="Wait after record_seed before overdub (SD reload)",
    )
    parser.add_argument(
        "--use-fixture-record",
        action="store_true",
        help="Dev only: skip record_seed and record EDIT_RECORD_FIXTURE in-scenario",
    )
    parser.add_argument("--start-transport", action="store_true")
    parser.add_argument("--clear-before-record", action="store_true", default=True)
    parser.add_argument("--clear-press-ms", type=int, default=900)
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_known_args(legacy)[0]

def run_edit_overdub_during_note_edit(args: object) -> int:
    import mido
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
        GLOBAL_TRANSPORT_NOTE,
        MIDI_CLOCKS_PER_BAR,
        OVERDUB_GRID_STEP_CLOCKS,
        RECORD_BUTTON_NOTE,
        TICKS_PER_BAR,
    )
    from hitl.serial_collector import (
        RunAbort,
        SerialCaptureCollector,
    )
    from hitl.midi_io import (
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )
    from hitl.capture_transitions import (
        _wait_for_transition_count,
        _count_capture_transitions,
    )
    from host_midi_automation_baseline import _run_overdub_pass
    from hitl.edit_controls import (
        _ensure_transport_running,
        _send_long_press,
        _stop_transport_if_running,
    )
    from host_midi_automation_edit_baseline import (
        DEFAULT_PERSISTENCE_WAIT_TIMEOUT_S,
        EDIT_RECORD_FIXTURE,
        RecordLayout,
        _build_fixture_step_to_tick,
        _build_select_navigation_slots,
        _create_note_at_bracket,
        _delete_selected_note,
        _ensure_clear_to_empty,
        _ensure_recording_started,
        _fader1_select_empty_fixture_step,
        _fader1_select_sixteenth_step,
        _fader1_select_then_wait_for_fader2,
        _fader2_move_to_sixteenth_step,
        _send_global_redo,
        _send_global_undo,
        _stream_fixture_record,
        _toggle_length_edit_mode,
        _track_cleared_for_record,
        _wait_for_persistence_result_after_marker,
        _can_skip_clear_for_record,
        _latest_track_state,
    )

    ns = _parse_common_args(args)
    ctx = get_context(args)
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    ctx.midi_channel = midi_channel
    ctx.record_bars = ns.record_bars

    use_base_seed = not ns.use_fixture_record
    record_layout = None
    if use_base_seed:
        from hitl.scenarios.edit_minimal import _resolve_base_seed

        try:
            _seed_lines, record_layout, seed_path = _resolve_base_seed(out_dir, ctx=ctx)
        except (FileNotFoundError, RuntimeError) as exc:
            print(f"[edit-overdub-hitl] {exc}")
            return 1
        print(f"[edit-overdub-hitl] using edit record seed: {seed_path}")

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
    shim.post_stream_settle_ms = 0
    shim.overdub_seconds = 2.0
    shim.root_note = 60
    shim.semitone_span = 12
    shim.note_gap_ms = 20
    shim.gate_ms = 80

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    abort = RunAbort()

    def pause() -> None:
        time.sleep(max(ns.phase_wait_ms, 1) / 1000.0)

    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
            serial_collector.start()

        if use_base_seed and ns.post_seed_settle_ms > 0:
            print(
                f"[edit-overdub-hitl] post-seed settle {ns.post_seed_settle_ms}ms "
                "(after edit record seed; loop reload from SD)"
            )
            time.sleep(ns.post_seed_settle_ms / 1000.0)

        track_index = ns.track_number - 1

        if use_base_seed:
            if record_layout is None:
                print("[edit-overdub-hitl] no record layout from seed")
                return 1
            if ns.start_transport:
                if not _ensure_transport_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                ):
                    print("[edit-overdub-hitl] warn: MIDI clock missing before track select")
            from hitl.control_constants import TRACK_SELECT_NOTE_BASE

            _send_short_press(
                out_port,
                note=TRACK_SELECT_NOTE_BASE + track_index,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            pause()
            ctx.record_layout = record_layout
            ctx.record_fixture = EDIT_RECORD_FIXTURE
        else:
            if ns.start_transport:
                if not _ensure_transport_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                ):
                    print("[edit-overdub-hitl] warn: MIDI clock missing before track select")

            _send_short_press(
                out_port,
                note=60 + track_index,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            pause()

            if ns.start_transport:
                if _stop_transport_if_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                ):
                    drained = _drain_input_messages(in_port)
                    if drained:
                        print(f"[edit-overdub-hitl] drained {drained} stale MIDI messages")

            if ns.clear_before_record and serial_collector is not None:
                snap = serial_collector.snapshot()
                if not _can_skip_clear_for_record(snap):
                    if not _ensure_clear_to_empty(
                        out_port,
                        serial_collector,
                        clear_press_ms=ns.clear_press_ms,
                        state_sync_timeout_ms=ns.state_sync_timeout_ms,
                        abort=abort,
                    ):
                        return 1
                    if not _track_cleared_for_record(serial_collector.snapshot()):
                        print(
                            f"[edit-overdub-hitl] clear failed latest="
                            f"{_latest_track_state(serial_collector.snapshot())}"
                        )
                        return 1

            if ns.start_transport:
                _send_short_press(
                    out_port,
                    note=GLOBAL_TRANSPORT_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=ns.press_ms,
                )
                pause()

            if serial_collector is not None:
                if not _ensure_recording_started(
                    out_port,
                    serial_collector,
                    press_ms=ns.press_ms,
                    state_sync_timeout_ms=ns.state_sync_timeout_ms,
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

            note_count, _ = _stream_fixture_record(
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
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            if serial_collector is not None:
                counts = _count_capture_transitions(serial_collector.snapshot())
                expected_play = counts.get(("STOPPED_RECORDING", "PLAYING"), 0) + 1
                _wait_for_transition_count(
                    serial_collector,
                    from_state="STOPPED_RECORDING",
                    to_state="PLAYING",
                    target_count=expected_play,
                    timeout_s=ns.state_sync_timeout_ms / 1000.0,
                    abort=abort,
                )
            if ns.start_transport:
                _ensure_transport_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                )
            pause()

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

        seconds_per_bar = 60.0 / shim.tempo_bpm * 4.0
        print("[edit-overdub-hitl] pre-edit overdub pass")
        _run_overdub_pass(
            track_index,
            out_port,
            in_port,
            serial_collector,
            abort,
            shim,
            overdub_bars=ns.overdub_bars,
            low_note=PRE_EDIT_OVERDUB_LOW,
            high_note=PRE_EDIT_OVERDUB_HIGH,
            step_clocks=OVERDUB_GRID_STEP_CLOCKS,
            gate_clocks=OVERDUB_GRID_STEP_CLOCKS,
            phase_start_delay_bars=0,
            phase_start_delay_beats=1,
            pass_label="pre-edit overdub",
            seconds_per_bar=seconds_per_bar,
        )
        ctx.markers.append("OverdubPassAdded")

        print("[edit-overdub-hitl] enter note edit")
        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        ctx.markers.append("entered note edit mode")
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

        print("[edit-overdub-hitl] in-edit overdub pass")
        _run_overdub_pass(
            track_index,
            out_port,
            in_port,
            serial_collector,
            abort,
            shim,
            overdub_bars=ns.overdub_bars,
            low_note=IN_EDIT_OVERDUB_LOW,
            high_note=IN_EDIT_OVERDUB_HIGH,
            step_clocks=OVERDUB_GRID_STEP_CLOCKS,
            gate_clocks=OVERDUB_GRID_STEP_CLOCKS,
            phase_start_delay_bars=0,
            phase_start_delay_beats=1,
            pass_label="in-edit overdub",
            seconds_per_bar=seconds_per_bar,
        )

        if getattr(args, "in_edit_undo_after_overdub", True):
            gap_s = max(ns.undo_redo_delay_ms, 0) / 1000.0
            time.sleep(gap_s)
            print("[edit-overdub-hitl] session undo after in-edit overdub")
            _send_global_undo(out_port, press_ms=ns.press_ms, label="in-edit overdub undo")
            ctx.markers.append("EditSession undo")
            time.sleep(gap_s)
            print("[edit-overdub-hitl] session redo after in-edit overdub undo")
            _send_global_redo(out_port, press_ms=ns.press_ms, label="in-edit overdub redo")
            ctx.markers.append("EditSession redo")
            time.sleep(gap_s)

        print("[edit-overdub-hitl] second edit pass move")
        _fader1_select_then_wait_for_fader2(out_port, layout=record_layout, fixture_step=4)
        pause()
        _fader2_move_to_sixteenth_step(out_port, layout=record_layout, fixture_step=0)
        ctx.markers.append("POSITION EDIT")
        pause()

        print("[edit-overdub-hitl] exit note edit")
        _send_long_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=700,
        )
        ctx.markers.append("exited edit mode")
        pause()

        if getattr(args, "post_exit_global_undo_redo", True):
            if serial_collector is not None:
                _wait_for_persistence_result_after_marker(
                    serial_collector,
                    marker="exited edit mode",
                    timeout_s=DEFAULT_PERSISTENCE_WAIT_TIMEOUT_S,
                    abort=abort,
                )
            gap_s = max(ns.undo_redo_delay_ms, 0) / 1000.0
            for step in range(2):
                time.sleep(gap_s)
                print(f"[edit-overdub-hitl] post-exit global undo {step + 1}/2")
                _send_global_undo(out_port, press_ms=ns.press_ms, label=f"post-exit undo {step + 1}")
            ctx.markers.append("Scoped edit pass undone")

        time.sleep(ns.final_wait_ms / 1000.0)

        exit_code = 0
        if serial_collector is not None:
            lines = serial_collector.snapshot()
            check = verify_edit_overdub_during_note_edit(lines, args)
            print(f"[edit-overdub-hitl] serial verification ok={check.get('ok')}")
            if check.get("issues"):
                for issue in check["issues"]:
                    print(f"  issue: {issue}")
            from datetime import datetime
            from pathlib import Path
            import json

            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            report_path = out_dir / f"host_midi_hitl_edit_overdub_during_note_edit_{stamp}.json"
            report_path.write_text(
                json.dumps(
                    {
                        "scenario": "edit_overdub_during_note_edit",
                        "track_number": ns.track_number,
                        "record_bars": ns.record_bars,
                        "overdub_bars": ns.overdub_bars,
                        "expected_pitch_bands": {
                            "pre_edit_overdub": {
                                "low": PRE_EDIT_OVERDUB_LOW,
                                "high": PRE_EDIT_OVERDUB_HIGH,
                            },
                            "in_edit_overdub": {
                                "low": IN_EDIT_OVERDUB_LOW,
                                "high": IN_EDIT_OVERDUB_HIGH,
                            },
                            "session_undo_targets": "in_edit_overdub_only",
                        },
                        "serial_verification": check,
                        "markers": ctx.markers,
                    },
                    indent=2,
                )
            )
            print(f"[edit-overdub-hitl] report: {report_path}")
            if not check.get("ok", False):
                exit_code = 2
        return exit_code
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()
        _drain_input_messages(in_port)

def verify_edit_overdub_during_note_edit(lines: list[str], args: object) -> dict[str, object]:
    from hitl.verify.edit_overdub_during_note_edit import (
        verify_edit_overdub_during_note_edit as _verify,
    )

    return _verify(lines, args)
