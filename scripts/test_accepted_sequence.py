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
import hashlib
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.flows.accepted_sequence import (  # noqa: E402
    DEFAULT_CENSUS_ATTEMPTS,
    DEFAULT_SEED_PRESS_ATTEMPTS,
    DEFAULT_TAP_WINDOW_S,
    Gesture,
    _await_slot_disp,
    _Ctx,
    _emit,
    _PressSpacing,
    _run_census,
    _run_plan,
    _switch_pending,
    leg_gestures,
    to_firmware_slot,
)

#: Task 4.4b split the driver: the **sequence** lives in `hitl.flows.accepted_sequence` (and the names this
#: suite patches for it must live there), while the **session, authorization and report** live in
#: `hitl.accepted_sequence_run`. `_patch_target` routes a patch name to its owner, so a test states *what* it
#: fakes rather than *where the object happens to be filed today*.
_FLOW_OWNED_PREFIXES = ("slots.", "stopped_preparation.", "observation_window", "time.sleep")
_FLOW_OWNED_NAMES = ("_await_markers", "_await_slot_disp", "_emit", "_ensure_transport", "_run_census",
                     "_run_plan", "_slot_disp_rows", "_slot_ready_state", "_gesture_clock_us", "_wait_for")


def _patch_target(name: str) -> str:
    if name in _FLOW_OWNED_NAMES or name.startswith(_FLOW_OWNED_PREFIXES):
        return "hitl.flows.accepted_sequence." + name
    return "hitl.accepted_sequence_run." + name
from hitl.accepted_sequence_probe import (  # noqa: E402
    Analysis,
    Check,
    _run_window,
    analyse,
    verdict_bundle,
)
from hitl.control_constants import (  # noqa: E402
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)
from hitl.flows import stopped_preparation  # noqa: E402
from hitl.library_observation import observation_window, occupied_slots  # noqa: E402
from hitl.flows.stopped_preparation import ClearStatus  # noqa: E402

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

DISP_STAMP_SEQ = {"t": 0}


def disp_row(slot_1based: int, state: str, *, stamp_us: int | None = None) -> str:
    """One device display row for `slot_1based`: the receipt the driver confirms a gesture against."""
    if stamp_us is None:
        DISP_STAMP_SEQ["t"] += 1000
        stamp_us = DISP_STAMP_SEQ["t"]
    return f"#CAP,{stamp_us},DISP,{slot_1based - 1},{state},0,0"


def answering_presses(log: Path, *, states: tuple[str, ...] = ("ARMED", "RECORDING")) -> object:
    """A `slots.press_slot_record` stand-in that answers the press the way the device does.

    The driver confirms every gesture against a live `DISP` row for that slot, so a fixture that patches the
    presses away has to model that row too -- otherwise it tests the wait instead of the flow.
    """
    def press(_ctx, slot_number, press_ms=None):
        with log.open("a", encoding="utf-8") as handle:
            for state in states:
                handle.write(disp_row(slot_number, state) + "\n")
    return press


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
            ["select_track", "a_overdub_start", "b_switch", "b_overdub_start", "b_stop_request"],
            [g.step for g in plan])

    def test_overdub_plan_matches_proven_retention_prefix(self):
        from hitl_retention_handoff import critical_window_gestures
        expected = critical_window_gestures(track=8, a=4, b=5, c=8)[:-1]
        actual = leg_gestures("od", track=8, a=4, b=5)
        self.assertEqual([(g.step, g.kind, g.value) for g in expected],
                         [(g.step, g.kind, g.value) for g in actual])
        self.assertNotIn("a_stop", [g.step for g in actual])

    def test_the_wire_gets_the_loop_and_record_notes_the_firmware_maps(self):
        plan = leg_gestures("od", track=8, a=4, b=5)
        notes = emit_all(plan).note_sequence()
        self.assertIn(TRACK_SELECT_NOTE_BASE + 7, notes, "track 8 -> TRACK_SELECT_NOTE_BASE + 7")
        self.assertNotIn(loop_note(4), notes, "B switch is A's stop; do not stop A separately")
        self.assertIn(loop_note(5), notes, "B -> slot 5's loop note")
        self.assertIn(RECORD_BUTTON_NOTE, notes, "the record button is the record/overdub gesture")
        self.assertNotIn(GLOBAL_TRANSPORT_NOTE, notes,
                         "the overdub leg drives the transport only through explicit transport gestures")

    def test_the_record_leg_uses_the_transport_for_its_stops(self):
        plan = leg_gestures("rec", track=8, a=4, b=5)
        notes = emit_all(plan).note_sequence()
        self.assertEqual(1, notes.count(GLOBAL_TRANSPORT_NOTE),
                         "A is stopped by the transport; B by the record button, one press each")

    def test_overdub_start_and_stop_use_different_buttons(self):
        plan = leg_gestures("od", track=8, a=4, b=5)
        self.assertEqual(("record", 0), (plan[-2].kind, plan[-2].value))
        self.assertEqual(("slot_record", 5), (plan[-1].kind, plan[-1].value))

    def test_slot_conversion_is_the_only_base_change(self):
        self.assertEqual(3, to_firmware_slot(4))
        self.assertEqual(4, to_firmware_slot(5))
        self.assertEqual(7, to_firmware_slot(8))


def chain_log(*, window: bool = True, release: bool = True, denials: bool = False,
              start: bool = True, stops_a: int = 1, stops_b: int = 1, ring: bool = False,
              boot: bool = True, human_window: bool = False, shed: int = 0) -> str:
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
        lines.append("#CAP,3500000,RECA,4,2304")
    for i in range(stops_b):
        lines.append(f"#CAP,{6000000 + i},RECS,stopToStopped,4,1900,0,1900,2304,0")
    if ring:
        lines.append("#CAP,7000000,RING,tier_a_evicted,9790,first=2380221900")
    for i in range(shed):
        lines.append(f"#CAP,{7000000 + i},RING,shed,1")
    return "\n".join(lines) + "\n"


def run(text, *, leg="rec", slot_a=4, slot_b=5, **kw):
    return analyse(text, leg=leg, slot_a=slot_a, slot_b=slot_b, metadata=METADATA, base_report=BASE_REPORT)


def by_name(result):
    return {c.name: c for c in result.checks}




def family_stat(family: str, *, entered: int, enq: int, rej: int = 0, tx: int | None = None,
                evict: int = 0, us: int = 9000000) -> str:
    """One `RING,record_stats,<family>,…` read-back row, in the owner's own wire form."""
    return (f"#CAP,{us},RING,record_stats,{family},entered={entered},enq={enq},rej={rej},"
            f"tx={entered if tx is None else tx},evict={evict}")


def od_chain_log(*, a_start: bool = True, a_bindings: int = 1, a_binding_interval: int | None = None,
                 b_start: bool = True, b_start_after_a_release: bool = False, b_terminals: int = 1,
                 a_release: bool = True, b_release: bool = True, a_requests: int = 1, b_requests: int = 1,
                 seed_records: bool = True, fallback: bool = False, b_interval: int = 4,
                 family_rows: tuple[str, ...] = ()) -> str:
    """A synthetic **overdub**-leg log: each keyword removes or duplicates exactly one identity link.

    Written against the wire forms the owners emit. Firmware rows are 0-based, so A = slot 4 = 3 and B = slot
    5 = 4. The seeds' `RECA`/`RECS` rows are present by default, because those are exactly what must **not** be
    read as the leg's own overdub evidence (the record leg's vocabulary does not describe an overdub interval).
    """
    lines = [BOOT]
    if seed_records:
        lines += ["#CAP,100000,RECA,3,0",
                  "#CAP,150000,RECS,stopToStopped,3,140000,0,140000,1536,0",
                  "#CAP,200000,RECA,4,0",
                  "#CAP,250000,RECS,stopToStopped,4,240000,0,240000,1536,0"]
    if a_start:
        lines.append("#CAP,1000000,DIAG,reentry,admit,100,3,3,0")
    for i in range(a_bindings):
        lines.append(f"#CAP,{2000000 + i},DIAG,reentry,bound,200,{a_binding_interval or 3},3,0")
    lines.append("#CAP,2000100,DIAG,reentry_window_open,3,200,1536")
    if fallback:
        lines.append("[26.900] [WARN] [TRACK] Overdub stop retarget: companion path stalled; raw pass only")
    if b_start:
        stamp = 6000000 if b_start_after_a_release else 3000000
        lines.append(f"#CAP,{stamp},DIAG,reentry,admit,300,{b_interval},4,1")
    if a_release:
        lines.append("#CAP,5000000,DIAG,canonical_release,3,40,1536,8")
    for i in range(b_terminals):
        lines.append(f"#CAP,{7000000 + i},DIAG,stop_state,7,4,7,5,900,{7000000 + i},1,0,0,0")
    if b_release:
        lines.append("#CAP,8000000,DIAG,canonical_release,4,0,1536,8")
    for i in range(a_requests):
        lines.append(f"[25.{100 + i}] [INFO] Loop 5: Switch gesture from slot 4")
    for i in range(b_requests):
        lines.append(f"[27.{100 + i}] [INFO] Loop 5: Stop Overdub")
    # The stopped-only read-back the leg's own driver asks for (task 2.2's accounting gate reads these).
    lines.extend(family_rows)
    return "\n".join(lines) + "\n"


def run_od(text, **kw):
    return analyse(text, leg="od", slot_a=4, slot_b=5, metadata=METADATA, base_report=BASE_REPORT, **kw)
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

    def test_unattributed_playback_cannot_discharge_a_switch(self):
        log = self._log("s.log", [
            "[26.217] [INFO] Loop 5: Queued playback switch",
            "[27.100] [DEBUG] [TRACK] Playback started @ tick 1536",
        ])
        self.assertTrue(_switch_pending(log, 0, 5),
                        "unattributed Playback started cannot discharge the target queue")

    def test_a_switch_for_another_slot_does_not_hold_this_one(self):
        log = self._log("s.log", ["[26.217] [INFO] Loop 8: Queued playback switch"])
        self.assertFalse(_switch_pending(log, 0, 5))

    def test_the_tap_window_is_at_least_the_firmware_window(self):
        # The firmware holds a second tap for ~300 ms before deciding short/double; pressing sooner merges.
        self.assertGreaterEqual(DEFAULT_TAP_WINDOW_S, 0.30)

    def test_a_switch_that_later_landed_still_invalidated_this_leg(self):
        """The runner's policy is stricter than the gate's own `blocked()`.

        This scenario needs the capture-switch route, so a queue row for the target ends the leg even when the
        switch later landed.
        """
        log = self._log("s.log", [
            "[26.217] [INFO] Loop 5: Queued playback switch",
            "[26.900] [INFO] Loop 5: Switch gesture from slot 4",
        ])
        self.assertTrue(_switch_pending(log, 0, 5))


    def test_playback_before_queue_cannot_discharge_it(self):
        log = self._log("s.log", ["Playback started @ tick 0",
                                  "Loop 5: Queued playback switch"])
        self.assertTrue(_switch_pending(log, 0, 5))

    def test_driver_aborts_before_next_press_despite_unrelated_playback(self):
        from unittest.mock import patch
        log = self._log("s.log", [])
        emitted = []
        def emit(port, gesture, **kw):
            emitted.append(gesture.step)
            with log.open("a") as f:
                f.write("Loop 5: Queued playback switch\nPlayback started @ tick 10\n")
        plan = leg_gestures("od", track=8, a=4, b=5)[2:]
        with patch(_patch_target("_emit"), side_effect=emit), \
             patch(_patch_target("time.sleep")):
            result = _run_plan(FakePort(), log, plan, slot_b=5, press_ms=120,
                               phase_timeout_s=0.01, events=[])
        self.assertEqual("UNEXPECTED_PLAYBACK_SWITCH", result)
        self.assertEqual(["b_switch"], emitted)

    def test_driver_does_not_stop_b_when_overdub_never_started(self):
        from unittest.mock import patch
        log = self._log("s.log", [])
        with patch(_patch_target("_emit")) as emit, \
             patch(_patch_target("_wait_for"), return_value=False):
            result = _run_plan(FakePort(), log, leg_gestures("od", track=8, a=4, b=5)[3:],
                               slot_b=5, press_ms=120, phase_timeout_s=0.01, events=[])
        self.assertEqual("OVERDUB_START_UNCONFIRMED", result)
        self.assertEqual(1, emit.call_count)


    def test_driver_runs_proven_overdub_prefix_and_injects_both_intervals(self):
        from unittest.mock import patch
        log = self._log("s.log", [])
        emitted = []
        def emit(port, gesture, **kw):
            emitted.append(gesture.step)
            with log.open("a") as f:
                if gesture.step.endswith("overdub_start"):
                    f.write("Overdubbing started @ tick 10\n")
                elif gesture.step == "b_switch":
                    f.write("Loop 5: Switch gesture from slot 4\n")
        port = FakePort()
        events = []
        with patch(_patch_target("_emit"), side_effect=emit), \
             patch(_patch_target("time.sleep")):
            result = _run_plan(port, log, leg_gestures("od", track=8, a=4, b=5),
                               slot_b=5, press_ms=120, phase_timeout_s=0.01, events=events,
                               note_channel=7, overdub_notes=2)
        self.assertEqual("DROVE_LEG", result)
        self.assertEqual(["select_track", "a_overdub_start", "b_switch",
                          "b_overdub_start", "b_stop_request"], emitted)
        self.assertEqual([60, 61, 79], port.note_sequence())



