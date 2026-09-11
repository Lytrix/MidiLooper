#!/usr/bin/env python3
"""OpenSpec 6.1 — compatibility consumer inventory and hot-path gates."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent

_MATERIALIZE_TO_EVENT_VECTOR_RE = re.compile(r"\bmaterializeToEventVector\s*\(")
_MATERIALIZE_EDIT_VIEW_RE = re.compile(r"\bmaterializeEditViewFromPasses\s*\(")
_ENSURE_EFFECTIVE_RE = re.compile(r"\bensureEffectiveEventStoreCurrent\s*\(")
_MIDI_EVENTS_RE = re.compile(r"\.midiEvents\s*\(\s*\)")
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
        "src/Loop/LoopCapture.cpp",
        "src/Loop/LoopEditPasses.cpp",
        "include/LoopPasses.h",
    }
)

_MATERIALIZE_EDIT_VIEW_ALLOW = frozenset(
    {
        "src/Loop/LoopMaterialization.cpp",
        "src/Utils/CommittedMidiScratch.cpp",
        "src/Track/TrackDeferredMaintenance.cpp",
        "include/Loop.h",
    }
)

_MIDI_EVENTS_ALLOW = frozenset(
    {
        "src/Loop/LoopMaterialization.cpp",
        "src/Utils/CommittedMidiScratch.cpp",
        "src/EditManager/NoteEditSessionLifecycle.cpp",
        "include/Loop.h",
        "include/Track.h",
    }
)

_GATHER_COMMITTED_ALLOW = frozenset(
    {
        "src/Loop/LoopMaterialization.cpp",
        "src/Loop/LoopVisualCache.cpp",
        "src/Track/TrackPlaybackWindowBuild.cpp",
        "src/Track/TrackDeferredMaintenance.cpp",
        "src/DisplayManager/DisplayNoteResolveCommitted.cpp",
        "src/DisplayManager/DisplayColdHelpers.cpp",
        "src/DisplayManager/DisplayNoteWindowGather.cpp",
        "include/Loop.h",
    }
)

_PASSES_MATERIALIZED_STORE_ALLOW = frozenset(
    {
        "src/Loop.cpp",
        "src/Loop/LoopMaterialization.cpp",
        "include/Loop.h",
    }
)

# Named 6.2 fallback sites (plan B1–B8). B1 uses narrow C1-a exception — see
# scripts/test_b1_narrow_session_exit_compat_gate.py.
_6_2_BLOCKER_SITES: dict[str, tuple[str, re.Pattern[str]]] = {
    "B2": ("src/Utils/CommittedMidiScratch.cpp", _MATERIALIZE_EDIT_VIEW_RE),
    "B3": ("src/Loop/LoopVisualCache.cpp", _GATHER_COMMITTED_RE),
    "B4": ("src/DisplayManager/DisplayNoteResolveCommitted.cpp", _GATHER_COMMITTED_RE),
    "B5": ("src/Track/TrackDeferredMaintenance.cpp", _MATERIALIZE_EDIT_VIEW_RE),
    "B6": ("src/EditManager/NoteEditSessionLifecycle.cpp", _MIDI_EVENTS_RE),
    "B7": ("src/LoopContentResolution.cpp", _MATERIALIZE_TO_EVENT_VECTOR_RE),
    "B8": ("src/Loop/LoopCapture.cpp", _MATERIALIZE_TO_EVENT_VECTOR_RE),
}

_B1_C1_A_SITE = ("src/Track/TrackPlaybackWindowBuild.cpp", _GATHER_COMMITTED_RE)

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
            _MATERIALIZE_EDIT_VIEW_RE,
            _ENSURE_EFFECTIVE_RE,
            _MIDI_EVENTS_RE,
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

    def test_materialize_edit_view_only_in_allowlist(self) -> None:
        hits = _scan_cpp_headers(_MATERIALIZE_EDIT_VIEW_RE)
        unexpected = _unexpected(hits, _MATERIALIZE_EDIT_VIEW_ALLOW)
        self.assertEqual(unexpected, {}, unexpected)

    def test_midi_events_only_in_allowlist(self) -> None:
        hits = _scan_cpp_headers(_MIDI_EVENTS_RE)
        unexpected = _unexpected(hits, _MIDI_EVENTS_ALLOW)
        self.assertEqual(unexpected, {}, unexpected)

    def test_gather_committed_only_in_allowlist(self) -> None:
        hits = _scan_cpp_headers(_GATHER_COMMITTED_RE)
        unexpected = _unexpected(hits, _GATHER_COMMITTED_ALLOW)
        self.assertEqual(unexpected, {}, unexpected)

    def test_passes_materialized_store_confined_to_loop_owner(self) -> None:
        hits: list[str] = []
        for folder in ("src", "include"):
            for path in (_REPO_ROOT / folder).rglob("*"):
                if path.suffix not in {".cpp", ".h"}:
                    continue
                if "passesMaterializedStore" not in path.read_text(
                    encoding="utf-8", errors="replace"
                ):
                    continue
                rel = _rel(path)
                if rel not in _PASSES_MATERIALIZED_STORE_ALLOW:
                    hits.append(rel)
        self.assertEqual(hits, [])

    def test_blocker_inventory_count_stable(self) -> None:
        """6.2 must shrink this set deliberately — not by accident."""
        scans = {
            _GATHER_COMMITTED_RE: _scan_cpp_headers(_GATHER_COMMITTED_RE),
            _MATERIALIZE_TO_EVENT_VECTOR_RE: _scan_cpp_headers(
                _MATERIALIZE_TO_EVENT_VECTOR_RE
            ),
            _MATERIALIZE_EDIT_VIEW_RE: _scan_cpp_headers(_MATERIALIZE_EDIT_VIEW_RE),
            _MIDI_EVENTS_RE: _scan_cpp_headers(_MIDI_EVENTS_RE),
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

    def test_b1_c1_a_exception_site_retained(self) -> None:
        rel_path, pattern = _B1_C1_A_SITE
        hits = _scan_cpp_headers(pattern)
        self.assertIn(rel_path, hits, "B1 C1-a exception must retain guarded gather")
        text = (_REPO_ROOT / rel_path).read_text(encoding="utf-8", errors="replace")
        self.assertIn("isPlaybackSessionExitPublicationGap", text)


if __name__ == "__main__":
    unittest.main()
