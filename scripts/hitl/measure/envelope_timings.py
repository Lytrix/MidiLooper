"""S7.5a envelope measurements: one measurement per missing continuity timing.

S7.5a — continuity envelope and timing oracle (`openspec/changes/runtime-working-set-ownership/tasks.md`)
requires numeric limits, units, measurement points and clock correlation to be **derived before** any
qualifying run, from grain costs and the worst permitted service gap (`DEC-108` §13b: "no invented
microsecond budget"). This module is the **measurement** half of that work: it reads the device's own rows
out of one serial capture and reports, per metric, what was measured, in which unit, from which rows — or
names why nothing could be measured.

Each metric has its own function (and its own test in `scripts/test_envelope_timings.py`) so that a metric
can be derived, challenged or re-measured on its own, as the task requires. **No function invents a
number and no function declares a limit**: the envelope's limits are the owner's decision (S7.5a stays
unchecked until they are signed off).

Three metric states, and their meaning:

* `measured` — rows carried the quantity the metric names; `value` is that quantity.
* `unproven` — the rows exist but cannot carry the metric (a maximum-only family that cannot show a
  distribution, an all-zero instrument that cannot distinguish "no lateness" from "never fed", or a proxy
  whose number would be misread). `value` is **None**; the proxy reading, if any, stays in `detail`.
* `missing` — the capturing image cannot carry the metric (family env-gated off, or no instrument exists at
  all). `value` is **None** and `reason` names what is owed.

Row formats are taken from the emitters, not from memory:

* `#CAP,<us>,DIAG,<family>,<maxUs>,<overCount>` — `src/Utils/RuntimeTimingTelemetry.cpp`
  (families documented in `include/Utils/DebugSessionCapture.h`).
* `#CAP,<us>,DIAG,win_gap,<maxDelta>,<opportunities>,<phase>,<running>,<discontinuities>,<eligibleMax>,
  <eligibleTurns>,<progressTurns>,<deniedTurns>,<maxWorkerUs>,<epoch>,<saturatedGaps>,<permitted>,
  <overruns>,<overrunMax>,<schema>` — verified in `scripts/hitl/verify/windowed_lane.py`.
* `#CAP,<us>,MO,<type>,<ch>,<d1>,<d2>` — `src/Utils/DebugSessionCapture.cpp`.
* `#CAP,<us>,DIAG,droid_queue,<depth>,<oldestAgeUs>,<overflowCount>` — same file.
* `#CAP,<us>,DIAG,reclaim,...` and the stored-note rows (`REVT`) as emitted per capture.
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional, Sequence

#: The three availability states a metric can be in.
MEASURED = "measured"
UNPROVEN = "unproven"
MISSING = "missing"

_CAP_RE = re.compile(r"#CAP,(\d+),")
_TELEMETRY_RE = re.compile(r"#CAP,(\d+),DIAG,([a-z_]+),(\d+),(\d+)")
_WIN_GAP_RE = re.compile(r"#CAP,(\d+),DIAG,win_gap,(.*)$")
_MO_RE = re.compile(r"#CAP,(\d+),MO,(\d+),(\d+),(-?\d+),(-?\d+)")
_DROID_QUEUE_RE = re.compile(r"#CAP,(\d+),DIAG,droid_queue,(\d+),(\d+),(\d+)")
_RECLAIM_RE = re.compile(r"#CAP,(\d+),DIAG,reclaim,(\d+),(\d+),(\d+),(\d+),(\d+),(\w+),")
_STORED_TICK_RE = re.compile(r"#CAP,(\d+),REVT,[^,]*,(-?\d+)")
#: `#CAP,<us>,MO,<type>,<ch>,<d1>,<d2>,<tick>` — the S7.5a instrument that put the **scheduled tick** on the
#: emission row. Captures predating it carry six fields, so the tick group is simply absent for them.
_MO_TICK_RE = re.compile(r"#CAP,(\d+),MO,(\d+),(\d+),(-?\d+),(-?\d+),(\d+)")
#: `#CAP,<us>,DIAG,envelope_instr,capstamp_max=<ticks>,capstamp_n=<n>,repl_wait_us=<us>,repl_work_us=<us>,`
#: `repl_refused=<n>,repl_pairs=<n>` — one aggregated row per telemetry window, from
#: `DebugSessionCapture::envelopeInstrRow` (fields verified against the emitter).
_ENVELOPE_INSTR_RE = re.compile(r"#CAP,(\d+),DIAG,envelope_instr,(.*)$")
_ENVELOPE_INSTR_FIELDS = ("capstamp_max", "capstamp_n", "repl_wait_us", "repl_work_us", "repl_refused", "repl_pairs")

#: `DIAG,late_on` / `late_off` are the output-lateness surfaces (`RuntimeTimingTelemetry.cpp`). Their
#: producer is not exercised by every route, and a capture whose samples are all zero with a zero over-count
#: cannot distinguish "no lateness" from "not fed" — that is `UNPROVEN`, never `measured 0`.
_LATENESS_FAMILIES = ("late_on", "late_off")

#: The DROID host-feedback queue is the one recorded queue *residence* instrument. Its family is gated by
#: `SESSION_CAPTURE_DROID_QUEUE`, which the default capture env sets to **0**
#: (`platformio.ini`, `[env:teensy41-capture-serial]`), so a default-image capture cannot carry it; the
#: `teensy41-capture-serial-investigation-diag` env turns it back on.
DROID_QUEUE_ENV_GATE = "SESSION_CAPTURE_DROID_QUEUE=0 (default capture env); investigation-diag env only"

#: The 16th-grid the edit/record fixture streams on (`TICKS_PER_16TH_STEP` = 48, `include/Globals.h`).
FIXTURE_16TH_TICKS = 48


@dataclass
class Measurement:
    """One metric's reading: the value, its unit, the rows it came from, or why it is not available."""

    metric: str
    unit: str
    availability: str
    value: Optional[float] = None
    source_rows: int = 0
    reason: str = ""
    detail: dict = field(default_factory=dict)

    def as_dict(self) -> dict:
        out = {
            "metric": self.metric,
            "unit": self.unit,
            "availability": self.availability,
            "value": self.value,
            "source_rows": self.source_rows,
        }
        if self.reason:
            out["reason"] = self.reason
        if self.detail:
            out["detail"] = self.detail
        return out