class StoppedPreparationTests(unittest.TestCase):
    """Scoped stopped preparation (task 4.1): clear only an authorized target, correlated by its own evidence."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = Path(self.tmp.name) / "session.log"
        self.log.write_text("")

    def _classify(self, lines, *, track0=7, slot0=3):
        return stopped_preparation.classify_clear_evidence(
            lines, 0, track_index0=track0, slot0=slot0)

    def test_preparation_plan_is_one_long_press_on_the_target_slot(self):
        self.assertEqual([("clear_slot_long_press", 4)],
                         stopped_preparation.preparation_gestures(4))
        with self.assertRaises(ValueError):
            stopped_preparation.preparation_gestures(9)

    def test_completed_clear_must_name_the_requested_slot(self):
        cleared = self._classify(["#CAP,1,MIDI: Clear selected slot 4"])
        self.assertEqual(ClearStatus.CLEARED, cleared.status)
        self.assertTrue(cleared.ok)
        wrong = self._classify(["#CAP,1,MIDI: Clear selected slot 5"])
        self.assertEqual(ClearStatus.WRONG_TARGET, wrong.status)
        self.assertFalse(wrong.ok)

    def test_ignored_empty_target_is_idempotent_success_but_another_target_is_not(self):
        same = self._classify(["Clear ignored — selected slot is empty (track 7 slot 3)"])
        self.assertEqual(ClearStatus.ALREADY_EMPTY, same.status)
        self.assertTrue(same.ok)
        other_track = self._classify(["Clear ignored — track is empty (track 6 slot 3)"])
        other_slot = self._classify(["Clear ignored — track is empty (track 7 slot 4)"])
        self.assertEqual(ClearStatus.WRONG_TARGET, other_track.status)
        self.assertEqual(ClearStatus.WRONG_TARGET, other_slot.status)

    def test_refusal_and_abort_are_distinct_non_successes(self):
        refused = self._classify(
            ["Clear refused: deferred save temporarily blocked by writer; slot left untouched"])
        aborted = self._classify(
            ["Clear aborted: could not complete deferred save first (blocked)"])
        self.assertEqual(ClearStatus.REFUSED, refused.status)
        self.assertEqual(ClearStatus.ABORTED, aborted.status)
        self.assertFalse(refused.ok)
        self.assertFalse(aborted.ok)

    def test_absent_evidence_is_unconfirmed_never_success(self):
        outcome = self._classify(["#CAP,1,DISP,0,PLAYING"])
        self.assertEqual(ClearStatus.UNCONFIRMED, outcome.status)
        self.assertFalse(outcome.ok)

    def test_clear_slot_uses_the_authorized_slots_production_long_press(self):
        from unittest.mock import patch
        seen = {"n": 0}

        def reader(_path):
            seen["n"] += 1
            if seen["n"] == 1:
                return []  # before the press
            return ["#CAP,1,MIDI: Clear selected slot 4"]

        nows = iter([0.0, 0.0, 0.0, 0.0, 9.0])
        with patch("hitl.actions.slots._send_long_press") as press:
            outcome = stopped_preparation.clear_slot(
                _Ctx(FakePort()), self.log, track_index0=7, slot0=3, long_press_ms=900,
                timeout_s=1.0, lines_reader=reader, sleep=lambda _s: None,
                now=lambda: next(nows, 9.0))
        self.assertEqual(ClearStatus.CLEARED, outcome.status)
        press.assert_called_once()
        self.assertEqual(LOOP_SELECT_NOTE_BASE + 3, press.call_args.kwargs["note"])
        self.assertEqual(900, press.call_args.kwargs["press_ms"])

    def test_a_long_press_on_an_unselected_filled_slot_is_not_a_clear(self):
        switched = self._classify(["[1.0] [INFO] Loop 4: long-press switch (transport stopped)"])
        queued = self._classify(["[1.0] [INFO] Loop 4: long-press queued at loop-end"])
        self.assertEqual(ClearStatus.NOT_SELECTED, switched.status)
        self.assertEqual(ClearStatus.NOT_SELECTED, queued.status)
        self.assertFalse(switched.ok)

    def _recognized_noop_reader(self):
        state = {"n": 0}

        def reader(_path):
            state["n"] += 1
            if state["n"] == 1:
                return []
            return ["[1.0] [DEBUG] [BTN] Button press: Loop 4 (long)"]

        return reader

    def _run_recognized_noop(self, **kwargs):
        from unittest.mock import patch
        nows = iter([0.0, 0.0, 0.0, 2.0, 2.0, 2.0, 9.0])
        with patch("hitl.actions.slots._send_long_press"):
            return stopped_preparation.clear_slot(
                _Ctx(FakePort()), self.log, track_index0=7, slot0=3, long_press_ms=900,
                noop_settle_s=1.0, timeout_s=5.0, lines_reader=self._recognized_noop_reader(),
                sleep=lambda _s: None, now=lambda: next(nows, 9.0), **kwargs)

    def test_a_recognized_long_press_without_evidence_is_unconfirmed(self):
        # Recognition is not completion: the firmware acknowledges the gesture before
        # deciding, so a silent slot could be a lost refusal or switch. Elapsed time
        # alone must never be reported as a successful preparation.
        outcome = self._run_recognized_noop()
        self.assertEqual(ClearStatus.UNCONFIRMED, outcome.status)
        self.assertFalse(outcome.ok)

    def test_a_recognized_long_press_with_confirmed_emptiness_is_the_empty_noop(self):
        outcome = self._run_recognized_noop(emptiness_reader=lambda: True)
        self.assertEqual(ClearStatus.ALREADY_EMPTY, outcome.status)
        self.assertTrue(outcome.ok)

    def test_a_recognized_long_press_on_a_populated_target_is_unconfirmed(self):
        outcome = self._run_recognized_noop(emptiness_reader=lambda: False)
        self.assertEqual(ClearStatus.UNCONFIRMED, outcome.status)
        self.assertFalse(outcome.ok)

    def test_a_recognized_long_press_with_unavailable_emptiness_is_unconfirmed(self):
        outcome = self._run_recognized_noop(emptiness_reader=lambda: None)
        self.assertEqual(ClearStatus.UNCONFIRMED, outcome.status)
        self.assertFalse(outcome.ok)

    def test_a_short_press_is_rejected_as_a_different_gesture(self):
        with self.assertRaises(ValueError):
            stopped_preparation.clear_slot(
                _Ctx(FakePort()), self.log, track_index0=7, slot0=3, long_press_ms=120,
                timeout_s=1.0, lines_reader=lambda _p: [], sleep=lambda _s: None)

    def test_clear_slot_reports_unconfirmed_when_the_timeout_expires(self):
        nows = iter([0.0, 9.0])
        outcome = stopped_preparation.clear_slot(
            _Ctx(FakePort()), self.log, track_index0=7, slot0=3, long_press_ms=900,
            timeout_s=1.0, lines_reader=lambda _p: [], sleep=lambda _s: None,
            now=lambda: next(nows, 9.0))
        self.assertEqual(ClearStatus.UNCONFIRMED, outcome.status)


class PreparationIntegrationTests(unittest.TestCase):
    """The reproducer must clear each authorized target before it seeds that target."""

    def test_preparation_failure_stops_before_any_record_gesture(self):
        import contextlib
        import json
        import tempfile
        from types import SimpleNamespace
        from unittest.mock import patch
        from hitl_accepted_sequence import main

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            log = root / "session.log"
            log.write_text("")
            capture = SimpleNamespace(
                session_log=log,
                # A real census channel: the target is filled, so the flow reaches the clear.
                send_command=lambda *a, **k: (True, "queued"),
                library_observation=lambda *a, **k: SimpleNamespace(closed=True, rows=[]))
            shutdown = SimpleNamespace(result=SimpleNamespace(
                ok=True, as_dict=lambda: {"status": "complete"}))

            @contextlib.contextmanager
            def managed(**kw):
                yield capture

            @contextlib.contextmanager
            def device(**kw):
                yield shutdown

            refused = stopped_preparation.ClearOutcome(4, ClearStatus.REFUSED, "save still owed")
            with contextlib.ExitStack() as stack:
                def mock(name, **kw):
                    return stack.enter_context(patch(_patch_target(name), **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                record_press = mock("slots.press_slot_record",
                                    side_effect=answering_presses(log))
                mock("stopped_preparation.clear_slot", return_value=refused)
                mock("observation_window", return_value=({(7, 3): True, (7, 4): True}, True))
                mock("_ensure_transport", return_value=True)
                mock("_wait_for", return_value=True); mock("_await_markers", return_value=True)
                mock("_run_plan", return_value="DROVE_LEG")
                mock("time.sleep")
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "rec", "--track", "8", "--slot-a", "4",
                           "--slot-b", "5", "--notes", "1", "--no-upload",
                           "--out-dir", str(out)])

            self.assertEqual(3, rc)
            report = json.loads((out / "accepted-sequence.json").read_text())
            # The refused clear is the target's own failure: the only press is the selection that
            # precedes it, and no seed (arm) press follows.
            self.assertEqual(1, record_press.call_count)
            self.assertIn("prepare_slot_a_selected", [e.get("event") for e in report["events"]])
            self.assertNotIn("seed_a_press", [e.get("event") for e in report["events"]])
            self.assertEqual(3, rc)
            report = json.loads((out / "accepted-sequence.json").read_text())
            self.assertEqual("PREPARATION_FAILED", report["result"])
            self.assertTrue(report["clear_targets"])
            self.assertEqual([{"slot": 4, "status": "REFUSED", "detail": "save still owed"}],
                             report["preparation"])
            self.assertIn("scoped clear REFUSED", report["preparation_error"])

    def test_each_seeded_target_is_cleared_first(self):
        import contextlib
        import json
        import tempfile
        from types import SimpleNamespace
        from unittest.mock import patch
        from hitl_accepted_sequence import main

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            log = root / "session.log"
            log.write_text("")
            capture = SimpleNamespace(
                session_log=log,
                # A real census channel: both targets are filled, so each seed reaches its clear.
                send_command=lambda *a, **k: (True, "queued"),
                library_observation=lambda *a, **k: SimpleNamespace(closed=True, rows=[]))
            shutdown = SimpleNamespace(result=SimpleNamespace(
                ok=True, as_dict=lambda: {"status": "complete"}))

            @contextlib.contextmanager
            def managed(**kw):
                yield capture

            @contextlib.contextmanager
            def device(**kw):
                yield shutdown

            cleared = stopped_preparation.ClearOutcome(0, ClearStatus.CLEARED, "ok")
            with contextlib.ExitStack() as stack:
                def mock(name, **kw):
                    return stack.enter_context(patch(_patch_target(name), **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                mock("slots.press_slot_record", side_effect=answering_presses(log))
                clear = mock("stopped_preparation.clear_slot", return_value=cleared)
                mock("observation_window", return_value=({(7, 3): True, (7, 4): True}, True))
                mock("_ensure_transport", return_value=True)
                mock("_wait_for", return_value=True); mock("_await_markers", return_value=True)
                mock("_run_plan", return_value="DROVE_LEG")
                mock("time.sleep")
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "od", "--track", "8", "--slot-a", "4",
                           "--slot-b", "5", "--notes", "1", "--no-upload",
                           "--out-dir", str(out)])

            self.assertEqual(0, rc)
            # B before A, as firmware 0-based rows.
            self.assertEqual([4, 3], [call.kwargs["slot0"] for call in clear.call_args_list])
            report = json.loads((out / "accepted-sequence.json").read_text())
            self.assertEqual("DROVE_LEG", report["result"])
            self.assertEqual(2, len(report["preparation"]))


class CensusPreparationTests(unittest.TestCase):
    """The read-only census decides whether a clear is needed; unknown never means empty."""

    def family(self, *slots):
        """`slots` are `(track, slot, has_content, payload_code)` (payload 1=absent, 3=present)."""
        lines = ["#CAP,1,LIBOBS,B,a=1,e=0,s=64"]
        for track, slot, has, payload in slots:
            lines.append(self.slot_row(track, slot, has, payload))
        lines.append("#CAP,3,LIBOBS,E,a=1,b=1,t=1,r=0,s=1,P=1,R=1,x=1")
        return lines

    @staticmethod
    def slot_row(track, slot, has, payload, *, include_payload=True, include_content=True):
        parts = [f"t={track}", f"s={slot}", "st=2", "c=n", "o=2", "q=5", "w=0", "m=1",
                 "g=0", "e=0"]
        if include_payload:
            parts.append(f"p={payload}")
        if include_content:
            parts += [f"k={has}", f"kp={has}", f"kr={has}", f"kc={has}"]
        parts += ["a=0", "h=0", "l=0", "v=0", "n=0", "i=0", "x=0"]
        return "#CAP,2,LIBOBS,S," + ",".join(parts)

    def test_occupancy_uses_both_content_representations(self):
        # RAM-only, SD-only (verified payload with k=0), both, and neither.
        occupancy = occupied_slots(self.family((7, 3, 1, 1), (7, 4, 0, 3),
                                               (7, 5, 1, 3), (7, 6, 0, 1)))
        self.assertEqual({(7, 3): True, (7, 4): True, (7, 5): True, (7, 6): False}, occupancy)

    def test_an_sd_only_slot_is_populated_not_empty(self):
        # The defect: a verified SD payload with no loaded content read as empty, so
        # preparation skipped the clear and then seeded into occupied content.
        self.assertIs(True, occupied_slots(self.family((7, 3, 0, 3)))[(7, 3)])

    def test_unavailable_evidence_is_unknown_never_empty(self):
        lines = ["#CAP,1,LIBOBS,B,a=1,e=0,s=64",
                 self.slot_row(7, 3, 0, 3, include_payload=False),   # SD evidence absent
                 self.slot_row(7, 4, 0, 0),                          # payload token unknown
                 self.slot_row(7, 5, 0, 1, include_content=False),   # RAM evidence absent
                 "#CAP,3,LIBOBS,E,a=1,b=1,t=1,r=0,s=1,P=1,R=1,x=1"]
        self.assertEqual({(7, 3): None, (7, 4): None, (7, 5): None}, occupied_slots(lines))

    def test_a_lost_window_is_unknown_not_empty(self):
        self.assertIsNone(occupied_slots(self.family((7, 3, 1, 1))[:-1]))
        self.assertEqual((None, None), observation_window(self.family((7, 3, 1, 1))[:-1]))

    def test_an_unsettled_sweep_still_yields_occupancy_with_its_flag(self):
        # The device flags a multi-turn sweep as not settled (x=0) while every slot row is
        # present; preparation may use the occupancy, a settled census must not.
        lines = self.family((7, 3, 1, 1), (7, 4, 0, 1))
        lines = [line.replace("P=1,R=1,x=1", "P=1,R=1,x=0") for line in lines]
        occupancy, settled = observation_window(lines)
        self.assertEqual({(7, 3): True, (7, 4): False}, occupancy)
        self.assertFalse(settled)

    def test_a_census_that_says_empty_skips_the_clear_gesture(self):
        import contextlib, json, tempfile
        from types import SimpleNamespace
        from unittest.mock import patch
        from hitl_accepted_sequence import main
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); log = root / "session.log"; log.write_text("")
            capture = SimpleNamespace(
                session_log=log,
                send_command=lambda *a, **k: (True, "queued"),
                library_observation=lambda *a, **k: SimpleNamespace(closed=True, rows=[]))
            shutdown = SimpleNamespace(result=SimpleNamespace(ok=True, as_dict=lambda: {"status": "complete"}))
            @contextlib.contextmanager
            def managed(**kw): yield capture
            @contextlib.contextmanager
            def device(**kw): yield shutdown
            with contextlib.ExitStack() as stack:
                def mock(name, **kw): return stack.enter_context(patch(_patch_target(name), **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                mock("slots.press_slot_record", side_effect=answering_presses(log))
                clear = mock("stopped_preparation.clear_slot")
                mock("observation_window", return_value=({(7, 3): False, (7, 4): False}, True))
                mock("_ensure_transport", return_value=True); mock("_wait_for", return_value=True); mock("_await_markers", return_value=True)
                mock("_run_plan", return_value="DROVE_LEG"); mock("time.sleep")
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5",
                           "--notes", "1", "--no-upload", "--out-dir", str(out)])
            self.assertEqual(0, clear.call_count)
            report = json.loads((out / "accepted-sequence.json").read_text())
            self.assertEqual(0, rc)
            self.assertTrue(all(p["status"] == "ALREADY_EMPTY" for p in report["preparation"]))
            self.assertEqual({f"{t}:{s}": v for (t, s), v in {(7, 3): False, (7, 4): False}.items()},
                             report["census"])

    def test_a_filled_target_is_selected_before_the_clear(self):
        import contextlib, tempfile
        from types import SimpleNamespace
        from unittest.mock import patch
        from hitl_accepted_sequence import main
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); log = root / "session.log"; log.write_text("")
            capture = SimpleNamespace(
                session_log=log,
                send_command=lambda *a, **k: (True, "queued"),
                library_observation=lambda *a, **k: SimpleNamespace(closed=True, rows=[]))
            shutdown = SimpleNamespace(result=SimpleNamespace(ok=True, as_dict=lambda: {"status": "complete"}))
            @contextlib.contextmanager
            def managed(**kw): yield capture
            @contextlib.contextmanager
            def device(**kw): yield shutdown
            cleared = stopped_preparation.ClearOutcome(0, ClearStatus.CLEARED, "ok")
            with contextlib.ExitStack() as stack:
                def mock(name, **kw): return stack.enter_context(patch(_patch_target(name), **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                select = mock("slots.press_slot_record", side_effect=answering_presses(log))
                mock("stopped_preparation.clear_slot", return_value=cleared)
                # Both targets are filled: neither may be long-pressed before it is selected.
                mock("observation_window", return_value=({(7, 3): True, (7, 4): True}, True))
                mock("_ensure_transport", return_value=True); mock("_wait_for", return_value=True); mock("_await_markers", return_value=True)
                mock("_run_plan", return_value="DROVE_LEG"); mock("time.sleep")
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "rec", "--track", "8", "--slot-a", "4", "--slot-b", "5",
                           "--notes", "1", "--no-upload", "--out-dir", str(out)])
            self.assertEqual(0, rc)
            self.assertGreaterEqual(select.call_count, 1)


class ManagedRunTests(unittest.TestCase):
    def test_aborted_leg_writes_report_after_shutdown_and_seeds_b_before_a(self):
        import contextlib
        import json
        import tempfile
        from types import SimpleNamespace
        from unittest.mock import patch
        from hitl_accepted_sequence import main

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            log = root / "session.log"
            log.write_text("")
            capture = SimpleNamespace(
                session_log=log,
                # No clear is requested here (`--no-clear-targets`), so no census is read.
                send_command=lambda *a, **k: (False, "no census requested in this fixture"),
                library_observation=lambda *a, **k: None)
            shutdown = SimpleNamespace(result=SimpleNamespace(
                ok=True, as_dict=lambda: {"status": "complete"}))
            exits = []
            @contextlib.contextmanager
            def managed(**kw):
                yield capture
                exits.append("capture")
            @contextlib.contextmanager
            def device(**kw):
                yield shutdown
                exits.append("shutdown")
            with contextlib.ExitStack() as stack:
                def mock(name, **kw):
                    return stack.enter_context(patch(_patch_target(name), **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                select = mock("slots.press_slot_record", side_effect=answering_presses(log))
                mock("_ensure_transport", return_value=True)
                mock("_wait_for", return_value=True); mock("_await_markers", return_value=True)
                mock("time.sleep")
                mock("_run_plan", return_value="UNEXPECTED_PLAYBACK_SWITCH")
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "od", "--track", "8", "--slot-a", "4",
                           "--slot-b", "5", "--notes", "1", "--no-upload", "--no-clear-targets",
                           "--out-dir", str(out)])
            self.assertEqual([5, 4], [call.args[1] for call in select.call_args_list])
            self.assertEqual(["shutdown", "capture"], exits)
            self.assertEqual(3, rc)
            report = json.loads((out / "accepted-sequence.json").read_text())
            self.assertEqual("UNEXPECTED_PLAYBACK_SWITCH", report["result"])
            self.assertEqual("complete", report["shutdown"]["status"])


    def test_a_cancelled_run_reports_the_cancel_and_sends_nothing_after_it(self):
        """The requirement's order: cancel, no further test gesture, teardown, then the capture is released."""
        import contextlib
        import json
        import tempfile
        from types import SimpleNamespace
        from unittest.mock import patch

        import mido

        from hitl.actions.cancellation import active_cancel_scope, cancel_active_run
        from hitl_accepted_sequence import main

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            log = root / "session.log"
            log.write_text("")
            capture = SimpleNamespace(
                session_log=log,
                send_command=lambda *a, **k: (False, "no census requested in this fixture"),
                library_observation=lambda *a, **k: None)
            shutdown = SimpleNamespace(result=SimpleNamespace(
                ok=True, as_dict=lambda: {"status": "complete"}))
            exits = []

            @contextlib.contextmanager
            def managed(**kw):
                try:
                    yield capture
                finally:
                    exits.append("capture")

            @contextlib.contextmanager
            def device(**kw):
                try:
                    yield shutdown
                finally:
                    # The real `DeviceSession.__exit__` drains on *every* exit, including the cancellation
                    # unwinding through it; the fixture records it the same way.
                    exits.append("shutdown")

            port = FakePort()
            edges_before_cancel: list[int] = []

            with contextlib.ExitStack() as stack:
                def mock(name, **kw):
                    return stack.enter_context(patch(_patch_target(name), **kw))

                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=port)
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                mock("slots.press_slot_record", side_effect=answering_presses(log))
                mock("_ensure_transport", return_value=True)
                mock("_wait_for", return_value=True); mock("_await_markers", return_value=True)
                mock("time.sleep")

                def cancelled_plan(out_port, *_a, **_k):
                    # The leg's first gesture is where the watchdog fires: the run is cancelled and the plan
                    # then tries to press anyway, which must leave nothing on the wire.
                    cancel_active_run("managed run watchdog 1.0s")
                    edges_before_cancel.append(len(port.sent))
                    out_port.send(mido.Message("note_on", note=36, channel=15, velocity=100))
                    return "DROVE_LEG"

                mock("_run_plan", side_effect=cancelled_plan)
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "od", "--track", "8", "--slot-a", "4",
                           "--slot-b", "5", "--notes", "1", "--no-upload", "--no-clear-targets",
                           "--out-dir", str(out)])
            report = json.loads((out / "accepted-sequence.json").read_text())
            self.assertEqual(3, rc, "a cancelled run is not a success")
            self.assertEqual("CANCELLED", report["result"])
            self.assertIn("watchdog", report["cancel_reason"])
            after = port.sent[edges_before_cancel[0]:]
            self.assertEqual(report["midi"]["release_edges"], len(after),
                             "only the guard's own note release went out after the cancel")
            self.assertNotIn("note_on", [message[0] for message in after],
                             "the cancelled run put no further test edge on the wire")
            self.assertGreaterEqual(report["midi"]["refused_after_cancel"], 1)
            self.assertEqual("complete", report["shutdown"]["status"], "the teardown still ran")
            self.assertEqual(["shutdown", "capture"], exits,
                             "the capture is released only after the run stopped and drained")
            self.assertIsNone(active_cancel_scope(), "the run is unregistered when it ends")

    def test_a_note_held_when_the_cancel_lands_is_released(self):
        """A cancel between a `note_on` and its `note_off` leaves a sounding note; the guard clears it."""
        import contextlib
        import json
        import tempfile
        from types import SimpleNamespace
        from unittest.mock import patch

        from hitl.actions.cancellation import cancel_active_run
        from hitl_accepted_sequence import main

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            log = root / "session.log"
            log.write_text("")
            capture = SimpleNamespace(
                session_log=log,
                send_command=lambda *a, **k: (False, "no census requested in this fixture"),
                library_observation=lambda *a, **k: None)
            shutdown = SimpleNamespace(result=SimpleNamespace(
                ok=True, as_dict=lambda: {"status": "complete"}))

            @contextlib.contextmanager
            def managed(**kw):
                yield capture

            @contextlib.contextmanager
            def device(**kw):
                yield shutdown

            port = FakePort()
            cancelled = {"done": False}

            def cancel_mid_press(seconds):
                """Land the cancel between a press's note-on and its note-off."""
                if not cancelled["done"]:
                    cancelled["done"] = True
                    cancel_active_run("managed run watchdog 1.0s")

            with contextlib.ExitStack() as stack:
                def mock(name, **kw):
                    return stack.enter_context(patch(_patch_target(name), **kw))

                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=port)
                mock("_find_midi_port", return_value="fake")
                stack.enter_context(patch("hitl.midi_io.time.sleep", side_effect=cancel_mid_press))
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "od", "--track", "8", "--slot-a", "4",
                           "--slot-b", "5", "--no-upload", "--no-clear-targets",
                           "--out-dir", str(out)])
            report = json.loads((out / "accepted-sequence.json").read_text())
            kinds = [message[0] for message in port.sent]
            self.assertEqual(3, rc)
            self.assertEqual("CANCELLED", report["result"])
            self.assertIn("note_on", kinds, "the press began")
            self.assertIn("note_off", kinds[-3:], "the held note was released")
            self.assertEqual(3, report["midi"]["released_notes"],
                             "the held note plus one channel silence")
            self.assertEqual("complete", report["shutdown"]["status"])


