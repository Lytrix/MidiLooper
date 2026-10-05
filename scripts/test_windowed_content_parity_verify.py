#!/usr/bin/env python3
"""Unit tests for the windowed lane's checks and task 6.4's windowed-profile verifier.

Rows are built exactly as `DebugSessionCapture` emits them (the formats were read from the emitters, not invented):

* `#CAP,<us>,DIAG,win_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>`
* `#CAP,<us>,DIAG,win_gap,<maxDelta>,<opportunities>,<phase>,<running>,<discontinuities>,<eligibleMax>,<eligibleTurns>,
   <progressTurns>,<deniedTurns>,<maxWorkerUs>,<epoch>,<saturatedGaps>,<permitted>,<overruns>,<overrunMax>,<schema>`
* `#CAP,<us>,DIAG,win_gap_overrun,<permitted>,<overruns>,<overrunMax>,<running>,<epoch>`
* `#CAP,<us>,DIAG,playback_build,<path>,0,<loopLen>,0,<revision>,<track>,<slot>,0,0,0,0,0`

The transport's own rows are human lines, and the stream's ordering is what "under transport" is read from.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.control_constants import CONTROL_CHANNEL_1BASED, LOOP_SELECT_NOTE_BASE
from hitl.scenarios.windowed_content_parity import (
    run_windowed_content_parity,
    select_loop_slot,
    split_loop_slot_flag,
)
from hitl.verify.windowed_lane import (
    gap_rows,
    overrun_rows,
    readiness_rows,
    running_intervals,
    verify_windowed_content_parity,
    verify_windowed_lane,
)

TICKS_PER_BAR = 768
LONG_LOOP = 24 * TICKS_PER_BAR


def _receipt(
    us: int,
    *,
    track: int = 8,
    slot: int = 4,
    readiness: str = "Ready",
    holder: str = "none",
    revision: int = 1,
    lead: int = 0,
) -> str:
    return f"#CAP,{us},DIAG,win_readiness,{track},{slot},{readiness},{holder},{revision},{lead}"


def _gap(
    us: int,
    *,
    opportunities: int = 4,
    running: int = 1,
    permitted: int = 1,
    overruns: int = 0,
    overrun_max: int = 0,
    schema: int = 3,
    phase: int = 100,
    max_delta: int = 64,
    discontinuities: int = 0,
    eligible_max: int = 1,
    eligible: int = 4,
    progress: int = 4,
    denied: int = 0,
    worker_us: int = 20,
    epoch: int = 1,
    saturated: int = 0,
) -> str:
    return (
        f"#CAP,{us},DIAG,win_gap,{max_delta},{opportunities},{phase},{running},{discontinuities},{eligible_max},"
        f"{eligible},{progress},{denied},{worker_us},{epoch},{saturated},{permitted},{overruns},{overrun_max},{schema}"
    )


def _overrun(us: int, *, permitted: int = 1, overruns: int = 2, overrun_max: int = 5, running: int = 1) -> str:
    return f"#CAP,{us},DIAG,win_gap_overrun,{permitted},{overruns},{overrun_max},{running},1"


def _build(us: int, *, path: str = "build_complete", loop_len: int = LONG_LOOP, revision: int = 1) -> str:
    return f"#CAP,{us},DIAG,playback_build,{path},0,{loop_len},0,{revision},8,4,0,0,0,0,0"


def _disp_window(line_index: int, *, loop_len: int = LONG_LOOP, window_start: int, state: str = "PLAYING") -> str:
    return f"#CAP,{line_index},DISP,0,{state},{loop_len},32,8,4,32,1,{window_start},16,2"


def _leg_passing_lines() -> list[str]:
    """The reused long-loop leg's own oracle, in the minimal shape its verifier accepts."""
    return [
        "MIDI Encoder: Short press - entered note edit mode",
        _disp_window(10, window_start=4096),
        _disp_window(20, window_start=4096),
        _disp_window(30, window_start=4096),
        "Detailed window centered on playhead",
        _disp_window(40, window_start=6144),
        _disp_window(50, window_start=6400),
        _disp_window(60, window_start=6656),
        _disp_window(70, window_start=6912),
    ]


def _leg_args() -> SimpleNamespace:
    return SimpleNamespace(
        record_bars=24,
        hold_track_ms=4000,
        phase_markers=[
            "entered note edit mode",
            "phase:freeze_wait_end",
            "phase:long_press_snap",
            "phase:hold_track_start",
            "phase:hold_track_end",
        ],
    )


