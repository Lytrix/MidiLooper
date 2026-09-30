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

from hitl.actions.correlation import Wire  # noqa: E402
from hitl_retention_handoff import (  # noqa: E402
    Gesture,
    _await_seed_receipt,
    _device_floor_us,
    _emit,
    _seed_arm_wires,
    _seed_recording_wires,
    _stage_report,
    cleared_slot_from_lines,
    critical_window_gestures,
    target_precondition,
)


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

    # --- the staging precondition: a populated successor must be refused, not silently cleared ---
    @staticmethod
    def census(*, track: int, committed: dict[int, bool]) -> dict:
        rows = []
        for t in range(1, 9):
            for sl in range(1, 9):
                rows.append({"track": t - 1, "slot": sl - 1,
                             "has_committed_content": bool(committed.get(sl, False)) if t == track else False})
        return {"library_observation": {"slots": rows}}

    def test_empty_targets_satisfy_the_precondition(self):
        ok, detail = target_precondition(self.census(track=8, committed={}), track=8, slots=(5, 6, 7))
        self.assertTrue(ok, detail)

    def test_a_populated_successor_is_refused_and_named(self):
        """The rerun's hypothesis, encoded: if C holds committed content, the run must stop as setup --
        a populated successor belongs to the populated-slot/overdub scenario, not this one."""
        ok, detail = target_precondition(self.census(track=8, committed={5: False, 6: False, 7: True}),
                                         track=8, slots=(5, 6, 7))
        self.assertFalse(ok)
        self.assertIn("slot(s) [7]", detail)
        self.assertIn("populated-slot/overdub acceptance scenario", detail)

    def test_a_populated_predecessor_is_also_refused(self):
        ok, detail = target_precondition(self.census(track=8, committed={5: True, 6: False, 7: False}),
                                         track=8, slots=(5, 6, 7))
        self.assertFalse(ok)
        self.assertIn("slot(s) [5]", detail)

    def test_an_unreported_target_is_refused_rather_than_assumed_empty(self):
        ok, detail = target_precondition({"library_observation": {"slots": []}}, track=8, slots=(5, 6, 7))
        self.assertFalse(ok)
        self.assertIn("unknown, which is not the same as empty", detail)

    def test_the_precondition_reads_the_named_track_not_any_slot_with_that_index(self):
        """A slot index is only meaningful with its track: another track's populated slot 5 must not fail
        track 8's slots 5/6/7."""
        data = self.census(track=8, committed={})
        for r in data["library_observation"]["slots"]:
            if r["track"] == 2 and r["slot"] == 4:      # track 3 slot 5 populated
                r["has_committed_content"] = True
        ok, detail = target_precondition(data, track=8, slots=(5, 6, 7))
        self.assertTrue(ok, detail)

    def test_transport_gesture_uses_the_global_transport_note(self):
        from hitl.control_constants import GLOBAL_TRANSPORT_NOTE
        port = FakePort()
        _emit(port, Gesture("t", "transport", 0), press_ms=120, gap_ms=0)
        self.assertIn(GLOBAL_TRANSPORT_NOTE, port.note_sequence())



