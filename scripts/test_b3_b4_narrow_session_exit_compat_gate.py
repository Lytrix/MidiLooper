#!/usr/bin/env python3
"""OpenSpec 6.2.2 — B3/B4 narrow C1-a session-exit compatibility gates."""

from __future__ import annotations

import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_B3 = _REPO_ROOT / "src/Loop/LoopVisualCache.cpp"
_B4 = _REPO_ROOT / "src/DisplayManager/DisplayNoteResolveCommitted.cpp"
_B4_WINDOW = _REPO_ROOT / "src/DisplayManager/DisplayColdHelpers.cpp"


class B3B4NarrowSessionExitCompatGateTests(unittest.TestCase):
    def test_b3_idle_slice_uses_session_exit_publication_gap_predicate(self) -> None:
        text = _B3.read_text(encoding="utf-8", errors="replace")
        self.assertIn("isPlaybackSessionExitPublicationGap", text)
        self.assertRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,220}?gatherCommittedEventsInWindow\s*\(",
        )

    def test_b3_full_rebuild_uses_session_exit_publication_gap_predicate(self) -> None:
        text = _B3.read_text(encoding="utf-8", errors="replace")
        self.assertRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,220}?gatherCommittedEvents\s*\(",
        )

    def test_b3_tracks_passes_resolve_source(self) -> None:
        text = _B3.read_text(encoding="utf-8", errors="replace")
        self.assertIn("VisualCacheIdleGatherSource::PassesResolve", text)

    def test_b4_paint_uses_session_exit_publication_gap_predicate(self) -> None:
        text = _B4.read_text(encoding="utf-8", errors="replace")
        self.assertIn("isPlaybackSessionExitPublicationGap", text)
        self.assertRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,220}?gatherCommittedEvents\s*\(",
        )

    def test_b4_window_gather_uses_session_exit_publication_gap_predicate(self) -> None:
        text = _B4_WINDOW.read_text(encoding="utf-8", errors="replace")
        self.assertIn("isPlaybackSessionExitPublicationGap", text)
        self.assertRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,400}?gatherCommittedEventsInWindow\s*\(",
        )

    def test_b4_paint_does_not_label_generic_fallback(self) -> None:
        text = _B4.read_text(encoding="utf-8", errors="replace")
        self.assertIn("SessionExitCompat", text)
        self.assertNotIn('"Fallback"', text)


if __name__ == "__main__":
    unittest.main()
