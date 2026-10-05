"""Task 6.4's windowed leg: the existing long-loop content-verification leg, read for the windowed lane.

The design's own rule is that F-7/F-8 *feed* the content-verification pipeline rather than building a second one
(`design.md`: "fed the windowed/demoted cases (new stimulus profile), not replaced"). So this module adds **no** driver
of its own: it reuses `run_long_loop_display_window` -- a leg that records a loop far longer than the served window
(`--record-bars`, default 24, and its own run refuses <= 16), keeps the transport running, and already has a host-tested
verifier -- and pairs it with the windowed lane's own checks (`hitl.verify.windowed_lane`), which judge the receipts and
coverage rows the profile declares and **name** the terms whose verdict belongs to another owner.
"""

from __future__ import annotations

from typing import Any

from hitl.scenarios.long_loop_display_window import run_long_loop_display_window
from hitl.verify.windowed_lane import verify_windowed_content_parity


def run_windowed_content_parity(args: object) -> int:
    """The reused leg's run, unchanged: the windowed rows come from the loop it plays, not from a new gesture."""
    return run_long_loop_display_window(args)


def verify_windowed_content_parity_leg(lines: list[str], args: object) -> dict[str, Any]:
    """The reused leg's oracle **plus** the windowed lane's checks (see `hitl.verify.windowed_lane`)."""
    return verify_windowed_content_parity(lines, args)
