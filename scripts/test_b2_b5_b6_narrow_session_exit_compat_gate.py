#!/usr/bin/env python3
"""OpenSpec 6.3.1 — B2/B5/B6 session-exit compat retired; stale-root only where noted."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_B2 = _REPO_ROOT / "src/Utils/CommittedMidiScratch.cpp"
_B5 = _REPO_ROOT / "src/Track/TrackDeferredMaintenance.cpp"
_B6 = _REPO_ROOT / "src/EditManager/NoteEditSessionLifecycle.cpp"

_MATERIALIZE_RE = re.compile(r"\bmaterializeEditViewFromPasses\s*\(")


class B2B5B6SessionExitCompatRetiredGateTests(unittest.TestCase):
    def test_b2_materialize_only_on_stale_root_not_session_exit_gap(self) -> None:
        text = _B2.read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("isPlaybackSessionExitPublicationGap", text)
        self.assertIn("publishedSnapshotIsCurrent", text)
        self.assertIn("RefreshKind::HoldEmpty", text)

    def test_b5_idle_does_not_materialize_on_session_exit_gap(self) -> None:
        text = _B5.read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,120}?"
            r"materializeEditViewFromPasses\s*\(",
        )

    def test_b5_oracle_gather_retained_for_verification(self) -> None:
        text = _B5.read_text(encoding="utf-8", errors="replace")
        self.assertIn("gatherCommittedEventsInWindow", text)

    def test_b6_note_edit_open_does_not_call_loop_midi_events(self) -> None:
        text = _B6.read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(text, r"\.midiEvents\s*\(")
        self.assertIn("copyEffectiveCommittedEvents", text)


if __name__ == "__main__":
    unittest.main()
