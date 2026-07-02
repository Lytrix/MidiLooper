#!/usr/bin/env python3
"""Unit tests for baseline clear-before-record preconditions."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from host_midi_automation_baseline import (
    _can_skip_clear_before_record,
    _latest_track_state,
    _track_cleared_for_record,
)


class ClearBeforeRecordPreconditionTests(unittest.TestCase):
    def test_skip_when_latest_empty(self) -> None:
        lines = ["#CAP,1,ST,Track,ARMED,EMPTY,0"]
        self.assertTrue(_can_skip_clear_before_record(lines))
        self.assertEqual(_latest_track_state(lines), "EMPTY")

    def test_skip_when_no_st_and_no_loop_content(self) -> None:
        lines = ["[StorageManager] idle"]
        self.assertTrue(_can_skip_clear_before_record(lines))

    def test_no_skip_when_stopped_with_recs(self) -> None:
        lines = [
            "#CAP,1,ST,Track,RECORDING,STOPPED,0",
            "#CAP,2,RECS,1536,96",
        ]
        self.assertFalse(_can_skip_clear_before_record(lines))

    def test_cleared_for_record_armed_without_content(self) -> None:
        lines = ["#CAP,1,ST,Track,EMPTY,ARMED,0"]
        self.assertTrue(_track_cleared_for_record(lines))


if __name__ == "__main__":
    unittest.main()
