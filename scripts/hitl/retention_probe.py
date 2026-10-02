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

plus `RETENTION_NOT_EXERCISED` (2), `RETENTION_FAIL` (2), `RETENTION_PARTIAL` (5) and `RETENTION_PASS` (0).

`RETENTION_PASS` is reserved for the **complete, unconceded** chain. Any link accepted on a weaker,
operator-conceded basis yields `RETENTION_PARTIAL` instead: a partial result is explicitly *not* full
acceptance and an automated consumer must not close the device gate on it. A conceded result can never exit 0.

Telemetry used, all existing and slot/identity-bearing where the chain needs it:

    DIAG,reentry_window_open,<slot>,<stopTick>,<loopLengthTicks>   DEC-067 successor_start_window_open
    DIAG,canonical_release,<slot>,<generation>,<loopLengthTicks>,<chunks>  canonical_finalization_capacity_released
    DIAG,reentry,<decision>,<tick>,<interval>,<slot>,<depth>       the denial, from the decision owner
    ODUB,stop,admission,...,successor_stop_denied_capacity         the denial's stop-side stage record
    DIAG,stop_phase|stop_grain,<track>,<slot>,...                  the deferred stop transaction, per slot
    RECA,<slot>,<tick>                                             the capture start
    RECS,stop,<slot>,... / RECS,stage,seal,...,<eventCount>,...     the sealed take and its event count

**Known telemetry gap, reported rather than papered over.** `captureInterval_` -- the identity the denial
names -- is emitted by `reentryDecision` and, since the logging fix, on the denial's timely human row
(`Overdub stop refused: <reason> slot=<n> interval=<n>`); the human row has no device timestamp, so the
device-time ordering anchor still comes from the `#CAP` row. Nothing names it when a stop later **binds**: the stop-tier
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

**Every slot and track argument is 0-based, exactly as the firmware's own records name them** -- which is
NOT how the HITL driver's CLI numbers them. The driver takes 1-based slots and tracks (its `--slot-b 5` is
this oracle's `--slot-b 4`, its `--track 8` is `--track-b 7`); passing the driver's numbers here judges a
correct run as having no denial and no retained request at all (measured on device 2026-09-27).
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

from hitl.content_deduction import validate_deduction
from hitl.lease_release import WS_RELEASE_RE

CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
WINDOW_OPEN_RE = re.compile(r"^DIAG,reentry_window_open,(\d+),(\d+),(\d+)$")
# Wire form: DIAG,canonical_release,<slot>,<generation>,<loopLengthTicks>,<availableChunks>. The slot and
# generation name the RETIRING owner -- not the currently active slot -- so this is the record that ties a
# successor's admission back to the predecessor that freed the capacity.
CANONICAL_RELEASE_RE = re.compile(r"^DIAG,canonical_release,(\d+),(\d+),(\d+),(\d+)$")
REENTRY_RE = re.compile(r"^DIAG,reentry,([a-z_]+),(\d+),(\d+),(\d+),(\d+)$")
# Wire form: DIAG,ws_release,<slot>,<generation>,<outcome>. This names the **working-set lease** release,
# which `canonical_release` does not: that row reports the retiring *occupancy*. Held separately and
# reported rather than gating, because the release path only fires where a sweep or pressure runs it -- a
# same-track A/B/C sequence excludes its own track from the take-start sweep. `hitl.lease_release` owns the
# row's wire form and its reading; this oracle keeps only its own reporting policy.
RECA_RE = re.compile(r"^RECA,(\d+),(\d+)$")
# Exactly the documented successful stop kinds, not an open `stop*` pattern: `TrackCaptureStopCommit` emits
# `"stop"` (the ordinary route) and `"stopToStopped"` (the transport-stop route). Anything else is not a
# successful stop this oracle knows how to read.
# Fields: kind, slot, tick, startTick, rawLength, finalLength, captureAlignFlag.
RECS_STOP_RE = re.compile(r"^RECS,(?:stop|stopToStopped),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)$")
RECS_SEAL_RE = re.compile(r"^RECS,stage,seal,")
STOP_SLOT_RE = re.compile(r"^DIAG,(?:stop_phase|stop_grain),(\d+),(\d+),")
DENIAL_STAGE_RE = re.compile(r"^ODUB,stop,admission,.*,successor_stop_denied_capacity$")
# The denial's own human row carries its identity since the logging fix: the deferred `#CAP` row can be
# dropped by the timing-critical shed, and this row is the timely edge for the same admission decision.
#
# Base convention, and it is a trap: the firmware's `slot=` fields are **0-based** (matching the `#CAP`
# identity and the census), while this oracle's `--slot-b` is the 1-based number the CLI and the action layer
# use. Verified on device 2026-09-27 -- the denial that refused the stop for the slot the driver selected as
# its 1-based slot 5 logged `slot=4`, and "Loop 5: Stop Overdub" named the same press. The conversion
# therefore lives here, and only here, exactly as `hitl_retention_handoff.cleared_slot_from_lines` does it for
# the clear identity: comparing the raw values judges every correct run a mismatch.
HUMAN_DENIAL_ID_RE = re.compile(r"Overdub stop refused:.* slot=(\d+) interval=(\d+)")
TEMPO_FEEDBACK_RE = re.compile(r"Sent tempo CC feedback: ([0-9.]+) BPM")
TEMPO_APPLIED_RE = re.compile(r"TEMPO: CC=\d+ -> ([0-9.]+) BPM")
# The firmware logs an applied tempo only when it changes; an already-correct value is reported as
# "Tempo unchanged: CC=<n> maps to current <bpm> BPM", which is equally a read-back of the current tempo.
TEMPO_UNCHANGED_RE = re.compile(r"Tempo unchanged: CC=\d+ maps to current ([0-9.]+) BPM")
RETAINED_LOG_RE = re.compile(r"Track (\d+) loop (\d+): switch and record start retained")
INVARIANT_VIOLATION_MARKERS = (
    "record activation invariant violated",
    "record activation violated its invariant",
)
OVERFLOW_MARKERS = ("RING,overflow", "RING,tier_a_evicted")

