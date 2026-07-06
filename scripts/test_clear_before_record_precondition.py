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
    _serial_has_clear_completed,
    _track_cleared_for_record,
)


class ClearBeforeRecordPreconditionTests(unittest.TestCase):
    def test_skip_when_latest_empty(self) -> None:
        lines = ["#CAP,1,ST,Track,ARMED,EMPTY,0"]
        self.assertTrue(_can_skip_clear_before_record(lines))
        self.assertEqual(_latest_track_state(lines), "EMPTY")

    def test_no_skip_when_no_st_lines_without_positive_empty(self) -> None:
        lines = ["[StorageManager] idle"]
        self.assertFalse(_can_skip_clear_before_record(lines))

    def test_skip_when_clear_ignored(self) -> None:
        lines = ["Clear ignored — track is empty"]
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

    def test_cleared_for_record_clear_log_without_st(self) -> None:
        lines = ["[INFO] MIDI: Clear Track"]
        self.assertTrue(_track_cleared_for_record(lines))
        self.assertTrue(_serial_has_clear_completed(lines))

    def test_cleared_for_record_clear_log_after_stopped_recs(self) -> None:
        """Long-loop clear: STOPPED+RECS may linger in log; clear log confirms precondition."""
        from host_midi_automation_edit_baseline import _track_cleared_for_record as edit_cleared

        lines = [
            "#CAP,1,ST,Track,RECORDING,STOPPED,0",
            "#CAP,2,RECS,49152,96",
            "[INFO] MIDI: Clear Track",
        ]
        self.assertTrue(edit_cleared(lines))

if __name__ == "__main__":
    unittest.main()
