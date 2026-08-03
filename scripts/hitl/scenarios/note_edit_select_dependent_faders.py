"""NOTE_EDIT slow F1 nav sweep + two-note toggle for F2/F3/F4 motor sync HITL."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context

def _parse_scenario_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    parser.add_argument("--post-seed-settle-ms", type=int, default=3500)
    parser.add_argument("--edit-enter-timeout-s", type=float, default=4.0)
    parser.add_argument("--dwell-ms", type=int, default=800, help="Pause per nav slot after settle")
    parser.add_argument("--toggle-dwell-ms", type=int, default=800, help="Pause per two-note toggle step")
    parser.add_argument("--toggle-cycles", type=int, default=6, help="A↔B toggle repetitions (0=skip)")
    parser.add_argument(
        "--seed-serial-log",
        type=Path,
        default=None,
        help="Override base seed serial log (dev only; prefer --preset for fresh 2+2 base)",
    )
    parser.add_argument(
        "--skip-sweep",
        action="store_true",
        help="Run toggle only (requires prior NOTE_EDIT state in capture)",
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    ns, _unknown = parser.parse_known_args(legacy)
    return ns

def _resolve_base_seed(
    out_dir: Path,
    seed_serial_log: Optional[Path],
    *,
    ctx: object | None = None,
) -> tuple[list[str], dict, Path | None]:
    from hitl.baseline_loop_inventory import (
        base_preset_config,
        latest_base_report,
        serial_log_from_base_report,
    )

    base_serial_path: Path | None = None
    report: dict | None = None

    if ctx is not None and getattr(ctx, "base_preset_passed", False):
        base_serial_path = getattr(ctx, "base_serial_log_path", None)
        report = getattr(ctx, "base_report", None)
        if base_serial_path is not None and base_serial_path.is_file():
            return (
                base_serial_path.read_text(encoding="utf-8", errors="replace").splitlines(),
                base_preset_config(report),
                base_serial_path,
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
    if report is None:
        raise FileNotFoundError(
            f"no host_midi_automation_baseline_*.json in {out_dir}; "
            "run --preset note_edit_select_dependent_faders first"
        )
    serial_path = serial_log_from_base_report(report)
    if serial_path is None:
        raise FileNotFoundError("base report has no serial_log_path")
    lines = serial_path.read_text(encoding="utf-8", errors="replace").splitlines()
    return lines, base_preset_config(report), serial_path

def _require_ok_base_seed(
    out_dir: Path,
    seed_serial_log: Optional[Path],
    *,
    ctx: object | None = None,
) -> tuple[list[str], dict, Path | None]:
    """Load nav inventory from base preset; require overall_ok unless explicit seed."""
    from hitl.baseline_loop_inventory import (
        base_report_loop_materialized,
        base_report_ok,
        base_report_usable_for_note_edit_sweep,
        latest_base_report,
    )

    if ctx is not None and getattr(ctx, "base_preset_passed", False):
        seed_lines, base_config, seed_path = _resolve_base_seed(
            out_dir, seed_serial_log, ctx=ctx
        )
        return seed_lines, base_config, seed_path

    if seed_serial_log is not None:
        print(
            "[select-dependent-faders] WARN: --seed-serial-log without base in this run; "
            "device loop must match seed. Prefer --preset note_edit_select_dependent_faders."
        )
        seed_lines, base_config, seed_path = _resolve_base_seed(
            out_dir, seed_serial_log, ctx=ctx
        )
        return seed_lines, base_config, seed_path

    report = latest_base_report(out_dir)
    if not base_report_usable_for_note_edit_sweep(report):
        issues = []
        if report is not None:
            assertions = report.get("assertions") or {}
            verification = assertions.get("verification") or {}
            issues = list(verification.get("issues") or [])
        detail = f" issues={issues}" if issues else ""
        raise RuntimeError(
            "No passing base loop seed. Run the full preset (2-bar record + 2 overdub passes, "
            "then F1 sweep):\n"
            "  .venv/bin/python scripts/host_midi_hitl.py run "
            "--preset note_edit_select_dependent_faders ...\n"
            f"Or fix the latest base report.{detail}"
        )

    seed_lines, base_config, seed_path = _resolve_base_seed(
        out_dir, seed_serial_log, ctx=ctx
    )
    print(
        f"[select-dependent-faders] using latest base seed: {seed_path} "
        f"(overall_ok={base_report_ok(report)}, loop_materialized="
        f"{base_report_loop_materialized(report)})"
    )
    return seed_lines, base_config, seed_path

def _pick_toggle_slots(layout) -> tuple[int, int] | None:
    note_slots = [
        (index, slot)
        for index, slot in enumerate(layout.nav_slots)
        if slot.note_idx >= 0
    ]
    for left in range(len(note_slots)):
        for right in range(left + 1, len(note_slots)):
            slot_a = note_slots[left][1]
            slot_b = note_slots[right][1]
            if slot_a.note_idx != slot_b.note_idx:
                return note_slots[left][0], note_slots[right][0]
    return None

def _slow_fader1_sweep(
    out_port,
    *,
    layout,
    dwell_ms: int,
) -> None:
    from hitl.control_constants import (
        FADER_SELECT_SETTLE_MS,
        NOTE_SELECTION_GRACE_MS,
    )
    from host_midi_automation_edit_baseline import _fader1_select_nav_slot_index

    count = layout.nav_slot_count
    per_slot_settle_ms = FADER_SELECT_SETTLE_MS + max(
        NOTE_SELECTION_GRACE_MS - FADER_SELECT_SETTLE_MS, 0
    )
    extra_dwell_ms = max(dwell_ms - per_slot_settle_ms, 0)
    print(
        f"[select-dependent-faders] fader1 sweep: {count} nav slots, "
        f"dwell={dwell_ms}ms (settle={per_slot_settle_ms}ms + extra={extra_dwell_ms}ms)"
    )
    for slot_index in range(count):
        _fader1_select_nav_slot_index(out_port, layout=layout, slot_index=slot_index)
        if extra_dwell_ms > 0:
            time.sleep(extra_dwell_ms / 1000.0)

def _two_note_toggle(
    out_port,
    *,
    layout,
    slot_a: int,
    slot_b: int,
    dwell_ms: int,
    cycles: int,
) -> None:
    from host_midi_automation_edit_baseline import _fader1_select_nav_slot_index

    if cycles <= 0:
        return
    print(
        f"[select-dependent-faders] two-note toggle slots {slot_a}<->{slot_b} "
        f"cycles={cycles} dwell={dwell_ms}ms"
    )
    for cycle in range(cycles):
        for slot_index in (slot_a, slot_b):
            _fader1_select_nav_slot_index(out_port, layout=layout, slot_index=slot_index)
            time.sleep(dwell_ms / 1000.0)
        if cycle == 0 or (cycle + 1) % 2 == 0:
            print(f"[select-dependent-faders] toggle progress {cycle + 1}/{cycles}")

def _ensure_note_edit_entered(
    out_port, collector, *, press_ms: int, phase_wait_ms: int, timeout_s: float
) -> bool:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_DEBOUNCE_MS,
        EDIT_BUTTON_NOTE,
    )
    from hitl.midi_io import _send_short_press

    def _last_toggle(lines: list[str]) -> tuple[int, str] | None:
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

    def _in_note_edit(lines: list[str]) -> bool:
        toggle = _last_toggle(lines)
        return toggle is not None and toggle[1] == "NOTE_EDIT"

    snapshot = collector.snapshot()
    if _in_note_edit(snapshot):
        print("[select-dependent-faders] NOTE_EDIT already active")
        return True

    for attempt in range(1, 3):
        baseline = len(collector.snapshot())
        print(f"[select-dependent-faders] enter NOTE_EDIT attempt {attempt}/2")
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
            if any("Edit session: NOTE_EDIT" in line for line in suffix):
                if any("exited edit mode" in line for line in suffix):
                    break
                print("[select-dependent-faders] NOTE_EDIT enter confirmed")
                return True
            time.sleep(0.05)
    return False

def run_note_edit_select_dependent_faders(args: object) -> int:
    import mido
    from hitl.control_constants import CONTROL_CHANNEL_1BASED
    from hitl.serial_collector import SerialCaptureCollector
    from hitl.midi_io import (
        _find_midi_port,
        _send_short_press,
    )
    from hitl.edit_controls import _ensure_transport_running
    from hitl.baseline_loop_inventory import (
        materialized_note_pairs_from_base_seed,
        nav_slot_stats,
        record_layout_from_base_seed,
    )

    ns = _parse_scenario_args(args)
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    ctx = get_context(args)
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number

    try:
        seed_lines, base_config, seed_path = _require_ok_base_seed(
            out_dir, ns.seed_serial_log, ctx=ctx
        )
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"[select-dependent-faders] {exc}")
        return 1

    layout = record_layout_from_base_seed(seed_lines, base_config)
    if layout.nav_slot_count <= 0:
        print("[select-dependent-faders] no nav slots from base seed")
        return 1

    loop_length, note_pairs = materialized_note_pairs_from_base_seed(seed_lines, base_config)
    stats = nav_slot_stats(note_pairs, loop_length=loop_length)
    print(
        f"[select-dependent-faders] seed={seed_path} loop_length={loop_length} "
        f"notes={stats['note_pairs']} nav_slots={stats['nav_slots']}"
    )

    if ns.serial_port is None:
        print("[select-dependent-faders] --serial-port required for live run")
        return 2

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: SerialCaptureCollector | None = None
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    serial_path = out_dir / f"note_edit_select_dependent_faders_{stamp}_serial.log"

    try:
        serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
        serial_collector.start()
        time.sleep(1.5)

        if ns.post_seed_settle_ms > 0:
            time.sleep(ns.post_seed_settle_ms / 1000.0)

        if not _ensure_transport_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        ):
            print("[select-dependent-faders] WARN: MIDI clock missing after transport start")

        track_index = ns.track_number - 1
        _send_short_press(
            out_port,
            note=60 + track_index,
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
        ):
            lines = serial_collector.snapshot()
            serial_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print("[select-dependent-faders] FAILED: NOTE_EDIT enter not confirmed")
            return 1

        ctx.markers.append("NOTE_EDIT active")

        if not ns.skip_sweep:
            _slow_fader1_sweep(out_port, layout=layout, dwell_ms=ns.dwell_ms)
            ctx.markers.append("fader1 sweep complete")

        toggle_pair = _pick_toggle_slots(layout)
        if toggle_pair is not None and ns.toggle_cycles > 0:
            slot_a, slot_b = toggle_pair
            _two_note_toggle(
                out_port,
                layout=layout,
                slot_a=slot_a,
                slot_b=slot_b,
                dwell_ms=ns.toggle_dwell_ms,
                cycles=ns.toggle_cycles,
            )
            ctx.markers.append(f"toggle slots {slot_a}<->{slot_b}")
        else:
            print("[select-dependent-faders] skip toggle (no distinct note slots)")

        time.sleep(ns.final_wait_ms / 1000.0)

        lines = serial_collector.snapshot()
        serial_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"[select-dependent-faders] serial log: {serial_path}")

        check = verify_note_edit_select_dependent_faders(lines, args)
        print(f"[select-dependent-faders] verification ok={check.get('ok')}")
        for issue in check.get("issues") or []:
            print(f"  issue: {issue}")

        report_path = out_dir / f"host_midi_hitl_note_edit_select_dependent_faders_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": "note_edit_select_dependent_faders",
                    "track_number": ns.track_number,
                    "midi_channel": midi_channel,
                    "seed_serial_log": str(seed_path) if seed_path else None,
                    "serial_log_path": str(serial_path),
                    "nav_slot_stats": stats,
                    "toggle_slots": list(toggle_pair) if toggle_pair else None,
                    "dwell_ms": ns.dwell_ms,
                    "toggle_dwell_ms": ns.toggle_dwell_ms,
                    "toggle_cycles": ns.toggle_cycles,
                    "serial_verification": check,
                    "markers": ctx.markers,
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        print(f"[select-dependent-faders] report: {report_path}")
        return 0 if check.get("ok", False) else 2
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()

def verify_note_edit_select_dependent_faders(lines: list[str], args: object) -> dict[str, object]:
    from hitl.verify.note_edit_select_dependent_faders import (
        verify_note_edit_select_dependent_faders as _verify,
    )

    return _verify(lines, args)
