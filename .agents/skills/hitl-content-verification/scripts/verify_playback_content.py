#!/usr/bin/env python3
"""Compare captured playback inventory against the committed effective content.

Stage 3 of the HITL content-verification recipe (see SKILL.md). Reads the dump
produced by dump_and_capture.py and writes content-review.json.

Verdicts:
  PASS                       every cycle matches effective exactly
  PASS_WITH_KNOWN_RESIDUALS  only documented ledger/pairing residuals differ
  FAIL                       unexplained missing or any extra/duplicated row
  INCONCLUSIVE_CAPTURE       no usable anchor or malformed dump

Known residuals are defined in docs/Guides/SNAPSHOT_CONTENT_VERIFICATION.md:
loop-boundary Offs (stored with NoteId=0 at the final tick) and coincident
same-pitch Offs (one emitted for two notes ending on the same tick).

  .venv/bin/python verify_playback_content.py captures/<run>-playback
"""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys

NOTE_ON = 144
NOTE_OFF = 128
CONTROL_CHANGE = 176
DEFAULT_CC = 74


def signature(event: tuple) -> tuple:
    """(status, channel, d1, d2) with note-off velocity normalized to 0."""
    _tick, status, channel, d1, d2 = event[:5]
    return (status, channel, d1, 0 if status == NOTE_OFF else d2)


