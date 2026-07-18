#!/usr/bin/env python3
"""Unit tests for HITL persistence row parsing (heap vs diag)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.persistence_rows import (
    extract_persistence_diag_rows,
    extract_persistence_heap_rows,
)


class PersistenceRowParseTests(unittest.TestCase):
    def test_extract_persistence_heap_rows_accepts_heap_stages_only(self) -> None:
        lines = [
            "#CAP,30156500,PERS,result,34781,28672,28672,ok",
            "#CAP,31263200,PERS,diag,463,49,16,0,0,0,0,0,42,34864,0,0,0,0",
            "#CAP,483870781,PERS,work,0,0,0,LoopPersist,slot:5:0,start,ok",
            "#CAP,5248388,PERS,mid_pass,0,19,19,ok:t4:s0:c19:n34",
        ]
        rows = extract_persistence_heap_rows(lines)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["stage"], "result")
        self.assertEqual(rows[0]["heap_before"], 28672)
        self.assertEqual(rows[0]["heap_after"], 28672)

    def test_extract_persistence_diag_rows_parses_chunk_pool(self) -> None:
        lines = [
            "#CAP,31263200,PERS,diag,463,49,16,0,0,0,0,0,42,34864,0,0,0,0",
        ]
        rows = extract_persistence_diag_rows(lines)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["free_chunks"], 463)
        self.assertEqual(rows[0]["used_chunks"], 49)
        self.assertEqual(rows[0]["chunk_reserve"], 16)


if __name__ == "__main__":
    unittest.main()