def _telemetry(lines: Iterable[str], family: str) -> list[tuple[int, int, int]]:
    """`(us, maxUs, overCount)` for every `DIAG,<family>` row, in capture order."""
    out: list[tuple[int, int, int]] = []
    for line in lines:
        match = _TELEMETRY_RE.search(line)
        if match is not None and match.group(2) == family:
            out.append((int(match.group(1)), int(match.group(3)), int(match.group(4))))
    return out


def _win_gap_rows(lines: Iterable[str]) -> list[list[str]]:
    """Every complete `DIAG,win_gap` row, as its field list (16 fields, emitter order)."""
    parsed: list[list[str]] = []
    for line in lines:
        match = _WIN_GAP_RE.search(line)
        if match is None:
            continue
        fields = [field.strip() for field in match.group(2).split(",")]
        if len(fields) >= 16:
            parsed.append(fields)
    return parsed


def measure_midi_input_service_gap(lines: Sequence[str]) -> Measurement:
    """Maximum MIDI input-service gap, µs — `DIAG,midi_gap,<maxUs>,<overCount>`.

    The row is a **maximum** plus a count of samples over the telemetry's own limiter, so this measures the
    worst gap seen; the distribution (p50/p99) is not in the row and is named as owed.
    """
    rows = _telemetry(lines, "midi_gap")
    if not rows:
        return Measurement(
            "max_midi_input_service_gap",
            "us",
            MISSING,
            reason="no DIAG,midi_gap row in this capture (instrument exists: RuntimeTimingTelemetry)",
        )
    worst = max(rows, key=lambda item: item[1])
    return Measurement(
        "max_midi_input_service_gap",
        "us",
        MEASURED,
        value=float(worst[1]),
        source_rows=len(rows),
        detail={
            "over_count_sum": sum(row[2] for row in rows),
            "distribution_available": False,
            "distribution_owed": "p50/p99 need a per-sample row; the family carries a maximum only",
            "row_emitter": "src/Utils/RuntimeTimingTelemetry.cpp (midi_gap)",
        },
    )


