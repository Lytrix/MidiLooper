#!/usr/bin/env python3
"""S4f.13 oracle — read an accepted-sequence leg's session log and report whether it is the required chain.

The requirement (DEC-078 / invariant 31) is that a stop is **accepted on one press** and executed by the
pipeline, so the successor can start while the predecessor still finalizes and no user ever presses twice.
The leg is therefore proven only as an **ordered chain**, each link on its own evidence (the names are the
check names this oracle emits):

    L1  A's stop was accepted and bound to its interval exactly once (`L1_a_stop_accepted_once`)
    L2  no capacity denial: `successor_stop_denied_capacity` and `reentryDecision("deny", …)` are absent
    L3  A's finalization window opened (the predecessor is finalizing: `L3_a_finalization_window`)
    L4  A's finalization capacity was released (`L4_a_capacity_released`)
    L5  B started while A still finalized -- the acceptance, not a queue that never ran
        (`L5_successor_started`)
    L6  B's stop executed exactly once (`L6_b_stop_executes_once`)
    L7  one press per stop (`L7_one_press_per_stop`)

**Which rows each link reads depends on the leg, and that is the whole point of the identity work** (OpenSpec
`response-driven-hitl-acceptance` task 2.2):

  * **Record leg** (`--leg rec`): the `RECA`/`RECS` family, which names its slot. One `RECS` stop row for A is
    the accepted stop; one for B is B's execution.
  * **Overdub leg** (`--leg od`): A and B are *seeded by a record* first, so their `RECA`/`RECS` rows are the
    **seeds'** stops and must never stand in for the leg's own overdub stops. The links come from the
    interval's own identity rows instead: the start admission (`DIAG,reentry,admit`), the accepted-stop
    binding edge (`DIAG,reentry,bound`), the execution (`DIAG,stop_state` 6→7 entry and 7→5|1 terminal), the
    retirement (`DIAG,canonical_release`), and the button action's own `Loop N:` **request** rows for L7. The
    od leg also reports `overdub_interval_identity`: `ok` only when each interval is identified by its own
    start, accepted stop and retirement rows, with A's and B's identities distinct.
  * **Absence is still not a pass, but on the overdub leg it is a *gap*, not a violation**: those families are
    only as complete as the capture (a cut excerpt, an evicted Tier-A row), so a missing overdub row reads as
    `unknown` (fix the telemetry). A *contradiction* -- a second acceptance, a second execution, a binding that
    names another interval, two intervals claiming one identity -- stays a `FAIL`.

A missing or reordered link is never a pass. Verdicts and exit codes keep "not this shape" apart from "this run
is not evidence":

  * `SETUP_INCOMPLETE` (3): freshness/identity/staging -- fix the run. Staging includes the *shape* links: if
    A's finalization window or its release never appeared, the acceptance was not exercised
    (`captures/s4f22-rec-rec-01` and `.../s4f13-od4-20260927_020114` are NOT_EXERCISED runs, not a firmware
    verdict) -- and on the overdub leg a successor that began *after* A released is the same class, because
    S4f.22's own device-leg text says "missing overlap is NOT_EXERCISED".
  * `INSUFFICIENT_EVIDENCE` (4): staged, but a link has no supporting telemetry -- fix the telemetry
  * `ACCEPTED_SEQUENCE_FAIL` (2): the chain is violated
  * `ACCEPTED_SEQUENCE_PASS` (0): every link on its own evidence

Base convention: the firmware's slot fields are **0-based** (`RECA`/`RECS`/`reentry`), the human `Loop N:`
rows are 1-based, and this oracle's `--slot-a/--slot-b` are 1-based like the driver's CLI. The conversion
happens in exactly one place, because comparing across the bases has already produced a wrong reading twice.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

ROOT = Path(__file__).resolve().parents[1]
#  Running this file directly puts its *own* directory (`scripts/hitl`) on `sys.path[0]`, where it **shadows
#  pyserial**: `scripts/hitl/serial/` then answers `import serial`, which is how the serial-stack modules reachable
#  from a verifier fail to import in the CLI that the tests never exercise (the tests import this module). Dropping
#  the entry is safe -- every import below is a `hitl.…` package path, resolved through `scripts`.
for _entry in (str(Path(__file__).resolve().parent),):
    if _entry in sys.path:
        sys.path.remove(_entry)
sys.path.insert(0, str(ROOT / "scripts"))

from hitl import acceptance_evidence  # noqa: E402
from hitl.reporting import build_report_bundle, write_report_bundle  # noqa: E402

OK, FAIL, UNKNOWN = "ok", "fail", "unknown"

CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
# Exactly the successful stop kinds the firmware emits, not an open `stop*` pattern.
RECS_STOP_RE = re.compile(r"^RECS,(?:stop|stopToStopped),(\d+),")
RECA_RE = re.compile(r"^RECA,(\d+),")
WINDOW_OPEN_RE = re.compile(r"^DIAG,reentry_window_open,(\d+),")
CANONICAL_RELEASE_RE = re.compile(r"^DIAG,canonical_release,(\d+),")
REENTRY_RE = re.compile(r"^DIAG,reentry,([a-z_]+),")
# The **overdub** leg's own identity chain (`response-driven-hitl-acceptance` task 2.2). `RECA`/`RECS` above
# describe the *record* lifecycle; an overdub interval's start, accepted stop, execution and retirement are
# emitted by different owners, each naming the interval identity a correlation needs:
#   start      `DIAG,reentry,admit,<tick>,<interval>,<slot>,<depth>`      the start admission
#   accepted   `DIAG,reentry,bound,<tick>,<interval>,<slot>,<depth>`      the S4f.3 binding edge ("so a denied
#              request and its later binding can be correlated instead of inferred", its own comment)
#   executed   `DIAG,stop_state,<track>,<slot>,6,7,...` and `...,7,5|1,...`  the stop entry and terminal
#   retired    `DIAG,canonical_release,<slot>,<generation>,<len>,<chunks>`   (`CANONICAL_RELEASE_RE`)
# All four families are Tier-A, so an evicted row is already the `overflow_free` setup gate rather than a
# silent gap. Per-interval stop *requests* are the button action's own `Loop N:` rows (1-based, like the other
# human rows).
ODUB_START_RE = re.compile(r"^DIAG,reentry,admit,(\d+),(\d+),(\d+),(\d+)$")
ODUB_BOUND_RE = re.compile(r"^DIAG,reentry,bound,(\d+),(\d+),(\d+),(\d+)$")
STOP_STATE_RE = re.compile(r"^DIAG,stop_state,(\d+),(\d+),(\d+),(\d+),")
HUMAN_STOP_OVERDUB_RE = re.compile(r"Loop (\d+): Stop Overdub")
HUMAN_SWITCH_SOURCE_RE = re.compile(r"Loop \d+: Switch gesture from slot (\d+)")
#  A clock-source transition, as the sole clock owner emits it (`DebugSessionCapture::clockSource`): one
#  `#CAP` one-shot naming the source it moved from and to (`INT` when the device runs its own clock, `EXT`
#  when it is slaved to the host's). It is ordinary (non-Tier-A) ring text, which is why the predicate below
#  treats a shed capture as UNKNOWN rather than reading absence as stability.
CAP_CS_RE = re.compile(r"^#CAP,\d+,CS,[A-Z]+,[A-Z]+\s*$")
#  The leg's own *stop* requests, for the fallback window when no run marks are supplied. Deliberately no
#  `Start Recording` row: the seeds arm and record too, so that row cannot separate the leg from preparation.
HUMAN_STOP_RECORDING_RE = re.compile(r"Loop \d+: Stop Recording")
# The per-family counters, as the stopped-only read-back reports them: `RING,record_stats,<family>,entered=…,
# enq=…,rej=…,tx=…,evict=…`. A family tag may itself carry a comma (`DIAG,latch`), so the name is matched as
# one or two identifier parts, never as "everything up to the next comma". `enq` counts appends that reached
# the ring, `rej` refused appends, `evict` displaced rows; `entered == enq + rej` is the emitter's own
# arithmetic, and an inconsistency is a finding.
FAMILY_STATS_RE = re.compile(r"^RING,record_stats,(?P<family>[A-Za-z_]+(?:,[A-Za-z_]+)?),"
                             r"entered=(?P<entered>\d+),enq=(?P<enq>\d+),rej=(?P<rej>\d+),"
                             r"tx=(?P<tx>\d+),evict=(?P<evict>\d+)$")
#  The firmware's own record-start vocabulary and its wrong-route rows, for the record leg's B start route
#  (task 4.2). Kept in step with `hitl.flows.accepted_sequence`'s `WRONG_MODE_ROUTE_MARKERS` by name and by
#  intent: `Playback started` is deliberately absent, because `PendingSwitchGate` owns it as the marker that a
#  switch *landed* -- so a predecessor's loop resuming after a legitimate switch is not a wrong route.
WRONG_MODE_ROUTE_ROWS = ("Queued playback switch", "Reaffirmed playback switch",
                         "Live Overdub", "Overdubbing started", "Toggle Play/Stop",
                         "long-press start (not playing)")
RECORD_START_CARRIED_ROWS = ("Record start carried on the switch", "switch and record start retained")
DENIAL_STAGE_RE = re.compile(r"^ODUB,stop,admission,.*,successor_stop_denied_capacity$")
HUMAN_DENIAL_RE = re.compile(r"Overdub stop refused")
HUMAN_WINDOW_RE = re.compile(r"Reentry window open slot=(\d+)")
HUMAN_RETAINED_RE = re.compile(r"switch and record start retained")
HUMAN_START_RE = re.compile(r"Recording started @ tick")
# Session-invalidating ring rows. `overflow` / `critical_rejected` are critical-record losses and `wiped`
# is an unreadable-head reset, so the capture cannot support a sequence conclusion --
# `scripts/hitl/serial_collector.py` marks the same three `CaptureIntegrity.INCOMPLETE`. `tier_a_evicted`
# stays a setup failure because this oracle's gate is pinned to it (see `test_accepted_sequence.py`).
# Deliberate timing **shedding is not a loss**: `DebugSessionCapture` drops only non-Tier-A diagnostics in
# the playback flush instead of blocking the transport on USB I/O (its own comment: "reported as either
# kind of loss" applies to `overflow`/`tier_a_evicted`, not to shed), and the guide says "Ordinary
# `RING,shed` remains separate". Counting it as one made **every** fresh-boot run `SETUP_INCOMPLETE` --
# recorded as an over-strict gate in the S4f.17 packet and `tasks.md` (2026-09-27) and again for
# `captures/s4f22-dec096-od-01` (2026-09-30).
RING_LOSS_RE = re.compile(r"RING,(?:tier_a_evicted|overflow|critical_rejected|wiped)")
RING_SHED_RE = re.compile(r"RING,shed")
IDENTITY_RE = re.compile(r"git_commit|firmware_hex_sha256")


@dataclass(frozen=True)
class Entry:
    us: int
    body: str


@dataclass
class Check:
    name: str
    status: str
    detail: str
    kind: str = "retention"
    link: str = ""


@dataclass
class Analysis:
    verdict: str
    checks: list[Check] = field(default_factory=list)
    evidence: dict[str, list[str]] = field(default_factory=dict)
    limitations: list[str] = field(default_factory=list)

    @property
    def exit_code(self) -> int:
        return {"ACCEPTED_SEQUENCE_PASS": 0, "ACCEPTED_SEQUENCE_FAIL": 2,
                "SETUP_INCOMPLETE": 3, "INSUFFICIENT_EVIDENCE": 4}[self.verdict]


def parse_entries(text: str) -> list[Entry]:
    out: list[Entry] = []
    for line in text.splitlines():
        m = CAP_RE.match(line.strip())
        if m:
            out.append(Entry(int(m.group(1)), m.group(2)))
    return out


def _overdub_identity(entries: list[Entry], slot: int) -> dict:
    """One overdub interval's own start / accepted-stop / execution / retirement rows, by firmware slot.

    `interval` is `captureInterval_` as the emitting owner stamped it, so two rows belong to one interval only
    when they agree on it -- that is what keeps another slot's rows from substituting for this interval's.
    """
    starts = [(e.us, int(m.group(2)), e.body) for e in entries
              if (m := ODUB_START_RE.match(e.body)) and int(m.group(3)) == slot]
    bindings = [(e.us, int(m.group(2)), e.body) for e in entries
                if (m := ODUB_BOUND_RE.match(e.body)) and int(m.group(3)) == slot]
    stop_rows = [(e.us, int(m.group(3)), int(m.group(4)), e.body) for e in entries
                 if (m := STOP_STATE_RE.match(e.body)) and int(m.group(2)) == slot]
    intervals = {iv for _, iv, _ in starts} | {iv for _, iv, _ in bindings}
    return {
        "starts": starts,
        "bindings": bindings,
        "stop_entries": [r for r in stop_rows if r[1] == 6 and r[2] == 7],
        "stop_terminals": [r for r in stop_rows if r[1] == 7 and r[2] in (5, 1)],
        "releases": [(e.us, e.body) for e in entries
                     if (m := CANONICAL_RELEASE_RE.match(e.body)) and int(m.group(1)) == slot],
        "interval": next(iter(intervals)) if len(intervals) == 1 else None,
    }


def _overdub_stop_requests(lines: list[str], slots_1based: tuple[int, ...]) -> dict[int, int]:
    """Per-interval stop *requests* from the device's own `Loop N:` rows (1-based slot numbers).

    `Loop N: Stop Overdub` is the press that asked for N's stop. `Loop T: Switch gesture from slot S` is a press
    that stops S -- in this leg's shape the capture-switch *is* A's stop -- so the request is counted against the
    slot that was stopped, not against the button that was pressed.
    """
    counts = {s: 0 for s in slots_1based}
    for line in lines:
        if (m := HUMAN_STOP_OVERDUB_RE.search(line)) and int(m.group(1)) in counts:
            counts[int(m.group(1))] += 1
        elif (m := HUMAN_SWITCH_SOURCE_RE.search(line)) and int(m.group(1)) in counts:
            counts[int(m.group(1))] += 1
    return counts


def analyse(text: str, *, leg: str, slot_a: int, slot_b: int,
            metadata: Optional[dict] = None, base_report: Optional[dict] = None,
            window: Optional[tuple[int, int]] = None, inputs: Optional[dict] = None) -> Analysis:
    # The **mixed** leg can be *driven* (its plan and preparation are in `hitl.flows.accepted_sequence`), but its
    # *grading* is not implemented yet: A is a record interval and B an overdub interval, so its links need A's
    # record vocabulary (`RECS`, the finalization window) for one side and the overdub families
    # (`DIAG,reentry,admit|bound`, `stop_state`, `canonical_release`) for the other. Grading it with either
    # existing mode would fail a conforming run, so this refuses by name instead -- the same "refuse rather than
    # invent a verdict" rule the rest of the oracle follows.
    if leg == "mixed":
        return Analysis("SETUP_INCOMPLETE", [Check(
            "mixed_leg_grading", FAIL,
            "the mixed leg's grading is not implemented: A is a record interval and B an overdub interval, so its "
            "links mix the record vocabulary (A's `RECS` stop and finalization window) with the overdub families "
            "(B's `DIAG,reentry,admit|bound`, `stop_state`, `canonical_release`). The flow drives the leg; this "
            "verdict says so rather than grading it with the wrong mode's vocabulary.", "setup")], {}, [])
    checks: list[Check] = []
    evidence: dict[str, list[str]] = {"stop_requests": [], "window_open": [], "releases": [],
                                      "successor_start": [], "denials": []}
    limitations: list[str] = []

    def add(name, status, detail, kind="retention", link=None):
        checks.append(Check(name, status, detail, kind, link))

    lines = text.splitlines()
    entries = parse_entries(text)
    # 1-based (driver CLI) -> 0-based (firmware rows). The one conversion site.
    a0, b0 = slot_a - 1, slot_b - 1

    # ---------------- setup: is this run usable evidence? ----------------
    if not entries:
        add("telemetry_present", FAIL, "no #CAP records in the log", "setup")
        return Analysis("SETUP_INCOMPLETE", checks, evidence, limitations)
    add("telemetry_present", OK, f"{len(entries)} #CAP records", "setup")

    fresh = any("BOOT,usb" in l for l in lines[:80])
    add("fresh_boot", OK if fresh else FAIL,
        "boot banner in the first 80 lines" if fresh else
        "no boot banner: the device did not reset, so this session inherits the ring and clock of an earlier "
        "one and is not verdict-grade", "setup")

    if metadata:
        pinned = bool(metadata.get("git_commit")) and bool(metadata.get("firmware_hex_sha256"))
        add("identity_pinned", OK if pinned else FAIL,
            f"git_commit={metadata.get('git_commit')} firmware_hex_sha256={metadata.get('firmware_hex_sha256')}",
            "setup")
    else:
        add("identity_pinned", UNKNOWN, "no metadata.json supplied, so the build identity is unrecorded",
            "evidence")

    if base_report is not None:
        integrity = str(base_report.get("capture_integrity", "")).upper()
        complete = bool(base_report.get("capture_complete"))
        add("capture_integrity", OK if integrity == "COMPLETE" else FAIL,
            f"base report capture_integrity={integrity}", "setup")
        add("observation_tail_complete", OK if complete else FAIL,
            f"base report capture_complete={complete}", "setup")
    ring_hits = [l.strip()[:120] for l in lines if RING_LOSS_RE.search(l)]
    add("overflow_free", OK if not ring_hits else FAIL,
        "no capture-ring eviction or critical loss" if not ring_hits else
        f"{len(ring_hits)} ring-loss row(s), first: {ring_hits[0]}", "setup")
    evidence["ring_loss"] = ring_hits

    # ---------------- family accounting: a counted loss is a verdict input ----------------
    # Task 2.2's "required event loss is detectable", turned into a gate: the firmware counts every family it
    # relies on (`RING,record_stats,<family>,entered=…,enq=…,rej=…,tx=…,evict=…`) and the stopped-only
    # read-back reports those counters. A family that *refused* an append or had a row *displaced* inside this
    # capture lost evidence, and a chain cannot be concluded from a capture that lost the rows it counts.
    #
    # Scoping: the counters are per boot, so the comparison is against this capture's **own first** read-back
    # (the two snapshots a managed run takes), never against zero -- loss that happened before the capture is
    # outside its window, and failing a leg for it would invent a verdict. A capture with **no** read-back row
    # adds no check: the read-back is the driver's obligation (the managed retrieval asks for it, and the
    # accepted-sequence driver does too), and requiring it here would invalidate captures that never asked.
    family_rows: dict[str, list[dict[str, int]]] = {}
    for e in entries:
        m = FAMILY_STATS_RE.match(e.body)
        if m:
            family_rows.setdefault(m.group("family"), []).append(
                {key: int(m.group(key)) for key in ("entered", "enq", "rej", "tx", "evict")})
    if family_rows:
        problems: list[str] = []
        attached: list[str] = []
        for family, rows in sorted(family_rows.items()):
            first, last = rows[0], rows[-1]
            attached.append(f"{family}: {last}" + ("" if len(rows) == 1 else f" (from {first})"))
            if last["entered"] != last["enq"] + last["rej"]:
                problems.append(f"{family}: entered {last['entered']} != enqueued {last['enq']} + "
                                f"rejected {last['rej']}")
            if last["rej"] > first["rej"]:
                problems.append(f"{family}: {last['rej'] - first['rej']} refused append(s) during the capture")
            if last["evict"] > first["evict"]:
                problems.append(f"{family}: {last['evict'] - first['evict']} displaced row(s) during the capture")
        evidence["family_stats"] = attached
        if problems:
            add("family_accounting", FAIL,
                "this capture's own family counters report loss: " + "; ".join(problems)
                + " -- a counted edge cannot be used as evidence once its family lost rows", "setup")
        else:
            add("family_accounting", OK,
                f"{len(family_rows)} counted famil(ies) with no refusal or displacement in this capture: "
                + ", ".join(sorted(family_rows)), "setup")

    # ---------------- the conjunction's *external* halves: content, timing, teardown (task 5.2) ----------------
    # Design decision 6: "Require matched input → recognition → accepted obligation → execution →
    # publication/retirement, continuous transport, **content parity and the existing timing gates**", and
    # "Shutdown result remains an independent field and can veto overall success". Each of the three is a check
    # that can veto an otherwise complete acceptance, and each is read from the owner that already decides it --
    # `hitl.acceptance_evidence` does the reading, this block only grades it:
    #
    #   * a verifier that *failed* is a FAIL of the conjunction's own kind (`content`/`timing` ⇒
    #     ACCEPTED_SEQUENCE_FAIL);
    #   * a teardown that did not complete is a **setup** failure: a run that did not drain is not verdict-grade,
    #     which is how the sealed bundle's own rule already reads it (`hitl.reporting.derive_verdict`);
    #   * **not supplied** is UNKNOWN, never a pass -- "PASS only for the complete conjunction" -- so a capture
    #     that carries no content or timing evidence can be graded, but not passed.
    supplied = dict(inputs or {})
    for check_name, kind, (ok, detail) in (
            ("content_parity", "content",
             acceptance_evidence.content_parity(supplied.get("content_parity") or supplied.get("content"))),
            ("timing_gates", "timing",
             acceptance_evidence.timing_gates(supplied.get("timing") or supplied.get("timing_gates"))),
            ("teardown", "setup",
             acceptance_evidence.teardown(supplied.get("teardown") or supplied.get("shutdown"))),
    ):
        status = UNKNOWN if ok is None else (OK if ok else FAIL)
        add(check_name, status, detail, kind)
        evidence[check_name] = [detail]

    # Timing shedding is reported, never fatal: the policy discards non-Tier-A diagnostics so the playback
    # turn never blocks on USB I/O, and it is distinguishable from "nothing was dropped at all" only by
    # name. Reported as `ok` with the rows attached, so a reader still sees which windows shed.
    shed_hits = [l.strip()[:120] for l in lines if RING_SHED_RE.search(l)]
    add("ring_shed_diagnostics_only", OK,
        "no timing shed" if not shed_hits else
        f"{len(shed_hits)} timing-shed row(s) -- non-Tier-A diagnostics discarded by policy, not a ring "
        f"loss; first: {shed_hits[0]}", "setup")
    evidence["ring_shed"] = shed_hits

    # The DEC-070 bounded retarget fallback (`companion path stalled; raw pass only`) is **not** a link of this
    # oracle: its closure condition belongs to `successor-capture-reentry-unguarding` task 5.11 / gate 6.2. It is
    # reported anyway, because a PASS here is a statement about this chain's links and must never be read as "the
    # successor path ran without its fallback".
    fallback_rows = [l.strip()[:140] for l in lines
                     if "companion path stalled" in l or "raw pass only" in l]
    evidence["dec070_retarget_fallback"] = fallback_rows

    # ---------------- L1/L6: the accepted stop and its execution ----------------
    # The record leg's evidence is a `RECS` stop row for that slot. On the **overdub** leg A and B are seeded by
    # a *record* first, so their `RECS` rows are the seeds' stops: reading them as the leg's own overdub stops
    # is the false acceptance the corpus fixture pins. The overdub interval's identity chain is used instead.
    od_a = od_b = None
    if leg == "od":
        od_a, od_b = _overdub_identity(entries, a0), _overdub_identity(entries, b0)
    stops_a = ([] if leg == "od" else
               [(e.us, e.body) for e in entries
                if (m := RECS_STOP_RE.match(e.body)) and int(m.group(1)) == a0])
    stops_b = ([] if leg == "od" else
               [(e.us, e.body) for e in entries
                if (m := RECS_STOP_RE.match(e.body)) and int(m.group(1)) == b0])
    for us, body in stops_a + stops_b:
        evidence["stop_requests"].append(f"{us}: {body}")

    if leg == "od":
        # L1 -- the acceptance. `DIAG,reentry,bound` is the edge at which the accepted stop is bound to the
        # interval the request named (the S4f.3 binding edge), so one binding is one accepted overdub stop. An
        # absent family reads as an evidence gap -- the overdub rows are only as complete as the capture, and a
        # cut excerpt or an evicted Tier-A row must not be reported as a chain violation -- while a duplicate or
        # a conflicting identity is a real finding.
        ids_a = {iv for _, iv, _ in od_a["starts"]} | {iv for _, iv, _ in od_a["bindings"]}
        if len(ids_a) > 1:
            add("L1_a_stop_accepted_once", UNKNOWN,
                f"A's overdub rows carry conflicting interval identities ({sorted(ids_a)}), so the acceptance "
                "cannot be attributed to one interval", "evidence", "L1")
        elif not od_a["bindings"]:
            add("L1_a_stop_accepted_once", UNKNOWN,
                f"no `DIAG,reentry,bound` row for A (firmware slot {a0}): the capture does not show A's overdub "
                "stop being accepted and bound to its interval", "evidence", "L1")
        else:
            add("L1_a_stop_accepted_once", OK if len(od_a["bindings"]) == 1 else FAIL,
                f"{len(od_a['bindings'])} accepted-stop binding(s) for A at interval {od_a['interval']}: "
                f"{od_a['bindings'][0][2]}" + ("" if len(od_a["bindings"]) == 1 else
                                               " -- more than one means a stop was accepted twice"),
                "retention", "L1")
        # L6 -- the execution. The terminal stop-state edge is the execution itself; without it, the retirement
        # edge is the next witness that the stop ran.
        executions = ([(us, body) for us, _from, _to, body in od_b["stop_terminals"]]
                      or od_b["releases"])
        if len(od_b["stop_terminals"]) > 1 or len(od_b["releases"]) > 1:
            add("L6_b_stop_executes_once", FAIL,
                f"{len(od_b['stop_terminals'])} terminal edge(s) and {len(od_b['releases'])} retirement(s) for B "
                f"(firmware slot {b0}): B's stop executed more than once", "retention", "L6")
        elif not executions:
            add("L6_b_stop_executes_once", UNKNOWN,
                f"no `DIAG,stop_state` terminal edge and no `DIAG,canonical_release` row for B (firmware slot "
                f"{b0}): the capture does not show B's overdub stop executing", "evidence", "L6")
        else:
            add("L6_b_stop_executes_once", OK,
                f"one stop execution for B: {executions[0][1]}", "retention", "L6")
    elif not stops_a:
        add("L1_a_stop_accepted_once", FAIL,
            f"no successful stop record for A (firmware slot {a0}): A's stop never executed", "retention", "L1")
    else:
        add("L1_a_stop_accepted_once", OK if len(stops_a) == 1 else FAIL,
            f"{len(stops_a)} stop record(s) for A: {stops_a[0][1]}" +
            ("" if len(stops_a) == 1 else " -- more than one means a stop was executed twice"),
            "retention", "L1")

    if leg != "od":
        if not stops_b:
            add("L6_b_stop_executes_once", UNKNOWN,
                f"no successful stop record for B (firmware slot {b0}); in the overdub leg B's stop is the one the "
                "DEC-078 shape bounds, so its absence is the link to close", "evidence", "L6")
        else:
            add("L6_b_stop_executes_once", OK if len(stops_b) == 1 else FAIL,
                f"{len(stops_b)} stop record(s) for B: {stops_b[0][1]}", "retention", "L6")

    # ---------------- L2: no capacity denial anywhere ----------------
    denials = [l.strip()[:140] for l in lines if HUMAN_DENIAL_RE.search(l)]
    stage_denials = [e.body for e in entries if DENIAL_STAGE_RE.match(e.body)]
    deny_decisions = [(e.us, e.body) for e in entries
                      if (m := REENTRY_RE.match(e.body)) and m.group(1) in ("deny", "deny_resource")]
    evidence["denials"] = denials + stage_denials + [f"{u}: {b}" for u, b in deny_decisions]
    if denials or stage_denials or deny_decisions:
        add("L2_no_capacity_denial", FAIL,
            f"{len(denials)} human denial row(s), {len(stage_denials)} stop-admission denial(s), "
            f"{len(deny_decisions)} reentry denial(s): the sequence hit the capacity-denied state, so the "
            "successor was NOT accepted unconditionally", "retention", "L2")
    else:
        add("L2_no_capacity_denial", OK,
            "no `successor_stop_denied_capacity`, no `reentryDecision(deny)`, no `Overdub stop refused` row",
            "retention", "L2")

    # ---------------- L3: A's finalization window ----------------
    windows = [(e.us, e.body) for e in entries
               if (m := WINDOW_OPEN_RE.match(e.body)) and int(m.group(1)) == a0]
    human_windows = [(i, l.strip()) for i, l in enumerate(lines)
                     if (m := HUMAN_WINDOW_RE.search(l)) and int(m.group(1)) == a0]
    for us, body in windows:
        evidence["window_open"].append(f"{us}: {body}")
    if windows:
        add("L3_a_finalization_window", OK, f"{windows[0][1]} at {windows[0][0]} us", "retention", "L3")
    elif human_windows:
        add("L3_a_finalization_window", OK,
            f"timely human row only: {human_windows[0][1][:120]}", "retention", "L3")
    else:
        # **Not staged, not violated.** The row's own reading ("this sequence did not exercise the
        # acceptance") is a staging outcome, so it belongs to the "fix the run" class: a run whose
        # stop never froze the predecessor has not put the acceptance on trial, and calling that a
        # chain violation would invent a firmware verdict (`captures/s4f13-od4-20260927_020114` ended
        # mid-sequence after a queued switch, and both the S4f.17 packet and `tasks.md` attribute it
        # to the leg plan).
        #
        # The **record** leg is held to the same shape, and this was previously read the other way round
        # ("its production stop publishes inline, so no window is the expected shape there"). That is no longer
        # true of this firmware: `finalizeCaptureAndSelectSlot`'s record branch admits or retains a Record stop
        # "exactly like an overdub stop", and `retainRecordStopGeometryObligation` is the same deferred
        # obligation -- so the spec's record scenario ("its single stop acceptance during A's ownership window")
        # asks for exactly this window, and its absence is the same staging outcome rather than a device defect.
        add("L3_a_finalization_window", FAIL,
            f"no `reentry_window_open` and no `Reentry window open slot={a0}` row: A never froze into a "
            "deferred finalization, so this sequence did not exercise the acceptance", "setup", "L3")

    releases = [(e.us, e.body) for e in entries
                if (m := CANONICAL_RELEASE_RE.match(e.body)) and int(m.group(1)) == a0]
    for us, body in releases:
        evidence["releases"].append(f"{us}: {body}")
    human_releases = [l for l in lines if re.search(rf"Reentry released slot={a0}(?:\s|$)", l)]
    l4_released = bool(releases) or bool(human_releases)
    # A release missing next to a missing window is the same staging outcome, not a second violation
    # (`test_window_open_is_not_release` still fails L4 when the window is present without a release).
    add("L4_a_capacity_released", OK if l4_released else FAIL,
        (releases[0][1] + f" at {releases[0][0]} us") if releases
        else ("A retired (human edge only)" if human_releases else
              "no release edge for A: it never retired, so B's stop could not be bounded by it"),
        "retention" if l4_released else "setup", "L4")

    # ---------------- L5: the successor started while A finalized ----------------
    # A *successor* start is one that follows A's stop; a start before it is the seed that set the leg up, and
    # counting it would let a leg with no successor at all still satisfy L5.
    if leg == "od":
        # The overdub start's identity row is the start admission (`DIAG,reentry,admit`), and the stop it
        # follows is the *binding* edge -- not a `RECS` row, which on this leg belongs to the record seed.
        a_stop_us = od_a["bindings"][0][0] if od_a["bindings"] else None
        all_starts = [(us, body, iv) for us, iv, body in od_b["starts"]]
    else:
        a_stop_us = stops_a[0][0] if stops_a else None
        all_starts = [(e.us, e.body, None) for e in entries
                      if (m := RECA_RE.match(e.body)) and int(m.group(1)) == b0]
    starts = [row for row in all_starts if a_stop_us is None or row[0] > a_stop_us]
    for us, body, iv in all_starts:
        evidence["successor_start"].append(
            f"{us}: {body}" + ("" if (us, body, iv) in starts else "  (before A's stop: the seed)"))
    release_us = releases[0][0] if releases else (
        # Fall back to the human window's line position, which is all a human row can offer.
        None)
    if not starts:
        if leg == "od" and (not all_starts or a_stop_us is None):
            # Absence is a gap to fix, not a violation: the overdub families are only as complete as the
            # capture (a cut excerpt, or an evicted Tier-A row), and A's own acceptance edge is the floor the
            # ordering would be measured from.
            add("L5_successor_started", UNKNOWN,
                f"no overdub successor start for B (firmware slot {b0}): no `DIAG,reentry,admit` row follows "
                f"A's stop" + ("; A's accepted-stop binding edge is missing as well, so the ordering has no "
                               "floor" if a_stop_us is None else ""), "evidence", "L5")
        else:
            add("L5_successor_started", FAIL,
                "no start follows A's stop: nothing succeeded the predecessor, so the acceptance was not exercised"
                + ("" if a_stop_us is None else f" (A's stop was at {a_stop_us} us)"), "retention", "L5")
    else:
        after_release = [row[0] for row in starts if release_us is not None and row[0] > release_us]
        overlap = (a_stop_us is not None and release_us is not None
                   and any(a_stop_us < us < release_us for us, _b, _iv in starts))
        if leg == "od" and not overlap and a_stop_us is not None and release_us is not None:
            # This leg's own rule (`tasks.md` S4f.22, the device leg's text): "Missing overlap is
            # NOT_EXERCISED." A successor that begins after the predecessor released did not put the
            # acceptance on trial -- the run has to be arranged so it overlaps -- so it is a staging outcome
            # to fix in the run, never a chain violation to attribute to the firmware. The record leg keeps
            # its pinned FAIL: there the successor start is its own explicit press and the existing suite
            # pins that reading.
            add("L5_successor_started", FAIL,
                f"{len(starts)} start(s), all after A's release edge ({release_us} us): the successor began "
                f"after the predecessor finished finalizing, so the acceptance was not exercised (missing "
                f"overlap is NOT_EXERCISED, not a chain violation)", "setup", "L5")
        else:
            add("L5_successor_started", OK if overlap else (UNKNOWN if release_us is None else FAIL),
                f"{len(starts)} start(s): " + ", ".join(body for _u, body, _iv in starts[:4])
                + (f"; {len(after_release)} after A's release edge" if release_us is not None else
                   "; A's release edge is human-positioned only, so the ordering is positional"),
                "retention", "L5")

    # ---------------- L5b: the successor's start route (tasks 4.2 and 4.3) ----------------
    # The spec asks each leg's one gesture to reach the successor "through the **production route**". Each route
    # names itself in the device's own rows, and each has a *wrong* twin that must not be read as success:
    #
    #   record: the S4f.6 carrier (`Loop N: Record start carried on the switch`) or its retained twin vs. the
    #           playback/overdub branches `handleToggleRecordForSlot` takes for a target that is not empty;
    #   overdub: the capture-switch gesture (`Loop N: Switch gesture from slot S`) vs. the playback queue
    #           (`Queued`/`Reaffirmed playback switch`), which is what a separate A stop before the press produces
    #           -- task 4.3's "no playback re-affirmation".
    #
    # The span is the **route decision**: from the predecessor's accepted stop to the successor's start. Measuring
    # it over the whole capture would read a later, legitimate `Playback started` -- the switched loop resuming --
    # as a wrong route, which is why the endpoint is the successor's own start row rather than the end of the log.
    # A wrong route only *fails* when the successor actually started through it (a capture that never got that far
    # is the existing L5 gap); absence of both vocabularies is an evidence gap, never a pass.
    def _row_body(line: str) -> str:
        """The `#CAP` body of a raw line, or the line itself for the directly printed rows."""
        m = CAP_RE.match(line.strip())
        return m.group(2) if m else line

    if leg == "rec":
        # The record route is decided between A's accepted stop and B's record start, and its rows are emitted
        # inside that span (the switch press carries the start, so the carrier lands at A's stop).
        stop_edge = "record stop"
        route_name = "a carried record start"
        started = [i for i, l in enumerate(lines)
                   if (m := RECA_RE.match(_row_body(l))) and int(m.group(1)) == b0]
        span_start = [i for i, l in enumerate(lines)
                      if (m := RECS_STOP_RE.match(_row_body(l))) and int(m.group(1)) == a0]
        span = lines[span_start[0]:(started[0] + 1 if started else len(lines))] if span_start else []
        production = [l.strip()[:120] for l in span if any(r in l for r in RECORD_START_CARRIED_ROWS)]
        wrong = [l.strip()[:120] for l in span if any(r in l for r in WRONG_MODE_ROUTE_ROWS)]
    else:
        # The overdub leg's route rows are the **press** rows themselves and are mutually exclusive at the press:
        # `Loop B: Switch gesture from slot A` is the capture-switch, `Queued`/`Reaffirmed playback switch` is the
        # playback queue. So the route is the *first* such row for B -- a later playback row belongs to B's own
        # stop press, which is legitimate -- and it is judged only when the successor actually started.
        stop_edge = "accepted-stop binding"
        route_name = "the capture-switch gesture"
        started = od_b["starts"]
        press_rows = [(i, l.strip()[:120]) for i, l in enumerate(lines)
                      if any(token in l for token in (f"Loop {slot_b}: Switch gesture from slot ",
                                                      f"Loop {slot_b}: Queued playback switch",
                                                      f"Loop {slot_b}: Reaffirmed playback switch"))]
        span_start = [i for i, l in enumerate(lines)
                      if (m := ODUB_BOUND_RE.match(_row_body(l))) and int(m.group(3)) == a0] or press_rows
        first = press_rows[0] if press_rows else None
        production = [first[1]] if first and "Switch gesture from slot " in first[1] else []
        wrong = [first[1]] if first and "Switch gesture from slot " not in first[1] else []
    if not span_start:
        add("L5b_b_start_route", UNKNOWN,
            f"no {stop_edge} for A (firmware slot {a0}) in this capture, so the switch that should carry B's "
            "start is not located", "evidence", "L5b")
    else:
        evidence["b_start_route"] = production + wrong
        if production:
            add("L5b_b_start_route", OK,
                "the production route named itself on the switch: " + production[0], "retention", "L5b")
        elif wrong and started:
            add("L5b_b_start_route", FAIL,
                "B started, but the switch was answered as playback/overdub rather than as " + route_name + ": "
                + wrong[0] + " -- design decision 5 refuses a wrong route as a substitute", "retention", "L5b")
        else:
            add("L5b_b_start_route", UNKNOWN,
                f"neither {route_name} nor a wrong-route row appears between A's {stop_edge} and B's start, so "
                "which route started B is not established by this capture", "evidence", "L5b")

    # ---------------- L7: no second press ----------------
    # A single execution cannot prove a single press (a retried press may collapse into one execution), so this
    # link counts the device's own per-interval **request** rows: `Loop N: Stop Overdub` is the press that asked
    # for N's stop, and `Loop T: Switch gesture from slot S` is a press that stops S -- in this leg's shape the
    # capture-switch *is* A's stop. Both name the interval, so another slot's press cannot substitute. On the
    # record leg the same question is answered by the stop rows themselves (a second press is a second stop).
    if leg == "od":
        requests = _overdub_stop_requests(lines, (slot_a, slot_b))
        requests_a, requests_b = requests[slot_a], requests[slot_b]
        duplicated = (len(od_a["bindings"]) > 1 or len(od_b["stop_terminals"]) > 1
                      or len(od_b["releases"]) > 1)
        evidence["stop_requests"] = [l.strip()[:120] for l in lines
                                     if HUMAN_STOP_OVERDUB_RE.search(l) or HUMAN_SWITCH_SOURCE_RE.search(l)]
        if duplicated:
            add("L7_one_press_per_stop", FAIL,
                "an interval shows a second accepted stop or a second execution, so a press was needed twice",
                "retention", "L7")
        elif requests_a == 0 and requests_b == 0:
            add("L7_one_press_per_stop", UNKNOWN,
                "the capture holds no per-interval stop-request row (`Loop N: Stop Overdub` or "
                "`Loop T: Switch gesture from slot S`), so the press count is not established", "evidence", "L7")
        else:
            add("L7_one_press_per_stop", OK if (requests_a == 1 and requests_b == 1) else FAIL,
                f"{requests_a} stop request(s) for A and {requests_b} for B, with one accepted stop for A and "
                f"one execution for B", "retention", "L7")
    else:
        # A second press would show as a *second* stop request for the same interval, which L1/L6 already count;
        # this link states the user-facing consequence explicitly.
        # Successful executions do not count button requests: retries may collapse
        # into one execution. The driver gesture report is a separate required input.
        second_press = (len(stops_a) > 1) or (len(stops_b) > 1)
        add("L7_one_press_per_stop", FAIL if second_press else UNKNOWN,
            "a second stop request exists for an interval, so a press was required twice" if second_press else
            "stop executions alone cannot prove one input press; correlate the driver's gesture report",
            "retention", "L7")

    if leg == "od":
        # `overdub_interval_identity`: is the leg's own identity chain established for **both** intervals --
        # start, accepted stop and retirement, each on its own slot? This is the link S4f.22's OD acceptance
        # reads, and it is `ok` only when every part is present and consistent; otherwise it names each missing
        # part, so a run is never called accepted on its record seeds. Every row is a Tier-A family, so an
        # evicted one is already the `overflow_free` setup gate rather than a silent gap.
        ids_a = {iv for _, iv, _ in od_a["starts"]} | {iv for _, iv, _ in od_a["bindings"]}
        ids_b = {iv for _, iv, _ in od_b["starts"]} | {iv for _, iv, _ in od_b["bindings"]}
        missing: list[str] = []
        for label, ident, accepts in (("A", od_a, "bindings"), ("B", od_b, None)):
            if not ident["starts"]:
                missing.append(f"{label}: no start admission (`DIAG,reentry,admit`)")
            if accepts is not None and not ident[accepts]:
                missing.append(f"{label}: no accepted stop (`DIAG,reentry,bound`)")
            if accepts is None and not (ident["stop_terminals"] or ident["releases"]):
                missing.append(f"{label}: no stop execution (`DIAG,stop_state`) and no retirement edge")
            if not ident["releases"]:
                missing.append(f"{label}: no retirement edge (`DIAG,canonical_release`)")
        if len(ids_a) > 1 or len(ids_b) > 1:
            missing.append("an interval reports more than one identity")
        elif ids_a and ids_b and ids_a & ids_b:
            # The correlation the whole link rests on: two intervals of one leg must not claim one identity,
            # or another slot's rows could stand in for this interval's.
            missing.append(f"A and B report the same interval identity {sorted(ids_a & ids_b)}")
        for label, ident in (("A", od_a), ("B", od_b)):
            for part in ("starts", "bindings", "stop_terminals", "releases"):
                for us, *_rest in ident[part]:
                    evidence.setdefault("overdub_identity", []).append(
                        f"{label} {part}: {us} us" + (f" interval {_rest[0]}" if part in ("starts", "bindings") else ""))
        if missing:
            add("overdub_interval_identity", UNKNOWN,
                "the overdub intervals' own identity chain is incomplete: " + "; ".join(missing)
                + " -- the seeds' `RECA`/`RECS` rows describe the record lifecycle and cannot stand in for it",
                "evidence")
        else:
            add("overdub_interval_identity", OK,
                f"both intervals identified by their own rows: A interval {od_a['interval']} slot {a0} "
                f"(start {od_a['starts'][0][0]} us, accepted {od_a['bindings'][0][0]} us, retired "
                f"{od_a['releases'][0][0]} us); B interval {od_b['interval']} slot {b0} "
                f"(start {od_b['starts'][0][0]} us, executed "
                f"{(od_b['stop_terminals'] or od_b['releases'])[0][0]} us, retired {od_b['releases'][0][0]} us)",
                "retention")

    # ---------------- the measured window's own invariants ----------------
    # Spec requirement *Continuously playing acceptance window*: the transport must keep playing through each
    # measured window, and a clock transition invalidates the evidence it spans. Two device-owned rows carry
    # it: `Transport stopped` (printed by `ClockManager::stopTransport` -- a human row carrying the device
    # clock, written straight to `Serial`, so it is not a ring row and its absence is live evidence) and the
    # one-shot `#CAP,<us>,CS,<from>,<to>` from `DebugSessionCapture::clockSource`, the sole clock owner.
    #
    # The window is the *runner's* boundary, not a guess: the driver records `leg_start_row` (immediately
    # before the leg's first gesture) and `leg_window_end_row` (when the plan returned, before the settle and
    # the final stopped-only stop), and the CLI passes those marks here. Without them the oracle falls back to
    # the leg's own press span -- the `Loop N:` request rows it already reads for L7 -- which is a *weaker*
    # boundary and is reported as such; with no boundary at all the invariants are UNKNOWN, never OK.
    bounds = window if (window is not None and 0 <= window[0] < window[1] <= len(lines)) else None
    boundary_source = "the run's own leg marks" if bounds is not None else ""
    if bounds is None:
        request_indices = [i for i, l in enumerate(lines)
                           if HUMAN_STOP_OVERDUB_RE.search(l) or HUMAN_SWITCH_SOURCE_RE.search(l)
                           or HUMAN_STOP_RECORDING_RE.search(l)]
        if len(request_indices) >= 2:
            bounds = (request_indices[0], request_indices[-1] + 1)
            boundary_source = ("the leg's own press span (no run marks were supplied, so this boundary "
                               "excludes anything before the first press and after the last one)")
    if bounds is None:
        add("continuous_transport", UNKNOWN,
            "the measured window is not bounded by this capture: no run marks and fewer than two leg press "
            "rows, so \"the transport never stopped inside it\" is not established", "window")
        add("clock_source_stable", UNKNOWN,
            "the measured window is not bounded by this capture, so a clock transition inside it cannot be "
            "ruled out", "window")
    else:
        start, end = bounds
        inside = lines[start:end]
        stops_in = [l.strip()[:120] for l in inside if "Transport stopped" in l]
        clock_in = [l.strip()[:120] for l in inside if (m := CAP_CS_RE.match(l.strip()))]
        # The shed rule is scoped to the window, not to the capture: a row dropped *outside* the span says
        # nothing about whether the span kept every row the device emitted in it (measured: the device run in
        # `captures/s4f22-dec096-od-13` shed one ordinary row at device 51.4 s, well before its leg began, and a
        # capture-wide rule would have called that window's clock source unmeasurable).
        shed_in_window = [l.strip()[:120] for l in inside if RING_SHED_RE.search(l)]
        # The rows themselves are evidence: a FAIL is only useful if a reader can see which row said so.
        evidence["window_transports_stopped"] = stops_in
        evidence["window_clock_transitions"] = clock_in
        evidence["window_shed_rows"] = shed_in_window
        if stops_in:
            add("continuous_transport", FAIL,
                f"{len(stops_in)} `Transport stopped` row(s) inside the measured window "
                f"({boundary_source}): " + "; ".join(stops_in) + " -- the acceptance window must stay "
                "playing; a stop inside it is a contract failure, not a pause", "window")
            if leg == "rec":
                # The one place where the failure is ours rather than the device's, said out loud instead of
                # left for a reader to infer: the record leg's plan (`leg_gestures`' `a_stop`) stops the global
                # transport itself, while the spec's record-successor scenario has the switch to B carry B's
                # start. Task 4.2 is that change; until it lands, a rec run's window contains its own stop.
                limitations.append(
                    "On the record leg this failure names the *plan* (`leg_gestures`' `a_stop`) rather than the "
                    "device: the spec's record-successor scenario has the switch to B carry B's record start, "
                    "which is task 4.2's change.")
        else:
            add("continuous_transport", OK,
                f"no `Transport stopped` row inside the measured window ({boundary_source})", "window")
        if clock_in:
            add("clock_source_stable", FAIL,
                f"{len(clock_in)} clock-source transition row(s) inside the measured window: "
                + "; ".join(clock_in) + " -- the evidence either side of a clock change is not one timeline",
                "window")
        elif shed_in_window:
            # A `CS` row is ordinary (non-Tier-A) ring text, so a row dropped *inside* the span means absence
            # is not evidence for this one -- the honest answer is UNKNOWN rather than OK.
            add("clock_source_stable", UNKNOWN,
                f"no clock-source row inside the measured window, but {len(shed_in_window)} diagnostic row(s) "
                "were shed by policy inside that span, so absence of the row is not proof of a stable clock "
                f"source: {shed_in_window[0]}", "window")
        else:
            add("clock_source_stable", OK,
                "no clock-source transition inside the measured window, and no row was shed inside that span "
                "(so a transition there would have been kept)", "window")
    evidence["window"] = (
        [f"rows [{bounds[0]}:{bounds[1]}) from {boundary_source}"] if bounds is not None
        else ["unbounded: the two window invariants above are UNKNOWN, not measured"]
    )

    # ---------------- verdict ----------------
    if any(c.status == FAIL and c.kind == "setup" for c in checks):
        verdict = "SETUP_INCOMPLETE"
    elif any(c.status == FAIL for c in checks):
        verdict = "ACCEPTED_SEQUENCE_FAIL"
    elif any(c.status == UNKNOWN for c in checks):
        verdict = "INSUFFICIENT_EVIDENCE"
    else:
        verdict = "ACCEPTED_SEQUENCE_PASS"

    limitations.append(
        "A's finalization window and B's start are ordered by device timestamps where the `#CAP` rows supply "
        "them; where only a human row exists the ordering is positional, and that is recorded rather than "
        "upgraded to a device-time claim.")
    limitations.append(
        "Content parity for both intervals is the `hitl-content-verification` inventory pipeline's job, not "
        "this oracle's: pointer to that pipeline rather than a duplicate of it.")
    if fallback_rows:
        limitations.append(
            "this run logs the DEC-070 bounded retarget fallback (`Overdub stop retarget: companion path "
            "stalled; raw pass only`), once per occurrence. It is not a link of this oracle and does not change "
            "the verdict above: the fallback's own closure condition is `successor-capture-reentry-unguarding` "
            "task 5.11, so this verdict says nothing about whether the fallback is gone.")
    return Analysis(verdict, checks, evidence, limitations)


def report_dict(analysis: Analysis, identity: Optional[dict] = None) -> dict[str, Any]:
    return {
        "verdict": analysis.verdict,
        "exit_code": analysis.exit_code,
        "identity": identity or {},
        "checks": [{"name": c.name, "kind": c.kind, "status": c.status, "link": c.link, "detail": c.detail}
                   for c in analysis.checks],
        "evidence": analysis.evidence,
        "limitations": analysis.limitations,
    }


def verdict_bundle(
    analysis: Analysis,
    *,
    leg: str,
    identity: dict,
    run_id: str,
    run_report: Optional[dict] = None,
) -> dict:
    """The oracle's verdict as a sealed bundle: one predicate per check, so a replay re-derives the verdict.

    The bundle is what makes this verdict checkable later without a device: the checks, their evidence and the
    run's teardown result are recorded verbatim, and `verify_report_bundle` re-runs the same rule over them.
    `UNKNOWN` counts as not-ok on purpose -- "the required edge is absent" is not a pass -- and a pass needs the
    run's own teardown to have succeeded ("shutdown failure SHALL prevent overall success"), which is why the
    driver's report is passed in: without it the bundle says so instead of claiming a pass. The run's own
    authorization record travels the same way, so a sealed verdict names the scope the run was driven under
    (task 4.1) instead of leaving the bundle's `authorization` section empty.
    """
    teardown = (run_report or {}).get("shutdown") if run_report else None
    predicates = [
        {
            "check": check.name + (f" [{check.link}]" if check.link else ""),
            "kind": check.kind,
            "ok": check.status == OK,
            "expected": "ok",
            "observed": check.detail,
        }
        for check in analysis.checks
    ]
    return build_report_bundle(
        run_id=run_id,
        kind=f"accepted-sequence:{leg}",
        identity=dict(identity),
        status=analysis.verdict,
        claims_success=(analysis.verdict == "ACCEPTED_SEQUENCE_PASS"),
        predicates=predicates,
        authorization=_authorization_section(run_report),
        teardown=dict(teardown) if isinstance(teardown, dict) else None,
        evidence={
            "leg": leg,
            "checks_total": len(analysis.checks),
            "unknown_checks": [c.name for c in analysis.checks if c.status == UNKNOWN],
            "limitations": list(analysis.limitations),
            "driver_result": (run_report or {}).get("result") if run_report else None,
            **dict(analysis.evidence),
        },
        notes=([
            "the verdict is the accepted-sequence oracle's; every check it made is a predicate here",
            "the run's own teardown result is part of the verdict: a pass needs a successful drain",
        ] + list(analysis.limitations)),
    )


def _authorization_section(run_report: Optional[dict]) -> Optional[dict]:
    """The run's own authorization record, verbatim, for the bundle's `authorization` section."""
    record = (run_report or {}).get("authorization")
    return dict(record) if isinstance(record, dict) else None


def _run_window(run_report: Optional[dict]) -> Optional[tuple[int, int]]:
    """The measured window as the *driver* recorded it: `(leg_start_row, leg_window_end_row)`.

    Both marks are line counts the driver took at the two boundaries the spec draws -- immediately before the
    leg's first gesture, and when the leg's last planned step returned, i.e. **before** the settle and the
    final stopped-only stop, which are the run's teardown rather than part of the window. They are accepted
    only as an ordered, non-empty `[start, end)` pair of line offsets into the capture being analysed;
    anything else (a run that never reached its first gesture, or a report from a driver that predates the
    window mark) yields `None`, and `analyse` then either falls back to the leg's own press span or reports
    the invariants as UNKNOWN -- never as OK.
    """
    if not isinstance(run_report, dict):
        return None
    start, end = run_report.get("leg_start_row"), run_report.get("leg_window_end_row")
    if any(isinstance(v, bool) or not isinstance(v, int) for v in (start, end)):
        return None
    if start < 0 or end <= start:
        return None
    return (start, end)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="S4f.13 accepted-sequence oracle over a managed HITL capture log")
    p.add_argument("--session-log", type=Path, required=True)
    p.add_argument("--leg", choices=("rec", "od"), required=True)
    p.add_argument("--slot-a", type=int, required=True,
                   help="A's slot as the DRIVER names it (1-based, matching its CLI)")
    p.add_argument("--slot-b", type=int, required=True, help="B's slot, same base as --slot-a")
    p.add_argument("--metadata", type=Path, default=None)
    p.add_argument("--base-report", type=Path, default=None)
    p.add_argument("--out", type=Path, default=None)
    p.add_argument("--run-report", type=Path, default=None,
                   help="the driver's accepted-sequence.json: its teardown result joins the sealed verdict")
    p.add_argument("--content-report", type=Path, default=None,
                   help="the content-parity owner's report (hitl.library_manifest.compare_manifests): a failure "
                        "vetoes an otherwise complete acceptance, and its absence is an evidence gap")
    p.add_argument("--timing-report", type=Path, default=None,
                   help="the timing owner's summary (hitl.verify.midi_boundary_timing): a reported issue vetoes an "
                        "otherwise complete acceptance, and its absence is an evidence gap")
    p.add_argument("--emit-bundle", type=Path, default=None,
                   help="seal this verdict as a run bundle (default: none; needs --run-report for a pass)")
    args = p.parse_args(argv)

    text = args.session_log.read_text(encoding="utf-8", errors="replace")
    meta = json.loads(args.metadata.read_text()) if args.metadata and args.metadata.exists() else None
    base = json.loads(args.base_report.read_text()) if args.base_report and args.base_report.exists() else None
    run_report = (json.loads(args.run_report.read_text())
                  if args.run_report and args.run_report.exists() else None)
    # The external halves of the conjunction come from their owners' own reports; the teardown is the run's own
    # shutdown record, which the driver already wrote into its report.
    inputs = {
        "content_parity": acceptance_evidence.load_report(args.content_report),
        "timing": acceptance_evidence.load_report(args.timing_report),
        "shutdown": (run_report or {}).get("shutdown"),
    }
    analysis = analyse(text, leg=args.leg, slot_a=args.slot_a, slot_b=args.slot_b,
                       metadata=meta, base_report=base, window=_run_window(run_report), inputs=inputs)
    identity = {k: (meta or {}).get(k) for k in ("git_commit", "firmware_hex_sha256")} if meta else {}
    payload = report_dict(analysis, identity)
    out = args.out or (args.session_log.parent / "accepted-sequence-review.json")

    if args.emit_bundle is not None:
        bundle = verdict_bundle(analysis, leg=args.leg, identity=identity,
                                run_id=f"accepted-sequence-{args.leg}-{args.session_log.parent.name}",
                                run_report=run_report)
        payload["bundle"] = str(write_report_bundle(args.emit_bundle.parent, bundle,
                                                   name=args.emit_bundle.name))
    out.write_text(json.dumps(payload, indent=2) + "\n")

    print(f"accepted-sequence verdict ({args.leg} leg): {analysis.verdict}")
    for c in analysis.checks:
        mark = {OK: "ok  ", FAIL: "FAIL", UNKNOWN: "????"}[c.status]
        print(f"  [{mark}] ({c.kind}{'/' + c.link if c.link else ''}) {c.name}: {c.detail}")
    for lim in analysis.limitations:
        print(f"  limitation: {lim}")
    print(f"  review: {out}")
    if "bundle" in payload:
        print(f"  bundle: {payload['bundle']} (replay: scripts/hitl_report_replay.py --bundle <path>)")
    return analysis.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
