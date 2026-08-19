# Occupy clock duplicate Off skips ledger

**Status:** Native **PASS** 1367/1367. RAM1 **425868** / **4768**. HITL **FAIL** leftover counts [`104654`](../../captures/session_20260819_104654.log). Pin two-Off dumps **0** this capture.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent (duplicate-identity MET):** [`overdub_occupy_duplicate_open_identity_bugfix.md`](overdub_occupy_duplicate_open_identity_bugfix.md)  
**Pin:** [`103234`](../../captures/session_20260819_103234.log) L6674 pitch 24 `n=3 a=2` `hs=120`; L6683–L6684 two `k=off` at `t=71`  
**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTickInLoop` / `nextEventIndex` from USB; Off stamping; changing `rebuildPlaybackOrder`; duplicate `noteOn` no-op push; catch-up per-phase Off then On

---

## Invariant (one sentence)

A second committed NoteOff (or NoteOn) at the same phase, pitch, and type still applies to `ActiveNoteLedger`; only the MIDI wire is deduped.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — writer stays `applyPlaybackLedgerEvent` from the clock cursor. Occupy stays a reader. |
| **State transition change?** | NO — `shouldApply` bound unchanged. Wrap `(prev, S]` unchanged. |
| **Reuse** | YES — extend `advancePlaybackCursor`. Duplicate suppression already exists for `PlaybackEmitPolicy::ActiveCommitted`; it must not skip ledger apply. |

## Debugging boundary

```
… → DEC-042 open-NoteOn ledger ← trust nested [`095902`]
 → catch-up per-phase Off then On ← trust native
 → duplicate identity catch-up then clock ← trust [`103234`] led == n
 → clock duplicate same-phase Off/On skips ledger ← this RC
 → prepared b=0 / span-start n=0 a=1 ← parked
```

Do not make occupy catch-up run when `occupyPhase == lastTick`. Do not send a second MIDI Off/On on the wire. Do not stamp Off identities.

## Root cause

`advancePlaybackCursor` with `PlaybackEmitPolicy::ActiveCommitted` treats same-phase, same-pitch, same-type as a duplicate and **does not call** `playbackCursorAdvanceSend`. That function is both ledger apply and MIDI emit. The second event is consumed by the cursor and never reaches `ActiveNoteLedger`.

Catch-up `applyOpenClosedInterval` applies every Off at that phase (no wire dedupe). After clock, `lastTickInLoop` has passed the tick (`cu=0`), so occupy cannot repair.

[`103234`](../../captures/session_20260819_103234.log) L6674–L6684 (pitch 24, `hs=120`, `rev=179`):

| Stream | What |
|--------|------|
| Covering spans | 5575 `72–167`, 5583 `120–144` (`a=2`) |
| Ledger | `n=3` `led=3` (newest 5583 `lst=120`) |
| `mmevt` | On@24 `5568`, **Off@71, Off@71**, On@72 `5575`, On@120 `5583` |

First Off@71 LIFO-pops one pre-72 identity. Second Off@71 is skipped. `5568` stays open. On@72 and On@120 stack on top → `n=3 a=2`.

Same leftover class as pitch 12 `lid=5604` `lst=288` (`n=1 a=0` when nothing covers; `n=2 a=1` when another span covers). Exclusive-end at the Off tick is not this writer: L6692 occupy 12 @ `hs=288` is `n=1 a=1` covering `240–336`.

## Fix

When the ActiveCommitted walk detects a duplicate NoteOn/NoteOff, call `playbackCursorAdvanceApplyLedger` (`applyPlaybackLedgerEvent` only). Keep `sendMidiEvent` skipped.

`playbackCursorAdvanceSendCapture` is unchanged (`ActiveCaptureOverdub` does not dedupe). Layered-slot walks do not dedupe.

## Tests

[`test_playback_cursor_advance.cpp`](../../test/test_playback_cursor_advance/test_playback_cursor_advance.cpp):

- `test_active_committed_duplicate_off_applies_ledger_without_second_midi` — two untagged Offs at phase 71 after On@24; MIDI Off count 1; ledger empty.

[`test_pending_note_change.cpp`](../../test/test_pending_note_change/test_pending_note_change.cpp):

- `test_occupy_clock_duplicate_off_closes_both_pre_abut_103234` — 103234 pitch 24 order On@24, On@48, Off@71, Off@71, On@72, On@120. Occupy at 120 is `{5575, 5583}` only. Two Ons before the duplicate Offs: skipping the second Off leaves `5568` (`n=3`).

## HITL

Want `n=3 a=2` = 0 vs [`103234`](../../captures/session_20260819_103234.log) (1). `n=1 a=0` down vs 18. `n=2 a=1` down vs 20. Keep `led == n` and `n=0 a=1` = 0.

Do not FAIL this RC on parked span-start `n=0 a=1` or prepared `eq=0`/`b=0`. Nested `n=2 a=2` may remain.

## HITL [`104654`](../../captures/session_20260819_104654.log)

72 `DIAG,lcr,part`. **41** mismatches, all `cu=0`. `led == n` **41/41**. `n=0 a=1` **0**. `eq=1` on 72/72. `ledger,overflow` **0**. One `RING,overflow` (after these occupies).

| n,a | 103234 | 104654 |
|-----|------:|-------:|
| 1,1 | 36 | 25 |
| 0,0 | 23 | 5 |
| **0,1** | **0** | **0** |
| 2,2 | 2 | 1 |
| **1,0** | **18** | **19** |
| **2,1** | **20** | **10** |
| 2,0 | 0 | 4 |
| **3,2** | **1** | **2** |
| 3,1 | 0 | 6 |
| n>a | 39 | 41 |

**Pin two-Off:** **0** consecutive same-tick `k=off,k=off` in this capture's `mmevt` dumps (103234 L6683–L6684 is not here).

**Remaining `n=3 a=2` is not that pin.** L3910 pitch 12 `hs=240`: covering 5805 `240–288` and 5799 `240–336` (`a=2`); `mmevt` two **On@240** (5799, 5805); `n=3` `lid=5805`. Nested same-start pair is on the ledger; one extra identity is not those Ons.

**`n=1 a=0` is one stuck identity:** all 19 are pitch 12 `lid=5701` `lst=96`, including occupy at `hs=96` (start tick, `a=0`) and wrap-head `hs=0`. Not exclusive-end at an Off tick. Same leftover also produces `n=2 a=1` / `n=3 a=1` when other spans cover.

`n=2 a=1` **down** 20→10. Do not reopen duplicate-Off ledger apply. Leftover `5701` is [`overdub_occupy_leftover_identity_bugfix.md`](overdub_occupy_leftover_identity_bugfix.md).

## Pre-implementation review

### Ready
- `advancePlaybackCursor` duplicate skip and `playbackCursorAdvanceSend` traced.
- Catch-up already applies both Offs; clock is the missing writer.
- Pin L6674 two Off@71 is in the capped `mmevt` dump (not inferred).

### Resolved
| Topic | Decision |
|-------|----------|
| Wire | Still one MIDI Off/On per phase/pitch/type |
| Ledger | Every crossed Off/On applies |
| Cursor | Clock still owns `lastTick` / `nextEventIndex` |

### Open before coding
None.

### Proceed?
YES.