CC_BEGIN_RE = re.compile(r"^CAPTURE_CONTENT,begin,track=(\d+),slot=(\d+),length=(\d+)$")
CC_EVENT_RE = re.compile(r"^CAPTURE_CONTENT,event,([a-z]+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)$")
# `CAPTURE_CONTENT,rows,<kind>,<pass>,<count>` -- the declared row count per group, so a dump can be checked
# for internal completeness rather than trusted.
CC_ROWS_RE = re.compile(r"^CAPTURE_CONTENT,rows,([a-z]+),(\d+),(\d+)$")
CC_DENIED = "CAPTURE_CONTENT,denied"

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
    # The observed interval from the window opening to the successor's denial, i.e. the lower bound the
    # exposure gate reads. None when no device-timestamped denial exists to bound the window.
    bounded_us: Optional[int] = None


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
        return {"RETENTION_PASS": 0, "RETENTION_PARTIAL": 5, "RETENTION_FAIL": 2,
                "RETENTION_NOT_EXERCISED": 2, "SETUP_INCOMPLETE": 3,
                "INSUFFICIENT_EVIDENCE": 4}[self.verdict]


# The logger prefixes human rows with the DEVICE time in seconds -- `[58.476]` -- which is the same clock the
# `#CAP` rows carry in microseconds. The human rows are the timely edge (the firmware documents that `#CAP`
# rows queue behind the deferred stop's flood and arrive late, and sometimes do not arrive at all), so a check
# that needs an edge prefers the `#CAP` row and falls back to the human row's own timestamp rather than
# reporting absence.
HUMAN_TS_RE = re.compile(r"^\[(\d+(?:\.\d+)?)\]")


def human_us(line: str) -> Optional[int]:
    m = HUMAN_TS_RE.match(line.strip())
    return None if m is None else int(round(float(m.group(1)) * 1_000_000))


def human_line(lines: list[str], marker: str) -> Optional[tuple[int, str]]:
    """The (host-micros, raw text) of the first human log line containing `marker`.

    `human_entry` deliberately keeps only the marker, which is enough for a timestamp-only fallback. An
    identity fallback needs the text itself (the denial now names its slot and interval on the human row).
    """
    for line in lines:
        if marker in line:
            us = human_us(line)
            if us is not None:
                return us, line.strip()
    return None


def human_entry(lines: list[str], marker: str) -> Optional[Entry]:
    hit = human_line(lines, marker)
    return None if hit is None else Entry(hit[0], f"HUMAN {marker}")


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


