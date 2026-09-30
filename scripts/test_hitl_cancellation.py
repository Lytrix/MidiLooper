#!/usr/bin/env python3
"""Tests for bounded cancellation in the action layer (task 3.2 of `response-driven-hitl-acceptance`).

The clauses this pins, each on its own evidence:

* **Cancellation suppresses every subsequent test edge.** Once the run is cancelled the guarded output refuses
  to send, counts the refusal and raises `RunCancelled`, so a press helper cannot put one more gesture on the
  wire after the abort.
* **Held notes are released.** A run stopped between a `note_on` and its `note_off` leaves the device sounding
  a note; the guard releases exactly what it was holding, plus one channel-mode silence per channel it touched.
* **The teardown is not a test gesture, and is counted apart.** Stopping the transport and draining still has
  to happen on an abandoned run, so it runs in an explicitly permitted phase; the report can then say that
  nothing but teardown went out.
* **Cancellation is bounded and observable.** `check()` raises with the reason, `cancellable_sleep` stops
  early, `wait_for(cancel=…)` stops waiting, and the run in progress is registered so the watchdog in
  `managed_capture` can reach a scope it does not own.
* **First reason wins.** A watchdog and a caught interrupt racing must not leave two causes.

Run: .venv/bin/python scripts/test_hitl_cancellation.py
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

import mido  # noqa: E402

from hitl.actions import slots  # noqa: E402
from hitl.actions.cancellation import (  # noqa: E402
    CancelScope,
    GuardedMidiOut,
    RunCancelled,
    active_cancel_scope,
    begin_run,
    cancel_active_run,
    cancellable_sleep,
    end_run,
)
from hitl.actions.correlation import CorrelatedGesture, Stage, StageRules, Wire  # noqa: E402
from hitl.config import HitlConfig  # noqa: E402
from hitl.context import ActionContext  # noqa: E402
from hitl.session import HitlSession  # noqa: E402


class FakePort:
    """A `mido`-shaped output that records what actually reached the wire."""

    def __init__(self) -> None:
        self.sent: list = []
        self.closed = False

    def send(self, message) -> None:
        self.sent.append(message)

    def close(self) -> None:
        self.closed = True

    def kinds(self) -> list[str]:
        return [message.type for message in self.sent]


class FakeClock:
    """A monotonic clock the test advances; sleeping only moves it."""

    def __init__(self) -> None:
        self.t = 100.0
        self.slept: list[float] = []

    def now(self) -> float:
        return self.t

    def sleep(self, seconds: float) -> None:
        self.slept.append(seconds)
        self.t += seconds


def action_context(port) -> ActionContext:
    config = HitlConfig(
        preset="base",
        scenario_ids=("record_overdub",),
        track_number=5,
        loop_slot=2,
        midi_channel=5,
        record_bars=2,
        overdub_bars=2,
        midi_out_name="Teensy",
        midi_in_name="Teensy",
        serial_port=None,
        verify_serial_log=None,
        verify_only=False,
        out_dir=Path("captures"),
        press_ms=120,
        phase_wait_ms=10,
    )
    return ActionContext(session=HitlSession(out_port=port, in_port=None), config=config)


class CancelScopeTests(unittest.TestCase):
    def test_the_first_reason_wins_and_a_later_call_cannot_rewrite_it(self):
        scope = CancelScope(name="leg")
        self.assertTrue(scope.cancel("managed run watchdog 360.0s"))
        self.assertFalse(scope.cancel("keyboard interrupt"))
        self.assertEqual("managed run watchdog 360.0s", scope.reason)
        self.assertTrue(scope.cancelled)

    def test_check_raises_with_the_reason(self):
        scope = CancelScope(name="leg")
        scope.check()  # a run that is not cancelled passes the boundary
        scope.cancel("watchdog")
        with self.assertRaises(RunCancelled) as caught:
            scope.check()
        self.assertEqual("watchdog", caught.exception.reason)
        self.assertEqual("watchdog", str(caught.exception))


class RunRegistryTests(unittest.TestCase):
    def tearDown(self) -> None:
        end_run()

    def test_the_watchdog_can_reach_a_scope_the_driver_owns(self):
        scope = begin_run(name="accepted-sequence-od")
        self.assertIs(scope, active_cancel_scope())
        self.assertTrue(cancel_active_run("managed run watchdog 12.0s"))
        self.assertEqual("managed run watchdog 12.0s", active_cancel_scope().reason)
        self.assertFalse(cancel_active_run("second cause"), "an already-cancelled run keeps its first cause")

    def test_end_run_unregisters_only_the_run_it_is_given(self):
        first = begin_run(name="first")
        end_run(CancelScope(name="someone else"))
        self.assertIs(first, active_cancel_scope())
        end_run(first)
        self.assertIsNone(active_cancel_scope())
        self.assertFalse(cancel_active_run("no run"), "no run registered: nothing to cancel")

    def test_a_second_run_cannot_start_over_a_live_one(self):
        begin_run(name="first")
        with self.assertRaises(RuntimeError):
            begin_run(name="second")
        cancel_active_run("watchdog")
        second = begin_run(name="second")
        self.assertIs(second, active_cancel_scope())


class GuardedMidiOutTests(unittest.TestCase):
    def test_every_test_edge_goes_through_the_guard_and_is_counted(self):
        port = FakePort()
        guard = GuardedMidiOut(port, scope=CancelScope(name="leg"))
        guard.send(mido.Message("note_on", channel=3, note=53, velocity=127))
        guard.send(mido.Message("note_off", channel=3, note=53, velocity=0))
        self.assertEqual(["note_on", "note_off"], port.kinds())
        self.assertEqual(2, guard.counters.edges)
        self.assertEqual((), guard.held_notes)
        self.assertEqual(1, guard.counters.notes_on)
        self.assertEqual(1, guard.counters.notes_off)

    def test_a_refused_send_reaches_the_wire_nowhere_and_raises(self):
        port = FakePort()
        guard = GuardedMidiOut(port, scope=CancelScope(name="leg"))
        guard.send(mido.Message("note_on", channel=3, note=53, velocity=127))
        guard.scope.cancel("watchdog")
        with self.assertRaises(RunCancelled):
            guard.send(mido.Message("note_on", channel=3, note=54, velocity=127))
        self.assertEqual(["note_on"], port.kinds(), "the refused edge never reached the wire")
        self.assertEqual(1, guard.counters.refused)
        self.assertEqual(1, guard.counters.edges, "a refused send is not an edge")

    def test_a_press_helper_cannot_send_after_the_abort(self):
        """The property the requirement names: no dependent gesture once the run is cancelled."""
        port = FakePort()
        guard = GuardedMidiOut(port, scope=CancelScope(name="leg"))
        ctx = action_context(guard)
        slots.press_slot_record(ctx, 4)
        guard.scope.cancel("watchdog")
        with self.assertRaises(RunCancelled):
            slots.press_slot_record(ctx, 4)
        self.assertEqual(["note_on", "note_off"], port.kinds(),
                         "the second press must leave nothing on the wire")
        self.assertEqual(1, guard.counters.refused)

    def test_the_teardown_phase_is_permitted_and_counted_apart(self):
        port = FakePort()
        guard = GuardedMidiOut(port, scope=CancelScope(name="leg"))
        guard.scope.cancel("watchdog")
        with self.assertRaises(RunCancelled):
            guard.send(mido.Message("note_on", channel=3, note=53, velocity=127))
        with guard.teardown():
            guard.send(mido.Message("stop"))
            self.assertTrue(guard.in_teardown())
        self.assertEqual(["stop"], port.kinds())
        self.assertEqual(1, guard.counters.teardown_edges)
        self.assertEqual(0, guard.counters.edges, "a teardown edge is not a test gesture")
        # ... and the permission ends with the phase again.
        with self.assertRaises(RunCancelled):
            guard.send(mido.Message("stop"))

    def test_teardown_call_returns_the_callers_value(self):
        guard = GuardedMidiOut(FakePort(), scope=CancelScope(name="leg"))
        guard.scope.cancel("watchdog")
        self.assertEqual("stopped", guard.teardown_call(lambda: "stopped"))

    def test_held_notes_are_released_exactly_and_a_second_call_releases_nothing(self):
        port = FakePort()
        guard = GuardedMidiOut(port, scope=CancelScope(name="leg"))
        guard.send(mido.Message("note_on", channel=3, note=53, velocity=100))
        guard.send(mido.Message("note_on", channel=3, note=55, velocity=100))
        guard.send(mido.Message("note_off", channel=3, note=55, velocity=0))
        self.assertEqual(((3, 53),), guard.held_notes)
        guard.scope.cancel("watchdog")
        sent = guard.release_held_notes()
        self.assertEqual(3, sent, "one note_off for the held note, plus All Sound Off and All Notes Off")
        self.assertEqual(["note_on", "note_on", "note_off", "note_off", "control_change", "control_change"],
                         port.kinds())
        self.assertEqual([123, 120], [message.control for message in port.sent
                                      if message.type == "control_change"][::-1])
        self.assertEqual((), guard.held_notes)
        self.assertEqual(3, guard.counters.release_edges)
        self.assertEqual(0, guard.release_held_notes())

    def test_a_release_survives_a_port_that_has_already_gone(self):
        class Dying(FakePort):
            def send(self, message) -> None:
                raise OSError("port closed")

        guard = GuardedMidiOut(Dying(), scope=CancelScope(name="leg"))
        guard.counters.notes_on = 1  # the run pressed a note before its port died
        guard._held.add((3, 53))
        self.assertEqual(0, guard.release_held_notes())

    def test_the_guard_records_itself_for_the_report(self):
        guard = GuardedMidiOut(FakePort(), scope=CancelScope(name="leg"), name="leg")
        guard.send(mido.Message("note_on", channel=3, note=53, velocity=100))
        guard.scope.cancel("managed run watchdog 360.0s")
        record = guard.as_dict()
        self.assertEqual("leg", record["name"])
        self.assertTrue(record["cancelled"])
        self.assertEqual("managed run watchdog 360.0s", record["cancel_reason"])
        self.assertEqual([[3, 53]], record["held_notes"])
        self.assertEqual(1, record["edges"])


    def test_owning_a_run_absorbs_the_cancel_and_releases_what_it_held(self):
        """The outer context a driver adopts: cancel in, nothing on the wire, notes released, run unregistered."""
        port = FakePort()
        scope = begin_run(scope=CancelScope(name="leg"))
        guard = GuardedMidiOut(port, scope=scope)
        with guard.run() as record:
            guard.send(mido.Message("note_on", channel=3, note=53, velocity=100))
            scope.cancel("managed run watchdog 1.0s")
            guard.send(mido.Message("note_on", channel=3, note=54, velocity=100))  # refused, absorbed below
        self.assertTrue(record.cancelled)
        self.assertEqual("managed run watchdog 1.0s", record.reason)
        self.assertEqual(3, record.released_notes)
        self.assertEqual(1, record.record["refused_after_cancel"])
        self.assertIsNone(active_cancel_scope(), "the run is unregistered on the way out")
        self.assertEqual(["note_on", "note_off", "control_change", "control_change"],
                         port.kinds(), "only the held note was released")

    def test_owning_a_run_does_not_swallow_another_exception(self):
        guard = GuardedMidiOut(FakePort(), scope=begin_run(name="leg"))
        with self.assertRaises(ValueError):
            with guard.run():
                raise ValueError("something else")
        self.assertIsNone(active_cancel_scope(), "even a failure unregisters the run")

    def test_a_run_that_completes_has_nothing_to_release(self):
        guard = GuardedMidiOut(FakePort(), scope=begin_run(name="leg"))
        with guard.run() as record:
            guard.send(mido.Message("note_on", channel=3, note=53, velocity=100))
            guard.send(mido.Message("note_off", channel=3, note=53, velocity=0))
        self.assertFalse(record.cancelled)
        self.assertEqual(2, record.released_notes, "the channel is silenced once even when nothing is held")
        self.assertEqual(2, record.record["edges"], "a note_on and its note_off, both counted as test edges")


class CancellableSleepTests(unittest.TestCase):
    def test_the_full_sleep_elapses_in_slices(self):
        clock = FakeClock()
        cancellable_sleep(0.2, scope=CancelScope(name="leg"), sleep=clock.sleep, now=clock.now)
        self.assertEqual(4, len(clock.slept), "the tail is its own slice")
        self.assertLessEqual(max(clock.slept), 0.05)
        self.assertAlmostEqual(0.2, sum(clock.slept))
        self.assertAlmostEqual(100.2, clock.t)

    def test_a_cancel_stops_the_sleep_almost_at_once(self):
        clock = FakeClock()
        scope = CancelScope(name="leg")

        def sleep(seconds: float) -> None:
            clock.sleep(seconds)
            scope.cancel("watchdog")

        with self.assertRaises(RunCancelled):
            cancellable_sleep(10.0, scope=scope, sleep=sleep, now=clock.now)
        self.assertEqual(1, len(clock.slept), "one slice, not the rest of the take")

    def test_no_scope_is_still_a_sleep(self):
        clock = FakeClock()
        cancellable_sleep(0.1, sleep=clock.sleep, now=clock.now)
        self.assertAlmostEqual(100.1, clock.t)


class WaitCancellationTests(unittest.TestCase):
    def _gesture(self):
        clock = FakeClock()
        gesture = CorrelatedGesture(
            name="seed",
            rules=StageRules(accept=(Wire("DISP,3,ARMED", ("#CAP,",)),)),
            now=clock.now,
            sleep=clock.sleep,
        )
        gesture.mark_sent(note=53, channel=16, kind="short", before_device_us=5_000_000)
        return clock, gesture

    def test_a_cancelled_wait_stops_at_once_and_is_not_met(self):
        clock, gesture = self._gesture()
        scope = CancelScope(name="leg")
        scope.cancel("watchdog")
        self.assertFalse(gesture.wait_for(Stage.ACCEPT, lambda: [], timeout_s=30.0, poll_s=1.0,
                                         cancel=lambda: scope.cancelled))
        self.assertEqual([], clock.slept, "a cancelled wait does not sleep out its bound")
        self.assertEqual("accept", gesture.blocking_stage(), "stopped is not met")

    def test_a_wait_that_is_not_cancelled_still_lands(self):
        clock, gesture = self._gesture()
        rows = ["#CAP,5200000,DISP,3,ARMED,0,0"]
        scope = CancelScope(name="leg")
        self.assertTrue(gesture.wait_for(Stage.ACCEPT, lambda: rows, timeout_s=1.0, poll_s=0.5,
                                         cancel=lambda: scope.cancelled))
        self.assertEqual([], clock.slept)


if __name__ == "__main__":
    unittest.main(verbosity=2)
