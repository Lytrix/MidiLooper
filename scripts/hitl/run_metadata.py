#!/usr/bin/env python3
"""Run metadata for the HITL regression corpus.

Writes ``metadata.json`` next to a run's report so a capture bundle can answer
the questions in HITL_ARCHITECTURE.md §11.1 — which git commit and which
firmware build generated this evidence. The write is best-effort: a missing
git binary or firmware hex records ``null`` and never fails the run.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Optional

SCHEMA_VERSION = 1
DEFAULT_BUILD_ENV = "teensy41-capture-serial"
_REPO_ROOT = Path(__file__).resolve().parents[2]


def _run_git(repo_root: Path, *args: str) -> Optional[str]:
    try:
        result = subprocess.run(
            ["git", "-C", str(repo_root), *args],
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def git_identity(repo_root: Path) -> tuple[Optional[str], Optional[bool]]:
    commit = _run_git(repo_root, "rev-parse", "HEAD")
    if commit is None:
        return None, None
    status = _run_git(repo_root, "status", "--porcelain")
    return commit, (bool(status) if status is not None else None)


def _platformio_build_dir(repo_root: Path) -> Optional[Path]:
    """The ``build_dir`` configured in platformio.ini, if present."""
    ini = repo_root / "platformio.ini"
    if not ini.is_file():
        return None
    for raw in ini.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line.startswith("build_dir") and "=" in line:
            value = line.split("=", 1)[1].strip()
            if value:
                return Path(value).expanduser()
    return None


def firmware_hex_path(repo_root: Path, build_env: str) -> Optional[Path]:
    candidates: list[Path] = []
    override = os.environ.get("PLATFORMIO_BUILD_DIR")
    if override:
        candidates.append(Path(override) / build_env / "firmware.hex")
    candidates.append(repo_root / ".pio" / "build" / build_env / "firmware.hex")
    configured = _platformio_build_dir(repo_root)
    if configured is not None:
        candidates.append(configured / build_env / "firmware.hex")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_run_metadata(
    out_dir: Path,
    *,
    preset: Optional[str] = None,
    scenario_ids: Iterable[str] = (),
    track_number: Optional[int] = None,
    loop_slot: Optional[int] = None,
    midi_channel: Optional[int] = None,
    record_bars: Optional[int] = None,
    overdub_bars: Optional[int] = None,
    build_env: str = DEFAULT_BUILD_ENV,
    capture_serial_port: Optional[str] = None,
    session_log: Optional[Path] = None,
    extra: Optional[dict] = None,
    repo_root: Optional[Path] = None,
) -> Path:
    """Write ``out_dir/metadata.json`` describing this HITL run."""
    root = Path(repo_root) if repo_root is not None else _REPO_ROOT
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    commit, dirty = git_identity(root)
    hex_path = firmware_hex_path(root, build_env)
    metadata = {
        "schema_version": SCHEMA_VERSION,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "git_commit": commit,
        "git_dirty": dirty,
        "build_env": build_env,
        "firmware_hex": str(hex_path) if hex_path is not None else None,
        "firmware_hex_sha256": _sha256(hex_path) if hex_path is not None else None,
        "preset": preset,
        "scenario_ids": list(scenario_ids or ()),
        "track_number": track_number,
        "loop_slot": loop_slot,
        "midi_channel": midi_channel,
        "record_bars": record_bars,
        "overdub_bars": overdub_bars,
        "capture_serial_port": capture_serial_port,
        "session_log": str(session_log) if session_log is not None else None,
        "out_dir": str(out),
    }
    if extra:
        metadata.update(extra)
    path = out / "metadata.json"
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return path
