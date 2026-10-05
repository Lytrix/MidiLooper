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

from hitl.control_constants import (
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
)
from hitl.scenarios.windowed_content_parity import (
    last_transport_row_running,
    press_record_for_active_slot,
    run_post_leg_phase,
    switch_leg,
    split_switch_target_slot_flag,
    switch_target_slot_from,
    serial_port_from,
    press_global_transport_stop,
    run_windowed_content_parity,
    select_loop_slot,
    split_loop_slot_flag,
)
from hitl.verify.windowed_lane import (
    gap_rows,
    row_us,
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
        lines.append(_receipt(2_000_000 + index, revision=1))
    for index in range(gaps):
        lines.append(_gap(2_100_000 + index))
    for index in range(overruns):
        lines.append(_overrun(2_200_000 + index))
    for index in range(completions):
        lines.append(_build(2_300_000 + index))
    lines.append("[9.000] [INFO] Transport stopped")
    return lines


class WindowedLaneTests(unittest.TestCase):
    def test_transport_intervals_are_read_from_device_time(self) -> None:
        """The edge stamps are the clock: both families share it, so a **retained** row keeps its own time."""
        lines = [
            "[1.000] [INFO] Transport started",
            "#CAP,1500000,DIAG,win_gap,0,4,0,1,0,0,0,0,0,0,1,0,64,0,0,3",
            "[2.500] [INFO] Transport stopped",
            "[3.000] [INFO] Transport started",
            "#CAP,4000000,DIAG,win_gap,0,4,0,1,0,0,0,0,0,0,1,0,64,0,0,3",
        ]
        self.assertEqual([(1_000_000, 2_500_000), (3_000_000, 4_000_000)], running_intervals(lines))
        # A retained row delivered after the stop is still "under transport": that is what the drain is for.
        self.assertEqual(4_000_000, row_us("#CAP,4000000,DIAG,win_gap"))
        self.assertEqual(2_500_000, row_us("[2.500] [INFO] Transport stopped"))
        self.assertIsNone(row_us("plain text"))

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
        lines = ["[1.000] [INFO] Transport started", _gap(2_100_000), _build(2_300_000), _build(2_400_000),
                 "[9.000] [INFO] Transport stopped", _receipt(9_500_000)]
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
        lines = _lane_lines() + ["[20.000] [INFO] Transport started", "[20.500] [INFO] Transport stopped",
                                 _overrun(21_000_000, running=0)]
        quiet = verify_windowed_lane(lines)
        self.assertTrue(quiet["ok"], quiet["issues"])

    def test_coverage_blocks_without_opportunities_are_not_coverage(self) -> None:
        lines = [
            "[1.000] [INFO] Transport started",
            _receipt(2_000_000),
            _gap(2_100_000, opportunities=0),
            _build(2_300_000),
            _build(2_400_000),
            "[9.000] [INFO] Transport stopped",
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
            "[1.000] [INFO] Transport started",
            _gap(2_100_000, opportunities=11776, running=0, eligible=0, progress=0, denied=0),
            _gap(2_200_000, opportunities=22784, running=0, eligible=0, progress=0, denied=0),
            "[9.000] [INFO] Transport stopped",
        ]
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        self.assertTrue(result["issues"][0].startswith("window_demand_observed"), result["issues"])
        names = [check["name"] for check in result["checks"]]
        self.assertEqual("window_demand_observed", names[0])

    def test_a_single_eligible_turn_is_enough_to_call_the_lane_exercised(self) -> None:
        lines = [
            "[1.000] [INFO] Transport started",
            _gap(2_100_000, opportunities=4, running=1, eligible=1, progress=1),
            _receipt(2_200_000),
            _build(2_300_000),
            _build(2_400_000),
            "[9.000] [INFO] Transport stopped",
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

    def test_the_serial_port_is_found_wherever_the_runner_kept_it(self) -> None:
        class _Direct:
            serial_port = "/dev/cu.direct"

        class _Legacy:
            legacy_args = ["--track-number", "8", "--serial-port", "/dev/cu.legacy"]

        class _Neither:
            legacy_args = ["--track-number", "8"]

        self.assertEqual("/dev/cu.direct", serial_port_from(_Direct()))
        self.assertEqual("/dev/cu.legacy", serial_port_from(_Legacy()))
        self.assertIsNone(serial_port_from(_Neither()))

    def test_the_transport_edge_is_read_from_the_last_one_in_the_capture(self) -> None:
        """The stop press is a toggle: sending it to a stopped transport would start one, the opposite of a drain."""
        self.assertIsNone(last_transport_row_running(["nothing here"]))
        self.assertTrue(last_transport_row_running(["Transport started", "x", "Transport stopped", "Transport started"]))
        self.assertFalse(last_transport_row_running(["Transport started", "x", "Transport stopped"]))

    def test_the_transport_stop_press_uses_the_global_transport_control(self) -> None:
        port = _StubPort()
        press_global_transport_stop(port, press_ms=1)
        self.assertEqual(
            [(GLOBAL_TRANSPORT_NOTE, CONTROL_CHANNEL_1BASED - 1)] * 2,
            port.sent,
        )

    def test_the_switch_target_flag_is_absent_by_default(self) -> None:
        """The base leg keeps its behaviour: no flag means no switch leg, so the two shapes stay comparable."""
        self.assertIsNone(switch_target_slot_from(["--track-number", "8", "--loop-slot", "6"]))
        self.assertEqual(3, switch_target_slot_from(["--switch-target-slot", "3"]))

    def test_the_record_press_uses_the_record_control(self) -> None:
        port = _StubPort()
        press_record_for_active_slot(port, press_ms=1)
        self.assertEqual([(RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED - 1)] * 2, port.sent)

    def test_the_switch_leg_needs_a_running_transport_and_a_named_slot(self) -> None:
        """Both halves of the post-leg phase are gated on the transport: a switch (or a stop press, which toggles)
        performed while it is stopped is not the regime the leg exists for."""
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            (out_dir / "host_midi_hitl_long_loop_display_serial_20260101_000000.log").write_text(
                "[1.000] [INFO] Transport started\n[2.000] [INFO] Transport stopped\n", encoding="utf-8"
            )

            class _Args:
                serial_port = "/dev/cu.stub"
                legacy_args: list[str] = []

            args = _Args()
            args.out_dir = out_dir
            # The capture ends stopped, so neither half runs -- and no port is even opened.
            self.assertIsNone(run_post_leg_phase(args, switch_target_slot=3))

    def test_the_wrappers_own_flags_never_reach_the_reused_leg(self) -> None:
        """The leg's parser is strict: a wrapper-owned flag forwarded to it aborts the run before it drives anything."""
        slot, remaining = split_loop_slot_flag(["--track-number", "8", "--loop-slot", "6"])
        self.assertEqual(6, slot)
        self.assertNotIn("--loop-slot", remaining)
        target, remaining = split_switch_target_slot_flag(remaining + ["--switch-target-slot", "4", "--midi-channel", "8"])
        self.assertEqual(4, target)
        self.assertNotIn("--switch-target-slot", remaining)
        self.assertEqual(["--track-number", "8", "--midi-channel", "8"], remaining)

    def test_the_switch_leg_selects_the_target_before_pressing_its_record_control(self) -> None:
        """The order is the safety argument: the press acts on the *selected* slot."""
        port = _StubPort()
        switch_leg(port, 3, settle_s=0.0)
        self.assertEqual(
            [(LOOP_SELECT_NOTE_BASE + 2, CONTROL_CHANNEL_1BASED - 1)] * 2
            + [(RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED - 1)] * 2,
            port.sent,
        )
        with self.assertRaises(ValueError):
            switch_leg(port, 9, settle_s=0.0)

    def test_the_slot_select_presses_that_slots_own_control(self) -> None:
        port = _StubPort()
        select_loop_slot(port, 6, press_ms=1)
        self.assertEqual(
            [(LOOP_SELECT_NOTE_BASE + 5, CONTROL_CHANNEL_1BASED - 1)] * 2,
            port.sent,
        )


if __name__ == "__main__":
    unittest.main()