def measure_midi_input_service_duration(lines: Sequence[str]) -> Measurement:
    """Maximum time spent *inside* one MIDI input service, µs — `DIAG,midi_input,<maxUs>,<overCount>`."""
    rows = _telemetry(lines, "midi_input")
    if not rows:
        return Measurement(
            "max_midi_input_service_duration",
            "us",
            MISSING,
            reason="no DIAG,midi_input row in this capture",
        )
    worst = max(rows, key=lambda item: item[1])
    return Measurement(
        "max_midi_input_service_duration",
        "us",
        MEASURED,
        value=float(worst[1]),
        source_rows=len(rows),
        detail={"over_count_sum": sum(row[2] for row in rows)},
    )


def _envelope_instr_totals(lines: Iterable[str]) -> Optional[dict[str, int]]:
    """Aggregate `DIAG,envelope_instr` rows: **max** for the max-typed fields, **sum** for the counts.

    Returns None when no row is present or a row is malformed, so a legacy capture falls back to its proxy.
    """
    totals: dict[str, int] = {}
    seen = False
    for line in lines:
        match = _ENVELOPE_INSTR_RE.search(line)
        if match is None:
            continue
        values: dict[str, int] = {}
        for field in match.group(2).split(","):
            name, sep, raw = field.partition("=")
            if not sep:
                continue
            try:
                values[name.strip()] = int(raw)
            except ValueError:
                return None
        if any(name not in values for name in _ENVELOPE_INSTR_FIELDS):
            return None
        seen = True
        for name in ("capstamp_max", "repl_wait_us", "repl_work_us"):
            totals[name] = max(totals.get(name, 0), values[name])
        for name in ("capstamp_n", "repl_refused", "repl_pairs"):
            totals[name] = totals.get(name, 0) + values[name]
    return totals if seen else None


def measure_capture_timestamp_error(lines: Sequence[str]) -> Measurement:
    """Capture timestamp error, in **ticks** — `DIAG,envelope_instr`'s `capstamp_max` when the capture carries it.

    That row is the instrument built for this metric: the playback tick at the entry to the MIDI input service
    against the tick the captured event was stamped with (the firmware stamps note ticks from `getCurrentTick()`
    at *process* time, not at arrival). A capture predating the row keeps the schedule-relative proxy — stored note
    ticks against the grid the host streamed — as `unproven` with its reading in `detail`, never as the metric.
    """
    totals = _envelope_instr_totals(lines)
    if totals is not None:
        return Measurement(
            "capture_timestamp_error",
            "ticks",
            MEASURED,
            value=float(totals["capstamp_max"]),
            source_rows=totals["capstamp_n"],
            detail={
                "basis": "device row DIAG,envelope_instr.capstamp_max (input-service entry tick vs stored tick)",
                "samples": totals["capstamp_n"],
            },
        )
    ticks = [int(match.group(2)) for line in lines if (match := _STORED_TICK_RE.search(line)) is not None]
    if not ticks:
        return Measurement(
            "capture_timestamp_error",
            "ticks",
            MISSING,
            reason=(
                "no DIAG,envelope_instr row and no stored-note (REVT) row in this capture; the stamp instrument "
                "is DebugSessionCapture::envelopeInstrRow on an image that carries it"
            ),
        )
    ticks.sort()
    errors = [(right - left) % FIXTURE_16TH_TICKS for left, right in zip(ticks, ticks[1:])]
    return Measurement(
        "capture_timestamp_error",
        "ticks",
        UNPROVEN,
        value=None,
        source_rows=len(ticks),
        reason=(
            "this capture predates the DIAG,envelope_instr row: the schedule-relative proxy below is not the "
            "instrument's reading and must not be reported as the metric"
        ),
        detail={
            "proxy_worst_off_grid_ticks": max(errors) if errors else 0,
            "proxy_basis": (
                "consecutive stored note ticks against the 16th grid the host streamed"
            ),
            "stored_tick_count": len(ticks),
            "stored_ticks_head": ticks[:32],
        },
    )