def analyse_content_dump(text: str, *, slot_c: int, expected_notes: tuple[int, ...]) -> dict[str, Any]:
    """Read the content pipeline's `!CAPTURE_CONTENT` dump: which slot it dumped, and which notes it holds.

    Field layout (firmware `Track::printStoppedCaptureContentForHitl`):
        CAPTURE_CONTENT,begin,track=<midiChannel>,slot=<activeLoopIndex>,length=<ticks>
        CAPTURE_CONTENT,event,<kind>,<pass>,<tick>,<type>,<channel>,<note>,<velocity>,<noteId>

    The `begin` row names the slot that was dumped, and each event carries its real pitch, so unlike the
    seal's `eventCount` this can show that the successor slot holds the *expected* notes. Note the oddity,
    recorded rather than relied on: the `track=` field is emitted from `midiChannel`, not a track index.
    """
    out: dict[str, Any] = {"present": False, "denied": False, "slot": None, "length": None,
                           "notes": [], "note_ons": [], "expected_notes": list(expected_notes),
                           "expected_absent": list(expected_notes), "notes_match": None}
    if CC_DENIED in text:
        out["denied"] = True
    notes: list[int] = []
    ons: list[int] = []
    # Full MIDI-relevant fields, not just (tick, type, pitch): channel and velocity are part of what the loop
    # stores and what parity must cover.
    per_kind: dict[str, list[tuple[int, int, int, int, int]]] = {}
    declared_counts: dict[tuple[str, int], int] = {}
    for line in text.splitlines():
        line = line.strip()
        m = CC_BEGIN_RE.match(line)
        if m:
            out["present"] = True
            out["slot"] = int(m.group(2))
            out["length"] = int(m.group(3))
            continue
        m = CC_ROWS_RE.match(line)
        if m:
            declared_counts[(f"{m.group(1)}#{m.group(2)}")] = int(m.group(3))
            continue
        m = CC_EVENT_RE.match(line)
        if m:
            kind, kidx, _tick, etype, _ch, note, velocity, _nid = m.groups()
            per_kind.setdefault(f"{kind}#{kidx}", []).append(
                (int(_tick), int(etype), int(_ch), int(note), int(velocity)))
            notes.append(int(note))
            # type 144 = NoteOn (3 << 4); a NoteOn with velocity 0 is a NoteOff in disguise.
            if int(etype) == 144 and int(velocity) > 0:
                ons.append(int(note))
    out["notes"] = sorted(set(notes))
    out["note_ons"] = sorted(set(ons))
    # The dump carries the loop's own storage rows (`record`/`overdub`) and its materialised `effective` rows,
    # so storage<->effective parity for THAT loop is readable directly from it. This matters because the
    # content pipeline's review compares a one-loop dump against the *whole* audible output, which cannot hold
    # when other slots or tracks are enabled (measured: the playback carried dozens of unrelated notes while
    # the successor's dump declared exactly one).
    storage_rows = [e for k, rows in per_kind.items()
                    if k.startswith(("record#", "overdub#")) for e in rows]
    effective_rows = per_kind.get("effective#0", [])
    out["storage_note_ons"] = sorted({n for (_t, ty, _c, n, _v) in storage_rows if ty == 144})
    out["effective_note_ons"] = sorted({n for (_t, ty, _c, n, _v) in effective_rows if ty == 144})
    # Storage is emitted pass-grouped, while effective content is emitted in the
    # merged playback order. Compare canonical event tuples, not producer order.
    out["storage_effective_match"] = (
        sorted(storage_rows) == sorted(effective_rows)
    ) if effective_rows else None
    # A committed overlap-resolve **Hides** a covered note (or shortens it) instead of rewriting storage, so the two
    # views legitimately differ where a later producer covers an earlier note of the same pitch. That deduction is
    # validated and named (`hitl.content_deduction`), never waved through: `resolved_storage_match` is True only when
    # every difference is accounted for by a covering row.
    out["deduction"] = validate_deduction(per_kind=per_kind, loop_length=out["length"] or 0)
    out["resolved_storage_match"] = (
        out["deduction"]["accounted"] and out["deduction"]["checked"]
    ) if effective_rows else None
    # A dump is only usable if it is a COMPLETE block and its declared per-group counts match what parsed.
    out["complete"] = "CAPTURE_CONTENT,end" in text
    parsed_counts = {k: len(v) for k, v in per_kind.items()}
    out["declared_counts_match"] = bool(declared_counts) and all(
        parsed_counts.get(k, 0) == n for k, n in declared_counts.items())
    out["declared_counts"] = {k: n for k, n in sorted(declared_counts.items())}
    if expected_notes:
        absent = [n for n in expected_notes if n not in ons]
        out["expected_absent"] = absent
        out["notes_match"] = not absent
    return out


