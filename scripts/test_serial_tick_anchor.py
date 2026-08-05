#!/usr/bin/env python3
"""Tests for serial transport tick anchoring."""

from __future__ import annotations

import unittest
from unittest import mock

from hitl.serial_tick_anchor import (
    TransportTickAnchor,
    parse_human_recording_started_tick,
    parse_latest_cap_bar_tick,
    parse_latest_cap_reca_tick,
    parse_record_start_tick,
    ticks_per_second_from_bpm,
    transport_record_start_monotonic,
)


class SerialTickAnchorTests(unittest.TestCase):
    def test_parse_reca_and_bar_ticks(self) -> None:
        lines = [
            "#CAP,1000,RECA,2,0",
            "#CAP,2000,BAR,384,0",
            "#CAP,3000,BAR,768,1",
        ]
        self.assertEqual(parse_latest_cap_reca_tick(lines), 0)
        self.assertEqual(parse_latest_cap_bar_tick(lines), 768)

    def test_ticks_per_second_at_120_bpm(self) -> None:
        self.assertAlmostEqual(ticks_per_second_from_bpm(120.0), 384.0)

    def test_anchor_extrapolates_from_bar(self) -> None:
        collector = mock.Mock()
        collector.snapshot.return_value = ["#CAP,1,BAR,0,0"]
        anchor = TransportTickAnchor.from_collector(collector, 120.0)
        with mock.patch("hitl.serial_tick_anchor.time.monotonic", return_value=anchor.anchor_mono + 0.25):
            self.assertGreaterEqual(anchor.estimated_tick(), 96)

    def test_anchor_refresh_updates_on_new_bar_line(self) -> None:
        lines = ["#CAP,1,BAR,0,0"]
        collector = mock.Mock()
        collector.snapshot.return_value = lines
        anchor = TransportTickAnchor.from_collector(collector, 120.0)
        self.assertEqual(anchor.anchor_tick, 0)

        lines.append("#CAP,2,BAR,768,1")
        collector.snapshot.return_value = lines
        anchor.refresh(collector)
        self.assertEqual(anchor.anchor_tick, 768)


    def test_transport_record_start_monotonic(self) -> None:
        sent = 1000.0
        start = transport_record_start_monotonic(sent, press_ms=120)
        self.assertAlmostEqual(start, sent + 0.42)

    def test_parse_human_recording_started_tick(self) -> None:
        lines = ["[24.356] [DEBUG] [TRACK] Recording started @ tick 0 (startLoopTick=0)"]
        self.assertEqual(parse_human_recording_started_tick(lines), 0)
        self.assertEqual(parse_record_start_tick(lines), 0)


if __name__ == "__main__":
    unittest.main()
