#!/usr/bin/env python3
"""Tests for the correlated gesture waits of the composable action layer (task 3.1).

The clauses this pins, each on its own evidence:

* **send / recognize / accept / execute stay distinct.** A row that only proves execution cannot satisfy
  recognition or acceptance: the record shows which stage the device actually reached instead of collapsing
  into "something happened".
* **Nothing re-presses.** A stage that never arrives stays pending and is named; the wait sends no MIDI at all,
  and no helper here has a retry.
* **A foreign row cannot substitute.** Evidence must carry the gesture's own identity, so the wrong-slot
  completion row the historical runs produced is refused (the mutation restoring "accept any slot" fails this
  suite).
* **Drained history cannot substitute.** A row stamped before the device clock observed at the press is
  history, not this gesture's answer -- the rule `hitl.device_time` owns, applied where the wait reads it.
* **Time is injected.** The bounded wait is driven by the caller's `now`/`sleep`, so the tests never sleep.
* **A pending switch blocks follow-ups** and an unattributed `Playback started` cannot discharge it.
* **A row that answers differently is a contradiction, not silence.** The device reading a double tap where a
  short press was sent is `contradicted` (the same absence-versus-contradiction split the acceptance oracle
  uses), and the wait returns at once instead of reporting a plain timeout.
* **The press helpers own SEND.** The helper that put the note on the wire records the stage, with the note, the
  channel and the firmware's own classification name.

Run: .venv/bin/python scripts/test_hitl_correlated_actions.py
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.actions.correlation import (  # noqa: E402
    CorrelatedGesture,
    PendingSwitchGate,
    Stage,
    StageOutcome,
    StageRules,
    Wire,
)

from hitl.actions import buttons, slots  # noqa: E402
from hitl.config import HitlConfig  # noqa: E402
from hitl.context import ActionContext  # noqa: E402
from hitl.session import HitlSession  # noqa: E402


class FakeClock:
    """A monotonic clock the test advances; `sleep` only moves it, so no test waits on real time."""

    def __init__(self) -> None:
        self.t = 1000.0
        self.slept: list[float] = []

    def now(self) -> float:
        return self.t

    def sleep(self, seconds: float) -> None:
        self.slept.append(seconds)
        self.t += seconds


def record_gesture(clock: FakeClock, *, slot: int = 4, before_device_us: int = 5_000_000):
    """A slot-record gesture over slot 4 (firmware slot 3), with both of its wire families."""
    slot0 = slot - 1
    gesture = CorrelatedGesture(
        name="slot_record",
        rules=StageRules(
            recognize=(Wire(f"Button press: Loop {slot} (short)"),
                       Wire(f"Loop {slot}: Start Recording")),
            accept=(Wire("ARMED", (f"DISP,{slot0},",)),),
            execute=(Wire("RECORDING", (f"DISP,{slot0},",)),
                     Wire(f"Loop {slot}: Start Recording")),
            refusal=("record admission deferred", "Stop Recording"),
            # The same button read as another classification: a double press is OVERDUB, a long press is clear.
            conflict=(Wire(f"Button press: Loop {slot} (double)"),
                      Wire(f"Button press: Loop {slot} (long)"),
                      Wire(f"Button press: Loop {slot} (triple)")),
        ),
        now=clock.now,
        sleep=clock.sleep,
    )
    gesture.mark_sent(note=42, channel=16, kind="short", before_device_us=before_device_us)
    return gesture


class StageDistinctnessTests(unittest.TestCase):
    def test_stages_advance_only_in_order_and_are_recorded_separately(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        rows = [
            "#CAP,5100000,DISP,3,RECORDING,0,0",          # execute-only evidence: cannot skip ahead
            "MIDI Encoder: Long press - exited edit mode",
        ]
        for line in rows:
            gesture.consider(line)
        self.assertEqual(Stage.RECOGNIZE, gesture.pending_stage(),
                         "a row that only proves execution must not satisfy recognition")
        self.assertEqual(StageOutcome.PENDING, gesture.outcomes.get(Stage.EXECUTE, StageOutcome.PENDING))

        gesture.consider("Button press: Loop 4 (short)")
        self.assertEqual(Stage.ACCEPT, gesture.pending_stage())
        gesture.consider("#CAP,5200000,DISP,3,ARMED,0,0")
        self.assertEqual(Stage.EXECUTE, gesture.pending_stage())
        gesture.consider("#CAP,5300000,DISP,3,RECORDING,0,0")
        self.assertIsNone(gesture.pending_stage())
        snapshot = gesture.snapshot()
        self.assertEqual({"send": "met", "recognize": "met", "accept": "met", "execute": "met"},
                         snapshot["stages"])
        self.assertEqual("short note=42 channel=16", snapshot["evidence"]["send"])

    def test_a_foreign_slot_cannot_satisfy_a_stage(self):
        """The historical wrong-slot completion: another slot's row is not this gesture's evidence."""
        clock = FakeClock()
        gesture = record_gesture(clock, slot=4)
        gesture.consider("Button press: Loop 4 (short)")
        self.assertIsNone(gesture.consider("#CAP,5200000,DISP,2,ARMED,0,0"),
                          "slot 3's ARMED row cannot satisfy slot 4's acceptance")
        self.assertEqual(Stage.ACCEPT, gesture.pending_stage())
        self.assertIsNone(gesture.consider("Loop 5: Start Recording"))
        self.assertEqual(Stage.ACCEPT, gesture.pending_stage())

    def test_drained_history_cannot_satisfy_a_stage(self):
        clock = FakeClock()
        gesture = record_gesture(clock, before_device_us=9_000_000)
        gesture.consider("Button press: Loop 4 (short)")
        # Stamped before the device clock observed at the press: the ring flushed it late, it is history.
        self.assertIsNone(gesture.consider("#CAP,8000000,DISP,3,ARMED,0,0"))
        self.assertEqual(Stage.ACCEPT, gesture.pending_stage())
        self.assertEqual(Stage.ACCEPT, gesture.consider("#CAP,9100000,DISP,3,ARMED,0,0"))

    def test_a_refusal_is_its_own_outcome_not_a_missing_stage(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        gesture.consider("Button press: Loop 4 (short)")
        gesture.consider("[33.100] [INFO] [TRACK] record admission deferred while a stop obligation is live")
        self.assertEqual(StageOutcome.REFUSED, gesture.outcomes.get(Stage.ACCEPT))
        self.assertIn("deferred", gesture.evidence[Stage.ACCEPT])


class BoundedWaitTests(unittest.TestCase):
    def test_the_wait_uses_the_injected_clock_and_returns_when_the_stage_lands(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        rows: list[str] = []
        # Nothing has answered yet: the bound is spent on the injected clock (two 0.5 s polls), and the stage
        # stays pending rather than being satisfied by the passage of time.
        self.assertFalse(gesture.wait_for(Stage.RECOGNIZE, lambda: list(rows), timeout_s=1.0, poll_s=0.5))
        self.assertEqual([0.5, 0.5], clock.slept, "the bound is polled on the injected clock, not on real time")

        # The device's own row lands: the same call now returns on its first pass, without sleeping again.
        rows.append("Button press: Loop 4 (short)")
        self.assertTrue(gesture.wait_for(Stage.RECOGNIZE, lambda: list(rows), timeout_s=1.0, poll_s=0.5))
        self.assertEqual([0.5, 0.5], clock.slept, "a satisfied wait must not sleep at all")

    def test_a_stage_that_never_arrives_is_named_and_nothing_is_re_pressed(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        pressed: list[str] = []          # the wait API has no send path at all; record it explicitly
        met = gesture.wait_for(Stage.RECOGNIZE, lambda: ["[1.0] [INFO] something else"], timeout_s=1.0,
                              poll_s=0.25)
        self.assertFalse(met)
        self.assertEqual(Stage.RECOGNIZE, gesture.pending_stage())
        self.assertEqual(StageOutcome.PENDING, gesture.outcomes.get(Stage.RECOGNIZE, StageOutcome.PENDING))
        self.assertEqual("recognize", gesture.blocking_stage())
        self.assertEqual([], pressed)
        self.assertGreaterEqual(len(clock.slept), 4, "the bound is polled on the injected clock, not on real time")

    def test_a_refused_stage_returns_immediately(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        gesture.consider("Button press: Loop 4 (short)")
        rows = ["[33.100] [INFO] [TRACK] record admission deferred while a stop obligation is live"]
        self.assertFalse(gesture.wait_for(Stage.ACCEPT, lambda: rows, timeout_s=30.0, poll_s=1.0))
        self.assertEqual([], clock.slept, "a refusal must not wait out the bound")
        self.assertEqual("accept", gesture.blocking_stage())

    def test_time_is_injected_not_read_from_the_wall_clock(self):
        """With `now`/`sleep` injected the wait is fully deterministic; a real sleep would be caught here."""
        clock = FakeClock()
        gesture = record_gesture(clock)
        real_sleeps: list[float] = []
        import time as _time

        original = _time.sleep
        try:
            _time.sleep = lambda seconds: real_sleeps.append(seconds)  # type: ignore[assignment]
            gesture.wait_for(Stage.EXECUTE, lambda: [], timeout_s=0.6, poll_s=0.3)
        finally:
            _time.sleep = original
        self.assertEqual([], real_sleeps, "the wait must never call the real sleep when one is injected")


class PendingSwitchGateTests(unittest.TestCase):
    def test_a_queued_switch_blocks_followups_until_its_own_release_row(self):
        gate = PendingSwitchGate(target="Loop 5:")
        lines = [
            "Button press: Loop 5 (short)",
            "Loop 5: Queued playback switch",
        ]
        self.assertEqual("pending", gate.state(lines))
        self.assertTrue(gate.blocked(lines))

        lines.append("Loop 5: Reaffirmed playback switch")
        self.assertTrue(gate.blocked(lines), "the reaffirm is the second countdown, not the release")

        lines.append("Loop 5: Switch gesture from slot 4")
        self.assertEqual("released", gate.state(lines))
        self.assertFalse(gate.blocked(lines))

    def test_an_unattributed_playback_row_cannot_discharge_the_gate(self):
        # The device's only playback-start row is `[TRACK] Playback started @ tick <n>`: no slot, no track. It is
        # in the gate's `released` vocabulary *and* still cannot release, because attribution refuses it first --
        # it may belong to another track, or to a transport start that predates this switch.
        gate = PendingSwitchGate(target="Loop 5:")
        self.assertIn("Playback started", gate.released)
        lines = [
            "Loop 5: Queued playback switch",
            "[27.100] [DEBUG] [TRACK] Playback started @ tick 1536",
        ]
        self.assertTrue(gate.blocked(lines))
        self.assertEqual("pending", gate.state(lines))

    def test_another_slots_switch_does_not_block_this_one(self):
        gate = PendingSwitchGate(target="Loop 5:")
        lines = ["Loop 4: Queued playback switch"]
        self.assertEqual("clear", gate.state(lines))
        self.assertFalse(gate.blocked(lines))



    def test_a_row_stamped_exactly_at_the_floor_is_history(self):
        """The floor is a stamp the host had already read, so a row carrying it cannot be the receipt."""
        clock = FakeClock()
        gesture = record_gesture(clock, before_device_us=8_000_000)
        gesture.consider("Button press: Loop 4 (short)")
        self.assertIsNone(gesture.consider("#CAP,8000000,DISP,3,ARMED,0,0"))
        self.assertEqual(Stage.ACCEPT, gesture.pending_stage())
        self.assertEqual(Stage.ACCEPT, gesture.consider("#CAP,8000001,DISP,3,ARMED,0,0"))


class StageCoverageTests(unittest.TestCase):
    """Which stages a gesture waits for at all -- `StageRules`' own contract, made explicit."""

    def test_a_stage_with_no_rules_is_not_awaited_and_the_next_one_is(self):
        """A receipt-only wait declares `accept`; recognition is simply not part of that gesture's record."""
        clock = FakeClock()
        gesture = CorrelatedGesture(
            name="receipt_only",
            rules=StageRules(accept=(Wire("ARMED", ("DISP,3,",)),)),
            now=clock.now,
            sleep=clock.sleep,
        )
        gesture.mark_sent(note=53, channel=16, kind="short", before_device_us=5_000_000)
        self.assertEqual(Stage.ACCEPT, gesture.pending_stage())
        self.assertEqual(Stage.ACCEPT, gesture.consider("#CAP,5200000,DISP,3,ARMED,0,0"))
        self.assertIsNone(gesture.pending_stage())
        self.assertEqual({"send": "met", "recognize": "pending", "accept": "met", "execute": "pending"},
                         gesture.snapshot()["stages"])

    def test_a_state_wait_that_never_sent_does_not_await_a_send(self):
        """The already-armed path: the press was an earlier gesture, so nothing here claims to have sent."""
        clock = FakeClock()
        gesture = CorrelatedGesture(
            name="already_armed",
            rules=StageRules(execute=(Wire("Recording started"),)),
            now=clock.now,
            sleep=clock.sleep,
        )
        self.assertEqual(Stage.EXECUTE, gesture.pending_stage())
        self.assertEqual({}, gesture.send)
        self.assertEqual("pending", gesture.snapshot()["stages"]["send"],
                         "nothing was sent, and the record says so")


    def test_a_stale_human_row_cannot_satisfy_a_stage(self):
        """Both families carry the same device clock, so the floor applies to a directly printed row too."""
        clock = FakeClock()
        gesture = CorrelatedGesture(
            name="seed_arm",
            rules=StageRules(accept=(Wire("Loop 4: Start Recording"),)),
            now=clock.now,
            sleep=clock.sleep,
        )
        gesture.mark_sent(note=53, channel=16, kind="short", before_device_us=11_000_000)
        self.assertIsNone(gesture.consider("[11.000] [TRACK] Loop 4: Start Recording"),
                          "a row stamped at the floor is history, whichever family it belongs to")
        self.assertIsNone(gesture.consider("[10.500] [TRACK] Loop 4: Start Recording"))
        self.assertEqual(Stage.ACCEPT, gesture.consider("[11.020] [TRACK] Loop 4: Start Recording"))


