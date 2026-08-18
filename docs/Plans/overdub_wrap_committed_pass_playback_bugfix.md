# Wrap-tick ledger catch-up from committed pass

**Status:** Native **PASS** 1346/1346. Firmware `teensy41-capture-serial` builds (RAM1 local 4768). HITL after flash.  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_present_at_tick_jit_enhancement.md`](overdub_present_at_tick_jit_enhancement.md)  
**Supersedes (this RC):** [`overdub_wrap_playback_rebuild_before_occupy_bugfix.md`](overdub_wrap_playback_rebuild_before_occupy_bugfix.md) — wrong prerequisite (full-loop rebuild vs ledger catch-up)  
**Evidence:** [`180844`](../../captures/session_20260818_180844.log) wrap 15 `n=0 a=1 b=1` pitch 60 @ 416 (`310503348`)

**Does not start:** occupy fallback; wrap logic in `collectOverdubNoteOnParticipantIds`; `length` on `Entry`; consume merge; wait-STOPPED-for-`lcr,mat`; shrinking ledger/gather

---

## Invariant (one sentence)

Before occupy at wrap tick S, `ActiveNoteLedger` contains every committed playback event that crosses into S.

Not: before occupy, the merged playback stream must be rebuilt.

This is **wrap-tick playback ledger catch-up**, not a special occupy path.

```text
normal tick:
    merged playback events → ledger

wrap tick:
    old merged events
    +
    newly sealed pass
        → ledger
        → occupy reads Entry.noteId
```

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Wrap sealer stays `commitOverdubWrapAtSessionStart`. |
| **State transition change?** | YES — wrap tick applies old-stream `(prev, S]` then the sealed pass `(prev, S]` before occupy can observe S. |

Reuse: YES — `playbackCursorAdvanceSend` (`applyPlaybackLedgerEvent` then `sendMidiEvent`). `LoopEventStore::appendChunkRefEvents` on `lastCommittedPassId()` chunks. File-local `FLASHMEM` / `noinline` helpers in [`TrackPlaybackHotPath.cpp`](../../src/Track/TrackPlaybackHotPath.cpp) so wrap catch-up does not consume ITCM. No new Track API.

---

## What the log determines

[`session_20260818_180844.log`](../../captures/session_20260818_180844.log) wrap 15:

- wrap `310486672` `why=wrap`
- occupy 60 @ storage **416** `n=0 a=1 b=1` at `310503348`
- `playback_build,14552,0,768,153` CAP after occupy

`playback_build` CAP is **deferred** (`recordPlaybackRebuild` → `maybeEmit` at end of `loop()`). Occupy-before-build in the log is emit order, not gather-end. Duration 14552 µs is real. The ledger was not updated from the newly committed wrap pass before occupy.

Do not reopen “wait for full playback build.” Parent RC `ddcf946` rebuilt merged+order and reanchored at prev; HITL wrap 15 still `n=0`.

---

## Fix

In `Track::playCommittedLoopMidi`:

1. Advance the old merged stream through `(prev, S]` using existing playback cursor semantics, **before** sealing the new pass.
2. `maybeCommitOverdubWrap` (sealer unchanged). Peek wrap with `armOverdubWrapAfterLeavingStart` + `shouldCommitOverdubWrap` only — do not call `consumeSuppressedOverdubWrapCrossing` twice.
3. If `CommitResult::Committed`: apply `lastCommittedPassId()` chunks for `(prev, S]` via `playbackCursorAdvanceSend` (O(pass)).
4. Heavy `ensurePlaybackMergedMidiEventsBuilt` (rest of bar / later ticks).
5. `lastTickInLoop = S`; `rebuildPlaybackOrder` + `reanchorPlaybackIndex` at S so the rebuilt stream does not replay `(prev, S]`.
6. Capture stream still advances `(prev, S]`.

**After wrap-pass application, the playback cursor must be reanchored at S before the rebuilt merged stream is subsequently used, so `(prev, S]` is applied exactly once.**

**At a wrap tick, `(prev, S]` is advanced exactly once through the old merged stream when there is no newly committed pass; when a pass is committed, the old stream advances first and only the newly committed pass supplies the additional post-commit events.**

---

## Tests

- Keep `test_wrap_committed_note_at_s_occupies_ledger_same_tick` (152745: 71 @ 656).
- `test_wrap_pass_events_at_s_occupy_without_merged_rebuild` (180844: 60 @ 416): `commitCapturePass(OverdubWrap)` → `appendChunkRefEvents(lastCommittedPassId chunks)` → `advancePlaybackCursor` `(prev, S]` → occupy. **Does not** call `ensurePlaybackMergedMidiEventsBuilt` or `gatherCommittedEvents`.
- `pio test -e native` before commit.

---

## HITL

After flash: 1-bar overdub, note that starts at S, wrap, play that pitch again at S. Expect `from=ledger,n=1,a=1,b=1` not [`310503348`](../../captures/session_20260818_180844.log) `n=0 a=1 b=1`.
