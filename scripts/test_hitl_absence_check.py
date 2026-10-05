#!/usr/bin/env python3
"""Unit tests for scripts/hitl_absence_check.py."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

import hitl_absence_check as ac

CLEAN = "#CAP,9,RING,stats,attempts=5,ok=5,flush=1,tx=5,shed=0,tierA_evict=0,overflow=0,crit_rej=0,wipe=0,wipe_bytes=0"
LOSSY = "#CAP,9,RING,stats,attempts=5,ok=5,flush=1,tx=5,shed=56863,tierA_evict=3,overflow=0,crit_rej=0,wipe=0,wipe_bytes=0"


def verdict(lines, tag="DIAG,x,", tier_a=False):
    return ac.verdicts(ac.scan(lines, [tag]), tier_a_of=lambda _t: tier_a)[0][0]


class AbsenceTests(unittest.TestCase):
    def test_present_row_is_present_even_when_ring_lossy(self) -> None:
        self.assertEqual(verdict(["#CAP,1,DIAG,x,1", LOSSY]), "PRESENT")

    def test_absence_with_clean_stats_is_evidence(self) -> None:
        self.assertEqual(verdict(["#CAP,1,ST,a", CLEAN]), "ABSENT-EVIDENCE")

    def test_absence_with_shed_or_evicted_rows_is_not_evidence(self) -> None:
        self.assertEqual(verdict(["#CAP,1,ST,a", LOSSY]), "ABSENT-NOT-EVIDENCE")

    def test_absence_without_stats_row_is_not_evidence(self) -> None:
        self.assertEqual(verdict(["#CAP,1,ST,a"]), "ABSENT-NOT-EVIDENCE")

    def test_shed_notice_rows_count_even_if_stats_say_clean(self) -> None:
        self.assertEqual(verdict(["#CAP,1,RING,shed,1", CLEAN]), "ABSENT-NOT-EVIDENCE")

    def test_tier_a_tag_ignores_routine_shed_but_not_tier_a_eviction(self) -> None:
        shed_only = CLEAN.replace("shed=0", "shed=3")
        self.assertEqual(verdict(["#CAP,1,RING,shed,1", shed_only], tier_a=True), "ABSENT-EVIDENCE")
        self.assertEqual(verdict(["#CAP,1,ST,a", LOSSY], tier_a=True), "ABSENT-NOT-EVIDENCE")

    def test_tag_must_start_the_row_body(self) -> None:
        self.assertEqual(verdict(["#CAP,1,ST,DIAG,x,1", CLEAN]), "ABSENT-EVIDENCE")

    def test_cli_usage_error(self) -> None:
        self.assertEqual(ac.main([]), 2)


if __name__ == "__main__":
    unittest.main()