class EvidenceGateTests(unittest.TestCase):
    """Every gesture is gated on the device's own slot-scoped evidence.

    Three measured device shapes drive this class (`captures/s4f22-dec096-{od-02,rec-01,mixed-01}`):
    a lost census turned a long press on a filled, non-selected target into a *playback start*; two taps of
    one button 0 ms apart merged into `Loop 3 (double)` = `OVERDUB_FOR_SLOT`; and two presses that a
    predecessor's in-flight Record job swallowed produced a leg with no take at all -- and, because the abort
    raised, no report either.
    """

    def _drive(self, argv, *, census=None, press=None, clear=None, log_lines=(),
               census_readable=True, run_plan="DROVE_LEG", real_ensure_transport=False,
               crash_expected=False, leg_rows=None, final_stop_row=None):
        """Run `main` against a fake managed capture; returns the report plus what the wire saw.

        `press(log, slot_number, attempt)` models the device's answer to a loop-button press: the driver's
        gates read `DISP` rows out of `log`, so a press that writes none is a *swallowed* press. The clock is
        faked (sleep advances it) so a bounded wait costs nothing and the press *spacing* stays observable.
        """
        import contextlib
        import json
        import tempfile
        from types import SimpleNamespace
        from unittest.mock import patch
        from hitl_accepted_sequence import main

        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        log = root / "session.log"
        log.write_text("".join(line + "\n" for line in log_lines))
        occupancy = {(7, 3): True, (7, 4): True} if census is None else dict(census)
        capture = SimpleNamespace(
            session_log=log,
            send_command=(lambda *a, **k: (True, "queued")) if census_readable
            else (lambda *a, **k: (False, "census channel refused in this fixture")),
            library_observation=(lambda *a, **k: SimpleNamespace(closed=True, rows=[])) if census_readable
            else (lambda *a, **k: None))
        shutdown = SimpleNamespace(result=SimpleNamespace(
            ok=True, as_dict=lambda: {"status": "complete"}))
        port = FakePort()
        clock = {"t": 0.0}
        presses: list[tuple[int, float]] = []

        def record_press(_ctx, slot_number, press_ms=None):
            presses.append((slot_number, clock["t"]))
            if press is not None:
                press(log, slot_number, len(presses))

        @contextlib.contextmanager
        def managed(**kw):
            yield capture

        @contextlib.contextmanager
        def device(**kw):
            yield shutdown

        stack = contextlib.ExitStack()
        self.addCleanup(stack.close)

        def mock(name, **kw):
            return stack.enter_context(patch(_patch_target(name), **kw))

        mock("ManagedCaptureSession", side_effect=managed)
        mock("DeviceSession", side_effect=device)
        mock("mido.open_output", return_value=port)
        mock("_find_midi_port", return_value="fake")
        mock("slots.press_track")
        mock("slots.press_slot_record", side_effect=record_press)
        clear_mock = mock("stopped_preparation.clear_slot", return_value=(
            clear if clear is not None else stopped_preparation.ClearOutcome(0, ClearStatus.CLEARED, "ok")))
        mock("observation_window", return_value=(occupancy, True))

        def write_row(row):
            if row:
                with log.open("a", encoding="utf-8") as handle:
                    handle.write(row + "\n")

        def ensure_transport(*a, **k):
            # The run's own final stop: the device's `Transport stopped` row, which must land *outside* the
            # measured window, because the window ends when the plan returned.
            if k.get("label") == "final":
                write_row(final_stop_row)
            return True

        if not real_ensure_transport:
            mock("_ensure_transport", side_effect=ensure_transport)
        mock("_wait_for", return_value=True); mock("_await_markers", return_value=True)
        if leg_rows is None:
            mock("_run_plan", return_value=run_plan)
        else:
            # Rows the leg's own plan produced: what a measured window has to contain.
            mock("_run_plan", side_effect=lambda *a, **k: (write_row(leg_rows), run_plan)[1])
        stack.enter_context(patch("time.monotonic", lambda: clock["t"]))
        stack.enter_context(
            patch("time.sleep", lambda seconds: clock.__setitem__("t", clock["t"] + max(seconds, 0.0))))
        stack.enter_context(patch("builtins.print"))
        out = root / "run"
        raised = None
        rc = None
        try:
            rc = main(list(argv) + ["--no-upload", "--out-dir", str(out)])
        except Exception as exc:  # a run that dies mid-leg must still have written its report (od-08)
            if not crash_expected:
                raise
            raised = exc
        report_path = out / "accepted-sequence.json"
        report = json.loads(report_path.read_text()) if report_path.exists() else None
        return SimpleNamespace(rc=rc, report=report, raised=raised, presses=presses, clear=clear_mock,
                               port=port, log=log, out=out,
                               events=[] if report is None else [e.get("event") for e in report["events"]])

    def test_an_unavailable_census_refuses_before_any_gesture(self):
        """`od-02`: a lost census used to leave the driver long-pressing an unknown target."""
        fixture = self._drive(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1"],
                              census_readable=False)
        self.assertEqual([], fixture.presses, "no loop button may be pressed on an unknown target")
        self.assertEqual(0, fixture.clear.call_count, "no clear may be pressed on an unknown target")
        self.assertEqual(3, fixture.rc)
        self.assertEqual("PREPARATION_FAILED", fixture.report["result"])
        self.assertIsNone(fixture.report["census"])
        self.assertEqual([], fixture.report["preparation"])
        self.assertIn("census unavailable for slot 5", fixture.report["preparation_error"])
        self.assertIn("Refusing before any gesture", fixture.report["preparation_error"])
        self.assertIn("no observation completed", fixture.report["preparation_error"])
        self.assertIn("prepare_slot_b_census_unavailable", fixture.events)
        # One retry per remaining attempt, and the transport is never touched.
        self.assertEqual(DEFAULT_CENSUS_ATTEMPTS - 1, fixture.events.count("census_retry"))
        self.assertNotIn(GLOBAL_TRANSPORT_NOTE, fixture.port.note_sequence())

    def test_a_swallowed_selection_press_is_named_and_never_pressed_again(self):
        """`mixed-01`: a predecessor's in-flight stop holds the selection, so the press leaves no row."""
        fixture = self._drive(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1",
                               "--arm-timeout-s", "0.0"], press=None)
        self.assertEqual(1, len(fixture.presses), "a silent selection is not a reason to press again")
        self.assertEqual(0, fixture.clear.call_count,
                         "a long press on a filled, non-selected slot starts playback, not a clear")
        self.assertEqual(3, fixture.rc)
        self.assertEqual("PREPARATION_FAILED", fixture.report["result"])
        self.assertIn("prepare_slot_b_selected", fixture.events)
        self.assertIn("was not selected by its own press", fixture.report["preparation_error"])
        self.assertNotIn(GLOBAL_TRANSPORT_NOTE, fixture.port.note_sequence())

    def test_a_swallowed_seed_press_never_starts_the_transport(self):
        """The mixed leg's seed press was swallowed; the old fallback started the transport anyway."""
        fixture = self._drive(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1",
                               "--arm-timeout-s", "0.0"], census={(7, 4): False}, press=None,
                              real_ensure_transport=True)
        self.assertEqual(DEFAULT_SEED_PRESS_ATTEMPTS, len(fixture.presses),
                         "a bounded number of gated presses, then a named failure")
        self.assertEqual(3, fixture.rc)
        self.assertEqual("PREPARATION_FAILED", fixture.report["result"])
        self.assertIn("never armed", fixture.report["preparation_error"])
        self.assertEqual([False] * DEFAULT_SEED_PRESS_ATTEMPTS,
                         [e["armed"] for e in fixture.report["events"]
                          if e.get("event") == "seed_b_press"])
        self.assertNotIn(GLOBAL_TRANSPORT_NOTE, fixture.port.note_sequence(),
                         "starting the transport records nothing and hides the absent take")
        gap = fixture.presses[1][1] - fixture.presses[0][1]
        self.assertGreaterEqual(gap, DEFAULT_TAP_WINDOW_S - 1e-6,
                                "a retry inside the tap window merges into Loop N (double)")

    def test_a_target_the_clear_already_armed_is_not_pressed_again(self):
        """`od-10`: the clear's selection press arms an empty target, so a seed press toggles it off.

        The device then answers that press with `Loop 5: Stop Recording` and the take it had already started
        is thrown away, which is what the run reported as `slot 5 never armed`.
        """
        clock = {"t": 1.0}

        def press(log: Path, slot_number: int, attempt: int) -> None:
            # The selection press that precedes the clear: it selects, arms this empty target and starts it.
            # The clock advances per press, as the device's own does.
            clock["t"] += 1.0
            with log.open("a", encoding="utf-8") as handle:
                handle.write(disp_row(slot_number, "ARMED", stamp_us=int(clock["t"] * 1e6)) + "\n")
                handle.write(f"[{clock['t']:.3f}] [INFO] Loop {slot_number}: Start Recording\n")
                handle.write(f"[{clock['t'] + 0.1:.3f}] [DEBUG] [TRACK] Recording started @ tick 0 "
                             "(startLoopTick=0 loopStart=0)\n")

        fixture = self._drive(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1",
                               "--arm-timeout-s", "0.0"], census={(7, 3): False, (7, 4): True}, press=press)
        self.assertIn("seed_b_already_ready", fixture.events)
        self.assertNotIn("seed_b_press", fixture.events)
        self.assertEqual(0, fixture.rc, fixture.report.get("preparation_error"))
        self.assertEqual("DROVE_LEG", fixture.report["result"])

    def test_a_retry_after_a_swallowed_press_arms_on_its_own_slot_row(self):
        """The working two-press pattern: the first press only moves focus, the second arms."""
        def press(log: Path, slot_number: int, attempt: int) -> None:
            if attempt == 1:
                return
            with log.open("a", encoding="utf-8") as handle:
                handle.write(disp_row(slot_number, "ARMED") + "\n")
                handle.write(disp_row(slot_number, "RECORDING") + "\n")

        fixture = self._drive(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1"],
                              census={(7, 3): False, (7, 4): False}, press=press)
        self.assertEqual(0, fixture.rc)
        self.assertEqual("DROVE_LEG", fixture.report["result"])
        self.assertEqual([False, True],
                         [e["armed"] for e in fixture.report["events"] if e.get("event") == "seed_b_press"])
        self.assertTrue(any(",DISP,4,ARMED," in line for line in fixture.log.read_text().splitlines()))

    def test_the_printed_arm_row_is_a_receipt_when_the_ring_has_not_flushed(self):
        """`od-10`: the take started at 30.160 s while its `DISP` rows only became readable at 35.864 s.

        The firmware's own logger rows go straight to `Serial`, so the arm is visible *before* the capture
        ring is flushed. A `DISP`-only gate pressed the same button again -- which the firmware reads as
        *Stop Recording* -- and refused the seed the device had actually made.
        """
        clock = {"t": 1.0}

        def press(log: Path, slot_number: int, attempt: int) -> None:
            clock["t"] += 1.0
            with log.open("a", encoding="utf-8") as handle:
                handle.write(f"[{clock['t']:.3f}] [INFO] Loop {slot_number}: Start Recording\n")
                handle.write(f"[{clock['t'] + 0.1:.3f}] [DEBUG] [TRACK] Recording started @ tick 0 "
                             "(startLoopTick=0 loopStart=0)\n")

        fixture = self._drive(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1"],
                              census={(7, 3): False, (7, 4): False}, press=press)
        self.assertEqual(0, fixture.rc, fixture.report.get("preparation_error"))
        self.assertEqual("DROVE_LEG", fixture.report["result"])
        self.assertEqual([True],
                         [e["armed"] for e in fixture.report["events"] if e.get("event") == "seed_b_press"])
        self.assertFalse(any(",DISP,4,ARMED," in line for line in fixture.log.read_text().splitlines()),
                         "the fixture writes no DISP row at all: the printed row is the receipt")

    def test_a_run_that_dies_after_the_census_still_leaves_a_report(self):
        """`od-08`: the run died inside the census decode and left **no** report at all.

        The flow's milestone hook is what prevents that: the census state and the preparation outcome are
        persisted while the leg is still running, so a crash after them is still explained by the report.
        """
        class _Boom(RuntimeError):
            pass

        def exploding_press(log: Path, slot_number: int, attempt: int) -> None:
            raise _Boom("device session died mid-leg")

        fixture = self._drive(["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1"],
                              census={(7, 3): True, (7, 4): False}, press=exploding_press,
                              crash_expected=True)
        self.assertIsInstance(fixture.raised, _Boom)
        self.assertIsNotNone(fixture.report, "the milestone report must exist before the leg finishes")
        self.assertEqual({"7:3": True, "7:4": False}, fixture.report["census"])
        self.assertEqual("INCOMPLETE", fixture.report["result"],
                         "the milestone records what is known, not a verdict")

    def test_a_row_from_before_the_gesture_cannot_confirm_it(self):
        """The floor is the device clock observed before the gesture: an older row is never its receipt."""
        with __import__("tempfile").TemporaryDirectory() as temp:
            log = Path(temp) / "session.log"
            log.write_text("\n".join([
                "[1.000] [INFO] Loop 5: Start Recording",          # the last row before the gesture: 1.000 s
                disp_row(3, "ARMED", stamp_us=900_000),            # stamped before it -> not a receipt
                disp_row(5, "ARMED", stamp_us=1_500_000),          # after it, but slot 5's, not the target's
            ]) + "\n")
            self.assertIsNone(_await_slot_disp(log, 1, 2, ("ARMED",), 0.0),
                              "a row emitted before the gesture must not confirm it")
            self.assertIsNotNone(_await_slot_disp(log, 1, 4, ("ARMED",), 0.0))
            self.assertIsNone(_await_slot_disp(log, 1, 4, ("RECORDING",), 0.0))

    def test_a_flooded_row_is_current_whatever_order_the_ring_flushed_it_in(self):
        """`od-03`/`od-04`: the take's RECORDING flood lands in the file *after* rows stamped 30 s later."""
        with __import__("tempfile").TemporaryDirectory() as temp:
            log = Path(temp) / "session.log"
            log.write_text("\n".join([
                "[55.218] [INFO] Button press: Loop 5 (long)",       # the last row before the gesture
                disp_row(5, "ARMED", stamp_us=54_091_000),           # before it -> not this gesture's
                "#CAP,85981327,DISP,4,STOPPED,9984,0,0",             # a *later* row, flushed first
                disp_row(5, "RECORDING", stamp_us=58_682_000),       # the receipt, later in file order too
            ]) + "\n")
            self.assertIsNotNone(_await_slot_disp(log, 1, 4, ("RECORDING",), 0.0),
                                 "an order-based freshness filter drops this row; its stamp is what counts")
            self.assertIsNone(_await_slot_disp(log, 1, 4, ("ARMED",), 0.0),
                              "the pre-gesture ARMED row must not satisfy the arm gate")

    def test_a_lost_census_window_is_retried_before_the_target_is_undecidable(self):
        """One lost window is a delivery outcome: `od-02` answered on the retry."""
        from types import SimpleNamespace

        rows = ["#CAP,1,LIBOBS,B,a=1,e=0,s=64",
                "#CAP,2,LIBOBS,S,t=7,s=3,st=2,c=n,o=2,q=5,w=0,m=1,g=0,e=0,p=1,"
                "k=1,kp=1,kr=1,kc=1,a=0,h=0,l=0,v=0,n=0,i=0,x=0",
                "#CAP,3,LIBOBS,E,a=1,b=1,t=1,r=0,s=1,P=1,R=1,x=1"]
        with __import__("tempfile").TemporaryDirectory() as temp:
            log = Path(temp) / "session.log"
            log.write_text("")
            answers = [None, SimpleNamespace(rows=rows)]
            capture = SimpleNamespace(send_command=lambda *a, **k: (True, "queued"),
                                      library_observation=lambda *a, **k: answers.pop(0))
            events: list[dict] = []
            occupied = _run_census(capture, log, events, attempts=3, retry_delay_s=0.0)
            self.assertEqual({(7, 3): True}, occupied)
            self.assertEqual(["census_lost", "census_retry", "census"], [e["event"] for e in events])
            # Every attempt failing is still None -- never an empty library.
            capture.library_observation = lambda *a, **k: None
            events.clear()
            self.assertIsNone(_run_census(capture, log, events, attempts=3, retry_delay_s=0.0))
            self.assertIsNone(events[-1].get("census"))

    def test_an_unreadable_census_family_is_named_and_retried_never_a_crash(self):
        """`od-08`: the capture log is read while it is written, so a partial `LIBOBS,E` row can be parsed."""
        from types import SimpleNamespace
        # The row as it appeared mid-write on device: the `p=` value had not arrived yet when the poll read it.
        truncated = ["#CAP,1,LIBOBS,B,a=1,e=0,s=64",
                     "#CAP,10545846,LIBOBS,E,a=10362645,b=10545829,t=64,r=0,s=64,p=,i=8,c=0,m=0,"
                     "P=0,R=0,n=0,e=1,v=0,x=0"]
        with __import__("tempfile").TemporaryDirectory() as temp:
            log = Path(temp) / "session.log"
            log.write_text("")
            capture = SimpleNamespace(send_command=lambda *a, **k: (True, "queued"),
                                      library_observation=lambda *a, **k: SimpleNamespace(rows=truncated))
            events: list[dict] = []
            self.assertIsNone(_run_census(capture, log, events, attempts=2, retry_delay_s=0.0))
            self.assertEqual(["census_unreadable", "census_retry", "census_unreadable"],
                             [e["event"] for e in events])
            self.assertIn("p=", events[-1]["tail"][-1])
            # And a target that stays undecidable is still refused, never read as empty.
            self.assertIsNone(events[-1].get("census"))

    def test_the_measured_window_ends_at_the_plan_not_at_the_runs_own_stop(self):
        """The window's end is the *plan's* return, before the settle and the run's own final stop.

        Load-bearing on the device: in `captures/s4f22-dec096-od-13` the driver's final stop lands ~8 s (and
        thousands of device rows) after the leg's last press, so a mark taken once the leg had returned would
        put the run's own teardown *inside* every window and fail `continuous_transport` on a conforming run.
        """
        def press(log, slot_number, attempt):
            # The seed presses answer on their own slot's display row, as the device does.
            with log.open("a", encoding="utf-8") as handle:
                for state in ("ARMED", "RECORDING"):
                    handle.write(disp_row(slot_number, state) + "\n")

        fixture = self._drive(
            ["--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5", "--notes", "1"],
            census={(7, 3): False, (7, 4): False}, press=press,
            log_lines=["Logger initialized with level: 3", "BOOT,usb_host,deferred"],
            leg_rows="#CAP,58214195,DIAG,reentry,admit,1454,4,4,1",
            final_stop_row="[67.747] [INFO] Transport stopped")
        report = fixture.report
        self.assertEqual("DROVE_LEG", report["result"])
        self.assertLess(report["leg_window_end_row"], report["leg_end_row"],
                        "the run's own final stop must not be inside the measured window")
        text = fixture.log.read_text()
        inside = analyse(text, leg="od", slot_a=4, slot_b=5,
                         window=(report["leg_start_row"], report["leg_window_end_row"]))
        self.assertEqual("ok", by_name(inside)["continuous_transport"].status)
        # What the mark is *for*: the same log read out to the run's own end fails the invariant.
        after = analyse(text, leg="od", slot_a=4, slot_b=5,
                        window=(report["leg_start_row"], report["leg_end_row"]))
        self.assertEqual("fail", by_name(after)["continuous_transport"].status)



