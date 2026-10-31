#!/usr/bin/env python3
"""Tests for `profile_report.py` (DEC-239 host decoder): capture-only, never guesses."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import profile_report  # noqa: E402

BOOT = ("#CAP,1000,DIAG,profile_boot,source=1,cpu_hz=600000000,trust_us=6158278,samples=256,"
        "short_min_cycles=40,short_avg_cycles=42,long_min_cycles=90,long_avg_cycles=95,ids=15,ring=256,depth=16")
CLEAN_STATS = "#CAP,9000,RING,stats,attempts=10,ok=10,shed=3,tierA_evict=0,overflow=0,crit_rej=0,wipe=0"
LOSSY_STATS = "#CAP,9000,RING,stats,attempts=10,ok=8,shed=3,tierA_evict=2,overflow=0,crit_rej=0,wipe=0"


def stat(ident: int, name: str, calls: int) -> str:
    return (f"#CAP,2000,DIAG,profile_stat,{ident},{name},calls={calls},avg_us=10,avg_self_us=5,max_us=30,"
            f"last_us=9,over=0,ref=0,order=0,coarse=0,last_result=255")


def ring(seq: int, name: str = "ViewPrewarm_unit", parent: str = "IdleOwner_unit", dur: int = 17000) -> str:
    return (f"#CAP,3000,DIAG,profile_ring,{seq},t_us={seq * 100},id=5,name={name},parent={parent},"
            f"dur_us={dur},trk=7,slot=3,result=1,src=0")


def ring_end(count: int, resume: int, start: int, end: int, gap: int = 0, dropped: int = 0) -> str:
    return (f"#CAP,3100,DIAG,profile_ring,end,count={count},dropped={dropped},resume={resume},"
            f"range_start={start},range_end={end},gap={gap},poisoned=0")


STATS_END = "#CAP,2100,DIAG,profile_stat,end,count={n},resume={r},multi_batch=0,poisoned=0,depth_overflow=0,isr_refused=0"


class ProfileReportTests(unittest.TestCase):
    def test_a_complete_capture_decodes_table_timeline_and_parent(self):
        lines = [BOOT, stat(2, "RecordStart_edge", 1), stat(5, "ViewPrewarm_unit", 38),
                 STATS_END.format(n=2, r=0), ring(1), ring(2), ring_end(2, 0, 1, 2), CLEAN_STATS]
        report = profile_report.decode(lines)
        self.assertEqual(1, report["boot"]["source"])
        self.assertTrue(report["aggregate"]["complete"])
        self.assertEqual(38, report["aggregate"]["rows"][5]["calls"])
        self.assertEqual([1, 2], [r["seq"] for r in report["timeline"]])
        self.assertEqual("IdleOwner_unit", report["timeline"][0]["parent"])
        self.assertFalse(report["capture_lossy"])
        self.assertEqual([], report["gaps"])
        text = profile_report.render(report)
        self.assertIn("immediate enclosing profiled scope only", text)
        self.assertIn("capture ring: no loss", text)

    def test_a_multi_batch_dump_is_merged_and_a_cut_off_dump_is_incomplete(self):
        lines = [stat(1, "SelfTest", 0), STATS_END.format(n=1, r=1), stat(2, "RecordStart_edge", 4),
                 STATS_END.format(n=1, r=0), stat(1, "SelfTest", 9)]  # a later dump never finished
        report = profile_report.decode(lines)
        self.assertFalse(report["aggregate"]["complete"])
        self.assertEqual({1}, set(report["aggregate"]["rows"]))
        self.assertEqual(1, report["dumps_completed"])
        self.assertIn("not measured zeros", profile_report.render(report))

    def test_tier_a_eviction_marks_the_output_incomplete(self):
        lines = [BOOT, ring(1), ring(3), ring_end(2, 0, 1, 3), LOSSY_STATS]
        report = profile_report.decode(lines)
        self.assertTrue(report["capture_lossy"])
        self.assertEqual(1, report["unexplained_missing"])
        text = profile_report.render(report)
        self.assertIn("LOST ROWS", text)
        self.assertIn("NOT a complete breach history", text)

    def test_a_seq_gap_from_overwrite_is_attributed_to_the_device(self):
        lines = [ring(5), ring(9), ring_end(2, 0, 5, 9, gap=3, dropped=4), CLEAN_STATS]
        report = profile_report.decode(lines)
        self.assertEqual([{"after": 5, "before": 9, "missing": 3}], report["gaps"])
        self.assertEqual(3, report["overwritten_before_read"])
        self.assertEqual(0, report["unexplained_missing"])
        self.assertEqual(4, report["dropped"])

    def test_a_capture_without_profile_rows_is_unavailable_not_zero(self):
        report = profile_report.decode(["#CAP,5,DIAG,midi_gap,100,1", CLEAN_STATS])
        self.assertIsNone(report["boot"])
        self.assertIsNone(report["aggregate"])
        text = profile_report.render(report)
        self.assertIn("no profile_boot row", text)
        self.assertIn("aggregate: unavailable", text)
        self.assertIn("breaches: none decoded", text)

    def test_no_ring_stats_means_absence_is_not_evidence(self):
        report = profile_report.decode([BOOT])
        self.assertTrue(report["capture_lossy"])
        self.assertIn("absence of profile rows is not evidence", profile_report.render(report))

    def test_poisoned_terminator_and_refusals_are_reported(self):
        lines = [STATS_END.format(n=0, r=0).replace("poisoned=0", "poisoned=1"),
                 "#CAP,7,DIAG,profile_cmd,refused,depth=1"]
        report = profile_report.decode(lines)
        self.assertTrue(report["poisoned"])
        self.assertEqual(1, report["refused_commands"])
        text = profile_report.render(report)
        self.assertIn("poisoned", text)
        self.assertIn("refused profile commands: 1", text)

    def test_duplicate_ring_rows_are_counted_once(self):
        report = profile_report.decode([ring(1), ring(1), ring_end(1, 0, 1, 1), CLEAN_STATS])
        self.assertEqual(1, len(report["timeline"]))


if __name__ == "__main__":
    unittest.main()
