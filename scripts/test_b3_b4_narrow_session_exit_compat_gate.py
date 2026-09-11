#!/usr/bin/env python3
"""OpenSpec 6.3.1 — B3/B4 must not call gatherCommittedEvents on session-exit gap."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_B3 = _REPO_ROOT / "src/Loop/LoopVisualCache.cpp"
_B4 = _REPO_ROOT / "src/DisplayManager/DisplayNoteResolveCommitted.cpp"
_B4_WINDOW = _REPO_ROOT / "src/DisplayManager/DisplayColdHelpers.cpp"

_GATHER_RE = re.compile(r"\bgatherCommittedEvents(?:InWindow|WithCapture|ForDerivedView)?\s*\(")


class B3B4SessionExitCompatRetiredGateTests(unittest.TestCase):
    def test_b3_has_no_gather_committed_calls(self) -> None:
        text = _B3.read_text(encoding="utf-8", errors="replace")
        self.assertIsNone(_GATHER_RE.search(text))

    def test_b3_hold_empty_during_session_exit_gap(self) -> None:
        text = _B3.read_text(encoding="utf-8", errors="replace")
        self.assertIn("isPlaybackSessionExitPublicationGap", text)
        self.assertIn("VisualCacheIdleGatherSource::None", text)

    def test_b4_paint_has_no_gather_committed_calls(self) -> None:
        text = _B4.read_text(encoding="utf-8", errors="replace")
        self.assertIsNone(_GATHER_RE.search(text))

    def test_b4_window_gather_has_no_gather_committed_calls(self) -> None:
        text = _B4_WINDOW.read_text(encoding="utf-8", errors="replace")
        self.assertIsNone(_GATHER_RE.search(text))

    def test_b4_paint_does_not_label_session_exit_compat(self) -> None:
        text = _B4.read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("SessionExitCompat", text)


if __name__ == "__main__":
    unittest.main()
