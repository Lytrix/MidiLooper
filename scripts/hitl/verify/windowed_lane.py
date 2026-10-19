"""The windowed lane's device rows, read for what task 6.4's profile declares.

`STIM-WINDOW-CONTENT-01` declares four outcomes (`must_prove`). This module judges the ones the lane's **own** rows can
answer, and *names* -- never restates -- the two whose verdict belongs to another owner:

| Declared term | Judged here? | Owner of the verdict |
|---|---|---|
| `readiness_receipts_under_transport` | **yes** | this module, from `#CAP,<us>,DIAG,win_frozen_readiness,…` inside a running-transport interval (AC-8: the receipt must arrive where it is produced, i.e. before the transport stops). It must be **source-qualified**: the row names the window that became ready, so a generic `win_readiness` row saying `not_servable` does not answer the term (see `frozen_readiness_rows`) |
| `windowed_service_coverage_under_transport` | **yes** | this module, from `#CAP,<us>,DIAG,win_gap,…` blocks with `running=1`, plus the **named** `win_gap_overrun` row, which fails the check when it is raised under transport |
| `content_parity_across_window_boundary` | **partly** | the boundary crossings are judged here (`build_complete` publications under transport, so a verdict can be formed); the parity **verdict** is the existing content pipeline's |
| `root_demoted_residency_over_loop_lengths` | **no** | deferred: the row set that states per-slot residency is not identified, so it is recorded as unknown rather than guessed |

A run is therefore never reported as F-7/F-8 qualification by this module: `oracle_complete` is always `False` and the
deferred terms travel with the result, each with the surface to ask.
"""

from __future__ import annotations

import re

from typing import Any, Iterable

