#!/usr/bin/env python3
"""Tests for the overlap-resolve deduction validator and the mixed-leg content verdict it feeds.

The rule under test is the owners' (see `hitl.content_deduction`): a later producer's note covering an earlier note of
the same pitch is **HIDDEN** when the earlier NoteOn is at or after the covering NoteOn, and **SHORTENED** to the
covering NoteOn minus one when it starts before. Both are logical outcomes, so the content verdict must pass them --
but only when every dropped or moved row is explained, which is what these tests pin from both directions.

`scripts/hitl/mixed_content_report.py` inserts `scripts` on `sys.path` itself; this suite is run from `scripts`.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.content_deduction import validate_deduction  # noqa: E402
from hitl.mixed_content_report import slot_passes, verdict_from_slots  # noqa: E402
from hitl.retention_probe import analyse_content_dump  # noqa: E402

LOOP = 1536
#: A NoteOn row / a NoteOff row, as the dump writes them: (tick, type, channel, note, velocity).
ON, OFF = 144, 128
CH = 8


def on(tick: int, note: int, velocity: int = 100) -> tuple[int, int, int, int, int]:
    return (tick, ON, CH, note, velocity)


def off(tick: int, note: int) -> tuple[int, int, int, int, int]:
    return (tick, OFF, CH, note, 0)


def dump_text(length: int, groups: dict[str, list[tuple[int, int, int, int, int]]]) -> str:
    """A `!CAPTURE_CONTENT` block for these groups, with the firmware's own declared counts."""
    lines = [f"CAPTURE_CONTENT,begin,track=8,slot=3,length={length}"]
    for kind, rows in groups.items():
        family, _, index = kind.partition("#")
        lines.append(f"CAPTURE_CONTENT,rows,{family},{index},{len(rows)}")
        for tick, status, channel, note, velocity in rows:
            lines.append(f"CAPTURE_CONTENT,event,{family},{index},{tick},{status},{channel},{note},{velocity},0")
    lines.append("CAPTURE_CONTENT,end")
    return "\n".join(lines) + "\n"


class DeductionTests(unittest.TestCase):
    """The deducer's own verdict, on the shapes the bench produces and on the shapes it must refuse."""

    def test_a_note_covered_from_its_own_tick_is_hidden_and_named(self) -> None:
        """The bench case (`captures/s45-fixcamp-20261002-181255-od2`, slot 4): the overdub's note 71 covers the
        record's, and both record rows leave the effective view while storage keeps them."""
        record = [on(1238, 71), off(1250, 71)]
        overdub = [on(1225, 71), off(1241, 71)]
        result = validate_deduction(per_kind={"record#0": record, "overdub#0": overdub, "effective#0": overdub},
                                    loop_length=LOOP)
        self.assertTrue(result["checked"])
        self.assertTrue(result["accounted"], result["unexplained"])
        self.assertEqual(1, len(result["hidden"]))
        self.assertIn("record#0 note 71 (On 1238, Off 1250)", result["hidden"][0])
        self.assertIn("hidden by overdub#0 On 1225", result["hidden"][0])
        self.assertEqual([], result["unexplained"])

    def test_a_note_starting_before_its_coverer_is_shortened_not_dropped(self) -> None:
        """`note.startTick < holdPhaseTick` keeps the NoteOn and moves the NoteOff to the coverer's NoteOn - 1."""
        record = [on(1000, 60), off(1200, 60)]
        overdub = [on(1100, 60), off(1300, 60)]
        effective = [on(1000, 60), off(1099, 60), on(1100, 60), off(1300, 60)]
        result = validate_deduction(per_kind={"record#0": record, "overdub#0": overdub, "effective#0": effective},
                                    loop_length=LOOP)
        self.assertTrue(result["accounted"], result["unexplained"])
        self.assertEqual([], result["hidden"])
        self.assertEqual(1, len(result["shortened"]))
        self.assertIn("Off 1200 -> 1099", result["shortened"][0])

    def test_a_dropped_row_with_no_coverer_is_unexplained(self) -> None:
        """The direction that must still fail: a row leaves the effective view and nothing covers it."""
        record = [on(500, 62), off(600, 62)]
        overdub = [on(1225, 71), off(1241, 71)]
        effective = overdub
        result = validate_deduction(per_kind={"record#0": record, "overdub#0": overdub, "effective#0": effective},
                                    loop_length=LOOP)
        self.assertFalse(result["accounted"])
        self.assertTrue(any("note 62" in reason for reason in result["unexplained"]), result["unexplained"])

    def test_an_effective_row_in_no_storage_group_is_unexplained(self) -> None:
        """Content the device plays but does not store is never a deduction."""
        record = [on(500, 62), off(600, 62)]
        effective = [on(500, 62), off(600, 62), on(900, 70), off(950, 70)]
        result = validate_deduction(per_kind={"record#0": record, "effective#0": effective}, loop_length=LOOP)
        self.assertFalse(result["accounted"])
        self.assertTrue(any("note 70" in reason for reason in result["unexplained"]), result["unexplained"])

    def test_a_coverer_from_the_same_group_is_not_a_deduction(self) -> None:
        """Only a *later producer* covers: a note inside one pass cannot hide its own pass's row."""
        record = [on(500, 62), off(600, 62), on(550, 62), off(800, 62)]
        effective = [on(550, 62), off(800, 62)]
        result = validate_deduction(per_kind={"record#0": record, "effective#0": effective}, loop_length=LOOP)
        self.assertFalse(result["accounted"])
        self.assertEqual([], result["hidden"])

    def test_identical_views_are_accounted_with_nothing_named(self) -> None:
        """The ordinary case: no deduction exists, so the raw comparison decides and nothing is named."""
        rows = [on(100, 60), off(200, 60)]
        result = validate_deduction(per_kind={"record#0": rows, "effective#0": rows}, loop_length=LOOP)
        self.assertTrue(result["checked"])
        self.assertTrue(result["accounted"])
        self.assertEqual([], result["hidden"] + result["shortened"] + result["unexplained"])

    def test_a_dump_without_effective_rows_is_never_read_as_accounted(self) -> None:
        result = validate_deduction(per_kind={"record#0": [on(100, 60), off(200, 60)]}, loop_length=LOOP)
        self.assertFalse(result["checked"])
        self.assertFalse(result["accounted"])

    def test_a_held_coverer_covers_across_the_loop_boundary(self) -> None:
        """A note held past the loop end (no stored NoteOff) covers a wrapped note: the boundary is not a gap."""
        record = [on(20, 64), off(80, 64)]
        overdub = [on(1500, 64)]           # held: no NoteOff is stored, so it sounds to the loop end and wraps
        effective = overdub
        result = validate_deduction(per_kind={"record#0": record, "overdub#0": overdub, "effective#0": effective},
                                    loop_length=LOOP)
        self.assertTrue(result["accounted"], result["unexplained"])
        self.assertIn("hidden by overdub#0 On 1500", result["hidden"][0])