class PressSpacingTests(unittest.TestCase):
    """The tap window is per button, and a repeat inside it is a *different* gesture."""

    def _guard(self, clock):
        from unittest.mock import patch
        patcher = patch("time.sleep", lambda s: clock.__setitem__("t", clock["t"] + max(s, 0.0)))
        now_patcher = patch("time.monotonic", lambda: clock["t"])
        self.addCleanup(patcher.stop)
        self.addCleanup(now_patcher.stop)
        patcher.start()
        now_patcher.start()
        return _PressSpacing()

    def test_a_second_press_waits_out_the_window(self):
        clock = {"t": 0.0}
        guard = self._guard(clock)
        self.assertEqual(0.0, guard.before(52))
        self.assertEqual(DEFAULT_TAP_WINDOW_S, round(guard.before(52), 6))
        self.assertEqual(DEFAULT_TAP_WINDOW_S, round(clock["t"], 6))

    def test_a_press_after_the_window_does_not_wait(self):
        clock = {"t": 0.0}
        guard = self._guard(clock)
        guard.before(52)
        clock["t"] = DEFAULT_TAP_WINDOW_S + 1.0
        self.assertEqual(0.0, guard.before(52))

    def test_the_window_is_per_button(self):
        clock = {"t": 0.0}
        guard = self._guard(clock)
        guard.before(52)
        self.assertEqual(0.0, guard.before(53), "another slot's button is a different tap sequence")


