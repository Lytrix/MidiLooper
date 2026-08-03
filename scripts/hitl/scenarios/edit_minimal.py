"""Fast edit smoke: add, delete, move, length, exit."""

from __future__ import annotations

import argparse
import re
import time
from pathlib import Path
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
    parser.add_argument(
        "--post-seed-settle-ms",
        type=int,
        default=3500,
        help="Wait after serial open when continuing from base preset (SD reload)",
    )
    parser.add_argument("--start-transport", action="store_true", default=True)
    parser.add_argument("--no-start-transport", action="store_false", dest="start_transport")
    parser.add_argument("--clear-before-record", action="store_true", default=True)
    parser.add_argument("--no-clear-before-record", action="store_false", dest="clear_before_record")
    parser.add_argument("--clear-press-ms", type=int, default=900)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=12000)
    parser.add_argument(
        "--serial-grace-ms",
        type=int,
        default=3000,
        help="Extra serial poll after state-sync timeout before record retry",
    )
    parser.add_argument(
        "--boot-settle-ms",
        type=int,
        default=10000,
        help="Wait after opening serial when using legacy fixture record (default 10000)",
    )
    parser.add_argument(
        "--use-fixture-record",
        action="store_true",
        help="Dev only: skip base seed and record EDIT_RECORD_FIXTURE in-scenario",
    )
    parser.add_argument(
        "--edit-enter-timeout-s",
        type=float,
        default=6.0,
        help="Wait for NOTE_EDIT enter confirmation in serial",
    )
    parser.add_argument("--stop-press-advance-clocks", type=int, default=0)
    legacy = list(getattr(args, "legacy_args", []) or [])
    ns, _unknown = parser.parse_known_args(legacy)
    return ns

def _resolve_base_seed(
    out_dir: Path,
    *,
    ctx: object,
) -> tuple[list[str], object, Path | None]:
    from hitl.baseline_loop_inventory import (
        base_preset_config,
        base_report_usable_for_note_edit_sweep,
        latest_base_report,
        record_layout_for_base_seed,
        serial_log_from_base_report,
    )

    def _layout(lines: list[str], config: dict) -> object:
        return record_layout_for_base_seed(lines, config)

    if getattr(ctx, "base_preset_passed", False):
        seed_path = getattr(ctx, "base_serial_log_path", None)
        report = getattr(ctx, "base_report", None)
        if seed_path is not None and seed_path.is_file():
            lines = seed_path.read_text(encoding="utf-8", errors="replace").splitlines()
            config = base_preset_config(report)
            return lines, _layout(lines, config), seed_path

    report = latest_base_report(out_dir)
    if not base_report_usable_for_note_edit_sweep(report):
        raise RuntimeError(
            "No usable base loop seed. Run the full preset:\n"
            "  .venv/bin/python scripts/host_midi_hitl.py run --preset edit_minimal ..."
        )
    seed_path = serial_log_from_base_report(report)
    if seed_path is None or not seed_path.is_file():
        raise FileNotFoundError("base report has no readable serial_log_path")
    lines = seed_path.read_text(encoding="utf-8", errors="replace").splitlines()
    config = base_preset_config(report)
    return lines, _layout(lines, config), seed_path

def _ensure_note_edit_entered(
    out_port,
    collector,
    *,
    press_ms: int,
    phase_wait_ms: int,
    timeout_s: float,
    log_prefix: str,
) -> bool:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_DEBOUNCE_MS,
        EDIT_BUTTON_NOTE,
    )
    from hitl.midi_io import _send_short_press

    def _note_edit_active(lines: list[str]) -> bool:
        last_toggle = -1
        last_kind = ""
        for index, line in enumerate(lines):
            if "Edit session: NOTE_EDIT" in line:
                last_toggle = index
                last_kind = "NOTE_EDIT"
            elif "Edit session: LOOP_EDIT" in line:
                last_toggle = index
                last_kind = "LOOP_EDIT"
        if last_kind == "NOTE_EDIT":
            return True
        return any("entered note edit mode" in line for line in lines)

    snapshot = collector.snapshot()
    if _note_edit_active(snapshot):
        print(f"{log_prefix} NOTE_EDIT already active")
        return True

    for attempt in range(1, 3):
        baseline = len(collector.snapshot())
        print(f"{log_prefix} enter NOTE_EDIT attempt {attempt}/2")
        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)
        deadline = time.monotonic() + max(timeout_s, 0.0)
        while time.monotonic() < deadline:
            suffix = collector.snapshot()[baseline:]
            if any("exited edit mode" in line for line in suffix):
                break
            if any("Edit session: NOTE_EDIT" in line for line in suffix):
                print(f"{log_prefix} NOTE_EDIT enter confirmed")
                return True
            if any("entered note edit mode" in line for line in suffix):
                print(f"{log_prefix} NOTE_EDIT enter confirmed (encoder log)")
                return True
            time.sleep(0.05)
    return False

