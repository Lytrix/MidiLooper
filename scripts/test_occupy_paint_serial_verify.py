#!/usr/bin/env python3
"""Host tests for occupy-paint HITL schedule and serial verifier."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.control_constants import TICKS_PER_BAR, TICKS_PER_BEAT
from hitl.flows.occupy_paint import (
    HALF_BAR_TICKS,
    PHRASE_SHORTEN_HIDE,
    QUARTER_BAR_TICKS,
    RECORD_SEED_PITCH,
    SAME_START_PITCH,
    SOURCE_PHRASE,
    WRAP_PITCHES,
    loop_wrap_occupy_edges,
    occupy_midi_edges,
    occupy_overdub_note_count,
    occupy_overdub_notes_min,
    occupy_record_note_count,
    occupy_record_notes_min,
    occupy_source_note_count,
    occupy_source_notes_min,
    record_midi_edges,
    source_overdub_midi_edges,
    wrap_note_count,
)
from hitl.verify.occupy_paint import verify_occupy_paint


class OccupyPaintScheduleTests(unittest.TestCase):
    def test_record_seed_is_not_occupy_phrase(self) -> None:
        edges = record_midi_edges(loop_bars=4)
        ons = [(tick, pitch) for tick, pitch, velocity in edges if velocity > 0]
        self.assertEqual(occupy_record_note_count(), 4)
        self.assertEqual(occupy_record_notes_min(), 3)
        self.assertEqual(len(ons), 4)
        ticks = [tick for tick, _pitch, _velocity in edges]
        self.assertEqual(ticks, sorted(ticks))
        source_pitches = {pitch for _beat, pitch in SOURCE_PHRASE}
        for tick, pitch in ons:
            self.assertEqual(pitch, RECORD_SEED_PITCH)
            self.assertNotIn(pitch, source_pitches)
            self.assertEqual(tick % TICKS_PER_BAR, 0)
            self.assertIn((tick + HALF_BAR_TICKS, pitch, 0), edges)

    def test_first_overdub_writes_unshifted_source_phrase(self) -> None:
        ons = [
            (tick, pitch)
            for tick, pitch, velocity in source_overdub_midi_edges(loop_bars=4)
            if velocity > 0
        ]
        self.assertEqual(occupy_source_note_count(), 16)
        self.assertEqual(occupy_source_notes_min(), 15)
        for bar in range(4):
            bar_tick = bar * TICKS_PER_BAR
            for beat, pitch in SOURCE_PHRASE:
                self.assertIn((bar_tick + (beat * TICKS_PER_BEAT), pitch), ons)

    def test_second_overdub_same_start_plus_quarter_then_minus_half(self) -> None:
        source_ons = {
            (tick, pitch)
            for tick, pitch, velocity in source_overdub_midi_edges(loop_bars=4)
            if velocity > 0
        }
        occupy_ons = {
            (tick, pitch)
            for tick, pitch, velocity in occupy_midi_edges(loop_bars=4)
            if velocity > 0
        }
        loop_ticks = 4 * TICKS_PER_BAR
        hide_shift = QUARTER_BAR_TICKS - HALF_BAR_TICKS
        self.assertEqual(hide_shift, -QUARTER_BAR_TICKS)
        self.assertIn((0, SAME_START_PITCH), occupy_ons)
        self.assertIn((0, SAME_START_PITCH), source_ons)
        self.assertIn((QUARTER_BAR_TICKS, 48), occupy_ons)
        self.assertIn((2 * TICKS_PER_BEAT + QUARTER_BAR_TICKS, 52), occupy_ons)
        self.assertIn((loop_ticks + hide_shift, 48), occupy_ons)
        self.assertNotIn((QUARTER_BAR_TICKS, SAME_START_PITCH), occupy_ons)
        same_start = [pitch for tick, pitch in occupy_ons if pitch == SAME_START_PITCH]
        self.assertEqual(len(same_start), 4)
        shorten_hide = [pitch for tick, pitch in occupy_ons if pitch in {p for _beat, p in PHRASE_SHORTEN_HIDE}]
        self.assertEqual(len(shorten_hide), 16)

    def test_note_count_and_loop_wraps(self) -> None:
        wraps = loop_wrap_occupy_edges(loop_bars=4)
        self.assertEqual(occupy_overdub_note_count(), 20)
        self.assertEqual(occupy_overdub_notes_min(), 19)
        self.assertEqual(wrap_note_count(), 2)
        self.assertEqual(len(wraps), 2)
        for on_tick, off_tick, pitch in wraps:
            self.assertIn(pitch, WRAP_PITCHES)
            self.assertGreaterEqual(on_tick, 4 * TICKS_PER_BAR - HALF_BAR_TICKS)
            self.assertLess(off_tick, HALF_BAR_TICKS)
            self.assertLess(off_tick, on_tick)


class OccupyPaintSerialVerifyTests(unittest.TestCase):
    def test_pass_shorten_hide_od2_and_gus(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,overlap_hold,note_offs,16,empty_sets,0,max_ids,0,overflows,0,"
            "looked_up,0,max_examined,0,sum_examined,0,max_lookup_us,0,sum_lookup_us,0,"
            "add,16,shorten,0,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3100,DIAG,draw_src,layers,visual,80,preview,1,sealed,0,snap,0,frame,80,"
            "hidden,0,missed_preview,0",
            "#CAP,3200,DIAG,overlap_hold,note_offs,20,empty_sets,0,max_ids,2,overflows,0,"
            "looked_up,12,max_examined,2,sum_examined,12,max_lookup_us,1,sum_lookup_us,2,"
            "add,4,shorten,8,hide,6",
            "#CAP,3300,WRAP,2880,192,5,52",
            "#CAP,3301,WRAP,2880,192,5,48",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result["wrap_source"], "WRAP")
        self.assertEqual(result["source_shorten"], 0)
        self.assertEqual(result["source_hide"], 0)

    def test_fail_when_source_pass_shortens(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,overlap_hold,note_offs,16,empty_sets,0,max_ids,1,overflows,0,"
            "looked_up,4,max_examined,1,sum_examined,4,max_lookup_us,1,sum_lookup_us,1,"
            "add,12,shorten,4,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3100,DIAG,draw_src,layers,visual,80,preview,1,sealed,0,snap,0,frame,80,"
            "hidden,0,missed_preview,0",
            "#CAP,3200,DIAG,overlap_hold,note_offs,20,empty_sets,0,max_ids,2,overflows,0,"
            "looked_up,12,max_examined,2,sum_examined,12,max_lookup_us,1,sum_lookup_us,2,"
            "add,4,shorten,8,hide,6",
            "#CAP,3300,WRAP,2880,192,5,52",
            "#CAP,3301,WRAP,2880,192,5,48",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertFalse(result["ok"])
        self.assertTrue(any(issue.startswith("occupy_source_pass_shorten") for issue in result["issues"]))

    def test_pass_wrap_from_mi_u_when_wrap_dump_missing(self) -> None:
        us_per_tick = 1_000_000.0 / 384.0
        on_ts = 3_010_000 + int(round(80 * us_per_tick))
        off_ts = 3_010_000 + int(round(464 * us_per_tick))
        off2_ts = off_ts + 1000
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,overlap_hold,note_offs,16,empty_sets,0,max_ids,0,overflows,0,"
            "looked_up,0,max_examined,0,sum_examined,0,max_lookup_us,0,sum_lookup_us,0,"
            "add,16,shorten,0,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3100,DIAG,draw_src,layers,visual,80,preview,1,sealed,0,snap,0,frame,80,"
            "hidden,0,missed_preview,0",
            "#CAP,3200,DIAG,overlap_hold,note_offs,20,empty_sets,0,max_ids,2,overflows,0,"
            "looked_up,12,max_examined,2,sum_examined,12,max_lookup_us,1,sum_lookup_us,2,"
            "add,4,shorten,8,hide,6",
            f"#CAP,3010000,BAR,2800,0",
            f"#CAP,{on_ts},MI,U,144,5,52,100",
            f"#CAP,{off_ts},MI,U,128,5,52,0",
            f"#CAP,{on_ts},MI,U,144,5,48,100",
            f"#CAP,{off2_ts},MI,U,128,5,48,0",
            "#CAP,6000000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result["wrap_source"], "MI_U")
        self.assertGreaterEqual(len(result["wrap_pairs"]), 2)

    def test_fail_when_shorten_missing(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,overlap_hold,note_offs,16,empty_sets,0,max_ids,0,overflows,0,"
            "looked_up,0,max_examined,0,sum_examined,0,max_lookup_us,0,sum_lookup_us,0,"
            "add,16,shorten,0,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3100,DIAG,draw_src,layers,visual,80,preview,1,sealed,0,snap,0,frame,80,"
            "hidden,0,missed_preview,0",
            "#CAP,3200,DIAG,overlap_hold,note_offs,20,empty_sets,0,max_ids,1,overflows,0,"
            "looked_up,8,max_examined,1,sum_examined,8,max_lookup_us,1,sum_lookup_us,1,"
            "add,8,shorten,0,hide,8",
            "#CAP,3300,WRAP,2880,192,5,52",
            "#CAP,3301,WRAP,2880,192,5,48",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertFalse(result["ok"])
        self.assertTrue(any(issue.startswith("occupy_shorten_lt_2") for issue in result["issues"]))

    def test_fail_when_od2_uses_current_snapshot(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,overlap_hold,note_offs,16,empty_sets,0,max_ids,0,overflows,0,"
            "looked_up,0,max_examined,0,sum_examined,0,max_lookup_us,0,sum_lookup_us,0,"
            "add,16,shorten,0,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3100,DIAG,draw_src,layers,visual,40,preview,1,sealed,0,snap,1,frame,40,"
            "hidden,39,missed_preview,0",
            "#CAP,3200,DIAG,overlap_hold,note_offs,20,empty_sets,0,max_ids,2,overflows,0,"
            "looked_up,12,max_examined,2,sum_examined,12,max_lookup_us,1,sum_lookup_us,2,"
            "add,4,shorten,8,hide,6",
            "#CAP,3300,WRAP,2880,192,5,52",
            "#CAP,3301,WRAP,2880,192,5,48",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertFalse(result["ok"])
        self.assertTrue(any("occupy_od2_snap_not_zero" in issue for issue in result["issues"]))

    def test_pass_when_od2_paint_and_occupy_use_snapshot(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,overlap_hold,note_offs,16,empty_sets,0,max_ids,0,overflows,0,"
            "looked_up,0,max_examined,0,sum_examined,0,max_lookup_us,0,sum_lookup_us,0,"
            "add,16,shorten,0,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3001,DIAG,lcr,src,why=open,from=snapshot,win=0,proj=1,tot=1,ev=0,notes=19,"
            "bars=16,live=3072,prep=0",
            "#CAP,3002,DIAG,lcr,srcspan,at=view,id=7,p=60,s=10,e=128,g=linear",
            "#CAP,3100,DIAG,draw_src,layers,visual,80,preview,1,sealed,0,snap,1,frame,80,"
            "hidden,0,missed_preview,0",
            "#CAP,3200,DIAG,overlap_hold,note_offs,20,empty_sets,0,max_ids,2,overflows,0,"
            "looked_up,12,max_examined,2,sum_examined,12,max_lookup_us,1,sum_lookup_us,2,"
            "add,4,shorten,8,hide,6",
            "#CAP,3300,WRAP,2880,192,5,52",
            "#CAP,3301,WRAP,2880,192,5,48",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result["od2_open_from"], "snapshot")
        self.assertEqual(result["od2_srcspan"][0]["id"], 7)

    def test_pass_when_overlap_hold_uses_short_oh_keys(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,oh,n,16,e,0,i,0,o,0,k,0,x,0,s,0,u,0,t,0,add,16,shorten,0,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3001,DIAG,lcr,src,why=open,from=snapshot,win=0,proj=1,tot=1,ev=0,notes,19,"
            "bars=16,live=3072,prep=0",
            "#CAP,3100,DIAG,draw_src,layers,visual,80,preview,1,sealed,0,snap,1,frame,80,"
            "hidden,0,missed_preview,0",
            "#CAP,3200,DIAG,oh,n,20,e,0,i,2,o,0,k,12,x,2,s,12,u,1,t,2,add,4,shorten,8,hide,6",
            "#CAP,3300,WRAP,2880,192,5,52",
            "#CAP,3301,WRAP,2880,192,5,48",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result["max_shorten"], 8)
        self.assertEqual(result["max_hide"], 6)

    def test_fail_when_wrap_pairs_missing(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,1100,DIAG,overlap_hold,note_offs,16,empty_sets,0,max_ids,0,overflows,0,"
            "looked_up,0,max_examined,0,sum_examined,0,max_lookup_us,0,sum_lookup_us,0,"
            "add,16,shorten,0,hide,0",
            "#CAP,2000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,2100,VCACHE,adopt_partial,ev,-1,notes,80,first,0,last,3,total,4,dsz,4,dcnt,0,dirty,0",
            "#CAP,2200,DISP,0,PLAYING,3072,80,80,80,80,1",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3100,DIAG,draw_src,layers,visual,80,preview,1,sealed,0,snap,0,frame,80,"
            "hidden,0,missed_preview,0",
            "#CAP,3200,DIAG,overlap_hold,note_offs,20,empty_sets,0,max_ids,2,overflows,0,"
            "looked_up,12,max_examined,2,sum_examined,12,max_lookup_us,1,sum_lookup_us,2,"
            "add,4,shorten,8,hide,6",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "Overdub undone",
            "Overdub undone",
            "Overdub redone",
            "Overdub redone",
        ]
        result = verify_occupy_paint(lines, SimpleNamespace(undo_redo_cycles=2))
        self.assertFalse(result["ok"])
        self.assertTrue(any(issue.startswith("occupy_wrap_pairs_lt_2") for issue in result["issues"]))


if __name__ == "__main__":
    unittest.main()
