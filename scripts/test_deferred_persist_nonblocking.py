#!/usr/bin/env python3
"""OpenSpec 5.5–5.6 persist verification: call-site gates + HITL capture checks."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _SCRIPT_DIR.parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.deferred_persist_large_loop_bounds import (  # noqa: E402
    deferred_persist_large_loop_bounds_issues,
    summarize_deferred_persist_large_loop_bounds,
)
from hitl.verify.deferred_persist_nonblocking import (  # noqa: E402
    deferred_persist_nonblocking_issues,
    summarize_deferred_persist_nonblocking,
)

_PROCESS_RE = re.compile(r"StorageManager::processDeferredSaveState\s*\(")
_SAVE_STATE_RE = re.compile(r"StorageManager::saveState\s*\(")
_WRITE_LOOP_RE = re.compile(r"\bwriteLoopPersisted\s*\(")
_REQUEST_RE = re.compile(r"StorageManager::requestDeferredSaveState\s*\(")

_PROCESS_ALLOW = {
    "src/main.cpp",
    "src/StorageManager/DeferredSaveScheduler.cpp",
    "src/StorageManager/SyncDrainSaveState.cpp",
    "include/StorageManager.h",
}
_SAVE_STATE_ALLOW = {
    "src/StorageManager.cpp",
    "src/MidiButtonActions.cpp",
    "src/StorageManager/SavedSetIo.cpp",
    "include/StorageManager.h",
}
_WRITE_LOOP_ALLOW = {
    "src/StorageLoopIo.cpp",
    "include/StorageLoopIo.h",
}

_TIMING_CRITICAL_FORBID_PATTERNS: tuple[tuple[re.Pattern[str], set[str]], ...] = (
    (_PROCESS_RE, {"src/MidiHandler.cpp", "src/ClockManager.cpp"}),
    (
        _PROCESS_RE,
        {
            "src/TrackManager/TrackManagerTransportTick.cpp",
            "src/Track/TrackCaptureStopCommit.cpp",
        },
    ),
    (_SAVE_STATE_RE, {"src/Track/TrackCaptureStopCommit.cpp", "src/ClockManager.cpp"}),
    (_WRITE_LOOP_RE, {"src/Track/TrackCaptureStopCommit.cpp", "src/ClockManager.cpp"}),
)

_STOP_PATH_FILES = {
    "src/Track/TrackCaptureStopCommit.cpp",
}


def _rel(path: Path) -> str:
    return path.relative_to(_REPO_ROOT).as_posix()


def _scan(pattern: re.Pattern[str], allow: set[str]) -> list[str]:
    hits: list[str] = []
    for folder in ("src", "include"):
        root = _REPO_ROOT / folder
        for path in root.rglob("*"):
            if path.suffix not in {".cpp", ".h"}:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if pattern.search(text) and _rel(path) not in allow:
                hits.append(_rel(path))
    return sorted(hits)


def _scan_forbidden_in_files(
    pattern: re.Pattern[str],
    forbidden_files: set[str],
) -> list[str]:
    hits: list[str] = []
    for rel in sorted(forbidden_files):
        path = _REPO_ROOT / rel
        if not path.exists():
            continue
        if pattern.search(path.read_text(encoding="utf-8", errors="replace")):
            hits.append(rel)
    return hits


class DeferredPersistNonblockingTests(unittest.TestCase):
    def test_sliced_loop_persist_without_drain_is_ok(self) -> None:
        lines = [
            "#CAP,100,ST,Track,5,RECORDING",
            "#CAP,200,PERS,work,40,0,0,LoopPersist,slot:4:0,start,ok",
            "#CAP,210,PERS,work,80,0,0,LoopPersist,slot:4:0,payload_slice,ok",
            "#CAP,220,PERS,work,90,0,0,LoopPersist,slot:4:0,done,ok",
            "#CAP,230,PERS,result,100,1,1,ok",
        ]
        summary = summarize_deferred_persist_nonblocking(lines, record_stop_ts=150)
        self.assertTrue(summary["ok"])
        self.assertEqual(deferred_persist_nonblocking_issues(summary), [])

    def test_sync_drain_after_stop_fails(self) -> None:
        lines = [
            "#CAP,200,PERS,work,40,0,0,LoopPersist,slot:4:0,start,ok",
            "#CAP,210,PERS,work,90,0,0,LoopPersist,slot:4:0,done,ok",
            "#CAP,220,PERS,drain,clear,8,0,1,0,8,100",
        ]
        summary = summarize_deferred_persist_nonblocking(lines, record_stop_ts=150)
        self.assertFalse(summary["ok"])
        self.assertIn(
            "persistence_sync_drain_after_record_stop",
            deferred_persist_nonblocking_issues(summary),
        )

    def test_capture_200657_sliced_without_drain(self) -> None:
        path = _REPO_ROOT / "captures" / "session_20260911_200657.log"
        if not path.exists():
            self.skipTest("200657 capture missing")
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        summary = summarize_deferred_persist_nonblocking(lines, record_stop_ts=1)
        self.assertTrue(summary["ok"])


class DeferredPersistLargeLoopBoundsTests(unittest.TestCase):
    def test_qualified_silent_reader_witness_and_rejection_controls(self) -> None:
        rows = [
            "#CAP,1000000,ST,Track,STOPPED,PLAYING",
            "#CAP,2000000,PERS,work,40,0,0,LoopPersist,slot:7:4,start,ok",
            "#CAP,2010000,PERS,work,80,0,0,LoopPersist,slot:7:4,payload_slice,ok",
            "#CAP,2020000,PERS,work,90,0,0,LoopPersist,slot:7:4,payload_done,ok",
            "#CAP,2030000,PERS,work,70,0,0,LoopPersist,slot:7:4,finalize_slice,ok",
            "#CAP,2040000,PERS,work,60,0,0,LoopPersist,slot:7:4,done,ok",
        ]
        witness = "#CAP,2025000,DIAG,playback_read,1,7,3,1024,5,29,2,768,768,1536,1024"
        summarize = lambda extra: summarize_deferred_persist_large_loop_bounds(rows + extra)
        self.assertFalse(summarize([])["active_snapshot_ok"])
        self.assertTrue(summarize([witness])["active_snapshot_ok"])
        self.assertTrue(summarize([witness.replace(",5,29,2,", ",0,29,2,")])["active_snapshot_ok"])
        available = witness.replace("playback_read", "playback_available").replace(",5,29,2,", ",0,2,0,")
        self.assertTrue(summarize([available])["active_snapshot_ok"])
        self.assertFalse(summarize([available.replace(",0,2,0,", ",0,2,2,")])["active_snapshot_ok"])
        for bad in (
            witness.replace("2025000", "1000000"),
            witness.replace("playback_read,1,", "playback_read,2,"),
            witness.replace(",5,29,2,", ",4294967296,29,2,"),
            witness.replace(",5,29,2,", ",5,0,2,"),
            witness.replace(",5,29,2,", ",5,29,3,"),
            witness.replace(",1536,1024", ",1536,64"),
            witness.replace(",768,768,1536,", ",768,0,1536,"),
            witness.replace("read,1,7,3,", "read,1,8,3,"),
            witness + ",extra",
            witness.rsplit(",", 1)[0],
        ):
            with self.subTest(row=bad):
                self.assertFalse(summarize([bad])["active_snapshot_ok"])

    def test_progress_phases_required(self) -> None:
        lines = [
            "#CAP,200,PERS,work,40,0,0,LoopPersist,slot:4:0,start,ok",
            "#CAP,210,PERS,work,80,0,0,LoopPersist,slot:4:0,done,ok",
        ]
        summary = summarize_deferred_persist_large_loop_bounds(lines, record_stop_ts=150)
        self.assertFalse(summary["progress_ok"])
        self.assertIn(
            "persistence_loop_persist_progress_incomplete",
            deferred_persist_large_loop_bounds_issues(summary),
        )

    def test_full_progress_with_active_snapshot(self) -> None:
        lines = [
            "#CAP,100,RECS,stage,save_request,0,0,4294967295,4294967295,0,0,requested",
            "#CAP,200,PERS,work,40,0,0,LoopPersist,slot:4:0,start,ok",
            "#CAP,210,PERS,work,80,0,0,LoopPersist,slot:4:0,payload_slice,ok",
            "#CAP,220,PERS,work,90,0,0,LoopPersist,slot:4:0,payload_done,ok",
            "#CAP,230,PERS,work,70,0,0,LoopPersist,slot:4:0,finalize_slice,ok",
            "#CAP,240,PERS,work,60,0,0,LoopPersist,slot:4:0,done,ok",
            "#CAP,225,MO,60,5,1,0",
            "#CAP,235,DIAG,persist_save,5000,0",
        ]
        summary = summarize_deferred_persist_large_loop_bounds(lines, record_stop_ts=150)
        self.assertTrue(summary["ok"])

    def test_idle_persist_window_skips_active_snapshot_requirement(self) -> None:
        lines = [
            "#CAP,100,ST,Track,PLAYING,STOPPED",
            "#CAP,200,PERS,work,40,0,0,LoopPersist,slot:4:0,start,ok",
            "#CAP,210,PERS,work,80,0,0,LoopPersist,slot:4:0,payload_slice,ok",
            "#CAP,220,PERS,work,90,0,0,LoopPersist,slot:4:0,payload_done,ok",
            "#CAP,230,PERS,work,70,0,0,LoopPersist,slot:4:0,finalize_slice,ok",
            "#CAP,240,PERS,work,60,0,0,LoopPersist,slot:4:0,done,ok",
            "#CAP,150,RECS,stage,save_request,0,0,4294967295,4294967295,0,0,requested",
        ]
        summary = summarize_deferred_persist_large_loop_bounds(lines, record_stop_ts=1)
        self.assertTrue(summary["active_snapshot_ok"])
        self.assertEqual(summary["playback_active_persist_windows"], 0)

    def test_capture_200657_large_loop_bounds(self) -> None:
        path = _REPO_ROOT / "captures" / "session_20260911_200657.log"
        if not path.exists():
            self.skipTest("200657 capture missing")
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        summary = summarize_deferred_persist_large_loop_bounds(lines, record_stop_ts=1)
        self.assertTrue(summary["progress_ok"])
        self.assertTrue(summary["slice_bounds_ok"])
        self.assertTrue(summary["active_snapshot_ok"])
        self.assertTrue(summary["stop_path_ok"])
        self.assertTrue(summary["ok"])
        self.assertEqual(deferred_persist_large_loop_bounds_issues(summary), [])


class PersistExecuteCallSiteGateTests(unittest.TestCase):
    def test_process_deferred_save_state_allowlist(self) -> None:
        self.assertEqual(_scan(_PROCESS_RE, _PROCESS_ALLOW), [])

    def test_save_state_allowlist(self) -> None:
        self.assertEqual(_scan(_SAVE_STATE_RE, _SAVE_STATE_ALLOW), [])

    def test_write_loop_persisted_allowlist(self) -> None:
        self.assertEqual(_scan(_WRITE_LOOP_RE, _WRITE_LOOP_ALLOW), [])

    def test_timing_critical_paths_forbid_persist_execute(self) -> None:
        for pattern, forbidden in _TIMING_CRITICAL_FORBID_PATTERNS:
            self.assertEqual(_scan_forbidden_in_files(pattern, forbidden), [])

    def test_stop_path_admits_without_execute(self) -> None:
        for rel in _STOP_PATH_FILES:
            text = (_REPO_ROOT / rel).read_text(encoding="utf-8", errors="replace")
            self.assertIn("requestDeferredSaveState", text)
            self.assertIn("admitLoopSlotPersist", text)
            self.assertNotRegex(text, _PROCESS_RE)
            self.assertNotRegex(text, _SAVE_STATE_RE)
            self.assertNotRegex(text, _WRITE_LOOP_RE)


if __name__ == "__main__":
    unittest.main()