#: The lane's own rows, exactly as `DebugSessionCapture` emits them (verified against the emitters, not from memory):
#: `#CAP,<us>,DIAG,win_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>`
READINESS_ROW = "DIAG,win_readiness,"
#: `#CAP,<us>,DIAG,win_frozen_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>,` +
#: `<sourceGeneration>,<captureToken>,<start>,<length>,<loopLength>` — the frozen Record-return **completion**'s own
#: receipt, emitted by `DebugSessionCapture::frozenReadinessReceiptImmediate` on the frozen owner's completion
#: transition only. Where `win_readiness` names the slot's *committed* readiness (for a first take: `not_servable`),
#: this row names the window that actually became ready, so a reader can tell a completion from "nothing to serve".
FROZEN_READINESS_ROW = "DIAG,win_frozen_readiness,"
#: `#CAP,<us>,DIAG,win_gap,<maxDelta>,<opportunities>,<phase>,<running>,<discontinuities>,<eligibleMax>,<eligibleTurns>,`
#: `<progressTurns>,<deniedTurns>,<maxWorkerUs>,<epoch>,<saturatedGaps>,<permitted>,<overruns>,<overrunMax>,<schema>`
#: (fields 4..19; the trailing `3` is the schema number the emitter writes)
GAP_ROW = "DIAG,win_gap,"
#: `#CAP,<us>,DIAG,win_gap_overrun,<permitted>,<overruns>,<overrunMax>,<running>,<epoch>` — the **named** overrun.
GAP_OVERRUN_ROW = "DIAG,win_gap_overrun,"
#: `#CAP,<us>,DIAG,playback_build,<path>,…` — the lane's path row (`build_complete`, `grain_denied`, …).
BUILD_ROW = "DIAG,playback_build,"
BUILD_COMPLETE = "build_complete"
#: The **demand path's own gate row** (`DIAG,win_gate`) — qualification-only, one row per state change. It is the
#: instrument that tells "this track owed no window" apart from "the window was refused, and **by which term**"
#: (F-16: five switch-leg runs were spent on that distinction), so a leg that produces no demand can name its reason
#: instead of prompting another run by guesswork.
GATE_ROW = "DIAG,win_gate,"
#: Field positions in the emitted wire form
#: `#CAP,<us>,DIAG,win_gate,t=,s=,x=,len=,ep=,av=,al=,pl=,ed=,od=,pv=,rw=,sv=,rt=,rl=,ow=,dm=,bld=,gr=,rp=,rq=` —
#: read from `DebugSessionCapture::winGateRow`, not from memory.
GATE_EXIT_FIELD = 6
GATE_LOOP_LEN_FIELD = 7
GATE_FLAG_FIRST_FIELD = 8
#: The 16 flag bits, in emission order. Each is read from the **active slot's own state** (see the header's list);
#: `ep` `av` `al` are the eligibility trio and `sv`/`rt` are the servability pair the demand's entry rests on.
GATE_FLAGS: tuple[str, ...] = (
    "ep", "av", "al", "pl", "ed", "od", "pv", "rw", "sv", "rt", "rl", "ow", "dm", "bld", "gr", "rp",
)
GATE_REQUEST_FIELD = GATE_FLAG_FIRST_FIELD + len(GATE_FLAGS)
#: Exits that prove the lane was given work: each one can only be reached with a frame or a request in existence, so
#: a run that shows any of them has exercised the lane even if no page, receipt or publication followed yet.
DEMAND_EXITS: frozenset[str] = frozenset(
    {
        "win_requested",
        "win_request_dropped",
        "win_serving",
        "win_ready",
        "win_waiting",
        "win_blocked",
        "win_blocked_by_request",
        "win_frame_invalidated",
    }
)
#: What each **refusing** exit means, in the active slot's own terms, so the reader is not left to decode flags.
REFUSAL_REASONS: dict[str, str] = {
    "win_no_runtime": "the active slot has no playback runtime allocated",
    "win_track_not_playing": "the state gates refused before any window was considered",
    "win_reprojection_blocked": "a queued playback start, jam or jam playback holds the projection",
    "win_window_ineligible": "the demand's entry trio refused",
    # Task 3.7c changed what this exit can mean: the lane now *requests* a first window where it used to refuse, so what
    # remains is having nothing to build from or nowhere to publish -- not "no window is being served".
    "win_not_servable": "the slot has no view set to publish into, or no current source to build a window from (neither "
                        "a current published root nor the retained revision the loop carries)",
    "win_busy": "an existing promise, owner or outstanding grain holds the slot",
    "win_reprojected_tick": "the tick fell outside the projection, so the demand's absolute position is not this one",
    "win_boundary_rejected": "the declared lead could not place a boundary inside the loop",
    "win_request_refused": "the request itself was refused by name",
}
#: The flags that explain each refusal, and the sentence each flag's `0` adds.
_FLAG_LABELS: dict[str, str] = {
    "ep": "the deadline clock is unstamped",
    "av": "the loop is not demoted",
    "al": "the length is not a whole number of two-bar spans",
    "pl": "the track is not playing",
    "ed": "a note-edit session is active",
    "od": "a deferred overdub stop is pending",
    "pv": "a preview source is active",
    "rw": "a record-return window is bound",
    "sv": "no complete published current window",
    "rt": "the published root is not current",
    "rl": "the coverage role is unavailable",
    "ow": "the slot owns a coverage source",
    "dm": "a view request is active",
    "bld": "a build is in flight",
    "gr": "grains are outstanding",
    "rp": "the tick was reprojected",
}
#: The flags that explain each refusal.
_REFUSAL_FLAGS: dict[str, tuple[str, ...]] = {
    "win_window_ineligible": ("ep", "av", "al"),
    "win_track_not_playing": ("pl", "ed", "od", "pv", "rw"),
    "win_not_servable": ("sv", "rt"),
    "win_busy": ("dm", "rl", "ow", "gr"),
    "win_reprojected_tick": ("rp",),
}

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


#: A capture row's own device stamp: `#CAP,<micros>,…` for the lane's rows, `[<seconds>.<millis>]` for the human ones.
_CAP_US = re.compile(r"^#CAP,(\d+),")
_HUMAN_US = re.compile(r"^\[(\d+)\.(\d{3})\]")


def row_us(line: str) -> int | None:
    """The row's device time in microseconds, or `None` when it carries none.

    Both families are stamped by the same clock -- `#CAP` in microseconds since boot, the human rows as `[12.555]`
    seconds -- so they *are* comparable, and time is the right comparison rather than stream position: a **retained**
    row is delivered after the transport stops, so position would call it post-stop when it was produced mid-session.
    That distinction is the whole reason the post-stop drain exists.
    """
    match = _CAP_US.match(line.strip())
    if match is not None:
        return int(match.group(1))
    match = _HUMAN_US.match(line.strip())
    if match is not None:
        return int(match.group(1)) * 1_000_000 + int(match.group(2)) * 1_000
    return None


