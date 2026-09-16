#!/usr/bin/env python3
"""Unit tests for HITL run metadata (regression corpus identity)."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.run_metadata import SCHEMA_VERSION, firmware_hex_path, git_identity, write_run_metadata


class RunMetadataTests(unittest.TestCase):
    def test_writes_identity_and_config(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "run"
            path = write_run_metadata(
                out,
                preset="base",
                scenario_ids=("base", "record_overdub"),
                track_number=5,
                loop_slot=1,
                midi_channel=5,
                record_bars=4,
                overdub_bars=2,
                build_env="__no_such_env__",
                capture_serial_port="/dev/cu.usbmodem154944801",
                session_log=Path("/tmp/session.log"),
            )
            self.assertTrue(path.is_file())
            meta = json.loads(path.read_text())
            self.assertEqual(meta["schema_version"], SCHEMA_VERSION)
            self.assertEqual(meta["preset"], "base")
            self.assertEqual(meta["scenario_ids"], ["base", "record_overdub"])
            self.assertEqual(meta["track_number"], 5)
            self.assertEqual(meta["loop_slot"], 1)
            self.assertEqual(meta["midi_channel"], 5)
            self.assertEqual(meta["record_bars"], 4)
            self.assertEqual(meta["overdub_bars"], 2)
            self.assertEqual(meta["session_log"], "/tmp/session.log")
            self.assertEqual(meta["build_env"], "__no_such_env__")
            self.assertIn("timestamp", meta)

    def test_git_commit_is_recorded_for_this_repo(self) -> None:
        commit, dirty = git_identity(Path(__file__).resolve().parents[1])
        self.assertIsNotNone(commit)
        self.assertEqual(len(commit), 40)
        self.assertIn(dirty, (True, False))

    def test_firmware_hash_from_build_dir_override(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / "teensy41-capture-serial"
            build.mkdir(parents=True)
            hex_path = build / "firmware.hex"
            hex_path.write_bytes(b":00000001FF\n")
            old = os.environ.get("PLATFORMIO_BUILD_DIR")
            os.environ["PLATFORMIO_BUILD_DIR"] = tmp
            try:
                found = firmware_hex_path(Path(__file__).resolve().parents[1], "teensy41-capture-serial")
                self.assertEqual(found, hex_path)
                out = Path(tmp) / "run"
                write_run_metadata(out, build_env="teensy41-capture-serial")
                meta = json.loads((out / "metadata.json").read_text())
                self.assertEqual(meta["firmware_hex"], str(hex_path))
                self.assertEqual(
                    meta["firmware_hex_sha256"],
                    hashlib.sha256(b":00000001FF\n").hexdigest(),
                )
            finally:
                if old is None:
                    os.environ.pop("PLATFORMIO_BUILD_DIR", None)
                else:
                    os.environ["PLATFORMIO_BUILD_DIR"] = old

    def test_missing_firmware_records_null(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "run"
            write_run_metadata(out, build_env="__definitely_absent_env__")
            meta = json.loads((out / "metadata.json").read_text())
            self.assertIsNone(meta["firmware_hex"])
            self.assertIsNone(meta["firmware_hex_sha256"])

    def test_non_repo_git_identity_is_null(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            commit, dirty = git_identity(Path(tmp))
            self.assertIsNone(commit)
            self.assertIsNone(dirty)


if __name__ == "__main__":
    unittest.main()
