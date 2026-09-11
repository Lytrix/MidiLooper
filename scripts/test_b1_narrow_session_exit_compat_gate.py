#!/usr/bin/env python3
"""OpenSpec 6.3.1 — B1 must not call gatherCommittedEvents (C1-a retired)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_B1 = _REPO_ROOT / "src/Track/TrackPlaybackWindowBuild.cpp"

_GATHER_RE = re.compile(r"\bgatherCommittedEvents(?:InWindow|ForDerivedView)?\s*\(")


class B1SessionExitCompatRetiredGateTests(unittest.TestCase):
    def test_b1_has_no_gather_committed_calls(self) -> None:
        text = _B1.read_text(encoding="utf-8", errors="replace")
        self.assertIsNone(_GATHER_RE.search(text))

    def test_b1_emits_hold_empty_on_gather_miss(self) -> None:
        text = _B1.read_text(encoding="utf-8", errors="replace")
        self.assertIn("GatherMissHoldEmpty", text)

    def test_b1_does_not_label_gather_miss_as_session_exit_compat(self) -> None:
        text = _B1.read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("SessionExitPublicationGap", text)


if __name__ == "__main__":
    unittest.main()