class ContentDumpWiringTests(unittest.TestCase):
    """`analyse_content_dump` states both readings, so a reader can see which one held."""

    def test_the_bench_dump_reads_as_a_validated_deduction_not_a_raw_match(self) -> None:
        record = [on(1238, 71), off(1250, 71), on(79, 63)]
        overdub = [on(1225, 71), off(1241, 71)]
        parsed = analyse_content_dump(dump_text(LOOP, {
            "record#0": record, "overdub#0": overdub,
            "effective#0": [on(79, 63)] + overdub,
        }), slot_c=3, expected_notes=())
        self.assertFalse(parsed["storage_effective_match"])
        self.assertTrue(parsed["resolved_storage_match"])
        self.assertTrue(parsed["deduction"]["accounted"])
        self.assertEqual(1, len(parsed["deduction"]["hidden"]))

    def test_an_identical_dump_reads_as_both(self) -> None:
        rows = [on(100, 60), off(200, 60)]
        parsed = analyse_content_dump(dump_text(LOOP, {"record#0": rows, "effective#0": rows}),
                                      slot_c=3, expected_notes=())
        self.assertTrue(parsed["storage_effective_match"])
        self.assertTrue(parsed["resolved_storage_match"])
        self.assertEqual([], parsed["deduction"]["hidden"])


class MixedVerdictTests(unittest.TestCase):
    """The report the accepted-sequence oracle reads, decided from two slots' dump reports."""

    @staticmethod
    def slot(slot: int, *, raw: bool | None, resolved: bool | None, complete: bool = True,
             counts: bool = True, dump_slot: int | None = None, deduction: dict | None = None) -> dict:
        return {"slot": slot, "firmware_slot": slot - 1, "complete": complete,
                "dump_slot": slot - 1 if dump_slot is None else dump_slot,
                "declared_counts_match": counts, "storage_matches_effective": raw,
                "resolved_storage_match": resolved, "deduction": deduction or {
                    "accounted": bool(resolved), "checked": resolved is not None, "hidden": [], "shortened": [],
                    "unexplained": []}}

    def test_a_validated_deduction_passes_and_is_named(self) -> None:
        deduction = {"accounted": True, "checked": True,
                     "hidden": ["record#0 note 71 (On 1238, Off 1250) hidden by overdub#0 On 1225"],
                     "shortened": [], "unexplained": []}
        report = verdict_from_slots([self.slot(4, raw=False, resolved=True, deduction=deduction),
                                     self.slot(5, raw=True, resolved=True)])
        self.assertTrue(report["pass"])
        self.assertEqual("pass", report["verdict"])
        self.assertEqual(["slot 4: record#0 note 71 (On 1238, Off 1250) hidden by overdub#0 On 1225"],
                         report["deductions"])
        self.assertEqual([], report["unexplained_manifest_differences"])

    def test_an_unexplained_remainder_fails_and_is_named(self) -> None:
        deduction = {"accounted": False, "checked": True, "hidden": [], "shortened": [],
                     "unexplained": ["record#0 NoteOn (tick 500, note 62) is not in the effective view"]}
        report = verdict_from_slots([self.slot(4, raw=False, resolved=False, deduction=deduction),
                                     self.slot(5, raw=True, resolved=True)])
        self.assertFalse(report["pass"])
        self.assertTrue(any("note 62" in reason for reason in report["unexplained_manifest_differences"]),
                        report["unexplained_manifest_differences"])

    def test_an_unresolved_slot_without_a_named_reason_still_fails(self) -> None:
        report = verdict_from_slots([self.slot(4, raw=False, resolved=False), self.slot(5, raw=True, resolved=True)])
        self.assertFalse(report["pass"])
        self.assertEqual(["mixed slot dump parity failed"], report["unexplained_manifest_differences"])

    def test_an_incomplete_or_misset_dump_never_passes(self) -> None:
        for item in (self.slot(4, raw=True, resolved=True, complete=False),
                     self.slot(4, raw=True, resolved=True, counts=False),
                     self.slot(4, raw=True, resolved=True, dump_slot=6)):
            self.assertFalse(slot_passes(item), item)

    def test_no_slots_never_reads_as_a_pass(self) -> None:
        self.assertFalse(verdict_from_slots([])["pass"])


if __name__ == "__main__":
    unittest.main()