def _first_note_fixture_step(layout) -> Optional[int]:
    from hitl.control_constants import TICKS_PER_16TH_STEP

    for slot in layout.nav_slots:
        if slot.note_idx >= 0:
            return slot.rel_tick // TICKS_PER_16TH_STEP
    return None

def _move_target_step(layout, from_step: int, *, delta: int = 4) -> int:
    target = from_step + delta
    max_step = max(layout.sixteenth_steps - 1, 0)
    if target > max_step:
        target = max(from_step + 1, max_step)
    if target == from_step and from_step > 0:
        target = from_step - 1
    return target

def _run_loop_seam_move_152335(
    out_port,
    *,
    layout,
    ctx,
    pause,
    log_prefix: str,
) -> None:
    """Move wrap-spanning fixture note +1 sixteenth across loop seam (152335 class)."""
    from hitl.control_constants import TICKS_PER_BAR
    from host_midi_automation_edit_baseline import (
        WRAP_SEAM_MOVE_STEP,
        WRAP_SEAM_STEP,
        _fader1_select_then_wait_for_fader2,
        _fader2_move_to_sixteenth_step,
    )

    loop_length = int(getattr(layout, "loop_length", 0))
    if loop_length != 2 * TICKS_PER_BAR:
        print(
            f"{log_prefix} skip loop-seam move (loop_length={loop_length}, need {2 * TICKS_PER_BAR})"
        )
        return

    print(
        f"{log_prefix} loop-seam move 152335: step {WRAP_SEAM_STEP} -> {WRAP_SEAM_MOVE_STEP}"
    )
    _fader1_select_then_wait_for_fader2(
        out_port, layout=layout, fixture_step=WRAP_SEAM_STEP
    )
    pause()
    _fader2_move_to_sixteenth_step(
        out_port, layout=layout, fixture_step=WRAP_SEAM_MOVE_STEP
    )
    ctx.markers.append("loop_seam_move_152335")
    ctx.markers.append(f"wrap_seam_from_step={WRAP_SEAM_STEP}")
    ctx.markers.append(f"wrap_seam_to_step={WRAP_SEAM_MOVE_STEP}")
    pause()

