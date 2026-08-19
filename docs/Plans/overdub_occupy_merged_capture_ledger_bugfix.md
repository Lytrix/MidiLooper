# Occupy merged-capture ledger last-writer

**Status:** FROZEN — leftover `n=1 a=0` **met** on [`231038`](../../captures/session_20260818_231038.log). Remaining `n=0 a=1` is USB occupy before the clock interval. Successor: [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](overdub_occupy_on_tick_clock_catchup_bugfix.md).  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent (frozen):** [`overdub_occupy_capture_stream_ledger_bugfix.md`](overdub_occupy_capture_stream_ledger_bugfix.md)  
**Pin:** [`224719`](../../captures/session_20260818_224719.log)  
**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; USB occupy `playMidiEvents` catch-up (reverted [`214856`](../../captures/session_20260818_214856.log)); occupy fallback; DisplayManager consume; folding capture into `mergedMidiEvents`; `n=1 a=2`

**Misattribution guard:** A later `n=0 a=1` after this freeze is not a regression of committed-only `mergedMidiEvents`. Investigate USB occupy vs clock `(prevTickInLoop, tickInLoop]` ([`overdub_occupy_on_tick_clock_catchup_bugfix.md`](overdub_occupy_on_tick_clock_catchup_bugfix.md)). A later `n=1 a=0` with capture folded into `mergedMidiEvents` is this gather RC.

---

## Invariant (one sentence)

Playback `runtime.mergedMidiEvents` is **committed-only**. Occupy reads `ActiveNoteLedger` Entries written from that representation, wrap-pass, and loop-head. Live capture must not appear in `mergedMidiEvents` and must not `applyPlaybackLedgerEvent`.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `playCommittedLoopMidi` → `applyPlaybackLedgerEvent` on committed playback only: `mergedMidiEvents` (committed-pass merge, never live capture), wrap-pass, and loop-head. Capture already owns MIDI echo via `sendMidiEvent` and must not last-write that ledger. |
| **State transition change?** | NO — no wrap/commit on USB NoteOn. Occupy stays `snapshotOverlapHoldCandidates` only. |
| **Reuse** | YES — `gatherCommittedEventsForDerivedView` / `gatherCommittedEventsInWindow` in [`TrackPlaybackWindowBuild.cpp`](../../src/Track/TrackPlaybackWindowBuild.cpp) `ensurePlaybackMergedMidiEventsBuilt`. Keep `playbackCursorAdvanceSendCapture`. No occupy fallback. No DisplayManager patch. |

## Debugging boundary

```
… → playbackCursorAdvanceSendCapture ← trust emit-only capture walk
 → ensurePlaybackMergedMidiEventsBuilt gather ← closed (committed-only)
```

Do not reopen the capture-stream emit-only walk. Do not fold live capture into `mergedMidiEvents` again. Remaining `n=0 a=1` on [`231038`](../../captures/session_20260818_231038.log) is the successor ledger catch-up.

## Root cause

Occupy reads `ActiveNoteLedger` at USB NoteOn. Source-view `a=` is committed geometry at that hold. [`224719`](../../captures/session_20260818_224719.log) still disagrees after `playbackCursorAdvanceSendCapture` because `ensurePlaybackMergedMidiEventsBuilt` called `gatherCommittedEventsWithCapture` (1-bar) / `gatherCommittedEventsInWindowWithCapture` (long loop). That folds `capture.store` into `mergedMidiEvents`. `playCommittedLoopMidi` walks that vector with `playbackCursorAdvanceSend` (`applyPlaybackLedgerEvent` then `sendMidiEvent`).

Capture Off of pitch P in `mergedMidiEvents` **clears** the committed Entry (`n=0 a=1`). Capture On of P **occupies** when source-view has no P (`n=1 a=0`).

The parent native tests applied a disconnected capture event to a second ledger copy; they never gathered into `mergedMidiEvents`. Device 1-bar overdub did.

| Kind | Count | Gate |
|------|------:|------|
| `n=1 a=1` | 62 | match |
| `n=0 a=0` | 28 | empty lane |
| **`n=0 a=1`** | **8** | **FAIL** (want 0) |
| **`n=1 a=0`** | **6** | **FAIL** (want 0) |
| `n=1 a=2` | 5 | not this gate |

Closed pins this take: occupy 12 @ `hs=0` `n=1 a=1`. Occupy 12 @ `hs=240` `n=1 a=1`. `hs=` on 109/109 occupies.

## Fix

`ensurePlaybackMergedMidiEventsBuilt` (not NOTE_EDIT preview):

- Short loop: always `gatherCommittedEventsForDerivedView`
- Long loop: always `gatherCommittedEventsInWindow`

Live overdub echo stays on `ActiveCaptureOverdub` + `playbackCursorAdvanceSendCapture`. Display/LED `gatherCommittedEventsWithCapture` stays paint.

Do **not**: call `playMidiEvents` from occupy; teach occupy about wrap/source-view; add a second ledger; change display gather; reopen wrap-S / loop-head / USB catch-up.

## Tests

- `test_capture_off_does_not_clear_committed_occupy` — committed On@384 Off@480 pitch 23; capture Off@400 in store; `gatherCommittedEventsWithCapture` applied through 420 → occupy `n=0`; `gatherCommittedEventsForDerivedView` → `n=1`.
- `test_capture_on_does_not_occupy_empty_source_view` — source-view empty at 45; capture On in store; WithCapture gather occupies (`n=1 a=0`); derived-view gather does not.
- `pio test -e native` 1352/1352.

## HITL

**FAIL** [`231038`](../../captures/session_20260818_231038.log): 1-bar 768 OVERDUBBING; `hs=` on 86/86 occupies. `RING,overflow` twice. Firmware includes this gather (leftover gate is 0).

| Kind | Count | Gate |
|------|------:|------|
| `n=1 a=1` | 30 | match |
| `n=0 a=0` | 49 | empty lane |
| **`n=0 a=1`** | **6** | **FAIL** (want 0) |
| **`n=1 a=0`** | **0** | **met** |
| `n=1 a=2` | 1 | not this gate |

This RC’s leftover (`n=1 a=0`) is gone. Remaining `n=0 a=1` are all pitch **12**, `b=1`:

| Line | hs | as–ae | vs span start |
|------|---:|---------|----------------|
| 4200 | 240 | 240–336 | occupy at start |
| 4273 | 432 | 432–528 | occupy at start |
| 4390 | 240 | 240–336 | occupy at start |
| 4721 | 384 | 336–432 | interior |
| 5241 | 192 | 192–280 | occupy at start |
| 5399 | 96 | 96–192 | occupy at start |

L5241: occupy us `65168812`; playback `MO,144,4,12,100` is later at us `65234756`. USB NoteOn at the committed On tick, ledger still empty. That leftover is [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](overdub_occupy_on_tick_clock_catchup_bugfix.md) — ledger catch-up, not `playMidiEvents` from occupy.

Closed pins this run: occupy 12 @ `hs=0` `n=1 a=1` (`6561`, `6785`, `6969`). Do not reopen wrap-S `(prev, S]` or USB `playMidiEvents` catch-up. Do not treat `n=1 a=2` as this FAIL. Do not fold capture into `mergedMidiEvents` again.
