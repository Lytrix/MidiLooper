#!/usr/bin/env python3
"""Tests for the boot-handoff gate that keeps host control gestures out of an unfinished boot.

The behaviour under test is the owner's 2026-10-12 panel report ("something in the test is
starting during bootup so too soon which makes the display hang after boot"), pinned by
`captures/i3a-edge-effect-boot-rerun-t8s4-03` / `-01` (first host press ~7.4 s, boot handoff
`DisplayManager: Boot setup complete` ~15-17 s and `boot_commit` ~17.5 s) and
`captures/i3a-edge-effect-boot-rerun-t8s4-02` (a press into the unfinished boot read as 1 ms and
cycled LOOP_EDIT -> NOTE_EDIT). The gate is row evidence, never a sleep, and never writes.

The same gate is ported to the edit driver `legacy_edit_baseline.run_edit_baseline` (the seeded-edit
preset `note_edit_reselect_stacked --boot-reset` hung the display the same way, owner report
2026-10-12); both drivers' call-site order is pinned by source-offset tripwires.

The seeded-edit scenarios that own their gestures (`note_edit_reselect_stacked`,
`note_edit_overdub_compose`, `edit_overdub_during_note_edit`, `windowed_content_parity`) are gated the
same way and pinned by the same tripwire; `overdub_occupy_paint` and `edit_full` reach their gestures
only through the already-gated drivers.

Run: .venv/bin/python scripts/test_hitl_boot_handoff.py
"""

from __future__ import annotations

import re
import sys
import time
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.boot_handoff import (  # noqa: E402
    APP_SCREEN_RELEASE_HOLD_MS,
    APP_SCREEN_WITNESS_ROW,
    BOOT_BANNER_MARKER,
    BOOT_COMMIT_ROW,
    BOOT_SCREEN_EXTRA_HOLD_MS,
    BOOT_SETUP_COMPLETE_ROW,
    boot_handoff_complete,
    fresh_boot_in_window,
    wait_for_boot_handoff,
)

_BANNER = "#CAP,2164003,DIAG,boot_reset,crash=0"
_SETUP = "DisplayManager: Boot setup complete"
_COMMIT = "#CAP,17838519,DIAG,loop_rem,boot_commit,387560,255,255,255,0"
#: An app-paint-path witness, not proof of physical pixels or completed DMA.
_DFRAME = "#CAP,19300000,DFRAME,8,18000,30,100,600,40"
_COMMIT_US = 17838519
_HOLD_US = APP_SCREEN_RELEASE_HOLD_MS * 1000


class _FakeCollector:
    """Line source with the real collectors' write surface, so a gate write would be visible."""

    def __init__(self, lines: list[str]) -> None:
        self.lines = list(lines)
        self.snapshot_calls = 0
        self.writes: list[str] = []

    def snapshot(self) -> list[str]:
        self.snapshot_calls += 1
        return list(self.lines)

    def write_line(self, text: str) -> None:
        self.writes.append(text)


