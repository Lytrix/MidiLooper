#!/usr/bin/env python3
"""OpenSpec 6.2.3 — B2/B5/B6 narrow C1-a / stale-root compatibility gates."""

from __future__ import annotations

import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_B2 = _REPO_ROOT / "src/Utils/CommittedMidiScratch.cpp"
_B5 = _REPO_ROOT / "src/Track/TrackDeferredMaintenance.cpp"
_B6 = _REPO_ROOT / "src/EditManager/NoteEditSessionLifecycle.cpp"


class B2B5B6NarrowSessionExitCompatGateTests(unittest.TestCase):
    def test_b2_uses_session_exit_publication_gap_predicate(self) -> None:
        text = _B2.read_text(encoding="utf-8", errors="replace")
        self.assertIn("isPlaybackSessionExitPublicationGap", text)

    def test_b2_materialize_only_on_session_exit_or_stale_root(self) -> None:
        text = _B2.read_text(encoding="utf-8", errors="replace")
        self.assertRegex(
            text,
            r"shouldMaterializeCommittedScratchFromPasses[\s\S]{0,320}?"
            r"isPlaybackSessionExitPublicationGap\(\)",
        )
        self.assertIn("publishedSnapshotIsCurrent", text)
        self.assertIn("RefreshKind::HoldEmpty", text)

    def test_b5_idle_materialize_only_on_session_exit_gap(self) -> None:
        text = _B5.read_text(encoding="utf-8", errors="replace")
        self.assertRegex(
            text,
            r"isPlaybackSessionExitPublicationGap\(\)[\s\S]{0,120}?"
            r"materializeEditViewFromPasses\s*\(",
        )

    def test_b6_note_edit_open_does_not_call_loop_midi_events(self) -> None:
        text = _B6.read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(text, r"\.midiEvents\s*\(")
        self.assertIn("copyEffectiveCommittedEvents", text)


if __name__ == "__main__":
    unittest.main()
