#!/usr/bin/env python3
"""OpenSpec 6.2.4 — B7 construction-only + B8 capture consumer gates."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_B7 = _REPO_ROOT / "src/LoopContentResolution.cpp"
_B8 = _REPO_ROOT / "src/Loop/LoopCapture.cpp"
_MATERIALIZE_RE = re.compile(r"\bmaterializeToEventVector\s*\(")


class B7B8ConstructionConsumerGateTests(unittest.TestCase):
    def test_b7_lcr_has_no_maintained_history_materialize(self) -> None:
        text = _B7.read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(text, _MATERIALIZE_RE)
        self.assertNotIn("Phase::Materialize", text)

    def test_b8_capture_has_no_direct_materialize_to_event_vector(self) -> None:
        text = _B8.read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(text, _MATERIALIZE_RE)

    def test_b8_capture_uses_snapshot_only_construction_owner(self) -> None:
        text = _B8.read_text(encoding="utf-8", errors="replace")
        self.assertIn("gatherConstructionCommittedEvents", text)
        self.assertIn("tryCopyCurrentSnapshotEditViewEvents", text)
        self.assertNotIn("copyCommittedPassReplayEvents", text)
        self.assertNotIn("gatherNarrowCompatCommittedEvents", text)


if __name__ == "__main__":
    unittest.main()