class OracleTests(unittest.TestCase):
    def test_record_telemetry_cannot_prove_an_overdub_leg(self):
        result = run(chain_log(), leg="od")
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict,
                         [(c.name, c.status, c.detail) for c in result.checks])
        self.assertEqual(4, result.exit_code)

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

    def test_every_session_invalidating_ring_row_is_a_setup_failure(self):
        """`overflow`/`critical_rejected`/`wiped` are exactly the rows `serial_collector` marks INCOMPLETE."""
        for row in ("#CAP,7000000,RING,overflow,512,3",
                    "#CAP,7000000,RING,critical_rejected,2,100",
                    "#CAP,7000000,RING,wiped,1,4096,1000"):
            with self.subTest(row=row):
                result = run(chain_log() + row + "\n")
                self.assertEqual("SETUP_INCOMPLETE", result.verdict)
                self.assertEqual("fail", by_name(result)["overflow_free"].status)

    def test_a_timing_shed_is_reported_but_is_not_a_setup_failure(self):
        """Regression (2026-09-27 and `captures/s4f22-dec096-od-01`): a deliberate timing shed is not a loss.

        `DebugSessionCapture` sheds only non-Tier-A diagnostics in the playback flush instead of blocking the
        transport on USB I/O, so counting it as ring loss made **every** fresh-boot leg `SETUP_INCOMPLETE` and
        hid the links the run actually failed to close.
        """
        baseline = run(chain_log())
        result = run(chain_log(shed=3))
        self.assertEqual(baseline.verdict, result.verdict,
                         "a shed must not change the verdict relative to a shed-free log: "
                         f"{baseline.verdict} -> {result.verdict}")
        self.assertEqual("ok", by_name(result)["overflow_free"].status)
        shed_check = by_name(result)["ring_shed_diagnostics_only"]
        self.assertEqual("ok", shed_check.status)
        self.assertIn("3 timing-shed row(s)", shed_check.detail)
        self.assertEqual(3, len(result.evidence["ring_shed"]))
        self.assertEqual([], result.evidence["ring_loss"])

    def test_a_missing_boot_banner_is_a_setup_failure(self):
        result = run(chain_log(boot=False))
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["fresh_boot"].status)

    def test_a_human_window_row_alone_still_satisfies_L3_but_says_so(self):
        result = run(chain_log(window=False, human_window=True))
        self.assertEqual("ok", by_name(result)["L3_a_finalization_window"].status)
        self.assertIn("timely human row", by_name(result)["L3_a_finalization_window"].detail)

    def test_window_open_is_not_release(self):
        result = run(chain_log(window=False, release=False, human_window=True))
        self.assertEqual("fail", by_name(result)["L4_a_capacity_released"].status)

    def test_wrong_slot_start_cannot_satisfy_successor(self):
        result = run(chain_log().replace("RECA,4,2304", "RECA,3,2304"))
        self.assertEqual("fail", by_name(result)["L5_successor_started"].status)

    def test_start_after_release_does_not_prove_overlap(self):
        result = run(chain_log().replace("3500000,RECA", "5000000,RECA"))
        self.assertEqual("fail", by_name(result)["L5_successor_started"].status)

    def test_a_missing_successor_start_is_a_failure_not_a_gap(self):
        result = run(chain_log(start=False))
        self.assertEqual("fail", by_name(result)["L5_successor_started"].status)

    def test_missing_metadata_is_reported_as_evidence_not_setup(self):
        result = analyse(chain_log(), leg="rec", slot_a=4, slot_b=5, metadata=None, base_report=None)
        self.assertEqual("unknown", by_name(result)["identity_pinned"].status)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)

    def test_the_slot_base_conversion_is_applied(self):
        """A log whose rows are 0-based must be read with the driver's 1-based CLI numbers."""
        result = run(chain_log(), slot_a=4, slot_b=5)
        self.assertEqual("ok", by_name(result)["L1_a_stop_accepted_once"].status)
        # Reading the same log with the wrong base finds no stop for A at all.
        wrong = run(chain_log(), slot_a=3, slot_b=4)
        self.assertEqual("fail", by_name(wrong)["L1_a_stop_accepted_once"].status)


