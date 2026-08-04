#!/usr/bin/env python3
"""Unit tests for serial/protocol.py parsers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.serial.protocol import (
    count_track_transitions,
    extract_recs_stop_lengths,
    latest_track_state,
    parse_track_transition,
)


class SerialProtocolTests(unittest.TestCase):
    def test_parse_track_transition(self) -> None:
        line = "#CAP,1000,ST,Track,ARMED,RECORDING"
        self.assertEqual(parse_track_transition(line), ("ARMED", "RECORDING"))

    def test_count_track_transitions(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
        ]
        counts = count_track_transitions(lines)
        self.assertEqual(counts[("ARMED", "RECORDING")], 1)
        self.assertEqual(counts[("RECORDING", "STOPPED_RECORDING")], 1)

    def test_latest_track_state(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,PLAYING",
        ]
        self.assertEqual(latest_track_state(lines), "PLAYING")

    def test_extract_recs_stop_lengths(self) -> None:
        line = "#CAP,5000,RECS,stop,100,200,1536,1536"
        rows = extract_recs_stop_lengths([line])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["final_length"], 1536)


if __name__ == "__main__":
    unittest.main()
