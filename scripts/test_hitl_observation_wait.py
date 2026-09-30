#!/usr/bin/env python3
"""Tests for the observation-wait facade (task 3.3 of `response-driven-hitl-acceptance`).

This is the wait a scenario may use without touching the serial or verify layers: it waits for rows the device
emits on its own, presses nothing, and parses nothing. The clauses pinned here:

* a marker that is already there is seen at once, and one that never arrives ends at the bound;
* the caller's row source is re-read every pass, so a marker that lands later is noticed;
* the ring-flush callback runs on its own cadence, not on every pass;
* cancellation stops the wait at once;
* time and sleep are injected, so none of this waits on the wall clock.

Run: .venv/bin/python scripts/test_hitl_observation_wait.py
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.actions.cancellation import CancelScope  # noqa: E402
from hitl.actions.observation import await_markers  # noqa: E402


class FakeClock:
    """A monotonic clock the test advances; `sleep` only moves it."""

    def __init__(self) -> None:
        self.t = 50.0
        self.slept: list[float] = []

    def now(self) -> float:
        return self.t

    def sleep(self, seconds: float) -> None:
        self.slept.append(seconds)
        self.t += seconds


class AwaitMarkersTests(unittest.TestCase):
    def test_a_marker_that_is_already_there_is_seen_on_the_first_pass(self):
        clock = FakeClock()
        self.assertTrue(await_markers(lambda: ["#CAP,1,ST,Track,STOPPED,PLAYING"],
                                      ("STOPPED,PLAYING",), timeout_s=5.0, now=clock.now, sleep=clock.sleep))
        self.assertEqual([], clock.slept, "no wait was needed")

    def test_a_marker_that_never_arrives_ends_at_the_bound(self):
        clock = FakeClock()
        found = await_markers(lambda: ["nothing useful"], ("never",), timeout_s=0.2, poll_s=0.05,
                              now=clock.now, sleep=clock.sleep)
        self.assertFalse(found)
        self.assertGreaterEqual(clock.t - 50.0, 0.2)
        self.assertLessEqual(clock.t - 50.0, 0.25, "a bounded wait may overshoot by at most one poll slice")

    def test_the_row_source_is_re_read_every_pass(self):
        clock = FakeClock()
        rows = ["first"]

        def read() -> list[str]:
            return list(rows)

        def sleep(seconds: float) -> None:
            clock.sleep(seconds)
            rows.append("Reentry released slot=3")      # lands between two passes

        self.assertTrue(await_markers(read, ("Reentry released slot=3",), timeout_s=5.0, poll_s=0.05,
                                      now=clock.now, sleep=sleep))
        self.assertEqual(1, len(clock.slept))

    def test_the_flush_callback_runs_on_its_own_cadence(self):
        clock = FakeClock()
        drains: list[float] = []
        await_markers(lambda: [], ("never",), timeout_s=1.0, poll_s=0.1, drain_every_s=0.5,
                      on_poll=lambda: drains.append(clock.t), now=clock.now, sleep=clock.sleep)
        self.assertEqual(1, len(drains), "one flush inside the bound, not one per pass")
        self.assertAlmostEqual(50.5, drains[0])
        self.assertGreaterEqual(len(clock.slept), 9, "the wait still polled every slice")

    def test_cancellation_stops_the_wait_at_once(self):
        clock = FakeClock()
        scope = CancelScope(name="leg")
        scope.cancel("managed run watchdog 1.0s")
        found = await_markers(lambda: [], ("never",), timeout_s=30.0, poll_s=1.0,
                             cancel=lambda: scope.cancelled, now=clock.now, sleep=clock.sleep)
        self.assertFalse(found)
        self.assertEqual([], clock.slept, "a cancelled run does not sleep out its bound")

    def test_an_uncancelled_scope_waits_normally(self):
        clock = FakeClock()
        scope = CancelScope(name="leg")
        self.assertTrue(await_markers(lambda: ["PERS,result,ok"], ("PERS,result,",), timeout_s=1.0,
                                      cancel=lambda: scope.cancelled, now=clock.now, sleep=clock.sleep))
        self.assertTrue(scope.reason is None and not scope.cancelled)

    def test_a_zero_bound_still_reads_the_rows_once(self):
        clock = FakeClock()
        self.assertTrue(await_markers(lambda: ["here"], ("here",), timeout_s=0.0,
                                      now=clock.now, sleep=clock.sleep))
        self.assertFalse(await_markers(lambda: ["here"], ("absent",), timeout_s=0.0,
                                       now=clock.now, sleep=clock.sleep))


if __name__ == "__main__":
    unittest.main(verbosity=2)
