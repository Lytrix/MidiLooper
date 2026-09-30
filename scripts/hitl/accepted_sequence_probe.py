#!/usr/bin/env python3
"""S4f.13 oracle — read an accepted-sequence leg's session log and report whether it is the required chain.

The requirement (DEC-078 / invariant 31) is that a stop is **accepted on one press** and executed by the
pipeline, so the successor can start while the predecessor still finalizes and no user ever presses twice.
The leg is therefore proven only as an **ordered chain**, each link on its own evidence:

    L1  A's stop was accepted and latched (or admitted) exactly once
    L2  no capacity denial: `successor_stop_denied_capacity` and `reentryDecision("deny", …)` are absent
    L3  A's finalization window opened (the predecessor is finalizing)
    L4  B started while A still finalized -- the acceptance, not a queue that never ran
    L5  B's stop executed exactly once, after A released
    L6  no second press was needed: exactly one stop request per interval
L7  the transport/display stayed healthy: `overflow_free` (no capture-ring loss), transport_ok

A missing or reordered link is never a pass. Verdicts and exit codes keep "not this shape" apart from "this run
is not evidence":

  * `SETUP_INCOMPLETE` (3): freshness/identity/staging -- fix the run. Staging includes the *shape* links:
    if A's finalization window or its release never appeared, the acceptance was not exercised
    (`captures/s4f22-rec-rec-01` and `.../s4f13-od4-20260927_020114` are NOT_EXERCISED runs, not a
    firmware verdict).
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

OK, FAIL, UNKNOWN = "ok", "fail", "unknown"

CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
# Exactly the successful stop kinds the firmware emits, not an open `stop*` pattern.
RECS_STOP_RE = re.compile(r"^RECS,(?:stop|stopToStopped),(\d+),")
RECA_RE = re.compile(r"^RECA,(\d+),")
WINDOW_OPEN_RE = re.compile(r"^DIAG,reentry_window_open,(\d+),")
CANONICAL_RELEASE_RE = re.compile(r"^DIAG,canonical_release,(\d+),")
REENTRY_RE = re.compile(r"^DIAG,reentry,([a-z_]+),")
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


def analyse(text: str, *, leg: str, slot_a: int, slot_b: int,
            metadata: Optional[dict] = None, base_report: Optional[dict] = None) -> Analysis:
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

    # Timing shedding is reported, never fatal: the policy discards non-Tier-A diagnostics so the playback
    # turn never blocks on USB I/O, and it is distinguishable from "nothing was dropped at all" only by
    # name. Reported as `ok` with the rows attached, so a reader still sees which windows shed.
    shed_hits = [l.strip()[:120] for l in lines if RING_SHED_RE.search(l)]
    add("ring_shed_diagnostics_only", OK,
        "no timing shed" if not shed_hits else
        f"{len(shed_hits)} timing-shed row(s) -- non-Tier-A diagnostics discarded by policy, not a ring "
        f"loss; first: {shed_hits[0]}", "setup")
    evidence["ring_shed"] = shed_hits

    # ---------------- L1/L6: the stop requests ----------------
    stops_a = [(e.us, e.body) for e in entries
               if (m := RECS_STOP_RE.match(e.body)) and int(m.group(1)) == a0]
    stops_b = [(e.us, e.body) for e in entries
               if (m := RECS_STOP_RE.match(e.body)) and int(m.group(1)) == b0]
    for us, body in stops_a + stops_b:
        evidence["stop_requests"].append(f"{us}: {body}")

    if not stops_a:
        add("L1_a_stop_accepted_once", FAIL,
            f"no successful stop record for A (firmware slot {a0}): A's stop never executed", "retention", "L1")
    else:
        add("L1_a_stop_accepted_once", OK if len(stops_a) == 1 else FAIL,
            f"{len(stops_a)} stop record(s) for A: {stops_a[0][1]}" +
            ("" if len(stops_a) == 1 else " -- more than one means a stop was executed twice"),
            "retention", "L1")

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
        # to the leg plan). The same applies to the record leg by design: its production stop publishes
        # inline, so no window is the expected shape there and the packet classifies it NOT_EXERCISED.
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
    a_stop_us = stops_a[0][0] if stops_a else None
    all_starts = [(e.us, e.body) for e in entries
                  if (m := RECA_RE.match(e.body)) and int(m.group(1)) == b0]
    starts = [(us, body) for us, body in all_starts if a_stop_us is None or us > a_stop_us]
    for us, body in all_starts:
        evidence["successor_start"].append(
            f"{us}: {body}" + ("" if (us, body) in starts else "  (before A's stop: the seed)"))
    release_us = releases[0][0] if releases else (
        # Fall back to the human window's line position, which is all a human row can offer.
        None)
    if not starts:
        add("L5_successor_started", FAIL,
            "no start follows A's stop: nothing succeeded the predecessor, so the acceptance was not exercised"
            + ("" if a_stop_us is None else f" (A's stop was at {a_stop_us} us)"), "retention", "L5")
    else:
        after_release = [u for u, _ in starts if release_us is not None and u > release_us]
        overlap = (a_stop_us is not None and release_us is not None
                   and any(a_stop_us < us < release_us for us, _ in starts))
        add("L5_successor_started", OK if overlap else (UNKNOWN if release_us is None else FAIL),
            f"{len(starts)} start(s): " + ", ".join(b for _, b in starts[:4])
            + (f"; {len(after_release)} after A's release edge" if release_us is not None else
               "; A's release edge is human-positioned only, so the ordering is positional"),
            "retention", "L5")

    # ---------------- L7: no second press ----------------
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
        # RECA and RECS are emitted by record start/stop, not overdub lifecycle.
        # In the old driver these were the seeds, producing a false acceptance.
        for check in checks:
            if check.name in ("L1_a_stop_accepted_once", "L6_b_stop_executes_once",
                               "L5_successor_started", "L7_one_press_per_stop"):
                check.status = UNKNOWN
                check.kind = "evidence"
                check.detail = "Record-only RECA/RECS cannot establish this overdub link"
        add("overdub_interval_identity", UNKNOWN,
            "RECA/RECS do not identify overdub intervals. Correlated overdub start, accepted-stop and "
            "retirement evidence for A and B is still required; record seeds cannot close this leg",
            "evidence")

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
    args = p.parse_args(argv)

    text = args.session_log.read_text(encoding="utf-8", errors="replace")
    meta = json.loads(args.metadata.read_text()) if args.metadata and args.metadata.exists() else None
    base = json.loads(args.base_report.read_text()) if args.base_report and args.base_report.exists() else None
    analysis = analyse(text, leg=args.leg, slot_a=args.slot_a, slot_b=args.slot_b,
                       metadata=meta, base_report=base)
    identity = {k: (meta or {}).get(k) for k in ("git_commit", "firmware_hex_sha256")} if meta else {}
    payload = report_dict(analysis, identity)
    out = args.out or (args.session_log.parent / "accepted-sequence-review.json")
    out.write_text(json.dumps(payload, indent=2) + "\n")

    print(f"accepted-sequence verdict ({args.leg} leg): {analysis.verdict}")
    for c in analysis.checks:
        mark = {OK: "ok  ", FAIL: "FAIL", UNKNOWN: "????"}[c.status]
        print(f"  [{mark}] ({c.kind}{'/' + c.link if c.link else ''}) {c.name}: {c.detail}")
    for lim in analysis.limitations:
        print(f"  limitation: {lim}")
    print(f"  review: {out}")
    return analysis.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
