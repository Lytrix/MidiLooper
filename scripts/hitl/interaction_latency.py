#!/usr/bin/env python3
"""S4f.11 — acceptance -> execution latency per interaction class, read from the device's own rows.

Invariant 31 (DEC-078) separates **acceptance** (immediate, unconditional) from **execution** (scheduled by the
MVCC pipeline), which makes "how long from acceptance to execution" a first-class quantity that differs by orders
of magnitude per class. Design Revision 17 declares an expectation per class and states that *measurement replaces
the numbers*; this module is that measurement. It never invents one: a class the capture does not exercise is
reported as `not_exercised`, together with the rows it looked for.

**Every pair is device time.** A `#CAP,<us>,…` row carries `micros()` directly; a directly printed row carries the
same clock as its `[<s>.<ms>]` prefix, at millisecond resolution. That resolution travels with the sample
(`resolution_us`), so a number derived from a printed row is never presented as microsecond-exact. Pairing is by
**device order**, not file order — the evidence ring flushes in batches
(`captures/s4f22-step11-h2`), and pairing a later acceptance with an earlier execution is exactly the stale-row
defect that produced a false verdict on the accepted-sequence leg
(`captures/s45-odleg1a-20261002-163640-od`). An execution row must therefore be **at or after** its acceptance.

Classes and their pairs (the device's own vocabulary, all Tier-A unless noted):

| Class | Acceptance row | Execution row |
|---|---|---|
| Stop overdub | `DIAG,reentry,bound,<tick>,<slot0>,<iv>,<f>` | `DIAG,stop_state,<tr>,<slot0>,6,7,…`, same slot |
| Record / overdub start | `GS,<ctl0>,36,<press>` | `RECA,<slot0>`, or `ODUB,stage,manager_enter` |
| Slot / track switch | `GS,<ctl0>,<slot note>,<press>` | the route row `Loop N: Switch gesture from slot S` |
| Switch handoff (same press) | the same `GS` row | the successor's `DIAG,reentry,admit` |
| Cold undo / redo | `MIDI: Undo (entries=N)` / `MIDI: Redo (entries=N)` | `Undo applied:` / `Redo applied:` |
| Load loop | `RRQ,<track>,<slot>,…,queued,…` | `RRQ,<track>,<slot>,…,terminal,…` |

The declared budgets are Revision 17's; where the design gives a number it is applied and a sample that exceeds it
is reported as an **event** (`exceeded`), where it does not the sample is informational and says so. A reported
exceedance is an observation for the interaction's own owner, never a widened budget: Revision 17's rule is that a
budget is not enlarged to fit a measurement.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, Sequence

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "scripts"))

from hitl.device_time import human_line_device_us  # noqa: E402  (the package path is set just above)

#: The control channel as the `GS` row encodes it (0-based; the wire channel is 1-based 16).
CONTROL_CHANNEL_0BASED = 15
#: The record/overdub button's note and the slot buttons' note base, as the firmware maps them.
RECORD_BUTTON_NOTE = 36
LOOP_SELECT_NOTE_BASE = 50

_CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
_GS_RE = re.compile(r"^GS,(\d+),(\d+),(\d+)$")
_REENTRY_BOUND_RE = re.compile(r"^DIAG,reentry,bound,(\d+),(\d+),(\d+),(\d+)$")
_REENTRY_ADMIT_RE = re.compile(r"^DIAG,reentry,admit,(\d+),(\d+),(\d+),(\d+)$")
_STOP_STATE_RE = re.compile(r"^DIAG,stop_state,(\d+),(\d+),(\d+),(\d+),")
#: The `at_us` field of a `DIAG,stop_state` row: the transition's own device time, not the ring's flush time.
_STOP_STATE_AT_US_RE = re.compile(r"^DIAG,stop_state,\d+,\d+,\d+,\d+,\d+,(\d+),")
_RECA_RE = re.compile(r"^RECA,(\d+),")
_ODUB_ENTER_RE = re.compile(r"^ODUB,stage,manager_enter,")
_RRQ_RE = re.compile(r"^RRQ,(\d+),(\d+),")
_ROUTE_RE = re.compile(r"Loop (\d+): Switch gesture from slot (\d+)")
_MIDI_UNDO_RE = re.compile(r"MIDI: (Undo|Redo) \(entries=(\d+)\)")
_UNDO_APPLIED_RE = re.compile(r"^(?:Undo|Redo) applied:|\[TRACK\] Overdub (?:undone|redone) @")


@dataclass(frozen=True)
class ClassSpec:
    """One interaction class: what the design declares, and the rows that measure it."""

    key: str
    interaction: str
    declared: str
    #: The declared bound in microseconds where Revision 17 gives a number, else None (informational sample).
    declared_bound_us: Optional[int]
    #: Why the bound means what it means -- recorded with the sample so a comparison is never bare.
    bound_note: str
    #: Rows this class was measured from, so `not_exercised` can say what was looked for.
    acceptance_rows: str
    execution_rows: str


CLASSES: tuple[ClassSpec, ...] = (
    ClassSpec(
        key="stop_overdub",
        interaction="Stop overdub",
        declared="microseconds (the same service turn or the next playhead boundary)",
        declared_bound_us=1_000,
        bound_note="`microseconds` is applied as <= 1 ms; the design's own wording is 'the same service turn or "
                   "the next playhead boundary', so a value near a fast boundary is not a violation",
        acceptance_rows="DIAG,reentry,bound,<tick>,<slot0>,<interval>,<flag>",
        execution_rows="DIAG,stop_state,<track>,<slot0>,6,7,… (same slot, at or after the binding)",
    ),
    ClassSpec(
        key="start",
        interaction="Record / overdub start",
        declared="prewarm-bound; must stay under the arm deadline",
        declared_bound_us=None,
        bound_note="Revision 17 declares no numeric arm deadline, so the sample is informational: the measured "
                   "value is reported against the qualitative bound only",
        acceptance_rows="GS,<ctl0>,36,<press> (the record/overdub button press the device read)",
        execution_rows="RECA,<slot0> (record) or ODUB,stage,manager_enter (overdub)",
    ),
    ClassSpec(
        key="switch",
        interaction="Slot / track switch (route recognised)",
        declared="must stay under the window's switch granularity (S4b)",
        declared_bound_us=31_000,
        bound_note="S4b's forward target is 1/16 beat = 31 ms (1/32 beat = 16 ms) at 120 BPM; today's unit is "
                   "one bar = 2,000 ms. This sample is the *capture* switch's route recognition, not the queued "
                   "playback switch S4b tunes, so an exceedance here is an observation for S4b, not its verdict",
        acceptance_rows="GS,<ctl0>,<slot note>,<press>",
        execution_rows="the printed route row `Loop N: Switch gesture from slot S`",
    ),
    ClassSpec(
        key="switch_handoff",
        interaction="Slot / track switch (capture handoff)",
        declared="must stay under the window's switch granularity (S4b)",
        declared_bound_us=31_000,
        bound_note="same S4b target; the handoff additionally contains the predecessor's accepted stop and the "
                   "successor's admission, which is what the accepted-sequence L5 link measures",
        acceptance_rows="GS,<ctl0>,<slot note>,<press> (the switch press)",
        execution_rows="the successor's DIAG,reentry,admit",
    ),
    ClassSpec(
        key="undo_redo",
        interaction="Undo / redo",
        declared="~1-2 s when the root is cold",
        declared_bound_us=2_000_000,
        bound_note="the declared figure is the *cold* root; a warm undo (right after the interaction that created "
                   "the root) is expected far below it, so the sample records which it was",
        acceptance_rows="MIDI: Undo (entries=N) / MIDI: Redo (entries=N) (printed)",
        execution_rows="Undo applied: / Redo applied: (or [TRACK] Overdub undone/redone @ tick)",
    ),
    ClassSpec(
        key="load_loop_dispatch",
        interaction="Load loop (request dispatched)",
        declared="seconds",
        declared_bound_us=None,
        bound_note="`seconds` is qualitative in Revision 17, and this half isolates the device picking the request up "
                   "(`queued` -> `running`) from the completion below",
        acceptance_rows="RRQ,<track>,<slot>,…,queued,…",
        execution_rows="RRQ,<track>,<slot>,…,running,…",
    ),
    ClassSpec(
        key="load_loop",
        interaction="Load loop (completed)",
        declared="seconds",
        declared_bound_us=None,
        bound_note="`seconds` is qualitative in Revision 17, so the sample is informational. **Read the context:** at "
                   "boot the same rows measure the 64-slot restore queue, where a late slot's number is queueing, not "
                   "the load's own cost -- a user-driven single-slot load is the interaction this class names",
        acceptance_rows="RRQ,<track>,<slot>,…,queued,…",
        execution_rows="RRQ,<track>,<slot>,…,terminal,…",
    ),
)


@dataclass(frozen=True)
class Sample:
    """One acceptance -> execution measurement, both ends in device time."""

    class_key: str
    interaction: str
    acceptance_us: int
    execution_us: int
    acceptance_row: str
    execution_row: str
    #: The coarser of the two rows' resolutions: 1 us for `#CAP` rows, 1,000 us for printed rows.
    resolution_us: int

    @property
    def latency_us(self) -> int:
        return self.execution_us - self.acceptance_us


@dataclass
class ClassReport:
    spec: ClassSpec
    samples: list[Sample] = field(default_factory=list)
    #: None when the class is exercised; otherwise the rows that were searched for.
    not_exercised: Optional[str] = None
    #: Reported events: samples that exceed the declared bound. Never a reason to change a budget.
    exceeded: list[Sample] = field(default_factory=list)

    @property
    def worst_us(self) -> Optional[int]:
        return max((s.latency_us for s in self.samples), default=None)
@dataclass
class LatencyReport:
    source: str
    classes: list[ClassReport]

    @property
    def events(self) -> list[str]:
        out: list[str] = []
        for report in self.classes:
            for sample in report.exceeded:
                out.append(f"{report.spec.key}: {sample.latency_us} us exceeds the declared bound "
                           f"({report.spec.declared_bound_us} us) -- {report.spec.bound_note}")
        return out


def _rows(lines: Sequence[str]) -> list[tuple[int, str, int]]:
    """`(device_us, body, resolution_us)` for every row that carries device time, in file order."""
    out: list[tuple[int, str, int]] = []
    for line in lines:
        stripped = line.strip()
        body = stripped
        match = _CAP_RE.match(stripped)
        if match is not None:
            out.append((int(match.group(1)), match.group(2), 1))
            continue
        stamp = human_line_device_us(stripped)
        if stamp is not None:
            # A printed row is `[<s>.<ms>] [LEVEL] body`: both prefixes are stripped so a class matcher anchors on
            # the row's own first token (`Undo applied:`, `Loop N: …`) rather than on the logger's level.
            body = re.sub(r"^\[\d+\.\d+\]\s*", "", stripped)
            body = re.sub(r"^\[[A-Z]+\]\s*", "", body)
            out.append((stamp, body, 1_000))
    return out


def _press_us(row: tuple[int, str, int]) -> bool:
    """Whether a row is a press the device read (`GS,<channel0>,<note>,<press>`)."""
    return _GS_RE.match(row[1]) is not None


def _next_press_us(rows: list[tuple[int, str, int]], acceptance_us: int) -> Optional[int]:
    """The device time of the next press row after `acceptance_us`, or None when the capture has none.

    A press-driven interaction's effect must land **before the next press**: the driver presses sequentially, so a
    later press's effects belong to that press. Without the bound, a boot-time slot press paired with a route row
    emitted tens of seconds later by an unrelated switch (measured: 17.7-34.9 s "switches" in
    `captures/s45-odleg1a-20261002-163640-od`).
    """
    for us, _body, _resolution in rows:
        if us > acceptance_us and _press_us((us, _body, _resolution)):
            return us
    return None


def _first_at_or_after(rows: list[tuple[int, str, int]], acceptance_us: int,
                       predicate, before_us: Optional[int] = None) -> Optional[tuple[int, str, int]]:
    """The **earliest** matching row in device time inside `[acceptance_us, before_us)`, or None.

    Never an earlier row (see the module docstring), and never one outside the pair's own span: a match after the
    next press -- or after the same interval's next lifecycle row -- belongs to the *next* interaction. Rows are
    given in **file** order while the bound is device time, so a row beyond the bound is skipped, not a reason to
    stop scanning: the evidence ring flushes in batches, and stopping there silently lost a legitimate pair
    (measured: the od leg's A/B start samples, whose admission row lands after later flush batches in the file).
    Among the matches, the earliest in device time wins -- file order is not the device's order.
    """
    best: Optional[tuple[int, str, int]] = None
    for us, body, resolution in rows:
        if us < acceptance_us:
            continue
        if before_us is not None and us >= before_us:
            continue
        if not predicate(body):
            continue
        if best is None or us < best[0]:
            best = (us, body, resolution)
    return best



def _route_slot(body: str) -> Optional[int]:
    """The destination slot a switch route body names, or None when the row is not a route at all."""
    match = _ROUTE_RE.search(body)
    return int(match.group(1)) if match is not None else None


def _next_route(rows: list[tuple[int, str, int]], acceptance_us: int) -> Optional[tuple[int, str, int]]:
    """The **next** switch route row the device printed after a press, whatever slot it names.

    A press's effect is the next route the device prints -- `Loop N: Switch gesture from slot S`, where `N` is where
    the press took the capture and `S` where it came from. Taking the next route (rather than the next route that
    happens to name the pressed slot) is what keeps a boot/seed slot press from borrowing a switch that happens tens
    of seconds later: measured, a bounded lookup of "the first route naming slot 4" paired boot presses with the
    leg's own switch (`captures/s45-odleg1a-20261002-163640-od`: 35.4 s and 34.9 s "handoffs").
    """
    return _first_at_or_after(rows, acceptance_us, lambda candidate: _route_slot(candidate) is not None)


def _measure_stop_overdub(rows: list[tuple[int, str, int]]) -> list[Sample]:
    samples: list[Sample] = []
    for us, body, resolution in rows:
        match = _REENTRY_BOUND_RE.match(body)
        if match is None:
            continue
        slot0 = int(match.group(2))

        def same_slot_lifecycle(candidate: str, *, _slot0: int = slot0) -> bool:
            """This interval's own end: the next binding or start admission for the same slot."""
            m = _REENTRY_BOUND_RE.match(candidate) or _REENTRY_ADMIT_RE.match(candidate)
            return m is not None and int(m.group(2)) == _slot0

        def execution(candidate: str, *, _slot0: int = slot0) -> bool:
            m = _STOP_STATE_RE.match(candidate)
            return m is not None and int(m.group(2)) == _slot0 and int(m.group(3)) == 6 and int(m.group(4)) == 7

        # The interval's own span bounds the pair: an execution after the *next* lifecycle row for this slot
        # belongs to that interval, which is the same stale-pairing guard the accepted-sequence oracle needed.
        after = _first_at_or_after(rows, us + 1, same_slot_lifecycle)
        bound = after[0] if after is not None else None
        found = _first_at_or_after(rows, us, execution, before_us=bound)
        if found is None:
            continue
        emitted_us, exec_body, exec_resolution = found
        # `stop_state` carries the **event's own** device time (`at_us`), while the `#CAP` prefix is when the row was
        # emitted from the ring. The latency is measured between event times: on this leg the row's prefix is
        # 169.5 ms after its event (measured: prefix 48,182,318 us against `at_us` 48,012,823 us), so using the prefix
        # would report the flush delay as execution latency.
        event_us = _stop_state_at_us(exec_body)
        if event_us is None or event_us < us or (bound is not None and event_us >= bound):
            continue
        samples.append(Sample("stop_overdub", CLASSES[0].interaction, us, event_us, body, exec_body,
                              max(resolution, exec_resolution)))
    return samples


def _stop_state_at_us(body: str) -> Optional[int]:
    """The device time a `DIAG,stop_state` row carries for the **event itself**, or None when it carries none.

    `DIAG,stop_state,<track>,<slot>,<from>,<to>,<tick>,<at_us>,<start_us>,<end_us>,<duration_us>`: the eighth field
    is when the transition happened; the `#CAP` prefix is only when the row left the ring.
    """
    match = _STOP_STATE_AT_US_RE.match(body)
    return int(match.group(1)) if match is not None else None


def _measure_start(rows: list[tuple[int, str, int]]) -> list[Sample]:
    samples: list[Sample] = []
    for us, body, resolution in rows:
        match = _GS_RE.match(body)
        if match is None or int(match.group(1)) != CONTROL_CHANNEL_0BASED:
            continue
        if int(match.group(2)) != RECORD_BUTTON_NOTE:
            continue
        # The start's execution is whichever of the device's own admissions comes first: `RECA` for a record, the
        # overdub lifecycle's entry, or the interval's start admission (`DIAG,reentry,admit`) when only that row
        # survived the ring -- all three are device-stamped and name the slot the press targeted.
        found = _first_at_or_after(
            rows, us,
            lambda candidate: (_RECA_RE.match(candidate) is not None
                               or _ODUB_ENTER_RE.match(candidate) is not None
                               or _REENTRY_ADMIT_RE.match(candidate) is not None),
            before_us=_next_press_us(rows, us))
        if found is None:
            continue
        exec_us, exec_body, exec_resolution = found
        samples.append(Sample("start", CLASSES[1].interaction, us, exec_us, f"GS,{match.group(1)},{match.group(2)},"
                                                                           f"{match.group(3)}", exec_body,
                              max(resolution, exec_resolution)))
    return samples


def _measure_switch(rows: list[tuple[int, str, int]], *, handoff: bool) -> list[Sample]:
    spec = next(spec for spec in CLASSES if spec.key == ("switch_handoff" if handoff else "switch"))
    samples: list[Sample] = []
    for us, body, resolution in rows:
        match = _GS_RE.match(body)
        if match is None or int(match.group(1)) != CONTROL_CHANNEL_0BASED:
            continue
        note = int(match.group(2))
        if not (LOOP_SELECT_NOTE_BASE <= note <= LOOP_SELECT_NOTE_BASE + 7):
            continue
        # A press is a switch only when the **next** route the device prints names that pressed slot as its
        # destination. A press that stopped something (`Loop N: Stop Overdub`) prints no route, and a boot/seed slot
        # press is separated from the leg's own switch by another press, so neither can borrow a later route.
        route = _next_route(rows, us)
        if route is None or _route_slot(route[1]) != note - LOOP_SELECT_NOTE_BASE + 1:
            continue
        before = _next_press_us(rows, us)
        if before is not None and route[0] >= before:
            continue
        if handoff:
            # The handoff's execution is the successor's admission, which legitimately lands **after** the driver's
            # next press (measured: B's admission 48.649488 s follows the switch press at 48.004262 s and the driver's
            # record press at 48.649163 s). So the route is the soundness gate and no press bound applies to the
            # admit; the pair is bounded instead by the successor interval's own end (its accepted-stop binding),
            # which every admitted interval reaches.
            found = _first_at_or_after(rows, route[0],
                                       lambda candidate: _REENTRY_ADMIT_RE.match(candidate) is not None)
            if found is None:
                continue
            slot_of_admit = int(_REENTRY_ADMIT_RE.match(found[1]).group(3))
            bound = _first_at_or_after(
                rows, found[0] + 1,
                lambda candidate, _slot=slot_of_admit: (
                    (m := _REENTRY_BOUND_RE.match(candidate)) is not None and int(m.group(2)) == _slot))
            if bound is None:
                continue
        else:
            found = route
        if found is None:
            continue
        exec_us, exec_body, exec_resolution = found
        samples.append(Sample(spec.key, spec.interaction, us, exec_us, body, exec_body,
                              max(resolution, exec_resolution)))
    return samples


def _measure_undo_redo(rows: list[tuple[int, str, int]]) -> list[Sample]:
    samples: list[Sample] = []
    for us, body, resolution in rows:
        match = _MIDI_UNDO_RE.search(body)
        if match is None:
            continue
        # The *next* undo/redo request bounds the pair: an application row after it belongs to that request.
        following = _first_at_or_after(rows, us + 1,
                                       lambda candidate: _MIDI_UNDO_RE.search(candidate) is not None)
        found = _first_at_or_after(rows, us, lambda candidate: _UNDO_APPLIED_RE.search(candidate) is not None,
                                   before_us=following[0] if following is not None else None)
        if found is None:
            continue
        exec_us, exec_body, exec_resolution = found
        samples.append(Sample("undo_redo", CLASSES[4].interaction, us, exec_us, body, exec_body,
                              max(resolution, exec_resolution)))
    return samples


def _measure_load_loop(rows: list[tuple[int, str, int]], *, state: str) -> list[Sample]:
    """`queued` -> `state` for each request, bounded by that slot's **next** request."""
    key = "load_loop_dispatch" if state == "running" else "load_loop"
    spec = next(spec for spec in CLASSES if spec.key == key)
    samples: list[Sample] = []
    for us, body, resolution in rows:
        match = _RRQ_RE.match(body)
        if match is None or ",queued," not in body:
            continue
        track, slot = match.group(1), match.group(2)

        def same_target(candidate: str, *, _track: str = track, _slot: str = slot) -> bool:
            m = _RRQ_RE.match(candidate)
            return m is not None and m.group(1) == _track and m.group(2) == _slot

        def execution(candidate: str, *, _track: str = track, _slot: str = slot, _state: str = state) -> bool:
            m = _RRQ_RE.match(candidate)
            return (m is not None and m.group(1) == _track and m.group(2) == _slot
                    and f",{_state}," in candidate)

        # Bound the pair at this target's **next request** (a re-request), not at its own dispatch/completion rows.
        following = _first_at_or_after(
            rows, us + 1,
            lambda candidate, _track=track, _slot=slot: (
                same_target(candidate) and ",queued," in candidate))
        found = _first_at_or_after(rows, us, execution, before_us=following[0] if following is not None else None)
        if found is None:
            continue
        exec_us, exec_body, exec_resolution = found
        samples.append(Sample(key, spec.interaction, us, exec_us, body, exec_body,
                              max(resolution, exec_resolution)))
    return samples


_MEASURERS = {
    "stop_overdub": lambda rows: _measure_stop_overdub(rows),
    "start": lambda rows: _measure_start(rows),
    "switch": lambda rows: _measure_switch(rows, handoff=False),
    "switch_handoff": lambda rows: _measure_switch(rows, handoff=True),
    "undo_redo": lambda rows: _measure_undo_redo(rows),
    "load_loop_dispatch": lambda rows: _measure_load_loop(rows, state="running"),
    "load_loop": lambda rows: _measure_load_loop(rows, state="terminal"),
}


def measure(text: str, *, source: str = "<text>") -> LatencyReport:
    """The per-class matrix for one capture: every sample from device rows, every gap named."""
    rows = _rows(text.splitlines())
    classes: list[ClassReport] = []
    for spec in CLASSES:
        samples = _MEASURERS[spec.key](rows)
        report = ClassReport(spec=spec, samples=samples)
        if not samples:
            report.not_exercised = (f"no acceptance row matched ({spec.acceptance_rows}) with an execution row at "
                                    f"or after it ({spec.execution_rows})")
        elif spec.declared_bound_us is not None:
            report.exceeded = [s for s in samples if s.latency_us > spec.declared_bound_us]
        classes.append(report)
    return LatencyReport(source=source, classes=classes)


def as_dict(report: LatencyReport) -> dict:
    return {
        "source": report.source,
        "owner": "interaction_latency",
        "declared": "design Revision 17 — per-action execution budgets (measurement replaces the numbers)",
        "classes": [
            {
                "key": cls.spec.key,
                "interaction": cls.spec.interaction,
                "declared": cls.spec.declared,
                "declared_bound_us": cls.spec.declared_bound_us,
                "bound_note": cls.spec.bound_note,
                "not_exercised": cls.not_exercised,
                "worst_us": cls.worst_us,
                "exceeded_count": len(cls.exceeded),
                "samples": [
                    {
                        "acceptance_us": s.acceptance_us,
                        "execution_us": s.execution_us,
                        "latency_us": s.latency_us,
                        "resolution_us": s.resolution_us,
                        "acceptance_row": s.acceptance_row,
                        "execution_row": s.execution_row,
                        "exceeds_declared": s in cls.exceeded,
                    }
                    for s in cls.samples
                ],
            }
            for cls in report.classes
        ],
        "events": report.events,
    }
