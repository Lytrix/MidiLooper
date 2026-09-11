#!/usr/bin/env python3
"""OpenSpec 6.2.1 — B1 C1 gather-miss inventory (read-only capture + fixture catalog)."""

from __future__ import annotations

import argparse
import re
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent

_SNAPSHOT_IDLE_RE = re.compile(
    r"#CAP,(\d+),DIAG,snapshot_idle,us=\d+,pending=(\d+),root=(0x[0-9a-f]+),"
    r"root_rev=(\d+),playback_rev=(\d+)"
)
_ST_TRACK_RE = re.compile(r"#CAP,(\d+),ST,Track,([^,\n]+),([^,\n]+)")
_MO_RE = re.compile(r"#CAP,(\d+),MO,")

# Native fixtures where tryGatherPublishedSnapshotEventsInWindow returns false (C1).
_NATIVE_C1_FIXTURES = (
    "test_try_gather_published_snapshot_events — empty loop before capture",
    "test_resetPassTimeline tail — published root cleared",
    "test_live_record_starts_snapshot_before_session_exit — in-flight construction",
    "test_overdub_idle_advances_in_flight_construction_only — queued publish, no root",
    "test_session_exit_handoff_when_live_record_notes_fit_sealed_length — pre-drain gap",
    "test_idle_slice_without_snapshot_uses_fallback — pass-only seed (display/B3 class)",
)

# HITL-class capture used for device evidence (full record/overdub session).
_DEFAULT_CAPTURE = _REPO_ROOT / "captures/session_20260911_190525.log"


@dataclass(frozen=True)
class PlayingWindowStats:
    label: str
    start_us: int
    end_us: int
    mo_count: int
    c1_mo_count: int
    c4_mo_count: int
    snap_lines: int
    snap_roots: frozenset[str]


