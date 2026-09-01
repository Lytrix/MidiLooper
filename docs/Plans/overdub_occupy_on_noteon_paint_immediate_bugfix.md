# Overdub occupy — paint Hide and capture on NoteOn (not wrap)

**Status:** Implemented — native **1543/1543**. RAM1 locals **6848**. Device recapture required.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41)  
**Parent:** [`overdub_occupy_hide_on_hold_current_tick_bugfix.md`](overdub_occupy_hide_on_hold_current_tick_bugfix.md)  
**Evidence:** [`104129`](../../captures/session_20260901_104129.log), [`110022`](../../captures/session_20260901_110022.log), [`111343`](../../captures/session_20260901_111343.log)

---

## Problem

[`104129`](../../captures/session_20260901_104129.log): second-loop overdub notes and occupy Hides appear only at wrap. Log shows `lcr,part` `n=0` while source-view `a=1` for pitch 60 (ledger empty at hold). `overlap_hold` reports `empty_sets=4`. After wrap, `DISP` frame count freezes while `snapshot_idle` runs; open capture notes stop mirroring until the next wrap visual-cache adopt.

[`110022`](../../captures/session_20260901_110022.log): regression when a **longer** note overlaps a **shorter** incoming note — display reverts to the shorter committed row and stale note data redraws after every overdub wrap.

[`111343`](../../captures/session_20260901_111343.log): after 3rd/4th same-pitch overlap, STOPPED frame dump includes `DNTE,0,0,0,0` while `SEVT` / `MI,U` stay in 60 and 69–84. Reconstruct dump has no MIDI 0 row. Stale compose counts after hide/dedupe then `resize` grew `liveDisplayNotes` with a default-constructed placeholder.

## Invariant

```
occupy NoteOn upserts paint Hide from ledger ids or present-at-hold geometry when ledger is empty
overlapNoteIds for NoteOff accumulate includes geometric participants when ledger is empty
over-budget overdub display resolve still synchronizes capturePreview and playhead tails
wrap adopt applies paint Hide only to the committed prefix (not the live capture suffix)
keepShortestEndAtPitchStart runs on committed prefix only during wrap adopt
visual-cache idle slice applies pending paint before same-start dedupe
compose committed/capture/base counts clamp to liveDisplayNotes.size() after hide/dedupe
liveDisplayNotes.resize never grows (no MIDI 0 / tick 0 placeholder rows)
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO |
| Reuse | YES — `upsertPendingHidesForOccupiedNoteIds`, `snapshotOverlapHoldCandidates`, `resolveDisplayNotesLiveCapture`, `adoptComposedDisplayNotesIfAuthoritative` |

## Implementation

**104129**
- `upsertPendingHidesForOccupiedNoteIds`: geometric fallback when `selected` is empty, not only norowid.
- `snapshotOverlapHoldCandidates`: merge geometric hold participants when ledger occupy set is empty.
- `resolveDisplayNotesLiveCapture`: on over-budget reuse, still `synchronizeCaptureLayer` when capture changed; apply playhead tails for overdub capture changes.

**110022**
- `liveDisplayPaintCommittedEnd_`: saved before wrap hold bumps committed count to full composed frame.
- `adoptComposedDisplayNotesIfAuthoritative`: paint boundary + `keepShortestEndAtPitchStartOnCommittedPrefix`.
- `LoopVisualCache` idle slice: pending paint before `keepShortestEndAtPitchStart`.
- `reuseLastValidFrame` + `captureLayerChanged`: overdub still runs `synchronizeCaptureLayer`.

**111343**
- `syncLiveDisplayComposeCounts` / `clampDisplayNoteResizeCount` after adopt hide/dedupe.
- Hold and capture-replace resize never grow `liveDisplayNotes`.
- Over-budget committed rebuild still `synchronizeCaptureLayer` while overdubbing.
