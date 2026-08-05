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
    deferred_save_active_in_suffix,
    deferred_save_active_in_tail,
    deferred_save_idle_in_suffix,
    deferred_save_idle_in_tail,
    persistence_drain_stuck_in_suffix,
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
        self.assertTrue(deferred_save_active_in_suffix(lines))

    def test_pers_already_pending_counts_as_active(self) -> None:
        lines = [
            "#CAP,1,PERS,request,52000,348160,348160,already_pending",
        ]
        self.assertTrue(deferred_save_active_in_suffix(lines))
        self.assertFalse(deferred_save_idle_in_suffix(lines))

    def test_pers_already_pending_settled_by_result_ok(self) -> None:
        lines = [
            "#CAP,1,PERS,request,52000,348160,348160,already_pending",
            "#CAP,2,PERS,result,52000,348160,348160,ok",
            "#CAP,3,SAVE,completed,0",
        ]
        self.assertFalse(deferred_save_active_in_suffix(lines))
        self.assertTrue(deferred_save_idle_in_suffix(lines))

    def test_clear_waiting_save_human_log_active_until_settled(self) -> None:
        lines = [
            "[604.472] Clear waiting for deferred save completion",
        ]
        self.assertTrue(deferred_save_active_in_suffix(lines))
        lines.append("#CAP,4,SAVE,completed,0")
        self.assertFalse(deferred_save_active_in_suffix(lines))

    def test_pers_work_without_completion_not_idle(self) -> None:
        lines = [
            "#CAP,1,PERS,work,GlobalMeta,0,0",
            "#CAP,2,PERS,work,SlotMeta,4,2",
        ]
        self.assertTrue(deferred_save_active_in_suffix(lines))
        self.assertFalse(deferred_save_idle_in_suffix(lines))

    def test_pers_work_settled_by_result_ok(self) -> None:
        lines = [
            "#CAP,1,PERS,work,GlobalMeta,0,0",
            "#CAP,2,PERS,result,52000,348160,348160,ok",
            "#CAP,3,SAVE,completed,0",
        ]
        self.assertFalse(deferred_save_active_in_suffix(lines))
        self.assertTrue(deferred_save_idle_in_suffix(lines))

    def test_draining_queue_human_log_active_until_success(self) -> None:
        lines = [
            "[StorageManager] Draining persistence work queue to SD card...",
        ]
        self.assertTrue(deferred_save_active_in_suffix(lines))
        lines.append("[StorageManager] State saved successfully (v4).")
        self.assertFalse(deferred_save_active_in_suffix(lines))

    def test_persistence_drain_stuck_counts_as_active(self) -> None:
        lines = [
            "[StorageManager] Draining persistence work queue to SD card...",
            "[StorageManager] ERROR: Persistence drain stuck after 256 iterations",
        ]
        self.assertTrue(persistence_drain_stuck_in_suffix(lines))
        self.assertTrue(deferred_save_active_in_suffix(lines))
        self.assertFalse(deferred_save_idle_in_suffix(lines))

    def test_current_set_saved_human_log_settles_active(self) -> None:
        lines = [
            "#CAP,1,PERS,work,0,0,0,GlobalMeta,singleton,start,ok",
            "[StorageManager] CurrentSet saved successfully (v6 deferred slices).",
            "#CAP,2,PERS,result,42601,348160,348160,ok",
            "#CAP,3,SAVE,completed,0",
        ]
        self.assertFalse(deferred_save_active_in_suffix(lines))
        self.assertTrue(deferred_save_idle_in_suffix(lines))

    def test_tail_active_ignores_stale_prefix(self) -> None:
        lines = ["#CAP,1,SAVE,completed,0"] * 100 + [
            "#CAP,2,SAVE,pending,0",
            "#CAP,3,SAVE,in_progress,1",
        ]
        self.assertTrue(deferred_save_active_in_tail(lines, lookback=5))
        self.assertFalse(deferred_save_idle_in_tail(lines, lookback=5))

    def test_no_save_markers_not_active(self) -> None:
        lines = [
            "[1.0] Transport started",
            "#CAP,100,ST,Track,PLAYING,EMPTY",
        ]
        self.assertFalse(deferred_save_active_in_suffix(lines))
        self.assertTrue(deferred_save_idle_in_suffix(lines))

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
