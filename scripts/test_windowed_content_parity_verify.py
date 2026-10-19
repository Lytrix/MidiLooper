#!/usr/bin/env python3
"""Unit tests for the windowed lane's checks and task 6.4's windowed-profile verifier.

Rows are built exactly as `DebugSessionCapture` emits them (the formats were read from the emitters, not invented):

* `#CAP,<us>,DIAG,win_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>`
* `#CAP,<us>,DIAG,win_frozen_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>,<sourceGeneration>,
   <captureToken>,<start>,<length>,<loopLength>` — the frozen Record-return completion's **source-qualified** receipt
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
    EDIT_BUTTON_NOTE,
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
)
from hitl.scenarios.windowed_content_parity import (
    last_transport_row_running,
    press_edit_exit,
    press_record_for_active_slot,
    run_post_leg_phase,
    switch_leg,
    split_switch_target_slot_flag,
    split_switch_settle_flag,
    switch_target_slot_from,
    serial_port_from,
    press_global_transport_stop,
    run_windowed_content_parity,
    select_loop_slot,
    split_loop_slot_flag,
)
from hitl.verify.windowed_lane import (
    demand_entry_summary,
    frozen_readiness_rows,
    frozen_receipt_is_source_qualified,
    gap_rows,
    gate_rows,
    row_us,
    overrun_rows,
    readiness_rows,
    running_intervals,
    verify_windowed_content_parity,
    verify_windowed_lane,
)
from hitl.verify.record_snapshot import snapshot_gate_rows, verify_record_snapshot_progress

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



def _frozen_receipt(
    us: int,
    *,
    track: int = 8,
    slot: int = 4,
    readiness: str = "complete_ready",
    holder: str = "clock_edge",
    revision: int = 1,
    lead: int = 1,
    source_generation: int = 1,
    capture_token: int = 7,
    start: int = LONG_LOOP - TICKS_PER_BAR,
    length: int = TICKS_PER_BAR,
    loop_length: int = LONG_LOOP,
) -> str:
    """The frozen Record-return completion's own receipt, in `frozenReadinessReceiptImmediate`'s field order."""
    return (
        f"#CAP,{us},DIAG,win_frozen_readiness,{track},{slot},{readiness},{holder},{revision},{lead},"
        f"{source_generation},{capture_token},{start},{length},{loop_length}"
    )


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


def _gate(
    us: int,
    *,
    exit: str,
    track: int = 8,
    slot: int = 4,
    loop_len: int = LONG_LOOP,
    rq: int = 0,
    ep: int = 1,
    av: int = 1,
    al: int = 1,
    pl: int = 1,
    ed: int = 0,
    od: int = 0,
    pv: int = 0,
    rw: int = 0,
    sv: int = 1,
    rt: int = 1,
    rl: int = 1,
    ow: int = 0,
    dm: int = 0,
    bld: int = 0,
    gr: int = 0,
    rp: int = 0,
) -> str:
    """The demand path's gate row, in `DebugSessionCapture::winGateRow`'s own field order."""
    return (
        f"#CAP,{us},DIAG,win_gate,t={track},s={slot},x={exit},len={loop_len},ep={ep},av={av},al={al},pl={pl},ed={ed},"
        f"od={od},pv={pv},rw={rw},sv={sv},rt={rt},rl={rl},ow={ow},dm={dm},bld={bld},gr={gr},rp={rp},rq={rq}"
    )

def _snapshot_gate(us: int, *, rec: int, sep: int, p: int, fc: int, elig: int, st: int, cu: int, rv: int, hp: int = 1, track: int = 7, slot: int = 3) -> str:
    return (f"#CAP,{us},DIAG,snap_gate,t={track},s={slot},a=3,u=2,p={p},sep={sep},fc={fc},wj=0,hp={hp},lr=1,vc=0,"
            f"rec={rec},play=0,od=0,step=1,cl=1,idle=1,elig={elig},st={st},cu={cu},rv={rv}")
def _snapshot_progress_lines() -> list[str]:
    return [
        "[1.000] [INFO] Transport started",
        _snapshot_gate(1_500_000, rec=1, sep=0, p=0, fc=1, elig=1, st=8, cu=32, rv=1),
        "[2.000] [DEBUG] Recording stopped @ tick 18432 (recStart=0 length=18432)",
        _snapshot_gate(3_000_000, rec=0, sep=1, p=1, fc=1, elig=1, st=16, cu=64, rv=1),
        _snapshot_gate(4_000_000, rec=0, sep=0, p=0, fc=0, elig=1, st=20, cu=0, rv=2),
    ]

def _ring_stats(us: int, *, tier_a_evict: int = 0) -> str:
    return (f"#CAP,{us},RING,stats,attempts=0,ok=0,flush=0,tx=0,shed=0,tierA_evict={tier_a_evict},"
            "overflow=0,crit_rej=0,wipe=0,wipe_bytes=0")
def _no_demand_lines(*gates: str) -> list[str]:
    """A running-transport capture with nothing owed: a dispatch block, zero eligible turns, and the gate rows."""
    lines = ["[1.000] [INFO] Transport started"]
    lines.extend(gates)
    lines.append(_gap(2_100_000, opportunities=22784, running=0, eligible=0, progress=0, denied=0))
    lines.append("[9.000] [INFO] Transport stopped")
    return lines


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
        track_number=8,
        loop_slot=4,
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


def _lane_lines(*, receipts: int = 1, gaps: int = 1, overruns: int = 0, completions: int = 2, frozen: int = 1,
                frozen_token: int = 7) -> list[str]:
    """A running-transport interval with the requested lane rows inside it."""
    lines = [_ring_stats(500_000), "[1.000] [INFO] Transport started"]
    for index in range(receipts):
        lines.append(_receipt(2_000_000 + index, revision=1))
    for index in range(frozen):
        lines.append(_frozen_receipt(2_050_000 + index, capture_token=frozen_token))
    for index in range(gaps):
        lines.append(_gap(2_100_000 + index))
    for index in range(overruns):
        lines.append(_overrun(2_200_000 + index))
    for index in range(completions):
        lines.append(_build(2_300_000 + index))
    lines.append("[9.000] [INFO] Transport stopped")
    lines.append(_ring_stats(9_500_000))
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

    def test_gap_row_reads_the_service_and_worker_fields_six_point_five_claims(self) -> None:
        """6.5's hardware claims are these exact fields, so their positions are pinned here.

        Grain duration is the block's worst **worker** span (`maxWorkerUs`, field 13); the maximum service gap is the
        largest delta whose predecessor was *eligible* (`eligiblePredecessorMax`, field 9) compared against the
        declaration's own `permittedAdvanceTicks` (field 16). A parser that silently read a neighbouring field would
        turn both readings into numbers of the wrong quantity, and both would still look plausible.
        """
        row = gap_rows([
            _gap(4000, max_delta=56, discontinuities=2, eligible_max=40, eligible=230, progress=91,
                 denied=1, worker_us=595, epoch=9, saturated=0, permitted=64, overruns=0, overrun_max=0, schema=3)
        ])[0]
        self.assertEqual(
            ("56", "2", "40", "230", "91", "1", "595", "9", "0", "64", "0", "0"),
            (row["max_delta_ticks"], row["discontinuities"], row["eligible_predecessor_max"],
             row["eligible_turns"], row["progress_turns"], row["denied_turns"], row["max_worker_us"],
             row["epoch"], row["saturated_gaps"], row["permitted"], row["overruns"], row["overrun_max_ticks"]),
        )

    def test_healthy_lane_judges_two_terms_and_names_the_deferred_ones(self) -> None:
        result = verify_windowed_lane(_lane_lines())
        self.assertTrue(result["ok"], result["issues"])
        self.assertFalse(result["oracle_complete"])
        names = [check["name"] for check in result["checks"]]
        self.assertEqual(
            [
                "capture_evidence_ring_intact",
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
    def test_tier_a_eviction_invalidates_lane_evidence(self) -> None:
        lines = _lane_lines()
        lines.insert(2, "#CAP,2000001,RING,tier_a_evicted,3,first=166376823")
        result = verify_windowed_lane(lines)
        check = next(item for item in result["checks"] if item["name"] == "capture_evidence_ring_intact")
        self.assertFalse(check["ok"])
        self.assertIn("tier_a_evicted", check["detail"])
        self.assertTrue(any(issue.startswith("capture_evidence_ring_intact") for issue in result["issues"]))

    def test_a_receipt_only_after_the_transport_stopped_fails(self) -> None:
        """AC-8: the receipt is emitted where it is produced, so a post-stop arrival is not a pass."""
        lines = ["[1.000] [INFO] Transport started", _gap(2_100_000), _build(2_300_000), _build(2_400_000),
                 "[9.000] [INFO] Transport stopped", _receipt(9_500_000)]
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        self.assertIn("readiness_receipts_under_transport", result["issues"][0])
        self.assertIn("under_transport=0", result["issues"][0])


    def test_a_generic_receipt_alone_does_not_answer_the_readiness_term(self) -> None:
        """"Receipts under transport" is about the lane's **own window becoming ready**, so a generic
        `win_readiness` row -- for a first Record take, `not_servable` -- must not be accepted for it. This is the
        reading that made the run2 capture look like a pass while no frozen completion was ever named."""
        lines = _lane_lines(frozen=0)
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        issue = next(issue for issue in result["issues"] if issue.startswith("readiness_receipts_under_transport"))
        self.assertIn("under_transport=1", issue)
        self.assertIn("source_qualified=0 of 0 frozen_receipts_under_transport", issue)

    def test_a_frozen_receipt_without_a_content_token_is_not_a_completion(self) -> None:
        """The row is source-qualified only when it names a **real** window: a zero content token or an invalid
        interval is not evidence that a completion happened."""
        lines = _lane_lines(frozen_token=0)
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        issue = next(issue for issue in result["issues"] if issue.startswith("readiness_receipts_under_transport"))
        self.assertIn("source_qualified=0 of 1 frozen_receipts_under_transport", issue)
        # An interval that cannot be a window is refused for the same reason.
        degenerate = _lane_lines()
        degenerate.insert(2, _frozen_receipt(2_060_000, start=0, length=0, loop_length=0))
        row = frozen_readiness_rows(degenerate)[0]
        self.assertFalse(frozen_receipt_is_source_qualified(row))

    def test_the_frozen_receipt_parser_reads_the_emitters_field_order(self) -> None:
        lines = [_frozen_receipt(1000, revision=3, lead=1, source_generation=2, capture_token=900000001)]
        row = frozen_readiness_rows(lines)[0]
        self.assertEqual(
            ("8", "4", "complete_ready", "clock_edge", "3", "1", "2", "900000001",
             str(LONG_LOOP - TICKS_PER_BAR), str(TICKS_PER_BAR), str(LONG_LOOP)),
            (row["track"], row["slot"], row["readiness"], row["holder"], row["revision"], row["lead_ticks"],
             row["source_generation"], row["capture_token"], row["start"], row["length"], row["loop_length"]),
        )
        self.assertTrue(frozen_receipt_is_source_qualified(row))
        # The two receipt families are told apart: a generic row is not read as a frozen one.
        self.assertEqual([], frozen_readiness_rows([_receipt(2000)]))

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
        self.assertEqual("capture_evidence_ring_intact", names[0])

    def test_a_single_eligible_turn_is_enough_to_call_the_lane_exercised(self) -> None:
        lines = [
            "[1.000] [INFO] Transport started",
            _gap(2_100_000, opportunities=4, running=1, eligible=1, progress=1),
            _receipt(2_200_000),
            _frozen_receipt(2_250_000),
            _build(2_300_000),
            _build(2_400_000),
            "[9.000] [INFO] Transport stopped",
        ]
        result = verify_windowed_lane(lines)
        demand = next(check for check in result["checks"] if check["name"] == "window_demand_observed")
        self.assertTrue(demand["ok"])
        self.assertTrue(result["ok"], result["issues"])


    def test_the_gate_row_is_read_by_the_emitters_field_order(self) -> None:
        """The row is parsed where `winGateRow` writes it, and a row the emitter never writes is skipped."""
        rows = gate_rows([_gate(2_100_000, exit="win_not_servable", rq=3, sv=0, rt=1)])
        self.assertEqual(1, len(rows))
        row = rows[0]
        self.assertEqual("win_not_servable", row["exit"])
        self.assertEqual("8", row["track"])
        self.assertEqual("4", row["slot"])
        self.assertEqual(str(LONG_LOOP), row["loop_len"])
        self.assertEqual("1", row["flags"]["av"])
        self.assertEqual("0", row["flags"]["sv"])
        self.assertEqual("3", row["request_result"])
        self.assertEqual(2_100_000, row["us"])
        # Truncated at the request field: the emitter never writes one, and reading it would be guessing.
        self.assertEqual([], gate_rows(["#CAP,1,DIAG,win_gate,t=8,s=4,x=win_ready"]))

    def test_an_ineligible_loop_names_the_term_that_refused(self) -> None:
        """An unaligned demoted loop: the check says *which* of the entry trio refused, not merely 'no demand'."""
        lines = _no_demand_lines(
            _gate(2_110_000, exit="win_window_ineligible", loop_len=25 * TICKS_PER_BAR, al=0),
        )
        result = verify_windowed_lane(lines)
        self.assertFalse(result["ok"])
        demand = next(check for check in result["checks"] if check["name"] == "window_demand_observed")
        self.assertFalse(demand["ok"])
        self.assertIn("win_window_ineligible(1)", demand["detail"])
        self.assertIn("not a whole number of two-bar spans", demand["detail"])
        self.assertEqual("win_window_ineligible", result["demand"]["dominant_exit"])
        self.assertEqual(1, result["demand"]["gate_rows"])

    def test_a_not_servable_slot_names_the_missing_published_window(self) -> None:
        """The refusal that remains after 3.7c: nothing to build a window from (or nowhere to publish it)."""
        lines = _no_demand_lines(*[_gate(2_110_000 + index, exit="win_not_servable", sv=0, rt=0)
                                   for index in range(3)])
        result = verify_windowed_lane(lines)
        demand = next(check for check in result["checks"] if check["name"] == "window_demand_observed")
        self.assertFalse(demand["ok"])
        self.assertIn("no current source to build a window from", demand["detail"])
        self.assertEqual({"win_not_servable": 3}, result["demand"]["exits"])

    def test_the_most_frequent_exit_is_the_one_reported(self) -> None:
        """One odd refusal among many is not reported as the reason: the dominant exit is."""
        lines = _no_demand_lines(
            _gate(2_110_000, exit="win_not_servable", sv=0, rt=0),
            _gate(2_120_000, exit="win_blocked", sv=0, rt=0),
            _gate(2_130_000, exit="win_blocked", sv=0, rt=0),
        )
        result = verify_windowed_lane(lines)
        self.assertEqual("win_blocked", result["demand"]["dominant_exit"])

    def test_a_request_exit_is_a_demand_even_with_no_receipt_or_eligible_turn(self) -> None:
        """`win_requested` can only be reached with a request in existence, so it evidences the demand itself."""
        lines = [
            "[1.000] [INFO] Transport started",
            _gate(2_100_000, exit="win_requested", sv=1, rt=1, rq=2),
            _gap(2_200_000, opportunities=9, running=1, eligible=0, progress=0, denied=0),
            "[9.000] [INFO] Transport stopped",
        ]
        summary = demand_entry_summary(lines)
        self.assertTrue(summary["observed"])
        result = verify_windowed_lane(lines)
        demand = next(check for check in result["checks"] if check["name"] == "window_demand_observed")
        self.assertTrue(demand["ok"])
        self.assertIn("win_requested(1)", demand["detail"])
        self.assertTrue(result["demand"]["observed"])

    def test_a_capture_without_the_gate_family_says_the_image_lacks_it(self) -> None:
        """The gate row is qualification-only: its absence is stated, because the entry term is then unreadable."""
        lines = [
            "[1.000] [INFO] Transport started",
            _gap(2_100_000, opportunities=11776, running=0, eligible=0, progress=0, denied=0),
            "[9.000] [INFO] Transport stopped",
        ]
        result = verify_windowed_lane(lines)
        demand = next(check for check in result["checks"] if check["name"] == "window_demand_observed")
        self.assertFalse(demand["ok"])
        self.assertIn("does not compile the lane's gate row", demand["detail"])
        self.assertEqual(0, result["demand"]["gate_rows"])
        self.assertIsNone(result["demand"]["refusal"])
    def test_one_publication_is_not_a_boundary_crossing(self) -> None:
        result = verify_windowed_lane(_lane_lines(completions=1))
        self.assertFalse(result["ok"])
        self.assertTrue(
            any(issue.startswith("window_boundaries_crossed_under_transport") for issue in result["issues"])
        )

    def test_switch_target_alignment_failure_names_length_and_entry_flags(self) -> None:
        lines = ["[1.000] [INFO] Transport started", _gate(2_000_000, exit="win_window_ineligible", track=7, slot=4, loop_len=19200, ep=1, av=1, al=0, pl=1), _build(3_000_000, loop_len=19200), "[9.000] [INFO] Transport stopped"]
        result = verify_windowed_lane(lines, expected_track_number=8, switch_target_slot=5)
        check = next(item for item in result["checks"] if item["name"] == "switch_target_span_alignment")
        self.assertFalse(check["ok"])
        self.assertIn("len=19200", check["detail"])
        self.assertIn("al=0", check["detail"])
    def test_switch_target_alignment_passes_for_a_two_bar_span_multiple(self) -> None:
        lines = ["[1.000] [INFO] Transport started", _gate(2_000_000, exit="win_not_servable", track=7, slot=4, loop_len=18432, ep=1, av=1, al=1, pl=1), "[9.000] [INFO] Transport stopped"]
        result = verify_windowed_lane(lines, expected_track_number=8, switch_target_slot=5)
        check = next(item for item in result["checks"] if item["name"] == "switch_target_span_alignment")
        self.assertTrue(check["ok"], check["detail"])
    def test_snapshot_progress_requires_live_and_post_stop_owner_work(self) -> None:
        result = verify_record_snapshot_progress(_snapshot_progress_lines(), track_number=8, loop_slot=4)
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(["snapshot_progress_during_recording", "snapshot_owner_continues_after_stop", "record_session_exit_root_published"], [item["name"] for item in result["checks"]])
        missing = verify_record_snapshot_progress(["[2.000] [DEBUG] Recording stopped @ tick 18432"], track_number=8, loop_slot=4)
        self.assertFalse(missing["ok"])
        self.assertIn("no DIAG,snap_gate rows", missing["issues"][0])
    def test_snapshot_publication_requires_target_post_step_receipt(self) -> None:
        progress = _snapshot_progress_lines()[:-1]
        anonymous = "#CAP,4000000,DIAG,snapshot_unit,kind=publish,q=0,a=4"
        foreign = _snapshot_gate(4_100_000, rec=0, sep=0, p=0, fc=0,
                                 elig=1, st=21, cu=0, rv=2, track=6, slot=2)
        for lines in (progress + [anonymous], progress + [anonymous, foreign]):
            result = verify_record_snapshot_progress(lines, track_number=8, loop_slot=4)
            self.assertFalse(result["ok"], result)
            self.assertFalse(result["checks"][2]["ok"])
        receipt = _snapshot_gate(4_200_000, rec=0, sep=0, p=0, fc=0,
                                 elig=0, st=20, cu=0, rv=2)
        self.assertEqual((7, 3, 2), tuple(snapshot_gate_rows([receipt])[0][key]
                                            for key in ("t", "s", "rv")))
        confirmed = verify_record_snapshot_progress(progress + [anonymous, foreign, receipt],
                                                    track_number=8, loop_slot=4)
        self.assertTrue(confirmed["ok"], confirmed)

    def test_a_confirmation_before_the_work_was_owed_is_not_a_confirmation(self) -> None:
        """The confirming row must come **after** the last row that still owed the session exit.

        A settled row that precedes a further owed turn is not a post-step confirmation of the publication: it is an
        earlier publication, and reading it as the confirmation of *this* one would let a run whose final owner turn
        published with no row after it still pass.
        """
        lines = _snapshot_progress_lines() + [
            _snapshot_gate(5_000_000, rec=0, sep=1, p=1, fc=1, elig=1, st=24, cu=64, rv=2),
        ]
        result = verify_record_snapshot_progress(lines, track_number=8, loop_slot=4)
        self.assertFalse(result["ok"], result)
        published = next(item for item in result["checks"] if item["name"] == "record_session_exit_root_published")
        self.assertFalse(published["ok"])
        self.assertIn("post_step_confirmed=False", published["detail"])
        # The same run with the confirmation after the owed turn is a pass.
        confirmed = verify_record_snapshot_progress(
            lines + [_snapshot_gate(5_100_000, rec=0, sep=0, p=0, fc=0, elig=0, st=24, cu=0, rv=2)],
            track_number=8, loop_slot=4)
        self.assertTrue(confirmed["ok"], confirmed)
    def test_the_profile_verifier_requires_both_halves(self) -> None:
        """The reused leg's oracle and the lane's checks must both pass; either failing is not a pass."""
        args = _leg_args()
        healthy = _leg_passing_lines() + _snapshot_progress_lines() + _lane_lines()
        both = verify_windowed_content_parity(healthy, args)
        self.assertTrue(both["ok"], both["issues"])
        self.assertFalse(both["oracle_complete"])

        leg_only = verify_windowed_content_parity(_leg_passing_lines(), args)
        self.assertFalse(leg_only["ok"], leg_only["issues"])
        self.assertTrue(all(issue.startswith(("lane: ", "snapshot: ")) for issue in leg_only["issues"]), leg_only["issues"])

        lane_only = verify_windowed_content_parity(_lane_lines(), args)
        self.assertFalse(lane_only["ok"], lane_only["issues"])
        self.assertTrue(any(issue.startswith("leg: ") for issue in lane_only["issues"]), lane_only["issues"])

    def test_the_drained_capture_decides_the_leg_not_the_pre_drain_record(self) -> None:
        """The ring is retained under transport, so a leg report recorded before the drain cannot be the verdict.

        The combined read is formed from the evidence handed in -- the capture **after** the post-stop drain -- for
        every component. A report the runner did record before the drain travels as `leg_run_report` provenance.
        Both directions are pinned: a pre-drain failure is not carried into a drained pass, and a pre-drain pass
        does not carry a drained run that lacks the rows.
        """
        args = _leg_args()
        args.track_number = 8
        args.loop_slot = 4

        # A pre-drain report that failed for want of the retained rows must not fail a drained run that has them.
        args.windowed_leg_run_report = {
            "ok": False,
            "issues": ["missing_bounded_window_disp_rows"],
            "phase_scope": "pre-drain-leg",
        }
        drained = verify_windowed_content_parity(
            _leg_passing_lines() + _snapshot_progress_lines() + _lane_lines(), args)
        self.assertTrue(drained["ok"], drained["issues"])
        self.assertTrue(drained["leg"]["ok"], drained["leg"])
        self.assertEqual("pre-drain-leg", drained["leg_run_report"]["phase_scope"])

        # ... and a pre-drain report that passed must not carry a drained run with no bounded display rows.
        args.windowed_leg_run_report = {"ok": True, "issues": [], "phase_scope": "pre-drain-leg"}
        empty = verify_windowed_content_parity(_snapshot_progress_lines() + _lane_lines(), args)
        self.assertFalse(empty["ok"], empty["issues"])
        self.assertFalse(empty["leg"]["ok"], empty["leg"])
        self.assertTrue(any(issue.startswith("leg: ") for issue in empty["issues"]), empty["issues"])

    def test_no_runner_report_leaves_no_provenance(self) -> None:
        """`leg_run_report` names what the leg recorded; a run with nothing recorded does not carry an empty one."""
        args = _leg_args()
        args.track_number = 8
        args.loop_slot = 4
        report = verify_windowed_content_parity(
            _leg_passing_lines() + _snapshot_progress_lines() + _lane_lines(), args)
        self.assertNotIn("leg_run_report", report)

class _StubPort:
    """A MIDI out port that records what it was asked to send (the `test_record_target_precondition` pattern)."""

    def __init__(self) -> None:
        self.sent: list[tuple[int, int]] = []

    def send(self, message: object) -> None:
        self.sent.append((int(message.note), int(message.channel)))  # type: ignore[attr-defined]
    def close(self) -> None:
        pass


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

    def test_the_settle_flag_is_taken_out_of_the_legacy_args(self) -> None:
        """The wrapper's own float flag, removed before the reused leg's strict parser sees it."""
        settle, remaining = split_switch_settle_flag(
            ["--record-bars", "24", "--switch-settle-s", "60", "--switch-target-slot", "3"]
        )
        self.assertEqual(60.0, settle)
        self.assertEqual(["--record-bars", "24", "--switch-target-slot", "3"], remaining)
        self.assertEqual((None, ["--record-bars", "24"]), split_switch_settle_flag(["--record-bars", "24"]))

    def test_an_unparsable_settle_is_refused_with_its_token(self) -> None:
        with self.assertRaises(ValueError) as caught:
            split_switch_settle_flag(["--switch-settle-s", "soon"])
        self.assertIn("'soon'", str(caught.exception))

    def test_the_default_settle_is_the_measured_one(self) -> None:
        """The longer 90 s opportunity is explicit; the verifier still requires actual repeated publications."""
        from hitl.scenarios.windowed_content_parity import SWITCH_SETTLE_S

        self.assertEqual(90.0, SWITCH_SETTLE_S)

    def test_a_settle_without_a_switch_target_is_refused_before_any_device_contact(self) -> None:
        """A wait that belongs to the switch leg cannot be named without a target: it would measure nothing."""
        class _Args:
            legacy_args = ["--loop-slot", "4", "--switch-settle-s", "45"]

        self.assertEqual(2, run_windowed_content_parity(_Args()))

    def test_a_non_positive_settle_is_refused_before_any_device_contact(self) -> None:
        class _Args:
            legacy_args = ["--loop-slot", "4", "--switch-target-slot", "3", "--switch-settle-s", "0"]

        self.assertEqual(2, run_windowed_content_parity(_Args()))

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

    def test_the_drain_reads_the_captures_log_when_the_capture_owns_the_port(self) -> None:
        """The managed capture owns the USB port, so the post-leg drain has to read **its** session log.

        The leg's own log is a copy it stopped writing at exit, so draining that file would read a file nobody is
        appending to -- which is how the first windowed run ended (`no serial port on this run: the retained rows cannot
        be drained`, exit 2) with the lane's own rows still unread.
        """
        import tempfile
        from unittest import mock

        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            session = out_dir / "session_20260101_000000.log"
            session.write_text(
                "[1.000] [INFO] Transport started\n[2.000] [INFO] Transport stopped\n"
                "[3.000] [INFO] Transport started\n",
                encoding="utf-8",
            )
            (out_dir / ".current_session").write_text(str(session), encoding="utf-8")

            class _Args:
                serial_port = None
                legacy_args: list[str] = []

            args = _Args()
            args.out_dir = out_dir

            class _Port:
                def send(self, message: object) -> None:
                    pass

                def close(self) -> None:
                    pass

            def _capture_keeps_writing(port: object, **kwargs: object) -> None:
                # The retained row is what the capture appends **after** the stop press: that is the drain.
                session.write_text(
                    session.read_text(encoding="utf-8")
                    + "#CAP,9000,DIAG,win_readiness,7,3,ready,0,4,1536\n",
                    encoding="utf-8",
                )

            with mock.patch("mido.open_output", return_value=_Port()), mock.patch(
                "hitl.midi_io._find_midi_port", return_value="Teensy"
            ), mock.patch(
                "hitl.scenarios.windowed_content_parity.press_global_transport_stop",
                side_effect=_capture_keeps_writing,
            ):
                drain = run_post_leg_phase(args, drain_s=0.0)

            self.assertIsNotNone(drain)
            rows = drain.read_text(encoding="utf-8").splitlines()
            self.assertEqual(1, len(rows))
            self.assertIn("win_readiness", rows[0])

    def test_the_wrappers_own_flags_never_reach_the_reused_leg(self) -> None:
        """The leg's parser is strict: a wrapper-owned flag forwarded to it aborts the run before it drives anything."""
        slot, remaining = split_loop_slot_flag(["--track-number", "8", "--loop-slot", "6"])
        self.assertEqual(6, slot)
        self.assertNotIn("--loop-slot", remaining)
        target, remaining = split_switch_target_slot_flag(remaining + ["--switch-target-slot", "4", "--midi-channel", "8"])
        self.assertEqual(4, target)
        self.assertNotIn("--switch-target-slot", remaining)
        self.assertEqual(["--track-number", "8", "--midi-channel", "8"], remaining)

    def test_the_switch_leg_selects_then_leaves_the_edit_session_then_presses_record(self) -> None:
        """Three gestures in one order: the selection is the safety argument, and the edit exit is what the switch's
        preparation needs (run 04: with the session open the preparation never starts)."""
        port = _StubPort()
        switch_leg(port, 3, settle_s=0.0)
        self.assertEqual(
            [(LOOP_SELECT_NOTE_BASE + 2, CONTROL_CHANNEL_1BASED - 1)] * 2
            + [(EDIT_BUTTON_NOTE, CONTROL_CHANNEL_1BASED - 1)] * 2
            + [(RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED - 1)] * 2,
            port.sent,
        )
        with self.assertRaises(ValueError):
            switch_leg(port, 9, settle_s=0.0)

    def test_the_edit_exit_holds_the_edit_control_past_the_firmware_long_press_window(self) -> None:
        """The firmware's own threshold is 350 ms; a short press would cycle the session instead of leaving it."""
        from hitl.control_constants import EDIT_BUTTON_DEBOUNCE_MS
        from hitl.scenarios.windowed_content_parity import EDIT_EXIT_PRESS_MS

        self.assertGreater(EDIT_EXIT_PRESS_MS, EDIT_BUTTON_DEBOUNCE_MS)
        port = _StubPort()
        press_edit_exit(port, press_ms=1)
        self.assertEqual([(EDIT_BUTTON_NOTE, CONTROL_CHANNEL_1BASED - 1)] * 2, port.sent)

    def test_the_slot_select_presses_that_slots_own_control(self) -> None:
        port = _StubPort()
        select_loop_slot(port, 6, press_ms=1)
        self.assertEqual(
            [(LOOP_SELECT_NOTE_BASE + 5, CONTROL_CHANNEL_1BASED - 1)] * 2,
            port.sent,
        )


