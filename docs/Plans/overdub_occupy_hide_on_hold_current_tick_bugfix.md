# Overdub occupy — hide on hold at currentTick

**Status:** Unmasked wrap-seal RC — see [`overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md`](overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md). Paint Hide on hold stays. Durable OverlapNoteOff is Shorten again.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41)  
**Parent:** [`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](overdub_occupy_same_start_shorter_hides_longer_bugfix.md)  
**Evidence:** [`003616`](../../captures/session_20260901_003616.log)

**Does not authorize:** Stage 2 snapshot-note copy; Overlay → false on `tryGather`; OpenSpec `3.5` / `3.6`; changing `classifyEditSessionInteraction` / NOTE_EDIT Shorten.

---

## Debugging boundary

```text
classify OverlapNoteOn / CompleteCover → Hide
    ← already correct
OverlapNoteOff → Shorten on overdub pending
    ← this slice: Hide entire earlier note
accumulate only on NoteOff; wrap seal paints Hide
    ← this slice: upsert Hide at occupy On / playback cross
applyPending Hide matches noteId only
    ← this slice: same-pitch overlapping span (`003616` id 73 ≠ cache row)
```

---

## Problem

[`003616`](../../captures/session_20260901_003616.log): incoming pitch 60 occupies earlier 60s (`lcr,part` `n≥1` after wrap 1). Consume waits for NoteOff. `DISP` cache stays 67 through the next loop; Hide is visible after wrap `adopt_partial` / stop seal (`overlap_hold` `hide=2`). STOPPED `DNTE` still has `60@0` lengths 48 and 192. `consume,norowid` 73/77/81 — occupy ids missing from source-view rows, so noteId overlay cannot Hide the painted cache row.

Required: when an incoming NoteOn is held and starts on or crosses an earlier recorded NoteOn, Hide the entire earlier note (any length). Paint that Hide on currentTick frames, not only at wrap seal.

## Invariant

```
occupy On / playback cross upserts Hide for occupied source-view noteIds
overdub pending maps any inclusive overlap to Hide (baseline start/end)
paint overlay drops a row by noteId or same-pitch overlapping span
NOTE_EDIT OverlapNoteOff remains Shorten
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — Hide upsert uses `appendNotesForIds` on the already-capped occupy set |
| Reuse | YES — `Loop::upsertPendingHidesForOccupiedNoteIds` + `applyPendingNoteChangesToDisplayNotes` |

## Implementation

- `accumulatePendingNoteChangesFromSourceNotes` emits Hide (not Shorten) for any inclusive overlap.
- `Track::snapshotOverlapHoldCandidates` and `Track::collectOverlapHoldPlaybackNoteOn` upsert those Hides when the hold starts or crosses.
- Paint `applyPendingNoteChangesToDisplayNotes` Hides a committed row when `noteId` matches or the same pitch overlaps the Hide baseline. The live-capture suffix (`liveCaptureBegin`) is not Hidden so the incoming hold still grows to currentTick. Source-view apply stays `noteId` only.
