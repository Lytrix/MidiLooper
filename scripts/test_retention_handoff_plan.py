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
        from hitl.control_constants import RECORD_BUTTON_NOTE
        plan = critical_window_gestures(track=5, a=1, b=2, c=3)
        port = emit_all(plan, gap_ms=0)
        # track 5 -> base+4; A's overdub and B's overdub/stop go through the RECORD button; B and C keep
        # their own loop buttons for the switch.
        expected = [TRACK_SELECT_NOTE_BASE + 4,
                    RECORD_BUTTON_NOTE,          # A overdubs (active slot is A)
                    loop_note(2),                # capture-switch to B, which is also A's stop
                    RECORD_BUTTON_NOTE,          # B overdubs
                    loop_note(2),                # B's stop request, denied on capacity
                    loop_note(3)]                # C switch request, retained
        self.assertEqual(expected, port.note_sequence())

    def test_no_two_consecutive_presses_share_a_button(self):
        """A loop button's double press has a triple action, so the firmware holds the second tap until the
        triple window expires, and any press on the same button inside that wait merges into a triple
        (measured: "Triple press detected ... Loop 6 (triple)", i.e. REDO instead of an overdub).

        The record button is used three times, but each use is separated by a state wait, so what the plan
        must guarantee is that no two *consecutive* gestures press the same note.
        """
        from hitl.control_constants import RECORD_BUTTON_NOTE
        plan = critical_window_gestures(track=5, a=1, b=2, c=3)

        def note_of(g):
            if g.kind == "track":
                return TRACK_SELECT_NOTE_BASE + g.value - 1
            if g.kind == "slot_record":
                return loop_note(g.value)
            if g.kind == "record":
                return RECORD_BUTTON_NOTE
            return None

        notes = [note_of(g) for g in plan]
        for prev, cur, step in zip(notes, notes[1:], [g.step for g in plan][1:]):
            self.assertNotEqual(prev, cur,
                                f"{step} presses the same button as the gesture before it; inside the triple "
                                "window they would merge")
        # A is never pressed on its own loop button. B's loop button is used twice -- the switch and the stop
        # -- but they are separated by the record press, which is exactly what the loop above checks.
        self.assertNotIn(loop_note(1), notes)
        self.assertEqual(2, notes.count(loop_note(2)))

    def test_the_slots_are_independent_of_the_track(self):
        plan = critical_window_gestures(track=1, a=6, b=7, c=8)
        port = emit_all(plan, gap_ms=0)
        self.assertIn(LOOP_SELECT_NOTE_BASE + 6, port.note_sequence())   # B = slot 7
        self.assertIn(LOOP_SELECT_NOTE_BASE + 7, port.note_sequence())   # C = slot 8
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
