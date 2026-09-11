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
    def test_b7_materialize_only_in_device_gate_construction_phase(self) -> None:
        text = _B7.read_text(encoding="utf-8", errors="replace")
        matches = list(_MATERIALIZE_RE.finditer(text))
        self.assertEqual(1, len(matches), "B7 must retain a single construction materialize site")
        self.assertRegex(
            text,
            r"case Phase::Materialize:[\s\S]{0,320}?"
            r"materializeToEventVector\s*\(",
        )

    def test_b8_capture_has_no_direct_materialize_to_event_vector(self) -> None:
        text = _B8.read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(text, _MATERIALIZE_RE)

    def test_b8_capture_uses_construction_owner_not_direct_materialize(self) -> None:
        text = _B8.read_text(encoding="utf-8", errors="replace")
        self.assertIn("gatherConstructionCommittedEvents", text)
        self.assertIn("gatherNarrowCompatCommittedEvents", text)
        self.assertIn("tryCopyCurrentSnapshotEditViewEvents", text)
        self.assertIn("isPlaybackSessionExitPublicationGap", text)
        self.assertIn("copyCommittedPassReplayEvents", text)


if __name__ == "__main__":
    unittest.main()