def _lane_lines(*, receipts: int = 1, gaps: int = 1, overruns: int = 0, completions: int = 2) -> list[str]:
    """A running-transport interval with the requested lane rows inside it."""
    lines = ["[1.000] [INFO] Transport started"]
    for index in range(receipts):
        lines.append(_receipt(1000 + index, revision=1))
    for index in range(gaps):
        lines.append(_gap(1100 + index))
    for index in range(overruns):
        lines.append(_overrun(1200 + index))
    for index in range(completions):
        lines.append(_build(1300 + index))
    lines.append("[9.000] [INFO] Transport stopped")
    return lines


class WindowedLaneTests(unittest.TestCase):
    def test_transport_intervals_are_read_from_the_stream_order(self) -> None:
        lines = ["Transport started", "row", "Transport stopped", "after", "Transport started", "still running"]
        self.assertEqual([(0, 2), (4, 6)], running_intervals(lines))

    def test_row_parsers_read_the_emitted_field_positions(self) -> None:
        lines = [_receipt(1000, readiness="OwesBuild", holder="WindowBuild", revision=7, lead=64)]
        row = readiness_rows(lines)[0]
        self.assertEqual(("8", "4", "OwesBuild", "WindowBuild", "7", "64"),
                         (row["track"], row["slot"], row["readiness"], row["holder"], row["revision"], row["lead_ticks"]))
        gap = gap_rows([_gap(2000, opportunities=9, running=1, permitted=2, schema=3)])[0]
        self.assertEqual(("9", "1", "2", "3"),
                         (gap["opportunities"], gap["running"], gap["permitted"], gap["schema"]))
        over = overrun_rows([_overrun(3000, overruns=2, overrun_max=5, running=1)])[0]
        self.assertEqual(("2", "5", "1"), (over["overruns"], over["overrun_max"], over["running"]))

    def test_healthy_lane_judges_two_terms_and_names_the_deferred_ones(self) -> None:
        result = verify_windowed_lane(_lane_lines())
        self.assertTrue(result["ok"], result["issues"])
        self.assertFalse(result["oracle_complete"])
        names = [check["name"] for check in result["checks"]]
        self.assertEqual(
            [
                "window_demand_observed",
                "readiness_receipts_under_transport",
                "windowed_service_coverage_under_transport",
                "no_windowed_overrun_under_transport",
                "window_boundaries_crossed_under_transport",
            ],
            names,
        )
        declared = {entry["term"] for entry in result["deferred"]}
        self.assertEqual(
            {"content_parity_across_window_boundary", "root_demoted_residency_over_loop_lengths"}, declared
        )
        # Each deferred term names the surface to ask, so a reader is never left to guess who owns the verdict.
        for entry in result["deferred"]:
            self.assertTrue(entry["verdict_owner"])

    def test_a_receipt_only_after_the_transport_stopped_fails(self) -> None:
        """AC-8: the receipt is emitted where it is produced, so a post-stop arrival is not a pass."""
        lines = ["Transport started", _gap(1100), _build(1200), _build(1300), "Transport stopped", _receipt(9000)]
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        self.assertIn("readiness_receipts_under_transport", result["issues"][0])
        self.assertIn("under_transport=0", result["issues"][0])

    def test_an_overrun_raised_under_transport_fails_by_name(self) -> None:
        result = verify_windowed_lane(_lane_lines(overruns=1))
        self.assertFalse(result["ok"])
        self.assertTrue(any(issue.startswith("no_windowed_overrun_under_transport") for issue in result["issues"]))
        # The same row **after** the transport stopped is not an under-transport exceedance: the check reads the
        # interval, not the mere presence of the row.
        lines = _lane_lines() + ["Transport started", "Transport stopped", _overrun(9000, running=0)]
        quiet = verify_windowed_lane(lines)
        self.assertTrue(quiet["ok"], quiet["issues"])

    def test_coverage_blocks_without_opportunities_are_not_coverage(self) -> None:
        lines = [
            "Transport started",
            _receipt(1000),
            _gap(1100, opportunities=0),
            _build(1200),
            _build(1300),
            "Transport stopped",
        ]
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        self.assertTrue(
            any(issue.startswith("windowed_service_coverage_under_transport") for issue in result["issues"])
        )

    def test_a_capture_without_a_window_demand_names_the_unexercised_leg(self) -> None:
        """The first authorized device run's shape: dispatches with nothing owed are not three separate lane defects.

        With no eligible turn and no receipt anywhere, the leg was never asked for window work -- the rolling lane is
        compiled off on that image and no launch was owed on a plain record-and-play. The verifier says that first, so a
        reader does not conclude the lane misbehaved.
        """
        lines = [
            "Transport started",
            _gap(1100, opportunities=11776, running=0, eligible=0, progress=0, denied=0),
            _gap(1200, opportunities=22784, running=0, eligible=0, progress=0, denied=0),
            "Transport stopped",
        ]
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        self.assertTrue(result["issues"][0].startswith("window_demand_observed"), result["issues"])
        names = [check["name"] for check in result["checks"]]
        self.assertEqual("window_demand_observed", names[0])

    def test_a_single_eligible_turn_is_enough_to_call_the_lane_exercised(self) -> None:
        lines = [
            "Transport started",
            _gap(1100, opportunities=4, running=1, eligible=1, progress=1),
            _receipt(1200),
            _build(1300),
            _build(1400),
            "Transport stopped",
        ]
        result = verify_windowed_lane(lines)
        demand = next(check for check in result["checks"] if check["name"] == "window_demand_observed")
        self.assertTrue(demand["ok"])
        self.assertTrue(result["ok"], result["issues"])

    def test_one_publication_is_not_a_boundary_crossing(self) -> None:
        result = verify_windowed_lane(_lane_lines(completions=1))
        self.assertFalse(result["ok"])
        self.assertTrue(
            any(issue.startswith("window_boundaries_crossed_under_transport") for issue in result["issues"])
        )

    def test_the_profile_verifier_requires_both_halves(self) -> None:
        """The reused leg's oracle and the lane's checks must both pass; either failing is not a pass."""
        args = _leg_args()
        healthy = _leg_passing_lines() + _lane_lines()
        both = verify_windowed_content_parity(healthy, args)
        self.assertTrue(both["ok"], both["issues"])
        self.assertFalse(both["oracle_complete"])

        leg_only = verify_windowed_content_parity(_leg_passing_lines(), args)
        self.assertFalse(leg_only["ok"], leg_only["issues"])
        self.assertTrue(all(issue.startswith("lane: ") for issue in leg_only["issues"]), leg_only["issues"])

        lane_only = verify_windowed_content_parity(_lane_lines(), args)
        self.assertFalse(lane_only["ok"], lane_only["issues"])
        self.assertTrue(any(issue.startswith("leg: ") for issue in lane_only["issues"]), lane_only["issues"])


