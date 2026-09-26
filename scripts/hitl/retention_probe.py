#!/usr/bin/env python3
"""S4f.3 retention oracle: prove the accepted-switch retention chain on the device, or say why not.

The requirement is proven only as an **ordered chain**, each link on its own evidence:

    L1  A's finalizer outstanding
    L2  B's stop denied, with B's own slot *and* interval identity
    L3  C's switch/start request retained (present, not consumed)
    L4  capacity released (A retired)
    L5  that same B stop admitted and bound
    L6  C starts exactly once, after the release
    L7  C recorded the expected MIDI
    L8  bounded follow-up shows no duplicate execution

A missing or reordered link is **never** a pass. Two verdicts keep the reasons apart:

  * `SETUP_INCOMPLETE` (exit 3) -- this run is not usable evidence (freshness, tempo, seed, pinned identity,
    capture integrity, observation tail, staging). Fix the run.
  * `INSUFFICIENT_EVIDENCE` (exit 4) -- the run was staged but a link has **no supporting telemetry**. Fix the
    telemetry or the stimulus; do not infer the link.

plus `RETENTION_NOT_EXERCISED` (2), `RETENTION_FAIL` (2), `RETENTION_PASS` (0).

Telemetry used, all existing and slot/identity-bearing where the chain needs it:

    DIAG,reentry_window_open,<slot>,<stopTick>,<loopLengthTicks>   DEC-067 successor_start_window_open
    DIAG,canonical_release,<slot>,<loopLengthTicks>,<chunks>       canonical_finalization_capacity_released
    DIAG,reentry,<decision>,<tick>,<interval>,<slot>,<depth>       the denial, from the decision owner
    ODUB,stop,admission,...,successor_stop_denied_capacity         the denial's stop-side stage record
    DIAG,stop_phase|stop_grain,<track>,<slot>,...                  the deferred stop transaction, per slot
    RECA,<slot>,<tick>                                             the capture start
    RECS,stop,<slot>,... / RECS,stage,seal,...,<eventCount>,...     the sealed take and its event count

**Known telemetry gap, reported rather than papered over.** `captureInterval_` -- the identity the denial
names -- is emitted *only* by `reentryDecision`. Nothing names it when a stop later **binds**: the stop-tier
records carry the job's `frozenGeneration` (`staging.captureGeneration`), which is a different identity space,
and `storedNoteEvent`/`storedNotes` have no callers at all. So L5 can be proven at **slot** granularity but
not tied to the denied interval, and L7 can be proven at **event-count** granularity but not at pitch level
unless the content pipeline's review is supplied. Both are reported as `INSUFFICIENT_EVIDENCE` unless the
operator explicitly concedes the weaker link, in which case the concession is recorded as such -- never as a
proof.

Usage:
    retention_probe.py --session-log <log> [--base-report <json>] [--metadata <json>]
                       [--content-review <json>] [--out retention-review.json]
                       [--slot-a 0] [--slot-b 1] [--slot-c 2] [--track-b 0]
                       [--tempo-bpm 120] [--min-window-us 150000] [--followup-us 500000]
                       [--expect-successor-events N] [--allow-setup-issue NAME]...
                       [--accept-slot-scoped-bind] [--accept-event-count-only]
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
WINDOW_OPEN_RE = re.compile(r"^DIAG,reentry_window_open,(\d+),(\d+),(\d+)$")
CANONICAL_RELEASE_RE = re.compile(r"^DIAG,canonical_release,(\d+),(\d+),(\d+)$")
REENTRY_RE = re.compile(r"^DIAG,reentry,([a-z_]+),(\d+),(\d+),(\d+),(\d+)$")
RECA_RE = re.compile(r"^RECA,(\d+),(\d+)$")
RECS_STOP_RE = re.compile(r"^RECS,stop,(\d+),")
RECS_SEAL_RE = re.compile(r"^RECS,stage,seal,")
STOP_SLOT_RE = re.compile(r"^DIAG,(?:stop_phase|stop_grain),(\d+),(\d+),")
DENIAL_STAGE_RE = re.compile(r"^ODUB,stop,admission,.*,successor_stop_denied_capacity$")
TEMPO_FEEDBACK_RE = re.compile(r"Sent tempo CC feedback: ([0-9.]+) BPM")
TEMPO_APPLIED_RE = re.compile(r"TEMPO: CC=\d+ -> ([0-9.]+) BPM")
RETAINED_LOG_RE = re.compile(r"Loop (\d+): Switch and record start retained")
INVARIANT_VIOLATION_MARKERS = (
    "record activation invariant violated",
    "record activation violated its invariant",
)
OVERFLOW_MARKERS = ("RING,overflow", "RING,tier_a_evicted")

DEFAULT_MIN_WINDOW_US = 150_000
DEFAULT_FOLLOWUP_US = 500_000

OK, FAIL, UNKNOWN = "ok", "fail", "unknown"


@dataclass
class Entry:
    us: int
    body: str


@dataclass
class Check:
    name: str
    status: str          # OK | FAIL | UNKNOWN
    detail: str
    kind: str = "retention"   # setup | retention | evidence
    link: Optional[str] = None


@dataclass
class Episode:
    slot_a: int
    window_open_us: int
    window_open_loop_len: int
    capacity_release_us: int
    window_us: int


@dataclass
class Analysis:
    verdict: str
    checks: list[Check] = field(default_factory=list)
    episode: Optional[Episode] = None
    evidence: dict[str, list[str]] = field(default_factory=dict)
    limitations: list[str] = field(default_factory=list)
    concessions: list[str] = field(default_factory=list)

    @property
    def exit_code(self) -> int:
        return {"RETENTION_PASS": 0, "RETENTION_FAIL": 2, "RETENTION_NOT_EXERCISED": 2,
                "SETUP_INCOMPLETE": 3, "INSUFFICIENT_EVIDENCE": 4}[self.verdict]


def parse_entries(text: str) -> list[Entry]:
    entries: list[Entry] = []
    for line in text.splitlines():
        m = CAP_RE.match(line.strip())
        if m:
            entries.append(Entry(us=int(m.group(1)), body=m.group(2)))
    return entries


def _first_after(entries: list[Entry], us: int, predicate) -> Optional[Entry]:
    for e in entries:
        if e.us > us and predicate(e):
            return e
    return None


def _all_after(entries: list[Entry], us: int, predicate) -> list[Entry]:
    return [e for e in entries if e.us > us and predicate(e)]


def _read_json(path: Optional[Path]) -> Optional[dict]:
    if path is None or not path.exists():
        return None
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def _find_base_report(session_log: Path) -> Optional[Path]:
    candidates = sorted(session_log.parent.glob("host_midi_automation_baseline_*.json"))
    return candidates[-1] if candidates else None


def analyse(
    session_log: str,
    *,
    slot_a: int,
    slot_b: int,
    slot_c: int,
    track_b: int = 0,
    tempo_bpm: float = 120.0,
    min_window_us: int = DEFAULT_MIN_WINDOW_US,
    followup_us: int = DEFAULT_FOLLOWUP_US,
    expect_successor_events: Optional[int] = None,
    base_report: Optional[dict] = None,
    metadata: Optional[dict] = None,
    content_review: Optional[dict] = None,
    allow_setup_issues: tuple[str, ...] = (),
    accept_slot_scoped_bind: bool = False,
    accept_event_count_only: bool = False,
) -> Analysis:
    entries = parse_entries(session_log)
    lines = session_log.splitlines()
    checks: list[Check] = []
    evidence: dict[str, list[str]] = {k: [] for k in
                                      ("window_open", "canonical_release", "denial", "retained_request",
                                       "stop_bind", "successor_start", "successor_seal",
                                       "invariant_violation", "overflow")}
    limitations: list[str] = []
    concessions: list[str] = []

    def add(name, status, detail, kind="retention", link=None):
        checks.append(Check(name, status, detail, kind, link))

    # ---------------- observation integrity (setup: this run is/isn't usable evidence) ----------------
    if not entries:
        add("telemetry_present", FAIL, "no #CAP records in the log", "setup")
        return Analysis("SETUP_INCOMPLETE", checks, None, evidence, limitations, concessions)
    add("telemetry_present", OK, f"{len(entries)} #CAP records", "setup")

    head = lines[:60]
    booted = any(l.strip().startswith("BOOT,") for l in head)
    add("fresh_boot", OK if booted else FAIL,
        "boot banner in the first 60 lines" if booted else
        "no BOOT banner: the device did not reset before this run (skill section 3)", "setup")

    commit = (metadata or {}).get("git_commit")
    fw = (metadata or {}).get("firmware_hex_sha256")
    pinned = bool(commit) and bool(fw)
    add("identity_pinned", OK if pinned else FAIL,
        f"git_commit={str(commit)[:12]} firmware_hex_sha256={str(fw)[:12]}" if pinned else
        "metadata.json lacks git_commit and/or firmware_hex_sha256: this evidence is not pinned to a build",
        "setup")

    overflow = [l.strip() for l in lines if any(m in l for m in OVERFLOW_MARKERS)]
    evidence["overflow"] = overflow[:3]
    integrity = ((base_report or {}).get("hitl_report") or {}).get("capture_integrity")
    if overflow:
        add("capture_integrity", FAIL,
            f"capture-ring eviction observed ({overflow[0]}): absent records cannot be trusted", "setup")
    elif integrity is not None and integrity != "COMPLETE":
        add("capture_integrity", FAIL, f"base report capture_integrity={integrity}", "setup")
    else:
        add("capture_integrity", OK,
            "no ring eviction" + (f"; base report capture_integrity={integrity}" if integrity else ""),
            "setup")

    truncated = bool(lines) and not session_log.endswith("\n")
    report_complete = ((base_report or {}).get("hitl_report") or {}).get("capture_complete")
    if truncated:
        add("observation_tail_complete", FAIL, "the log ends mid-line: the observation was cut off", "setup")
    elif report_complete is False:
        add("observation_tail_complete", FAIL, "base report capture_complete=false", "setup")
    else:
        add("observation_tail_complete", OK,
            "log ends on a complete line" + (f"; capture_complete={report_complete}"
                                             if report_complete is not None else ""), "setup")

    issues = list(((base_report or {}).get("hitl_report") or {}).get("issues") or [])
    unresolved = [i for i in issues if i not in allow_setup_issues]
    if unresolved:
        add("reported_issues_resolved", UNKNOWN,
            f"base report issues not accounted for: {unresolved}. Allowlisting one records an **operator "
            f"claim** that it is a setup artefact; it is not evidence, and nothing here proves the cause.",
            "evidence")
    else:
        add("reported_issues_resolved", OK,
            f"every base-report issue is explicitly allowlisted ({issues}) -- recorded as an **operator "
            "claim** that each is a setup artefact. This is not evidence and nothing here proves the cause."
            if issues else "base report lists no issues", "setup")

    tempos = [float(m.group(1)) for m in (TEMPO_FEEDBACK_RE.search(l) or TEMPO_APPLIED_RE.search(l)
                                          for l in lines) if m]
    tempo_ok = any(abs(t - tempo_bpm) <= 0.05 for t in tempos)
    add("tempo_verified", OK if tempo_ok else FAIL,
        f"device tempo {tempos[0]:.1f} BPM matches {tempo_bpm:.1f}" if tempo_ok else
        f"no tempo line matching {tempo_bpm:.1f} BPM (found {tempos[:3] or 'none'}); the pin does not survive "
        "the serial-open reset and must be re-applied and read back per run", "setup")

    setup_transitions = ((base_report or {}).get("hitl_report") or {}).get("setup_transitions_confirmed")
    if setup_transitions is False:
        add("setup_transitions_confirmed", FAIL, "base report setup_transitions_confirmed=false", "setup")
    else:
        add("setup_transitions_confirmed", OK,
            "per-transition setup confirmed" if setup_transitions else "no base report to contradict it",
            "setup")

    # ---------------- L1: A's finalizer outstanding ----------------
    seed = next((e for e in entries if (m := RECS_STOP_RE.match(e.body)) and int(m.group(1)) == slot_a), None)
    add("seed_content_on_a", OK if seed else FAIL,
        f"RECS,stop,{slot_a} at {seed.us} us" if seed else
        f"no RECS,stop,{slot_a}: the seed take on A never landed", "setup")

    window_open = next((e for e in entries
                        if (m := WINDOW_OPEN_RE.match(e.body)) and int(m.group(1)) == slot_a), None)
    if window_open is None:
        add("L1_finalizer_outstanding", FAIL,
            f"no DIAG,reentry_window_open,{slot_a}: A's deferred stop never froze its interval, so the "
            "capacity-denied state cannot be staged", "setup", "L1")
        return Analysis("SETUP_INCOMPLETE", checks, None, evidence, limitations, concessions)
    evidence["window_open"].append(f"{window_open.us}: {window_open.body}")
    add("L1_finalizer_outstanding", OK, f"{window_open.body} at {window_open.us} us", "retention", "L1")

    m_open = WINDOW_OPEN_RE.match(window_open.body)
    release = _first_after(entries, window_open.us,
                           lambda e: (mm := CANONICAL_RELEASE_RE.match(e.body)) is not None
                           and int(mm.group(1)) == slot_a)
    if release is None:
        add("L4_capacity_released", FAIL,
            f"no DIAG,canonical_release,{slot_a} after A's window opened: A never retired", "setup", "L4")
        return Analysis("SETUP_INCOMPLETE", checks, None, evidence, limitations, concessions)
    evidence["canonical_release"].append(f"{release.us}: {release.body}")
    window_us = release.us - window_open.us
    episode = Episode(slot_a, window_open.us, int(m_open.group(3)), release.us, window_us)
    wide = window_us >= min_window_us
    add("capacity_denied_window_wide_enough", OK if wide else FAIL,
        f"{window_us} us ({window_us/1000:.1f} ms) between window_open and canonical_release; floor "
        f"{min_window_us} us" + ("" if wide else ": too narrow to stage two host gestures -- an exposure "
                                 "problem, not a retention result"), "setup")
    add("L4_capacity_released", OK, f"{release.body} at {release.us} us", "retention", "L4")

    def in_window(us: int) -> bool:
        return episode.window_open_us < us < episode.capacity_release_us

    # ---------------- L2: B's identified stop denied ----------------
    denials = [e for e in entries
               if (m := REENTRY_RE.match(e.body)) and m.group(1) in ("deny", "deny_resource")
               and int(m.group(4)) == slot_b]
    staged_denials = [e for e in denials if in_window(e.us)]
    stage_lines = [e for e in entries if DENIAL_STAGE_RE.match(e.body) and in_window(e.us)]
    for e in denials:
        evidence["denial"].append(f"{e.us}: {e.body}{'' if in_window(e.us) else '  (outside the window)'}")

    if staged_denials:
        denial = staged_denials[0]
        interval_b = int(REENTRY_RE.match(denial.body).group(3))
        add("L2_denial_identified_for_b", OK,
            f"{denial.body} at {denial.us} us names slot {slot_b} and interval {interval_b}",
            "retention", "L2")
    elif stage_lines:
        denial = None
        interval_b = None
        add("L2_denial_identified_for_b", UNKNOWN,
            f"only the generic '{stage_lines[0].body.split(',')[-1]}' stop-stage record is present inside the "
            f"window. It does not name the denied slot or interval, so it cannot establish that B's stop was "
            "the one denied; the identity-specific DIAG,reentry,deny,<tick>,<interval>,<slot>,<depth> record "
            "is missing", "evidence", "L2")
    else:
        denial = None
        interval_b = None
        add("L2_denial_identified_for_b", FAIL,
            f"no denial for slot {slot_b} inside A's finalization window ({len(denials)} denial(s) "
            "elsewhere): the capacity-denied state was never entered", "retention", "L2")

    if denial is None:
        # Distinguish "never entered" from "entered but unidentifiable" -- and never let either of those
        # mask a failed setup, which would report a run that is not evidence as a behavioural result.
        if any(c.status == FAIL and c.kind == "setup" for c in checks):
            verdict = "SETUP_INCOMPLETE"
        elif stage_lines:
            verdict = "INSUFFICIENT_EVIDENCE"
        else:
            verdict = "RETENTION_NOT_EXERCISED"
        return Analysis(verdict, checks, episode, evidence, limitations, concessions)

    # ---------------- L3: C's request retained ----------------
    retained_hits = []
    for idx, line in enumerate(lines):
        m = RETAINED_LOG_RE.search(line)
        if not m:
            continue
        mcap = CAP_RE.match(line.strip()) or re.match(r"^#CAP,(\d+),", line.strip())
        # The retained line is a human log row, not a #CAP row; use the surrounding chronology instead.
        retained_hits.append((int(m.group(1)), line.strip()))
    retained_for_c = [h for h in retained_hits if h[0] == slot_c + 1]
    for slot1, line in retained_hits:
        evidence["retained_request"].append(f"Loop {slot1}: {line[:120]}")

    starts = [e for e in entries if (m := RECA_RE.match(e.body)) and int(m.group(1)) == slot_c]
    for e in starts:
        evidence["successor_start"].append(f"{e.us}: {e.body}")
    pre_release_starts = [e for e in starts if e.us < episode.capacity_release_us]

    # The retained log row carries no timestamp of its own; it must sit between the denial and the release,
    # which is checked textually by ordering the log lines.
    denial_line = next((i for i, l in enumerate(lines) if f"DIAG,reentry,deny" in l
                        and f",{slot_b}," in l), None)
    release_line = next((i for i, l in enumerate(lines) if CANONICAL_RELEASE_RE.match(
        (CAP_RE.match(l.strip()).group(2) if CAP_RE.match(l.strip()) else ""))), None)
    retained_line = next((i for i, l in enumerate(lines) if RETAINED_LOG_RE.search(l)
                          and int(RETAINED_LOG_RE.search(l).group(1)) == slot_c + 1), None)
    ordered_retention = (retained_line is not None and denial_line is not None
                         and retained_line > denial_line
                         and (release_line is None or retained_line < release_line))
    if retained_for_c and ordered_retention and not pre_release_starts:
        add("L3_request_retained_for_c", OK,
            f"the retained-request log row for slot {slot_c} appears after the denial and before the "
            "release, and no start was executed before the release", "retention", "L3")
    elif retained_for_c and pre_release_starts:
        add("L3_request_retained_for_c", FAIL,
            f"a start for slot {slot_c} executed at {pre_release_starts[0].us} us, before capacity was "
            f"released at {episode.capacity_release_us} us: the request was not held", "retention", "L3")
    elif not retained_for_c:
        add("L3_request_retained_for_c", UNKNOWN,
            f"no 'Switch and record start retained' row for slot {slot_c} (1-based {slot_c + 1}) in the log. "
            "Retention is currently asserted only by that human log line; there is no #CAP record for a "
            "retained successor request", "evidence", "L3")
    else:
        add("L3_request_retained_for_c", UNKNOWN,
            "the retained-request row for slot "
            f"{slot_c} is present but not ordered after the denial and before the release", "evidence", "L3")

    # ---------------- L5: that same B stop admitted and bound ----------------
    bind_candidates = _all_after(entries, denial.us, lambda e: (
        (mm := WINDOW_OPEN_RE.match(e.body)) is not None and int(mm.group(1)) == slot_b) or (
        (mm := CANONICAL_RELEASE_RE.match(e.body)) is not None and int(mm.group(1)) == slot_b) or (
        (mm := STOP_SLOT_RE.match(e.body)) is not None and int(mm.group(2)) == slot_b))
    after_release = [e for e in bind_candidates if e.us > episode.capacity_release_us]
    for e in after_release[:3]:
        evidence["stop_bind"].append(f"{e.us}: {e.body}")
    limitations.append(
        "L5 identity link unavailable: the denied interval (`captureInterval_`) is emitted only by "
        "`reentryDecision`; nothing names it when a stop binds. Stop-tier records carry the job's "
        "`frozenGeneration` (`staging.captureGeneration`), a different identity space, so slot-scoped bind "
        "evidence cannot be tied back to the denied interval.")
    if after_release:
        detail = (f"slot-scoped bind evidence for slot {slot_b} after the release: {after_release[0].body} "
                  f"at {after_release[0].us} us -- but not tied to interval {interval_b}")
        if accept_slot_scoped_bind:
            concessions.append(f"L5 accepted on slot-scoped evidence (operator concession): {detail}")
            add("L5_same_b_stop_bound", OK, detail + " [operator concession]", "retention", "L5")
        else:
            add("L5_same_b_stop_bound", UNKNOWN, detail, "evidence", "L5")
    else:
        add("L5_same_b_stop_bound", UNKNOWN,
            f"no stop-tier or window record for slot {slot_b} after the release: cannot show B's denied stop "
            "was admitted and bound", "evidence", "L5")

    # ---------------- L6: C starts exactly once, after the release ----------------
    if pre_release_starts:
        add("L6_successor_started_once", FAIL,
            f"a start for slot {slot_c} executed at {pre_release_starts[0].us} us, before capacity was "
            f"released at {episode.capacity_release_us} us", "retention", "L6")
    elif len(starts) == 0:
        add("L6_successor_started_once", FAIL,
            f"no RECA,{slot_c}: the retained successor start never executed", "retention", "L6")
    elif len(starts) > 1:
        add("L6_successor_started_once", FAIL,
            f"{len(starts)} RECA records for slot {slot_c}: the retained start executed more than once",
            "retention", "L6")
    elif starts[0].us < denial.us:
        add("L6_successor_started_once", FAIL,
            f"the only RECA,{slot_c} at {starts[0].us} us precedes the denial at {denial.us} us: the start "
            "did not follow its blocked prerequisite", "retention", "L6")
    else:
        add("L6_successor_started_once", OK,
            f"exactly one RECA,{slot_c} at {starts[0].us} us, after the release at "
            f"{episode.capacity_release_us} us", "retention", "L6")

    # ---------------- L7: C recorded the expected MIDI ----------------
    start_us = starts[0].us if starts else None
    seal = None
    stop_rec = None
    if start_us is not None:
        stop_rec = _first_after(entries, start_us,
                                lambda e: (m := RECS_STOP_RE.match(e.body)) and int(m.group(1)) == slot_c)
        if stop_rec is not None:
            seal = next((e for e in reversed(entries)
                         if e.us < stop_rec.us and RECS_SEAL_RE.match(e.body)), None)
    for e in ([seal] if seal else []):
        evidence["successor_seal"].append(f"{e.us}: {e.body}")

    review_ok = None
    if content_review is not None:
        # The content pipeline's per-cycle verdict, when the run included it.
        cycles = content_review.get("cycles") if isinstance(content_review.get("cycles"), list) else None
        flags = ([c.get("inventory_equal") for c in cycles] if cycles
                 else [content_review.get("inventory_equal")])
        review_ok = bool(flags) and all(f is True for f in flags)

    if review_ok is True:
        add("L7_successor_recorded_expected_midi", OK,
            "content review reports inventory_equal on every cycle (pitch-level)", "retention", "L7")
    elif seal is not None:
        fields = seal.body.split(",")
        try:
            event_count = int(fields[7])
        except (IndexError, ValueError):
            event_count = None
        matches = event_count is not None and event_count > 0 and (
            expect_successor_events is None or event_count == expect_successor_events)
        detail = (f"sealed take for slot {slot_c} reports eventCount={event_count}"
                  + (f" (expected {expect_successor_events})" if expect_successor_events is not None else "")
                  + "; event-count granularity, not pitch-level")
        if matches and accept_event_count_only:
            concessions.append(f"L7 accepted on event-count evidence (operator concession): {detail}")
            add("L7_successor_recorded_expected_midi", OK, detail + " [operator concession]",
                "retention", "L7")
        elif matches:
            add("L7_successor_recorded_expected_midi", UNKNOWN,
                detail + " -- pitch-level verification needs the content pipeline "
                "(--content-review), which this run did not supply", "evidence", "L7")
        else:
            add("L7_successor_recorded_expected_midi", FAIL, detail, "retention", "L7")
    else:
        add("L7_successor_recorded_expected_midi", UNKNOWN,
            f"no sealed take found for slot {slot_c} after its start, and no content review supplied: "
            "cannot show C recorded anything", "evidence", "L7")

    # ---------------- L8: bounded follow-up, no duplicate execution ----------------
    required = [e for e in (window_open, release, denial, *starts, *([seal] if seal else []),
                            *([stop_rec] if stop_rec else [])) if e is not None]
    last_us = max(e.us for e in required)
    # The tail is the largest telemetry timestamp, not the file's last row: records can be flushed out of
    # order, and using file order made a later-in-file-but-earlier-in-time row shrink the follow-up.
    tail_us = (max(e.us for e in entries) - last_us) if entries else 0
    dup_starts = [e for e in starts if e.us > (starts[0].us if starts else 0)]
    dup_denials = [e for e in denials if e.us > episode.capacity_release_us]
    dup_seals = ([e for e in entries if RECS_SEAL_RE.match(e.body) and e.us > (seal.us if seal else 0)
                  and stop_rec is not None and e.us < stop_rec.us + followup_us]
                 if seal is not None and stop_rec is not None else [])
    if tail_us < followup_us:
        add("L8_bounded_followup_clean", FAIL,
            f"only {tail_us} us of telemetry after the last required event (need {followup_us} us): the "
            "follow-up is too short to bound duplicate execution", "retention", "L8")
    elif dup_starts or dup_denials or len(dup_seals) > 1:
        add("L8_bounded_followup_clean", FAIL,
            f"follow-up re-executed a request: starts={len(dup_starts)} "
            f"post-release denials={len(dup_denials)} extra seals={max(0, len(dup_seals) - 1)}",
            "retention", "L8")
    else:
        add("L8_bounded_followup_clean", OK,
            f"{tail_us} us of follow-up after the last required event with no repeat start, no re-issued "
            "denial and no extra seal", "retention", "L8")

    # ---------------- verdict ----------------
    violations = [l.strip() for l in lines if any(m in l for m in INVARIANT_VIOLATION_MARKERS)]
    evidence["invariant_violation"] = violations[:5]
    add("activation_invariant_held", OK if not violations else FAIL,
        "no activation-invariant violation logged" if not violations else
        f"{len(violations)} violation(s): {violations[0]}", "retention")

    if any(c.status == FAIL and c.kind == "setup" for c in checks):
        verdict = "SETUP_INCOMPLETE"
    elif any(c.status == FAIL for c in checks if c.kind == "retention"):
        # An affirmative violation outranks an unknown link: if the chain is broken we say so, rather than
        # hiding a proven break behind "we could not tell".
        verdict = "RETENTION_FAIL"
    elif any(c.status == UNKNOWN for c in checks):
        verdict = "INSUFFICIENT_EVIDENCE"
    else:
        verdict = "RETENTION_PASS"
    return Analysis(verdict, checks, episode, evidence, limitations, concessions)


def report_dict(analysis: Analysis, identity: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {
        "verdict": analysis.verdict,
        "identity": identity,
        "checks": [{"name": c.name, "kind": c.kind, "status": c.status, "link": c.link,
                    "detail": c.detail} for c in analysis.checks],
        "evidence": analysis.evidence,
        "limitations": analysis.limitations,
        "concessions": analysis.concessions,
    }
    if analysis.episode is not None:
        e = analysis.episode
        out["episode"] = {
            "slot_a": e.slot_a, "window_open_us": e.window_open_us,
            "window_open_loop_len_ticks": e.window_open_loop_len,
            "capacity_release_us": e.capacity_release_us,
            "capacity_denied_window_us": e.window_us,
            "capacity_denied_window_ms": round(e.window_us / 1000.0, 1),
        }
    return out


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description="S4f.3 retention oracle over a managed HITL capture log")
    p.add_argument("--session-log", type=Path, required=True)
    p.add_argument("--base-report", type=Path, default=None)
    p.add_argument("--metadata", type=Path, default=None)
    p.add_argument("--content-review", type=Path, default=None)
    p.add_argument("--out", type=Path, default=None)
    p.add_argument("--slot-a", type=int, default=0)
    p.add_argument("--slot-b", type=int, default=1)
    p.add_argument("--slot-c", type=int, default=2)
    p.add_argument("--track-b", type=int, default=0)
    p.add_argument("--tempo-bpm", type=float, default=120.0)
    p.add_argument("--min-window-us", type=int, default=DEFAULT_MIN_WINDOW_US)
    p.add_argument("--followup-us", type=int, default=DEFAULT_FOLLOWUP_US)
    p.add_argument("--expect-successor-events", type=int, default=None)
    p.add_argument("--allow-setup-issue", action="append", default=[])
    p.add_argument("--accept-slot-scoped-bind", action="store_true")
    p.add_argument("--accept-event-count-only", action="store_true")
    args = p.parse_args(argv)

    text = args.session_log.read_text(errors="replace")
    base_report_path = args.base_report or _find_base_report(args.session_log)
    metadata_path = args.metadata or (args.session_log.parent / "metadata.json")
    analysis = analyse(
        text, slot_a=args.slot_a, slot_b=args.slot_b, slot_c=args.slot_c, track_b=args.track_b,
        tempo_bpm=args.tempo_bpm, min_window_us=args.min_window_us, followup_us=args.followup_us,
        expect_successor_events=args.expect_successor_events,
        base_report=_read_json(base_report_path), metadata=_read_json(metadata_path),
        content_review=_read_json(args.content_review),
        allow_setup_issues=tuple(args.allow_setup_issue),
        accept_slot_scoped_bind=args.accept_slot_scoped_bind,
        accept_event_count_only=args.accept_event_count_only,
    )
    payload = report_dict(analysis, _load_identity(metadata_path))
    out = args.out or (args.session_log.parent / "retention-review.json")
    out.write_text(json.dumps(payload, indent=2) + "\n")

    print(f"retention verdict: {analysis.verdict}")
    if "episode" in payload:
        ep = payload["episode"]
        print(f"  capacity-denied window: {ep['capacity_denied_window_ms']} ms (slot {ep['slot_a']})")
    for c in analysis.checks:
        mark = {OK: "ok  ", FAIL: "FAIL", UNKNOWN: "????"}[c.status]
        print(f"  [{mark}] ({c.kind}{'/' + c.link if c.link else ''}) {c.name}: {c.detail}")
    for lim in analysis.limitations:
        print(f"  limitation: {lim}")
    for con in analysis.concessions:
        print(f"  CONCESSION: {con}")
    print(f"  review: {out}")
    return analysis.exit_code


def _load_identity(metadata_path: Optional[Path]) -> dict[str, Any]:
    data = _read_json(metadata_path) or {}
    return {k: data.get(k) for k in ("git_commit", "firmware_hex_sha256")}


if __name__ == "__main__":
    raise SystemExit(main())
