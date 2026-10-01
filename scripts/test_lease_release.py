#!/usr/bin/env python3
"""`hitl.lease_release` + the S4.1 device-leg reader: the release row is read by name, absence stays absence.

Every row string below is **verbatim** from a real capture, with its provenance named, so the reader stays tied
to what the firmware actually prints (`test_capture_line_tier` pins the same wire form on the firmware side).
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.lease_release import (  # noqa: E402
    cap_row_body,
    lease_release_rows,
    outcome_for_slot,
    release_context,
    release_rows_for_slot,
    released_slot_numbers,
)
import hitl_s41_lease_release_read as reader  # noqa: E402

# Verbatim rows, each with the capture it came from.
TODAY_ROW = "#CAP,69967546,DIAG,ws_release,0,0,released"                     # captures/session_20261001_092818.log
PASS_ROW = "#CAP,3794271032,DIAG,ws_release,0,3,released"                    # captures/session_20260930_142350.log (base PASS)
SWITCH_TARGET_ROW = "#CAP,486025325,DIAG,ws_release,4,1,released"            # captures/s41-lease-20260927_013834
STATS_ROW = "#CAP,79579062,RING,record_stats,DIAG,latch,entered=0,enq=0,rej=0,tx=0,evict=0"


class RowReadingTests(unittest.TestCase):
    def test_the_retained_pass_run_reads_the_same_row_shape(self):
        """The base preset's own PASS run (identity hex 577841d6) released its slot under test at generation 3."""
        rows = lease_release_rows([PASS_ROW])
        self.assertEqual("released", outcome_for_slot(rows, 0))
        self.assertEqual((3, 3_794_271_032), (rows[0].generation, rows[0].us))

    def test_every_release_row_is_read_with_its_line_and_both_slot_bases(self):
        lines = [PASS_ROW, STATS_ROW, TODAY_ROW]
        rows = lease_release_rows(lines)
        self.assertEqual(2, len(rows))
        first = rows[0]
        self.assertEqual((0, 3, "released", 1), (first.slot0, first.generation, first.outcome, first.line_no))
        self.assertEqual(1, first.slot_number)  # 0-based row field -> the button's `Loop 1`
        self.assertEqual(PASS_ROW, first.raw)
        self.assertEqual(TODAY_ROW, rows[1].raw)
        self.assertEqual("DIAG,ws_release,0,0,released", rows[1].body)

    def test_the_family_stats_readback_is_not_a_release_row(self):
        """`RING,record_stats,DIAG,latch,...` counts the latch family; it names no lease and no slot."""
        self.assertEqual([], lease_release_rows([STATS_ROW, "#CAP,1,RID,stats,entered=0,enq=0,rej=0,tx=0,evict=0"]))
        self.assertIsNone(cap_row_body("DIAG,ws_release,0,0,released"))  # a bare row is not a captured row
        self.assertIsNone(cap_row_body(""))

    def test_a_sibling_slots_release_never_stands_in_for_the_slot_under_test(self):
        """The measured trap: on a switch shape the row names the *incoming* slot, not the retired one."""
        rows = lease_release_rows([SWITCH_TARGET_ROW])
        self.assertEqual("released", outcome_for_slot(rows, 4))
        self.assertEqual("absent", outcome_for_slot(rows, 0))
        self.assertEqual([5], released_slot_numbers(rows))  # 1-based: firmware slot 4 is `Loop 5`

    def test_absence_and_non_released_outcomes_are_named_not_counted_as_success(self):
        self.assertEqual("absent", outcome_for_slot([], 0))
        refused = lease_release_rows(["#CAP,100,DIAG,ws_release,0,0,refused_busy"])
        self.assertEqual("refused_busy", outcome_for_slot(refused, 0))
        self.assertEqual([], released_slot_numbers(refused))
        stale = lease_release_rows(["#CAP,101,DIAG,ws_release,0,1,stale_generation"])
        self.assertEqual("stale_generation", outcome_for_slot(stale, 0))
        self.assertEqual([], released_slot_numbers(stale))
        # A refusal followed by the release on the same slot is the released slot: the row is the evidence.
        both = lease_release_rows(["#CAP,100,DIAG,ws_release,0,0,refused_busy",
                                   "#CAP,200,DIAG,ws_release,0,1,released"])
        self.assertEqual("released", outcome_for_slot(both, 0))
        self.assertEqual(2, len(release_rows_for_slot(both, 0)))  # the refusal *and* the release are reported


