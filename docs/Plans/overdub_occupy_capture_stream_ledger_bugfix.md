# Occupy capture-stream ledger last-writer

**Status:** Native **PASS** 1352/1352. HITL open.  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_present_at_tick_jit_enhancement.md`](overdub_present_at_tick_jit_enhancement.md)  
**Pin:** [`221334`](../../captures/session_20260818_221334.log)  
**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; USB occupy `playMidiEvents` catch-up (reverted [`214856`](../../captures/session_20260818_214856.log)); occupy fallback; DisplayManager consume

---

## Invariant (one sentence)

Occupy reads `ActiveNoteLedger` Entries written from **committed** playback only. Live capture echo must not `applyPlaybackLedgerEvent`.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `playCommittedLoopMidi` → `applyPlaybackLedgerEvent` on merged / wrap-pass / loop-head. Capture already owns MIDI echo via `sendMidiEvent`. |
| **State transition change?** | NO — no wrap/commit on USB NoteOn. Occupy stays `snapshotOverlapHoldCandidates` only. |
| **Reuse** | YES — `playbackCursorAdvanceSendCapture` next to `playbackCursorAdvanceSend` in [`TrackPlaybackHotPath.cpp`](../../src/Track/TrackPlaybackHotPath.cpp). No occupy fallback. No DisplayManager patch. |

## Root cause

Occupy reads `ActiveNoteLedger` at USB NoteOn. Source-view `a=` is committed geometry at that hold. Those disagree in [`221334`](../../captures/session_20260818_221334.log) because `playCommittedLoopMidi` applied the live capture stream through `playbackCursorAdvanceSend` (ledger then emit).

Capture Off of pitch P **clears** the committed `Entry` for P (`n=0 a=1`). Capture On of P **leaves** an `Entry` when source-view has no P (`n=1 a=0`).

151 occupy lines. Closed gates hold (wrap-S every 768 at storage 240; loop-head @ 0 `n=1 a=1`; 12 `as=96–192` `n=1 a=1`).

| Kind | Count | Meaning |
|------|------:|---------|
| `n=1 a=1` | 97 | match |
| `n=0 a=0` | 38 | empty lane |
| **`n=0 a=1`** | **10** | ledger empty, committed span present |
| **`n=1 a=0`** | **5** | ledger occupied, no source-view span (`as=0 ae=0`) |

This is **not** wrap-S `(prev, S]`, not loop-head Q16, not On@96 after S.

## Fix

In `playCommittedLoopMidi`, the `ActiveCaptureOverdub` walk emits with `playbackCursorAdvanceSendCapture`:

- `sendMidiEvent` (live echo and `collectOverlapHoldPlaybackNoteOn`)
- no `applyPlaybackLedgerEvent`

Merged / wrap-pass / loop-head walks keep `playbackCursorAdvanceSend` (ledger then emit).

Orphan capture Off still emits (the matching capture On was already sent). Duplicate Off on a later committed Off is harmless.

Do **not**: teach `collectOverdubNoteOnParticipantIds` about wrap or source-view; apply pending Hide at NoteOn; two ledgers; `playMidiEvents` from occupy.

Occupy CAP adds `hs=` (phased `pending.startNoteTick`). Keep `as=` / `ae=`.

## Tests

- `test_capture_off_does_not_clear_committed_occupy` — committed On@384 Off@480 pitch 23; capture Off@400; occupy at 420 `n=1`. Applying capture Off would be `n=0`.
- `test_capture_on_does_not_occupy_empty_source_view` — source-view empty at hold 45; capture On must not occupy. Applying capture On would be `n=1 a=0`.
- `pio test -e native` 1352/1352.

## HITL

After flash: 1-bar overdub like 221334; occupy `n=0 a=1` = 0 and `n=1 a=0` = 0. Closed pins must still pass (wrap at 240; occupy 12 @ 0 and @ 96 `n=1 a=1`).
