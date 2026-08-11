# Overview strip stuck on first ~18 bars

**Status:** Implemented — `drawPianoRoll` uses `visualCache` for minimap density  
**Evidence:** [`session_20260811_110408.log`](../../captures/session_20260811_110408.log), user report after RC4d / LEN fix

## Problem

On long loops (>16 bars), the minimap below the piano roll showed note density only in the
first ~18 bars while the detailed roll followed correctly. The window box could move but the
note dots did not span the full loop.

## Root cause

`drawPianoRoll` passed the same `notes` vector to `drawOverviewStrip` as resolved for the
detailed window. For long committed loops, `resolveDisplayNotesCommitted` →
`resolveWindowedDisplayNotes` returns either:

- window-filtered `visualCache` notes (16 bars), or
- gather+reconstruct over **16 + 2 margin bars** (~18 bars).

The overview strip maps notes across **full loop length** — it must use full-loop density, not
the bounded paint/gather set.

DISP in `110408` for 133-bar loop: `visualCache` size 497, `frameNotes` 447 window subset,
`wStart` advancing — detailed path OK, minimap data source wrong.

## Fix

In `drawPianoRoll`, when `useBoundedWindow` and `loop.visualCache.notes` is non-empty, pass
`visualCache.notes` to `drawOverviewStrip` (window box still uses `windowStart` / `windowLength`).

## Load-state blank piano (same capture)

User cycled tracks during boot/deferred `LoadLoopJob`. Expected behavior, not minimap RC:

1. `resolveDisplayNotesCommitted` clears `liveDisplayNotes` on track/slot context change.
2. Focus slot may still be an empty shell until `LoadLoopJob` commit (`applySnapshotToLoop`).
3. Rapid track switch → brief or sustained empty roll until that slot's job completes.

Example: `DISP,0,STOPPED,0` after switch to empty track 8; `DISP,4,STOPPED,102144,497` after
focus returns to loaded 133-bar slot.

Improvement (out of scope here): stale-while-revalidate or load placeholder for in-flight slot.

## Acceptance

- [x] Overview minimap uses full `visualCache` when available on long loops.
- [x] `pio test -e native`, `pio run -e teensy41-capture-serial`.
- [ ] Manual: 133-bar loop — minimap dots span full width; window box follows detailed roll.