class ManagedRunnerHandshakeTests(unittest.TestCase):
    """F-17: the runner's managed-capture handshake must not lose the capture, and must not abort the reused leg.

    The real failing argv came from a managed run (`captures/s64-win-gate-01`): the runner appends
    `--out-dir <dir>` (`runner.py`) and the managed-capture hand-off appends `--follow-serial-log <session log>`
    (`hitl/managed_capture.py`), and the reused leg's **strict** parser answered `unrecognized arguments: …`
    before a single gesture was sent. Both halves are pinned here: the wrapper consumes **only** its own flags,
    and the leg then *accepts* the runner's -- it reads the log instead of opening the port, because in a
    managed run the capture owns the port. Nothing is stripped: a managed run that silently lost
    `--follow-serial-log` would have no capture at all.
    """

    #: The argv a managed windowed run hands down: runner flags, the hand-off's log pointer, and the wrapper's
    #: two own flags (`--loop-slot` is the wrapper's because the reused leg takes no slot argument).
    MANAGED_ARGV = [
        "--track", "8", "--loop-slot", "5", "--midi-channel", "8",
        "--follow-serial-log", "/tmp/session_20261005.log", "--out-dir", "captures/managed-run",
        "--switch-target-slot", "6",
    ]

    def _wrapper_to_leg(self, legacy: list[str]) -> tuple[int | None, int | None, list[str]]:
        slot, remaining = split_loop_slot_flag(legacy)
        target, remaining = split_switch_target_slot_flag(remaining)
        return slot, target, remaining

    def test_the_wrapper_consumes_only_its_own_flags(self) -> None:
        slot, target, remaining = self._wrapper_to_leg(list(self.MANAGED_ARGV))
        self.assertEqual(5, slot)
        self.assertEqual(6, target)
        self.assertNotIn("--loop-slot", remaining)
        self.assertNotIn("--switch-target-slot", remaining)
        for kept in ("--follow-serial-log", "/tmp/session_20261005.log", "--out-dir", "captures/managed-run"):
            self.assertIn(kept, remaining)

    def test_the_leg_accepts_what_the_wrapper_hands_it(self) -> None:
        from hitl.scenarios.long_loop_display_window import _parse_common_args

        _slot, _target, remaining = self._wrapper_to_leg(list(self.MANAGED_ARGV))
        ns = _parse_common_args(SimpleNamespace(legacy_args=remaining))
        self.assertEqual(8, ns.track_number)
        self.assertEqual("/tmp/session_20261005.log", ns.follow_serial_log)
        self.assertEqual("captures/managed-run", ns.out_dir)

    def test_the_followed_log_is_read_through_the_collector_interface(self) -> None:
        """The leg and its helpers only ever ask a collector for `snapshot()`; the adapter must satisfy exactly that."""
        import tempfile

        from hitl.scenarios.long_loop_display_window import FollowedSerialLog

        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "session.log"
            log.write_text("boot\nEdit session: LOOP_EDIT\nplayback_start\n", encoding="utf-8")
            adapter = FollowedSerialLog(log)
            self.assertEqual(["boot", "Edit session: LOOP_EDIT", "playback_start"], adapter.snapshot())
            adapter.stop()  # no-op: this view does not own the port
            self.assertEqual([], FollowedSerialLog(Path(tmp) / "missing.log").snapshot())

    def test_a_canonical_follow_current_session_resolves_to_the_pointer(self) -> None:
        """`--follow-current-session` is the base drivers' canonical form: resolve it through the same pointer."""
        import tempfile

        from hitl.scenarios.long_loop_display_window import _parse_common_args, followed_serial_log_path

        with tempfile.TemporaryDirectory() as tmp:
            session = Path(tmp) / "session_20261005.log"
            session.write_text("boot\n", encoding="utf-8")
            (Path(tmp) / ".current_session").write_text(str(session), encoding="utf-8")
            ns = _parse_common_args(SimpleNamespace(legacy_args=["--follow-current-session", "--out-dir", tmp]))
            self.assertEqual(session, followed_serial_log_path(ns))
            # Without the runner naming anything, there is nothing to follow and the leg keeps its own behaviour.
            self.assertIsNone(followed_serial_log_path(_parse_common_args(SimpleNamespace(legacy_args=[]))))


