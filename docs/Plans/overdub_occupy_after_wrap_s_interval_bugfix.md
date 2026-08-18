# Occupy after wrap S — `(S, occupyTick]` ledger catch-up

**Status:** Native **PASS** 1350/1350. Device gate: 203948-shaped occupy 32 ticks after S=64.  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_occupy_off_tick_display_investigation.md`](overdub_occupy_off_tick_display_investigation.md)  
**Pin:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ **96** `n=0 a=1 b=1` (`204277855`)  
**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; occupy fallback; DisplayManager consume

---

## Invariant (one sentence)

Before occupy at tick T after wrap S, `ActiveNoteLedger` contains every committed playback event that crosses `(S, T]` — wrap-S `(prev, S]` is not that interval.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `playCommittedLoopMidi` → `applyPlaybackLedgerEvent`. |
| **State transition change?** | NO — wrap still commits at S. Occupy still snapshots at NoteOn. Catch-up runs the existing playback walk when `lastTickInLoop < occupyPhase`. |
| **Reuse** | YES — `Track::snapshotOverlapHoldCandidates` calls `playMidiEvents` when the playback cursor is behind occupy phase. Same `advancePlaybackCursor` `(prev, current]`. |

---

## Fix

`Track::snapshotOverlapHoldCandidates`: if overdubbing and `lastTickInLoop < occupyPhase`, call `playMidiEvents` so `(lastTick, occupyPhase]` writes the ledger before `collectOverdubNoteOnParticipantIds`.

Occupy CAP adds `as=` / `ae=` (first source-view span present at hold).

Do **not** hide in DisplayManager. Do not teach occupy about wrap or source-view.

---

## Tests

- `test_wrap_pass_on_at_96_occupies_after_s_interval` — wrap-S miss `n=0`; `(64, 96]` occupy `n=1`; source-view `a=1` at 96.
- `test_wrap_pass_spanning_on_at_0_occupies_at_96` — On@0 Off@200, loop-head then wrap at 64, occupy at 96 `n=1`.
- `pio test -e native` 1350/1350

## HITL

1-bar overdub, session start storage 64. After wrap, occupy the wrap-committed pitch at 96. Expect `from=ledger,n=1,a=1,b=1` and `as=` matching the source-view start. Opposite of [`203948`](../../captures/session_20260818_203948.log) `204277855` `n=0`.