def analyse_content_review(review: Optional[dict]) -> dict[str, Any]:
    """Read `content-review.json`: storage<->effective parity per playback cycle."""
    out: dict[str, Any] = {"present": review is not None, "result": None, "parity_ok": None,
                           "storage_matches_effective": None}
    if review is None:
        return out
    out["result"] = review.get("result")
    out["storage_matches_effective"] = review.get("storage_matches_effective")
    cycles = review.get("cycles") if isinstance(review.get("cycles"), list) else []
    flags = [c.get("inventory_equal") for c in cycles] or [review.get("inventory_equal")]
    out["parity_ok"] = bool(flags) and all(f is True for f in flags)
    return out


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
    content_dump: Optional[str] = None,
    expect_successor_notes: tuple[int, ...] = (),
    allow_setup_issues: tuple[str, ...] = (),
    accept_slot_scoped_bind: bool = False,
    accept_event_count_only: bool = False,
) -> Analysis:
    entries = parse_entries(session_log)
    lines = session_log.splitlines()
    checks: list[Check] = []
    evidence: dict[str, list[str]] = {k: [] for k in
                                      ("window_open", "canonical_release", "working_set_lease_release", "denial", "retained_request",
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
        if issues:
            concessions.append(
                f"base-report issues allowlisted as setup artefacts (operator claim, not evidence): {issues}")
            add("reported_issues_resolved", OK,
                f"every base-report issue is explicitly allowlisted ({issues}) -- recorded as an **operator "
                "claim** that each is a setup artefact. This is not evidence and nothing here proves the "
                "cause; the verdict is therefore PARTIAL at best.", "setup")
        else:
            add("reported_issues_resolved", OK, "base report lists no issues", "setup")

    tempos = [float(m.group(1)) for m in (TEMPO_FEEDBACK_RE.search(l) or TEMPO_APPLIED_RE.search(l)
                                          or TEMPO_UNCHANGED_RE.search(l) for l in lines) if m]
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
    seed_final = int(RECS_STOP_RE.match(seed.body).group(5)) if seed is not None else 0
    # A stop record is not proof that content landed -- the successor proved that: it produced
    # `RECS,stopToStopped,6,...,4001,0,0`, a documented successful stop kind with a ZERO final length. The seed
    # check therefore requires a nonzero final length, not merely a row.
    if seed is None:
        add("seed_content_on_a", FAIL,
            f"no documented stop record (stop|stopToStopped) for slot {slot_a}: the seed take never landed",
            "setup")
    elif seed_final == 0:
        add("seed_content_on_a", FAIL,
            f"slot {slot_a} has a stop record but a ZERO final length ({seed.body}) -- a stop record alone is "
            "not proof that content landed", "setup")
    else:
        add("seed_content_on_a", OK,
            f"{seed.body} at {seed.us} us (final length {seed_final} ticks)", "setup")

    window_open = next((e for e in entries
                        if (m := WINDOW_OPEN_RE.match(e.body)) and int(m.group(1)) == slot_a), None)
    window_open_fallback = False
    if window_open is None:
        window_open = human_entry(lines, f"Reentry window open slot={slot_a}")
        window_open_fallback = window_open is not None
    if window_open is None:
        add("L1_finalizer_outstanding", FAIL,
            f"neither a DIAG,reentry_window_open,{slot_a} row nor the timely human 'Reentry window open "
            f"slot={slot_a}' row is present: A's deferred stop never froze its interval, so the "
            "capacity-denied state cannot be staged", "setup", "L1")
        return Analysis("SETUP_INCOMPLETE", checks, None, evidence, limitations, concessions)
    evidence["window_open"].append(
        f"{window_open.us}: {window_open.body}"
        + ("  (from the timely human row; the deferred #CAP row is absent)" if window_open_fallback else ""))
    add("L1_finalizer_outstanding", OK, f"{window_open.body} at {window_open.us} us", "retention", "L1")

    m_open = WINDOW_OPEN_RE.match(window_open.body)
    # The human window row names the slot but not the loop length (the `#CAP` row does). Zero means "not
    # carried by the row that supplied this edge", and it is reported as such rather than pretended.
    window_open_loop_len = int(m_open.group(3)) if m_open is not None else 0
    release = _first_after(entries, window_open.us - 1,
                           lambda e: (mm := CANONICAL_RELEASE_RE.match(e.body)) is not None
                           and int(mm.group(1)) == slot_a)
    if release is None:
        release = human_entry(lines, f"Reentry released slot={slot_a}")
    if release is None:
        add("L4_capacity_released", FAIL,
            f"no DIAG,canonical_release,{slot_a} after A's window opened: A never retired", "setup", "L4")
        return Analysis("SETUP_INCOMPLETE", checks, None, evidence, limitations, concessions)
    evidence["canonical_release"].append(f"{release.us}: {release.body}")
    m_rel = CANONICAL_RELEASE_RE.match(release.body)
    if m_rel is None:
        # `Reentry released slot=4 generation=40` -- the timely human form carries the same identity.
        m_h = re.search(r"Reentry released slot=(\d+) generation=(\d+)", release.body)
        if m_h is not None:
            add("L4_release_names_the_retiring_owner", OK,
                f"slot {m_h.group(1)}, generation {m_h.group(2)} (from the timely human row; the deferred "
                "#CAP row is absent)", "retention", "L4")
    if m_rel is not None:
        add("L4_release_names_the_retiring_owner", OK,
            f"slot {m_rel.group(1)}, generation {m_rel.group(2)}, loop {m_rel.group(3)} ticks, "
            f"{m_rel.group(4)} chunks free", "retention", "L4")
    window_us = release.us - window_open.us
    # The window WIDTH is not gated here, and the reason is measured rather than stylistic: the release edge
    # is `canonical_finalization_capacity_released`, emitted by the deferred-maintenance turn that services
    # the `canonicalReleaseMarker` -- so `window_us` is an **upper bound** on how long the window stayed open,
    # not the closure instant, and the release turn can service several slots at once (measured on device
    # 2026-09-27: two retirements released 48 us apart in one turn, giving 34.8 ms for a window that a
    # successor's denial had been staged inside 513 ms earlier). Gating on it would fail a run whose staging
    # demonstrably worked, and neither log row marks the closure, so the width this quantity reports is not
    # usable evidence in either direction. The bounded check follows L2, once the denial is identified.
    add("L4_capacity_released", OK,
        f"{release.body} at {release.us} us -- names the retiring owner. This is the first maintenance turn "
        "at which the resources were observed free, not the availability edge or the window closure, so a bind "
        "earlier than this record is expected (the bind's own admission guard is what proves availability), "
        "and the window width it implies is an upper bound only.", "retention", "L4")

    def in_window(us: int) -> bool:
        return window_open.us < us < release.us

    # ---------------- L2: B's identified stop denied ----------------
    denials = [e for e in entries
               if (m := REENTRY_RE.match(e.body)) and m.group(1) in ("deny", "deny_resource")
               and int(m.group(4)) == slot_b]
    staged_denials = [e for e in denials if in_window(e.us)]
    stage_lines = [e for e in entries if DENIAL_STAGE_RE.match(e.body) and in_window(e.us)]
    for e in denials:
        evidence["denial"].append(f"{e.us}: {e.body}{'' if in_window(e.us) else '  (outside the window)'}")

    human_at_us: Optional[int] = None
    human_text: Optional[str] = None
    human_denied = False
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
        # The denial has a timely human row. Since the logging fix that row names its slot and interval, so
        # it can now (a) prove the denial was for a DIFFERENT slot -- a real failure, not a gap -- and
        # (b) report the identity even when the device-timestamped row is absent. It is deliberately NOT
        # promoted to the L2 pass here: L5 orders the bind against `denial.us` in DEVICE micros, while this
        # row's timestamp is host milliseconds, and silently substituting one base for the other would
        # corrupt the very ordering L5 exists to prove. The `#CAP` row is Tier-A now, so it should be present.
        hit = human_line(lines, "Overdub stop refused")
        if hit is not None:
            human_denied = True
            human_at_us, human_text = hit
            m = HUMAN_DENIAL_ID_RE.search(human_text)
            # The row's slot is 0-based (see HUMAN_DENIAL_ID_RE); `--slot-b` is 1-based.
            denied_slot_1based = int(m.group(1)) + 1 if m is not None else None
            if m is not None and denied_slot_1based != slot_b:
                add("L2_denial_identified_for_b", FAIL,
                    f"the denial names slot {denied_slot_1based} (the row's own 0-based field is "
                    f"{m.group(1)}) with interval {m.group(2)}, but B holds slot {slot_b}: a different slot's "
                    "stop was denied, so B's stop was not the one refused", "retention", "L2")
            elif m is not None:
                add("L2_denial_identified_for_b", UNKNOWN,
                    f"the timely human denial row names B's slot {slot_b} (the row's own 0-based field is "
                    f"{m.group(1)}) and interval {m.group(2)} ({human_text[:120]} at {human_at_us} us), but "
                    "the device-timestamped DIAG,reentry,deny,<tick>,<interval>,<slot>,<depth> row is absent "
                    "from this log, so the denial cannot be placed in device time; L5's ordering needs that "
                    "anchor", "evidence", "L2")
            else:
                add("L2_denial_identified_for_b", UNKNOWN,
                    f"the denial happened ({human_text[:120]} at {human_at_us} us) but its human row carries "
                    "no slot or interval (pre-identity firmware), and the identity-grade "
                    "DIAG,reentry,deny row is absent: which slot/interval was denied is not established",
                    "evidence", "L2")
        else:
            human_denied = False
            add("L2_denial_identified_for_b", FAIL,
                f"no denial for slot {slot_b} inside A's finalization window ({len(denials)} denial(s) "
                "elsewhere): the capacity-denied state was never entered", "retention", "L2")

    # ---------------- the exposure gate: what the window width CAN prove ----------------
    # The successor's denial is the point at which the run stops being staged and starts being exercised, so
    # the interval from A's window opening to that denial is a **lower bound** on how long the window stayed
    # open -- it is directly observed, and no deferred turn can inflate it. The release-turn upper bound
    # (`window_us`) is reported beside it for the width's order of magnitude and never gates on its own,
    # because a late maintenance turn makes it read arbitrarily small (see L4 above).
    bounded_us: Optional[int] = None
    if denial is not None:
        # The device-timestamped denial is the bounded edge, so this is a real floor verdict -- and on a real
        # FAIL the run is genuinely unusable evidence, which is what `setup` asserts.
        bounded_us = denial.us - window_open.us
        if bounded_us >= min_window_us:
            add("capacity_denied_window_wide_enough", OK,
                f"the window was open at least {bounded_us} us ({bounded_us/1000:.1f} ms) -- from A's window "
                f"opening to the successor's denial (device timestamp) -- against a "
                f"{min_window_us} us floor; the successor's stop was denied against it, which bounds it below. "
                f"The release-turn upper bound is {window_us} us ({window_us/1000:.1f} ms), which is a "
                "servicing latency, not the closure", "setup", "L1")
        else:
            add("capacity_denied_window_wide_enough", FAIL,
                f"the window was open for at most {bounded_us} us ({bounded_us/1000:.1f} ms) before the "
                f"successor's denial, against a {min_window_us} us floor: too narrow to stage two host "
                "gestures -- an exposure problem, not a retention result", "setup", "L1")
    elif human_denied:
        # No device-timestamped denial. The bounded edge would be the host-timestamped human row, and that
        # arithmetic is INVALID: the human row's prefix is host seconds since the capture started while
        # `window_open.us` is device `micros()` since boot, so the difference is not a duration at all
        # (measured: it computes a negative window). The codebase already refuses to mix the two bases for
        # L5's ordering; the same refusal applies here, so the width is reported as ungated and L2 has
        # already reported the missing row that would anchor it.
        add("capacity_denied_window_wide_enough", UNKNOWN,
            f"the window width cannot be gated: the bounded edge is the successor's denial, and its only row "
            f"here is host-timestamped, which shares no time base with the device's window-open record (host "
            f"seconds since capture start vs device micros since boot). The release-turn upper bound is "
            f"{window_us} us, which is a servicing latency rather than the closure, so it does not gate "
            "either. L2 reports the missing device-timestamped denial row", "evidence", "L1")
    elif human_at_us is None and denial is None:
        # No denial was identified at all: L2 has already failed for that reason, so no floor check is added
        # (a second failure for the same cause would only make the report harder to read).
        pass

    episode = Episode(slot_a, window_open.us, window_open_loop_len, release.us, window_us, bounded_us)

    if denial is None:
        # Distinguish "never entered" from "entered but unidentifiable" -- and never let either of those
        # mask a failed setup, which would report a run that is not evidence as a behavioural result.
        if any(c.status == FAIL and c.kind == "setup" for c in checks):
            verdict = "SETUP_INCOMPLETE"
        elif stage_lines or human_denied:
            verdict = "INSUFFICIENT_EVIDENCE"
        else:
            verdict = "RETENTION_NOT_EXERCISED"
        return Analysis(verdict, checks, episode, evidence, limitations, concessions)

    # ---------------- S4.1: the working-set lease release, by name ----------------
    # Reported, not gating. `canonical_release` (L4) is the retiring *occupancy*; this is the lease. The
    # release path runs from a take-start sweep (which excludes the capturing track), the memory-pressure
    # release, or the playback-reset/undo path -- so a same-track sequence may legitimately show none, and
    # a missing row here means "this run did not exercise it", not "the release did not happen".
    lease_releases = []
    for e in entries:
        m = WS_RELEASE_RE.match(e.body)
        if m:
            lease_releases.append((int(m.group(1)), int(m.group(2)), m.group(3), e))
            evidence["working_set_lease_release"].append(f"{e.us}: {e.body}")
    released_slots = sorted({slot for slot, _gen, outcome, _e in lease_releases if outcome == "released"})
    if lease_releases:
        refused = [f"slot {slot} ({outcome})" for slot, _gen, outcome, _e in lease_releases
                   if outcome != "released"]
        limitations.append(
            f"the working-set lease release IS observed by name in this run (`DIAG,ws_release`): released "
            f"slot(s) {released_slots or 'none'}"
            + (f"; refusals reported rather than discarded: {', '.join(refused)}" if refused else "")
            + ". This is the lease, which `canonical_release` does not report.")
    else:
        limitations.append(
            "the working-set lease release is NOT observed by name in this run: no `DIAG,ws_release` row. "
            "That row is emitted only where the release path fires -- a take-start sweep (which excludes the "
            "capturing track, so a same-track sequence never sweeps its own slots), the memory-pressure "
            "release, or the playback-reset/undo path. Its absence means this run did not exercise it, not "
            "that the release failed; the release remains exercised natively (S4.3's reuse fixture).")

    # ---------------- L3: C's request retained ----------------
    # The retained-request row is a human log line, not a #CAP row, so it carries no device timestamp and
    # its ordering is resolved positionally. It must name the track *and* the target slot, or it cannot be
    # tied to this request: the earlier form named only the 1-based loop number, which is ambiguous across
    # tracks. `Track %d` uses the manager's 0-based index, matching the other CAT_TRACK rows.
    retained_hits = []
    for line in lines:
        m = RETAINED_LOG_RE.search(line)
        if m:
            retained_hits.append((int(m.group(1)), int(m.group(2)), line.strip()))
    retained_for_c = [h for h in retained_hits if h[1] == slot_c + 1 and h[0] == track_b]
    for track_i, slot1, line in retained_hits:
        evidence["retained_request"].append(f"track {track_i} loop {slot1}: {line[:140]}")

    starts = [e for e in entries if (m := RECA_RE.match(e.body)) and int(m.group(1)) == slot_c]
    for e in starts:
        evidence["successor_start"].append(f"{e.us}: {e.body}")
    pre_release_starts = [e for e in starts if e.us < episode.capacity_release_us]

    # The retained log row carries no timestamp of its own; it must sit between the denial and the release,
    # which is checked textually by ordering the log lines.
    # Order the retained row against the TIMELY human denial line, not the `#CAP` denial row: `#CAP` rows are
    # deferred and flush late, so their file order does not reflect their order in time, while both this row
    # and the retained row are human rows at the same latency class.
    denial_line = next((i for i, l in enumerate(lines) if "Overdub stop refused" in l), None)
    if denial_line is None:
        denial_line = next((i for i, l in enumerate(lines) if "DIAG,reentry,deny" in l
                            and f",{slot_b}," in l), None)
    release_line = next((i for i, l in enumerate(lines) if CANONICAL_RELEASE_RE.match(
        (CAP_RE.match(l.strip()).group(2) if CAP_RE.match(l.strip()) else ""))), None)
    retained_line = next((i for i, l in enumerate(lines)
                          if (m := RETAINED_LOG_RE.search(l)) and int(m.group(2)) == slot_c + 1
                          and int(m.group(1)) == track_b), None)
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
            f"no unambiguous retained-request row for track {track_b} / slot {slot_c} (1-based "
            f"{slot_c + 1}). Retention is asserted only by that human log line, so it must name both the "
            "track and the slot; otherwise this link stays insufficient", "evidence", "L3")
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
        "L5 is identity-grade: the bind is read from `DIAG,reentry,bound,<tick>,<interval>,<slot>,<depth>`, "
        "which names the same `captureInterval_` the denial names. (The earlier claim that nothing names that "
        "identity on the binding edge is withdrawn -- it described the state before this change added the "
        "`bound` record.) The *release* marker is separate and is NOT the availability edge: it is emitted on "
        "the first maintenance turn at which the resources were observed free, which is measurably later than "
        "the admission it enables.")
    bound_identity = [e for e in entries
                      if (m := REENTRY_RE.match(e.body)) and m.group(1) == "bound"
                      and int(m.group(4)) == slot_b and int(m.group(3)) == interval_b]
    # The requirement is that the denied stop BOUND after the denial (and after A's window opened). It is
    # deliberately not ordered against `canonical_release`: that record is the first maintenance turn at which
    # the resources were observed free, measured 25 ms *after* B had already bound, so a bind preceding the
    # release *record* is expected rather than a contradiction.
    bound_identity_after = [e for e in bound_identity
                            if e.us > denial.us and e.us > episode.window_open_us]
    bound_identity_before = [e for e in bound_identity
                             if e.us <= denial.us or e.us <= episode.window_open_us]
    for e in bound_identity:
        evidence["stop_bind"].append(f"{e.us}: {e.body}")

    if bound_identity_after:
        add("L5_same_b_stop_bound", OK,
            f"{bound_identity_after[0].body} at {bound_identity_after[0].us} us names the **denied interval** "
            f"{interval_b} on slot {slot_b}, after the denial at {denial.us} us", "retention", "L5")
    elif bound_identity_before:
        add("L5_same_b_stop_bound", FAIL,
            f"interval {interval_b} on slot {slot_b} bound at {bound_identity_before[0].us} us, at or before "
            f"the denial at {denial.us} us: the stop bound without a denied prerequisite", "retention", "L5")
    elif after_release:
        detail = (f"slot-scoped bind evidence for slot {slot_b} after the release: {after_release[0].body} "
                  f"at {after_release[0].us} us -- but not tied to interval {interval_b}; the identity-grade "
                  "DIAG,reentry,bound record carrying the request's interval is absent")
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

    dump = analyse_content_dump(content_dump or "", slot_c=slot_c,
                                expected_notes=expect_successor_notes)
    review = analyse_content_review(content_review)
    limitations.append(
        "L7 note-level evidence: `storedNoteEvent`/`storedNotes` have no callers, so the capture has no "
        "per-note record of its own. Pitch-level proof therefore comes from the content pipeline's "
        "`!CAPTURE_CONTENT` dump (`--content-dump`), whose `begin` row names the dumped slot and whose events "
        "carry real pitches, plus `content-review.json` (`--content-review`) for storage<->effective parity. "
        "Note the dump's own oddity, which is why the slot is read from `begin` and not from `track=`: that "
        "field is emitted from `midiChannel`, not a track index.")

    if dump["present"] and dump["slot"] != slot_c:
        add("L7_successor_recorded_expected_midi", FAIL,
            f"the content dump is for slot {dump['slot']}, not the successor slot {slot_c}: it cannot speak "
            "for the successor's take", "retention", "L7")
    elif dump["present"] and expect_successor_notes and dump["notes_match"] is False:
        add("L7_successor_recorded_expected_midi", FAIL,
            f"the content dump for slot {dump['slot']} does not contain the expected note(s) "
            f"{dump['expected_absent']} (note-ons present: {dump['note_ons']})", "retention", "L7")
    elif dump["present"] and not expect_successor_notes:
        add("L7_successor_recorded_expected_midi", UNKNOWN,
            f"a content dump for slot {dump['slot']} is present with note-ons {dump['note_ons']}, but no "
            "expected note was given (--expect-successor-note), so 'the expected MIDI' is undefined here",
            "evidence", "L7")
    elif dump["present"] and not dump["complete"]:
        add("L7_successor_recorded_expected_midi", FAIL,
            "the content dump has no `CAPTURE_CONTENT,end`, so it is not a complete block and cannot be "
            "evidence", "retention", "L7")
    elif dump["present"] and not dump["declared_counts_match"]:
        add("L7_successor_recorded_expected_midi", FAIL,
            f"the content dump's declared group counts do not match what parsed: declared "
            f"{dump['declared_counts']} -- an incomplete dump is not evidence", "retention", "L7")
    elif (dump["present"] and dump["notes_match"] and dump["storage_effective_match"] is True):
        # The successor's own loop: the expected notes are present in BOTH its storage rows and its
        # materialised rows, and the two agree. That is storage<->effective parity for the loop under test,
        # read from the dump itself rather than from a whole-playback comparison that cannot hold while other
        # slots are audible.
        extra_review = ""
        if review["present"]:
            extra_review = (f"; content review additionally reports inventory_equal="
                            f"{review['parity_ok']} over the whole playback, which layers the other enabled "
                            "slots and so is not a statement about the successor alone")
        add("L7_successor_recorded_expected_midi", OK,
            f"complete dump for slot {dump['slot']} (declared counts {dump['declared_counts']} match parsed) "
            f"declares the expected note(s) {list(expect_successor_notes)} in both storage and effective "
            f"rows, and the two agree on the full MIDI fields compared -- tick, type, channel, note, velocity "
            f"(storage note-ons {dump['storage_note_ons']}, effective {dump['effective_note_ons']}). This is "
            "loop-scoped committed/effective parity for the successor; it is NOT MIDI-output playback parity, "
            "which would require the other enabled slots and tracks to be silent"
            + extra_review, "retention", "L7")
    elif dump["present"] and dump["notes_match"] and dump["storage_effective_match"] is False:
        add("L7_successor_recorded_expected_midi", FAIL,
            f"the dump for slot {dump['slot']} disagrees with itself: storage note-ons "
            f"{dump['storage_note_ons']} vs effective {dump['effective_note_ons']}", "retention", "L7")
    elif dump["present"] and dump["notes_match"] and review["present"] and review["parity_ok"]:
        add("L7_successor_recorded_expected_midi", OK,
            f"content dump for slot {dump['slot']} contains the expected note(s) {list(expect_successor_notes)} "
            f"(note-ons {dump['note_ons']}), and the content review reports inventory_equal on every cycle "
            f"({review['result']})", "retention", "L7")
    elif dump["present"] and dump["notes_match"] and not review["present"]:
        add("L7_successor_recorded_expected_midi", UNKNOWN,
            f"the dump for slot {dump['slot']} contains the expected note(s), but no content review was "
            "supplied: pitch presence alone does not establish storage<->effective parity", "evidence", "L7")
    elif review["present"] and not review["parity_ok"]:
        add("L7_successor_recorded_expected_midi", FAIL,
            f"the content review reports a storage<->effective parity failure ({review['result']}): the "
            "successor's recorded content does not reach playback row for row", "retention", "L7")
    elif review["present"] and not dump["present"]:
        add("L7_successor_recorded_expected_midi", UNKNOWN,
            f"a content review is present and reports parity ({review['result']}), but parity is not "
            "note-level: it cannot say *which* notes were recorded. Supply the pipeline's dump "
            "(--content-dump) so the successor slot's actual notes can be checked", "evidence", "L7")
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
        if not matches:
            add("L7_successor_recorded_expected_midi", FAIL, detail, "retention", "L7")
        elif accept_event_count_only:
            concessions.append(f"L7 accepted on event-count evidence (operator concession): {detail}")
            add("L7_successor_recorded_expected_midi", OK, detail + " [operator concession]",
                "retention", "L7")
        else:
            add("L7_successor_recorded_expected_midi", UNKNOWN,
                detail + " -- run the content pipeline (--content-dump/--content-review) to show the "
                "expected notes were recorded; event count alone cannot", "evidence", "L7")
    else:
        add("L7_successor_recorded_expected_midi", UNKNOWN,
            f"no sealed take and no content dump for slot {slot_c}: cannot show C recorded anything",
            "evidence", "L7")

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
    elif concessions:
        # Every link is satisfied, but at least one only on a weaker operator-conceded basis. That is not
        # full acceptance: an automated consumer must not treat it as a pass, so it gets its own verdict and
        # can never exit 0.
        verdict = "RETENTION_PARTIAL"
    else:
        verdict = "RETENTION_PASS"
    return Analysis(verdict, checks, episode, evidence, limitations, concessions)


