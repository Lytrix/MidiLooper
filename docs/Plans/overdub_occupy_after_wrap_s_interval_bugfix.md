# Occupy after wrap S — `(S, occupyTick]` ledger catch-up

**Status:** Device **FAIL** [`214856`](../../captures/session_20260818_214856.log). Production catch-up **reverted**. `as=`/`ae=` stay. Native `(S, occupy]` tests stay as cursor math only.  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_occupy_off_tick_display_investigation.md`](overdub_occupy_off_tick_display_investigation.md)  
**Pin:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ **96** `n=0 a=1 b=1` (`204277855`)  
**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; occupy fallback; DisplayManager consume

---

## Invariant (one sentence)

Before occupy at tick T after wrap S, `ActiveNoteLedger` contains every committed playback event that crosses `(S, T]` — wrap-S `(prev, S]` is not that interval.

---

## Architecture checkpoint (revert)

The production catch-up failed this checkpoint. `playMidiEvents` from occupy can run `maybeCommitOverdubWrap` when `(lastTick, occupyPhase]` crosses session start.

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `playCommittedLoopMidi` → `applyPlaybackLedgerEvent`. |
| **State transition change?** | YES if occupy calls `playMidiEvents` — wrap can commit on USB NoteOn. **Reverted.** Occupy snapshots only. |
| **Reuse** | YES — occupy CAP `as=`/`ae=` stays on `snapshotOverlapHoldCandidates`. No playback call. |

---

## Fix (reverted production walk)

Do **not** call `playMidiEvents` from `snapshotOverlapHoldCandidates`.

Occupy CAP keeps `as=` / `ae=` (first source-view span present at hold). Native `(S, occupy]` tests remain cursor math only.

Do **not** hide in DisplayManager. Do not teach occupy about wrap or source-view.

---

## Tests

- `test_wrap_pass_on_at_96_occupies_after_s_interval` — wrap-S miss `n=0`; `(64, 96]` occupy `n=1`; source-view `a=1` at 96.
- `test_wrap_pass_spanning_on_at_0_occupies_at_96` — On@0 Off@200, loop-head then wrap at 64, occupy at 96 `n=1`.
- `pio test -e native` 1350/1350

## HITL FAIL [`214856`](../../captures/session_20260818_214856.log)

Occupy CAP `as=`/`ae=` names the painted source-view span. The five `n=0 a=1` occupies are **not** wrap-committed On@96 after S:

| CAP | Pitch | `as`–`ae` | Occupy |
|-----|-------|-----------|--------|
| `98573816` | 30 | **528–544** | `n=0 a=1` |
| `107573372` | 12 | **0–192** | `n=0 a=1` |
| `109577175` | 12 | **144–232** | `n=0 a=1` |
| `111112659` | 12 | **720–767** | `n=0 a=1` |
| `122149713` | 24 | **216–408** | `n=0 a=1` |

`playMidiEvents` from `snapshotOverlapHoldCandidates` when `lastTickInLoop < occupyPhase` can cross session start (`shouldCommitOverdubWrap`) and seal a wrap on the USB occupy path. That is a transition change. Reverted.

Do **not** call `playMidiEvents` from occupy. Writer stays clock `playCommittedLoopMidi`. Successor: [`overdub_occupy_capture_stream_ledger_bugfix.md`](overdub_occupy_capture_stream_ledger_bugfix.md).