class ClearVerificationTests(unittest.TestCase):
    """The staging clear must prove it cleared the INTENDED slot.

    Before the firmware named the slot, `_clear_slot` accepted any `Track cleared`/`Clear ignored` marker.
    That is how a preparation reported success while the successor slot still held content, which then made
    the successor ineligible for the record-start scenario. These pin the identity verdict, including the
    distinction between "no identity line" (None) and "a clear line naming another slot" (False).
    """

    def test_the_intended_slot_clearing_is_a_pass(self):
        """Target 8 (1-based) is `active_slot=7` (0-based): the conversion is the whole point here."""
        self.assertTrue(cleared_slot_from_lines(
            ["[1885.0] [DEBUG] [TRACK] Track cleared @ tick 1885 (active_slot=7)"], slot_number=8))

    def test_a_clear_line_naming_another_slot_is_a_failure(self):
        """The defect: a bare marker match must not pass when the clear hit a different slot."""
        self.assertFalse(cleared_slot_from_lines(
            ["[1885.0] [DEBUG] [TRACK] Track cleared @ tick 1885 (active_slot=5)"], slot_number=8))

    def test_the_base_conversion_is_not_off_by_one(self):
        """Regression from the device run: the firmware emits `active_slot=4` for target slot 5.

        Comparing the raw values judged every real clear a mismatch (`clear_5_wrong_slot`) while the device
        had cleared exactly the intended slot. Both directions are pinned so the base can never drift again.
        """
        line = ["[26.336] [DEBUG] [TRACK] Track cleared @ tick 0 (active_slot=4)"]
        self.assertTrue(cleared_slot_from_lines(line, slot_number=5), "target 5 must match active_slot=4")
        self.assertFalse(cleared_slot_from_lines(line, slot_number=4), "target 4 must not match active_slot=4")
        self.assertFalse(cleared_slot_from_lines(line, slot_number=6), "target 6 must not match active_slot=4")

    def test_the_intended_slot_already_empty_is_acceptable_for_a_preparation(self):
        self.assertTrue(cleared_slot_from_lines(
            ["[32.898] [DEBUG] Clear ignored \u2014 selected slot is empty (track 7 slot 5)"],
            slot_number=6))

    def test_a_missing_identity_line_is_unknown_not_a_pass(self):
        """An older firmware emits the legacy marker only; that must not be claimed as identity-grade."""
        self.assertIsNone(cleared_slot_from_lines(
            ["[20.456] [DEBUG] [TRACK] Track cleared @ tick 1885"], slot_number=7))

    def test_the_legacy_one_based_line_is_not_treated_as_identity(self):
        """`MIDI: Clear selected slot N` is 1-based; parsing it as identity would be off by one."""
        self.assertIsNone(cleared_slot_from_lines(
            ["[1456.072] [INFO] MIDI: Clear selected slot 2"], slot_number=2))

    def test_a_wrong_slot_clear_does_not_mask_a_later_correct_one(self):
        lines = [
            "[1.0] [DEBUG] [TRACK] Track cleared @ tick 100 (active_slot=5)",
            "[2.0] [DEBUG] [TRACK] Track cleared @ tick 200 (active_slot=7)",
        ]
        self.assertTrue(cleared_slot_from_lines(lines, slot_number=8))

    def test_the_whole_track_empty_form_names_the_slot_too(self):
        self.assertTrue(cleared_slot_from_lines(
            ["[1.0] [DEBUG] Clear ignored \u2014 track is empty (track 7 slot 5)"], slot_number=6))