class WindowSemanticsTests(unittest.TestCase):
    def test_banner_alone_is_fresh_but_not_complete(self) -> None:
        lines = [_BANNER]
        self.assertTrue(fresh_boot_in_window(lines))
        self.assertFalse(boot_handoff_complete(lines))

    def test_boot_commit_plus_app_frame_completes_without_setup_text(self) -> None:
        lines = ["[Memory] heap", _BANNER, _COMMIT, _DFRAME]
        self.assertTrue(fresh_boot_in_window(lines))
        self.assertFalse(any(_SETUP in line for line in lines))
        self.assertTrue(boot_handoff_complete(lines))

    def test_boot_commit_without_app_frame_is_not_complete(self) -> None:
        # The owner commit alone does not prove that display painting resumed.
        self.assertFalse(boot_handoff_complete([_BANNER, _COMMIT]))

    def test_app_frame_without_boot_commit_is_not_complete(self) -> None:
        self.assertFalse(boot_handoff_complete([_BANNER, _DFRAME]))

    def test_no_banner_is_not_a_fresh_boot(self) -> None:
        self.assertFalse(fresh_boot_in_window(["#CAP,1,BPM,120", _SETUP, _COMMIT, _DFRAME]))

    def test_a_banner_handoff_pair_outside_the_examined_window_is_not_complete(self) -> None:
        # A second boot banner restarts the handoff window: the first boot's rows no longer count.
        lines = [_BANNER, _SETUP, _COMMIT, _DFRAME, "#CAP,99,DIAG,boot_reset,crash=1"]
        self.assertTrue(fresh_boot_in_window(lines))
        self.assertFalse(boot_handoff_complete(lines))

    def test_a_witness_from_an_earlier_boot_does_not_count(self) -> None:
        # The earlier boot painted; the new boot's window holds no witness of its own yet.
        lines = [
            _BANNER,
            _SETUP,
            _COMMIT,
            _DFRAME,
            "#CAP,99,DIAG,boot_reset,crash=1",
            _SETUP,
            "#CAP,25000000,DIAG,loop_rem,boot_commit,387560,255,255,255,0",
        ]
        self.assertTrue(fresh_boot_in_window(lines))
        self.assertFalse(boot_handoff_complete(lines))

    def test_elapsed_hold_without_a_frame_never_completes(self) -> None:
        for offset in (_HOLD_US - 1, _HOLD_US, _HOLD_US + 100000000):
            with self.subTest(offset=offset):
                row = f"#CAP,{_COMMIT_US + offset},DIAG,idle_maint,0,0"
                self.assertFalse(boot_handoff_complete([_BANNER, _SETUP, _COMMIT, row]))

    def test_delayed_stale_witness_after_commit_is_rejected(self) -> None:
        stale = "#CAP,1000000,DFRAME,8,18000,30,100,600,40"
        self.assertFalse(boot_handoff_complete([_BANNER, _SETUP, _COMMIT, stale]))

    def test_witness_timestamp_must_be_strictly_after_commit(self) -> None:
        for stamp in (_COMMIT_US - 1, _COMMIT_US):
            with self.subTest(stamp=stamp):
                row = f"#CAP,{stamp},DFRAME,8,18000,30,100,600,40"
                self.assertFalse(boot_handoff_complete([_BANNER, _SETUP, _COMMIT, row]))
        row = f"#CAP,{_COMMIT_US + 1},DFRAME,8,18000,30,100,600,40"
        self.assertTrue(boot_handoff_complete([_BANNER, _SETUP, _COMMIT, row]))

    def test_witness_before_timestamped_banner_is_rejected(self) -> None:
        banner = "#CAP,20000000,DIAG,boot_reset,crash=0"
        self.assertFalse(boot_handoff_complete([banner, _SETUP, _COMMIT, _DFRAME]))

    def test_app_frame_must_follow_the_timestamped_commit(self) -> None:
        self.assertFalse(boot_handoff_complete([_BANNER, _DFRAME, _COMMIT]))
        self.assertTrue(boot_handoff_complete([_BANNER, _COMMIT, _DFRAME]))

    def test_unstamped_commit_cannot_qualify_a_witness(self) -> None:
        self.assertFalse(boot_handoff_complete([_BANNER, _SETUP, BOOT_COMMIT_ROW, _DFRAME]))

    def test_unstamped_banner_still_requires_timestamped_commit_and_witness(self) -> None:
        self.assertTrue(boot_handoff_complete([BOOT_BANNER_MARKER, _SETUP, _COMMIT, _DFRAME]))

    def test_malformed_witness_rows_fail_closed(self) -> None:
        rows = (
            "DFRAME,8,18000,30,100,600,40",
            "mentioned " + _DFRAME,
            "#CAP,invalid,DFRAME,8,18000,30,100,600,40",
            "#CAP,19300000,DFRAME,",
            "#CAP,19300000,DFRAME,8,18000,30,100,600",
            "#CAP,19300000,DFRAME,8,18000,30,100,600,bad",
            "#CAP,19300000,DFRAME,8,18000,30,100,600,40,trailing",
            "#CAP,19300000,DFRAME_FAKE,8,18000,30,100,600,40",
        )
        for row in rows:
            with self.subTest(row=row):
                self.assertFalse(boot_handoff_complete([_BANNER, _SETUP, _COMMIT, row]))

    def test_witness_and_hold_constants_pin_the_measured_firmware_hold(self) -> None:
        self.assertEqual("DFRAME", APP_SCREEN_WITNESS_ROW)
        self.assertEqual(2000, BOOT_SCREEN_EXTRA_HOLD_MS)
        self.assertEqual(2500, APP_SCREEN_RELEASE_HOLD_MS)
        self.assertGreaterEqual(APP_SCREEN_RELEASE_HOLD_MS, BOOT_SCREEN_EXTRA_HOLD_MS)


