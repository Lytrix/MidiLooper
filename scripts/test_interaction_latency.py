#!/usr/bin/env python3
"""S4f.11 — the acceptance -> execution latency owner's own checks.

Every case is synthetic and pinned to the wire forms the device emits, because the measurement's whole job is to
read *rows*: a fixture that only looks like a row would let the owner report a number the device never produced.
The capture-derived readings live in the S4f.11 evidence, not here.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.interaction_latency import CLASSES, as_dict, measure  # noqa: E402


def by_key(report, key: str):
    return next(cls for cls in report.classes if cls.spec.key == key)


#: The od leg's own rows (`captures/s45-odleg1a-20261002-163640-od`), trimmed to the pairs under test.
OD_LEG = "\n".join([
    "#CAP,45139809,GS,15,36,0",
    "#CAP,45139912,ODUB,stage,manager_enter,0,4294967295,4294967295,ok",
    "#CAP,45140156,DIAG,reentry,admit,667,3,3,0",
    "#CAP,48004262,GS,15,54,0",
    "[48.182] [INFO] Loop 5: Switch gesture from slot 4",
    "#CAP,48012717,DIAG,reentry,bound,1770,3,3,0",
    "#CAP,48012769,DIAG,reentry_window_open,3,1770,2304",
    "#CAP,48012823,DIAG,stop_state,7,3,6,7,1770,48012823,6110,48007867,48010471,2604",
    "#CAP,48012840,DIAG,stop_state,7,3,7,5,1770,48012840,6110,48007867,48010471,2604",
    "#CAP,48649163,GS,15,36,0",
    "#CAP,48649488,DIAG,reentry,admit,2015,4,4,1",
    "#CAP,49606319,GS,15,54,0",
    "#CAP,49617613,DIAG,reentry,bound,2384,4,4,0",
    "#CAP,49617700,DIAG,stop_state,7,4,6,7,2384,49617700,6724,49606723,49609327,2604",
    "#CAP,5103979,RRQ,7,4,0,0,queued,none,none",
    "#CAP,6658424,RRQ,7,4,0,0,running,none,none",
    "#CAP,6776755,RRQ,7,4,0,0,terminal,none,committed",
]) + "\n"


class StopOverdubTests(unittest.TestCase):
    def test_the_binding_pairs_with_the_execution_at_or_after_it(self):
        report = measure(OD_LEG)
        cls = by_key(report, "stop_overdub")
        self.assertIsNone(cls.not_exercised)
        # Two bindings (A's 1770, B's 2384), each paired with the 6->7 entry that follows it.
        self.assertEqual([106, 87], [s.latency_us for s in cls.samples])
        self.assertEqual([1, 1], [s.resolution_us for s in cls.samples])

    def test_an_execution_before_the_acceptance_is_never_paired_with_it(self):
        """The stale-row defect that produced a false accepted-sequence verdict, as a fixture.

        The device's own interval 6 of an earlier (seed) interval sits before the binding: pairing it would
        report a *negative* latency for a stop that had not been accepted yet.
        """
        text = "\n".join([
            "#CAP,1000000,DIAG,stop_state,7,3,6,7,140,1000000,1,0,0,0",
            "#CAP,2000000,DIAG,reentry,bound,200,3,3,0",
            "#CAP,2000050,DIAG,stop_state,7,3,6,7,200,2000050,1,0,0,0",
        ]) + "\n"
        cls = by_key(measure(text), "stop_overdub")
        self.assertEqual([50], [s.latency_us for s in cls.samples])
        self.assertNotIn(1000000, [s.execution_us for s in cls.samples])

    def test_a_binding_with_no_execution_is_skipped_not_guessed(self):
        text = "#CAP,2000000,DIAG,reentry,bound,200,3,3,0\n"
        cls = by_key(measure(text), "stop_overdub")
        self.assertEqual([], cls.samples)
        self.assertIsNotNone(cls.not_exercised)

    def test_the_flush_time_is_not_the_execution_time(self):
        """`stop_state` carries the event's own device time; the `#CAP` prefix is when the ring emitted the row.

        Measured (`captures/s45-odleg1a-20261002-163640-od`): A's 6->7 entry carries `at_us` 48,012,823 us under a
        `#CAP` prefix of 48,182,318 us -- 169.5 ms later -- so pairing prefixes would report the flush delay as the
        stop's execution latency and flag a false budget exceedance against the microseconds bound.
        """
        text = "\n".join([
            "#CAP,48012717,DIAG,reentry,bound,1770,3,3,0",
            "#CAP,48182318,DIAG,stop_state,7,3,6,7,1770,48012823,6110,48007867,48010471,2604",
        ]) + "\n"
        cls = by_key(measure(text), "stop_overdub")
        self.assertEqual([106], [s.latency_us for s in cls.samples])
        self.assertEqual([48_012_823], [s.execution_us for s in cls.samples])
        self.assertEqual([], cls.exceeded)


class StartTests(unittest.TestCase):
    def test_the_press_row_is_the_acceptance_and_the_admission_the_execution(self):
        report = measure(OD_LEG)
        cls = by_key(report, "start")
        self.assertEqual([103, 325], [s.latency_us for s in cls.samples])
        self.assertEqual(["GS,15,36,0", "GS,15,36,0"], [s.acceptance_row for s in cls.samples])
        self.assertEqual(["ODUB,stage,manager_enter,0,4294967295,4294967295,ok",
                          "DIAG,reentry,admit,2015,4,4,1"],
                         [s.execution_row for s in cls.samples])

    def test_a_record_start_is_measured_by_its_own_admission_row(self):
        text = "\n".join(["#CAP,14090756,RECA,4,566",
                          "#CAP,13792498,GS,15,36,0"]) + "\n"
        cls = by_key(measure(text), "start")
        self.assertIsNone(cls.not_exercised, cls.not_exercised)

    def test_a_press_the_device_never_answered_is_not_a_sample(self):
        cls = by_key(measure("#CAP,100,GS,15,36,0\n"), "start")
        self.assertEqual([], cls.samples)
        self.assertIn("GS,<ctl0>,36,<press>", cls.not_exercised)


class SwitchTests(unittest.TestCase):
    def test_the_route_and_the_handoff_are_separate_samples(self):
        report = measure(OD_LEG)
        route = by_key(report, "switch")
        handoff = by_key(report, "switch_handoff")
        # The switch press at 48.004262 s: the route names itself at 48.182 s (printed, ms resolution) and the
        # successor is admitted at 48.649488 s.
        self.assertEqual([177_738], [s.latency_us for s in route.samples])
        self.assertEqual([1_000], [s.resolution_us for s in route.samples])
        self.assertEqual([645_226], [s.latency_us for s in handoff.samples])
        self.assertEqual([1], [s.resolution_us for s in handoff.samples])

    def test_a_slot_press_with_no_route_or_successor_is_a_named_gap(self):
        report = measure("#CAP,100,GS,15,57,0\n")
        self.assertIsNotNone(by_key(report, "switch").not_exercised)
        self.assertIsNotNone(by_key(report, "switch_handoff").not_exercised)

    def test_a_non_slot_press_is_not_a_switch(self):
        report = measure("#CAP,100,GS,15,67,0\n#CAP,200,DIAG,reentry,admit,10,4,4,1\n")
        self.assertIsNotNone(by_key(report, "switch_handoff").not_exercised)

    def test_a_slot_press_that_stopped_something_is_not_a_switch_handoff(self):
        """B's own stop press carries the **same note** as the switch: without a route row it must measure nothing.

        Measured (`captures/s45-odleg1a-20261002-163640-od`): the switch press `GS,15,54,0` at 48.004 s carries the
        route row and B's admission at 48.649 s, while B's stop press `GS,15,54,0` at 49.606 s prints
        `Loop 5: Stop Overdub` and admits nothing. Pairing by note alone would hand that stop the *next* interval's
        admission as its "handoff" -- the same stale-pairing defect the accepted-sequence oracle had.
        """
        text = "\n".join([
            "#CAP,49000000,GS,15,54,0",
            "[49.617] [INFO] Loop 5: Stop Overdub",
            "#CAP,50000000,DIAG,reentry,admit,300,4,4,1",
        ]) + "\n"
        report = measure(text)
        self.assertEqual([], by_key(report, "switch_handoff").samples)
        self.assertIsNotNone(by_key(report, "switch_handoff").not_exercised)


class UndoRedoTests(unittest.TestCase):
    def test_the_printed_clock_places_the_press_at_millisecond_resolution(self):
        """`MIDI: Undo` is a printed row: its `[<s>.<ms>]` prefix is the same device clock, coarser."""
        text = "\n".join([
            "[55.329] [INFO] MIDI: Undo (entries=3)",
            "[55.376] [DEBUG] Undo applied: kind=1 undo_count=6",
            "[55.376] [DEBUG] [TRACK] Overdub undone @ tick 14233",
            "[58.479] [INFO] MIDI: Redo (entries=1)",
            "[58.531] [DEBUG] Redo applied: kind=1 redo_count=0",
        ]) + "\n"
        cls = by_key(measure(text), "undo_redo")
        self.assertEqual([47_000, 52_000], [s.latency_us for s in cls.samples])
        self.assertEqual([1_000, 1_000], [s.resolution_us for s in cls.samples])
        self.assertEqual(["MIDI: Undo (entries=3)"], [s.acceptance_row for s in cls.samples[:1]])

    def test_an_undo_without_an_application_row_is_not_measured(self):
        cls = by_key(measure("[55.329] [INFO] MIDI: Undo (entries=3)\n"), "undo_redo")
        self.assertEqual([], cls.samples)
        self.assertIn("Undo applied", cls.not_exercised)


class LoadLoopTests(unittest.TestCase):
    def test_the_request_pairs_with_its_own_terminal_row(self):
        cls = by_key(measure(OD_LEG), "load_loop")
        self.assertEqual([1_672_776], [s.latency_us for s in cls.samples])
        self.assertEqual(["RRQ,7,4,0,0,queued,none,none"], [s.acceptance_row for s in cls.samples])

    def test_another_slots_terminal_row_does_not_close_this_request(self):
        text = "\n".join([
            "#CAP,5103979,RRQ,7,4,0,0,queued,none,none",
            "#CAP,6776755,RRQ,7,5,0,0,terminal,none,committed",
        ]) + "\n"
        cls = by_key(measure(text), "load_loop")
        self.assertEqual([], cls.samples)
        self.assertIsNotNone(cls.not_exercised)


class BudgetTests(unittest.TestCase):
    def test_exceeding_a_declared_bound_is_a_reported_event(self):
        text = "\n".join([
            "#CAP,2000000,DIAG,reentry,bound,200,3,3,0",
            "#CAP,2005000,DIAG,stop_state,7,3,6,7,200,2005000,1,0,0,0",
        ]) + "\n"
        report = measure(text)
        cls = by_key(report, "stop_overdub")
        self.assertEqual([1], [len(cls.exceeded)])
        self.assertEqual(1, len(report.events))
        self.assertIn("exceeds the declared bound", report.events[0])
        self.assertIn("1000", report.events[0])

    def test_a_class_without_a_numeric_bound_is_informational(self):
        report = measure(OD_LEG)
        start = by_key(report, "start")
        self.assertIsNone(start.spec.declared_bound_us)
        self.assertEqual([], start.exceeded)
        self.assertIn("informational", start.spec.bound_note)

    def test_the_matrix_records_every_class_and_its_declared_expectation(self):
        payload = as_dict(measure(OD_LEG, source="fixture"))
        self.assertEqual([spec.key for spec in CLASSES], [cls["key"] for cls in payload["classes"]])
        for cls in payload["classes"]:
            self.assertTrue(cls["declared"])
            self.assertTrue(cls["bound_note"])
        self.assertEqual("interaction_latency", payload["owner"])
        self.assertIn("Revision 17", payload["declared"])

    def test_an_unexercised_class_says_which_rows_it_looked_for(self):
        payload = as_dict(measure("#CAP,100,MO,128,7,50,0\n"))
        for cls in payload["classes"]:
            self.assertEqual([], cls["samples"])
            self.assertIsNone(cls["worst_us"])
            self.assertIn("no acceptance row matched", cls["not_exercised"])


if __name__ == "__main__":
    unittest.main()
