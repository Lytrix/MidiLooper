#!/usr/bin/env python3
"""OpenSpec 5.5 — sliced LoopPersist verify + persist execute call-site gate."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _SCRIPT_DIR.parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.deferred_persist_nonblocking import (  # noqa: E402
    deferred_persist_nonblocking_issues,
    summarize_deferred_persist_nonblocking,
)

_PROCESS_RE = re.compile(r"StorageManager::processDeferredSaveState\s*\(")
_SAVE_STATE_RE = re.compile(r"StorageManager::saveState\s*\(")
_WRITE_LOOP_RE = re.compile(r"\bwriteLoopPersisted\s*\(")

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
        self.assertEqual(summary["loop_persist_work_count"], 3)
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

    def test_single_work_row_is_not_sliced(self) -> None:
        lines = ["#CAP,200,PERS,work,40,0,0,LoopPersist,slot:4:0,done,ok"]
        summary = summarize_deferred_persist_nonblocking(lines, record_stop_ts=150)
        self.assertFalse(summary["loop_persist_sliced"])
        self.assertIn(
            "persistence_loop_persist_not_sliced",
            deferred_persist_nonblocking_issues(summary),
        )

    def test_capture_181945_sliced_without_drain(self) -> None:
        path = _REPO_ROOT / "captures" / "session_20260911_181945.log"
        if not path.exists():
            self.skipTest("181945 capture missing")
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        summary = summarize_deferred_persist_nonblocking(lines, record_stop_ts=1)
        self.assertGreaterEqual(int(summary["loop_persist_work_count"]), 2)
        self.assertTrue(summary["loop_persist_sliced"])
        self.assertEqual(int(summary["sync_drain_count"]), 0)
        self.assertTrue(summary["ok"])

    def test_capture_192637_sliced_with_mo_interleave(self) -> None:
        path = _REPO_ROOT / "captures" / "session_20260911_192637.log"
        if not path.exists():
            self.skipTest("192637 capture missing")
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        summary = summarize_deferred_persist_nonblocking(lines, record_stop_ts=1)
        self.assertTrue(summary["ok"])
        self.assertGreater(int(summary["mo_during_loop_persist"]), 0)


class PersistExecuteCallSiteGateTests(unittest.TestCase):
    def test_process_deferred_save_state_allowlist(self) -> None:
        self.assertEqual(_scan(_PROCESS_RE, _PROCESS_ALLOW), [])

    def test_save_state_allowlist(self) -> None:
        self.assertEqual(_scan(_SAVE_STATE_RE, _SAVE_STATE_ALLOW), [])

    def test_write_loop_persisted_allowlist(self) -> None:
        self.assertEqual(_scan(_WRITE_LOOP_RE, _WRITE_LOOP_ALLOW), [])


if __name__ == "__main__":
    unittest.main()