def classify_missing(sig: tuple, count: int, occurrences: list[tuple], loop_length: int) -> int:
    """How many of `count` missing rows this signature are documented residuals."""
    if sig[0] != NOTE_OFF:
        return 0
    terminal = sum(1 for tick, _nid in occurrences if tick == loop_length - 1)
    by_tick = collections.Counter(tick for tick, _nid in occurrences)
    coincident = sum(c - 1 for c in by_tick.values() if c > 1)
    return min(count, terminal + coincident)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", type=pathlib.Path)
    parser.add_argument(
        "--midi-channel",
        type=int,
        default=None,
        help="1-based MIDI channel; default derives it from the content dump",
    )
    parser.add_argument("--cc", type=int, default=DEFAULT_CC, help="CC lane to compare (default 74)")
    parser.add_argument("--ticks-per-bar", type=int, default=768)
    args = parser.parse_args()

    p = args.capture_dir
    sections: dict[tuple[str, int], list[tuple]] = collections.defaultdict(list)
    declared: dict[tuple[str, int], int] = {}
    length = None
    for line in (p / "serial.log").read_text(errors="replace").splitlines():
        f = line.split(",")
        if f[:2] == ["CAPTURE_CONTENT", "begin"]:
            length = int(f[-1].split("=")[1])
        elif f[:2] == ["CAPTURE_CONTENT", "rows"]:
            declared[(f[2], int(f[3]))] = int(f[4])
        elif f[:2] == ["CAPTURE_CONTENT", "event"]:
            sections[(f[2], int(f[3]))].append(tuple(map(int, f[4:])))
    for key, count in declared.items():
        if len(sections[key]) != count:
            raise SystemExit(f"Malformed dump: {key} declared {count}, parsed {len(sections[key])}")

    effective = sections[("effective", 0)]
    dump_channel = next((e[2] for e in effective if e[1] in (NOTE_ON, NOTE_OFF)), None)
    channel = args.midi_channel if args.midi_channel is not None else dump_channel
    if channel is None:
        raise SystemExit("No note channel in the content dump; pass --midi-channel")

    order = lambda e: (e[0], 0 if e[1] == NOTE_OFF else 1 if e[1] == NOTE_ON else 2)
    ref = sorted(effective, key=order)
    expected_full = [
        (signature(e), e[0], e[5] if len(e) > 5 else 0) for e in ref
        if e[1] in (NOTE_ON, NOTE_OFF) or (e[1] == CONTROL_CHANGE and e[3] == args.cc)
    ]

    actions = [json.loads(x) for x in (p / "actions.jsonl").read_text().splitlines()]
    lo, hi = actions[0]["host_ns"], actions[1]["host_ns"]
    actual = []
    for line in (p / "midi-output.jsonl").read_text().splitlines():
        row = json.loads(line)
        msg = row["message"]; t = row["host_ns"]
        if not lo < t < hi or msg.get("channel") != channel - 1:
            continue
        typ = msg["type"]
        if typ in ("note_on", "note_off"):
            on = typ == "note_on" and msg["velocity"]
            actual.append((NOTE_ON if on else NOTE_OFF, channel, msg["note"],
                           msg["velocity"] if on else 0))
        elif typ == "control_change" and msg["control"] == args.cc:
            actual.append((CONTROL_CHANGE, channel, args.cc, msg["value"]))

    exp_counts = collections.Counter(sig for sig, _t, _n in expected_full)
    act_counts = collections.Counter(actual)
    anchor = next((sig for sig, _t, _n in expected_full if exp_counts[sig] == 1 and act_counts[sig] >= 2), None)
    if anchor is None:
        result = {
            "result": "INCONCLUSIVE_CAPTURE",
            "reason": "no unique effective event recurs in the playback window",
            "effective_rows_all": len(effective),
        }
        (p / "content-review.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
        return 2

    start = next(i for i, (sig, _t, _n) in enumerate(expected_full) if sig == anchor)
    rotated = expected_full[start:] + expected_full[:start]
    rotated_counts = collections.Counter(sig for sig, _t, _n in rotated)
    occurrences: dict[tuple, list[tuple]] = collections.defaultdict(list)
    for sig, tick, nid in rotated:
        occurrences[sig].append((tick, nid))

    idx = [i for i, s in enumerate(actual) if s == anchor]
    cycles = []
    for a, b in zip(idx, idx[1:]):
        observed = actual[a:b]
        obs_counts = collections.Counter(observed)
        missing = rotated_counts - obs_counts
        extra = obs_counts - rotated_counts
        residual = 0
        residual_rows = []
        unexplained_rows = []
        for sig, count in missing.items():
            explained = classify_missing(sig, count, occurrences[sig], length)
            residual += explained
            if explained:
                residual_rows.append([list(sig), explained])
            if count - explained:
                unexplained_rows.append([list(sig), count - explained])
        missing_total = sum(missing.values())
        cycles.append({
            "rows": len(observed),
            "inventory_equal": missing_total == 0 and not extra,
            "exact_order": observed == [s for s, _t, _n in rotated],
            "missing_count": missing_total,
            "extra_count": sum(extra.values()),
            "residual_missing_count": residual,
            "unexplained_missing_count": missing_total - residual,
            "missing": [list(k) + [v] for k, v in missing.items()],
            "extra": [list(k) + [v] for k, v in extra.items()],
            "residual_missing": residual_rows,
            "unexplained_missing": unexplained_rows,
        })

    per_bar = collections.Counter(e[0] // args.ticks_per_bar for e in effective)
    record = sum(v for k, v in declared.items() if k[0] == "record")
    overdub = sum(v for k, v in declared.items() if k[0] == "overdub")
    if not cycles:
        verdict = "INCONCLUSIVE_CAPTURE"
    elif all(c["inventory_equal"] for c in cycles):
        verdict = "PASS"
    elif all(c["extra_count"] == 0 and c["unexplained_missing_count"] == 0 for c in cycles):
        verdict = "PASS_WITH_KNOWN_RESIDUALS"
    else:
        verdict = "FAIL"
    result = {
        "result": verdict,
        "length": length,
        "anchor": list(anchor),
        "raw_counts": {f"{k[0]},{k[1]}": v for k, v in declared.items()},
        "effective_rows_all": len(effective),
        "expected_note_cc_rows": len(expected_full),
        "storage_matches_effective": record + overdub == len(effective),
        "per_bar_effective": {str(b): n for b, n in sorted(per_bar.items())},
        "max_bar_refs": max(per_bar.values()) if per_bar else 0,
        "capacity_path_exercised": bool(per_bar) and max(per_bar.values()) > 256,
        "cycles": cycles,
    }
    (p / "content-review.json").write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k != "cycles"}, indent=2))
    for i, c in enumerate(cycles):
        print(f"cycle{i}: rows={c['rows']} missing={c['missing_count']} "
              f"(residual={c['residual_missing_count']} unexplained={c['unexplained_missing_count']}) "
              f"extra={c['extra_count']} inventory_equal={c['inventory_equal']} "
              f"exact_order={c['exact_order']}")
        if c["unexplained_missing"]:
            print(f"   UNEXPLAINED missing: {c['unexplained_missing']}")
        if c["extra"]:
            print(f"   EXTRA: {c['extra']}")
    return 0 if verdict in ("PASS", "PASS_WITH_KNOWN_RESIDUALS") else (2 if verdict == "INCONCLUSIVE_CAPTURE" else 1)


if __name__ == "__main__":
    sys.exit(main())