def report_dict(analysis: Analysis, identity: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {
        "verdict": analysis.verdict,
        # The authoritative code the process exits with. It was a computed property only, so every review file
        # omitted it and a consumer reading the record could not tell a pass from a partial.
        "exit_code": analysis.exit_code,
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
            # Two different quantities, named apart on purpose. The release-turn delta is an UPPER bound (a
            # maintenance turn services the marker, possibly long after the window closed, and possibly for
            # several slots at once), while the bounded value is the observed interval from the window opening
            # to the successor's denial -- the only one of the two that may be read as "the window was open at
            # least this long".
            "release_turn_delta_us": e.window_us,
            "release_turn_delta_ms": round(e.window_us / 1000.0, 1),
            "bounded_open_to_denial_us": e.bounded_us,
            "bounded_open_to_denial_ms": round(e.bounded_us / 1000.0, 1) if e.bounded_us is not None else None,
        }
    return out


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description="S4f.3 retention oracle over a managed HITL capture log")
    p.add_argument("--session-log", type=Path, required=True)
    p.add_argument("--base-report", type=Path, default=None)
    p.add_argument("--metadata", type=Path, default=None)
    p.add_argument("--content-review", type=Path, default=None)
    p.add_argument("--out", type=Path, default=None)
    # Base convention, matching the firmware's own fields and the native fixtures -- NOT the 1-based numbers
    # the HITL driver and its CLI take. Mixing them is a real trap: running this oracle with the driver's
    # numbers judges a correct run as having no denial for B at all (measured 2026-09-27).
    p.add_argument("--slot-a", type=int, default=0,
                   help="A's slot as the FIRMWARE names it (0-based): the driver's --slot-a minus 1")
    p.add_argument("--slot-b", type=int, default=1,
                   help="B's slot as the FIRMWARE names it (0-based): the driver's --slot-b minus 1")
    p.add_argument("--slot-c", type=int, default=2,
                   help="C's slot as the FIRMWARE names it (0-based): the driver's --slot-c minus 1")
    p.add_argument("--track-b", type=int, default=0,
                   help="B's track as the firmware's rows name it (0-based): the driver's --track minus 1")
    p.add_argument("--tempo-bpm", type=float, default=120.0)
    p.add_argument("--min-window-us", type=int, default=DEFAULT_MIN_WINDOW_US)
    p.add_argument("--followup-us", type=int, default=DEFAULT_FOLLOWUP_US)
    p.add_argument("--expect-successor-events", type=int, default=None)
    p.add_argument("--content-dump", type=Path, default=None,
                   help="serial.log from dump_and_capture.py (carries the !CAPTURE_CONTENT dump)")
    p.add_argument("--expect-successor-note", action="append", type=int, default=[],
                   help="a MIDI note the successor take must contain (repeatable)")
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
        content_dump=(args.content_dump.read_text(errors="replace") if args.content_dump
                      and args.content_dump.exists() else None),
        expect_successor_notes=tuple(args.expect_successor_note),
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
        # Both quantities, named apart: the bounded one is evidence, the release-turn delta is not.
        bounded = ep["bounded_open_to_denial_ms"]
        print(f"  window (slot {ep['slot_a']}): bounded open->denial "
              f"{'unknown' if bounded is None else f'{bounded} ms'}; "
              f"release-turn delta {ep['release_turn_delta_ms']} ms (servicing latency, upper bound only)")
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
