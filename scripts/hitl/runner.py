"""HITL runner: preset/scenario dispatch and verify-only replay."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Callable, Optional

_SCRIPT_ROOT = Path(__file__).resolve().parent.parent
_PROJECT_ROOT = _SCRIPT_ROOT.parent
if str(_SCRIPT_ROOT) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_ROOT))

from hitl.layered_cli import (
    config_from_runner_args,
    legacy_args_with_slot_flags,
    require_slot_target,
)
from hitl.registry import get_registry, resolve_scenarios


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Modular HITL MIDI automation runner")
    sub = parser.add_subparsers(dest="command", required=True)

    run_parser = sub.add_parser("run", help="Run MIDI phases and optional serial verifiers")
    run_parser.add_argument("--preset", default=None, help="Named preset (base, edit_full, …)")
    run_parser.add_argument(
        "--scenarios",
        default=None,
        help="Comma-separated scenario ids (overrides --preset when set)",
    )
    run_parser.add_argument("--verify-serial-log", type=Path, default=None)
    run_parser.add_argument("--verify-only", action="store_true", help="Run verifiers only (no MIDI)")
    run_parser.add_argument("--out-dir", type=Path, default=Path("captures"))
    run_parser.add_argument("--list-scenarios", action="store_true")
    run_parser.add_argument(
        "--track",
        "--track-number",
        type=int,
        default=None,
        dest="track_number",
        metavar="N",
        help="Track row to exercise (1-8, required)",
    )
    run_parser.add_argument(
        "--loop-slot",
        type=int,
        default=None,
        metavar="N",
        help="Loop slot on that track (1-8, required)",
    )
    run_parser.add_argument(
        "--midi-channel",
        type=int,
        default=None,
        help="MIDI channel for note input (1-15, required)",
    )
    run_parser.add_argument(
        "--layered",
        action="store_true",
        help="Use layered foundation_runner (Phase 2+ scenarios)",
    )
    run_parser.add_argument(
        "--in-edit-undo-after-overdub",
        action="store_true",
        default=True,
        help="Session undo after in-edit overdub (edit_overdub_during_note_edit)",
    )
    run_parser.add_argument(
        "--no-in-edit-undo-after-overdub",
        action="store_false",
        dest="in_edit_undo_after_overdub",
    )
    run_parser.add_argument(
        "--post-exit-global-undo-redo",
        action="store_true",
        default=True,
        help="Three global undos after exit (edit_overdub_during_note_edit)",
    )
    run_parser.add_argument(
        "--no-post-exit-global-undo-redo",
        action="store_false",
        dest="post_exit_global_undo_redo",
    )
    run_parser.add_argument(
        "--overlay-scroll-step-dwell-ms",
        type=int,
        default=3000,
        help="Pause on each overlay list row so you can watch the OLED (0=immediate)",
    )
    run_parser.add_argument(
        "--overlay-root-dwell-ms",
        type=int,
        default=5000,
        help="Pause with root overlay open before scroll/exit",
    )
    run_parser.add_argument(
        "--overlay-dirty-dwell-ms",
        type=int,
        default=3000,
        help="Pause on dirty-prompt row before confirm",
    )
    run_parser.add_argument(
        "--catalog-set-index",
        type=int,
        default=0,
        help="0 = first Set row in overlay root list (after Save/Current)",
    )
    run_parser.add_argument(
        "--build-upload",
        action="store_true",
        help="Build and upload firmware before scenarios (default: assume firmware on device)",
    )
    run_parser.add_argument(
        "--build-env",
        default="teensy41-capture-serial",
        help="PlatformIO env for --build-upload (default: teensy41-capture-serial)",
    )
    run_parser.add_argument(
        "--capture-port",
        default="/dev/cu.usbmodem154944801",
        help="USB serial port for managed capture_session.py (default Teensy cu device)",
    )
    run_parser.add_argument(
        "--capture-boot-wait",
        type=float,
        default=0.0,
        help="Boot settle seconds passed to capture_session.py (default: 0)",
    )
    run_parser.add_argument(
        "--no-managed-capture",
        action="store_true",
        help="Do not spawn capture_session.py (use --serial-port for built-in serial instead)",
    )
    return parser


def _legacy_has_serial_follow(legacy: list[str]) -> bool:
    return "--follow-current-session" in legacy or "--follow-serial-log" in legacy


def _should_use_managed_capture(args: argparse.Namespace, legacy: list[str]) -> bool:
    if getattr(args, "verify_only", False):
        return False
    if getattr(args, "no_managed_capture", False):
        return False
    if "--serial-port" in legacy:
        return False
    return True


def _run_with_optional_managed_capture(
    args: argparse.Namespace,
    legacy: list[str],
    run_fn: Callable[[], int],
) -> int:
    if not _should_use_managed_capture(args, legacy):
        return run_fn()
    from hitl.config import HitlConfig
    from hitl.managed_capture import legacy_args_with_managed_capture, run_with_managed_capture

    track_number, loop_slot, midi_channel = require_slot_target(args, legacy)
    config = HitlConfig(
        preset=getattr(args, "preset", None),
        scenario_ids=(),
        track_number=track_number,
        loop_slot=loop_slot,
        midi_channel=midi_channel,
        record_bars=2,
        overdub_bars=2,
        midi_out_name="Teensy",
        midi_in_name="Teensy",
        serial_port=None,
        verify_serial_log=getattr(args, "verify_serial_log", None),
        verify_only=False,
        out_dir=Path(getattr(args, "out_dir", Path("captures"))),
        managed_capture=True,
        capture_serial_port=getattr(args, "capture_port", "/dev/cu.usbmodem154944801"),
        capture_boot_wait_s=float(getattr(args, "capture_boot_wait", 10.0)),
    )

    def _legacy_run(_active: HitlConfig) -> int:
        merged = legacy_args_with_managed_capture(legacy, args)
        setattr(args, "legacy_args", merged)
        return run_fn()

    return run_with_managed_capture(config, _legacy_run)


def maybe_build_upload(args: argparse.Namespace) -> int:
    if not getattr(args, "build_upload", False):
        return 0
    env = getattr(args, "build_env", "teensy41-capture-serial")
    print(
        f"[hitl] build-upload: pio run -e {env} -t upload "
        "(Teensy will reset — managed capture will restart on the next run)"
    )
    result = subprocess.run(
        ["pio", "run", "-e", env, "-t", "upload"],
        cwd=_PROJECT_ROOT,
    )
    if result.returncode != 0:
        print(f"[hitl] build-upload failed (exit {result.returncode})")
        return result.returncode
    print("[hitl] build-upload complete")
    return 0


def _read_verification_lines(args: argparse.Namespace) -> list[str]:
    if args.verify_serial_log and args.verify_serial_log.is_file():
        return args.verify_serial_log.read_text(encoding="utf-8", errors="replace").splitlines()
    return []


def run_verify_only(args: argparse.Namespace, scenario_ids: list[str]) -> int:
    lines = _read_verification_lines(args)
    if not lines:
        print("[hitl] verify-only requires --verify-serial-log with a readable file")
        return 2
    registry = get_registry()
    results: dict[str, object] = {}
    ok = True
    for sid in scenario_ids:
        spec = registry[sid]
        if spec.verify is None:
            print(f"[hitl] scenario {sid}: no verifier (skipped)")
            continue
        check = spec.verify(lines, args)
        results[sid] = check
        if not check.get("ok", False):
            ok = False
            print(f"[hitl] scenario {sid}: FAIL — {check.get('issues', check)}")
        else:
            print(f"[hitl] scenario {sid}: PASS")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    report_path = args.out_dir / f"host_midi_hitl_verify_{stamp}.json"
    report_path.write_text(json.dumps({"scenarios": scenario_ids, "results": results}, indent=2))
    print(f"[hitl] report: {report_path}")
    return 0 if ok else 2


def run_scenarios(args: argparse.Namespace, scenario_ids: list[str]) -> int:
    legacy = list(getattr(args, "legacy_args", []) or [])
    if getattr(args, "verify_only", False):
        return run_verify_only(args, scenario_ids)
    if args.verify_serial_log and not getattr(args, "serial_port", None):
        if not _legacy_has_serial_follow(legacy):
            return run_verify_only(args, scenario_ids)

    registry = get_registry()
    exit_code = 0
    for sid in scenario_ids:
        spec = registry[sid]
        if spec.run is None:
            print(f"[hitl] scenario {sid}: no run handler")
            exit_code = max(exit_code, 2)
            continue
        print(f"[hitl] running scenario: {sid}")
        code = spec.run(args)
        if code != 0:
            exit_code = max(exit_code, code)
            preset = getattr(args, "preset", None)
            if preset == "edit_minimal" and sid == "base":
                from hitl.baseline_loop_inventory import (
                    base_report_record_seed_ok,
                    latest_base_report,
                )

                report = latest_base_report(getattr(args, "out_dir", Path("captures")))
                if base_report_record_seed_ok(report):
                    print(
                        "[hitl] base scenario failed strict exit but edit seed ok — "
                        "running edit_minimal"
                    )
                    continue
            print(f"[hitl] scenario {sid} failed (exit {code}); aborting remaining scenarios")
            break
    return exit_code


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args, legacy = parser.parse_known_args(argv)
    if args.command != "run":
        parser.print_help()
        return 2
    if not args.list_scenarios:
        try:
            legacy = legacy_args_with_slot_flags(args, legacy)
        except ValueError as exc:
            print(f"[hitl] {exc}")
            return 2
    setattr(args, "legacy_args", legacy)
    if args.list_scenarios:
        if getattr(args, "layered", False):
            from hitl.layered_registry import get_layered_registry

            for sid in sorted(get_layered_registry().keys()):
                print(sid)
        else:
            for sid in sorted(get_registry().keys()):
                print(sid)
        return 0
    scenario_ids: list[str]
    try:
        if getattr(args, "layered", False):
            from hitl.foundation_runner import run_layered_preset

            upload_code = maybe_build_upload(args)
            if upload_code != 0:
                return upload_code
            if not args.preset and not args.scenarios:
                args.preset = "base"
            config = config_from_runner_args(args, legacy)
            return run_layered_preset(config)
        scenario_ids = resolve_scenarios(
            preset=args.preset,
            scenario_ids=args.scenarios.split(",") if args.scenarios else None,
        )
    except ValueError as exc:
        print(f"[hitl] {exc}")
        return 2
    if not args.preset and not args.scenarios:
        args.preset = "base"
        scenario_ids = resolve_scenarios(preset="base", scenario_ids=None)
    upload_code = maybe_build_upload(args)
    if upload_code != 0:
        return upload_code
    return _run_with_optional_managed_capture(
        args,
        legacy,
        lambda: run_scenarios(args, scenario_ids),
    )
