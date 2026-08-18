# Occupy same-tick Off before On (catch-up interval)

**Status:** Native **PASS** 1357/1357. `teensy41-capture-serial` links (RAM1 code 425852, locals 4768). HITL **FAIL** [`235314`](../../captures/session_20260818_235314.log) — leftover `n=1 a=0` **met** (0); `n=0 a=1` **not met** (4). Pin [`233247`](../../captures/session_20260818_233247.log) was 11.  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent (interval catch-up shipped, gate not met):** [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](overdub_occupy_on_tick_clock_catchup_bugfix.md)  
**Pin:** [`233247`](../../captures/session_20260818_233247.log)  
**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; `playMidiEvents` from occupy ([`214856`](../../captures/session_20260818_214856.log)); folding capture into `mergedMidiEvents`; occupy fallback; DisplayManager; mutating `nextEventIndex` / `lastTickInLoop`; `n=1 a=2` as this gate; `rebuildPlaybackOrder` Off-before-On; changing `applyPlaybackLedgerEvent`

---

## Invariant (one sentence)

When reconstructing committed ledger state over `(lastTickInLoop, occupyPhase]`, equal-tick replacement resolves Off before On. Occupy still does not advance playback.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `applyPlaybackEvent` on committed-only `mergedMidiEvents`. |
| **State transition change?** | NO — no wrap/commit on USB. Clock still owns cursor, `nextEventIndex`, `lastTickInLoop`, send. |
| **Reuse** | YES — two-pass apply in [`CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval`](../../include/Utils/CommittedPlaybackLedgerCatchUp.h) over `(lastTick, occupyPhase]` only (Offs, then remaining in-interval events). Same equal-tick rule as `NoteUtils::sortMidiEventsChronologically`. No scratch vector. Keep wrap skip and exclusive `occupyPhase <= lastTick`. No occupy fallback. No DisplayManager. Do **not** change `rebuildPlaybackOrder` or `applyPlaybackLedgerEvent`. |

## Debugging boundary

```
… → USB occupy ledger catch-up (lastTick, occupyPhase] ← trust interval after Off-before-On
 → equal-tick Off then On inside that interval ← current investigation
```

Do not rebuild ledger from tick 0 on USB. Do not call `playMidiEvents` from occupy. Park `rebuildPlaybackOrder` Off-before-On as a possible next RC — not this commit.

## Root cause

Source-view `a=` uses inclusive start (`linearStart <= s && s < linearEnd`). Adjacent same-pitch grid notes **abut**. [`233247`](../../captures/session_20260818_233247.log) pitch 12 `192–240` and `240–288`: occupy at 240 is present on the new note and absent on the old (exclusive end).

L2387: USB `MI,U,144,4,12,100` then occupy `hs=240` `as=240–288` `n=0 a=1`. No `MO` On 12 in ±150 ms — clock did not emit. Catch-up `(lastTick, 240]` **includes both Off@240 of 192–240 and On@240 of 240–288**.

[`CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval`](../../include/Utils/CommittedPlaybackLedgerCatchUp.h) walked `mergedEvents` in **vector order**. [`LoopPasses::mergeSortedMidiVectors`](../../src/Loop/LoopPasses.cpp) compares **tick only**. If On@240 is applied then Off@240, `ActiveNoteLedger` last-writes empty → `n=0` while `a=1`.

The parent L5241 fixture used On@192 Off@280. Off is **not** in `(184, 192]`. That fixture cannot fail this way.

This capture proves catch-up vector order is wrong. It does **not** prove the clock path is the gate. Do not bundle `rebuildPlaybackOrder` into this RC.

| Kind | Count | Gate |
|------|------:|------|
| `n=1 a=1` | 35 | match |
| `n=0 a=0` | 49 | empty lane |
| **`n=0 a=1`** | **11** | **this RC** (8× pitch 12, plus 30, 24, 23) |
| **`n=1 a=0`** | **0** | parent leftover **met** |
| `n=1 a=2` | 3 | product; not this gate |

Closed pins: occupy 12 @ `hs=0` sounding `n=1 a=1`. `RING,overflow` once is observability.

## Fix

Two sequential walks of the same `events` in `applyOpenClosedInterval`:

1. In-interval Offs
2. Remaining in-interval events (Ons and non-note)

No scratch allocation. `shouldApply` unchanged. FLASHMEM occupy TU unchanged.

## Tests

Native in [`test_pending_note_change.cpp`](../../test/test_pending_note_change/test_pending_note_change.cpp):

- `test_occupy_ledger_catchup_same_tick_off_before_on_replaces` — On@192 Off@240 (old id) and On@240 Off@288 (new id); vector lists On@240 before Off@240; lastTick 232; occupy 240. Occupy `noteId` is the new On@240 id. `lastTick` / `nextEventIndex` unchanged.
- `test_occupy_ledger_catchup_same_tick_skip_when_occupy_equals_last_tick` — same events; lastTick 240 occupy 240. `shouldApply` false. Ledger unchanged (empty if start empty).
- Keep L5241 isolated On@192 Off@280 unchanged.

## HITL

**FAIL** [`235314`](../../captures/session_20260818_235314.log): 1-bar 768 OVERDUBBING; `hs=` on 133/133 occupies. `RING,overflow` three times. Firmware includes two-pass catch-up (`e723313`).

| Kind | Count | Gate |
|------|------:|------|
| `n=1 a=1` | 43 | match |
| `n=0 a=0` | 85 | empty lane |
| **`n=0 a=1`** | **4** | **FAIL** (want 0; was 11 on [`233247`](../../captures/session_20260818_233247.log)) |
| **`n=1 a=0`** | **0** | **met** (parent leftover) |
| `n=1 a=2` | 1 | product; not this gate |

`n=0 a=1` all pitch 12:

| Line | us | `as`–`ae` | `hs` | Kind |
|------|---:|-----------|------:|------|
| L4294 | `75477461` | 240–384 | 352 | interior |
| L4750 | `89187597` | 192–288 | 240 | interior |
| L4811 | `92188216` | 624–720 | 624 | span start (`hs==as`) |
| L4899 | `95191106` | 240–336 | 240 | span start (`hs==as`) |

L4899: prior occupy 12 @ `hs=48` `as=48–240` `n=1 a=1` (L4897). Then occupy 12 @ `hs=240` `as=240–336` `n=0 a=1`. Same abut exclusive-end / inclusive-start shape as [`233247`](../../captures/session_20260818_233247.log) L2387.

Closed pins this run: occupy 12 @ `hs=0` sounding `n=1 a=1` (L3964, L3997 `as=0–192`). L3888 / L4605 occupy 12 @ `hs=0` are `n=0 a=0` `as=ae=0` (empty source-view). Do not treat `n=1 a=2` as this FAIL. Do not call `playMidiEvents` from occupy. Do not fold capture into `mergedMidiEvents`. Do not change `rebuildPlaybackOrder` from this capture without a native/HITL fixture that shows clock last-writes Off after On at equal tick.
