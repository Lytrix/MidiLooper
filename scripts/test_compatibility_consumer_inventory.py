#!/usr/bin/env python3
"""OpenSpec 6.1 — compatibility consumer inventory and hot-path gates."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent

_MATERIALIZE_TO_EVENT_VECTOR_RE = re.compile(r"\bmaterializeToEventVector\s*\(")
_PASSES_MATERIALIZED_STORE_RE = re.compile(r"\bpassesMaterializedStore_")
_GATHER_COMMITTED_RE = re.compile(
    r"\bgatherCommittedEvents(?:InWindow|WithCapture|ForDerivedView)?\s*\("
)

_HOT_PATH_FORBID = frozenset(
    {
        "src/Track/TrackPlaybackHotPath.cpp",
        "src/MidiHandler.cpp",
        "src/ClockManager.cpp",
        "src/TrackManager/TrackManagerTransportTick.cpp",
    }
)

_MATERIALIZE_TO_EVENT_VECTOR_ALLOW = frozenset(
    {
        "src/Loop/LoopMaterialization.cpp",
        "src/Loop/LoopPasses.cpp",
        "src/LoopContentResolution.cpp",
        "src/Loop/LoopEditPasses.cpp",
        "include/LoopPasses.h",
    }
)

_GATHER_COMMITTED_ALLOW = frozenset(
    {
        "src/Loop/LoopMaterialization.cpp",
        "src/Track/TrackDeferredMaintenance.cpp",
        "src/DisplayManager/DisplayNoteWindowGather.cpp",
        "include/Loop.h",
    }
)

# OpenSpec 6.3.1 — C1-a session-exit gather retired at these owners.
_C1_A_GATHER_RETIRED_SITES = frozenset(
    {
        "src/Track/TrackPlaybackWindowBuild.cpp",
        "src/Loop/LoopVisualCache.cpp",
        "src/DisplayManager/DisplayNoteResolveCommitted.cpp",
        "src/DisplayManager/DisplayColdHelpers.cpp",
    }
)

# Named 6.2 fallback sites (plan B1–B8). B1/B3/B4/B2/B5 use narrow C1-a exceptions;
# B7 is construction-only (LCR device gate); B8 retired direct materialize in capture.
# See scripts/test_b7_b8_construction_consumer_gate.py.
_6_2_BLOCKER_SITES: dict[str, tuple[str, re.Pattern[str]]] = {}

_B7_CONSTRUCTION_SITE = ("src/LoopContentResolution.cpp", _MATERIALIZE_TO_EVENT_VECTOR_RE)

_EXPECTED_BLOCKER_COUNT = len(_6_2_BLOCKER_SITES)


def _rel(path: Path) -> str:
    return path.relative_to(_REPO_ROOT).as_posix()


def _scan_cpp_headers(pattern: re.Pattern[str]) -> dict[str, list[str]]:
    hits: dict[str, list[str]] = {}
    for folder in ("src", "include"):
        root = _REPO_ROOT / folder
        for path in root.rglob("*"):
            if path.suffix not in {".cpp", ".h"}:
                continue
            matching_lines: list[str] = []
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                stripped = line.strip()
                if stripped.startswith("//"):
                    continue
                if pattern.search(line):
                    matching_lines.append(stripped)
            if not matching_lines:
                continue
            hits[_rel(path)] = matching_lines
    return hits


def _unexpected(hits: dict[str, list[str]], allow: set[str]) -> dict[str, list[str]]:
    return {rel: lines for rel, lines in hits.items() if rel not in allow}


def _blocker_files(hits: dict[str, list[str]], allow: set[str]) -> list[str]:
    return sorted(_unexpected(hits, allow).keys())


class CompatibilityConsumerInventoryTests(unittest.TestCase):
    def test_hot_paths_forbid_compat_execute(self) -> None:
        patterns = (
            _MATERIALIZE_TO_EVENT_VECTOR_RE,
            _PASSES_MATERIALIZED_STORE_RE,
            _GATHER_COMMITTED_RE,
        )
        for rel in _HOT_PATH_FORBID:
            text = (_REPO_ROOT / rel).read_text(encoding="utf-8", errors="replace")
            for pattern in patterns:
                self.assertNotRegex(
                    text,
                    pattern,
                    msg=f"{rel} must not call compatibility API {pattern.pattern}",
                )

    def test_materialize_to_event_vector_only_in_machinery_owners(self) -> None:
        hits = _scan_cpp_headers(_MATERIALIZE_TO_EVENT_VECTOR_RE)
        unexpected = _unexpected(hits, _MATERIALIZE_TO_EVENT_VECTOR_ALLOW)
        self.assertEqual(unexpected, {}, unexpected)

    def test_passes_materialized_store_removed(self) -> None:
        hits = _scan_cpp_headers(_PASSES_MATERIALIZED_STORE_RE)
        self.assertEqual(hits, {})

    def test_gather_committed_only_in_allowlist(self) -> None:
        hits = _scan_cpp_headers(_GATHER_COMMITTED_RE)
        unexpected = _unexpected(hits, _GATHER_COMMITTED_ALLOW)
        self.assertEqual(unexpected, {}, unexpected)

    def test_blocker_inventory_count_stable(self) -> None:
        """6.2 must shrink this set deliberately — not by accident."""
        scans = {
            _GATHER_COMMITTED_RE: _scan_cpp_headers(_GATHER_COMMITTED_RE),
            _MATERIALIZE_TO_EVENT_VECTOR_RE: _scan_cpp_headers(
                _MATERIALIZE_TO_EVENT_VECTOR_RE
            ),
        }

        missing: list[str] = []
        for blocker_id, (rel_path, pattern) in _6_2_BLOCKER_SITES.items():
            hits = scans[pattern]
            if rel_path not in hits:
                missing.append(f"{blocker_id}:{rel_path}")

        self.assertEqual(
            len(_6_2_BLOCKER_SITES),
            _EXPECTED_BLOCKER_COUNT,
            "update _6_2_BLOCKER_SITES when adding/removing blockers",
        )
        self.assertEqual(missing, [], missing)

    def test_c1_a_gather_retired_at_b1_b3_b4_sites(self) -> None:
        hits = _scan_cpp_headers(_GATHER_COMMITTED_RE)
        for rel_path in _C1_A_GATHER_RETIRED_SITES:
            self.assertNotIn(
                rel_path,
                hits,
                f"{rel_path} must not call gatherCommittedEvents after 6.3.1",
            )

    def test_b2_does_not_materialize_on_session_exit_gap(self) -> None:
        text = (_REPO_ROOT / "src/Utils/CommittedMidiScratch.cpp").read_text(
            encoding="utf-8", errors="replace"
        )
        self.assertNotIn("isPlaybackSessionExitPublicationGap", text)
        self.assertIn("RefreshKind::HoldEmpty", text)

    def test_b6_note_edit_lifecycle_has_no_midi_events_consumer(self) -> None:
        rel_path = "src/EditManager/NoteEditSessionLifecycle.cpp"
        text = (_REPO_ROOT / rel_path).read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(text, r"\.midiEvents\s*\(")

    def test_b7_construction_materialize_site_retained(self) -> None:
        rel_path, pattern = _B7_CONSTRUCTION_SITE
        hits = _scan_cpp_headers(pattern)
        self.assertIn(rel_path, hits, "B7 construction must retain device-gate materialize")
        text = (_REPO_ROOT / rel_path).read_text(encoding="utf-8", errors="replace")
        self.assertIn("Phase::Materialize", text)

    def test_b8_capture_has_no_materialize_to_event_vector(self) -> None:
        rel_path = "src/Loop/LoopCapture.cpp"
        text = (_REPO_ROOT / rel_path).read_text(encoding="utf-8", errors="replace")
        self.assertNotRegex(text, r"\bmaterializeToEventVector\s*\(")


if __name__ == "__main__":
    unittest.main()