class WaitForBootHandoffTests(unittest.TestCase):
    def test_fresh_banner_without_the_rows_times_out_and_never_writes(self) -> None:
        collector = _FakeCollector(["#CAP,2164003,DIAG,boot_reset,crash=0"])
        started = time.monotonic()
        result = wait_for_boot_handoff(collector, timeout_s=0.2, poll_s=0.05)
        elapsed_s = time.monotonic() - started
        self.assertEqual("timeout", result)
        self.assertGreaterEqual(elapsed_s, 0.15)
        self.assertLess(elapsed_s, 2.0)
        self.assertGreater(collector.snapshot_calls, 1, "the gate must poll for the handoff rows")
        self.assertEqual([], collector.writes, "the gate must never write to the device")

    def test_handoff_rows_with_the_witness_complete_immediately_without_writing(self) -> None:
        collector = _FakeCollector([_BANNER, _COMMIT, _DFRAME])
        self.assertEqual("complete", wait_for_boot_handoff(collector))
        self.assertEqual(1, collector.snapshot_calls)
        self.assertEqual([], collector.writes)

    def test_handoff_rows_without_the_witness_time_out_and_never_write(self) -> None:
        # The new RED against the pre-witness predicate: banner + both handoff rows is not enough.
        collector = _FakeCollector([_BANNER, _SETUP, _COMMIT])
        started = time.monotonic()
        result = wait_for_boot_handoff(collector, timeout_s=0.2, poll_s=0.05)
        elapsed_s = time.monotonic() - started
        self.assertEqual("timeout", result)
        self.assertGreaterEqual(elapsed_s, 0.15)
        self.assertLess(elapsed_s, 2.0)
        self.assertGreater(collector.snapshot_calls, 1, "the gate must poll for the witness")
        self.assertEqual([], collector.writes, "the gate must never write to the device")

    def test_elapsed_hold_without_a_witness_times_out_without_writing(self) -> None:
        hold_row = f"#CAP,{_COMMIT_US + _HOLD_US},DIAG,idle_maint,0,0"
        collector = _FakeCollector([_BANNER, _SETUP, _COMMIT, hold_row])
        self.assertEqual("timeout", wait_for_boot_handoff(collector, timeout_s=0.02, poll_s=0.01))
        self.assertGreater(collector.snapshot_calls, 1)
        self.assertEqual([], collector.writes)

    def test_stale_witness_times_out_without_writing(self) -> None:
        stale = "#CAP,1000000,DFRAME,8,18000,30,100,600,40"
        collector = _FakeCollector([_BANNER, _SETUP, _COMMIT, stale])
        self.assertEqual("timeout", wait_for_boot_handoff(collector, timeout_s=0.02, poll_s=0.01))
        self.assertEqual([], collector.writes)

    def test_default_timeout_remains_thirty_seconds(self) -> None:
        self.assertEqual(30.0, wait_for_boot_handoff.__kwdefaults__["timeout_s"])

    def test_handoff_rows_that_arrive_while_waiting_complete(self) -> None:
        collector = _FakeCollector([_BANNER])

        original_snapshot = collector.snapshot

        def snapshot() -> list[str]:
            lines = original_snapshot()
            if collector.snapshot_calls >= 3:
                lines.extend([_COMMIT, _DFRAME])
            return lines

        collector.snapshot = snapshot  # type: ignore[method-assign]
        self.assertEqual("complete", wait_for_boot_handoff(collector, timeout_s=2.0, poll_s=0.01))
        self.assertEqual([], collector.writes)

    def test_no_banner_returns_not_fresh_without_waiting(self) -> None:
        collector = _FakeCollector(["#CAP,1,BPM,120", "#CAP,2,BAR,1"])
        started = time.monotonic()
        result = wait_for_boot_handoff(collector)  # default 30 s must not be spent
        elapsed_s = time.monotonic() - started
        self.assertEqual("not-fresh-boot", result)
        self.assertEqual(1, collector.snapshot_calls, "an attached run must not be delayed")
        self.assertLess(elapsed_s, 0.5)
        self.assertEqual([], collector.writes)

    def test_a_new_banner_hides_an_earlier_pair_and_times_out(self) -> None:
        collector = _FakeCollector([_BANNER, _SETUP, _COMMIT, "#CAP,99,DIAG,boot_reset,crash=1"])
        self.assertEqual("timeout", wait_for_boot_handoff(collector, timeout_s=0.1, poll_s=0.05))
        self.assertEqual([], collector.writes)