def running_intervals(lines: list[str]) -> list[tuple[int, int]]:
    """Device-time ranges `[start_us, stop_us)` in which the transport was running.

    An edge with no stamp is ignored rather than guessed: a range nobody can place in time would silently classify the
    rows it brackets.
    """
    intervals: list[tuple[int, int]] = []
    start: int | None = None
    for line in lines:
        stamp = row_us(line)
        if "Transport started" in line:
            if start is None and stamp is not None:
                start = stamp
        elif "Transport stopped" in line and start is not None:
            intervals.append((start, stamp if stamp is not None else start))
            start = None
    if start is not None:
        last = max((row_us(line) or start) for line in lines)
        intervals.append((start, last))
    return intervals


def _under_transport(us: int | None, intervals: Iterable[tuple[int, int]]) -> bool:
    if us is None:
        return False
    return any(start <= us < stop for start, stop in intervals)


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
                "us": row_us(line),
                "track": fields[4],
                "slot": fields[5],
                "readiness": fields[6],
                "holder": fields[7],
                "revision": fields[8],
                "lead_ticks": fields[9],
            }
        )
    return rows

def frozen_readiness_rows(lines: list[str]) -> list[dict[str, Any]]:
    """The frozen Record-return completion's own receipt rows, in the emitter's field order.

    These are what make a completion **source-qualified**: the row carries the window's own identity (revision, source
    generation, frozen capture content token) and the interval it covers, so a reader never has to infer which window
    became ready from a generic slot verdict. `captureToken` is emitted in full (64-bit), and a row with a zero token
    or an invalid interval is not evidence of a completion.
    """
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if not _is_lane_row(line, FROZEN_READINESS_ROW):
            continue
        fields = _split(line)
        if len(fields) < 15:
            continue
        rows.append(
            {
                "index": index,
                "us": row_us(line),
                "track": fields[4],
                "slot": fields[5],
                "readiness": fields[6],
                "holder": fields[7],
                "revision": fields[8],
                "lead_ticks": fields[9],
                "source_generation": fields[10],
                "capture_token": fields[11],
                "start": fields[12],
                "length": fields[13],
                "loop_length": fields[14],
            }
        )
    return rows


def frozen_receipt_is_source_qualified(row: dict[str, Any]) -> bool:
    """Whether a frozen receipt row names a **real** window: a nonzero content token over a valid interval."""
    try:
        token = int(row["capture_token"])
        start = int(row["start"])
        length = int(row["length"])
        loop_length = int(row["loop_length"])
    except (TypeError, ValueError):
        return False
    return token != 0 and length > 0 and loop_length > 0 and length <= loop_length and start < loop_length



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
                "us": row_us(line),
                # Fields 4/8/9 are the block's own **service** observations: the largest tick delta between two
                # dispatches, the epochs/tick-rewinds that broke that comparison, and the largest delta whose
                # predecessor was *eligible*. 12/13 are the calibration turns and the worst **worker** span, which is
                # the preparation grain's own duration. 14/15 name the era and the instrument's counter overflow.
                # They are read here rather than re-parsed by a caller because 6.5's hardware claims (grain duration,
                # maximum service gaps) are exactly these fields -- see
                # `evidence/2026-10-17-s65-hardware-claims-reading/EVIDENCE.md`.
                "max_delta_ticks": fields[4],
                "opportunities": fields[5],
                "phase": fields[6],
                "running": fields[7],
                "discontinuities": fields[8],
                "eligible_predecessor_max": fields[9],
                "eligible_turns": fields[10],
                "progress_turns": fields[11],
                "denied_turns": fields[12],
                "max_worker_us": fields[13],
                "epoch": fields[14],
                "saturated_gaps": fields[15],
                # Fields 16/17 are the declaration's own `permittedAdvanceTicks` and that block's `overruns`;
                # 19 is the literal schema number the emitter writes (3), which is what makes the row self-describing.
                "permitted": fields[16],
                "overruns": fields[17],
                "overrun_max_ticks": fields[18],
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
                "us": row_us(line),
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
                "us": row_us(line),
                "path": fields[4],
                "loop_len": fields[6],
                "revision": fields[8],
                "track": fields[9],
                "slot": fields[10],
            }
        )
    return rows