def _parse_capture(
    path: Path,
) -> tuple[list[int], list[tuple[int, int, str, int, int]], list[tuple[int, str, str]]]:
    snaps: list[tuple[int, int, str, int, int]] = []
    mos: list[int] = []
    transitions: list[tuple[int, str, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = _SNAPSHOT_IDLE_RE.search(line)
        if m:
            snaps.append(
                (
                    int(m.group(1)),
                    int(m.group(2)),
                    m.group(3),
                    int(m.group(4)),
                    int(m.group(5)),
                )
            )
            continue
        m = _MO_RE.search(line)
        if m:
            mos.append(int(m.group(1)))
            continue
        m = _ST_TRACK_RE.search(line)
        if m:
            transitions.append((int(m.group(1)), m.group(2), m.group(3)))
    return mos, snaps, transitions


def _playing_windows(transitions: list[tuple[int, str, str]]) -> list[tuple[int, int, str]]:
    windows: list[tuple[int, int, str]] = []
    open_phase: tuple[int, str] | None = None
    for t, from_state, to_state in transitions:
        if to_state in ("RECORDING", "STOPPED", "EMPTY", "ARMED"):
            if open_phase:
                windows.append((open_phase[0], t, open_phase[1]))
                open_phase = None
        if from_state in ("PLAYING", "OVERDUBBING"):
            if open_phase and open_phase[1] == from_state:
                windows.append((open_phase[0], t, open_phase[1]))
                open_phase = None
        if to_state in ("PLAYING", "OVERDUBBING"):
            if open_phase is None or open_phase[1] != to_state:
                open_phase = (t, to_state)
    if open_phase:
        windows.append((open_phase[0], sys.maxsize, open_phase[1]))
    return windows


def analyze_capture(path: Path) -> list[PlayingWindowStats]:
    mos, snaps, transitions = _parse_capture(path)

    stats: list[PlayingWindowStats] = []
    for idx, (start, end, phase) in enumerate(_playing_windows(transitions)):
        phase_mos = [t for t in mos if start <= t <= end]
        c1 = 0
        c4 = 0
        for mo_t in phase_mos:
            prior = [s for s in snaps if s[0] <= mo_t]
            if not prior:
                continue
            _, pending, root, _, _ = prior[-1]
            if root == "0x0":
                c1 += 1
            elif pending:
                c4 += 1
        window_snaps = [s for s in snaps if start <= s[0] <= end]
        roots = frozenset(s[2] for s in window_snaps)
        stats.append(
            PlayingWindowStats(
                label=f"{phase}#{idx + 1}",
                start_us=start,
                end_us=end if end != sys.maxsize else start,
                mo_count=len(phase_mos),
                c1_mo_count=c1,
                c4_mo_count=c4,
                snap_lines=len(window_snaps),
                snap_roots=roots,
            )
        )
    return stats


def first_nonzero_root_after(path: Path, after_us: int) -> tuple[int, str] | None:
    _, snaps, _ = _parse_capture(path)
    for t, _, root, _, _ in snaps:
        if t >= after_us and root != "0x0":
            return t, root
    return None


def print_report(path: Path) -> int:
    if not path.is_file():
        print(f"capture missing: {path}", file=sys.stderr)
        return 1

    stats = analyze_capture(path)
    print(f"B1 C1 inventory — {path.name}")
    print("\nNative C1 fixtures (tryGather false / no published root):")
    for row in _NATIVE_C1_FIXTURES:
        print(f"  - {row}")

    print("\nHITL-class PLAYING/OVERDUBBING windows (MO vs last snapshot_idle root):")
    for row in stats:
        print(
            f"  {row.label} [{row.start_us},{row.end_us}] "
            f"MO={row.mo_count} C1(root=0x0)={row.c1_mo_count} "
            f"C4(pending+root)={row.c4_mo_count} snap_lines={row.snap_lines} roots={sorted(row.snap_roots)}"
        )

    # Pin the first post-record PLAYING window when present.
    record_stop = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = _ST_TRACK_RE.search(line)
        if m and m.group(2) == "RECORDING" and m.group(3) == "STOPPED_RECORDING":
            record_stop = int(m.group(1))
    if record_stop is not None:
        first_play = next((s for s in stats if s.start_us > record_stop and "PLAYING" in s.label), None)
        first_root = first_nonzero_root_after(path, record_stop)
        if first_play and first_root:
            gap_us = first_root[0] - first_play.start_us
            print(
                f"\nC1-a session-exit gap (first PLAYING after record stop): "
                f"PLAYING@{first_play.start_us} first_root@{first_root[0]} "
                f"gap={gap_us}us C1_MOs_in_window={first_play.c1_mo_count}/{first_play.mo_count}"
            )
    return 0


class B1C1InventoryTests(unittest.TestCase):
    def test_native_fixture_catalog_non_empty(self) -> None:
        self.assertGreaterEqual(len(_NATIVE_C1_FIXTURES), 5)

    def test_hitl_capture_shows_c1a_gap_on_first_playing_after_record(self) -> None:
        """190525: first PLAYING after record stop runs with root=0x0 until publish completes."""
        if not _DEFAULT_CAPTURE.is_file():
            self.skipTest(f"missing {_DEFAULT_CAPTURE}")
        stats = analyze_capture(_DEFAULT_CAPTURE)
        record_stop = None
        for line in _DEFAULT_CAPTURE.read_text(encoding="utf-8", errors="replace").splitlines():
            m = _ST_TRACK_RE.search(line)
            if m and m.group(2) == "RECORDING" and m.group(3) == "STOPPED_RECORDING":
                record_stop = int(m.group(1))
        self.assertIsNotNone(record_stop)
        playing_after = [
            s
            for s in stats
            if s.label.startswith("PLAYING") and s.start_us > record_stop
        ]
        self.assertTrue(playing_after, stats)
        first_play = min(playing_after, key=lambda s: s.start_us)
        self.assertGreater(first_play.c1_mo_count, 0, first_play)
        self.assertIn("0x0", first_play.snap_roots)

    def test_b1_overlay_not_c1_on_second_playing_window(self) -> None:
        """After overdub, PLAYING uses stale root + pending (C4), not root=0x0."""
        if not _DEFAULT_CAPTURE.is_file():
            self.skipTest(f"missing {_DEFAULT_CAPTURE}")
        stats = analyze_capture(_DEFAULT_CAPTURE)
        post_overdub = [s for s in stats if s.label.startswith("PLAYING") and s.c1_mo_count == 0]
        self.assertTrue(post_overdub, stats)
        self.assertGreater(post_overdub[-1].c4_mo_count, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "capture",
        nargs="?",
        default=str(_DEFAULT_CAPTURE),
        help="Session capture log (default: 190525 HITL-class session)",
    )
    parser.add_argument("--test", action="store_true", help="Run host inventory gates")
    args = parser.parse_args()

    if args.test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(B1C1InventoryTests)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        return 0 if result.wasSuccessful() else 1

    return print_report(Path(args.capture))


if __name__ == "__main__":
    raise SystemExit(main())
