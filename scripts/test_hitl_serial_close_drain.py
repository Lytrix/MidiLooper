#!/usr/bin/env python3
"""Unit tests for Mode A serial close drain (trailing USB TX flush)."""

from __future__ import annotations

import sys
import threading
import time
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from host_midi_automation_baseline import _wait_for_serial_line_idle


class SerialCloseDrainTests(unittest.TestCase):
    def test_returns_immediately_when_already_idle(self) -> None:
        added = _wait_for_serial_line_idle(lambda: 10, idle_ms=50, max_drain_ms=500)
        self.assertEqual(added, 0)

    def test_waits_for_trailing_lines_then_stops_at_idle(self) -> None:
        count = {"n": 0}

        def bump_later() -> None:
            time.sleep(0.08)
            count["n"] = 3

        threading.Thread(target=bump_later, daemon=True).start()
        started = time.monotonic()
        added = _wait_for_serial_line_idle(
            lambda: count["n"],
            idle_ms=80,
            max_drain_ms=2000,
            poll_s=0.01,
        )
        elapsed = time.monotonic() - started
        self.assertEqual(added, 3)
        self.assertGreaterEqual(elapsed, 0.14)
        self.assertLess(elapsed, 1.5)

    def test_respects_max_drain_cap(self) -> None:
        started = time.monotonic()
        added = _wait_for_serial_line_idle(
            lambda: 0,
            idle_ms=500,
            max_drain_ms=120,
            poll_s=0.01,
        )
        elapsed = time.monotonic() - started
        self.assertEqual(added, 0)
        self.assertGreaterEqual(elapsed, 0.10)
        self.assertLess(elapsed, 0.35)


if __name__ == "__main__":
    unittest.main()
