# NOTE_EDIT close snapshot NoteRange pitch

**Status:** device PASS
[`163849`](../../captures/session_20260908_163849.log)
**OpenSpec:** `immutable-loop-snapshot-passes` close-publish pitch reuse
**Evidence:** [`session_20260908_162207.log`](../../captures/session_20260908_162207.log)
NoteRange pre-commit `pitch=0`; persist apply does not use that field.
Device: NoteRange `86` `768→1584–1871` paints `DNTE,63,1584,1584,287` after close.
No `DNTE` at pitch 0. Pitch `89` `63→79` paints `DNTE,79,1536`. Reopen spans/visual **95**
(open **97** − 2 Deletes).
**Does not start:** 4.4e Stage 2/3, hydrate, persist apply.

## Debugging boundary

```
commitAllPendingNoteEditActions
  NoteRange row (start/end only; pitch default 0)
  applyMoveNoteById                    ← trust (ticks only)
        ↓
closeNoteEditPass
  tryPublishNoteEditSnapshot
    appendEditRowChange                ← this RC
```

`missing in recon` is the parked `commitBaseline` home lookup. It is not this RC.

## Invariant

A `NoteRange` snapshot Replace reuses the current note pitch (parent, or an
earlier same-`NoteId` change in the close set). It does not write `row.pitch`.

## Owner

`appendEditRowChange` in [`LoopSnapshotPublish.cpp`](../../src/Loop/LoopSnapshotPublish.cpp).
`closeNoteEditPass` still calls `tryPublishNoteEditSnapshot`.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO |
