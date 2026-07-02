#!/usr/bin/env python3
"""Unit tests for geometry-driver F1 motor serial verification."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))


FEEDBACK_IGNORE_PERIOD_MS = 1500


def verify_geometry_fader1_no_select_apply_after_flush(
    lines: list[str],
    *,
    guard_window_s: float = FEEDBACK_IGNORE_PERIOD_MS / 1000.0,
) -> dict[str, object]:
    """After geometry_motor_sync sent, F1 echo must not trigger select apply or exit Move state."""
    geometry_sent_re = re.compile(r"geometry_motor_sync sent=1")
    select_apply_re = re.compile(r"#DBG select_apply .* apply=1")
    exit_move_re = re.compile(r"Exited EditStartNoteState")
    ts_re = re.compile(r"^\[(\d+\.\d+)\]")

    flush_times: list[float] = []
    select_apply_in_window = 0
    exit_move_in_window = 0

    for line in lines:
        ts_match = ts_re.search(line)
        if not ts_match:
            continue
        t = float(ts_match.group(1))

        if geometry_sent_re.search(line):
            flush_times.append(t)
            continue

        for flush_t in flush_times:
            if flush_t <= t <= flush_t + guard_window_s:
                if select_apply_re.search(line):
                    select_apply_in_window += 1
                if exit_move_re.search(line):
                    exit_move_in_window += 1
                break

    ok = (
        len(flush_times) > 0
        and select_apply_in_window == 0
        and exit_move_in_window == 0
    )
    return {
        "ok": ok,
        "flush_count": len(flush_times),
        "select_apply_in_window": select_apply_in_window,
        "exit_move_in_window": exit_move_in_window,
        "guard_window_s": guard_window_s,
    }


def _pb_wire(pb: int) -> tuple[int, int]:
    raw = pb + 8192
    return raw & 0x7F, (raw >> 7) & 0x7F


def verify_geometry_fader1_motor_flush(
    lines: list[str],
    *,
    motor_idle_s: float = 0.3,
    motor_window_s: float = 0.45,
) -> dict[str, object]:
    """Expect F1 MO cluster after geometry schedule, without F2 MO in same window."""
    schedule_re = re.compile(
        r"selection_motor_sync scheduled=1 driver=geometry.*bracket_tick=(\d+)"
    )
    geometry_sent_re = re.compile(r"geometry_motor_sync sent=1")
    mo_f1_re = re.compile(r"MO,224,16,(\d+),(\d+)")
    mo_f2_re = re.compile(r"MO,224,14,")

    last_schedule_t: float | None = None
    last_bracket: int | None = None
    flush_t: float | None = None
    f1_mo_count = 0
    f2_mo_in_window = 0

    ts_re = re.compile(r"^\[(\d+\.\d+)\]")

    for line in lines:
        ts_match = ts_re.search(line)
        if not ts_match:
            continue
        t = float(ts_match.group(1))

        sched = schedule_re.search(line)
        if sched:
            last_schedule_t = t
            last_bracket = int(sched.group(1))
            continue

        if geometry_sent_re.search(line) and last_schedule_t is not None:
            if t - last_schedule_t >= motor_idle_s:
                flush_t = t
            continue

        if flush_t is not None and t <= flush_t + motor_window_s:
            if mo_f1_re.search(line):
                f1_mo_count += 1
            if mo_f2_re.search(line):
                f2_mo_in_window += 1

    ok = (
        last_schedule_t is not None
        and flush_t is not None
        and f1_mo_count >= 3
        and f2_mo_in_window == 0
    )
    return {
        "ok": ok,
        "last_schedule_t": last_schedule_t,
        "flush_t": flush_t,
        "last_bracket": last_bracket,
        "f1_mo_count": f1_mo_count,
        "f2_mo_in_window": f2_mo_in_window,
    }


class GeometryFader1SerialVerifyTests(unittest.TestCase):
    def _geometry_bundle(self, schedule_t: float, *, bracket: int = 1110, pb: int = 4026) -> list[str]:
        d1, d2 = _pb_wire(pb)
        motor_t = schedule_t + 0.35
        return [
            f"[{schedule_t:.3f}] [INFO] #DBG selection_motor_sync scheduled=1 driver=geometry f1=1 bracket_tick={bracket}",
            f"[{motor_t:.3f}] [INFO] #DBG geometry_motor_sync sent=1 bracket_tick={bracket} pb={pb} mode=GEOMETRY_SYNC sequence=timed_burst",
            f"[{motor_t + 0.01:.3f}] #CAP,1,MO,224,16,{d1},{d2}",
            f"[{motor_t + 0.05:.3f}] #CAP,1,MO,224,16,{d1},{d2}",
            f"[{motor_t + 0.09:.3f}] #CAP,1,MO,224,16,{d1},{d2}",
            f"[{motor_t + 0.10:.3f}] #CAP,1,MO,144,16,0,127",
            f"[{motor_t + 0.20:.3f}] #CAP,1,MO,128,16,0,0",
        ]

    def test_geometry_flush_detects_f1_burst_without_f2(self) -> None:
        result = verify_geometry_fader1_motor_flush(self._geometry_bundle(18.5))
        self.assertTrue(result["ok"])
        self.assertEqual(result["f1_mo_count"], 3)
        self.assertEqual(result["f2_mo_in_window"], 0)

    def test_geometry_flush_fails_when_f2_present(self) -> None:
        lines = self._geometry_bundle(18.5)
        lines.insert(4, "[18.860] #CAP,1,MO,224,14,87,37")
        result = verify_geometry_fader1_motor_flush(lines)
        self.assertFalse(result["ok"])
        self.assertGreater(result["f2_mo_in_window"], 0)

    def test_geometry_flush_fails_before_idle(self) -> None:
        lines = self._geometry_bundle(18.5)
        lines[1] = "[18.520] [INFO] #DBG geometry_motor_sync sent=1 bracket_tick=1110 pb=4026 mode=GEOMETRY_SYNC sequence=timed_burst"
        result = verify_geometry_fader1_motor_flush(lines)
        self.assertFalse(result["ok"])

    def _geometry_guard_bundle(self, flush_t: float, *, bracket: int = 1011, pb: int = 3003) -> list[str]:
        """session_20260702_183747 pattern: geometry flush then F1 echo off by >100."""
        schedule_t = flush_t - 0.35
        echo_pb = 3201
        d1, d2 = _pb_wire(echo_pb)
        return [
            f"[{schedule_t:.3f}] [INFO] #DBG selection_motor_sync scheduled=1 driver=geometry f1=1 bracket_tick={bracket}",
            f"[{flush_t:.3f}] [INFO] #DBG geometry_motor_sync sent=1 bracket_tick={bracket} pb={pb} mode=GEOMETRY_SYNC sequence=timed_burst",
            f"[{flush_t + 0.011:.3f}] MI,H,224,16,{d1},{d2}",
        ]

    def test_geometry_flush_blocks_select_apply_on_f1_echo(self) -> None:
        result = verify_geometry_fader1_no_select_apply_after_flush(
            self._geometry_guard_bundle(33.469)
        )
        self.assertTrue(result["ok"])
        self.assertEqual(result["select_apply_in_window"], 0)
        self.assertEqual(result["exit_move_in_window"], 0)

    def test_geometry_guard_fails_when_select_apply_present(self) -> None:
        lines = self._geometry_guard_bundle(33.469)
        lines.append("[33.481] #DBG select_apply bracket_tick=1011 note_idx=42 slot=1 apply=1 reason=note_changed")
        result = verify_geometry_fader1_no_select_apply_after_flush(lines)
        self.assertFalse(result["ok"])
        self.assertEqual(result["select_apply_in_window"], 1)

    def test_geometry_guard_fails_when_move_state_exits(self) -> None:
        lines = self._geometry_guard_bundle(40.129, bracket=1107)
        lines.append("[40.134] Exited EditStartNoteState")
        result = verify_geometry_fader1_no_select_apply_after_flush(lines)
        self.assertFalse(result["ok"])
        self.assertEqual(result["exit_move_in_window"], 1)


if __name__ == "__main__":
    unittest.main()
