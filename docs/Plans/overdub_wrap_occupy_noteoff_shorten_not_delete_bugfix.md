# Overdub wrap — NoteOff OverlapNoteOn must Shorten, not Delete

**Status:** RC1 + RC2 implemented. Native **1539/1539**. RAM1 locals **6848**. Device recapture required.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41)  
**Parent:** [`overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md`](overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md)  
**Evidence:** [`010926`](../../captures/session_20260901_010926.log)

**Does not authorize:** Stage 2 snapshot-note copy; Overlay → false on `tryGather`; OpenSpec `3.5` / `3.6`; changing `classifyEditSessionInteraction` / NOTE_EDIT Shorten.

---

## Debugging boundary

```text
occupy On upsert Hide into pendingPaintHides_
    ← previous slice
sealPendingNoteChangesToEditPasses skips paint Hide
    ← previous slice
NoteOff accumulate OverlapNoteOn → Hide → wrap seals Delete
    ← this slice: Shorten to occupy exclusive end − 1
CompleteCover still Hide / Delete
    ← unchanged
norowid paint + visual-cache paint overlay
    ← RC2: hold-participant fallback + cache paint
```

## Problem

[`010926`](../../captures/session_20260901_010926.log): occupy On paint Hide is not sealed. NoteOff still runs `resolveConstrainedGeometry`, which treats OverlapNoteOn as `visible=false`. That upserts durable Hide. Each wrap `sealPendingNoteChangesToEditPasses` writes Delete companions and removes earlier recorded overdub notes.

## Invariant

```
overdub NoteOff OverlapNoteOn seals Length (Shorten to occupy exclusive end − 1)
CompleteCover still seals Delete
occupy On paint Hide stays in pendingPaintHides_
NOTE_EDIT OverlapNoteOn remains Hide
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO |
| Reuse | YES — `accumulatePendingNoteChangesFromSourceNotes` |