def measure_per_route_output_lateness(lines: Sequence[str]) -> Measurement:
    """Per-route output lateness, µs — `DIAG,late_on` / `DIAG,late_off` maxima.

    An all-zero reading with a zero over-count is **unproven**, not zero lateness: the same shape would
    appear if the producer were never fed, and nothing in the row distinguishes the two. Fault injection or
    a non-zero sample is required before this metric can carry a number.
    """
    rows: list[tuple[int, int, int]] = []
    for family in _LATENESS_FAMILIES:
        rows.extend(_telemetry(lines, family))
    if not rows:
        return Measurement(
            "per_route_output_lateness",
            "us",
            MISSING,
            reason="no DIAG,late_on / DIAG,late_off row in this capture",
        )
    worst = max(rows, key=lambda item: item[1])
    over = sum(row[2] for row in rows)
    if worst[1] == 0 and over == 0:
        return Measurement(
            "per_route_output_lateness",
            "us",
            UNPROVEN,
            value=None,
            source_rows=len(rows),
            reason=(
                "every sample is 0 with a 0 over-count: 'no lateness' and 'producer never fed' are "
                "indistinguishable in this row, so no number can be claimed"
            ),
            detail={"provenness_owed": "a non-zero sample or a named fault-injection control"},
        )
    return Measurement(
        "per_route_output_lateness",
        "us",
        MEASURED,
        value=float(worst[1]),
        source_rows=len(rows),
        detail={"families": list(_LATENESS_FAMILIES), "over_count_sum": over},
    )


