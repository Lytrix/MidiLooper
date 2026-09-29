#!/usr/bin/env python3
"""Tests for device-time freshness: a drained capture record must not authorize a phase.

The firmware writes capture rows through a fixed evidence ring that flushes in batches. When the
reader has been behind, a batch of **older** records lands after newer ones, so line order is not
device order and `lines[after_index:]` cannot answer "is the transport running now?".

Real shape from `captures/s4f22-step11-h2` (a run that attached to a device whose ring still held a
long pre-run backlog): `BAR` rows stamped 850-1864 s interleaved after a 1855 s `state=STOPPED`
reply; the harness read the drained `BAR` rows as current, skipped its transport start, and later
pressed a "stop" onto an already-stopped device -- which *started* it, so the clear could not drain.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl import serial_transport  # noqa: E402
from hitl.device_time import (  # noqa: E402
    device_clock_step,
    device_stamp_us,
    filter_live_lines,
    is_drained_history,
    newest_device_us,
)


class DeviceStampTests(unittest.TestCase):
    def test_cap_and_family_rows_carry_the_device_clock(self) -> None:
        self.assertEqual(1855136515, device_stamp_us("#CAP,1855136515,BAR,318720,415"))
        self.assertEqual(425131, device_stamp_us("#DMA_XFER,425131,init=1,degraded=0"))
        self.assertEqual(426427337, device_stamp_us("#MEM_OWNERS,426427337,phase=owners"))

    def test_directly_printed_rows_have_no_stamp_and_are_live(self) -> None:
        # `#STOP_STATUS` and human lines are written straight to Serial, never through the ring.
        self.assertIsNone(device_stamp_us("#STOP_STATUS,us=1855136515,state=STOPPED,pending=0"))
        self.assertIsNone(device_stamp_us("[1857.073] [INFO] Transport started"))
        self.assertFalse(is_drained_history("#STOP_STATUS,us=1,state=STOPPED", watermark_us=999))


class DrainedHistoryTests(unittest.TestCase):
    def test_a_row_behind_the_watermark_is_drained_history(self) -> None:
        self.assertTrue(is_drained_history("#CAP,850583933,BAR,318720,415", watermark_us=1855136515))
        self.assertFalse(is_drained_history("#CAP,1864337352,BAR,1,2", watermark_us=1855136515))

    def test_the_watermark_advances_with_the_newest_row(self) -> None:
        self.assertEqual(20, device_clock_step("#CAP,20,X,", 10))
        self.assertEqual(20, device_clock_step("#CAP,15,X,", 20))
        self.assertEqual(20, device_clock_step("#STOP_STATUS,us=99", 20))
        self.assertEqual(3000, device_clock_step("#CAP,3000,X,", 20))

    def test_a_boot_restarts_the_device_clock_instead_of_looking_drained(self) -> None:
        # `micros()` starts again after `!REBOOT`, so the banner is small by definition.
        banner = "#CAP,1583004,DIAG,boot_reset,crash=0"
        self.assertFalse(is_drained_history(banner, watermark_us=1_855_000_000))
        self.assertEqual(1583004, device_clock_step(banner, 1_855_000_000))
        # ... and rows after it are judged against the restarted clock.
        self.assertFalse(
            is_drained_history("#CAP,1600000,ST,Track,PLAYING,PLAYING", watermark_us=1583004)
        )

    def test_newest_stamp_respects_the_window(self) -> None:
        lines = ["#CAP,10,X,", "#CAP,30,X,", "#CAP,20,X,"]
        self.assertEqual(30, newest_device_us(lines))
        self.assertEqual(10, newest_device_us(lines, upto_index=1))

    def test_filter_keeps_live_rows_and_order(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,STOPPED,STOPPED",     # baseline
            "#STOP_STATUS,us=1000,state=STOPPED",     # live, no stamp
            "#CAP,900,BAR,1,2",                       # drained: older than the watermark
            "#CAP,1100,BAR,3,4",                      # live
            "#CAP,1050,DIAG,clk,0,0",                 # drained
        ]
        self.assertEqual(
            ["#STOP_STATUS,us=1000,state=STOPPED", "#CAP,1100,BAR,3,4"],
            filter_live_lines(lines, after_index=1),
        )


class TransportActivityTests(unittest.TestCase):
    def test_drained_bar_rows_do_not_authorize_transport_activity(self) -> None:
        """The h2 shape: the tail is nothing but drained `BAR`/`DFRAME` history."""
        lines = [
            "#CAP,1855136515,ST,Track,STOPPED,STOPPED",  # run baseline at 1855 s
            "#STOP_STATUS,us=1855136515,state=STOPPED,pending=0",
            "#CAP,850583933,BAR,318720,415",
            "#CAP,852583855,BAR,319488,416",
            "#CAP,856582600,DFRAME,32,13511,2370,187,619,38",
        ]
        self.assertFalse(
            serial_transport.serial_lines_show_transport_activity(lines, after_index=1)
        )

    def test_live_transport_rows_still_authorize_it(self) -> None:
        lines = [
            "#CAP,1855136515,ST,Track,STOPPED,STOPPED",
            "#CAP,1856000000,BAR,318720,415",
        ]
        self.assertTrue(
            serial_transport.serial_lines_show_transport_activity(lines, after_index=1)
        )

    def test_the_prefix_before_the_baseline_is_never_activity(self) -> None:
        # Only a `DISP` row after the baseline: the pre-baseline `BAR` must not count.
        lines = ["#CAP,1000000,BAR,1,2", "#CAP,1000001,DISP,0,STOPPED,2304"]
        self.assertFalse(
            serial_transport.serial_lines_show_transport_activity(lines, after_index=1)
        )
        self.assertTrue(
            serial_transport.serial_lines_show_transport_activity(lines, after_index=0)
        )

    def test_an_unstamped_direct_line_is_kept(self) -> None:
        lines = [
            "#CAP,1855136515,ST,Track,STOPPED,STOPPED",
            "#CAP,1856000000,BPM,120.0,120.0,1,1",
        ]
        self.assertTrue(
            serial_transport.serial_lines_show_transport_activity(lines, after_index=1)
        )


class ArmTimeTransportTests(unittest.TestCase):
    """The arm-time "is a take already playing?" answer is compared in device time.

    `ST,Track,…PLAYING` goes through the evidence ring, while `Transport stopped` is a directly
    printed logger line. Comparing their line positions lets a drained `PLAYING` record that landed
    after the stop decide that the transport is still running.
    """

    def test_logger_prefix_is_the_device_clock(self) -> None:
        from hitl.device_time import human_line_device_us

        self.assertEqual(1857780000, human_line_device_us("[1857.780] [INFO] Transport stopped"))
        self.assertIsNone(human_line_device_us("#CAP,1857780000,ST,Track,STOPPED,STOPPED"))

    def test_a_drained_playing_row_does_not_override_a_later_stop(self) -> None:
        from hitl import legacy_record_baseline as baseline

        lines = [
            "[1855.000] [INFO] Transport stopped",              # live, device 1855 s
            "#CAP,850583933,ST,Track,PAUSED,PLAYING",           # drained: device 850 s
            "#CAP,1860000000,ST,Track,PLAYING,STOPPED",         # live, newer state
        ]
        self.assertFalse(baseline._transport_running_before_record_arm(lines, after_index=0))

    def test_a_live_playing_after_the_stop_still_counts(self) -> None:
        from hitl import legacy_record_baseline as baseline

        lines = [
            "[1855.000] [INFO] Transport stopped",
            "#CAP,1856000000,ST,Track,PAUSED,PLAYING",
        ]
        self.assertTrue(baseline._transport_running_before_record_arm(lines, after_index=0))

    def test_position_comparison_remains_the_fallback_without_clocks(self) -> None:
        # Synthetic logs without device stamps keep the previous behaviour.
        from hitl import legacy_record_baseline as baseline

        lines = [
            "Transport stopped",
            "#CAP,ts,ST,Track,PAUSED,PLAYING",
        ]
        self.assertTrue(baseline._transport_running_before_record_arm(lines, after_index=0))


if __name__ == "__main__":
    unittest.main()
