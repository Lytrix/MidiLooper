#!/usr/bin/env python3
"""Tests for the stored-record grid (`REVT`) window: the take, not the flush clock.

`REVT` rows are emitted when their queued record leaves the capture ring
(`flushOneRevtRecord` stamps `micros()` at flush), so their `#CAP,<us>` is a **flush** clock and they
legitimately land after `RECS,stop` — which the function's docstring has said since `373566a7`.
`21b8ca8f` bounded the take by that stamp (`ts > recs_stop_ts -> skip`) and so discarded exactly the
deferred rows: every run after it reports `record_stored_revt_missing`, while the same rows counted as
32 with `grid_ok: true` before it. Replays of the stored runs pin both sides:

| run | rows delivered | before `21b8ca8f` | after | with the span rule |
|---|---|---|---|---|
| `s4f22-step4-a1`, `s4f22-step5-b1`, `s4f22-step8-e3` | 32 (at 72.5 s / 71.8 s / 180.3 s vs stops 43.6 s / 42.9 s / 151.5 s) | 32, grid ok | 0, missing | 32, grid ok |
| `s4f22-step11-h5`, `s4f22-step11-h6` | 32 | -- | 0, missing | 32, grid ok |
| `s4f22-step8-e2` | 4 (the *stray* take's) | 4, grid ok | 4, grid ok | 0, missing |
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl import legacy_record_baseline as baseline  # noqa: E402


def _grid_lines_with_deferred_revt() -> list[str]:
    """The `s4f22-step11-h5` shape: the take's `REVT` rows arrive 36 s after its stop."""
    lines = [
        "#CAP,32013135,RECA,0,0",
        "#CAP,36649217,RECS,stage,record_stop,9,0,4294967295,4294967295,96,0,en",
        "#CAP,36649334,RECS,stop,0,365,0,1901,1536,0",
        "#CAP,36649659,RECS,stage,finalize,454,453,4294967295,4294967295,0,0,de",
        # ... other phases, and then the deferred flush of the take's stored note-ons
        "#CAP,72967420,REVT,16,5,77",
        "#CAP,72967430,REVT,64,5,78",
        "#CAP,72967439,REVT,111,5,79",
        "#CAP,72967448,REVT,158,5,77",
    ]
    return lines


class DeferredRevtTests(unittest.TestCase):
    def test_rows_flushed_after_the_stop_are_still_the_take_s(self) -> None:
        ticks = baseline._extract_revt_note_on_ticks(_grid_lines_with_deferred_revt())
        self.assertEqual([16, 64, 111, 158], ticks)

    def test_the_grid_verifies_on_a_complete_deferred_set(self) -> None:
        grid = baseline._verify_stored_record_note_grid(_grid_lines_with_deferred_revt())
        self.assertFalse(grid["revt_missing"])
        self.assertEqual(4, grid["note_on_count"])

    def test_the_flush_stamp_is_not_the_phase_clock(self) -> None:
        """The regression `21b8ca8f` introduced: bounding by `ts > RECS,stop` empties the set."""
        lines = _grid_lines_with_deferred_revt()
        recs_stop_us = 36649334
        would_be_kept_by_flush_stamp = [
            line for line in lines
            if ",REVT," in line and int(line.split(",")[1]) <= recs_stop_us
        ]
        self.assertEqual([], would_be_kept_by_flush_stamp)          # the old bound
        self.assertEqual(4, len(baseline._extract_revt_note_on_ticks(lines)))  # the span rule


class TakeSpanTests(unittest.TestCase):
    def test_a_stray_successor_take_closes_the_span(self) -> None:
        """`s4f22-step8-e2`: the scored take stopped, a stray one-bar take followed."""
        lines = [
            "#CAP,70000000,RECA,0,383",                       # scored take
            "#CAP,70203837,RECS,stage,record_stop,9,0,1,1,96,0,en",
            "#CAP,70203914,RECS,stopToStopped,0,365,0,1901,1536,0",
            "#CAP,71600000,RECA,0,0",                         # stray successor
            "#CAP,118128347,REVT,28,5,60",                    # its stored rows
            "#CAP,118128380,REVT,75,5,61",
        ]
        self.assertEqual([], baseline._extract_revt_note_on_ticks(lines))

    def test_the_span_continues_to_the_end_when_the_take_has_no_successor(self) -> None:
        lines = [
            "#CAP,70000000,RECA,0,383",
            "#CAP,70203914,RECS,stopToStopped,0,365,0,1901,1536,0",
            "#CAP,118128347,REVT,28,5,60",
        ]
        self.assertEqual([28], baseline._extract_revt_note_on_ticks(lines))

    def test_a_stats_row_is_not_a_take_identity(self) -> None:
        lines = [
            "#CAP,95874518,RING,record_stats,RECA,entered=1,enq=1,rej=0,tx=1,evict=0",
            "#CAP,36000000,RECA,0,383",
            "#CAP,36649334,RECS,stop,0,365,0,1901,1536,0",
            "#CAP,72967420,REVT,16,5,77",
        ]
        # The real take is the second row; the stats row must not be mistaken for one.
        self.assertEqual([16], baseline._extract_revt_note_on_ticks(lines))

    def test_no_take_start_means_no_evidence(self) -> None:
        self.assertEqual([], baseline._extract_revt_note_on_ticks(["#CAP,1,SEVT,N,1,5,24"]))

    def test_a_reca_before_any_stop_does_not_close_the_span(self) -> None:
        # The same take re-reported (the firmware does that), not a successor.
        lines = [
            "#CAP,70000000,RECA,0,383",
            "#CAP,70100000,RECA,0,383",
            "#CAP,70203914,RECS,stop,0,365,0,1901,1536,0",
            "#CAP,72967420,REVT,16,5,77",
        ]
        self.assertEqual([16], baseline._extract_revt_note_on_ticks(lines))
        self.assertIsNone(
            baseline._next_record_take_row_index(lines, after_index=0)
        )


if __name__ == "__main__":
    unittest.main()
