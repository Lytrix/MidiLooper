"""HITL runner: preset/scenario dispatch and verify-only replay."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional

_SCRIPT_ROOT = Path(__file__).resolve().parent.parent
if str(_SCRIPT_ROOT) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_ROOT))

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
    return parser


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
    if getattr(args, "verify_only", False) or (
        args.verify_serial_log and not getattr(args, "serial_port", None)
    ):
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
            print(f"[hitl] scenario {sid} failed (exit {code}); aborting remaining scenarios")
            break
    return exit_code


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args, legacy = parser.parse_known_args(argv)
    setattr(args, "legacy_args", legacy)
    if args.command != "run":
        parser.print_help()
        return 2
    if args.list_scenarios:
        for sid in sorted(get_registry().keys()):
            print(sid)
        return 0
    scenario_ids: list[str]
    try:
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
    return run_scenarios(args, scenario_ids)