def gate_rows(lines: list[str]) -> list[dict[str, Any]]:
    """The demand path's own state-change rows, parsed by the **emitter's** field order.

    A row the emitter never writes is skipped rather than guessed at: `len(fields)` is checked against the request
    field, which is the last one, so a truncated row cannot silently become a row whose `rq` reads as something else.
    """
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if not _is_lane_row(line, GATE_ROW):
            continue
        fields = _split(line)
        if len(fields) <= GATE_REQUEST_FIELD:
            continue
        flags: dict[str, str] = {}
        for offset, name in enumerate(GATE_FLAGS):
            value = fields[GATE_FLAG_FIRST_FIELD + offset]
            flags[name] = value.split("=", 1)[1] if "=" in value else value
        rows.append(
            {
                "index": index,
                "us": row_us(line),
                "track": fields[4].split("=", 1)[-1],
                "slot": fields[5].split("=", 1)[-1],
                "exit": fields[GATE_EXIT_FIELD].split("=", 1)[-1],
                "loop_len": fields[GATE_LOOP_LEN_FIELD].split("=", 1)[-1],
                "flags": flags,
                "request_result": fields[GATE_REQUEST_FIELD].split("=", 1)[-1],
            }
        )
    return rows


def _exit_counts(rows: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["exit"]] = counts.get(row["exit"], 0) + 1
    return counts


def _dominant_exit(counts: dict[str, int]) -> tuple[str, int] | None:
    """The most frequent exit, ties broken by name so two runs of one capture read the same."""
    if not counts:
        return None
    return max(sorted(counts.items()), key=lambda item: item[1])


def _refusal_detail(exit_name: str, row: dict[str, Any] | None) -> str:
    """Translate the refusing exit **and the flag that refused** into the active slot's own terms."""
    reason = REFUSAL_REASONS.get(exit_name, "an exit this reader does not know")
    if row is None:
        return reason
    flags = row["flags"]
    named = [
        _FLAG_LABELS.get(name, name)
        for name in _REFUSAL_FLAGS.get(exit_name, ())
        if flags.get(name) == "0"
    ]
    if not named:
        return reason
    return f"{reason}: {'; '.join(named)}"


