"""Occupy-paint HITL: seed record, source OD1, occupy OD2 (same-start / +1/4 / −1/2), GUS."""

from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

from hitl.baseline_canonical_args import (
    canonical_occupy_paint_legacy_args,
    merge_legacy_cli_args,
    strip_scenario_only_legacy_args,
)
from hitl.verify.occupy_paint import verify_occupy_paint


def _resolve_serial_lines(args: object, report: dict[str, Any] | None) -> list[str]:
    verify_log = getattr(args, "verify_serial_log", None)
    if verify_log is not None and Path(verify_log).is_file():
        return Path(verify_log).read_text(encoding="utf-8", errors="replace").splitlines()
    if report is not None:
        serial_path = report.get("serial_log_path")
        if serial_path and Path(serial_path).is_file():
            return Path(serial_path).read_text(encoding="utf-8", errors="replace").splitlines()
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    pointer = out_dir / ".current_session"
    if pointer.is_file():
        raw = pointer.read_text(encoding="utf-8").strip()
        if raw:
            path = Path(raw)
            if not path.is_absolute():
                path = (Path.cwd() / path).resolve()
            if path.is_file():
                return path.read_text(encoding="utf-8", errors="replace").splitlines()
    return []


def run_overdub_occupy_paint(args: object) -> int:
    import host_midi_automation_baseline as baseline
    from hitl.baseline_loop_inventory import latest_base_report

    legacy = strip_scenario_only_legacy_args(list(getattr(args, "legacy_args", []) or []))
    legacy = merge_legacy_cli_args(canonical_occupy_paint_legacy_args(), legacy)
    old_argv = sys.argv
    try:
        sys.argv = ["host_midi_automation_baseline.py"] + legacy
        code = baseline.run()
    finally:
        sys.argv = old_argv

    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    report = latest_base_report(out_dir)
    lines = _resolve_serial_lines(args, report)
    occupy_check = verify_occupy_paint(lines, args)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    occupy_path = out_dir / f"host_midi_hitl_occupy_paint_{stamp}.json"
    occupy_path.write_text(json.dumps(occupy_check, indent=2), encoding="utf-8")
    print(f"[occupy-paint] verifier report: {occupy_path}")
    if occupy_check.get("issues"):
        print(f"[occupy-paint] FAIL — {occupy_check['issues']}")
    else:
        rc1b = occupy_check.get("rc1b") or {}
        print(
            "[occupy-paint] PASS — "
            f"shorten={occupy_check.get('max_shorten')} "
            f"hide={occupy_check.get('max_hide')} "
            f"undo={occupy_check.get('undo_count')} "
            f"redo={occupy_check.get('redo_count')} "
            f"frame={rc1b.get('frame_count')} "
            f"effective={rc1b.get('effective_count')}"
        )
    if not occupy_check.get("ok", False):
        return max(code, 2)
    return code


def verify_overdub_occupy_paint(lines: list[str], args: object) -> dict[str, object]:
    return verify_occupy_paint(lines, args)