class SeedReceiptTests(unittest.TestCase):
    """The seed press's receipt is attributed to its own slot, and its stages are named.

    Measured on device: the two `Loop 3 (short)` presses a predecessor's in-flight Record job held left **no**
    `DISP` row for that slot, and the driver could only report `slot 4 never started recording`. Each case here
    pins one part of the fix -- the receipt must be the slot's own row, the press must have been recognised as
    *this* gesture, and a stage that never arrived is named instead of timed out.
    """

    def _log(self, lines: list[str]) -> Path:
        import tempfile
        path = Path(tempfile.mkdtemp()) / "session.log"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    def test_the_arm_receipt_must_name_the_slot(self):
        log = self._log([
            "[10.000] [INFO] Transport stopped",
            "[10.050] [INFO] MIDI: Clear selected slot 5",         # the floor: newest row before the mark
            "[10.100] [INFO] Button press: Loop 5 (short)",        # the press itself, after the mark
            "#CAP,10150000,DISP,4,ARMED,0,0",                      # slot 5's own, 0-based, arm row
        ])
        row, gesture = _await_seed_receipt(log, 2, slot_number=5, stage="arm", timeout_s=0.0)
        self.assertIsNotNone(row)
        self.assertIn("DISP,4,ARMED", row)
        self.assertEqual({"send": "met", "recognize": "met", "accept": "met", "execute": "pending"},
                         gesture.snapshot()["stages"])

    def test_another_slot_or_an_unattributed_transition_is_not_the_receipt(self):
        """The track-state family names no slot, so it cannot stand in for this slot's own row."""
        lines = [
            "[10.000] [INFO] Transport stopped",
            "[10.050] [INFO] MIDI: Clear selected slot 5",
            "[10.100] [INFO] Button press: Loop 5 (short)",
        ]
        foreign = self._log([*lines, "#CAP,10150000,DISP,2,ARMED,0,0"])
        unattributed = self._log([*lines, "#CAP,10150000,ST,Track,STOPPED,ARMED"])
        for log in (foreign, unattributed):
            row, gesture = _await_seed_receipt(log, 2, slot_number=5, stage="arm", timeout_s=0.0)
            self.assertIsNone(row)
            self.assertEqual("blocked at accept", _stage_report(gesture).split(" (")[0])

    def test_a_press_that_was_never_recognized_is_named(self):
        """A swallowed press is the `recognize` stage, not a bare timeout: the device never named this press."""
        log = self._log([
            "[10.000] [INFO] Transport stopped",
            "[10.050] [INFO] MIDI: Clear selected slot 5",
            "#CAP,10150000,DISP,4,ARMED,0,0",                      # an arm row, but for an older gesture
        ])
        row, gesture = _await_seed_receipt(log, 2, slot_number=5, stage="arm", timeout_s=0.0)
        self.assertIsNone(row)
        self.assertEqual("blocked at recognize", _stage_report(gesture).split(" (")[0])

    def test_a_merged_classification_is_a_named_contradiction(self):
        """Measured: two taps inside the tap window arrive as `Loop N (double)` -- not this gesture at all."""
        log = self._log([
            "[10.000] [INFO] Transport stopped",
            "[10.050] [INFO] MIDI: Clear selected slot 5",
            "[10.100] [INFO] Button press: Loop 5 (double)",
            "#CAP,10150000,DISP,4,ARMED,0,0",
        ])
        row, gesture = _await_seed_receipt(log, 2, slot_number=5, stage="arm", timeout_s=0.0)
        self.assertIsNone(row)
        self.assertEqual("recognize", gesture.contradicted())
        self.assertIn("answered as a different gesture", _stage_report(gesture))

    def test_a_row_from_before_the_press_is_never_the_receipt(self):
        """The floor is the device clock observed before the press (`_device_floor_us`), from either family."""
        log = self._log([
            "[10.000] [INFO] Transport stopped",
            "[10.050] [INFO] MIDI: Clear selected slot 5",
            "[10.100] [INFO] Button press: Loop 5 (short)",
            "#CAP,10040000,DISP,4,ARMED,0,0",                      # stamped before the floor
        ])
        self.assertEqual(10_050_000, _device_floor_us(log, 2))
        row, _gesture = _await_seed_receipt(log, 2, slot_number=5, stage="arm", timeout_s=0.0)
        self.assertIsNone(row)

    def test_the_take_is_only_proved_after_this_gesture_s_own_arm(self):
        """`Recording started @ tick` names no slot, so ordering is what attributes it to this seed."""
        lines = [
            "[10.000] [INFO] Transport stopped",
            "[10.050] [INFO] MIDI: Clear selected slot 5",
            "[10.100] [INFO] Button press: Loop 5 (short)",
        ]
        unarmed = self._log([*lines, "[10.200] [TRACK] Recording started @ tick 0"])
        armed = self._log([*lines, "#CAP,10150000,DISP,4,ARMED,0,0",
                           "[10.200] [TRACK] Recording started @ tick 0"])
        row, gesture = _await_seed_receipt(unarmed, 2, slot_number=5, stage="recording", timeout_s=0.0)
        self.assertIsNone(row)
        self.assertEqual("blocked at accept", _stage_report(gesture).split(" (")[0])
        row, gesture = _await_seed_receipt(armed, 2, slot_number=5, stage="recording", timeout_s=0.0)
        self.assertIn("Recording started", row)
        self.assertEqual("met", gesture.snapshot()["stages"]["execute"])

    def test_the_wire_vocabulary_is_slot_scoped_in_each_family(self):
        arm = _seed_arm_wires(5, 4)
        take = _seed_recording_wires(5, 4)
        self.assertIn(Wire("DISP,4,ARMED", ("#CAP,",)), arm)
        self.assertIn(Wire("Loop 5: Start Recording"), arm)
        self.assertIn(Wire(",RECA,4,", ()), take)
        self.assertIn(Wire("Recording started"), take)


if __name__ == "__main__":
    unittest.main(verbosity=2)
