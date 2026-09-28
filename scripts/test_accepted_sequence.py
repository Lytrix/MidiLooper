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
    _Ctx,
    _emit,
    _switch_pending,
    _run_plan,
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
        lines.append("#CAP,3500000,RECA,4,2304")
    for i in range(stops_b):
        lines.append(f"#CAP,{6000000 + i},RECS,stopToStopped,4,1900,0,1900,2304,0")
    if ring:
        lines.append("#CAP,7000000,RING,tier_a_evicted,9790,first=2380221900")
    return "\n".join(lines) + "\n"


def run(text, *, leg="rec", slot_a=4, slot_b=5, **kw):
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
        with patch("hitl_accepted_sequence._emit", side_effect=emit), \
             patch("hitl_accepted_sequence.time.sleep"):
            result = _run_plan(FakePort(), log, plan, slot_b=5, press_ms=120,
                               phase_timeout_s=0.01, events=[])
        self.assertEqual("UNEXPECTED_PLAYBACK_SWITCH", result)
        self.assertEqual(["b_switch"], emitted)

    def test_driver_does_not_stop_b_when_overdub_never_started(self):
        from unittest.mock import patch
        log = self._log("s.log", [])
        with patch("hitl_accepted_sequence._emit") as emit, \
             patch("hitl_accepted_sequence._wait_for", return_value=False):
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
        with patch("hitl_accepted_sequence._emit", side_effect=emit), \
             patch("hitl_accepted_sequence.time.sleep"):
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

    def test_a_recognized_long_press_with_no_clear_row_is_the_empty_noop(self):
        from unittest.mock import patch
        state = {"n": 0}

        def reader(_path):
            state["n"] += 1
            if state["n"] == 1:
                return []
            return ["[1.0] [DEBUG] [BTN] Button press: Loop 4 (long)"]

        nows = iter([0.0, 0.0, 0.0, 2.0, 2.0, 2.0, 9.0])
        with patch("hitl.actions.slots._send_long_press"):
            outcome = stopped_preparation.clear_slot(
                _Ctx(FakePort()), self.log, track_index0=7, slot0=3, long_press_ms=900,
                noop_settle_s=1.0, timeout_s=5.0, lines_reader=reader,
                sleep=lambda _s: None, now=lambda: next(nows, 9.0))
        self.assertEqual(ClearStatus.ALREADY_EMPTY, outcome.status)
        self.assertTrue(outcome.ok)

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
                # The census channel refuses here so these cases exercise the
                # gesture-only preparation fallback unchanged.
                send_command=lambda *a, **k: (False, "no census in this fixture"),
                library_observation=lambda *a, **k: None)
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
                    return stack.enter_context(patch("hitl_accepted_sequence." + name, **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                record_press = mock("slots.press_slot_record")
                mock("stopped_preparation.clear_slot", return_value=refused)
                mock("_ensure_transport", return_value=True)
                mock("_run_plan", return_value="DROVE_LEG")
                mock("time.sleep")
                stack.enter_context(patch("builtins.print"))
                out = root / "run"
                rc = main(["--leg", "rec", "--track", "8", "--slot-a", "4",
                           "--slot-b", "5", "--notes", "1", "--no-upload",
                           "--out-dir", str(out)])

            # The refused clear is the target's own failure: no record press followed it.
            self.assertEqual(0, record_press.call_count)
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
                # The census channel refuses here so these cases exercise the
                # gesture-only preparation fallback unchanged.
                send_command=lambda *a, **k: (False, "no census in this fixture"),
                library_observation=lambda *a, **k: None)
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
                    return stack.enter_context(patch("hitl_accepted_sequence." + name, **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                mock("slots.press_slot_record")
                clear = mock("stopped_preparation.clear_slot", return_value=cleared)
                mock("_ensure_transport", return_value=True)
                mock("_wait_for", return_value=True)
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
        lines = ["#CAP,1,LIBOBS,B,a=1,e=0,s=64"]
        for track, slot, has in slots:
            lines.append(f"#CAP,2,LIBOBS,S,t={track},s={slot},st=2,c=n,p=3,o=2,q=5,w=0,m=1,"
                         f"g=0,e=0,k={has},kp={has},kr={has},kc={has},a=0,h=0,l=0,v=0,n=0,i=0,x=0")
        lines.append("#CAP,3,LIBOBS,E,a=1,b=1,t=1,r=0,s=1,P=1,R=1,x=1")
        return lines

    def test_occupancy_uses_the_device_union_content_signal(self):
        self.assertEqual({(7, 3): True, (7, 4): False},
                         occupied_slots(self.family((7, 3, 1), (7, 4, 0))))

    def test_a_lost_window_is_unknown_not_empty(self):
        self.assertIsNone(occupied_slots(self.family((7, 3, 1))[:-1]))
        self.assertEqual((None, None), observation_window(self.family((7, 3, 1))[:-1]))

    def test_an_unsettled_sweep_still_yields_occupancy_with_its_flag(self):
        # The device flags a multi-turn sweep as not settled (x=0) while every slot row is
        # present; preparation may use the occupancy, a settled census must not.
        lines = self.family((7, 3, 1), (7, 4, 0))
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
                def mock(name, **kw): return stack.enter_context(patch("hitl_accepted_sequence." + name, **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track"); mock("slots.press_slot_record")
                clear = mock("stopped_preparation.clear_slot")
                mock("observation_window", return_value=({(7, 3): False, (7, 4): False}, True))
                mock("_ensure_transport", return_value=True); mock("_wait_for", return_value=True)
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
                def mock(name, **kw): return stack.enter_context(patch("hitl_accepted_sequence." + name, **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                select = mock("slots.press_slot_record")
                mock("stopped_preparation.clear_slot", return_value=cleared)
                mock("observation_window", return_value=({(7, 3): True}, True))
                mock("_ensure_transport", return_value=True); mock("_wait_for", return_value=True)
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
                # The census channel refuses here so these cases exercise the
                # gesture-only preparation fallback unchanged.
                send_command=lambda *a, **k: (False, "no census in this fixture"),
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
                    return stack.enter_context(patch("hitl_accepted_sequence." + name, **kw))
                mock("ManagedCaptureSession", side_effect=managed)
                mock("DeviceSession", side_effect=device)
                mock("mido.open_output", return_value=FakePort())
                mock("_find_midi_port", return_value="fake")
                mock("slots.press_track")
                select = mock("slots.press_slot_record")
                mock("_ensure_transport", return_value=True)
                mock("_wait_for", return_value=True)
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
