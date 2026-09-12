#!/usr/bin/env python3
"""OpenSpec 6.3.7 — run all compatibility machinery retirement host gates."""

from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent

_GATE_SCRIPTS = (
    "scripts/test_compatibility_consumer_inventory.py",
    "scripts/test_b1_narrow_session_exit_compat_gate.py",
    "scripts/test_b2_b5_b6_narrow_session_exit_compat_gate.py",
    "scripts/test_b3_b4_narrow_session_exit_compat_gate.py",
    "scripts/test_b7_b8_construction_consumer_gate.py",
)


class MachineryCloseoutGateTests(unittest.TestCase):
    def test_all_6_3_host_gates_pass(self) -> None:
        failures: list[str] = []
        for rel in _GATE_SCRIPTS:
            proc = subprocess.run(
                [sys.executable, str(_REPO_ROOT / rel)],
                cwd=_REPO_ROOT,
                capture_output=True,
                text=True,
            )
            if proc.returncode != 0:
                failures.append(f"{rel} failed:\n{proc.stdout}\n{proc.stderr}")
        self.assertEqual(failures, [], "\n\n".join(failures))


if __name__ == "__main__":
    unittest.main()