class StageContradictionTests(unittest.TestCase):
    """A row that answers *differently* is a contradiction, and the absence/contradiction split is the point."""

    def test_a_row_that_names_another_classification_is_a_contradiction(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        # The device read a double tap where the host sent one short press: not silence, not this gesture.
        self.assertEqual(Stage.RECOGNIZE, gesture.consider("Button press: Loop 4 (double)"))
        self.assertEqual(StageOutcome.CONTRADICTED, gesture.outcomes[Stage.RECOGNIZE])
        self.assertEqual(Stage.RECOGNIZE, gesture.pending_stage(), "a contradiction is not a met stage")
        self.assertEqual("recognize", gesture.contradicted())
        self.assertEqual("recognize", gesture.blocking_stage())

    def test_a_contradiction_ends_the_wait_immediately(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        rows = ["Button press: Loop 4 (long)"]
        self.assertFalse(gesture.wait_for(Stage.RECOGNIZE, lambda: rows, timeout_s=30.0, poll_s=1.0))
        self.assertEqual([], clock.slept, "a contradiction must not wait out the bound")

    def test_another_loops_press_is_not_a_contradiction_of_this_one(self):
        clock = FakeClock()
        gesture = record_gesture(clock)
        self.assertIsNone(gesture.consider("Button press: Loop 5 (double)"))
        self.assertEqual(StageOutcome.PENDING, gesture.outcomes.get(Stage.RECOGNIZE, StageOutcome.PENDING))
        self.assertIsNone(gesture.contradicted())


class PressRecordsSendTests(unittest.TestCase):
    """The SEND stage belongs to the press helper that put the note on the wire, not to the flow calling it."""

    def setUp(self):
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
        session = HitlSession(out_port=MagicMock(), in_port=MagicMock())
        self.ctx = ActionContext(session=session, config=config)

    def test_a_slot_record_press_records_its_own_send(self):
        clock = FakeClock()
        gesture = CorrelatedGesture(name="slot_record", now=clock.now, sleep=clock.sleep)
        with patch("hitl.actions.slots._send_short_press") as send:
            slots.press_slot_record(self.ctx, 4, correlate=gesture, before_device_us=5_000_000)
        send.assert_called_once()
        self.assertTrue(gesture.met(Stage.SEND), "the press helper met SEND")
        self.assertFalse(gesture.awaited(Stage.RECOGNIZE), "no rules declared: nothing later is awaited")
        self.assertIsNone(gesture.pending_stage())
        self.assertEqual({"note": 53, "channel": 16, "kind": "short", "before_device_us": 5_000_000},
                         gesture.send)

    def test_the_classification_the_host_sent_is_named_from_the_firmware_vocabulary(self):
        clock = FakeClock()
        overdub = CorrelatedGesture(name="slot_overdub", now=clock.now, sleep=clock.sleep)
        clear = CorrelatedGesture(name="slot_clear", now=clock.now, sleep=clock.sleep)
        with patch("hitl.actions.slots._send_multi_short_press"), \
                patch("hitl.actions.slots._send_long_press"):
            slots.press_slot_overdub(self.ctx, 4, correlate=overdub)
            slots.press_slot_clear(self.ctx, 4, correlate=clear)
        self.assertEqual("double", overdub.send["kind"])
        self.assertEqual("long", clear.send["kind"])

    def test_a_press_without_a_correlation_stays_fire_and_forget(self):
        with patch("hitl.actions.buttons._send_short_press") as send:
            buttons.press_record(self.ctx)          # no gesture passed: no error, no record to make
        send.assert_called_once()

    def test_the_press_and_its_own_rows_complete_the_four_stage_chain(self):
        clock = FakeClock()
        gesture = CorrelatedGesture(name="slot_record", rules=record_gesture(clock).rules,
                                    now=clock.now, sleep=clock.sleep)
        with patch("hitl.actions.slots._send_short_press"):
            slots.press_slot_record(self.ctx, 4, correlate=gesture, before_device_us=5_000_000)
        rows = [
            "Button press: Loop 4 (short)",
            "Loop 4: Start Recording",
            "#CAP,5200000,DISP,3,ARMED,0,0",
            "#CAP,5300000,DISP,3,RECORDING,0,0",
        ]
        self.assertTrue(gesture.wait_for(Stage.EXECUTE, lambda: rows, timeout_s=0.5, poll_s=0.1))
        self.assertEqual({"send": "met", "recognize": "met", "accept": "met", "execute": "met"},
                         gesture.snapshot()["stages"])
        self.assertEqual([], clock.slept, "evidence already present must not need a poll")

    def test_a_foreign_slots_armed_row_cannot_complete_the_chain(self):
        """The historical wrong-slot completion: the press's own send does not license another slot's row."""
        clock = FakeClock()
        gesture = CorrelatedGesture(name="slot_record", rules=record_gesture(clock).rules,
                                    now=clock.now, sleep=clock.sleep)
        with patch("hitl.actions.slots._send_short_press"):
            slots.press_slot_record(self.ctx, 4, correlate=gesture, before_device_us=5_000_000)
        rows = ["Button press: Loop 4 (short)", "#CAP,5200000,DISP,2,ARMED,0,0"]
        self.assertFalse(gesture.wait_for(Stage.ACCEPT, lambda: rows, timeout_s=0.4, poll_s=0.2))
        self.assertEqual("accept", gesture.blocking_stage())
        self.assertEqual("pending", gesture.snapshot()["stages"]["accept"],
                         "a foreign slot's row leaves the stage pending, never met")


if __name__ == "__main__":
    unittest.main(verbosity=2)