class ContextTests(unittest.TestCase):
    """The edge is attributed by device clock, because a managed capture writes the two streams apart."""

    def test_the_gesture_that_caused_the_reset_is_quoted_and_a_distant_stop_is_not(self):
        lines = [
            "#CAP,58017858,DIAG,reentry,bound,9876,3,0,0",          # ~12 s before the release (the retirement)
            "#CAP,58017991,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "[69.966] [INFO] MIDI: Undo (entries=3)",                # the undo that resets the slot's playback state
            "#CAP,69966339,GS,15,36,1",                              # the undo's own gesture row
            "#CAP,69966528,MO,128,5,25,0",                           # its note-off burst: noise, not attribution
            TODAY_ROW,
        ]
        rows = lease_release_rows(lines)
        self.assertEqual(1, len(rows))
        quoted = release_context(lines, rows[0])
        self.assertEqual(["[69.966] [INFO] MIDI: Undo (entries=3)", "#CAP,69966339,GS,15,36,1"], quoted)

    def test_the_release_row_does_not_quote_itself(self):
        lines = ["#CAP,69966339,GS,15,36,1", TODAY_ROW]
        rows = lease_release_rows(lines)
        self.assertNotIn(TODAY_ROW, release_context(lines, rows[0]))

    def test_a_capture_with_no_marker_rows_quotes_nothing_rather_than_guessing(self):
        lines = ["#CAP,100,MO,128,5,25,0", TODAY_ROW]
        rows = lease_release_rows(lines)
        self.assertEqual([], release_context(lines, rows[0]))


class ReaderCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def _log(self, name: str, lines: list[str]) -> Path:
        path = self.root / name
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    def test_the_slot_under_test_passing_returns_zero_and_writes_the_record(self):
        log = self._log("run.log", ["[69.966] [INFO] MIDI: Undo (entries=3)",
                                    "#CAP,69966339,GS,15,36,1", TODAY_ROW])
        metadata = self.root / "metadata.json"
        metadata.write_text(json.dumps({"git_commit": "f63d5a02", "firmware_hex_sha256": "577841d6",
                                        "preset": "base", "track_number": 5, "loop_slot": 1}), encoding="utf-8")
        out = self.root / "s41-lease-release.json"
        code = reader.main(["--log", str(log), "--slot", "1", "--metadata", str(metadata), "--out", str(out)])
        self.assertEqual(0, code)
        record = json.loads(out.read_text(encoding="utf-8"))
        self.assertEqual("LEASE_RELEASE_OBSERVED", record["verdict"])
        self.assertEqual([TODAY_ROW], record["release_rows"])
        self.assertEqual([1], record["released_slots_1based"])
        self.assertEqual(0, record["slot0"])
        self.assertEqual("f63d5a02", record["identity"]["git_commit"])
        self.assertIn("[69.966] [INFO] MIDI: Undo (entries=3)", record["edge"])

    def test_a_sibling_slots_row_and_an_absent_slot_are_not_observed(self):
        log = self._log("sibling.log", [SWITCH_TARGET_ROW])
        self.assertEqual(2, reader.main(["--log", str(log), "--slot", "1"]))
        self.assertEqual(0, reader.main(["--log", str(log), "--slot", "5"]))
        empty = self._log("empty.log", ["#CAP,1,ST,Track,STOPPED,STOPPED"])
        self.assertEqual(2, reader.main(["--log", str(empty), "--slot", "1"]))

    def test_a_required_generation_is_not_satisfied_by_another_generation(self):
        log = self._log("gen.log", [TODAY_ROW])  # generation 0
        self.assertEqual(0, reader.main(["--log", str(log), "--slot", "1", "--generation", "0"]))
        self.assertEqual(2, reader.main(["--log", str(log), "--slot", "1", "--generation", "3"]))

    def test_a_missing_log_is_not_a_verdict(self):
        self.assertEqual(3, reader.main(["--log", str(self.root / "nope.log"), "--slot", "1"]))

    def test_unreadable_metadata_is_quoted_as_absent_not_guessed(self):
        log = self._log("run.log", [TODAY_ROW])
        broken = self.root / "broken.json"
        broken.write_text("{not json", encoding="utf-8")
        out = self.root / "rec.json"
        self.assertEqual(0, reader.main(["--log", str(log), "--slot", "1", "--metadata", str(broken),
                                         "--out", str(out)]))
        self.assertEqual({}, json.loads(out.read_text(encoding="utf-8"))["identity"])


if __name__ == "__main__":
    unittest.main()