def demand_entry_summary(lines: list[str]) -> dict[str, Any]:
    """Whether the lane was given work, and -- when it was not -- **which term refused**.

    Three independent surfaces can show a demand: a gap block with an eligible or progressing turn, a readiness
    receipt, or any gate exit that can only be reached with a frame or a request in existence. The gate rows are the
    only surface that can explain the **absence** of one, which is why they are read here rather than left to the
    reader: five device runs were once spent telling "nothing was owed" from "refused, and by which term" (F-16).
    """
    gaps = gap_rows(lines)
    gap_work = [row for row in gaps if row["eligible_turns"] != "0" or row["progress_turns"] != "0"]
    receipts = readiness_rows(lines)
    gates = gate_rows(lines)
    counts = _exit_counts(gates)
    demand_exits = {name: count for name, count in counts.items() if name in DEMAND_EXITS}
    dominant = _dominant_exit(counts)
    last = gates[-1] if gates else None
    return {
        "observed": bool(gap_work or receipts or demand_exits),
        "gap_work": len(gap_work),
        "receipts": len(receipts),
        "gate_rows": len(gates),
        "exits": counts,
        "demand_exits": demand_exits,
        "dominant_exit": dominant[0] if dominant else None,
        "dominant_count": dominant[1] if dominant else 0,
        "last": last,
        "refusal": _refusal_detail(dominant[0], last) if dominant else None,
    }


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

    A gate exit that can only be reached with a frame or a request in existence counts as a demand too, and is the
    surface that **names** the reason when there is none: see `demand_entry_summary`.
    """
    return bool(demand_entry_summary(lines)["observed"])


def _gate_state(row: dict[str, Any]) -> str:
    """The one line a reader needs from a gate row: where, which exit, and the flags that decide it."""
    flags = row["flags"]
    return (
        f"t={row['track']} s={row['slot']} x={row['exit']} len={row['loop_len']} "
        f"ep={flags.get('ep')} av={flags.get('av')} al={flags.get('al')} "
        f"sv={flags.get('sv')} rt={flags.get('rt')} rq={row['request_result']}"
    )


def _demand_met_detail(summary: dict[str, Any]) -> str:
    """What evidenced the demand -- each surface named, so a reader can see which one carried it."""
    surfaces: list[str] = []
    if summary["demand_exits"]:
        rendered = ", ".join(f"{name}({count})" for name, count in sorted(summary["demand_exits"].items()))
        surfaces.append(f"gate exits={rendered}")
    if summary["gap_work"]:
        surfaces.append(f"gap blocks with eligible/progress turns={summary['gap_work']}")
    if summary["receipts"]:
        surfaces.append(f"readiness receipts={summary['receipts']}")
    detail = "the lane was asked for window work in this capture: " + "; ".join(surfaces)
    last = summary["last"]
    if last is not None:
        detail += f". last gate row {_gate_state(last)}"
    return detail


def _demand_detail(summary: dict[str, Any]) -> str:
    """No demand: say so, **and name the term that refused** rather than prompting another run.

    The wording of F-16's original finding is kept in front, because it is the finding a reader must not lose: a
    dispatch with nothing owed is not a lane defect. What is appended is the measurement five runs once lacked.
    """
    detail = (
        "no eligible/progress turn and no readiness receipt in the whole capture: no slot owed a window build, so "
        "this run did not exercise the lane (a demand needs the rolling lane enabled, or a launch/retry in flight -- "
        "a plain record-and-play owes none)"
    )
    if not summary["gate_rows"]:
        return detail + (
            "; and this capture carries no `DIAG,win_gate` row at all, so this image does not compile the lane's gate "
            "row (it is qualification-only) and the entry term cannot be read from this run"
        )
    counted = sorted(summary["exits"].items(), key=lambda item: (-item[1], item[0]))
    rendered = ", ".join(f"{name}({count})" for name, count in counted)
    detail += f". The lane's own gate rows name why: rows={summary['gate_rows']} exits={rendered}"
    if summary["dominant_exit"] is not None:
        detail += f", dominant={summary['dominant_exit']}({summary['dominant_count']})"
    last = summary["last"]
    if last is not None:
        detail += f"; last row {_gate_state(last)}"
    if summary["refusal"]:
        detail += f" -> {summary['refusal']}"
    return detail


RING_EVIDENCE_LOSS = frozenset({"overflow", "critical_rejected", "tier_a_evicted", "wiped"})
def ring_evidence_loss_rows(lines: list[str]) -> list[dict[str, Any]]:
    """Return explicit ring events that invalidate absence/count conclusions for critical evidence."""
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        fields = _split(line)
        if len(fields) < 4 or fields[0] != "#CAP" or fields[2] != "RING":
            continue
        if fields[3] in RING_EVIDENCE_LOSS:
            rows.append({"index": index, "us": row_us(line), "kind": fields[3], "line": line.strip()})
    return rows
def verify_windowed_lane(
    lines: list[str], *, expected_track_number: int | None = None, switch_target_slot: int | None = None,
    require_ring_stats: bool = False,
) -> dict[str, Any]:
    """Judge the declared lane oracle and, when named, validate the switch target's eligibility."""
    intervals = running_intervals(lines)
    checks: list[dict[str, Any]] = []
    summary = demand_entry_summary(lines)
    losses = ring_evidence_loss_rows(lines)
    stats = ring_stats_rows(lines)
    counter_keys = ("tierA_evict", "overflow", "crit_rej", "wipe")
    if len(stats) >= 2:
        baseline_stats, final_stats = stats[0], stats[-1]
        deltas = {key: final_stats[key] - baseline_stats[key] for key in counter_keys}
        counters_clean = all(delta == 0 for delta in deltas.values())
        scoped_losses = [row for row in losses if row["us"] is None or baseline_stats["us"] is None or row["us"] >= baseline_stats["us"]]
        stats_detail = f"stats_readbacks={len(stats)} deltas={deltas}"
    else:
        deltas = None
        counters_clean = not require_ring_stats
        scoped_losses = losses
        stats_detail = f"stats_readbacks={len(stats)} baseline_and_final_required={require_ring_stats}"
    loss_counts = {kind: sum(row["kind"] == kind for row in scoped_losses) for kind in sorted(RING_EVIDENCE_LOSS)}
    ring_clean = not scoped_losses and counters_clean
    checks.append(_check("capture_evidence_ring_intact", ring_clean,
                         f"critical_evidence_loss_rows={len(scoped_losses)} kinds={loss_counts} {stats_detail}"))
    demand = bool(summary["observed"])
    checks.append(
        _check(
            "window_demand_observed",
            demand,
            _demand_detail(summary) if not demand else _demand_met_detail(summary),
        )
    )

    # **AC-8: the receipt must arrive while the transport runs.** A receipt that only reaches the stream after the
    # stop is exactly what the direct-when-USB-has-room path exists to avoid, so it is a failure and not a pass.
    #
    # **And it must be source-qualified.** The declared term is about the lane's *own* window becoming ready. A generic
    # `win_readiness` row naming `not_servable` is not that: for a first Record take `playbackWindowReadinessForSlot`
    # exits on `!loop.hasCommittedPasses()` before a frozen Capture ready can be named, so accepting it silently passed
    # a run whose frozen completions were never named (the reading that made
    # `captures/e2-lane-rows-202609…-run2` look like a pass). The row that answers the term is the frozen family's own
    # `win_frozen_readiness`, carrying the completed window's identity and interval.
    receipts = readiness_rows(lines)
    receipts_under = [row for row in receipts if _under_transport(row["us"], intervals)]
    frozen = frozen_readiness_rows(lines)
    frozen_under = [row for row in frozen if _under_transport(row["us"], intervals)]
    qualified = [row for row in frozen_under if frozen_receipt_is_source_qualified(row)]
    checks.append(
        _check(
            "readiness_receipts_under_transport",
            bool(qualified),
            f"receipts={len(receipts)} under_transport={len(receipts_under)}"
            + (f" first={receipts_under[0]['readiness']}/{receipts_under[0]['holder']}" if receipts_under else "")
            + f" source_qualified={len(qualified)} of {len(frozen_under)} frozen_receipts_under_transport"
            + (f" first={qualified[0]['readiness']}/{qualified[0]['holder']}"
               f" token={qualified[0]['capture_token']} interval={qualified[0]['start']}+{qualified[0]['length']}"
               f"/{qualified[0]['loop_length']}" if qualified else ""),
        )
    )

    # **Coverage blocks the lane actually served under transport**, in the schemas that carry the declaration: a block
    # with no opportunity is not coverage, and a pre-schema-3 row does not carry `running`/`permitted` to be read.
    gaps = gap_rows(lines)
    gaps_under = [row for row in gaps if row["running"] == "1" and _under_transport(row["us"], intervals)]
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
    overruns_under = [row for row in overruns if row["running"] == "1" and _under_transport(row["us"], intervals)]
    checks.append(
        _check(
            "no_windowed_overrun_under_transport",
            not overruns_under,
            f"overrun_rows={len(overruns)} under_transport={len(overruns_under)}"
            + (f" worst={overruns_under[0]['overrun_max']}" if overruns_under else ""),
        )
    )

    if expected_track_number is not None and switch_target_slot is not None:
        target_track = str(expected_track_number - 1)
        target_slot = str(switch_target_slot - 1)
        target_gates = [
            row for row in gate_rows(lines)
            if row["track"] == target_track and row["slot"] == target_slot
            and _under_transport(row["us"], intervals)
        ]
        aligned = [
            row for row in target_gates
            if all(row["flags"].get(name) == "1" for name in ("ep", "av", "al", "pl"))
        ]
        if not target_gates:
            alignment_detail = f"target t={target_track} s={target_slot}: no win_gate row under transport"
        elif aligned:
            last_aligned = aligned[-1]
            alignment_detail = f"target t={target_track} s={target_slot} span-aligned len={last_aligned['loop_len']}"
        else:
            last_target = target_gates[-1]
            alignment_detail = (
                f"target t={target_track} s={target_slot} has no eligible aligned gate under transport; "
                f"last len={last_target['loop_len']} x={last_target['exit']} "
                f"ep={last_target['flags'].get('ep')} av={last_target['flags'].get('av')} "
                f"al={last_target['flags'].get('al')} pl={last_target['flags'].get('pl')}"
            )
        checks.append(_check("switch_target_span_alignment", bool(aligned), alignment_detail))
    # Successive publications under transport: the precondition for a parity verdict at a boundary (the verdict itself
    # stays the content pipeline's -- see `DEFERRED_TERMS`).
    completions = [
        row for row in build_rows(lines, path=BUILD_COMPLETE) if _under_transport(row["us"], intervals)
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
        # The demand's own entry reading travels with the verdict: when there is no demand, this is **where the reason
        # is**, and a reader must not have to re-derive it from the capture (F-16).
        "demand": {
            "observed": demand,
            "gap_work": summary["gap_work"],
            "receipts": summary["receipts"],
            "gate_rows": summary["gate_rows"],
            "exits": dict(summary["exits"]),
            "dominant_exit": summary["dominant_exit"],
            "refusal": summary["refusal"],
            "last": summary["last"],
        },
        "deferred": [dict(entry) for entry in DEFERRED_TERMS],
        # A green lane is **not** F-7/F-8 qualification: two declared terms are not judged here at all.
        "oracle_complete": False,
    }


