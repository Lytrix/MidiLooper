#!/usr/bin/env python3
"""Tests for the boot-handoff gate that keeps host control gestures out of an unfinished boot.

The behaviour under test is the owner's 2026-10-12 panel report ("something in the test is
starting during bootup so too soon which makes the display hang after boot"), pinned by
`captures/i3a-edge-effect-boot-rerun-t8s4-03` / `-01` (first host press ~7.4 s, boot handoff
`DisplayManager: Boot setup complete` ~15-17 s and `boot_commit` ~17.5 s) and
`captures/i3a-edge-effect-boot-rerun-t8s4-02` (a press into the unfinished boot read as 1 ms and
cycled LOOP_EDIT -> NOTE_EDIT). The gate is row evidence, never a sleep, and never writes.

Run: .venv/bin/python scripts/test_hitl_boot_handoff.py
"""

from __future__ import annotations

import sys
import time
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.boot_handoff import (  # noqa: E402
    BOOT_BANNER_MARKER,
    BOOT_COMMIT_ROW,
    BOOT_SETUP_COMPLETE_ROW,
    boot_handoff_complete,
    fresh_boot_in_window,
    wait_for_boot_handoff,
)

_BANNER = "#CAP,2164003,DIAG,boot_reset,crash=0"
_SETUP = "DisplayManager: Boot setup complete"
_COMMIT = "#CAP,17838519,DIAG,loop_rem,boot_commit,387560,255,255,255,0"


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

    def test_both_rows_after_the_banner_complete_the_handoff(self) -> None:
        lines = ["[Memory] heap", _BANNER, _SETUP, _COMMIT]
        self.assertTrue(fresh_boot_in_window(lines))
        self.assertTrue(boot_handoff_complete(lines))

    def test_one_handoff_row_alone_is_not_complete(self) -> None:
        self.assertFalse(boot_handoff_complete([_BANNER, _SETUP]))
        self.assertFalse(boot_handoff_complete([_BANNER, _COMMIT]))

    def test_no_banner_is_not_a_fresh_boot(self) -> None:
        self.assertFalse(fresh_boot_in_window(["#CAP,1,BPM,120", _SETUP, _COMMIT]))

    def test_a_banner_handoff_pair_outside_the_examined_window_is_not_complete(self) -> None:
        # A second boot banner restarts the handoff window: the first boot's rows no longer count.
        lines = [_BANNER, _SETUP, _COMMIT, "#CAP,99,DIAG,boot_reset,crash=1"]
        self.assertTrue(fresh_boot_in_window(lines))
        self.assertFalse(boot_handoff_complete(lines))


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

    def test_both_rows_complete_immediately_without_writing(self) -> None:
        collector = _FakeCollector([_BANNER, _SETUP, _COMMIT])
        self.assertEqual("complete", wait_for_boot_handoff(collector))
        self.assertEqual(1, collector.snapshot_calls)
        self.assertEqual([], collector.writes)

    def test_handoff_rows_that_arrive_while_waiting_complete(self) -> None:
        collector = _FakeCollector([_BANNER])

        original_snapshot = collector.snapshot

        def snapshot() -> list[str]:
            lines = original_snapshot()
            if collector.snapshot_calls >= 3:
                lines.extend([_SETUP, _COMMIT])
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


if __name__ == "__main__":
    unittest.main()