class StartSequencingTripwireTests(unittest.TestCase):
    """The gate must run before the first control-surface gesture of `run()`."""

    _SOURCE = _SCRIPT_DIR / "hitl" / "legacy_record_baseline.py"
    _GATE_CALL = "wait_for_boot_handoff("
    _GESTURE_CALLS = (
        "_select_loop_slot_before_record_arm(",
        "_prepare_deferred_save_before_clear(",
        "ensure_loop_edit_before_record(",
        "_ensure_transport_stopped_before_record_arm(",
    )

    def test_gate_call_site_precedes_the_control_gesture_call_sites(self) -> None:
        source = self._SOURCE.read_text(encoding="utf-8")
        run_body = source[source.index("def run() -> int:") :]
        self.assertIn(self._GATE_CALL, run_body)
        gate_offset = run_body.index(self._GATE_CALL)
        gesture_offsets = [
            run_body.index(name) for name in self._GESTURE_CALLS if name in run_body
        ]
        self.assertTrue(gesture_offsets, "the tripwire must find a control-gesture call site")
        self.assertLess(
            gate_offset,
            min(gesture_offsets),
            "wait_for_boot_handoff must be called before the first control gesture in run()",
        )

    def test_gate_reads_the_legacy_run_s_own_serial_collector(self) -> None:
        source = self._SOURCE.read_text(encoding="utf-8")
        run_body = source[source.index("def run() -> int:") :]
        gate_offset = run_body.index(self._GATE_CALL)
        self.assertIn("wait_for_boot_handoff(serial_collector)", run_body[gate_offset:])
        # The timeout branch refuses by name and aborts before any gesture.
        self.assertIn("refusing to send control gestures", run_body)
        self.assertIn("raise SystemExit(3)", run_body)

    def test_markers_match_the_recorded_capture_rows(self) -> None:
        self.assertEqual("DIAG,boot_reset", BOOT_BANNER_MARKER)
        self.assertEqual("DisplayManager: Boot setup complete", BOOT_SETUP_COMPLETE_ROW)
        self.assertEqual("DIAG,loop_rem,boot_commit", BOOT_COMMIT_ROW)



class EditStartSequencingTripwireTests(unittest.TestCase):
    """The gate must also run before the first control-surface gesture of `run_edit_baseline()`."""

    _SOURCE = _SCRIPT_DIR / "hitl" / "legacy_edit_baseline.py"
    _FUNCTION = "def run_edit_baseline("
    _GATE_CALL = "wait_for_boot_handoff("
    _GESTURE_CALLS = (
        "_send_short_press(",
        "_send_double_press(",
        "_send_triple_press(",
        "_send_long_press(",
        "_select_loop_slot_before_record_arm(",
        "_run_clear_before_record(",
        "_run_record_arm_until_capturing(",
    )

    def _function_body(self, source: str) -> str:
        """`run_edit_baseline()` up to the next top-level def (its gestures live in `main()`'s file)."""
        start = source.index(self._FUNCTION)
        next_def = re.search(r"\ndef [A-Za-z_]", source[start + 1 :])
        end = start + 1 + next_def.start() if next_def is not None else len(source)
        return source[start:end]

    def test_gate_call_site_precedes_the_control_gesture_call_sites(self) -> None:
        body = self._function_body(self._SOURCE.read_text(encoding="utf-8"))
        self.assertIn(self._GATE_CALL, body)
        gate_offset = body.index(self._GATE_CALL)
        gesture_offsets = [body.index(name) for name in self._GESTURE_CALLS if name in body]
        self.assertTrue(gesture_offsets, "the tripwire must find a control-gesture call site")
        self.assertLess(
            gate_offset,
            min(gesture_offsets),
            "wait_for_boot_handoff must be called before the first control gesture in "
            "run_edit_baseline()",
        )

    def test_gate_reads_the_edit_run_s_own_serial_collector(self) -> None:
        body = self._function_body(self._SOURCE.read_text(encoding="utf-8"))
        gate_offset = body.index(self._GATE_CALL)
        self.assertIn("wait_for_boot_handoff(serial_collector)", body[gate_offset:])
        # The timeout branch refuses by name and aborts before any gesture.
        self.assertIn("refusing to send control gestures", body)
        self.assertIn("raise SystemExit(3)", body)

    def test_gate_is_skipped_when_no_serial_collector_exists(self) -> None:
        body = self._function_body(self._SOURCE.read_text(encoding="utf-8"))
        self.assertIn("if serial_collector is not None:", body)