def _legacy_int_option(args: object, name: str) -> int | None:
    values = list(getattr(args, "legacy_args", []) or [])
    for index, token in enumerate(values[:-1]):
        if token == name:
            try:
                return int(values[index + 1])
            except ValueError:
                return None
    return None


def verify_windowed_content_parity(lines: list[str], args: object) -> dict[str, Any]:
    """Combine the legacy leg, lane oracle, target alignment and Record Snapshot progress."""
    from hitl.verify.display_window import verify_long_loop_display_window
    from hitl.verify.record_snapshot import verify_record_snapshot_progress

    leg = getattr(args, "windowed_leg_run_report", None)
    if not isinstance(leg, dict):
        import json
        from pathlib import Path
        out_dir = getattr(args, "out_dir", None)
        reports = sorted(Path(out_dir).glob("host_midi_hitl_long_loop_display_window_*.json")) if out_dir else []
        if reports:
            try:
                standalone = json.loads(reports[-1].read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                standalone = {}
            leg = standalone.get("serial_verification")
    if not isinstance(leg, dict):
        leg = verify_long_loop_display_window(lines, args)
    track_number = getattr(args, "track_number", None)
    target_slot = getattr(args, "windowed_switch_target_slot", None)
    if target_slot is None:
        target_slot = _legacy_int_option(args, "--switch-target-slot")
    lane = verify_windowed_lane(
        lines,
        expected_track_number=track_number if target_slot is not None else None,
        switch_target_slot=target_slot,
        require_ring_stats=True,
    )
    loop_slot = getattr(args, "windowed_record_slot", getattr(args, "loop_slot", None))
    snapshot = verify_record_snapshot_progress(lines, track_number=track_number, loop_slot=loop_slot)
    issues: list[str] = []
    if not leg.get("ok"):
        issues.extend(f"leg: {issue}" for issue in (leg.get("issues") or ["failed"]))
    issues.extend(f"lane: {issue}" for issue in lane["issues"])
    if snapshot["applicable"]:
        issues.extend(f"snapshot: {issue}" for issue in snapshot["issues"])
    return {
        "ok": not issues,
        "issues": issues,
        "leg": leg,
        "lane": lane,
        "snapshot": snapshot,
        "deferred": lane["deferred"],
        "oracle_complete": False,
    }
def ring_stats_rows(lines: list[str]) -> list[dict[str, Any]]:
    """Parse stopped-only `RING,stats` counter readbacks."""
    counters = ("tierA_evict", "overflow", "crit_rej", "wipe")
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        fields = _split(line)
        if len(fields) < 4 or fields[0] != "#CAP" or fields[2:4] != ["RING", "stats"]:
            continue
        values = {}
        for item in fields[4:]:
            if "=" not in item:
                continue
            key, value = item.split("=", 1)
            try:
                values[key] = int(value)
            except ValueError:
                continue
        if all(key in values for key in counters):
            rows.append({"index": index, "us": row_us(line), **values})
    return rows
