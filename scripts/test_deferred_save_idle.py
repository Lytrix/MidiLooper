#!/usr/bin/env python3
"""Unit tests for deferred save idle detection."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.deferred_save_idle import (
    deferred_save_idle_in_suffix,
    deferred_save_idle_in_tail,
)
from hitl.serial_transport import serial_global_transport_running


class DeferredSaveIdleTests(unittest.TestCase):
    def test_save_completed_counts_as_idle(self) -> None:
        lines = [
            "#CAP,1,SAVE,pending,0",
            "#CAP,2,SAVE,in_progress,2",
            "#CAP,3,PERS,result,52000,348160,348160,ok",
            "#CAP,4,SAVE,completed,0",
        ]
        self.assertTrue(deferred_save_idle_in_suffix(lines))

    def test_pending_without_completion_not_idle(self) -> None:
        lines = [
            "#CAP,1,SAVE,pending,0",
            "#CAP,2,SAVE,in_progress,2",
        ]
        self.assertFalse(deferred_save_idle_in_suffix(lines))

    def test_completed_before_anchor_still_idle(self) -> None:
        lines = [
            "#CAP,1,SAVE,pending,0",
            "#CAP,2,SAVE,completed,0",
            "[604.472] [INFO] Button press: Loop 2 (short)",
        ]
        self.assertTrue(deferred_save_idle_in_suffix(lines, after_index=2))

    def test_tail_uses_save_completed(self) -> None:
        lines = ["prefix"] * 100 + [
            "#CAP,3,PERS,result,1,2,3,ok",
            "#CAP,4,SAVE,completed,0",
        ]
        self.assertTrue(deferred_save_idle_in_tail(lines, lookback=5))


class TransportRunningTests(unittest.TestCase):
    def test_started_after_stopped_is_running(self) -> None:
        lines = [
            "[1.0] Transport stopped",
            "[2.0] Transport started",
        ]
        self.assertTrue(serial_global_transport_running(lines))

    def test_stopped_after_started_is_not_running(self) -> None:
        lines = [
            "[1.0] Transport started",
            "[2.0] Transport stopped",
        ]
        self.assertFalse(serial_global_transport_running(lines))


if __name__ == "__main__":
    unittest.main()
