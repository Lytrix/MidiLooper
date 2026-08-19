# Occupy duplicate open identity (catch-up then clock)

**Status:** Native **PASS** 1365/1365. RAM1 **425804** / **4768**. HITL [`103234`](../../captures/session_20260819_103234.log) **duplicate-identity MET** (`led == n` on all mismatches). Extra unique identities remain — not this FAIL.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent (catch-up per-phase native, HITL extra-open remains):** [`overdub_occupy_catchup_open_note_stack_bugfix.md`](overdub_occupy_catchup_open_note_stack_bugfix.md)  
**Pin:** [`101319`](../../captures/session_20260819_101319.log) L2318 pitch 12 `n=2` `led=4` `hs=736`  
**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTickInLoop` / `nextEventIndex` from USB; Off stamping; changing `rebuildPlaybackOrder`; span-start `n=0 a=1` (pitch 23 `hs=384` on the pin)

---

## Invariant (one sentence)

An open playback identity occupies at most one `ActiveNoteLedger::Entry`. A second NoteOn with that `noteId` does not push; clock may still emit.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — writer stays `ActiveNoteLedger::noteOn` via `applyPlaybackEvent`. Catch-up still does not send or move the cursor. |
| **State transition change?** | NO — `shouldApply` bound unchanged. |
| **Reuse** | YES — `findIndexByNoteId` already used for identified Off. Duplicate On is a no-op push, `applyPlaybackEvent` still returns true so `playbackCursorAdvanceSend` still emits. |

## Debugging boundary

```
… → DEC-042 open-NoteOn ledger ← trust nested [`095902`]
 → catch-up per-phase Off then On ← trust native; HITL extra-open not met [`101319`]
 → same identity applied by catch-up then clock ← this RC
 → span-start n=0 a=1 / prepared b=0 ← parked
```

Do not make occupy catch-up run when `occupyPhase == lastTick`. Do not skip clock MIDI send when the identity is already open.

## Root cause

[`CommittedPlaybackLedgerCatchUp`](../../include/Utils/CommittedPlaybackLedgerCatchUp.h) applies committed events onto the ledger and **does not** mutate `lastTickInLoop` or `nextEventIndex`. Clock then walks the same `(lastTick, tickInLoop]` via `playbackCursorAdvanceSend` → `applyPlaybackEvent` → `noteOn`, which **always pushed**.

Before DEC-042 one slot last-wrote; a second On of the same id was invisible. After the stack, [`101319`](../../captures/session_20260819_101319.log) shows `led > n` (L2318 `n=2` `led=4`): occupy counts unique ids, the table holds extra copies. All 37 mismatches are `cu=0` at snapshot time; USB-before-clock occupies still run catch-up earlier in the interval, then clock replays.

## Fix

In `ActiveNoteLedger::noteOn`, if `noteId` is known and `findIndexByNoteId` hits, return without pushing and without overflow. Untagged On (`kInvalidNoteId`) still pushes. Identified Off resolution unchanged.

## Tests

[`test_playback_midi_output.cpp`](../../test/test_playback_midi_output/test_playback_midi_output.cpp):

- `test_ledger_note_on_same_identity_does_not_push_second_entry` — two `noteOn` with id 42; `forEachActive` count 1.

[`test_pending_note_change.cpp`](../../test/test_pending_note_change/test_pending_note_change.cpp):

- `test_occupy_ledger_catchup_then_clock_replay_keeps_one_covering_095902` — catch-up the 095902 interval, then apply the same in-interval events in phase order (clock replay). Occupy set is `{5242}` only; active count on that pitch is 1.

Keep `test_ledger_note_on_pushes_untagged_off_pops_lifo` (two **different** ids still stack).

## HITL

Want `led == n` on mismatches (no extra copies). Extra-open (`n>a`) down vs [`101319`](../../captures/session_20260819_101319.log) (25× `n=2 a=1`). Do not FAIL this RC on the parked span-start `n=0 a=1` or prepared `eq=0`/`b=0`.

## HITL [`103234`](../../captures/session_20260819_103234.log)

103 `DIAG,lcr,part`. **42** mismatches, all `cu=0`. `ledger,overflow` **0**. `eq=1` on all 103. **`led == n` on 42/42 mismatches** — duplicate-copy gate **MET**. No `led > n`.

| n,a | 101319 | 103234 |
|-----|------:|-------:|
| 1,1 | 25 | 36 |
| 0,0 | 4 | 23 |
| **0,1** | **1** | **0** |
| 2,2 | 0 | 2 |
| 1,2 | 1 | 3 |
| **1,0** | **3** | **18** |
| **2,1** | **25** | **20** |
| 2,0 | 1 | 0 |
| 3,1 | 6 | 0 |
| 3,2 | 0 | 1 |
| n>a | 35 | 39 |

`n=2 a=1` is two **distinct** ids (`led=2`), e.g. L6480 pitch 24 `hs=216` covering 5584 `168–360`. Not copies. `n=1 a=0` rose (L6898 pitch 12 `hs=192` `lid=5604` `lst=288`) — parked exclusive-end / wrap leftover, unmasked now that copies are gone.

Do not reopen occupy catching up when `occupyPhase <= lastTick`. Remaining extra unique identities and `n=1 a=0` are [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](overdub_occupy_clock_duplicate_off_ledger_bugfix.md) (clock duplicate Off skips ledger).

## Pre-implementation review

### Ready
- `noteOn` / `findIndexByNoteId` / `playbackCursorAdvanceSend` traced.
- Catch-up skip bound stays.

### Resolved
| Topic | Decision |
|-------|----------|
| Duplicate On | No-op push, `applyPlaybackEvent` returns true (clock still sends) |
| Cursor | Clock still owns `lastTick` / `nextEventIndex` |

### Open before coding
None.

### Proceed?
YES.