def measure_per_route_output_jitter(lines: Sequence[str]) -> Measurement:
    """Per-route output jitter, µs — deviation from the route's fitted (emission µs ↔ scheduled tick) line.

    Jitter is scheduled-versus-actual, so it needs the scheduled tick per emitted event — which is what the S7.5a
    instrument put on the emission row. Per route, the device clock is fitted from that route's own pairs, and the
    metric is the worst deviation of an emission instant from the instant its own scheduled tick implies. Captures
    without the tick column keep the onset-spacing proxy (dominated by musical rests) as `unproven`, in `detail`.
    """
    per_route: dict[tuple[int, int], list[tuple[int, int]]] = {}
    for line in lines:
        match = _MO_TICK_RE.search(line)
        if match is None:
            continue
        route = (int(match.group(2)), int(match.group(3)))
        per_route.setdefault(route, []).append((int(match.group(6)), int(match.group(1))))
    if per_route:
        route, pairs = max(per_route.items(), key=lambda item: len(item[1]))
        pairs.sort()
        tick_span = pairs[-1][0] - pairs[0][0]
        if len(pairs) >= 8 and tick_span > 0:
            rate = (pairs[-1][1] - pairs[0][1]) / tick_span
            base_tick, base_us = pairs[0]
            residuals = sorted(abs(us - base_us - (tick - base_tick) * rate) for tick, us in pairs)
            return Measurement(
                "per_route_output_jitter",
                "us",
                MEASURED,
                value=float(residuals[-1]),
                source_rows=len(pairs),
                detail={
                    "route_key_(type,ch)": list(route),
                    "basis": "deviation from the route's fitted (emission µs ↔ scheduled tick) line",
                    "fitted_us_per_tick": round(rate, 3),
                    "median_residual_us": round(residuals[len(residuals) // 2], 3),
                    "samples": len(pairs),
                    "caveat": "one clock rate is assumed inside the window; a tempo change inflates the residual",
                },
            )
        return Measurement(
            "per_route_output_jitter",
            "us",
            UNPROVEN,
            value=None,
            source_rows=len(pairs),
            reason=(
                f"only {len(pairs)} emission(s) with a scheduled tick on this route"
                + (" (no tick span)" if tick_span <= 0 else "")
                + ": not enough to fit the route's clock line"
            ),
            detail={"route_key_(type,ch)": list(route)},
        )
    onsets = sorted(int(match.group(1)) for line in lines if (match := _MO_RE.search(line)) is not None)
    if len(onsets) < 3:
        return Measurement(
            "per_route_output_jitter",
            "us",
            MISSING,
            reason=f"only {len(onsets)} MO row(s): not enough onset samples for a spacing distribution",
        )
    deltas = sorted(right - left for left, right in zip(onsets, onsets[1:]) if right > left)
    if not deltas:
        return Measurement("per_route_output_jitter", "us", MISSING, reason="no distinct MO timestamps")
    return Measurement(
        "per_route_output_jitter",
        "us",
        UNPROVEN,
        value=None,
        source_rows=len(onsets),
        reason=(
            "this capture predates the emission-row tick column: onset spacing across a capture includes musical "
            "rests, so the spread is not a jitter and must not be read as one"
        ),
        detail={
            "proxy_median_onset_delta_us": deltas[len(deltas) // 2],
            "proxy_onset_spread_us": deltas[-1] - deltas[0],
            "onset_samples": len(deltas),
        },
    )


def measure_queue_residence(lines: Sequence[str]) -> Measurement:
    """Queue residence, µs — `DIAG,droid_queue,<depth>,<oldestAgeUs>,<overflowCount>`.

    The DROID host-feedback queue is the one recorded residence instrument, and its family is gated by
    `SESSION_CAPTURE_DROID_QUEUE`, which the **default capture env disables** — so a default-image capture
    cannot carry this metric by construction.
    """
    rows: list[tuple[int, int, int]] = []
    for line in lines:
        match = _DROID_QUEUE_RE.search(line)
        if match is not None:
            rows.append((int(match.group(2)), int(match.group(3)), int(match.group(4))))
    if not rows:
        return Measurement(
            "queue_residence",
            "us",
            MISSING,
            reason=f"no DIAG,droid_queue row; family env-gated: {DROID_QUEUE_ENV_GATE}",
        )
    worst = max(rows, key=lambda item: item[1])
    return Measurement(
        "queue_residence",
        "us",
        MEASURED,
        value=float(worst[1]),
        source_rows=len(rows),
        detail={
            "scope": "DROID host-feedback queue only; every other queue has no residence instrument",
            "max_depth": max(row[0] for row in rows),
            "overflow_sum": sum(row[2] for row in rows),
        },
    )


def measure_route_throughput(lines: Sequence[str]) -> Measurement:
    """Route throughput, events/s — sustained over the capture and the busiest 100 ms window.

    The route key is the row's own `(type, ch)` pair; the emitter writes `#CAP,<us>,MO,<type>,<ch>,<d1>,<d2>`.
    """
    by_route: dict[tuple[int, int], list[int]] = {}
    for line in lines:
        match = _MO_RE.search(line)
        if match is None:
            continue
        route = (int(match.group(2)), int(match.group(3)))
        by_route.setdefault(route, []).append(int(match.group(1)))
    if not by_route:
        return Measurement("route_throughput", "events/s", MISSING, reason="no MO row in this capture")
    busiest_route, timestamps = max(by_route.items(), key=lambda item: len(item[1]))
    timestamps.sort()
    span_us = max(1, timestamps[-1] - timestamps[0])
    sustained = len(timestamps) * 1_000_000.0 / span_us
    peak = 0.0
    window_us = 100_000
    start = 0
    for end, stamp in enumerate(timestamps):
        while stamp - timestamps[start] > window_us:
            start += 1
        peak = max(peak, (end - start + 1) * 1_000_000.0 / window_us)
    return Measurement(
        "route_throughput",
        "events/s",
        MEASURED,
        value=peak,
        source_rows=len(timestamps),
        detail={
            "route_key_(type,ch)": list(busiest_route),
            "sustained_events_per_s": round(sustained, 3),
            "peak_window_us": window_us,
            "span_us": span_us,
            "routes_seen": len(by_route),
        },
    )


def measure_same_tick_burst(lines: Sequence[str]) -> Measurement:
    """Same-tick burst, events — emissions sharing one **scheduled tick**, from the emission row's tick column.

    The admission limit applies per musical tick, which is what this counts when the tick column is present (the
    S7.5a instrument). Captures predating it keep the device-microsecond-stamp burst as `unproven`, with that
    reading in `detail`.
    """
    ticks: dict[int, int] = {}
    for line in lines:
        match = _MO_TICK_RE.search(line)
        if match is not None:
            tick = int(match.group(6))
            ticks[tick] = ticks.get(tick, 0) + 1
    if ticks:
        worst_tick, worst = max(ticks.items(), key=lambda item: item[1])
        return Measurement(
            "same_tick_burst",
            "events",
            MEASURED,
            value=float(worst),
            source_rows=len(ticks),
            detail={
                "basis": "emissions sharing one scheduled tick (emission-row tick column)",
                "worst_tick": worst_tick,
                "distinct_ticks": len(ticks),
            },
        )
    stamps: dict[int, int] = {}
    for line in lines:
        match = _MO_RE.search(line)
        if match is not None:
            stamp = int(match.group(1))
            stamps[stamp] = stamps.get(stamp, 0) + 1
    if not stamps:
        return Measurement("same_tick_burst", "events", MISSING, reason="no MO row in this capture")
    worst_stamp, worst = max(stamps.items(), key=lambda item: item[1])
    return Measurement(
        "same_tick_burst",
        "events",
        UNPROVEN,
        value=None,
        source_rows=len(stamps),
        reason=(
            "this capture predates the emission-row tick column: measured on the emission microsecond stamp "
            "instead, which is a lower bound, and the admission limit applies per musical tick"
        ),
        detail={"proxy_worst_emission_stamp_us": worst_stamp, "proxy_burst_events": worst},
    )


def measure_deferred_owner_service_advance(lines: Sequence[str]) -> Measurement:
    """Per-opportunity service advance, ticks — `DIAG,win_gap`'s `maxDelta` vs its `permitted`.

    This is the measured half of `Delta-max × H × bounded per-track work`: the row reports the worst phase
    advance across a turn whose predecessor was eligible, beside the declared permitted value it is judged
    against (`include/Track.h`).
    """
    parsed = _win_gap_rows(lines)
    if not parsed:
        return Measurement(
            "deferred_owner_service_advance",
            "ticks",
            MISSING,
            reason="no complete DIAG,win_gap row in this capture (qualification-image family)",
        )
    return Measurement(
        "deferred_owner_service_advance",
        "ticks",
        MEASURED,
        value=float(max(int(row[0]) for row in parsed)),
        source_rows=len(parsed),
        detail={
            "declared_permitted_ticks": sorted({int(row[12]) for row in parsed}),
            "overrun_sum": sum(int(row[13]) for row in parsed),
            "overrun_max_ticks": max(int(row[14]) for row in parsed),
            "saturated_gap_sum": sum(int(row[11]) for row in parsed),
            "caveat": "blocked runs of lost turns are not held by a per-turn constant (DEC-108 §13b)",
        },
    )


def measure_deferred_owner_denial(lines: Sequence[str]) -> Measurement:
    """Deferred-owner denial and rotation, **turns** — `DIAG,win_gap`'s `deniedTurns`/`eligibleTurns`."""
    parsed = _win_gap_rows(lines)
    if not parsed:
        return Measurement(
            "deferred_owner_denial",
            "turns",
            MISSING,
            reason="no complete DIAG,win_gap row in this capture (qualification-image family)",
        )
    return Measurement(
        "deferred_owner_denial",
        "turns",
        MEASURED,
        value=float(max(int(row[8]) for row in parsed)),
        source_rows=len(parsed),
        detail={
            "opportunities_sum": sum(int(row[1]) for row in parsed),
            "eligible_turns_sum": sum(int(row[6]) for row in parsed),
            "progress_turns_sum": sum(int(row[7]) for row in parsed),
            "rotation_bound_turns": "NUM_TRACKS - 1 = 7 (include/Track.h)",
        },
    )


def measure_capture_replenishment(lines: Sequence[str]) -> Measurement:
    """Capture replenishment request→completion, µs — `DIAG,envelope_instr` when the capture carries it.

    The device row reports the replenishment owner's own pair: a **refused** attempt (below target but the reserve
    could not be topped up — a request that could not complete) and the top-up that resolved it, whose wait is the
    latency this metric wants. A capture with refusals but no completed pair has not observed the interval, so it is
    `unproven` rather than reported as a number. Captures predating the row fall back to the inter-reclaim cadence,
    which is a cadence and never the latency: that reading stays in `detail`.
    """
    totals = _envelope_instr_totals(lines)
    if totals is not None:
        if totals["repl_pairs"] > 0:
            return Measurement(
                "capture_replenishment",
                "us",
                MEASURED,
                value=float(totals["repl_wait_us"]),
                source_rows=totals["repl_pairs"],
                detail={
                    "basis": "device row DIAG,envelope_instr (refused attempt → resolving top-up)",
                    "refused_attempts": totals["repl_refused"],
                    "completed_pairs": totals["repl_pairs"],
                    "max_replenish_work_us": totals["repl_work_us"],
                },
            )
        return Measurement(
            "capture_replenishment",
            "us",
            UNPROVEN,
            value=None,
            source_rows=totals["repl_refused"],
            reason=(
                f"{totals['repl_refused']} refused replenish attempt(s) and no completed pair in this capture: "
                "the request→completion interval was not observed"
            ),
            detail={"max_replenish_work_us": totals["repl_work_us"]},
        )
    stamps = sorted(
        int(match.group(1)) for line in lines if (match := _RECLAIM_RE.search(line)) is not None
    )
    if len(stamps) < 2:
        return Measurement(
            "capture_replenishment",
            "us",
            MISSING,
            reason=(
                f"no DIAG,envelope_instr row (this capture predates the instrument) and only {len(stamps)} "
                "DIAG,reclaim row(s): no interval to measure"
            ),
        )
    gaps = [right - left for left, right in zip(stamps, stamps[1:])]
    return Measurement(
        "capture_replenishment",
        "us",
        UNPROVEN,
        value=None,
        source_rows=len(stamps),
        reason=(
            "no request→completion row in this capture: the inter-reclaim interval is a cadence, not "
            "replenishment latency, and it is dominated by idle stretches"
        ),
        detail={
            "proxy_median_reclaim_interval_us": sorted(gaps)[len(gaps) // 2],
            "proxy_max_reclaim_interval_us": max(gaps),
            "owed": "the DIAG,envelope_instr row (built) on a capture that contains it",
        },
    )


#: Host log lines carry `[<seconds>.<micros>]`; used when an acceptance marker is a host line.
LINE_TIMESTAMP_RE = re.compile(r"^\[(\d+)\.(\d+)\]")


def _preceding_cap_stamp(lines: Sequence[str], marker_line: str) -> Optional[int]:
    """Device µs of the nearest `#CAP` row at or before `marker_line`."""
    last: Optional[int] = None
    for line in lines:
        if line == marker_line:
            return last
        match = _CAP_RE.search(line)
        if match is not None:
            last = int(match.group(1))
    return None


def measure_action_to_audible(lines: Sequence[str], *, accept_row: Optional[str] = None) -> Measurement:
    """Action-to-audible, µs — the first emission row *after* a named **acceptance** row.

    Needs an acceptance marker; the caller names it (HITL host markers are not device rows). The interval is
    positional (the marker's own line index) and its base is the marker's **device** time — the nearest
    `#CAP` row at or before it — so a host-stamped marker is never mixed with device-stamped emissions
    (that would subtract host microseconds from device microseconds). The marker's host time, when it has
    one, is reported in `detail` for the host↔device clock correlation the envelope owes separately.
    """
    if not accept_row:
        return Measurement(
            "action_to_audible",
            "us",
            MISSING,
            reason="no acceptance marker named (pass accept_row=<text of the host's acceptance line>)",
        )
    marker_index: Optional[int] = None
    for index, line in enumerate(lines):
        if accept_row in line:
            marker_index = index
            break
    if marker_index is None:
        return Measurement(
            "action_to_audible", "us", MISSING, reason=f"acceptance row not found: {accept_row!r}"
        )
    marker_line = lines[marker_index]
    accept_stamp = _preceding_cap_stamp(lines, marker_line)
    host = LINE_TIMESTAMP_RE.search(marker_line)
    host_stamp: Optional[int] = None
    if host is not None:
        host_stamp = int(host.group(1)) * 1_000_000 + int((host.group(2) + "000000")[:6])
    if accept_stamp is None:
        return Measurement(
            "action_to_audible",
            "us",
            MISSING,
            reason=(
                "the acceptance row carries no device-time base (no preceding #CAP row), so no interval "
                "can be measured"
            ),
        )
    after = []
    for line in lines[marker_index + 1 :]:
        match = _MO_RE.search(line)
        if match is not None:
            after.append(int(match.group(1)))
    if not after:
        return Measurement(
            "action_to_audible",
            "us",
            MISSING,
            reason="no MO row after the acceptance marker in this capture",
        )
    earliest = min(after)
    if earliest < accept_stamp:
        return Measurement(
            "action_to_audible",
            "us",
            UNPROVEN,
            value=None,
            source_rows=len(after),
            reason=(
                "an emission after the marker carries an earlier device stamp than the marker's base, so the "
                "capture is not device-time ordered around the marker and the interval cannot be read"
            ),
            detail={
                "accept_row": accept_row,
                "device_base_us": accept_stamp,
                "earliest_emission_us": earliest,
                "marker_host_time_us": host_stamp,
                "owed": "host↔device clock correlation for a host-stamped marker, or a device-stamped marker",
            },
        )
    return Measurement(
        "action_to_audible",
        "us",
        MEASURED,
        value=float(earliest - accept_stamp),
        source_rows=len(after),
        detail={
            "accept_row": accept_row,
            "basis": "earliest emission row after the acceptance row's position",
            "device_base_us": accept_stamp,
            "marker_host_time_us": host_stamp,
        },
    )


def metric_names() -> list[str]:
    """Every metric this module measures, in Table B order."""
    return [
        "max_midi_input_service_gap",
        "max_midi_input_service_duration",
        "capture_timestamp_error",
        "per_route_output_lateness",
        "per_route_output_jitter",
        "queue_residence",
        "route_throughput",
        "same_tick_burst",
        "deferred_owner_service_advance",
        "deferred_owner_denial",
        "capture_replenishment",
        "action_to_audible",
    ]


def measure_all(lines: Sequence[str], *, accept_row: Optional[str] = None) -> list[Measurement]:
    """Every measurement for one capture, one entry per metric (no metric folds into another)."""
    return [
        measure_midi_input_service_gap(lines),
        measure_midi_input_service_duration(lines),
        measure_capture_timestamp_error(lines),
        measure_per_route_output_lateness(lines),
        measure_per_route_output_jitter(lines),
        measure_queue_residence(lines),
        measure_route_throughput(lines),
        measure_same_tick_burst(lines),
        measure_deferred_owner_service_advance(lines),
        measure_deferred_owner_denial(lines),
        measure_capture_replenishment(lines),
        measure_action_to_audible(lines, accept_row=accept_row),
    ]


def _main(argv: Sequence[str]) -> int:
    if not argv:
        print("usage: python -m hitl.measure.envelope_timings <serial-log> [--accept-row TEXT] [--json]")
        return 2
    path = Path(argv[0])
    accept_row = None
    if "--accept-row" in argv:
        accept_row = argv[argv.index("--accept-row") + 1]
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    results = measure_all(lines, accept_row=accept_row)
    if "--json" in argv:
        print(json.dumps([item.as_dict() for item in results], indent=2))
    else:
        for item in results:
            value = "-" if item.value is None else f"{item.value:g}"
            print(
                f"{item.metric:36s} {item.availability:9s} {value:>12s} {item.unit:10s} "
                f"rows={item.source_rows}"
            )
            if item.reason:
                print(f"{'':36s} reason: {item.reason}")
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(_main(sys.argv[1:]))
