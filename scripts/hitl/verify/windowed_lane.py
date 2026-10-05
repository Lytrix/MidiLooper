"""The windowed lane's device rows, read for what task 6.4's profile declares.

`STIM-WINDOW-CONTENT-01` declares four outcomes (`must_prove`). This module judges the ones the lane's **own** rows can
answer, and *names* -- never restates -- the two whose verdict belongs to another owner:

| Declared term | Judged here? | Owner of the verdict |
|---|---|---|
| `readiness_receipts_under_transport` | **yes** | this module, from `#CAP,<us>,DIAG,win_readiness,…` inside a running-transport interval (AC-8: the receipt must arrive where it is produced, i.e. before the transport stops) |
| `windowed_service_coverage_under_transport` | **yes** | this module, from `#CAP,<us>,DIAG,win_gap,…` blocks with `running=1`, plus the **named** `win_gap_overrun` row, which fails the check when it is raised under transport |
| `content_parity_across_window_boundary` | **partly** | the boundary crossings are judged here (`build_complete` publications under transport, so a verdict can be formed); the parity **verdict** is the existing content pipeline's |
| `root_demoted_residency_over_loop_lengths` | **no** | deferred: the row set that states per-slot residency is not identified, so it is recorded as unknown rather than guessed |

A run is therefore never reported as F-7/F-8 qualification by this module: `oracle_complete` is always `False` and the
deferred terms travel with the result, each with the surface to ask.
"""

from __future__ import annotations

from typing import Any, Iterable

#: The lane's own rows, exactly as `DebugSessionCapture` emits them (verified against the emitters, not from memory):
#: `#CAP,<us>,DIAG,win_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>`
READINESS_ROW = "DIAG,win_readiness,"
#: `#CAP,<us>,DIAG,win_gap,<maxDelta>,<opportunities>,<phase>,<running>,<discontinuities>,<eligibleMax>,<eligibleTurns>,`
#: `<progressTurns>,<deniedTurns>,<maxWorkerUs>,<epoch>,<saturatedGaps>,<permitted>,<overruns>,<overrunMax>,<schema>`
#: (fields 4..19; the trailing `3` is the schema number the emitter writes)
GAP_ROW = "DIAG,win_gap,"
#: `#CAP,<us>,DIAG,win_gap_overrun,<permitted>,<overruns>,<overrunMax>,<running>,<epoch>` — the **named** overrun.
GAP_OVERRUN_ROW = "DIAG,win_gap_overrun,"
#: `#CAP,<us>,DIAG,playback_build,<path>,…` — the lane's path row (`build_complete`, `grain_denied`, …).
BUILD_ROW = "DIAG,playback_build,"
BUILD_COMPLETE = "build_complete"

#: The transport's own human rows (the stream's own ordering is the clock here; no unit conversion is attempted).
TRANSPORT_STARTED = "Transport started"
TRANSPORT_STOPPED = "Transport stopped"

#: The terms this module does **not** judge, with the surface that owns each verdict.
DEFERRED_TERMS: tuple[dict[str, str], ...] = (
    {
        "term": "content_parity_across_window_boundary",
        "judged": "the boundary crossings (successive `build_complete` publications under transport)",
        "verdict_owner": "the existing content pipeline (`hitl/content_deduction.py`, `scripts/hitl/mixed_content_report.py`)",
    },
    {
        "term": "root_demoted_residency_over_loop_lengths",
        "judged": "nothing",
        "verdict_owner": "unidentified row set: the library-observation path (`!LIB_OBSERVE`) is the candidate, and this is recorded as unknown rather than guessed",
    },
)


def _split(line: str) -> list[str]:
    return line.strip().split(",")


def _is_lane_row(line: str, token: str) -> bool:
    return "DIAG," in line and token in line


