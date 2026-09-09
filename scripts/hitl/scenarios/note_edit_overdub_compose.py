"""NOTE_EDIT plus in-edit overdub compose cases after a 2-bar + one-overdub seed."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

from hitl.context import get_context
from hitl.stimuli import (
    STIM_NE_OD_COMPOSE_01,
    format_fingerprint_text,
    format_human_checklist,
    prepend_hitl_header,
    run_stimulus,
    stimulus_report_fields,
)
from hitl.verify.note_edit_overdub_compose import (
    LIVE_CAPTURE_HIGH,
    LIVE_CAPTURE_LOW,
    verify_note_edit_overdub_compose as _verify_note_edit_overdub_compose,
)

IN_EDIT_OVERDUB_BARS = 2
LIVE_STREAM_BARS = 1


def _parse_scenario_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--follow-current-session", action="store_true")
    parser.add_argument("--follow-serial-log", type=Path, default=None)
    parser.add_argument("--track-number", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    parser.add_argument("--post-seed-settle-ms", type=int, default=1500)
    parser.add_argument("--edit-enter-timeout-s", type=float, default=4.0)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=8000)
    parser.add_argument("--boot-settle-ms", type=int, default=0)
    parser.add_argument("--tempo-bpm", type=float, default=120.0)
    parser.add_argument(
        "--seed-serial-log",
        type=Path,
        default=None,
        help="Override seed serial log (dev only; prefer the compose preset)",
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    ns, _unknown = parser.parse_known_args(legacy)
    ns.out_dir = Path(getattr(args, "out_dir", Path("captures")))
    return ns


def _open_serial_collector(ns: argparse.Namespace) -> Any:
    from hitl.serial_transport import serial_follow_active

    if ns.serial_port:
        from hitl.serial_collector import SerialCaptureCollector

        collector = SerialCaptureCollector(ns.serial_port, ns.serial_baud)
        collector.start()
        boot_ms = max(ns.boot_settle_ms, 0)
        if boot_ms > 0:
            print(f"[note-edit-overdub-compose] boot settle {boot_ms}ms")
            time.sleep(boot_ms / 1000.0)
        else:
            time.sleep(1.5)
        print(f"[note-edit-overdub-compose] serial capture enabled: {ns.serial_port}")
        return collector

    if serial_follow_active(ns):
        from hitl.serial_follow import ExternalSerialFollowCollector

        follow_path = (
            ExternalSerialFollowCollector.resolve_current_session_path(ns.out_dir)
            if ns.follow_current_session
            else ns.follow_serial_log
        )
        if follow_path is None:
            raise RuntimeError("--follow-serial-log requires a path")
        collector = ExternalSerialFollowCollector(follow_path, out_dir=ns.out_dir)
        collector.start()
        time.sleep(0.15)
        print(f"[note-edit-overdub-compose] serial follow enabled: {follow_path}")
        return collector

    raise RuntimeError(
        "serial required: managed capture (--follow-current-session) or --serial-port"
    )


def _require_base_seed(
    out_dir: Path,
    seed_serial_log: Optional[Path],
    *,
    ctx: object,
) -> tuple[list[str], dict, Path | None]:
    from hitl.baseline_loop_inventory import (
        base_preset_config,
        base_report_usable_for_note_edit_sweep,
        latest_base_report,
        resolve_base_seed_serial_log,
    )

    report = getattr(ctx, "base_report", None)
    if getattr(ctx, "base_preset_passed", False):
        seed_path = resolve_base_seed_serial_log(
            report,
            out_dir,
            explicit=getattr(ctx, "base_serial_log_path", None),
        )
        if seed_path is not None:
            return (
                seed_path.read_text(encoding="utf-8", errors="replace").splitlines(),
                base_preset_config(report),
                seed_path,
            )

    if seed_serial_log is not None:
        if not seed_serial_log.is_file():
            raise FileNotFoundError(seed_serial_log)
        report = latest_base_report(out_dir)
        return (
            seed_serial_log.read_text(encoding="utf-8", errors="replace").splitlines(),
            base_preset_config(report),
            seed_serial_log,
        )

    report = latest_base_report(out_dir)
    if not base_report_usable_for_note_edit_sweep(report):
        raise RuntimeError(
            "No usable 2-bar + one-overdub seed. Run:\n"
            "  .venv/bin/python scripts/host_midi_hitl.py run "
            "--preset note_edit_overdub_compose "
            "--midi-out Teensy --midi-in Teensy --track-number 5 --loop-slot 1 "
            "--midi-channel 5"
        )
    seed_path = resolve_base_seed_serial_log(report, out_dir)
    if seed_path is None:
        raise FileNotFoundError("no seed serial log (report path or captures/.current_session)")
    return (
        seed_path.read_text(encoding="utf-8", errors="replace").splitlines(),
        base_preset_config(report),
        seed_path,
    )


def _first_note_slot_index(layout) -> Optional[int]:
    for index, slot in enumerate(layout.nav_slots):
        if getattr(slot, "note_idx", -1) >= 0:
            return index
    return None


def _stream_live_capture_bar(
    out_port,
    in_port,
    serial_collector,
    *,
    ns: argparse.Namespace,
    midi_channel: int,
    abort,
) -> int:
    from hitl.control_constants import OVERDUB_GRID_STEP_CLOCKS
    from hitl.serial_transport import resolve_wall_tempo_for_proxy
    from hitl.transport_clock import ensure_transport_clock, stream_pattern_for_bars

    clock_ok, using_proxy = ensure_transport_clock(
        in_port,
        out_port,
        serial_collector,
        ns,
        min_clocks=24,
        timeout_s=2.0,
        abort=abort,
    )
    if not clock_ok:
        print("[note-edit-overdub-compose] warn: clock missing before live Capture stream")
        return 0
    wall_tempo = resolve_wall_tempo_for_proxy(
        serial_collector, ns, using_serial_proxy=using_proxy
    )
    notes, _cc, _clocks, _timing = stream_pattern_for_bars(
        out_port,
        in_port,
        midi_channel_1based=midi_channel,
        low_note=LIVE_CAPTURE_LOW,
        high_note=LIVE_CAPTURE_HIGH,
        step_clocks=OVERDUB_GRID_STEP_CLOCKS,
        gate_clocks=OVERDUB_GRID_STEP_CLOCKS,
        target_bars=LIVE_STREAM_BARS,
        cc_number=74,
        cc_step=9,
        pitch_cycle_bars=2,
        phase_start_delay_bars=0,
        phase_start_delay_beats=0,
        max_seconds_guard=max(10.0, LIVE_STREAM_BARS * 8.0),
        abort=abort,
        emit_immediate_first_step=True,
        wall_clock_tempo_bpm=wall_tempo,
    )
    return notes


def run_note_edit_overdub_compose(args: object) -> int:
    import mido
    from hitl.capture_transitions import (
        _wait_for_overdubbing_active,
        _wait_for_transition_count,
        _count_capture_transitions,
    )
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
        FADER_SELECT_SETTLE_MS,
        NOTE_SELECTION_GRACE_MS,
        RECORD_BUTTON_NOTE,
        TICKS_PER_16TH_STEP,
        TRACK_SELECT_NOTE_BASE,
    )
    from hitl.edit_controls import _ensure_transport_running, _send_long_press
    from hitl.midi_io import _find_midi_port, _send_short_press
    from hitl.serial_collector import RunAbort
    from hitl.scenarios.edit_minimal import _ensure_note_edit_entered
    from hitl.baseline_loop_inventory import (
        nav_slot_stats,
        record_layout_from_base_seed,
        materialized_note_pairs_from_base_seed,
    )
    from hitl.legacy_edit_baseline import (
        _fader1_select_nav_slot_index,
        _fader2_move_to_sixteenth_step,
    )

    spec = run_stimulus(STIM_NE_OD_COMPOSE_01)
    ns = _parse_scenario_args(args)
    ctx = get_context(args)
    out_dir = ns.out_dir
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number

    try:
        seed_lines, base_config, seed_path = _require_base_seed(
            out_dir, ns.seed_serial_log, ctx=ctx
        )
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"[note-edit-overdub-compose] {exc}")
        return 1

    layout = record_layout_from_base_seed(seed_lines, base_config)
    note_slot = _first_note_slot_index(layout)
    if note_slot is None:
        print("[note-edit-overdub-compose] no note slot in seed layout")
        return 1
    loop_length, note_pairs = materialized_note_pairs_from_base_seed(seed_lines, base_config)
    stats = nav_slot_stats(note_pairs, loop_length=loop_length)
    print(
        f"[note-edit-overdub-compose] seed={seed_path} loop_length={loop_length} "
        f"notes={stats['note_pairs']} nav_slots={stats['nav_slots']}"
    )
    print(format_fingerprint_text(spec))

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector = None
    abort = RunAbort()
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    serial_path = out_dir / f"note_edit_overdub_compose_{stamp}_serial.log"

    try:
        serial_collector = _open_serial_collector(ns)
        abort.serial_collector = serial_collector
        if ns.post_seed_settle_ms > 0:
            time.sleep(ns.post_seed_settle_ms / 1000.0)

        if not _ensure_transport_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        ):
            print("[note-edit-overdub-compose] warn: MIDI clock missing after transport start")

        track_index = ns.track_number - 1
        _send_short_press(
            out_port,
            note=TRACK_SELECT_NOTE_BASE + track_index,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        if not _ensure_note_edit_entered(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            timeout_s=ns.edit_enter_timeout_s,
            log_prefix="[note-edit-overdub-compose]",
        ):
            print("[note-edit-overdub-compose] FAILED: NOTE_EDIT enter not confirmed")
            return 1
        ctx.markers.append("NOTE_EDIT open")

        _fader1_select_nav_slot_index(
            out_port,
            layout=layout,
            slot_index=note_slot,
        )
        time.sleep((FADER_SELECT_SETTLE_MS + NOTE_SELECTION_GRACE_MS) / 1000.0)
        ctx.markers.append("selected seed note")

        from_step = layout.nav_slots[note_slot].rel_tick // TICKS_PER_16TH_STEP
        to_step = min(from_step + 4, layout.sixteenth_steps - 1)
        if to_step == from_step and from_step > 0:
            to_step = from_step - 1

        overdub_baseline = len(serial_collector.snapshot())
        print("[note-edit-overdub-compose] in-edit overdub start")
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        timeout_s = ns.state_sync_timeout_ms / 1000.0
        if not _wait_for_overdubbing_active(
            serial_collector,
            baseline_len=overdub_baseline,
            timeout_s=timeout_s,
            abort=abort,
        ):
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            if not _wait_for_overdubbing_active(
                serial_collector,
                baseline_len=overdub_baseline,
                timeout_s=timeout_s,
                abort=abort,
            ):
                print("[note-edit-overdub-compose] FAILED: in-edit OVERDUBBING not confirmed")
                return 1
        ctx.markers.append("in-edit overdub")

        live_before = _stream_live_capture_bar(
            out_port,
            in_port,
            serial_collector,
            ns=ns,
            midi_channel=midi_channel,
            abort=abort,
        )
        print(f"[note-edit-overdub-compose] live Capture notes before move={live_before}")

        print(
            f"[note-edit-overdub-compose] move selected note "
            f"sixteenth {from_step}->{to_step}"
        )
        _fader2_move_to_sixteenth_step(
            out_port,
            layout=layout,
            fixture_step=to_step,
            serial_collector=serial_collector,
        )
        ctx.markers.append("mover drag during overdub")

        live_after = _stream_live_capture_bar(
            out_port,
            in_port,
            serial_collector,
            ns=ns,
            midi_channel=midi_channel,
            abort=abort,
        )
        print(f"[note-edit-overdub-compose] live Capture notes after move={live_after}")

        counts = _count_capture_transitions(serial_collector.snapshot())
        expected_stop = counts.get(("OVERDUBBING", "PLAYING"), 0) + 1
        print("[note-edit-overdub-compose] in-edit overdub stop")
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        _wait_for_transition_count(
            serial_collector,
            from_state="OVERDUBBING",
            to_state="PLAYING",
            target_count=expected_stop,
            timeout_s=timeout_s,
            abort=abort,
        )

        print("[note-edit-overdub-compose] exit NOTE_EDIT")
        _send_long_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=700,
        )
        ctx.markers.append("exited edit mode")
        time.sleep(ns.final_wait_ms / 1000.0)

        lines = prepend_hitl_header(serial_collector.snapshot(), spec)
        serial_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"[note-edit-overdub-compose] serial log: {serial_path}")

        check = _verify_note_edit_overdub_compose(lines, args)
        print(f"[note-edit-overdub-compose] verification ok={check.get('ok')}")
        for issue in check.get("issues") or []:
            print(f"  issue: {issue}")
        machine_cases = check.get("cases") or {}
        machine_total = (
            f"{sum(1 for case in machine_cases.values() if case.get('ok'))}"
            f"/{len(machine_cases)}"
        )
        print(
            format_human_checklist(
                spec,
                machine_ok=bool(check.get("ok")),
                machine_total=machine_total,
            )
        )

        report_path = out_dir / f"host_midi_hitl_note_edit_overdub_compose_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": "note_edit_overdub_compose",
                    "stimulus": stimulus_report_fields(spec),
                    "track_number": ns.track_number,
                    "midi_channel": midi_channel,
                    "seed_serial_log": str(seed_path) if seed_path else None,
                    "serial_log_path": str(serial_path),
                    "nav_slot_stats": stats,
                    "live_capture_pitch_band": {
                        "low": LIVE_CAPTURE_LOW,
                        "high": LIVE_CAPTURE_HIGH,
                    },
                    "in_edit_overdub_bars": IN_EDIT_OVERDUB_BARS,
                    "serial_verification": check,
                    "human_checks": list(spec.human_checks),
                    "markers": ctx.markers,
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        print(f"[note-edit-overdub-compose] report: {report_path}")
        return 0 if check.get("ok", False) else 2
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()


def verify_note_edit_overdub_compose(lines: list[str], args: object) -> dict[str, object]:
    return _verify_note_edit_overdub_compose(lines, args)
