#!/usr/bin/env python3
"""Tests for scripts/hitl/record_stop_persist_target.py — the record-stop slot-identity correlator.

The device evidence this guards: a take on `(5,1)` whose record stop admits `slot:5:0` — the adjacent
slot. The correlator must name that as a failure at the exact boundary, and must not report a pass when
the admission never happened at all. Pure functions only: no device, no SD.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.record_stop_persist_target import (  # noqa: E402
    correlate_takes,
    parse_persist_admissions,
    parse_rid_lines,
)


def rid(event: str, take: int, track: int, ss: int, sp: int, cs: int, pt: int, ps: int,
        reason: str = "r", work: str = "L", chg: int = 0, us: int = 100) -> str:
    return (
        f"#CAP,{us},RID,{event},take={take},t={track},ss={ss},sp={sp},cs={cs},pt={pt},ps={ps},"
        f"r={reason},w={work},chg={chg}"
    )


class ParseTests(unittest.TestCase):
    def test_start_and_admit_join_on_the_take_id(self) -> None:
        chains = parse_rid_lines(
            [
                rid("start", 7, 5, 1, 1, 255, 255, 255, us=100),
                rid("admit", 7, 5, 1, 1, 1, 5, 1, us=200),
            ]
        )
        self.assertEqual([7], list(chains))
        chain = chains[7]
        self.assertEqual(1, chain.start_slot)
        self.assertEqual(1, chain.stop_slot)
        self.assertEqual(1, chain.commit_slot)
        self.assertEqual((5, 1), (chain.persist_track, chain.persist_slot))
        self.assertTrue(chain.observed_admission)

    def test_unrelated_lines_are_ignored(self) -> None:
        self.assertEqual({}, parse_rid_lines(["#CAP,1,PERS,work,1,0,0,LoopPersist,slot:5:1,x,ok"]))


class CorrelationTests(unittest.TestCase):
    def test_matching_chain_passes_and_states_its_non_claim(self) -> None:
        report = correlate_takes(
            [rid("start", 3, 5, 1, 1, 255, 255, 255), rid("admit", 3, 5, 1, 1, 1, 5, 1)],
            expected_slot=1,
            expected_track=5,
        )
        self.assertTrue(report["pass"])
        verdict = report["takes"][0]
        self.assertEqual(1, verdict["boundaries"]["recorded_slot_at_capture_start"])
        self.assertEqual(1, verdict["boundaries"]["admitted_persist_slot"])
        self.assertIn("not proof that the payload bytes", verdict["note"])

    def test_adjacent_slot_admission_is_the_demonstrated_failure(self) -> None:
        """The exact device shape: recorded slot 1, admission names slot 0."""
        report = correlate_takes(
            [
                rid("start", 4, 5, 1, 1, 255, 255, 255),
                rid("admit", 4, 5, 1, 1, 1, 5, 0, chg=1),
            ],
            expected_slot=1,
            expected_track=5,
        )
        self.assertFalse(report["pass"])
        problems = report["takes"][0]["problems"]
        self.assertIn("persist_slot_differs_from_recorded_slot", problems)
        self.assertIn("firmware_reports_slot_changed", problems)
        self.assertEqual(0, report["takes"][0]["boundaries"]["admitted_persist_slot"])

    def test_stop_slot_changing_mid_take_is_reported_at_its_own_boundary(self) -> None:
        report = correlate_takes(
            [rid("start", 5, 5, 2, 2, 255, 255, 255), rid("admit", 5, 5, 2, 1, 1, 5, 1)],
        )
        self.assertFalse(report["pass"])
        self.assertIn("stop_slot_differs_from_start_slot", report["takes"][0]["problems"])

    def test_commit_slot_diverging_from_the_stop_slot_is_reported(self) -> None:
        report = correlate_takes(
            [rid("start", 6, 5, 1, 1, 255, 255, 255), rid("admit", 6, 5, 1, 1, 0, 5, 1)],
        )
        self.assertFalse(report["pass"])
        self.assertIn("commit_slot_differs_from_stop_slot", report["takes"][0]["problems"])

    def test_a_missing_admission_never_passes(self) -> None:
        """No admission means no correlation: an absent redirect is not a clean take."""
        report = correlate_takes([rid("start", 8, 5, 1, 1, 255, 255, 255)], expected_slot=1)
        self.assertFalse(report["pass"])
        self.assertIn("persist_admission_not_observed", report["takes"][0]["problems"])

    def test_no_records_at_all_is_not_a_pass(self) -> None:
        report = correlate_takes([], expected_slot=1)
        self.assertFalse(report["pass"])
        self.assertIn("no RID record was captured", report["reason"])

    def test_a_take_on_the_wrong_pinned_target_fails(self) -> None:
        report = correlate_takes(
            [rid("start", 9, 5, 3, 3, 255, 255, 255), rid("admit", 9, 5, 3, 3, 3, 5, 3)],
            expected_slot=1,
            expected_track=5,
        )
        self.assertFalse(report["pass"])
        self.assertIn("record_start_slot_differs_from_pinned_target",
                      report["takes"][0]["problems"])


class PersistAdmissionTests(unittest.TestCase):
    def test_loop_persist_keys_are_readable(self) -> None:
        rows = parse_persist_admissions(
            [
                "#CAP,100,PERS,work,39,0,0,LoopPersist,slot:5:1,start,ok",
                "#CAP,200,PERS,work,50,0,0,SlotMeta,slot:5:0,start,ok",
            ]
        )
        self.assertEqual("slot:5:1", rows[0]["key"])
        self.assertEqual("LoopPersist", rows[0]["type"])
        self.assertEqual("slot:5:0", rows[1]["key"])


if __name__ == "__main__":
    unittest.main()
