# Occupy catch-up per-phase Off then On (open-NoteOn stack)

**Status:** Native **PASS** 1363/1363. Firmware RAM1 **425804** / **4768**. HITL [`101319`](../../captures/session_20260819_101319.log) **not met** — extra-open remains; one span-start `n=0 a=1`.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent (DEC-042 nested HITL MET, extra-open unmasked):** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](overdub_occupy_active_note_ledger_cardinality_refinement.md)  
**Pin:** [`095902`](../../captures/session_20260819_095902.log) pitch 12 `hs=336` `n=3 a=1`  
**Does not reopen:** Off stamping; option B; clock equal-phase Off before On; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; occupy catching up when `occupyPhase <= lastTick`; changing `rebuildPlaybackOrder`; `n=1 a=2` same-tick double On (pitch 24 `hs=456` on the pin)

---

## Invariant (one sentence)

`CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval` applies in-interval events in phase order, and at each phase Off before On, so an untagged Off can close a NoteOn that started in the same `(lastTick, occupy]` window.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — writer stays `CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval` → `ActiveNoteLedger::applyPlaybackEvent`. Occupy still does not send, wrap, or mutate `lastTickInLoop` / `nextEventIndex`. |
| **State transition change?** | NO — `shouldApply` bound unchanged (`occupyPhase <= lastTick` skips; wrap skip unchanged). |
| **Reuse** | YES — same helper, same `didPlaybackEventCross(false, lastTick, evPhase, occupyPhase)` interval. Equal-tick rule stays the clock/NoteUtils key (Off before On at one phase). No scratch vector. Do not walk `playbackOrder` from USB (native helper has no order; prior RC forbade `rebuildPlaybackOrder` changes). |

## Debugging boundary

```
… → DEC-042 open-NoteOn ledger (nested n=0 a=1) ← trust [`095902`]
 → USB occupy ledger catch-up (lastTick, occupyPhase] ← this RC (global two-pass vs stack)
 → clock equal-phase Off before On ← trust [`001021`]
 → same-tick double On n=1 a=2 / exclusive-end n=1 a=0 ← parked
```

Do not stamp Offs. Do not call `playMidiEvents` from occupy. Do not make occupy catch-up run when `occupyPhase == lastTick`.

## Root cause

DEC-042 made a lane a stack. Catch-up still walks **all Offs in the interval, then all Ons** ([`overdub_occupy_same_tick_off_before_on_bugfix.md`](overdub_occupy_same_tick_off_before_on_bugfix.md)). That was correct for one-slot last-write at equal tick. It is wrong for a stack: an Off for a note that **starts in the same interval** hits an empty lane (orphan, no pop), then the On is pushed and never closed.

[`095902`](../../captures/session_20260819_095902.log) pitch 12 `hs=336` `n=3 a=1` `lid=5242`: covering `328–432` (5242); ended `240–287` (5218) and `288–327` (5224). `mmevt` has `Off@287` and `Off@327` (`id=0`). All 44 mismatches in that capture are `cu=0` (this occupy did not catch-up). The two-pass algorithm still cannot reconstruct that interval correctly; native fixture is the gate for this RC.

## Fix

In `applyOpenClosedInterval`, repeatedly take the next in-interval phase after the last applied phase, then two walks **at that phase only** (Offs, then remaining). No scratch. Equal-tick replacement (`test_occupy_ledger_catchup_same_tick_off_before_on_replaces`) stays Off then On at 240.

The per-phase loop lives in [`src/Utils/CommittedPlaybackLedgerCatchUp.cpp`](../../src/Utils/CommittedPlaybackLedgerCatchUp.cpp) `FLASHMEM` / `noinline` (`applyOpenClosedIntervalEvents`). An inline header template of that loop grew RAM1 code across the 32 KB ITCM boundary (425804 → 426060, locals −28000). Same placement as `ActiveNoteLedger::applyPlaybackEvent`.

## Tests

Native in [`test_pending_note_change.cpp`](../../test/test_pending_note_change/test_pending_note_change.cpp):

- Keep `test_occupy_ledger_catchup_same_tick_off_before_on_replaces` (On listed before Off at 240; occupy id is the new On).
- `test_occupy_ledger_catchup_closes_ons_started_in_same_interval_095902` — vector order from [`095902`](../../captures/session_20260819_095902.log) `mmevt` (`On@240` 5218, `Off@287`, `On@288` 5224, `On@328` 5242, `Off@327`); lastTick 232; occupy 336. Occupy set is `{5242}` only. `lastTick` / `nextEventIndex` unchanged.

## HITL

Want extra-open (`n>a`, including `n=2 a=0`) down vs [`095902`](../../captures/session_20260819_095902.log) (18× `n=3 a=1`, 15× `n=2 a=0`). Nested `n=0 a=1` stays 0. Do not FAIL this RC on parked `n=1 a=2` same-tick double On or exclusive-end `n=1 a=0`. Pin mismatches remain `cu=0`; a remaining extra-open after this ship is a new clock-path RC, not a silent widening of this helper.

## HITL [`101319`](../../captures/session_20260819_101319.log)

66 `DIAG,lcr,part`. **37** mismatches, all `cu=0`. `ledger,overflow` **0**. `RING,overflow` **3**. `eq=0` on all 66 (`b=0`): `tryCollectPreparedPresentNoteIdsAtTick` was not ready — parked prepared-span miss, not this gate.

| n,a | 095902 | 101319 |
|-----|------:|-------:|
| 1,1 | 24 | 25 |
| 0,0 | 7 | 4 |
| **0,1** | **0** | **1** |
| 2,2 | 2 | 0 |
| 1,2 | 1 | 1 |
| 1,0 | 2 | 3 |
| 3,1 | 18 | 6 |
| **2,0** | **15** | **1** |
| **2,1** | **6** | **25** |
| 4,2 | 1 | 0 |
| n>a total | 40 | 35 |

**`n=2 a=0` pin shape nearly gone** (15 → 1). Extra-open is not gone: pitch 12 is 22× `n=2 a=1`. Several mismatches have `led > n` (e.g. L2318 `n=2` `led=4`): occupy counts unique `noteId`s, the ledger holds extra copies of those ids.

One `n=0 a=1`: pitch 23 `hs=384` `as=384–432` (5395), `led=0` `ltick=384` `cu=0`. Span-start empty ledger. First eight `mmevt` do not include `On@384`.

Do not widen catch-up. Do not make occupy catch-up when `occupyPhase <= lastTick`. Remaining extra-open / span-start empty ledger is successor [`overdub_occupy_duplicate_open_identity_bugfix.md`](overdub_occupy_duplicate_open_identity_bugfix.md).

## Pre-implementation review

### Ready
- Owner and call site traced: `applyOpenClosedInterval` / `Track::catchUpCommittedPlaybackLedgerToPhase`.
- Equal-tick fixture already pins Off-before-On at one phase.

### Resolved
| Topic | Decision |
|-------|----------|
| Apply order | Per-phase Off then On (clock / `sortMidiEventsChronologically` keys) |
| Scratch / `playbackOrder` | No — scan for next phase; keep helper vector-only |
| Skip bound | Unchanged |

### Open before coding
None.

### Proceed?
YES.
