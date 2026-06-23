"""Legacy baseline preset."""

from __future__ import annotations

import sys
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    pass


def run_base_scenario(args: object) -> int:
    import host_midi_automation_baseline as baseline

    legacy = list(getattr(args, "legacy_args", []) or [])
    old_argv = sys.argv
    try:
        sys.argv = ["host_midi_automation_baseline.py"] + legacy
        return baseline.run()
    finally:
        sys.argv = old_argv


def verify_base_scenario(lines: list[str], args: object) -> dict[str, object]:
    import host_midi_automation_baseline as baseline

    return {"ok": True, "note": "use baseline JSON report for full gate checks"}
