# Overdub occupy — same-start shorter hides longer

**Status:** Implemented — native **1533/1533**, RAM1 locals **6848**. Device recapture required.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41)  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Evidence:** [`002145`](../../captures/session_20260901_002145.log) `consume,norow` pitch 63 ids 75/76; frame `63@768` lengths 96 and 288

**Does not authorize:** Stage 2 snapshot-note copy; Overlay → false on `tryGather`; OpenSpec `3.5` / `3.6`; changing `classifyEditSessionInteraction`.

---

## Debugging boundary

```text
classifyEditSessionInteraction same-start longer target → OverlapNoteOn → Hide
    ← already correct
source-view reconstruct unique-key pitch+start (1b) drops longer noteId
    ← this slice
adopt copies unhidden liveDisplayNotes (96+288)
    ← this slice
```

---

## Problem

Incoming shorter overdub at the same On tick must Hide the longer earlier note (resolver already does). [`002145`](../../captures/session_20260901_002145.log) occupy lists ids 75/76 and then `norow` — `appendNotesForIds` cannot Hide a row that reconstruct dropped. Playback still sounds the long note. Adopted STOPPED/PLAYING frame still has 96 and 288 at pitch 63.

## Invariant

```
source-view reconstruct keeps distinct noteIds at the same pitch+start
incoming shorter [S, S+96) Hides earlier [S, S+288)
display adopt / idle cache keeps the shortest end at that pitch+start
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO |
| Reuse | YES — revert `dedupeProjectedDisplayNoteVec` unique-key to pitch+start+end; `keepShortestEndAtPitchStart` on adopt/idle only |

## Implementation

- Reconstruct unique-key is identical geometry only (pre-1b). Occupy / source view keep both lengths.
- `keepShortestEndAtPitchStart` runs on `liveDisplayNotes` at adopt and on `visualCache.notes` after idle splice.
- `test_pending_hide_same_start_shorter_hides_longer` — incoming 64–160 Hides source 64–288.