def running_intervals(lines: list[str]) -> list[tuple[int, int]]:
    """Index ranges `[start, stop)` in which the transport was running.

    The capture is ordered, so the stream itself is the clock: a row is "under transport" when it sits between a
    `Transport started` row and the following `Transport stopped` row. No device-time conversion is done here on
    purpose -- the two row families do not share a unit, and the ordering is what the claim is about.
    """
    intervals: list[tuple[int, int]] = []
    start: int | None = None
    for index, line in enumerate(lines):
        if TRANSPORT_STARTED in line:
            if start is None:
                start = index
        elif TRANSPORT_STOPPED in line and start is not None:
            intervals.append((start, index))
            start = None
    if start is not None:
        intervals.append((start, len(lines)))
    return intervals


def _under_transport(index: int, intervals: Iterable[tuple[int, int]]) -> bool:
    return any(start <= index < stop for start, stop in intervals)


def readiness_rows(lines: list[str]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if not _is_lane_row(line, READINESS_ROW):
            continue
        fields = _split(line)
        if len(fields) < 10:
            continue
        rows.append(
            {
                "index": index,
                "track": fields[4],
                "slot": fields[5],
                "readiness": fields[6],
                "holder": fields[7],
                "revision": fields[8],
                "lead_ticks": fields[9],
            }
        )
    return rows


def gap_rows(lines: list[str]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if not _is_lane_row(line, GAP_ROW) or _is_lane_row(line, GAP_OVERRUN_ROW):
            continue
        fields = _split(line)
        if len(fields) < 20:
            continue
        rows.append(
            {
                "index": index,
                "opportunities": fields[5],
                "phase": fields[6],
                "running": fields[7],
                "eligible_turns": fields[10],
                "progress_turns": fields[11],
                # Fields 16/17 are the declaration's own `permittedAdvanceTicks` and that block's `overruns`;
                # 19 is the literal schema number the emitter writes (3), which is what makes the row self-describing.
                "permitted": fields[16],
                "overruns": fields[17],
                "schema": fields[19],
            }
        )
    return rows


def overrun_rows(lines: list[str]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if not _is_lane_row(line, GAP_OVERRUN_ROW):
            continue
        fields = _split(line)
        if len(fields) < 9:
            continue
        rows.append(
            {
                "index": index,
                "permitted": fields[4],
                "overruns": fields[5],
                "overrun_max": fields[6],
                "running": fields[7],
                "epoch": fields[8],
            }
        )
    return rows


def build_rows(lines: list[str], *, path: str | None = None) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if not _is_lane_row(line, BUILD_ROW):
            continue
        fields = _split(line)
        if len(fields) < 11:
            continue
        if path is not None and fields[4] != path:
            continue
        rows.append(
            {
                "index": index,
                "path": fields[4],
                "loop_len": fields[6],
                "revision": fields[8],
                "track": fields[9],
                "slot": fields[10],
            }
        )
    return rows


def _check(name: str, ok: bool, detail: str) -> dict[str, Any]:
    return {"name": name, "ok": ok, "detail": detail}


def window_demand_observed(lines: list[str]) -> bool:
    """Was any window *work owed* in this capture?

    The lane dispatches on the deferred **idle** owner and its rows are event-shaped: a dispatch with no eligible
    holder reports zero eligible turns and emits no receipt, and no completion can publish. So the leg's own
    prerequisite is a **demand** -- a slot owing a window build -- not merely a running transport. Task 6.4's first
    authorized device run established this on hardware: 44 `win_gap` blocks, every one with `eligible=progress=denied=0`
    and `running=0`, and **no** `win_readiness` receipt at all, on an image where the rolling lane is compiled off and
    no launch was owed. Reporting that as "three lane checks failed" would blame the lane for not being asked.
    """
    for row in gap_rows(lines):
        if row["eligible_turns"] != "0" or row["progress_turns"] != "0":
            return True
    return bool(readiness_rows(lines))


def verify_windowed_lane(lines: list[str]) -> dict[str, Any]:
    """Judge the declared oracle's judgeable half, and name the rest."""
    intervals = running_intervals(lines)
    checks: list[dict[str, Any]] = []
    demand = window_demand_observed(lines)
    checks.append(
        _check(
            "window_demand_observed",
            demand,
            "the lane was asked for window work in this capture"
            if demand
            else "no eligible/progress turn and no readiness receipt in the whole capture: no slot owed a window "
                 "build, so this run did not exercise the lane (a demand needs the rolling lane enabled, or a "
                 "launch/retry in flight -- a plain record-and-play owes none)",
        )
    )

    # **AC-8: the receipt must arrive while the transport runs.** A receipt that only reaches the stream after the
    # stop is exactly what the direct-when-USB-has-room path exists to avoid, so it is a failure and not a pass.
    receipts = readiness_rows(lines)
    receipts_under = [row for row in receipts if _under_transport(row["index"], intervals)]
    checks.append(
        _check(
            "readiness_receipts_under_transport",
            bool(receipts_under),
            f"receipts={len(receipts)} under_transport={len(receipts_under)}"
            + (f" first={receipts_under[0]['readiness']}/{receipts_under[0]['holder']}" if receipts_under else ""),
        )
    )

    # **Coverage blocks the lane actually served under transport**, in the schemas that carry the declaration: a block
    # with no opportunity is not coverage, and a pre-schema-3 row does not carry `running`/`permitted` to be read.
    gaps = gap_rows(lines)
    gaps_under = [row for row in gaps if row["running"] == "1" and _under_transport(row["index"], intervals)]
    served = [row for row in gaps_under if int(row["opportunities"]) > 0]
    checks.append(
        _check(
            "windowed_service_coverage_under_transport",
            bool(served),
            f"blocks={len(gaps)} under_transport={len(gaps_under)} with_opportunities={len(served)}"
            + (f" permitted={served[0]['permitted']}" if served else ""),
        )
    )

    # The **named** overrun row: raised under transport it is a real exceedance of the permitted advance, so a leg that
    # shows one is reported as failed here rather than smoothed into the coverage count.
    overruns = overrun_rows(lines)
    overruns_under = [row for row in overruns if row["running"] == "1" and _under_transport(row["index"], intervals)]
    checks.append(
        _check(
            "no_windowed_overrun_under_transport",
            not overruns_under,
            f"overrun_rows={len(overruns)} under_transport={len(overruns_under)}"
            + (f" worst={overruns_under[0]['overrun_max']}" if overruns_under else ""),
        )
    )

    # Successive publications under transport: the precondition for a parity verdict at a boundary (the verdict itself
    # stays the content pipeline's -- see `DEFERRED_TERMS`).
    completions = [
        row for row in build_rows(lines, path=BUILD_COMPLETE) if _under_transport(row["index"], intervals)
    ]
    checks.append(
        _check(
            "window_boundaries_crossed_under_transport",
            len(completions) >= 2,
            f"build_complete_under_transport={len(completions)}"
            + (f" loop_len={completions[0]['loop_len']} revisions={sorted({r['revision'] for r in completions})}"
               if completions else ""),
        )
    )

    issues = [f"{check['name']}: {check['detail']}" for check in checks if not check["ok"]]
    return {
        "ok": not issues,
        "checks": checks,
        "issues": issues,
        "deferred": [dict(entry) for entry in DEFERRED_TERMS],
        # A green lane is **not** F-7/F-8 qualification: two declared terms are not judged here at all.
        "oracle_complete": False,
    }


def verify_windowed_content_parity(lines: list[str], args: object) -> dict[str, Any]:
    """The windowed profile's verifier: the reused long-loop leg's own oracle **plus** the windowed lane's rows.

    The leg is the existing content-verification pipeline's (task 6.4 feeds it, it does not replace it), so both halves
    must pass; a run where the windowed rows are healthy but the leg's own oracle failed is not a pass, and neither is
    one where the leg was fine but the lane's receipts never arrived under transport.
    """
    from hitl.verify.display_window import verify_long_loop_display_window

    leg = verify_long_loop_display_window(lines, args)
    lane = verify_windowed_lane(lines)
    issues: list[str] = []
    if not leg.get("ok"):
        issues.extend(f"leg: {issue}" for issue in (leg.get("issues") or ["failed"]))
    issues.extend(f"lane: {issue}" for issue in lane["issues"])
    return {
        "ok": not issues,
        "issues": issues,
        "leg": leg,
        "lane": lane,
        "deferred": lane["deferred"],
        "oracle_complete": False,
    }
