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

from hitl.serial_collector import _wait_for_serial_line_idle, _wait_for_serial_predicate
from hitl.verify.dnte_src_dumps import dnte_src_dump_trio_complete

class SerialCloseDrainTests(unittest.TestCase):
    def test_returns_immediately_when_already_idle(self) -> None:
        added = _wait_for_serial_line_idle(lambda: 10, idle_ms=50, max_drain_ms=500)
        self.assertEqual(added, 0)

    def test_waits_for_trailing_lines_then_stops_at_idle(self) -> None:
        count = {"n": 0}

        def bump_later() -> None:
            time.sleep(0.03)
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
        # bump at ~30ms + idle 80ms after last growth
        self.assertGreaterEqual(elapsed, 0.10)
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

    def test_predicate_waits_until_dump_trio_complete(self) -> None:
        lines: list[str] = []

        def arrive_later() -> None:
            time.sleep(0.03)
            lines.extend(
                [
                    "#CAP,1,DIAG,dnte,src,reconstruct,count,1",
                    "#CAP,2,DNTE,60,0,0,40,0",
                    "#CAP,3,DIAG,dnte,src,effective,count,1",
                    "#CAP,4,DNTE,60,0,0,40,0",
                    "#CAP,5,DIAG,dnte,src,frame,count,1",
                    "#CAP,6,DNTE,60,0,0,40,0",
                ]
            )

        threading.Thread(target=arrive_later, daemon=True).start()
        started = time.monotonic()
        complete = _wait_for_serial_predicate(
            lambda: list(lines),
            dnte_src_dump_trio_complete,
            max_drain_ms=2000,
            poll_s=0.01,
        )
        elapsed = time.monotonic() - started
        self.assertTrue(complete)
        self.assertGreaterEqual(elapsed, 0.03)
        self.assertLess(elapsed, 1.5)

    def test_predicate_times_out_when_frame_missing(self) -> None:
        lines = [
            "#CAP,1,DIAG,dnte,src,reconstruct,count,1",
            "#CAP,2,DNTE,60,0,0,40,0",
            "#CAP,3,DIAG,dnte,src,effective,count,1",
            "#CAP,4,DNTE,60,0,0,40,0",
        ]
        started = time.monotonic()
        complete = _wait_for_serial_predicate(
            lambda: lines,
            dnte_src_dump_trio_complete,
            max_drain_ms=80,
            poll_s=0.01,
        )
        elapsed = time.monotonic() - started
        self.assertFalse(complete)
        self.assertGreaterEqual(elapsed, 0.07)
        self.assertLess(elapsed, 0.35)


if __name__ == "__main__":
    unittest.main()
