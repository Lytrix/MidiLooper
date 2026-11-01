#!/usr/bin/env python3
"""Tests for `parse_tick_routes.py` (capture-only, never reports a missing count as zero)."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import parse_tick_routes  # noqa: E402

CLEAN = "#CAP,9000,RING,stats,attempts=10,ok=10,shed=1,tierA_evict=0,overflow=0,crit_rej=0,wipe=0"
LOSSY = "#CAP,9000,RING,stats,attempts=10,ok=8,shed=1,tierA_evict=3,overflow=0,crit_rej=0,wipe=0"


def row(merged: int, view: int = 0, none: int = 0, miss_int: int = 0) -> str:
    return (f"#CAP,1000,DIAG,tick_route,view={view},bridge=0,paged=0,merged={merged},none={none},"
            f"miss_int={miss_int},miss_ext=0")


class ParseTickRoutesTests(unittest.TestCase):
    def test_rows_are_split_by_clock_interval(self):
        lines = [row(5), "[14.367] [INFO] External MIDI clock detected", row(100, view=2), row(200),
                 "[139.938] [INFO] MIDI clock lost, switching to internal at 120.1 BPM", row(50, none=3, miss_int=3),
                 CLEAN]
        report = parse_tick_routes.decode(lines)
        self.assertTrue(report["available"])
        self.assertTrue(report["complete"])
        self.assertEqual({"before_clock": 1, "external": 2, "internal": 1}, report["windows"])
        self.assertEqual(300, report["intervals"]["external"]["merged"])
        self.assertEqual(2, report["intervals"]["external"]["view"])
        self.assertEqual(3, report["intervals"]["internal"]["miss_int"])
        self.assertEqual(355, report["total"]["merged"])
        self.assertIn("merged share", parse_tick_routes.render(report))

    def test_no_rows_is_unavailable_not_zero(self):
        report = parse_tick_routes.decode(["#CAP,5,DIAG,midi_gap,100,1", CLEAN])
        self.assertFalse(report["available"])
        self.assertIn("unavailable", parse_tick_routes.render(report))

    def test_tier_a_loss_marks_totals_as_a_lower_bound(self):
        report = parse_tick_routes.decode([row(10), LOSSY])
        self.assertFalse(report["complete"])
        self.assertIn("lower bound", parse_tick_routes.render(report))

    def test_missing_ring_stats_is_reported(self):
        report = parse_tick_routes.decode([row(10)])
        self.assertIsNone(report["ring_loss"])
        self.assertIn("no RING,stats", parse_tick_routes.render(report))


if __name__ == "__main__":
    unittest.main()
