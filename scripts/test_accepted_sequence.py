#!/usr/bin/env python3
"""Host tests for the S4f.13 accepted-sequence reproducer (`hitl_accepted_sequence.py` + its oracle).

Two things are pinned here without a device:

1. **The gesture order is the requirement.** A stop is one press, the switch to B comes after it, and B's stop
   is one press -- so the plan is asserted as data, and the presses are driven through the real action layer
   against a fake port to confirm the right notes and counts reach the wire.
2. **The oracle's verdicts.** Each mutation of a synthetic log removes or reorders exactly one link, and the
   verdict must name that link rather than pass.

Run: .venv/bin/python scripts/test_accepted_sequence.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl_accepted_sequence import (  # noqa: E402
    DEFAULT_TAP_WINDOW_S,
    Gesture,
    _emit,
    _switch_pending,
    leg_gestures,
    to_firmware_slot,
)
from hitl.accepted_sequence_probe import analyse  # noqa: E402
from hitl.control_constants import (  # noqa: E402
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)

BOOT = "BOOT,usb_host,deferred"
METADATA = {"git_commit": "abc123def456", "firmware_hex_sha256": "f00dbabe"}
BASE_REPORT = {"capture_integrity": "COMPLETE", "capture_complete": True, "setup_transitions_confirmed": True,
               "issues": []}


class FakePort:
    """Records (type, note, channel) and press counts; ignores timing."""

    def __init__(self):
        self.sent = []

    def send(self, message):
        self.sent.append((message.type, getattr(message, "note", None), getattr(message, "channel", None)))

    def note_sequence(self):
        return [n for (t, n, _c) in self.sent if t == "note_on"]


def emit_all(gestures, *, press_ms=120):
    port = FakePort()
    for g in gestures:
        _emit(port, g, press_ms=press_ms)
    return port


def loop_note(slot_1based: int) -> int:
    return LOOP_SELECT_NOTE_BASE + slot_1based - 1


class GesturePlanTests(unittest.TestCase):
    def test_the_record_leg_order_is_the_requirement(self):
        plan = leg_gestures("rec", track=8, a=4, b=5)
        self.assertEqual(
            ["select_track", "a_record_start", "a_stop", "b_switch_start", "b_stop"],
            [g.step for g in plan])
        # A's stop is ONE gesture, and it precedes the switch to B.
        self.assertLess([g.step for g in plan].index("a_stop"),
                        [g.step for g in plan].index("b_switch_start"))

    def test_the_overdub_leg_order_is_the_requirement(self):
        plan = leg_gestures("od", track=8, a=4, b=5)
        self.assertEqual(
            ["select_track", "a_overdub_start", "a_stop", "b_switch", "b_overdub_start", "b_stop"],
            [g.step for g in plan])

    def test_each_stop_appears_exactly_once_per_leg(self):
        for leg in ("rec", "od"):
            plan = leg_gestures(leg, track=8, a=4, b=5)
            stops = [g for g in plan if g.step.endswith("_stop")]
            self.assertEqual(2, len(stops), f"{leg}: one stop per interval, no more")

    def test_the_switch_comes_after_a_stop_and_before_b_start(self):
        plan = leg_gestures("od", track=8, a=4, b=5)
        steps = [g.step for g in plan]
        self.assertLess(steps.index("a_stop"), steps.index("b_switch"))
        self.assertLess(steps.index("b_switch"), steps.index("b_overdub_start"))

    def test_the_wire_gets_the_loop_and_record_notes_the_firmware_maps(self):
        plan = leg_gestures("od", track=8, a=4, b=5)
        notes = emit_all(plan).note_sequence()
        self.assertIn(TRACK_SELECT_NOTE_BASE + 7, notes, "track 8 -> TRACK_SELECT_NOTE_BASE + 7")
        self.assertIn(loop_note(4), notes, "A -> slot 4's loop note")
        self.assertIn(loop_note(5), notes, "B -> slot 5's loop note")
        self.assertIn(RECORD_BUTTON_NOTE, notes, "the record button is the record/overdub gesture")
        self.assertNotIn(GLOBAL_TRANSPORT_NOTE, notes,
                         "the overdub leg drives the transport only through explicit transport gestures")

    def test_the_record_leg_uses_the_transport_for_its_stops(self):
        plan = leg_gestures("rec", track=8, a=4, b=5)
        notes = emit_all(plan).note_sequence()
        self.assertEqual(1, notes.count(GLOBAL_TRANSPORT_NOTE),
                         "A is stopped by the transport; B by the record button, one press each")

    def test_b_is_stopped_with_the_record_button_not_its_loop_button(self):
        """A loop-button press on a filled playing slot reaches the switch/mute branch, never
        `Track::stopOverdubbing` -- measured 2026-09-27, where it produced "Reaffirmed playback switch"."""
        for leg in ("rec", "od"):
            plan = leg_gestures(leg, track=8, a=4, b=5)
            b_stop = next(g for g in plan if g.step == "b_stop")
            self.assertEqual("record", b_stop.kind, f"{leg}: B must be stopped with the record button")

    def test_slot_conversion_is_the_only_base_change(self):
        self.assertEqual(3, to_firmware_slot(4))
        self.assertEqual(4, to_firmware_slot(5))
        self.assertEqual(7, to_firmware_slot(8))


def chain_log(*, window: bool = True, release: bool = True, denials: bool = False,
              start: bool = True, stops_a: int = 1, stops_b: int = 1, ring: bool = False,
              boot: bool = True, human_window: bool = False) -> str:
    """A synthetic session log for one leg. Each keyword removes or duplicates exactly one link."""
    lines = [BOOT if boot else "Logger initialized with level: 3"]
    if human_window:
        lines.append("[40.659] [INFO] [TRACK] Reentry window open slot=3")
    # A's seed and stop. Firmware rows are 0-based: A = slot 4 = 3.
    lines.append("#CAP,1000000,RECA,3,0")
    for i in range(stops_a):
        lines.append(f"#CAP,{2000000 + i},RECS,stopToStopped,3,1900,0,1900,2304,0")
    if window:
        lines.append("#CAP,3000000,DIAG,reentry_window_open,3,1117,2304")
    if release:
        lines.append("#CAP,4000000,DIAG,canonical_release,3,40,2304,8")
    if denials:
        lines.append("#CAP,4500000,ODUB,stop,admission,0,0,4294967295,4294967295,1,0,"
                     "successor_stop_denied_capacity")
        lines.append("#CAP,4500001,DIAG,reentry,deny,1311,2,4,1")
        lines.append("[41.190] [INFO] [TRACK] Overdub stop refused: stop_job_pending slot=4 interval=2")
    # B = slot 5 = 4.
    if start:
        lines.append("#CAP,5000000,RECA,4,2304")
    for i in range(stops_b):
        lines.append(f"#CAP,{6000000 + i},RECS,stopToStopped,4,1900,0,1900,2304,0")
    if ring:
        lines.append("#CAP,7000000,RING,tier_a_evicted,9790,first=2380221900")
    return "\n".join(lines) + "\n"


def run(text, *, leg="od", slot_a=4, slot_b=5, **kw):
    return analyse(text, leg=leg, slot_a=slot_a, slot_b=slot_b, metadata=METADATA, base_report=BASE_REPORT)


def by_name(result):
    return {c.name: c for c in result.checks}


class SwitchRepeatGuardTests(unittest.TestCase):
    """Regression for the double-countdown defect found on device 2026-09-27.

    Measured: the driver pressed slot 5 twice inside the firmware's tap window, and the device logged
    `Queued playback switch` -> `Reaffirmed playback switch` -> `Reaffirmed ...`, i.e. **two countdowns for
    one switch**, because `queuePlayingSlotSwitch` clears and re-requests without checking whether the same
    target is already pending. `_switch_pending` is the guard that makes the driver wait it out.
    """

    def _log(self, tmp_name, lines):
        import tempfile
        from pathlib import Path as _P
        path = _P(tempfile.mkdtemp()) / tmp_name
        path.write_text("\n".join(lines) + "\n")
        return path

    def test_a_queued_switch_that_has_not_executed_reads_as_pending(self):
        log = self._log("s.log", [
            "[26.158] [INFO] Button press: Loop 5 (short)",
            "[26.217] [INFO] Loop 5: Queued playback switch",
        ])
        self.assertTrue(_switch_pending(log, 0, 5),
                        "a queued switch with no execution must hold the slot buttons")

    def test_the_reaffirm_row_also_reads_as_pending(self):
        """The reaffirm is the second countdown; it must not read as 'done'."""
        log = self._log("s.log", [
            "[26.217] [INFO] Loop 5: Queued playback switch",
            "[26.595] [INFO] Loop 5: Reaffirmed playback switch",
        ])
        self.assertTrue(_switch_pending(log, 0, 5))

    def test_an_executed_switch_is_no_longer_pending(self):
        log = self._log("s.log", [
            "[26.217] [INFO] Loop 5: Queued playback switch",
            "[27.100] [DEBUG] [TRACK] Playback started @ tick 1536",
        ])
        self.assertFalse(_switch_pending(log, 0, 5),
                         "once the switch has run, the buttons are free again")

    def test_a_switch_for_another_slot_does_not_hold_this_one(self):
        log = self._log("s.log", ["[26.217] [INFO] Loop 8: Queued playback switch"])
        self.assertFalse(_switch_pending(log, 0, 5))

    def test_the_tap_window_is_at_least_the_firmware_window(self):
        # The firmware holds a second tap for ~300 ms before deciding short/double; pressing sooner merges.
        self.assertGreaterEqual(DEFAULT_TAP_WINDOW_S, 0.30)


class OracleTests(unittest.TestCase):
    def test_the_complete_chain_passes(self):
        result = run(chain_log())
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict,
                         [(c.name, c.status, c.detail) for c in result.checks])
        self.assertEqual(0, result.exit_code)

    def test_a_missing_finalization_window_fails_L3(self):
        result = run(chain_log(window=False, release=False))
        self.assertEqual("fail", by_name(result)["L3_a_finalization_window"].status)
        self.assertNotEqual(0, result.exit_code)

    def test_a_capacity_denial_fails_L2_even_though_every_other_link_holds(self):
        """The acceptance is unconditional; a denial means the successor was NOT accepted."""
        result = run(chain_log(denials=True))
        self.assertEqual("fail", by_name(result)["L2_no_capacity_denial"].status)
        self.assertEqual("ACCEPTED_SEQUENCE_FAIL", result.verdict)

    def test_a_second_stop_for_one_interval_fails_the_one_press_link(self):
        result = run(chain_log(stops_b=2))
        self.assertEqual("fail", by_name(result)["L6_b_stop_executes_once"].status)
        self.assertEqual("fail", by_name(result)["L7_one_press_per_stop"].status)

    def test_a_ring_eviction_is_a_setup_failure(self):
        result = run(chain_log(ring=True))
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["overflow_free"].status)

    def test_a_missing_boot_banner_is_a_setup_failure(self):
        result = run(chain_log(boot=False))
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["fresh_boot"].status)

    def test_a_human_window_row_alone_still_satisfies_L3_but_says_so(self):
        result = run(chain_log(window=False, human_window=True))
        self.assertEqual("ok", by_name(result)["L3_a_finalization_window"].status)
        self.assertIn("timely human row", by_name(result)["L3_a_finalization_window"].detail)

    def test_a_missing_successor_start_is_a_failure_not_a_gap(self):
        result = run(chain_log(start=False))
        self.assertEqual("fail", by_name(result)["L5_successor_started"].status)

    def test_missing_metadata_is_reported_as_evidence_not_setup(self):
        result = analyse(chain_log(), leg="od", slot_a=4, slot_b=5, metadata=None, base_report=None)
        self.assertEqual("unknown", by_name(result)["identity_pinned"].status)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)

    def test_the_slot_base_conversion_is_applied(self):
        """A log whose rows are 0-based must be read with the driver's 1-based CLI numbers."""
        result = run(chain_log(), slot_a=4, slot_b=5)
        self.assertEqual("ok", by_name(result)["L1_a_stop_accepted_once"].status)
        # Reading the same log with the wrong base finds no stop for A at all.
        wrong = run(chain_log(), slot_a=3, slot_b=4)
        self.assertEqual("fail", by_name(wrong)["L1_a_stop_accepted_once"].status)


if __name__ == "__main__":
    unittest.main(verbosity=2)