class _StubPort:
    """A MIDI out port that records what it was asked to send (the `test_record_target_precondition` pattern)."""

    def __init__(self) -> None:
        self.sent: list[tuple[int, int]] = []

    def send(self, message: object) -> None:
        self.sent.append((int(message.note), int(message.channel)))  # type: ignore[attr-defined]


class WindowedTargetTests(unittest.TestCase):
    """The wrapper owns exactly one thing: which slot the reused leg will clear and record on."""

    def test_the_slot_flag_is_taken_out_of_the_legacy_args(self) -> None:
        slot, remaining = split_loop_slot_flag(["--track-number", "8", "--loop-slot", "6", "--midi-channel", "8"])
        self.assertEqual(6, slot)
        # The leg's parser is strict and has no slot flag: leaving it in is how the run would fail to start.
        self.assertNotIn("--loop-slot", remaining)
        self.assertEqual(["--track-number", "8", "--midi-channel", "8"], remaining)

    def test_a_repeated_slot_flag_is_not_silently_ignored(self) -> None:
        slot, remaining = split_loop_slot_flag(["--loop-slot", "6", "--loop-slot", "7"])
        self.assertEqual(6, slot)
        self.assertEqual(["--loop-slot", "7"], remaining)

    def test_a_run_without_a_named_slot_is_refused_before_any_device_contact(self) -> None:
        class _Args:
            legacy_args: list[str] = []

        self.assertEqual(2, run_windowed_content_parity(_Args()))

    def test_the_slot_select_presses_that_slots_own_control(self) -> None:
        port = _StubPort()
        select_loop_slot(port, 6, press_ms=1)
        self.assertEqual(
            [(LOOP_SELECT_NOTE_BASE + 5, CONTROL_CHANNEL_1BASED - 1)] * 2,
            port.sent,
        )


if __name__ == "__main__":
    unittest.main()
