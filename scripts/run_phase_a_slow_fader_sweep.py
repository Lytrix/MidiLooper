#!/usr/bin/env python3
"""Phase A HITL: base preset (2+2 + second overdub), then slow fader-1 nav sweep."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import mido

_SCRIPT_DIR = Path(__file__).resolve().parent
_ROOT = _SCRIPT_DIR.parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))


def _resolve_base_seed(
    args: argparse.Namespace,
) -> tuple[list[str], dict, Path | None]:
    from hitl.baseline_loop_inventory import (
        base_preset_config,
        latest_base_report,
        serial_log_from_base_report,
    )

    if args.seed_serial_log is not None:
        if not args.seed_serial_log.is_file():
            raise FileNotFoundError(args.seed_serial_log)
        report = latest_base_report(args.out_dir)
        return (
            args.seed_serial_log.read_text(encoding="utf-8", errors="replace").splitlines(),
            base_preset_config(report),
            args.seed_serial_log,
        )

    report = latest_base_report(args.out_dir)
    if report is None:
        raise FileNotFoundError("no host_midi_automation_baseline_*.json in captures")
    serial_path = serial_log_from_base_report(report)
    if serial_path is None:
        raise FileNotFoundError("base report has no serial_log_path")
    lines = serial_path.read_text(encoding="utf-8", errors="replace").splitlines()
    return lines, base_preset_config(report), serial_path


def _run_base_seed(args: argparse.Namespace) -> tuple[list[str], dict]:
    captures_dir = args.out_dir
    captures_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(args.venv_python),
        str(_SCRIPT_DIR / "host_midi_hitl.py"),
        "run",
        "--preset",
        "base",
        "--midi-out",
        args.midi_out,
        "--midi-in",
        args.midi_in,
        "--serial-port",
        args.serial_port,
        "--track-number",
        str(args.track),
        "--midi-channel",
        str(args.midi_channel),
        "--out-dir",
        str(captures_dir),
    ]
    cmd.extend(args.base_legacy_args or [])
    print("[phase-a-sweep] base preset (record + 2 overdub passes):", " ".join(cmd))
    result = subprocess.run(cmd, cwd=str(_ROOT))
    if result.returncode != 0:
        print(
            f"[phase-a-sweep] base preset exit={result.returncode} "
            "(continuing if baseline report exists)"
        )
    seed_lines, config, serial_path = _resolve_base_seed(args)
    print(f"[phase-a-sweep] seed serial log: {serial_path}")
    print(
        f"[phase-a-sweep] base config: record_bars={config.get('record_bars')} "
        f"overdub_bars={config.get('overdub_bars')} "
        f"second_overdub_bars={config.get('second_overdub_bars')}"
    )
    print("[phase-a-sweep] base seed done — entering NOTE_EDIT for fader sweep")
    return seed_lines, config


def _last_note_edit_toggle_index(lines: list[str]) -> tuple[int, str] | None:
    last_index = -1
    last_kind = ""
    for index, line in enumerate(lines):
        if "Edit session: NOTE_EDIT" in line:
            last_index = index
            last_kind = "NOTE_EDIT"
        elif "Edit session: LOOP_EDIT" in line:
            last_index = index
            last_kind = "LOOP_EDIT"
    if last_index < 0:
        return None
    return last_index, last_kind


def _note_edit_enter_seen_in_lines(lines: list[str]) -> bool:
    toggle = _last_note_edit_toggle_index(lines)
    if toggle is None:
        return False
    _index, kind = toggle
    if kind != "NOTE_EDIT":
        return False
    tail = lines[_index : _index + 40]
    return not any("exited edit mode" in line for line in tail)


def _already_in_note_edit(lines: list[str]) -> bool:
    toggle = _last_note_edit_toggle_index(lines)
    return toggle is not None and toggle[1] == "NOTE_EDIT"


def _wait_for_note_edit_enter(
    collector,
    *,
    baseline_line_count: int,
    timeout_s: float,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        suffix = collector.snapshot()[baseline_line_count:]
        if _note_edit_enter_seen_in_lines(suffix):
            return True
        time.sleep(0.05)
    return False


def _note_edit_recently_exited(lines: list[str]) -> bool:
    from host_midi_automation_edit_baseline import _find_edit_enter_index

    enter_idx = _find_edit_enter_index(lines)
    exit_idx = None
    for i, line in enumerate(lines):
        if "exited edit mode" in line or "Long press - exited edit mode" in line:
            exit_idx = i
    if exit_idx is None:
        return False
    if enter_idx is None:
        return True
    return exit_idx > enter_idx


def _ensure_note_edit_entered(
    out_port: mido.ports.BaseOutput,
    collector,
    *,
    press_ms: int,
    phase_wait_ms: int,
    enter_timeout_s: float,
) -> bool:
    from host_midi_automation_baseline import CONTROL_CHANNEL_1BASED, _send_short_press
    from host_midi_automation_edit_baseline import EDIT_BUTTON_DEBOUNCE_MS, EDIT_BUTTON_NOTE

    snapshot = collector.snapshot()
    if _already_in_note_edit(snapshot):
        print("[phase-a-sweep] NOTE_EDIT already active (serial); skipping enter press")
        return True

    for attempt in range(1, 3):
        baseline = len(collector.snapshot())
        if _already_in_note_edit(collector.snapshot()[baseline:]):
            print("[phase-a-sweep] NOTE_EDIT already active (serial)")
            return True
        print(f"[phase-a-sweep] enter NOTE_EDIT (short press, attempt {attempt}/2)")
        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)
        if _wait_for_note_edit_enter(
            collector, baseline_line_count=baseline, timeout_s=enter_timeout_s
        ):
            tail = collector.snapshot()[baseline:]
            if any("exited edit mode" in line for line in tail):
                print("[phase-a-sweep] edit exited immediately after enter; retrying")
                continue
            print("[phase-a-sweep] NOTE_EDIT enter confirmed in serial")
            return True
        tail = collector.snapshot()[baseline:]
        if any("Edit session: LOOP_EDIT" in line for line in tail):
            print("[phase-a-sweep] edit press cycled to LOOP_EDIT; toggling back")
            continue
        print("[phase-a-sweep] NOTE_EDIT enter not seen in serial")
    return False


def _slow_fader1_sweep(
    out_port: mido.ports.BaseOutput,
    *,
    layout,
    slot_dwell_ms: int,
) -> None:
    from host_midi_automation_edit_baseline import (
        FADER_SELECT_SETTLE_MS,
        NOTE_SELECTION_GRACE_MS,
        _fader1_select_nav_slot_index,
    )
    from hitl.fader_motor_probe import arm_note_edit_session

    arm_note_edit_session(out_port, step_label="phase_a_sweep")
    time.sleep(0.25)

    count = layout.nav_slot_count
    per_slot_settle_ms = FADER_SELECT_SETTLE_MS + max(
        NOTE_SELECTION_GRACE_MS - FADER_SELECT_SETTLE_MS, 0
    )
    extra_dwell_ms = max(slot_dwell_ms - per_slot_settle_ms, 0)
    print(
        f"[phase-a-sweep] slow fader1 sweep: {count} nav slots, "
        f"dwell={slot_dwell_ms}ms (settle={per_slot_settle_ms}ms + extra={extra_dwell_ms}ms)"
    )
    for slot_index in range(count):
        _fader1_select_nav_slot_index(out_port, layout=layout, slot_index=slot_index)
        if extra_dwell_ms > 0:
            time.sleep(extra_dwell_ms / 1000.0)


def _run_sweep_phase(
    args: argparse.Namespace,
    seed_lines: list[str],
    base_config: dict,
) -> int:
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        SerialCaptureCollector,
        _find_midi_port,
        _send_short_press,
    )
    from host_midi_automation_edit_baseline import _ensure_transport_running
    from hitl.baseline_loop_inventory import (
        materialized_note_pairs_from_base_seed,
        nav_slot_stats,
        record_layout_from_base_seed,
    )

    layout = record_layout_from_base_seed(seed_lines, base_config)
    if layout.nav_slot_count <= 0:
        print("[phase-a-sweep] no nav slots from base seed inventory")
        return 1

    loop_length, note_pairs = materialized_note_pairs_from_base_seed(seed_lines, base_config)
    stats = nav_slot_stats(note_pairs, loop_length=loop_length)
    print(
        f"[phase-a-sweep] loop_length={loop_length} notes={stats['note_pairs']} "
        f"nav_slots={stats['nav_slots']} "
        f"multi_note_steps={stats['sixteenth_steps_with_multiple_notes']} "
        f"max_notes_per_step={stats['max_notes_per_sixteenth_step']}"
    )

    out_port = mido.open_output(_find_midi_port(args.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(args.midi_in, "input"))
    serial_collector: SerialCaptureCollector | None = None
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    serial_path = args.out_dir / f"phase_a_slow_fader_sweep_{stamp}_serial.log"

    try:
        serial_collector = SerialCaptureCollector(args.serial_port, baud=args.serial_baud)
        serial_collector.start()
        time.sleep(1.5)

        if args.post_seed_settle_ms > 0:
            print(f"[phase-a-sweep] post-seed settle {args.post_seed_settle_ms}ms")
            time.sleep(args.post_seed_settle_ms / 1000.0)

        if not _ensure_transport_running(
            out_port,
            in_port,
            press_ms=args.press_ms,
            phase_wait_ms=args.phase_wait_ms,
        ):
            print("[phase-a-sweep] WARN: MIDI clock missing after transport start")

        track_index = args.track - 1
        _send_short_press(
            out_port,
            note=60 + track_index,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=args.press_ms,
        )
        time.sleep(args.phase_wait_ms / 1000.0)

        if not _ensure_note_edit_entered(
            out_port,
            serial_collector,
            press_ms=args.press_ms,
            phase_wait_ms=args.phase_wait_ms,
            enter_timeout_s=args.edit_enter_timeout_s,
        ):
            fail_lines = serial_collector.snapshot()
            serial_path.write_text("\n".join(fail_lines) + "\n", encoding="utf-8")
            print("[phase-a-sweep] FAILED: could not confirm NOTE_EDIT enter")
            print(f"[phase-a-sweep] debug serial log: {serial_path}")
            return 1

        _slow_fader1_sweep(
            out_port,
            layout=layout,
            slot_dwell_ms=args.slot_dwell_ms,
        )
        time.sleep(args.final_wait_ms / 1000.0)

        lines = serial_collector.snapshot()
        if _note_edit_recently_exited(lines):
            print("[phase-a-sweep] WARN: serial shows edit exit during sweep capture")
        serial_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"[phase-a-sweep] serial log: {serial_path}")

        analyze = subprocess.run(
            [
                str(args.venv_python),
                str(_SCRIPT_DIR / "analyze_fader2_select_feedback.py"),
                str(serial_path),
                "--limit",
                str(max(layout.nav_slot_count + 8, 64)),
            ],
            cwd=str(_ROOT),
        )
        return 0 if analyze.returncode == 0 else analyze.returncode
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()


def run_sweep(args: argparse.Namespace) -> int:
    seed_lines, config = _run_base_seed(args)
    return _run_sweep_phase(args, seed_lines, config)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=(
            "Seeds via host_midi_hitl --preset base (canonical 2+2 record/overdub + "
            "second overdub pass by default). Pass extra baseline flags after '--', e.g. "
            "-- --second-overdub-bars 0 to disable the second overdub."
        ),
    )
    parser.add_argument("--venv-python", type=Path, default=_ROOT / ".venv" / "bin" / "python")
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default="/dev/cu.usbmodem154944801")
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=5)
    parser.add_argument("--slot-dwell-ms", type=int, default=2000, help="Pause per nav slot")
    parser.add_argument("--edit-enter-timeout-s", type=float, default=4.0)
    parser.add_argument("--post-seed-settle-ms", type=int, default=3500)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    parser.add_argument("--out-dir", type=Path, default=_ROOT / "captures")
    parser.add_argument(
        "--seed-serial-log",
        type=Path,
        default=None,
        help="Reuse this base serial log (with latest baseline JSON for config)",
    )
    parser.add_argument(
        "--skip-base-seed",
        action="store_true",
        help="Skip base preset run; use latest baseline report serial log",
    )
    parser.add_argument(
        "--",
        dest="base_legacy_args",
        nargs=argparse.REMAINDER,
        help="Forwarded to host_midi_hitl base preset (e.g. --second-overdub-bars 0)",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.skip_base_seed:
            seed_lines, config, serial_path = _resolve_base_seed(args)
            print(f"[phase-a-sweep] reuse seed serial log: {serial_path}")
            return _run_sweep_phase(args, seed_lines, config)
        return run_sweep(args)
    except FileNotFoundError as exc:
        print(f"[phase-a-sweep] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
