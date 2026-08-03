"""Full edit overlap suite preset."""

from __future__ import annotations

import sys

def run_edit_full_scenario(args: object) -> int:
    import host_midi_automation_edit_baseline as edit_baseline

    legacy = list(getattr(args, "legacy_args", []) or [])
    old_argv = sys.argv
    try:
        sys.argv = ["host_midi_automation_edit_baseline.py"] + legacy
        return edit_baseline.main()
    finally:
        sys.argv = old_argv

def verify_edit_full_scenario(lines: list[str], args: object) -> dict[str, object]:
    import host_midi_automation_edit_baseline as edit_baseline

    from host_midi_automation_edit_baseline import (
        EDIT_RECORD_FIXTURE,
        _verify_edit_serial,
        _verify_live_record_display,
    )
    from hitl.capture_transitions import (
        _count_capture_state_entries,
        _count_capture_transitions,
    )

    ctx = getattr(args, "hitl_context", None)
    layout = ctx.record_layout if ctx else None
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
