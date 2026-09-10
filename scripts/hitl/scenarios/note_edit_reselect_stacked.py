"""NOTE_EDIT: move onto an occupied sixteenth, deselect, reselect stacked notes."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

from hitl.context import get_context
from hitl.stimuli import (
    format_human_checklist,
    prepend_hitl_header,
    print_stimulus_banner,
    run_stimulus,
    stimulus_for_scenario,
    stimulus_report_fields,
)
from hitl.verify.note_edit_reselect_stacked import (
    parse_select_apply,
    pitchbend_for_step_and_stack,
    select_apply_lands_on_step,
    verify_note_edit_reselect_stacked as _verify,
)

SOURCE_STEP = 8
DEST_STEP = 4
STACKED_COUNT = 2
# ControlSurfaceManager::FEEDBACK_IGNORE_PERIOD after NOTE_EDIT motor sync.
FADER1_FEEDBACK_IGNORE_S = 1.6


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
    parser.add_argument("--post-seed-settle-ms", type=int, default=3500)
    parser.add_argument("--edit-enter-timeout-s", type=float, default=6.0)
    parser.add_argument("--boot-settle-ms", type=int, default=0)
    parser.add_argument(
        "--seed-serial-log",
        type=Path,
        default=None,
        help="Override seed serial log (dev only; prefer the preset)",
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
            print(f"[note-edit-reselect-stacked] boot settle {boot_ms}ms")
            time.sleep(boot_ms / 1000.0)
        else:
            time.sleep(1.5)
        print(f"[note-edit-reselect-stacked] serial capture enabled: {ns.serial_port}")
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
        print(f"[note-edit-reselect-stacked] serial follow enabled: {follow_path}")
        return collector

    raise RuntimeError(
        "serial required: managed capture (--follow-current-session) or --serial-port"
    )


def _send_select_pitchbend(out_port, pitch: int) -> None:
    import mido
    from hitl.legacy_edit_baseline import FADER_SELECT_CHANNEL_1BASED, _send_program_change

    _send_program_change(out_port, FADER_SELECT_CHANNEL_1BASED, 1)
    out_port.send(
        mido.Message("pitchwheel", channel=FADER_SELECT_CHANNEL_1BASED - 1, pitch=pitch)
    )


def _first_empty_sixteenth(layout: object, *, avoid: set[int]) -> int:
    from hitl.control_constants import TICKS_PER_16TH_STEP

    for slot in getattr(layout, "nav_slots", []) or []:
        if getattr(slot, "note_idx", -1) >= 0:
            continue
        step = int(getattr(slot, "rel_tick", 0)) // TICKS_PER_16TH_STEP
        if step not in avoid:
            return step
    raise RuntimeError("no empty sixteenth in seed layout")


def _layout_from_seed_lines(lines: list[str], fallback: object, *, midi_channel: int) -> object:
    from hitl.baseline_loop_inventory import record_layout_for_base_seed

    rebuilt = record_layout_for_base_seed(
        lines,
        {
            "record_bars": 2,
            "edit_record_fixture": True,
            "midi_channel": midi_channel,
        },
    )
    if getattr(rebuilt, "step_to_tick", None):
        return rebuilt
    return fallback


def _wait_select_apply(
    collector,
    *,
    expected_tick: int,
    baseline: int,
    timeout_s: float = 2.0,
    want_note: bool = True,
) -> Optional[tuple[int, int]]:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for line in collector.snapshot()[baseline:]:
            parsed = parse_select_apply(line)
            if parsed is None:
                continue
            tick, note_idx, applied = parsed
            if select_apply_lands_on_step(
                tick, note_idx, applied, expected_tick, want_note=want_note
            ):
                return tick, note_idx
        time.sleep(0.03)
    return None


def _select_step_until_apply(
    out_port,
    collector,
    *,
    pitch: int,
    expected_tick: int,
    log_prefix: str,
    label: str,
    attempts: int = 3,
    want_note: bool = True,
) -> Optional[tuple[int, int]]:
    for attempt in range(1, attempts + 1):
        baseline = len(collector.snapshot())
        print(
            f"{log_prefix} F1 {label} tick={expected_tick} pb={pitch} "
            f"attempt {attempt}/{attempts}"
        )
        _send_select_pitchbend(out_port, pitch)
        found = _wait_select_apply(
            collector,
            expected_tick=expected_tick,
            baseline=baseline,
            timeout_s=2.0,
            want_note=want_note,
        )
        if found is not None:
            return found
        time.sleep(FADER1_FEEDBACK_IGNORE_S)
    return None


def run_note_edit_reselect_stacked(args: object) -> int:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        SELECT_DEPENDENT_SETTLE_MS,
        TICKS_PER_16TH_STEP,
        TRACK_SELECT_NOTE_BASE,
    )
    from hitl.edit_controls import _ensure_transport_running
    from hitl.midi_io import _drain_input_messages, _find_midi_port, _send_short_press
    from hitl.scenarios.edit_minimal import (
        _ensure_note_edit_entered,
        _ensure_note_edit_exited,
        _resolve_base_seed,
    )
    from hitl.legacy_edit_baseline import (
        _fader1_select_fixture_note,
        _fader2_move_to_sixteenth_step,
    )

    spec = run_stimulus(stimulus_for_scenario("note_edit_reselect_stacked"))
    print_stimulus_banner(spec)
    ns = _parse_scenario_args(args)
    ctx = get_context(args)
    out_dir = ns.out_dir
    log_prefix = "[note-edit-reselect-stacked]"

    try:
        _seed_lines, record_layout, seed_path = _resolve_base_seed(out_dir, ctx=ctx)
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"{log_prefix} {exc}")
        return 1
    print(f"{log_prefix} using base seed: {seed_path}")

    import mido

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    collector = None
    try:
        collector = _open_serial_collector(ns)
        if ns.post_seed_settle_ms > 0:
            print(f"{log_prefix} post-seed settle {ns.post_seed_settle_ms}ms")
            time.sleep(ns.post_seed_settle_ms / 1000.0)

        if not _ensure_transport_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        ):
            print(f"{log_prefix} WARN: MIDI clock missing after transport start")

        track_index = max(0, ns.track_number - 1)
        _send_short_press(
            out_port,
            note=TRACK_SELECT_NOTE_BASE + track_index,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(max(ns.phase_wait_ms, 1) / 1000.0)

        if not _ensure_note_edit_entered(
            out_port,
            collector,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            timeout_s=ns.edit_enter_timeout_s,
            log_prefix=log_prefix,
        ):
            print(f"{log_prefix} failed to enter NOTE_EDIT")
            return 1
        print(f"{log_prefix} wait {FADER1_FEEDBACK_IGNORE_S}s after NOTE_EDIT motor sync")
        time.sleep(FADER1_FEEDBACK_IGNORE_S)

        midi_channel = int(ns.midi_channel or ns.track_number)
        record_layout = _layout_from_seed_lines(
            collector.snapshot(), record_layout, midi_channel=midi_channel
        )
        nav_length = int(getattr(record_layout, "loop_length", 0))
        if nav_length <= 0:
            print(f"{log_prefix} missing loop_length on seed layout")
            return 1
        source_step = int(record_layout.sixteenth_step_for(SOURCE_STEP))
        dest_step = int(record_layout.sixteenth_step_for(DEST_STEP))
        empty_step = _first_empty_sixteenth(
            record_layout, avoid={source_step, dest_step}
        )
        source_tick = source_step * TICKS_PER_16TH_STEP
        dest_tick = dest_step * TICKS_PER_16TH_STEP
        empty_tick = empty_step * TICKS_PER_16TH_STEP
        print(
            f"{log_prefix} recorded sixteenths source={source_step}@{source_tick} "
            f"dest={dest_step}@{dest_tick} empty={empty_step}@{empty_tick}"
        )

        if not _fader1_select_fixture_note(
            out_port,
            layout=record_layout,
            fixture_step=SOURCE_STEP,
            serial_collector=collector,
        ):
            source_pb = pitchbend_for_step_and_stack(source_step, nav_length, 0, 1)
            if (
                _select_step_until_apply(
                    out_port,
                    collector,
                    pitch=source_pb,
                    expected_tick=source_tick,
                    log_prefix=log_prefix,
                    label=f"source sixteenth={source_step}",
                    want_note=True,
                )
                is None
            ):
                print(f"{log_prefix} source select_apply missing")
                return 1
        time.sleep(SELECT_DEPENDENT_SETTLE_MS / 1000.0)
        time.sleep(FADER1_FEEDBACK_IGNORE_S)

        print(f"{log_prefix} F2 move source sixteenth {source_step} -> dest {dest_step}")
        _fader2_move_to_sixteenth_step(
            out_port,
            layout=record_layout,
            fixture_step=DEST_STEP,
            serial_collector=collector,
        )
        time.sleep(FADER1_FEEDBACK_IGNORE_S)

        empty_pb = pitchbend_for_step_and_stack(empty_step, nav_length, 0, 1)
        if (
            _select_step_until_apply(
                out_port,
                collector,
                pitch=empty_pb,
                expected_tick=empty_tick,
                log_prefix=log_prefix,
                label=f"deselect sixteenth={empty_step}",
                want_note=False,
            )
            is None
        ):
            print(f"{log_prefix} empty-step select_apply missing")
            return 1
        time.sleep(FADER1_FEEDBACK_IGNORE_S)

        first_pb = pitchbend_for_step_and_stack(dest_step, nav_length, 0, STACKED_COUNT)
        first = _select_step_until_apply(
            out_port,
            collector,
            pitch=first_pb,
            expected_tick=dest_tick,
            log_prefix=log_prefix,
            label=f"stacked 0 dest sixteenth={dest_step}",
            want_note=True,
        )
        if first is None:
            print(f"{log_prefix} stacked-0 select_apply missing")
            return 1
        time.sleep(FADER1_FEEDBACK_IGNORE_S)

        late_pb = pitchbend_for_step_and_stack(dest_step, nav_length, 1, STACKED_COUNT)
        late = _select_step_until_apply(
            out_port,
            collector,
            pitch=late_pb,
            expected_tick=dest_tick,
            log_prefix=log_prefix,
            label=f"stacked 1 dest sixteenth={dest_step}",
            want_note=True,
        )
        if late is None:
            print(f"{log_prefix} stacked-1 select_apply missing")
            return 1
        print(f"{log_prefix} stacked apply note_idx first={first[1]} late={late[1]}")

        _ensure_note_edit_exited(
            out_port,
            collector,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            timeout_s=ns.edit_enter_timeout_s,
            log_prefix=log_prefix,
        )
        time.sleep(ns.final_wait_ms / 1000.0)

        lines = collector.snapshot()
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        serial_path = out_dir / f"note_edit_reselect_stacked_{stamp}_serial.log"
        headered = prepend_hitl_header(lines, spec)
        serial_path.write_text("\n".join(headered) + "\n", encoding="utf-8")
        print(f"{log_prefix} serial log: {serial_path}")

        check = _verify(headered, args, dest_tick=dest_tick, empty_tick=empty_tick)
        report_path = out_dir / f"host_midi_hitl_note_edit_reselect_stacked_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": "note_edit_reselect_stacked",
                    "stimulus": stimulus_report_fields(spec),
                    "seed_path": str(seed_path) if seed_path is not None else None,
                    "serial_log": str(serial_path),
                    "verification": check,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        print(f"{log_prefix} report: {report_path}")
        print(
            format_human_checklist(
                spec,
                machine_ok=bool(check.get("ok")),
                machine_total="stacked_reselect",
            )
        )
        if check.get("ok"):
            print(f"{log_prefix} Result: PASS")
            return 0
        print(f"{log_prefix} Result: FAIL — {check.get('issues', check)}")
        return 1
    finally:
        if collector is not None:
            collector.stop()
        out_port.close()
        in_port.close()
        _drain_input_messages(in_port)


def verify_note_edit_reselect_stacked(lines: list[str], args: object) -> dict[str, object]:
    from hitl.control_constants import TICKS_PER_16TH_STEP

    layout = _layout_from_seed_lines(lines, object(), midi_channel=5)
    dest_step = int(getattr(layout, "sixteenth_step_for", lambda _step: DEST_STEP)(DEST_STEP))
    source_step = int(getattr(layout, "sixteenth_step_for", lambda _step: SOURCE_STEP)(SOURCE_STEP))
    try:
        empty_step = _first_empty_sixteenth(layout, avoid={source_step, dest_step})
    except RuntimeError:
        empty_step = 2
    return _verify(
        lines,
        args,
        dest_tick=dest_step * TICKS_PER_16TH_STEP,
        empty_tick=empty_step * TICKS_PER_16TH_STEP,
    )
