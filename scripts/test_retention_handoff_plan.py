#!/usr/bin/env python3
"""Host tests for the retention-handoff gesture plan (`hitl_retention_handoff.py`).

The device run's correctness depends on the **order** of the presses in the critical window, and on the
right notes and press counts reaching the wire. That is all assertable without a device, by driving the
plan through the same action layer against a fake MIDI port.

Firmware mapping under test (channel 16): loop buttons are `LOOP_SELECT_NOTE_BASE + slot - 1` (short =
slot gesture, double = overdub), track buttons are `TRACK_SELECT_NOTE_BASE + track - 1`.

Run: .venv/bin/python scripts/test_retention_handoff_plan.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.control_constants import LOOP_SELECT_NOTE_BASE, TRACK_SELECT_NOTE_BASE  # noqa: E402
from hitl_retention_handoff import Gesture, _emit, critical_window_gestures  # noqa: E402


class FakePort:
    """Records (type, note, channel) and press counts; ignores timing."""

    def __init__(self):
        self.sent = []

    def send(self, message):
        self.sent.append((message.type, getattr(message, "note", None),
                          getattr(message, "channel", None), getattr(message, "velocity", None)))

    def note_sequence(self):
        return [n for (t, n, _c, v) in self.sent if t == "note_on" and (v or 0) > 0]


def emit_all(gestures, *, press_ms=120, gap_ms=0):
    port = FakePort()
    for g in gestures:
        _emit(port, g, press_ms=press_ms, gap_ms=gap_ms)
    return port


def loop_note(slot_1based: int) -> int:
    return LOOP_SELECT_NOTE_BASE + slot_1based - 1


class RetentionGesturePlanTests(unittest.TestCase):
    def test_the_chain_order_is_the_requirement(self):
        """A overdubs, the switch to B (which is also A's stop), B overdubs, B stops, C is requested."""
        plan = critical_window_gestures(track=5, a=1, b=2, c=3)
        self.assertEqual(
            ["select_track", "a_overdub_start", "b_switch", "b_overdub_start", "b_stop_request",
             "c_switch_request"],
            [g.step for g in plan],
        )

    def test_there_is_no_separate_a_stop_press(self):
        """The capture-switch performs A's stop. Measured: an admitted stop leaves the track PLAYING, so a
        separate stop press first makes the next press take the playing/filled branch and the
        finalization window never opens."""
        steps = [g.step for g in critical_window_gestures(track=5, a=1, b=2, c=3)]
        self.assertNotIn("a_stop_request", steps)
        # ...and the switch that performs it comes after A's overdub has been asked for.
        self.assertLess(steps.index("a_overdub_start"), steps.index("b_switch"))

    def test_b_overdub_precedes_b_stop_because_only_an_overdub_stop_is_capacity_governed(self):
        steps = [g.step for g in critical_window_gestures(track=5, a=1, b=2, c=3)]
        self.assertLess(steps.index("b_overdub_start"), steps.index("b_stop_request"))

    def test_c_switch_follows_b_stop_so_the_denial_it_depends_on_already_happened(self):
        steps = [g.step for g in critical_window_gestures(track=5, a=1, b=2, c=3)]
        self.assertLess(steps.index("b_stop_request"), steps.index("c_switch_request"))

    def test_the_track_is_selected_first(self):
        plan = critical_window_gestures(track=5, a=1, b=2, c=3)
        self.assertEqual("select_track", plan[0].step)
        self.assertEqual(5, plan[0].value)

    def test_the_presses_reach_the_expected_notes_and_counts(self):
        plan = critical_window_gestures(track=5, a=1, b=2, c=3)
        port = emit_all(plan, gap_ms=0)
        notes = port.note_sequence()
        # track 5 -> note base + 4; A=1, B=2, C=3 -> their loop notes; overdub is a double press.
        expected = [TRACK_SELECT_NOTE_BASE + 4,
                    loop_note(1), loop_note(1),                  # A: overdub (x2)
                    loop_note(2), loop_note(2), loop_note(2),    # B: switch, then overdub (x2)
                    loop_note(2),                                # B: stop request
                    loop_note(3)]                                # C: switch request
        self.assertEqual(expected, notes)

    def test_the_overdub_gestures_are_double_presses_and_the_rest_are_single(self):
        plan = critical_window_gestures(track=5, a=1, b=2, c=3)
        port = emit_all(plan, gap_ms=0)
        notes = port.note_sequence()
        # A: exactly one double press; the switch performs its stop, so there is no third press.
        self.assertEqual(2, notes.count(loop_note(1)), "A gets one double press and nothing else")
        # B: the switch, the overdub double press, and the stop request = 4.
        self.assertEqual(4, notes.count(loop_note(2)), "B gets a switch, a double press, and a stop")
        self.assertEqual(1, notes.count(loop_note(3)), "C gets a single switch request")

    def test_the_slots_are_independent_of_the_track(self):
        plan = critical_window_gestures(track=1, a=6, b=7, c=8)
        port = emit_all(plan, gap_ms=0)
        self.assertIn(LOOP_SELECT_NOTE_BASE + 5, port.note_sequence())
        self.assertIn(LOOP_SELECT_NOTE_BASE + 7, port.note_sequence())
        self.assertIn(TRACK_SELECT_NOTE_BASE, port.note_sequence())

    def test_out_of_range_slots_are_rejected_rather_than_silently_mapped(self):
        port = FakePort()
        with self.assertRaises(ValueError):
            _emit(port, Gesture("x", "slot_record", 9), press_ms=120, gap_ms=0)
        with self.assertRaises(ValueError):
            _emit(port, Gesture("x", "track", 0), press_ms=120, gap_ms=0)

    def test_transport_gesture_uses_the_global_transport_note(self):
        from hitl.control_constants import GLOBAL_TRANSPORT_NOTE
        port = FakePort()
        _emit(port, Gesture("t", "transport", 0), press_ms=120, gap_ms=0)
        self.assertIn(GLOBAL_TRANSPORT_NOTE, port.note_sequence())


if __name__ == "__main__":
    unittest.main(verbosity=2)
