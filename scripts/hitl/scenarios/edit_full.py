"""Full edit overlap suite preset."""

from __future__ import annotations

from pathlib import Path
from typing import Any


def _parse_edit_args(args: object) -> Any:
    from hitl.baseline_canonical_args import collapse_legacy_cli_args
    from hitl.legacy_edit_baseline import parse_edit_baseline_args

    legacy = collapse_legacy_cli_args(list(getattr(args, "legacy_args", []) or []))
    parsed = parse_edit_baseline_args(legacy)
    parsed.out_dir = Path(getattr(args, "out_dir", parsed.out_dir))
    verify_log = getattr(args, "verify_serial_log", None)
    if verify_log is not None:
        parsed.verify_serial_log = verify_log
    hitl_context = getattr(args, "hitl_context", None)
    if hitl_context is not None:
        parsed.hitl_context = hitl_context
    return parsed


def run_edit_full_scenario(args: object) -> int:
    from hitl.legacy_edit_baseline import run_edit_baseline

    parsed = _parse_edit_args(args)
    session = getattr(args, "hitl_session", None)
    if session is not None:
        return run_edit_baseline(
            parsed,
            out_port=session.out_port,
            in_port=session.in_port,
            serial_collector=session.collector,
            owns_resources=False,
        )
    return run_edit_baseline(parsed)


def _record_layout_from_edit_report(report: dict[str, object]) -> Any:
    from hitl.legacy_edit_baseline import RecordLayout

    layout_blob = report.get("record_layout")
    if not isinstance(layout_blob, dict):
        return None
    loop_length = int(layout_blob.get("loop_length") or 0)
    if loop_length <= 0:
        return None
    return RecordLayout(
        loop_start=int(layout_blob.get("loop_start") or 0),
        loop_length=loop_length,
        step_to_tick={},
        nav_slots=[],
    )


def latest_edit_report(out_dir: Path) -> dict[str, object] | None:
    candidates = sorted(
        out_dir.glob("host_midi_automation_edit_baseline_*.json"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    for path in candidates:
        if path.name.endswith("_serial.log"):
            continue
        try:
            import json

            return json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
    return None


def verify_edit_full_scenario(lines: list[str], args: object) -> dict[str, object]:
    from host_midi_automation_edit_baseline import (
        EDIT_RECORD_FIXTURE,
        _verify_edit_serial,
        _verify_live_record_display,
    )
    from hitl.capture_transitions import (
        _count_capture_state_entries,
        _count_capture_transitions,
    )

    parsed = _parse_edit_args(args)
    ctx = getattr(parsed, "hitl_context", None)
    layout = ctx.record_layout if ctx else None
    if layout is None:
        report = latest_edit_report(parsed.out_dir)
        if report is not None:
            layout = _record_layout_from_edit_report(report)

    check = _verify_edit_serial(
        lines,
        expected_markers=[],
        min_revt_count=len(EDIT_RECORD_FIXTURE),
        min_undo_logs=2,
        min_redo_logs=2,
        verify_move_display=layout is not None,
        verify_long_over_short_pitch=layout is not None,
        verify_m8_edit_pass=True,
        record_layout=layout,
    )
    live = _verify_live_record_display(lines)
    ok = bool(check.get("ok")) and bool(live.get("ok", True))
    return {
        "ok": ok,
        "edit": check,
        "live_record_display": live,
        "state_counts": _count_capture_state_entries(lines),
        "transitions": {
            f"{a}->{b}": c for (a, b), c in _count_capture_transitions(lines).items()
        },
    }
