#!/usr/bin/env python3
"""Unit tests for revision commit save HITL serial verification."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.revision_commit_save import verify_revision_commit_save


class RevisionCommitSaveSerialVerifyTests(unittest.TestCase):
    def test_ok_commit_and_cleanup(self) -> None:
        lines = [
            "#CAP,1,PERS,result,100,0,0,ok",
            "#CAP,2,PERS,rev_hitl_arm,0,0,0,armed",
            "#CAP,3,PERS,rev_request,0,0,0,queued",
            "#CAP,4,PERS,rev_dispatch,0,0,0,run",
            "#CAP,5,PERS,rev_complete,0,3,1,S0003_v0001",
            "[StorageManager] Revision commit complete S3 v1",
            "#CAP,6,PERS,rev_cleanup,0,0,0,ok",
        ]
        args = SimpleNamespace(
            revision_commit_serial_anchor=1,
            revision_cleanup_serial_anchor=6,
            require_hitl_cleanup=True,
        )
        result = verify_revision_commit_save(lines, args)
        self.assertTrue(result["ok"], result.get("issues"))

    def test_fail_missing_cleanup(self) -> None:
        lines = [
            "#CAP,1,PERS,rev_request,0,0,0,queued",
            "#CAP,2,PERS,rev_hitl_arm,0,0,0,armed",
            "#CAP,3,PERS,rev_dispatch,0,0,0,run",
            "#CAP,4,PERS,rev_complete,0,1,1,S0001_v0001",
            "[StorageManager] Revision commit complete S1 v1",
        ]
        args = SimpleNamespace(
            revision_commit_serial_anchor=0,
            revision_cleanup_serial_anchor=5,
            require_hitl_cleanup=True,
        )
        result = verify_revision_commit_save(lines, args)
        self.assertFalse(result["ok"])
        self.assertIn("missing_rev_cleanup_ok_after_cleanup_anchor", result["issues"])


if __name__ == "__main__":
    unittest.main()
