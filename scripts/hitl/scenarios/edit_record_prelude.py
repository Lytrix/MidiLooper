"""2-bar fixture record prelude for edit scenarios."""

from __future__ import annotations

import sys

def run_edit_record_prelude(args: object) -> int:
    """Record prelude is bundled in edit_full runner."""
    preset = getattr(args, "preset", None)
    scenarios = getattr(args, "scenarios", None) or ""
    if preset in (None, "edit_full"):
        return 0
    if "edit_full" in scenarios:
        return 0
    print("[hitl] edit_record_prelude: run with edit_full preset")
    return 2

def verify_edit_record_prelude(lines: list[str], args: object) -> dict[str, object]:
    from hitl.capture_transitions import (
        _count_capture_transitions,
        _record_entry_to_recording_count,
    )

    transitions = _count_capture_transitions(lines)
    record_ok = _record_entry_to_recording_count(transitions) >= 1
    play_ok = transitions.get(("STOPPED_RECORDING", "PLAYING"), 0) >= 1
    ok = record_ok and play_ok
    issues: list[str] = []
    if not record_ok:
        issues.append("missing_record_transition")
    if not play_ok:
        issues.append("missing_play_transition")
    return {"ok": ok, "issues": issues, "transitions": {f"{a}->{b}": c for (a, b), c in transitions.items()}}