class ScenarioStartSequencingTripwireTests(unittest.TestCase):
    """Each scenario that owns control gestures must gate them behind the boot handoff."""

    _SCENARIO_DIR = _SCRIPT_DIR / "hitl" / "scenarios"
    _GATE_CALL = "wait_for_boot_handoff("
    #: (source file, function that owns the gate, control-gesture call sites in that function).
    _CASES = (
        (
            "note_edit_reselect_stacked.py",
            "def run_note_edit_reselect_stacked(",
            (
                "_send_short_press(",
                "_send_select_pitchbend(",
                "_fader1_select_fixture_note(",
                "_fader2_move_to_sixteenth_step(",
                "_ensure_target_playing(",
                "_ensure_note_edit_entered(",
            ),
        ),
        (
            "note_edit_overdub_compose.py",
            "def run_note_edit_overdub_compose(",
            (
                "_ensure_transport_running(",
                "_send_short_press(",
                "_fader1_select_nav_slot_index(",
                "_ensure_note_edit_entered(",
            ),
        ),
        (
            "edit_overdub_during_note_edit.py",
            "def run_edit_overdub_during_note_edit(",
            (
                "_ensure_transport_running(",
                "_send_short_press(",
                "_send_long_press(",
                "_stop_transport_if_running(",
            ),
        ),
        (
            "windowed_content_parity.py",
            "def run_post_leg_phase(",
            ("switch_leg(", "press_global_transport_stop("),
        ),
    )

    def _body_for(self, case: tuple) -> str:
        file_name, function_marker, _gesture_calls = case
        source = (self._SCENARIO_DIR / file_name).read_text(encoding="utf-8")
        start = source.index(function_marker)
        next_def = re.search(r"\ndef [A-Za-z_]", source[start + 1 :])
        end = start + 1 + next_def.start() if next_def is not None else len(source)
        return source[start:end]

    def test_gate_call_site_precedes_the_control_gesture_call_sites(self) -> None:
        for case in self._CASES:
            file_name, _function_marker, gesture_calls = case
            with self.subTest(scenario=file_name):
                body = self._body_for(case)
                self.assertIn(self._GATE_CALL, body)
                gate_offset = body.index(self._GATE_CALL)
                gesture_offsets = [
                    body.index(name) for name in gesture_calls if name in body
                ]
                self.assertTrue(
                    gesture_offsets, f"{file_name}: the tripwire must find a gesture call site"
                )
                self.assertLess(
                    gate_offset,
                    min(gesture_offsets),
                    f"{file_name}: wait_for_boot_handoff must precede the first control gesture",
                )

    def test_gate_reads_the_scenarios_own_collector_and_refuses_by_name(self) -> None:
        for case in self._CASES:
            file_name, _function_marker, _gesture_calls = case
            with self.subTest(scenario=file_name):
                body = self._body_for(case)
                match = re.search(r"wait_for_boot_handoff\((\w+)\)", body)
                self.assertIsNotNone(match, f"{file_name}: no collector passed to the gate")
                collector_var = match.group(1)
                self.assertIn(f"if {collector_var} is not None:", body)
                self.assertIn("refusing to send control gestures", body)
                self.assertIn("raise SystemExit(3)", body)

if __name__ == "__main__":
    unittest.main()