FIXTURES = Path(__file__).parent / "hitl" / "fixtures" / "accepted_sequence"
OD_EXCERPT = FIXTURES / "s4f22-dec096-od-13-excerpt.log"
OD_EXCERPT_PROVENANCE = FIXTURES / "s4f22-dec096-od-13-excerpt.json"


class OverdubIdentityOracleTests(unittest.TestCase):
    """Task 2.2 of `response-driven-hitl-acceptance`: the overdub leg's links come from the intervals' own rows.

    Measured on `captures/s4f22-dec096-od-13` (the leg that drove the whole plan): the start admission
    (`DIAG,reentry,admit`), the accepted-stop binding (`DIAG,reentry,bound`), the execution (`DIAG,stop_state`
    terminal) and the retirement (`DIAG,canonical_release`) each name the interval, and the button action's own
    `Loop N:` rows name which interval each stop *request* was for. Before this the oracle force-unknown'd
    L1/L5/L6/L7 on the overdub leg, which was correct while its only vocabulary was `RECA`/`RECS`.
    """

    def test_a_complete_overdub_chain_is_ok_on_every_link(self):
        result = run_od(od_chain_log())
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict,
                         [(c.name, c.status, c.detail) for c in result.checks])
        self.assertEqual(0, result.exit_code)
        for name in ("L1_a_stop_accepted_once", "L2_no_capacity_denial", "L3_a_finalization_window",
                     "L4_a_capacity_released", "L5_successor_started", "L6_b_stop_executes_once",
                     "L7_one_press_per_stop", "overdub_interval_identity"):
            self.assertEqual("ok", by_name(result)[name].status, name)

    def test_the_seeds_record_rows_are_not_the_overdub_evidence(self):
        """The same chain with and without the seeds' `RECA`/`RECS` rows must read identically."""
        with_seeds = run_od(od_chain_log(seed_records=True))
        without = run_od(od_chain_log(seed_records=False))
        self.assertEqual(with_seeds.verdict, without.verdict)
        self.assertEqual([(c.name, c.status) for c in with_seeds.checks],
                         [(c.name, c.status) for c in without.checks])

    def test_a_missing_successor_start_is_a_gap_not_a_violation_on_the_overdub_leg(self):
        """The overdub families are only as complete as the capture, so absence names a gap to fix."""
        result = run_od(od_chain_log(b_start=False))
        self.assertEqual("unknown", by_name(result)["L5_successor_started"].status)
        self.assertEqual("unknown", by_name(result)["overdub_interval_identity"].status)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertNotEqual(0, result.exit_code)

    def test_a_successor_start_after_the_release_is_NOT_EXERCISED_never_a_violation(self):
        """S4f.22's own device-leg text: "Missing overlap is NOT_EXERCISED" -- the run's shape, not firmware."""
        result = run_od(od_chain_log(b_start_after_a_release=True))
        link = by_name(result)["L5_successor_started"]
        self.assertEqual("fail", link.status)
        self.assertEqual("setup", link.kind, "a staging outcome must stay out of the chain-violation class")
        self.assertIn("NOT_EXERCISED", link.detail)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual(3, result.exit_code)

    def test_a_second_accepted_stop_fails_L1_and_the_press_link(self):
        result = run_od(od_chain_log(a_bindings=2))
        self.assertEqual("fail", by_name(result)["L1_a_stop_accepted_once"].status)
        self.assertEqual("fail", by_name(result)["L7_one_press_per_stop"].status)
        self.assertEqual("ACCEPTED_SEQUENCE_FAIL", result.verdict)

    def test_a_second_execution_for_b_fails_the_execution_link(self):
        result = run_od(od_chain_log(b_terminals=2))
        self.assertEqual("fail", by_name(result)["L6_b_stop_executes_once"].status)
        self.assertEqual("ACCEPTED_SEQUENCE_FAIL", result.verdict)

    def test_a_binding_for_another_interval_cannot_close_L1(self):
        """The binding edge must name the *same* interval the start admission did."""
        result = run_od(od_chain_log(a_binding_interval=9))
        self.assertEqual("unknown", by_name(result)["L1_a_stop_accepted_once"].status)
        self.assertEqual("unknown", by_name(result)["overdub_interval_identity"].status)
        self.assertNotEqual(0, result.exit_code)

    def test_two_intervals_cannot_share_one_identity(self):
        result = run_od(od_chain_log(b_interval=3))
        self.assertEqual("unknown", by_name(result)["overdub_interval_identity"].status)
        self.assertIn("the same interval identity", by_name(result)["overdub_interval_identity"].detail)
        self.assertNotEqual(0, result.exit_code)

    def test_two_stop_requests_for_one_interval_fail_the_press_link(self):
        result = run_od(od_chain_log(a_requests=2))
        self.assertEqual("fail", by_name(result)["L7_one_press_per_stop"].status)

    def test_no_request_rows_leave_the_press_count_unestablished(self):
        result = run_od(od_chain_log(a_requests=0, b_requests=0))
        self.assertEqual("unknown", by_name(result)["L7_one_press_per_stop"].status)
        self.assertNotEqual(0, result.exit_code)

    def test_the_dec070_retarget_fallback_is_reported_and_does_not_change_the_verdict(self):
        """The fallback's closure condition is the sibling change's task 5.11, not a link of this oracle."""
        with_fallback = run_od(od_chain_log(fallback=True))
        without = run_od(od_chain_log())
        self.assertEqual(without.verdict, with_fallback.verdict)
        self.assertEqual(1, len(with_fallback.evidence["dec070_retarget_fallback"]))
        self.assertEqual([], without.evidence["dec070_retarget_fallback"])
        self.assertTrue(any("task 5.11" in lim for lim in with_fallback.limitations),
                        "the report must say the fallback is not this verdict's subject")
        self.assertFalse(any("task 5.11" in lim for lim in without.limitations))

    def test_the_slot_base_conversion_is_applied_on_the_overdub_leg(self):
        """The rows are 0-based; the driver's CLI numbers are 1-based and converted in one place."""
        wrong = analyse(od_chain_log(), leg="od", slot_a=3, slot_b=4,
                        metadata=METADATA, base_report=BASE_REPORT)
        self.assertNotEqual(0, wrong.exit_code)
        self.assertEqual("unknown", by_name(wrong)["overdub_interval_identity"].status)


