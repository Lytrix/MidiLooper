#!/usr/bin/env python3
"""OpenSpec 6.2.1b — B1 narrow C1-a session-exit compatibility gates."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_B1 = _REPO_ROOT / "src/Track/TrackPlaybackWindowBuild.cpp"
_LOOP_PUBLISH = _REPO_ROOT / "src/Loop/LoopSnapshotPublish.cpp"


class B1NarrowSessionExitCompatGateTests(unittest.TestCase):
    def test_b1_uses_session_exit_publication_gap_predicate(self) -> None:
        text = _B1.read_text(encoding="utf-8", errors="replace")
        self.assertIn("isPlaybackSessionExitPublicationGap", text)

    def test_b1_gather_only_on_session_exit_gap_branch(self) -> None:
        text = _B1.read_text(encoding="utf-8", errors="replace")
        for pattern in (
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,220}?gatherCommittedEventsInWindow\s*\(",
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,220}?gatherCommittedEventsForDerivedView\s*\(",
        ):
            self.assertRegex(text, pattern, msg=f"missing guarded gather for {pattern}")

    def test_b1_emits_hold_empty_policy_on_unbounded_miss(self) -> None:
        text = _B1.read_text(encoding="utf-8", errors="replace")
        self.assertIn("hold_empty", text)
        self.assertIn("playbackB1GatherMissPolicy", text)

    def test_b1_does_not_label_gather_miss_as_generic_compatibility(self) -> None:
        text = _B1.read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("PlaybackRebuildPath::CompatibilityPasses", text)

    def test_loop_predicate_requires_session_exit_queue(self) -> None:
        text = _LOOP_PUBLISH.read_text(encoding="utf-8", errors="replace")
        self.assertRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,240}?pendingSnapshotSessionExitQueued\(\)",
        )
        self.assertRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,120}?publishedSnapshot\(\)",
        )


if __name__ == "__main__":
    unittest.main()
