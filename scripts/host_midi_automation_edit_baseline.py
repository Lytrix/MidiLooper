#!/usr/bin/env python3
"""Legacy note-edit HITL CLI — thin shim over hitl.legacy_edit_baseline.

Prefer: scripts/host_midi_hitl.py run --preset edit_full
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl import legacy_edit_baseline as _impl


def __getattr__(name: str) -> Any:
    return getattr(_impl, name)


def __dir__() -> list[str]:
    return sorted(set(dir(_impl)) | {"__getattr__", "__dir__"})


if __name__ == "__main__":
    if "-h" in sys.argv or "--help" in sys.argv:
        raise SystemExit(_impl.main())
    from hitl.runner import main as hitl_main

    raise SystemExit(hitl_main(["run", "--preset", "edit_full", *sys.argv[1:]]))