class FamilyAccountingGateTests(unittest.TestCase):
    """Task 2.2's "required event loss is detectable", as a verdict input.

    The firmware counts each family it relies on and the stopped-only read-back reports those counters, so a leg
    can be gated on its **own** accounting: a refused append or a displaced row inside the capture means the
    capture lost the rows it counts, and a chain cannot be concluded from it. The scope matters as much as the
    gate: the counters are per boot, so the baseline is the capture's own first read-back -- a loss that
    happened before the capture is outside its window, and failing a leg for it would invent a verdict. A
    capture with no read-back at all adds no check (the read-back is the driver's obligation).
    """

    def test_a_capture_whose_own_counters_report_loss_cannot_pass(self):
        text = od_chain_log(family_rows=(
            family_stat("DIAG,latch", entered=2, enq=2),
            family_stat("DIAG,latch", entered=3, enq=2, rej=1),
        ))
        result = run_od(text)
        link = by_name(result)["family_accounting"]
        self.assertEqual("fail", link.status)
        self.assertEqual("setup", link.kind, "lost evidence is a capture problem, like a ring loss")
        self.assertIn("1 refused append(s)", link.detail)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual(3, result.exit_code)

    def test_a_displaced_row_is_reported_too(self):
        text = od_chain_log(family_rows=(
            family_stat("DIAG,latch", entered=4, enq=4, evict=0),
            family_stat("DIAG,latch", entered=4, enq=4, evict=2),
        ))
        result = run_od(text)
        self.assertEqual("fail", by_name(result)["family_accounting"].status)
        self.assertIn("2 displaced row(s)", by_name(result)["family_accounting"].detail)

    def test_an_inconsistent_family_arithmetic_is_a_finding(self):
        """`entered != enq + rej` means the emitter's own count and the ring's disagree."""
        text = od_chain_log(family_rows=(
            family_stat("RECS", entered=9, enq=7, tx=7),
        ))
        result = run_od(text)
        link = by_name(result)["family_accounting"]
        self.assertEqual("fail", link.status)
        self.assertIn("entered 9 != enqueued 7 + rejected 0", link.detail)

    def test_clean_counters_do_not_change_the_verdict(self):
        clean = (family_stat("DIAG,latch", entered=2, enq=2),
                 family_stat("RECA", entered=1, enq=1),
                 family_stat("DIAG,latch", entered=2, enq=2))
        with_rows = run_od(od_chain_log(family_rows=clean))
        without = run_od(od_chain_log())
        self.assertEqual(without.verdict, with_rows.verdict)
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", with_rows.verdict)
        self.assertEqual("ok", by_name(with_rows)["family_accounting"].status)
        self.assertIn("2 counted famil(ies)", by_name(with_rows)["family_accounting"].detail)
        self.assertEqual(2, len(with_rows.evidence["family_stats"]))

    def test_a_boot_scoped_loss_before_the_capture_does_not_fail_the_leg(self):
        """Measured before this capture, reported in both snapshots: outside its window, so not this leg's."""
        text = od_chain_log(family_rows=(
            family_stat("DIAG,latch", entered=5, enq=3, rej=2, tx=3),
            family_stat("DIAG,latch", entered=6, enq=4, rej=2, tx=4),
        ))
        result = run_od(text)
        self.assertEqual("ok", by_name(result)["family_accounting"].status)
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict)

    def test_no_read_back_row_adds_no_check(self):
        """The read-back is the driver's obligation; requiring it here would invalidate captures that never asked."""
        result = run_od(od_chain_log())
        self.assertNotIn("family_accounting", by_name(result))
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict)

    def test_the_gate_reads_the_device_wire_form_verbatim(self):
        """The row below is copied from `captures/base_20260930_164240_serial.log` (flashed image
        `577841d6…`), so the gate's regex is pinned against what the firmware actually prints -- including the
        family tag's own comma."""
        real = ("#CAP,64968688,RING,record_stats,DIAG,latch,entered=0,enq=0,rej=0,tx=0,evict=0")
        result = run_od(od_chain_log(family_rows=(real,)))
        link = by_name(result)["family_accounting"]
        self.assertEqual("ok", link.status)
        self.assertIn("DIAG,latch", link.detail)
        self.assertEqual(["DIAG,latch: {'entered': 0, 'enq': 0, 'rej': 0, 'tx': 0, 'evict': 0}"],
                         result.evidence["family_stats"])


class OverdubIdentityDeviceExcerpt(unittest.TestCase):
    """The driven device run's own identity chain, pinned row-by-row against the log it came from."""

    def setUp(self):
        self.provenance = json.loads(OD_EXCERPT_PROVENANCE.read_text())
        self.text = OD_EXCERPT.read_text()
        self.metadata = {"git_commit": self.provenance["git_commit"],
                         "firmware_hex_sha256": self.provenance["firmware_hex_sha256"]}

    def run_excerpt(self, text=None):
        return analyse(text if text is not None else self.text, leg=self.provenance["leg"],
                       slot_a=self.provenance["slot_a"], slot_b=self.provenance["slot_b"],
                       metadata=self.metadata, base_report=BASE_REPORT)

    def test_the_excerpt_is_the_byte_exact_row_set_it_claims(self):
        self.assertEqual(self.provenance["excerpt_sha256"],
                         hashlib.sha256(self.text.encode()).hexdigest())

    def test_the_excerpt_is_an_accepted_overdub_leg_on_its_own_rows(self):
        result = self.run_excerpt()
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict,
                         [(c.name, c.status, c.detail) for c in result.checks])
        self.assertEqual(0, result.exit_code)
        for name in ("L1_a_stop_accepted_once", "L5_successor_started", "L6_b_stop_executes_once",
                     "L7_one_press_per_stop", "overdub_interval_identity"):
            self.assertEqual("ok", by_name(result)[name].status, name)

    def test_every_excerpt_row_is_verbatim_at_its_recorded_source_line(self):
        source = Path(__file__).resolve().parents[1] / self.provenance["source"]
        if not source.exists():
            self.skipTest(f"{self.provenance['source']} is absent (captures/ is gitignored): this is an "
                          "offline provenance check, not release evidence")
        lines = source.read_text(errors="replace").splitlines()
        self.assertLessEqual(max(self.provenance["source_line_numbers"]), len(lines),
                             "the capture is shorter than the excerpt's own provenance claims")
        self.assertEqual(len(self.text.splitlines()), len(self.provenance["source_line_numbers"]))
        for excerpt_line, number in zip(self.text.splitlines(), self.provenance["source_line_numbers"]):
            self.assertEqual(excerpt_line, lines[number - 1].strip(), f"source line {number}")

    def test_removing_the_successor_start_from_the_excerpt_does_not_pass(self):
        """Load-bearing check: the excerpt is not a rubber stamp for the verdict it carries."""
        mutated = self.text.replace("#CAP,58214195,DIAG,reentry,admit,1454,4,4,1\n", "")
        self.assertNotEqual(self.text, mutated)
        result = self.run_excerpt(mutated)
        self.assertNotEqual(0, result.exit_code)
        self.assertEqual("unknown", by_name(result)["L5_successor_started"].status)
        self.assertEqual("unknown", by_name(result)["overdub_interval_identity"].status)

    def test_the_runs_dec070_fallback_row_is_reported_and_not_fatal(self):
        result = self.run_excerpt()
        self.assertEqual(1, len(result.evidence["dec070_retarget_fallback"]))
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict)
        self.assertTrue(any("task 5.11" in lim for lim in result.limitations))


