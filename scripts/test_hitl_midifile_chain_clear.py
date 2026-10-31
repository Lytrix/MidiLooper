#!/usr/bin/env python3
"""Clear-verification contract for the MIDI-file chain rig (2026-10-30).

The device refuses a CLEAR while a deferred save is owed and the synchronous drain is in its
post-load holdoff, leaving the slot untouched. The rig must read that outcome from the capture log
and retry, and must never treat a refusal as a cleared slot: on the aborted recapture it did, and the
next Record press opened an overdub on the old take.

Pins the outcome classification (including the direct `DIAG,clear_refused` row and the slot identity),
the post-load-holdoff fast-retry decision, the read-only `disp_src` mode heartbeat, and the
appended-window read, which is what makes a stale clear row from an earlier attempt unable to vouch
for a later one.
"""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl_midifile_chain import (  # noqa: E402
    _assert_loop_edit_mode,
    _classify_clear,
    _clear_retry_delay,
    _read_log_since,
)


REFUSED = (
    "[582.542] [INFO] Button press: Loop 4 (long)\n"
    "[582.542] [INFO] Clear waiting for deferred save completion\n"
    "[582.542] [ERROR] Clear refused: deferred save temporarily blocked by post_load_holdoff; "
    "slot left untouched, save still owed\n"
)


class ClearVerificationTests(unittest.TestCase):
    def test_refused_is_not_success(self) -> None:
        self.assertEqual("refused", _classify_clear(REFUSED, 4))

    def test_aborted_is_not_success(self) -> None:
        self.assertEqual(
            "aborted",
            _classify_clear("[1.0] [ERROR] Clear aborted: could not complete deferred save first "
                            "(sd_timeout)", 4))

    def test_selected_slot_is_success_only_for_the_requested_slot(self) -> None:
        row = "[582.900] [INFO] MIDI: Clear selected slot 4\n"
        self.assertEqual("cleared", _classify_clear(row, 4))
        self.assertEqual("other_slot", _classify_clear(row, 5))

    def test_removed_persisted_payload_is_success(self) -> None:
        row = "[1.0] [INFO] MIDI: Clear removed persisted slot 4 payload (absent)\n"
        self.assertEqual("cleared", _classify_clear(row, 4))
        self.assertEqual("other_slot", _classify_clear(row, 2))

    def test_already_empty_slot_is_success(self) -> None:
        self.assertEqual(
            "empty",
            _classify_clear("[1.0] [DEBUG] Clear ignored — selected slot is empty (track 7 slot 3)\n", 4))
        self.assertEqual(
            "empty", _classify_clear("[1.0] [DEBUG] Clear ignored — track is empty\n", 4))

    def test_no_outcome_row_is_unknown(self) -> None:
        self.assertIsNone(_classify_clear("[1.0] [INFO] Button press: Loop 4 (long)\n", 4))

    def test_read_log_since_reads_only_the_appended_window(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "session.log"
            path.write_bytes(b"old refusal Clear refused: stale\n")
            offset = path.stat().st_size
            with path.open("ab") as handle:
                handle.write(b"new row MIDI: Clear selected slot 4\n")
            text, new_offset = _read_log_since(path, offset)
            self.assertNotIn("stale", text)
            self.assertIn("Clear selected slot 4", text)
            self.assertEqual(path.stat().st_size, new_offset)

    def test_direct_refusal_row_is_not_success(self) -> None:
        row = "#CAP,123,DIAG,clear_refused,4,post_load_holdoff\n"
        self.assertEqual("refused", _classify_clear(row, 4))
        self.assertEqual("other_slot", _classify_clear(row, 3))

    def test_post_load_holdoff_retry_is_short_and_bounded(self) -> None:
        row = "#CAP,123,DIAG,clear_refused,4,post_load_holdoff\n"
        self.assertAlmostEqual(0.2, _clear_retry_delay(row, 1.5))
        self.assertAlmostEqual(0.2, _clear_retry_delay(row, 0.05))

    def test_other_refusals_keep_the_conservative_settle(self) -> None:
        row = "#CAP,123,DIAG,clear_refused,4,transport_active\n"
        self.assertAlmostEqual(1.5, _clear_retry_delay(row, 1.5))

    def test_mode_heartbeat_proves_loop_edit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "session.log"
            path.write_text("#CAP,1,DIAG,disp_src,t=7,s=3,crev=1,cpass=1,mode=0,ret=0\n")
            self.assertEqual({"mode": "LOOP_EDIT", "source": "disp_src"},
                             _assert_loop_edit_mode(path, timeout_s=0.2))

    def test_note_edit_heartbeat_refuses_the_clear(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "session.log"
            path.write_text("#CAP,1,DIAG,disp_src,t=7,s=3,crev=1,cpass=1,mode=1,ret=0\n")
            with self.assertRaisesRegex(RuntimeError, "NOTE_EDIT"):
                _assert_loop_edit_mode(path, timeout_s=0.2)

    def test_missing_mode_heartbeat_refuses_the_clear(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "session.log"
            path.write_text("#CAP,1,DIAG,disp_turn,n=1\n")
            with self.assertRaisesRegex(RuntimeError, "cannot prove LOOP_EDIT"):
                _assert_loop_edit_mode(path, timeout_s=0.05)

    def test_a_stale_success_cannot_vouch_for_a_later_attempt(self) -> None:
        """The appended-window read is what keeps attempt N from re-reading attempt N-1's success."""
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "session.log"
            path.write_bytes(b"[1.0] [INFO] MIDI: Clear selected slot 4\n")
            offset = path.stat().st_size
            with path.open("ab") as handle:
                handle.write(REFUSED.encode())
            text, _ = _read_log_since(path, offset)
            self.assertNotIn("Clear selected slot", text)
            self.assertEqual("refused", _classify_clear(text, 4))


if __name__ == "__main__":
    unittest.main()