class CombinedVerifierArtifactTests(unittest.TestCase):
    def test_run_writes_snapshot_lane_and_leg_verdict_together(self) -> None:
        import json
        import tempfile
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            session = out_dir / "session.log"
            session.write_text("\n".join(_leg_passing_lines() + _snapshot_progress_lines() + _lane_lines()) + "\n", encoding="utf-8")
            (out_dir / ".current_session").write_text(str(session), encoding="utf-8")
            standalone_leg = out_dir / "host_midi_hitl_long_loop_display_window_20200101_000000.json"
            standalone_leg.write_text(json.dumps({"serial_verification": {"ok": True, "issues": [], "phase_scope": "recorded-leg"}}), encoding="utf-8")
            args = _leg_args()
            args.track_number = 8
            args.loop_slot = 4
            args.legacy_args = ["--loop-slot", "4", "--track-number", "8", "--midi-channel", "8"]
            args.out_dir = out_dir
            with mock.patch("mido.open_output", return_value=_StubPort()):
                with mock.patch("hitl.midi_io._find_midi_port", return_value="stub"):
                    with mock.patch("hitl.scenarios.windowed_content_parity.select_loop_slot"):
                        with mock.patch("hitl.scenarios.windowed_content_parity.run_long_loop_display_window", return_value=2):
                            with mock.patch("hitl.scenarios.windowed_content_parity.run_post_leg_phase", return_value=None):
                                result = run_windowed_content_parity(args)
            self.assertEqual(2, result)
            reports = list(out_dir.glob("host_midi_hitl_windowed_content_parity_*.json"))
            self.assertEqual(1, len(reports))
            report = json.loads(reports[0].read_text(encoding="utf-8"))
            # The drained capture decides the leg; the report the leg recorded before the drain is provenance.
            self.assertTrue(report["leg"]["ok"], report["leg"])
            self.assertEqual("recorded-leg", report["leg_run_report"]["phase_scope"])
            self.assertEqual(str(standalone_leg), report["run"]["standalone_leg_report"])
            self.assertTrue(report["snapshot"]["ok"], report["snapshot"])
            self.assertTrue(report["lane"]["ok"], report["lane"])
            self.assertFalse(report["ok"])
            self.assertTrue(any(issue.startswith("execution: reused long-loop leg exited 2") for issue in report["issues"]))
            self.assertEqual(2, report["run"]["leg_exit_code"])

if __name__ == "__main__":
    unittest.main()