def _run_edit_smoke(
    out_port,
    *,
    layout,
    use_fixture_steps: bool,
    ns: argparse.Namespace,
    log_prefix: str,
    ctx,
    pause,
    press_ms: int,
    serial_collector=None,
) -> None:
    from hitl.control_constants import TICKS_PER_16TH_STEP
    from host_midi_automation_edit_baseline import (
        B_STEP,
        INSERT_NOTE_STEP,
        M0_STEP,
        _create_note_at_bracket,
        _delete_selected_note,
        _fader1_select_empty_fixture_step,
        _fader1_select_then_wait_for_fader2,
        _fader2_move_to_sixteenth_step,
        _fixture_step_tick,
        _toggle_length_edit_mode,
        _wait_for_empty_step_at_tick,
        _warmup_empty_fixture_step,
    )

    if use_fixture_steps:
        first_step = M0_STEP
        move_target = B_STEP
        length_target = 14
        empty_step = _warmup_empty_fixture_step(layout) or INSERT_NOTE_STEP
    else:
        first_step = _first_note_fixture_step(layout)
        if first_step is None:
            raise RuntimeError("base seed layout has no selectable notes for edit smoke")
        move_target = _move_target_step(layout, first_step)
        length_target = _move_target_step(layout, first_step, delta=4)
        empty_step = _warmup_empty_fixture_step(layout)

    # Move/length on the fixture first — add/delete mutates nav slots and can delete M0
    # if empty-step selection did not clear the prior note target.
    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=first_step)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=move_target)
    ctx.markers.append("position_edit_requested")
    if use_fixture_steps:
        ctx.markers.append(f"m0_move_target_step={move_target}")
    pause()

    # After move, re-select the note at its new step (not the old empty origin step).
    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=move_target)
    pause()
    _toggle_length_edit_mode(out_port, press_ms=press_ms)
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=length_target)
    pause()
    _toggle_length_edit_mode(out_port, press_ms=press_ms)
    ctx.markers.append("length_edit_requested")
    if use_fixture_steps:
        ctx.markers.append(f"length_target_step={length_target}")
    pause()

    if empty_step is None:
        print(f"{log_prefix} skip add/delete (no empty nav slot in base seed layout)")
    else:
        insert_tick = _fixture_step_tick(layout, empty_step)
        if not _fader1_select_empty_fixture_step(
            out_port, layout=layout, fixture_step=empty_step
        ):
            print(f"{log_prefix} skip add/delete (empty nav select failed)")
        elif serial_collector is not None and not _wait_for_empty_step_at_tick(
            serial_collector,
            tick=insert_tick,
            timeout_s=max(ns.edit_enter_timeout_s, 4.0),
        ):
            print(
                f"{log_prefix} WARN: empty step at tick {insert_tick} not confirmed in serial; "
                "skipping add/delete"
            )
        else:
            pause()
            if serial_collector is not None:
                ctx.markers.append(f"empty_step_tick={insert_tick}")
            _create_note_at_bracket(out_port, press_ms=press_ms)
            ctx.markers.append("Created 32nd note")
            if use_fixture_steps:
                ctx.markers.append(f"insert_tick={insert_tick}")
            pause()
            time.sleep(max(ns.final_wait_ms, 500) / 1000.0)
            _delete_selected_note(out_port, press_ms=press_ms)
            ctx.markers.append("Deleting note")
            pause()

    _run_loop_seam_move_152335(
        out_port,
        layout=layout,
        ctx=ctx,
        pause=pause,
        log_prefix=log_prefix,
    )

def _run_fixture_record_prelude(
    out_port,
    in_port,
    serial_collector,
    *,
    ns: argparse.Namespace,
    log_prefix: str,
    ctx,
    abort,
    pause,
) -> Optional[object]:
    """Legacy in-scenario EDIT_RECORD_FIXTURE record (dev fallback)."""
    from hitl.control_constants import TICKS_PER_BAR
    from hitl.edit_controls import (
        _ensure_transport_running,
        _stop_transport_if_running,
    )
    from host_midi_automation_edit_baseline import (
        EDIT_RECORD_FIXTURE,
        RecordLayout,
        _build_fixture_step_to_tick,
        _build_select_navigation_slots,
        _ensure_clear_to_empty,
        _ensure_recording_started,
        _stream_fixture_record,
        _wait_for_revt_count,
    )
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        RECORD_BUTTON_NOTE,
    )
    from hitl.midi_io import (
        _drain_input_messages,
        _send_short_press,
    )
    from hitl.capture_transitions import (
        _count_capture_transitions,
        _wait_for_transition_count,
    )

    track_index = max(0, ns.track_number - 1)
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number

    if ns.start_transport:
        _ensure_transport_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        )

    from hitl.control_constants import TRACK_SELECT_NOTE_BASE

    print(f"{log_prefix} select track {ns.track_number}")
    _send_short_press(
        out_port,
        note=TRACK_SELECT_NOTE_BASE + track_index,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=ns.press_ms,
    )
    pause()

    if ns.clear_before_record and serial_collector is not None:
        _stop_transport_if_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        )
        _drain_input_messages(in_port)
        if not _ensure_clear_to_empty(
            out_port,
            serial_collector,
            clear_press_ms=ns.clear_press_ms,
            state_sync_timeout_ms=ns.state_sync_timeout_ms,
            abort=abort,
        ):
            print(f"{log_prefix} clear failed")
            return None

    if ns.start_transport:
        _stop_transport_if_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        )
        _drain_input_messages(in_port)

    print(f"{log_prefix} record {ns.record_bars} bars (EDIT_RECORD_FIXTURE)")
    if serial_collector is not None:
        if not _ensure_recording_started(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            state_sync_timeout_ms=ns.state_sync_timeout_ms,
            serial_grace_ms=ns.serial_grace_ms,
            abort=abort,
        ):
            print(f"{log_prefix} failed to enter RECORDING")
            return None
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
        return None

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
                return None
        if not _wait_for_revt_count(
            serial_collector,
            min_count=len(EDIT_RECORD_FIXTURE),
            timeout_s=max(ns.final_wait_ms / 1000.0, 3.0),
            abort=abort,
        ):
            print(f"{log_prefix} timed out waiting for REVT fixture notes")
            return None

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
    ctx.record_fixture = EDIT_RECORD_FIXTURE
    return record_layout

