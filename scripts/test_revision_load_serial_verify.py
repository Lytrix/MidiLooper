#!/usr/bin/env python3
"""Unit tests for revision load HITL serial verification."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.revision_load import verify_revision_load


class RevisionLoadSerialVerifyTests(unittest.TestCase):
    def test_ok_nuke_commit_load_cleanup(self) -> None:
        lines = [
            "#CAP,1,PERS,rev_nuke_sets,0,2,0,ok",
            "#CAP,2,PERS,result,100,0,0,ok",
            "#CAP,3,PERS,rev_hitl_arm,0,0,0,armed",
            "#CAP,4,PERS,rev_request,0,0,0,queued",
            "#CAP,5,PERS,rev_dispatch,0,0,0,run",
            "#CAP,6,PERS,rev_complete,0,3,1,S0003_v0001",
            "[StorageManager] Revision commit complete S3 v1",
            "#CAP,7,PERS,rev_load_request,0,3,1,queued",
            "#CAP,8,PERS,rev_load_hitl_arm,0,3,1,armed",
            "#CAP,9,PERS,rev_load_dispatch,0,3,1,run",
            "#CAP,10,PERS,rev_load_complete,0,3,1,S0003_v0001",
            "[StorageManager] Revision load complete S3 v1",
            "#CAP,11,PERS,rev_cleanup,0,0,0,ok",
        ]
        args = SimpleNamespace(
            revision_nuke_serial_anchor=0,
            revision_commit_serial_anchor=2,
            revision_load_serial_anchor=7,
            revision_cleanup_serial_anchor=11,
            require_sets_nuke=True,
            require_hitl_cleanup=True,
        )
        result = verify_revision_load(lines, args)
        self.assertTrue(result["ok"], result.get("issues"))

    def test_fail_load_id_mismatch(self) -> None:
        lines = [
            "#CAP,1,PERS,rev_nuke_sets,0,0,0,ok",
            "#CAP,2,PERS,result,100,0,0,ok",
            "#CAP,3,PERS,rev_complete,0,1,1,S0001_v0001",
            "[StorageManager] Revision commit complete S1 v1",
            "#CAP,4,PERS,rev_load_request,0,1,2,queued",
            "#CAP,5,PERS,rev_load_hitl_arm,0,1,2,armed",
            "#CAP,6,PERS,rev_load_dispatch,0,1,2,run",
            "#CAP,7,PERS,rev_load_complete,0,1,2,S0001_v0002",
            "[StorageManager] Revision load complete S1 v2",
        ]
        args = SimpleNamespace(
            revision_nuke_serial_anchor=0,
            revision_commit_serial_anchor=2,
            revision_load_serial_anchor=4,
            revision_cleanup_serial_anchor=-1,
            require_sets_nuke=True,
            require_hitl_cleanup=False,
        )
        result = verify_revision_load(lines, args)
        self.assertFalse(result["ok"])
        self.assertTrue(
            any("load_request_id_mismatch" in issue for issue in result["issues"]),
            result["issues"],
        )


if __name__ == "__main__":
    unittest.main()
