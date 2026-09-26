#!/usr/bin/env python3
"""S4f.3 retention oracle: did the device reach -- and correctly leave -- the capacity-denied state?

This is the **evidence oracle** for the temporary retention stimulus. It reads a managed HITL capture
session log (the `#CAP` telemetry stream) and decides the retention requirement from the device's own
admission/ownership records, deliberately **not** from the brief `OVERDUB_STOPPING` state:

    DIAG,reentry_window_open,<slot>,<stopTick>,<loopLengthTicks>
        The deferred predecessor stop has frozen its interval and handed the live capture role back, so a
        successor interval may start (DEC-067 `successor_start_window_open`, one shot per deferred stop).
        This is "the predecessor's finalizer is admitted and outstanding" for that slot.

    DIAG,canonical_release,<slot>,<loopLengthTicks>,<availableChunks>
        The deferred stop's finalization resources are free again (DEC-067
        `canonical_finalization_capacity_released`, one shot per retired stop). This is the moment the
        successor's stop *may* be admitted, i.e. the end of the capacity-denied window.

    DIAG,reentry,deny,<tick>,<interval>,<slot>,<depth>   (also `deny_resource`)
        One shot per reentry decision from the decision owner; `slot` and `interval` are the identity of
        the interval whose admission was denied. Inside (window_open, canonical_release) this is
        "the successor's stop admission was unavailable", named per slot.

    ODUB,stop,admission,0,0,4294967295,4294967295,<depth>,0,successor_stop_denied_capacity
        The stop-side stage record emitted by the same denial branch.

    RECA,<slot>,<tick>
        The capture actually started on that slot. Exactly one of these for the successor slot, after the
        denial, is the "starts and records MIDI exactly once" half.

Verdict vocabulary and exit codes keep **setup** distinct from **retention**:

    SETUP_INCOMPLETE        exit 3  -- the scenario was not staged; says nothing about retention
    RETENTION_NOT_EXERCISED exit 2  -- staged, but the capacity-denied state was never entered
    RETENTION_FAIL          exit 2  -- denied state entered, requirement violated
    RETENTION_PASS          exit 0

Usage:
    retention_probe.py --session-log <log> [--metadata <metadata.json>] [--out retention-review.json]
                       [--slot-a 0] [--slot-b 1] [--slot-c 2] [--tempo-bpm 120]
                       [--min-window-us 150000]
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
REENTRY_WINDOW_OPEN_RE = re.compile(r"^DIAG,reentry_window_open,(\d+),(\d+),(\d+)$")
CANONICAL_RELEASE_RE = re.compile(r"^DIAG,canonical_release,(\d+),(\d+),(\d+)$")
REENTRY_RE = re.compile(r"^DIAG,reentry,([a-z_]+),(\d+),(\d+),(\d+),(\d+)$")
RECA_RE = re.compile(r"^RECA,(\d+),(\d+)$")
RECS_STOP_RE = re.compile(r"^RECS,stop,(\d+),")
DENIAL_STAGE_RE = re.compile(r"^ODUB,stop,admission,.*,successor_stop_denied_capacity$")
TEMPO_FEEDBACK_RE = re.compile(r"Sent tempo CC feedback: ([0-9.]+) BPM")
TEMPO_APPLIED_RE = re.compile(r"TEMPO: CC=\d+ -> ([0-9.]+) BPM")
INVARIANT_VIOLATION_MARKERS = (
    "record activation invariant violated",
    "record activation violated its invariant",
)

# The default minimum capacity-denied window. A host that issues two gestures needs tens of
# milliseconds, not seconds; the measured device window on a 2-bar loop is ~1.3 s, so this is a
# conservative floor that still fails loudly if a future change collapses the window.
DEFAULT_MIN_WINDOW_US = 150_000


@dataclass
class Entry:
    us: int
    body: str


@dataclass
class Check:
    name: str
    ok: bool
    detail: str
    kind: str = "retention"  # "setup" | "retention"


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

    @property
    def exit_code(self) -> int:
        if self.verdict == "RETENTION_PASS":
            return 0
        if self.verdict == "SETUP_INCOMPLETE":
            return 3
        return 2


def parse_entries(text: str) -> list[Entry]:
    """Every `#CAP,<us>,<body>` record, in file order."""
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


def analyse(
    session_log: str,
    *,
    slot_a: int,
    slot_b: int,
    slot_c: int,
    tempo_bpm: float = 120.0,
    min_window_us: int = DEFAULT_MIN_WINDOW_US,
) -> Analysis:
    entries = parse_entries(session_log)
    checks: list[Check] = []
    evidence: dict[str, list[str]] = {
        "window_open": [],
        "canonical_release": [],
        "denial": [],
        "successor_start": [],
        "invariant_violation": [],
    }

    # ---- setup checks: these decide whether the run is a retention verdict at all ----
    if not entries:
        checks.append(Check("telemetry_present", False, "no #CAP records in the log", "setup"))
        return Analysis("SETUP_INCOMPLETE", checks, None, evidence)

    head = session_log.splitlines()[:60]
    booted = any(l.strip().startswith("BOOT,") for l in head)
    checks.append(
        Check("fresh_boot", booted, "boot banner in the first 60 lines" if booted else
              "no BOOT banner: the device did not reset before this run (skill section 3)", "setup")
    )

    tempos = [float(m.group(1)) for m in (TEMPO_FEEDBACK_RE.search(l) or TEMPO_APPLIED_RE.search(l)
                                          for l in session_log.splitlines()) if m]
    tempo_ok = any(abs(t - tempo_bpm) <= 0.05 for t in tempos)
    checks.append(
        Check("tempo_verified", tempo_ok,
              f"device tempo {tempos[0]:.1f} BPM matches {tempo_bpm:.1f}" if tempo_ok else
              f"no tempo line matching {tempo_bpm:.1f} BPM (found {tempos[:3] or 'none'}); the tempo pin "
              "does not survive the serial-open reset and must be re-applied and read back per run",
              "setup")
    )

    seed = next((e for e in entries if (m := RECS_STOP_RE.match(e.body)) and int(m.group(1)) == slot_a),
                None)
    checks.append(
        Check("seed_content_on_a", seed is not None,
              f"RECS,stop,{slot_a} at {seed.us} us" if seed else
              f"no RECS,stop,{slot_a}: the seed take on A never landed", "setup")
    )

    window_open = next((e for e in entries
                        if (m := REENTRY_WINDOW_OPEN_RE.match(e.body)) and int(m.group(1)) == slot_a),
                       None)
    checks.append(
        Check("finalizer_admitted_on_a", window_open is not None,
              "" if window_open else
              f"no DIAG,reentry_window_open,{slot_a}: A's deferred stop never froze its interval, so the "
              "capacity-denied state was never entered", "setup")
    )
    if window_open is not None:
        evidence["window_open"].append(f"{window_open.us}: {window_open.body}")

    episode: Optional[Episode] = None
    if window_open is not None:
        m_open = REENTRY_WINDOW_OPEN_RE.match(window_open.body)
        release = _first_after(entries, window_open.us,
                               lambda e: (mm := CANONICAL_RELEASE_RE.match(e.body)) is not None
                               and int(mm.group(1)) == slot_a)
        checks.append(
            Check("capacity_released_after_a", release is not None,
                  "" if release else
                  f"no DIAG,canonical_release,{slot_a} after A's finalizer opened: A never retired, so the "
                  "denied window never closed", "setup")
        )
        if release is not None:
            evidence["canonical_release"].append(f"{release.us}: {release.body}")
            window_us = release.us - window_open.us
            episode = Episode(slot_a, window_open.us, int(m_open.group(3)), release.us, window_us)
            wide = window_us >= min_window_us
            checks.append(
                Check("capacity_denied_window_wide_enough", wide,
                      f"{window_us} us ({window_us/1000:.1f} ms) between window_open and canonical_release; "
                      f"floor {min_window_us} us"
                      + ("" if wide else ": too narrow to stage two host gestures inside it -- this is an "
                         "exposure/setup problem, not a retention result"), "setup")
            )

    # ---- retention checks ----
    in_window = lambda us: episode is not None and episode.window_open_us < us < episode.capacity_release_us
    denials = [e for e in entries
               if (m := REENTRY_RE.match(e.body))
               and m.group(1) in ("deny", "deny_resource")
               and int(m.group(4)) == slot_b]
    staged_denials = [e for e in denials if in_window(e.us)]
    stage_lines = [e for e in entries if DENIAL_STAGE_RE.match(e.body) and in_window(e.us)]
    for e in denials:
        evidence["denial"].append(f"{e.us}: {e.body}{'' if in_window(e.us) else '  (outside the window)'}")

    denial = staged_denials[0] if staged_denials else (denials[0] if denials else None)
    denial_ok = bool(staged_denials) or bool(stage_lines)
    checks.append(
        Check("successor_stop_admission_denied", denial_ok,
              f"denied inside the predecessor's finalization window: "
              f"{staged_denials[0].body if staged_denials else (stage_lines[0].body if stage_lines else '')}"
              if denial_ok else
              f"no DIAG,reentry,deny/deny_resource for slot {slot_b} inside A's finalization window "
              f"({len(denials)} denial(s) elsewhere): the capacity-denied state was never entered",
              "retention")
    )

    starts = [e for e in entries if (m := RECA_RE.match(e.body)) and int(m.group(1)) == slot_c]
    for e in starts:
        evidence["successor_start"].append(f"{e.us}: {e.body}")
    after_denial = [e for e in starts if denial is None or e.us > denial.us]
    if not denial_ok:
        start_ok, start_detail = False, "not evaluated: no denial to follow"
    elif len(starts) == 0:
        start_ok, start_detail = False, f"no RECA,{slot_c}: the retained successor start never executed"
    elif len(starts) > 1:
        start_ok, start_detail = False, (f"{len(starts)} RECA records for slot {slot_c}: the retained "
                                         "start executed more than once")
    elif not after_denial:
        start_ok, start_detail = False, (f"the only RECA,{slot_c} precedes the denial: the start did not "
                                         "follow its blocked prerequisite")
    else:
        start_ok, start_detail = True, f"exactly one RECA,{slot_c} at {starts[0].us} us, after the denial"
    checks.append(Check("successor_started_exactly_once", start_ok, start_detail, "retention"))

    violations = [l.strip() for l in session_log.splitlines()
                  if any(marker in l for marker in INVARIANT_VIOLATION_MARKERS)]
    evidence["invariant_violation"] = violations[:5]
    checks.append(
        Check("activation_invariant_held", not violations,
              "no activation-invariant violation logged" if not violations else
              f"{len(violations)} activation-invariant violation(s): {violations[0]}", "retention")
    )

    replay = [e for e in denials if denial is not None and e.us > denial.us and episode is not None
              and e.us > episode.capacity_release_us]
    checks.append(
        Check("no_stop_obligation_replay", not replay,
              "no denial re-issued after capacity was released" if not replay else
              f"the stop obligation was re-issued after capacity released: {replay[0].body}", "retention")
    )

    setup_failures = [c for c in checks if c.kind == "setup" and not c.ok]
    if setup_failures:
        verdict = "SETUP_INCOMPLETE"
    elif not denial_ok:
        verdict = "RETENTION_NOT_EXERCISED"
    else:
        verdict = "RETENTION_PASS" if all(c.ok for c in checks if c.kind == "retention") \
            else "RETENTION_FAIL"
    return Analysis(verdict, checks, episode, evidence)


def _load_identity(metadata_path: Optional[Path]) -> dict[str, Any]:
    if metadata_path is None or not metadata_path.exists():
        return {}
    try:
        data = json.loads(metadata_path.read_text())
    except (OSError, json.JSONDecodeError):
        return {}
    return {k: data.get(k) for k in ("git_commit", "firmware_hex_sha256")}


def report_dict(analysis: Analysis, identity: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {
        "verdict": analysis.verdict,
        "identity": identity,
        "checks": [{"name": c.name, "kind": c.kind, "ok": c.ok, "detail": c.detail}
                   for c in analysis.checks],
        "evidence": analysis.evidence,
    }
    if analysis.episode is not None:
        e = analysis.episode
        out["episode"] = {
            "slot_a": e.slot_a,
            "window_open_us": e.window_open_us,
            "window_open_loop_len_ticks": e.window_open_loop_len,
            "capacity_release_us": e.capacity_release_us,
            "capacity_denied_window_us": e.window_us,
            "capacity_denied_window_ms": round(e.window_us / 1000.0, 1),
        }
    return out


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description="S4f.3 retention oracle over a managed HITL capture log")
    p.add_argument("--session-log", type=Path, required=True)
    p.add_argument("--metadata", type=Path, default=None)
    p.add_argument("--out", type=Path, default=None)
    p.add_argument("--slot-a", type=int, default=0)
    p.add_argument("--slot-b", type=int, default=1)
    p.add_argument("--slot-c", type=int, default=2)
    p.add_argument("--tempo-bpm", type=float, default=120.0)
    p.add_argument("--min-window-us", type=int, default=DEFAULT_MIN_WINDOW_US)
    args = p.parse_args(argv)

    text = args.session_log.read_text(errors="replace")
    metadata = args.metadata or (args.session_log.parent / "metadata.json")
    analysis = analyse(text, slot_a=args.slot_a, slot_b=args.slot_b, slot_c=args.slot_c,
                       tempo_bpm=args.tempo_bpm, min_window_us=args.min_window_us)
    payload = report_dict(analysis, _load_identity(metadata))

    out = args.out or (args.session_log.parent / "retention-review.json")
    out.write_text(json.dumps(payload, indent=2) + "\n")

    print(f"retention verdict: {analysis.verdict}")
    if "episode" in payload:
        ep = payload["episode"]
        print(f"  capacity-denied window: {ep['capacity_denied_window_ms']} ms "
              f"(slot {ep['slot_a']}, loop {ep['window_open_loop_len_ticks']} ticks)")
    for c in analysis.checks:
        print(f"  [{'ok ' if c.ok else 'FAIL'}] ({c.kind}) {c.name}: {c.detail}")
    print(f"  review: {out}")
    return analysis.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