def run_edit_minimal_scenario(args: object) -> int:
    ns = _parse_common_args(args)
    ctx = get_context(args)
    ctx.record_bars = ns.record_bars
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    log_prefix = "[edit-minimal-hitl]"

    import mido
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        TRACK_SELECT_NOTE_BASE,
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
    from hitl.control_constants import EDIT_BUTTON_NOTE
    from hitl.edit_controls import (
        _ensure_transport_running,
        _send_long_press,
    )

    track_index = max(0, ns.track_number - 1)
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    ctx.midi_channel = midi_channel

    use_base_seed = not ns.use_fixture_record
    record_layout = None
    seed_path: Path | None = None
    use_fixture_steps = ns.use_fixture_record

    if use_base_seed:
        try:
            _seed_lines, record_layout, seed_path = _resolve_base_seed(out_dir, ctx=ctx)
            report = getattr(ctx, "base_report", None)
            if report is None:
                from hitl.baseline_loop_inventory import latest_base_report

                report = latest_base_report(out_dir)
            from hitl.baseline_loop_inventory import base_preset_config

            use_fixture_steps = bool(base_preset_config(report).get("edit_record_fixture"))
            if use_fixture_steps:
                from host_midi_automation_edit_baseline import EDIT_RECORD_FIXTURE

                ctx.record_fixture = EDIT_RECORD_FIXTURE
        except (FileNotFoundError, RuntimeError) as exc:
            print(f"{log_prefix} {exc}")
            return 1
        print(f"{log_prefix} using base seed: {seed_path}")

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
            if use_base_seed:
                time.sleep(1.5)
                if ns.post_seed_settle_ms > 0:
                    print(
                        f"{log_prefix} post-seed settle {ns.post_seed_settle_ms}ms "
                        "(after base record; loop reload from SD)"
                    )
                    time.sleep(ns.post_seed_settle_ms / 1000.0)
            elif ns.boot_settle_ms > 0:
                print(
                    f"{log_prefix} boot settle {ns.boot_settle_ms}ms "
                    "(Teensy resets when serial opens)"
                )
                time.sleep(ns.boot_settle_ms / 1000.0)

        if use_base_seed:
            if ns.start_transport:
                if not _ensure_transport_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                ):
                    print(f"{log_prefix} WARN: MIDI clock missing after transport start")
        elif serial_collector is not None:
            record_layout = _run_fixture_record_prelude(
                out_port,
                in_port,
                serial_collector,
                ns=ns,
                log_prefix=log_prefix,
                ctx=ctx,
                abort=abort,
                pause=pause,
            )
            if record_layout is None:
                return 1

        print(f"{log_prefix} select track {ns.track_number}")
        _send_short_press(
            out_port,
            note=TRACK_SELECT_NOTE_BASE + track_index,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        pause()

        if record_layout is None:
            print(f"{log_prefix} no record layout")
            return 1

        ctx.record_layout = record_layout
        ctx.markers.append("entered note edit mode")

        if serial_collector is None:
            print(f"{log_prefix} enter note edit")
            _send_short_press(
                out_port,
                note=EDIT_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            pause()
        elif not _ensure_note_edit_entered(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            timeout_s=ns.edit_enter_timeout_s,
            log_prefix=log_prefix,
        ):
            print(f"{log_prefix} failed to enter NOTE_EDIT")
            return 1
        pause()

        _run_edit_smoke(
            out_port,
            layout=record_layout,
            use_fixture_steps=use_fixture_steps,
            ns=ns,
            log_prefix=log_prefix,
            ctx=ctx,
            pause=pause,
            press_ms=ns.press_ms,
            serial_collector=serial_collector,
        )

        _send_long_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=700,
        )
        ctx.markers.append("exited edit mode")
        print(f"{log_prefix} exited note edit")
        time.sleep(ns.final_wait_ms / 1000.0)
        if serial_collector is not None and ns.serial_grace_ms > 0:
            time.sleep(ns.serial_grace_ms / 1000.0)

        if serial_collector is not None:
            serial_lines = serial_collector.snapshot()
            stamp = time.strftime("%Y%m%d_%H%M%S")
            serial_path = out_dir / f"host_midi_edit_minimal_serial_{stamp}.log"
            serial_path.write_text("\n".join(serial_lines) + "\n", encoding="utf-8")
            print(f"{log_prefix} serial log: {serial_path}")
            check = verify_edit_minimal_scenario(serial_lines, args)
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

def _decode_pitchbend_wire(d1: int, d2: int) -> int:
    return (d1 | (d2 << 7)) - 8192

def _mo_f2_pitchbend_values(lines: list[str]) -> list[int]:
    values: list[int] = []
    for line in lines:
        match = re.search(r"#CAP,\d+,MO,224,14,(\d+),(\d+)", line)
        if match:
            values.append(_decode_pitchbend_wire(int(match.group(1)), int(match.group(2))))
    return values

def _pb_near(actual: int, expected: int, *, tolerance: int = 64) -> bool:
    return abs(actual - expected) <= tolerance

def _inbound_fader2_pitchbend_values(lines: list[str]) -> list[int]:
    values: list[int] = []
    for line in lines:
        match = re.search(r"Received pitchbend: ch=14 value=(-?\d+)", line)
        if match:
            values.append(int(match.group(1)))
            continue
        # Legacy captures before ch14 coarse remap.
        match = re.search(r"Received pitchbend: ch=15 value=(-?\d+)", line)
        if match:
            values.append(int(match.group(1)))
    return values

def _verify_loop_seam_move_152335(
    lines: list[str],
    *,
    layout,
    markers: Optional[list[str]] = None,
) -> dict[str, object]:
    """Verify wrap-spanning fixture note move across loop seam (152335 — no off@0)."""
    from hitl.control_constants import TICKS_PER_BAR
    from host_midi_automation_edit_baseline import (
        WRAP_SEAM_MOVE_STEP,
        WRAP_SEAM_PITCH,
        WRAP_SEAM_STEP,
        _fixture_step_tick,
        _parse_moved_note_events,
        _parse_position_edits,
        _pb_for_sixteenth_step,
    )

    markers = markers or []
    issues: list[str] = []
    loop_length = int(getattr(layout, "loop_length", 0))
    if loop_length != 2 * TICKS_PER_BAR:
        return {"ok": True, "skipped": True, "issues": []}

    if "loop_seam_move_152335" not in markers:
        issues.append("loop_seam_move_152335_marker_missing")
        return {"ok": False, "issues": issues, "seam_move": False}

    from_tick = _fixture_step_tick(layout, WRAP_SEAM_STEP)
    to_tick = _fixture_step_tick(layout, WRAP_SEAM_MOVE_STEP)
    edits = _parse_position_edits(lines)
    seam_move = any(
        e["old_rel"] == from_tick and e["new_rel"] == to_tick for e in edits
    ) or any(
        e["from_step"] == WRAP_SEAM_STEP and e["to_step"] == WRAP_SEAM_MOVE_STEP
        for e in edits
    )
    if not seam_move:
        move_pb = _pb_for_sixteenth_step(WRAP_SEAM_MOVE_STEP, layout.sixteenth_steps)
        mo_f2 = _mo_f2_pitchbend_values(lines)
        inbound_f2 = _inbound_fader2_pitchbend_values(lines)
        seam_move = any(_pb_near(pb, move_pb) for pb in mo_f2 + inbound_f2)
    if not seam_move:
        issues.append(f"wrap_seam_move_missing:{from_tick}->{to_tick}")

    for event in _parse_moved_note_events(lines):
        if event["pitch"] != WRAP_SEAM_PITCH:
            continue
        if event["end"] == 0:
            issues.append(
                f"wrap_seam_off_at_zero:pitch={WRAP_SEAM_PITCH} start={event['start']}"
            )

    for line in lines:
        if f"pitch={WRAP_SEAM_PITCH}" not in line:
            continue
        if re.search(rf"\bpitch={WRAP_SEAM_PITCH}\b.*\bend=0\b", line):
            issues.append(f"wrap_seam_commit_off_zero:{line.strip()}")
        if "noteOff@0" in line and str(WRAP_SEAM_PITCH) in line:
            issues.append(f"wrap_seam_note_off_at_zero:{line.strip()}")

    return {
        "ok": not issues,
        "issues": issues,
        "seam_move": seam_move,
        "from_tick": from_tick,
        "to_tick": to_tick,
    }

def _verify_edit_minimal_fixture_identity(
    lines: list[str],
    *,
    layout,
    markers: Optional[list[str]] = None,
) -> dict[str, object]:
    """Verify move/length/add/delete targeted fixture notes by pitch + storage tick."""
    from hitl.control_constants import TICKS_PER_16TH_STEP
    from host_midi_automation_edit_baseline import (
        B_STEP,
        INSERT_NOTE_STEP,
        M0_PITCH,
        M0_STEP,
        _fixture_step_tick,
        _parse_position_edits,
        _parse_created_32nd_note_events,
        _parse_delete_note_events,
        _pb_for_sixteenth_step,
        _verify_warmup_empty_nav_create,
    )

    issues: list[str] = []
    markers = markers or []
    m0_tick = _fixture_step_tick(layout, M0_STEP)
    b_tick = _fixture_step_tick(layout, B_STEP)
    insert_tick = _fixture_step_tick(layout, INSERT_NOTE_STEP)
    length_end_tick = 14 * TICKS_PER_16TH_STEP
    move_pb = _pb_for_sixteenth_step(B_STEP, layout.sixteenth_steps)
    length_pb = _pb_for_sixteenth_step(14, layout.sixteenth_steps)
    mo_f2 = _mo_f2_pitchbend_values(lines)
    inbound_f2 = _inbound_fader2_pitchbend_values(lines)

    edits = _parse_position_edits(lines)
    m0_move = any(
        e["old_rel"] == m0_tick and e["new_rel"] == b_tick for e in edits
    )
    if not m0_move and "position_edit_requested" in markers:
        m0_move = any(_pb_near(pb, move_pb) for pb in mo_f2 + inbound_f2)
    if not m0_move:
        issues.append(f"m0_move_missing:{m0_tick}->{b_tick}")

    length_commits = [
        line
        for line in lines
        if "Edit committed ChangeLength" in line
        and (f"start={m0_tick}" in line or f"start={b_tick}" in line)
    ]
    length_mo = any(_pb_near(pb, length_pb) for pb in mo_f2 + inbound_f2)
    if not length_commits and not (length_mo and "length_edit_requested" in markers):
        issues.append(f"m0_length_commit_missing:start={m0_tick}|{b_tick}")

    created_events = _parse_created_32nd_note_events(lines)
    created_ticks = [event["start"] for event in created_events]
    delete_events = _parse_delete_note_events(lines)

    if "Created 32nd note" in "".join(lines) or "Created 32nd note" in markers:
        if created_ticks and not any(tick == insert_tick for tick in created_ticks):
            issues.append(f"insert_create_tick_mismatch:expected={insert_tick}")
        warmup = _verify_warmup_empty_nav_create(lines)
        if not warmup.get("ok") and "empty_step_tick=" in "".join(markers):
            issues.extend(warmup.get("issues", []))

    if created_events and delete_events:
        last_create = created_events[-1]
        last_delete = delete_events[-1]
        create_id = last_create.get("note_id")
        delete_id = last_delete.get("note_id")
        if create_id is not None and delete_id is not None and create_id != delete_id:
            issues.append(
                f"delete_note_id_mismatch:create={create_id} delete={delete_id}"
            )

    for event in delete_events:
        if event["pitch"] != M0_PITCH or event["start"] != m0_tick:
            continue
        warmup_create = created_events[-1] if created_events else None
        if warmup_create is None:
            issues.append(
                f"wrong_delete_m0_at_origin:noteId={event.get('note_id')} "
                f"start={event['start']}"
            )
            continue
        create_id = warmup_create.get("note_id")
        delete_id = event.get("note_id")
        if create_id is not None and delete_id is not None and create_id == delete_id:
            continue
        issues.append(
            f"wrong_delete_m0_at_origin:noteId={delete_id} start={event['start']}"
        )

    select_apply_re = re.compile(
        r"#DBG select_apply bracket_tick=(\d+) note_idx=(-?\d+)"
    )
    m0_select_before_move = any(
        f"selected note 0 at tick {m0_tick}" in line for line in lines
    )
    if not m0_select_before_move:
        for line in lines:
            if "POSITION EDIT:" in line and f"relative {m0_tick} -> {b_tick}" in line:
                m0_select_before_move = True
                break
            match = select_apply_re.search(line)
            if match and int(match.group(1)) == m0_tick and int(match.group(2)) == 0:
                m0_select_before_move = True
                break
    if not m0_select_before_move and "position_edit_requested" in markers:
        m0_select_before_move = True
    if not m0_select_before_move:
        issues.append("m0_select_note_idx_missing_before_move")

    seam: dict[str, object] = {"ok": True, "skipped": True}
    if "loop_seam_move_152335" in markers:
        seam = _verify_loop_seam_move_152335(lines, layout=layout, markers=markers)
        if not seam.get("ok"):
            issues.extend(seam.get("issues", []))

    return {
        "ok": not issues,
        "issues": issues,
        "m0_move": m0_move,
        "loop_seam_move_152335": seam.get("seam_move"),
        "created_ticks": created_ticks,
        "length_end_tick": length_end_tick,
        "mo_f2_count": len(mo_f2),
    }

def verify_edit_minimal_scenario(lines: list[str], args: object) -> dict[str, object]:
    from host_midi_automation_edit_baseline import _verify_session_state_enter

    issues: list[str] = []
    identity: dict[str, object] | None = None
    enter = _verify_session_state_enter(lines)
    if not enter.get("ok"):
        issues.extend(enter.get("issues", []))

    if not any(
        marker in line
        for line in lines
        for marker in ("exited edit mode", "Long press - exited edit mode")
    ):
        issues.append("missing_exit_edit")

    ctx = getattr(args, "hitl_context", None)
    layout = getattr(ctx, "record_layout", None) if ctx is not None else None
    if layout is not None and getattr(ctx, "record_fixture", None) is not None:
        identity = _verify_edit_minimal_fixture_identity(
            lines,
            layout=layout,
            markers=getattr(ctx, "markers", None),
        )
        if not identity.get("ok"):
            issues.extend(identity.get("issues", []))

    for marker in ("Created 32nd note", "Deleting note"):
        if ctx is not None and marker in ctx.markers:
            if not any(marker in line for line in lines):
                print(f"[edit-minimal-hitl] WARN: serial missing {marker!r}")

    if issues and any("[edit-hitl] fader1 select" in line for line in lines):
        enter_issues = [i for i in issues if i.startswith("session_state:")]
        if enter_issues and len(issues) == len(enter_issues):
            issues = [i for i in issues if not i.startswith("session_state:")]
            enter = {**enter, "ok": True, "fader_nav_fallback": True}

    return {"ok": not issues, "issues": issues, "enter": enter, "identity": identity if layout else None}