class WindowInvariantTests(unittest.TestCase):
    """Spec *Continuously playing acceptance window* as oracle predicates (task 5.1).

    The requirement is that the global transport keeps playing through the measured window and that one clock
    source is in force across it (its two scenarios are "global transport does not stop" and the clock-source
    change). The window's boundaries are the **driver's own marks** -- `leg_start_row` (before the leg's first
    gesture) and `leg_window_end_row` (when the leg's plan returned, before the settle and the run's own final
    stop) -- and the invariants are read over exactly that span. Pinned here: a stop *inside* the window fails,
    a stop *outside* it does not, a clock transition inside it fails, a shed capture makes clock-absence
    non-evidence, and a capture that cannot bound its window says UNKNOWN rather than claiming stability from
    absence. The rows the predicates read are the device's own: `Transport stopped` from `ClockManager` and
    the `#CAP,<us>,CS,<from>,<to>` one-shot from `DebugSessionCapture::clockSource`.
    """

    def setUp(self):
        self.provenance = json.loads(OD_EXCERPT_PROVENANCE.read_text())
        self.text = OD_EXCERPT.read_text()
        self.metadata = {"git_commit": self.provenance["git_commit"],
                         "firmware_hex_sha256": self.provenance["firmware_hex_sha256"]}
        self.stop_row = "[30.000] [INFO] Transport stopped"

    def run_window(self, text=None, window=None, leg=None):
        return analyse(text if text is not None else self.text, leg=leg or self.provenance["leg"],
                       slot_a=self.provenance["slot_a"], slot_b=self.provenance["slot_b"],
                       metadata=self.metadata, base_report=BASE_REPORT, window=window)

    def with_row(self, row, at):
        lines = self.text.splitlines()
        return "\n".join(lines[:at] + [row] + lines[at:]) + "\n"

    def test_the_excerpt_bounds_its_window_from_its_own_press_rows(self):
        """No run marks in a hand-cut excerpt: the leg's own `Loop N:` press rows are the (weaker) boundary."""
        result = self.run_window()
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict,
                         [(c.name, c.status, c.detail) for c in result.checks])
        self.assertEqual("ok", by_name(result)["continuous_transport"].status)
        self.assertEqual("ok", by_name(result)["clock_source_stable"].status)
        self.assertIn("press span", result.evidence["window"][0])

    def test_a_transport_stop_inside_the_measured_window_fails(self):
        """The requirement itself: the acceptance window must stay playing."""
        text = self.with_row(self.stop_row, 3)
        result = self.run_window(text)
        self.assertEqual("fail", by_name(result)["continuous_transport"].status)
        self.assertEqual("ACCEPTED_SEQUENCE_FAIL", result.verdict)
        self.assertEqual([self.stop_row], result.evidence["window_transports_stopped"])

    def test_a_record_leg_stop_inside_the_window_names_the_plans_own_step(self):
        """The record leg's plan still stops the global transport itself: the oracle must say whose it is.

        The spec's record-successor scenario has the switch to B carry B's record start, so a stop inside a rec
        run's window is the *plan's* (`leg_gestures`' `a_stop`), not a device defect. Task 4.2 is that change;
        until it lands the oracle names it instead of leaving a reader to infer it from the row.
        """
        text = self.with_row(self.stop_row, 3)
        result = self.run_window(text, leg="rec")
        self.assertEqual("fail", by_name(result)["continuous_transport"].status)
        self.assertTrue(any("names the *plan*" in lim and "task 4.2" in lim for lim in result.limitations),
                        result.limitations)
        # And on the overdub leg the same row is the device's own stop, with no such excuse recorded.
        od = self.run_window(text)
        self.assertEqual("fail", by_name(od)["continuous_transport"].status)
        self.assertFalse(any("names the *plan*" in lim for lim in od.limitations))

    def test_a_transport_stop_after_the_legs_last_press_is_not_a_window_failure(self):
        """The run's final stop is *teardown*, not a contract failure -- the boundary has to be exact."""
        text = self.with_row(self.stop_row, len(self.text.splitlines()))
        result = self.run_window(text)
        self.assertEqual("ok", by_name(result)["continuous_transport"].status)
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict)

    def test_the_runs_own_marks_bound_the_window_when_they_are_supplied(self):
        """A live run states its own boundaries, and the oracle names which boundary it used."""
        text = self.with_row(self.stop_row, len(self.text.splitlines()))
        lines = text.splitlines()
        result = self.run_window(text, window=(2, len(lines) - 1))
        self.assertEqual("ok", by_name(result)["continuous_transport"].status)
        self.assertIn("run's own leg marks", result.evidence["window"][0])
        # The same capture, read one row further, contains the stop and fails: the mark is what decides it.
        after = self.run_window(text, window=(2, len(lines)))
        self.assertEqual("fail", by_name(after)["continuous_transport"].status)

    def test_a_clock_source_transition_inside_the_window_fails(self):
        """One timeline: the evidence either side of a clock change is not the same measurement."""
        text = self.with_row("#CAP,60000000,CS,INT,EXT", 3)
        result = self.run_window(text)
        self.assertEqual("fail", by_name(result)["clock_source_stable"].status)
        self.assertEqual("ACCEPTED_SEQUENCE_FAIL", result.verdict)
        self.assertEqual(["#CAP,60000000,CS,INT,EXT"], result.evidence["window_clock_transitions"])

    def test_a_shed_capture_makes_clock_absence_not_evidence(self):
        """`CS` is ordinary (non-Tier-A) ring text, so a row shed *inside* the span cannot be read as a
        stable clock: absence of the transition row is not evidence when rows went missing in the window."""
        text = self.with_row("RING,shed,tier=ordinary,dropped=3", 3)
        result = self.run_window(text)
        self.assertEqual("ok", by_name(result)["continuous_transport"].status)
        self.assertEqual("unknown", by_name(result)["clock_source_stable"].status)
        self.assertNotEqual("ACCEPTED_SEQUENCE_PASS", result.verdict)

    def test_a_shed_row_outside_the_window_leaves_the_clock_check_measured(self):
        """The shed rule is scoped to the span: a drop *outside* it does not make the window unmeasurable.

        Measured: the device run in `captures/s4f22-dec096-od-13` shed one ordinary row at device 51.4 s, well
        before its leg began. Reading the whole capture rather than the window would have reported that run's
        clock source as unmeasurable for a drop that could not have touched it.
        """
        text = self.with_row("RING,shed,tier=ordinary,dropped=1", len(self.text.splitlines()))
        result = self.run_window(text)
        self.assertEqual("ok", by_name(result)["continuous_transport"].status)
        self.assertEqual("ok", by_name(result)["clock_source_stable"].status)
        self.assertEqual([], result.evidence["window_shed_rows"])
        self.assertEqual("ACCEPTED_SEQUENCE_PASS", result.verdict)

    def test_a_capture_with_no_boundary_reports_the_invariants_as_unknown(self):
        """No run marks and no press rows: "the transport never stopped inside it" is not established."""
        text = "\n".join(line for line in self.text.splitlines() if "Loop 5:" not in line) + "\n"
        self.assertNotEqual(self.text, text)
        result = self.run_window(text)
        self.assertEqual("unknown", by_name(result)["continuous_transport"].status)
        self.assertEqual("unknown", by_name(result)["clock_source_stable"].status)
        self.assertNotEqual("ACCEPTED_SEQUENCE_PASS", result.verdict)
        self.assertIn("unbounded", result.evidence["window"][0])

    def test_one_press_row_is_not_a_window(self):
        """A single request row bounds nothing: the "window" would be that row alone, and claiming the
        transport never stopped over it would be reading a boundary out of one line."""
        text = "\n".join(line for line in self.text.splitlines() if "Switch gesture" not in line) + "\n"
        self.assertNotEqual(self.text, text)
        result = self.run_window(text)
        self.assertEqual("unknown", by_name(result)["continuous_transport"].status)
        self.assertEqual("unknown", by_name(result)["clock_source_stable"].status)

    def test_only_an_ordered_pair_of_line_offsets_is_a_window(self):
        """A driver report that predates the mark, or a malformed one, yields no window -- never a false OK."""
        valid = {"leg_start_row": 10, "leg_window_end_row": 20}
        self.assertEqual((10, 20), _run_window(valid))
        for bad in (None, {}, {"leg_start_row": 10}, {"leg_window_end_row": 20},
                    {"leg_start_row": None, "leg_window_end_row": 20},
                    {"leg_start_row": 20, "leg_window_end_row": 20},
                    {"leg_start_row": 21, "leg_window_end_row": 20},
                    {"leg_start_row": -1, "leg_window_end_row": 20},
                    {"leg_start_row": "10", "leg_window_end_row": 20},
                    {"leg_start_row": True, "leg_window_end_row": 20}):
            self.assertIsNone(_run_window(bad), bad)


class SealedVerdictTests(unittest.TestCase):
    """The oracle's verdict, sealed into a bundle that can be re-verified without a device (task 3.3).

    The point is not another report: it is that the verdict *and the checks it rests on* are recorded together,
    so `scripts/hitl_report_replay.py` can re-run the same rule later and reach the same answer -- and can
    refuse a bundle whose recorded verdict its own checks no longer support.
    """

    def _analysis(self, *checks, verdict="ACCEPTED_SEQUENCE_PASS", limitations=()):
        return Analysis(verdict, list(checks), {"L2_no_capacity_denial": "no denial"}, list(limitations))

    def _seal(self, tmp, analysis, *, run_report=None):
        from hitl.reporting import write_report_bundle
        return write_report_bundle(
            Path(tmp) / "run",
            verdict_bundle(analysis, leg="od", identity={"git_commit": "abc"},
                           run_id="accepted-sequence-od-fixture", run_report=run_report))

    def test_a_pass_is_sealed_and_replays_as_a_pass(self):
        import tempfile

        from hitl.reporting import verify_report_bundle

        analysis = self._analysis(Check("L2_no_capacity_denial", "ok", "no denial", "evidence"),
                                  Check("L3_a_finalization_window", "ok", "window opened", "evidence"))
        with tempfile.TemporaryDirectory() as tmp:
            path = self._seal(tmp, analysis, run_report={"shutdown": {"ok": True, "status": "complete"}})
            verdict = verify_report_bundle(path)
        self.assertTrue(verdict.ok, verdict.failures)
        self.assertTrue(verdict.passed)

    def test_an_unknown_check_is_not_a_pass_even_when_the_oracle_says_pass(self):
        import json
        import tempfile

        from hitl.reporting import verify_report_bundle

        analysis = self._analysis(Check("L5_a_stop_accepted", "unknown",
                                        "RECA/RECS do not identify overdub intervals", "evidence"),
                                  verdict="INSUFFICIENT_EVIDENCE")
        with tempfile.TemporaryDirectory() as tmp:
            path = self._seal(tmp, analysis, run_report={"shutdown": {"ok": True, "status": "complete"}})
            record = json.loads(path.read_text())
            verdict = verify_report_bundle(path)
        self.assertFalse(record["claims_success"], "an unknown edge is not a success claim")
        self.assertFalse(record["passed"])
        self.assertEqual(["L5_a_stop_accepted"], [f["check"] for f in verdict.failures])
        self.assertFalse(verdict.passed)

    def test_a_pass_without_a_proven_drain_is_not_sealed_as_a_pass(self):
        """The spec's rule at the bundle level: `shutdown failure SHALL prevent overall success`."""
        import tempfile

        from hitl.reporting import verify_report_bundle

        analysis = self._analysis(Check("L2_no_capacity_denial", "ok", "no denial", "evidence"))
        with tempfile.TemporaryDirectory() as tmp:
            no_report = self._seal(tmp, analysis)                       # no driver report at all
            failed = self._seal(Path(tmp) / "second", analysis, run_report={
                "shutdown": {"ok": False, "status": "incomplete_timeout"}})
            first, second = verify_report_bundle(no_report), verify_report_bundle(failed)
        self.assertFalse(first.passed)
        self.assertEqual("none recorded", first.failures[0]["observed"])
        self.assertFalse(second.passed)
        self.assertEqual("incomplete_timeout", second.failures[0]["observed"])

    def test_a_setup_failure_keeps_its_own_verdict_and_names_the_check(self):
        import json
        import tempfile

        analysis = self._analysis(Check("fresh_boot", "fail", "no boot banner inside the window", "setup"),
                                  verdict="SETUP_INCOMPLETE")
        with tempfile.TemporaryDirectory() as tmp:
            path = self._seal(tmp, analysis, run_report={"shutdown": {"ok": True, "status": "complete"}})
            record = json.loads(path.read_text())
        self.assertEqual("SETUP_INCOMPLETE", record["status"])
        self.assertFalse(record["passed"], "a setup failure is not a pass")
        self.assertEqual(["fresh_boot"], [f["check"] for f in record["failures"]])
        self.assertEqual("the verdict is the accepted-sequence oracle's; every check it made is a predicate here",
                         record["notes"][0])

    def test_the_runs_authorization_record_travels_into_the_sealed_bundle(self):
        """Task 4.1: a sealed verdict names the scope the run was driven under, not an empty section."""
        import json
        import tempfile

        analysis = self._analysis(Check("L2_no_capacity_denial", "ok", "no denial", "evidence"))
        authorization = {"source": "manifest", "value": "STIM-BASE-2-2-01", "fingerprint": "STIM-BASE-2-2-01@v1",
                         "track": 8, "slots": [4, 5], "targets": "track 8 slots 4,5", "declared": None}
        with tempfile.TemporaryDirectory() as tmp:
            carried = self._seal(tmp, analysis, run_report={
                "shutdown": {"ok": True, "status": "complete"}, "authorization": authorization,
                "result": "DROVE_LEG"})
            absent = self._seal(Path(tmp) / "second", analysis, run_report={
                "shutdown": {"ok": True, "status": "complete"}, "result": "DROVE_LEG"})
            with_record = json.loads(carried.read_text())["authorization"]
            without_record = json.loads(absent.read_text())["authorization"]
        self.assertEqual(authorization, with_record)
        self.assertIsNone(without_record, "a run that recorded no authorization says so")


if __name__ == "__main__":
    unittest.main(verbosity=2)
