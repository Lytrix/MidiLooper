# Post-undo LED lookup — resumable source

**Status:** Stage 1 device PASS [`114736`](../../captures/session_20260816_114736.log). Stage 2 **rejected**.  
**Date:** 2026-08-16  
**Kind:** refinement  
**Parent measurement:** [`post_undo_playing_loop_prefix_measurement.md`](post_undo_playing_loop_prefix_measurement.md) — device PASS [`113804`](../../captures/session_20260816_113804.log)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) (do not wire resolution onto MIDI/display until three gates); DEC-016 derived representations

## Problem

After undo, PLAYING stays armed for LED rem and the next bar wrap runs `MidiLedManager::prepareLedNoteLookup` → `Loop::gatherCommittedEventsWithCapture`. That is **99.99%** of `midi_led_lookup` (225–403 ms). It sits in `loop()` before idle, so MIDI Input Gap equals that gather.

`gatherCommittedEventsWithCapture` is `gatherCommittedEvents` (`copyEffectiveCommittedEvents` → `ensureEffectiveEventStoreCurrent` + full `passesMaterializedStore_` copy) then `mergeCaptureStoreIntoMaterializedEvents`. After undo, capture is not the cost; the full committed flatten is.

This is not a drain-lifecycle fix. Re-arming PLAYING drain polls around idle/load. It does not split `updateLeds`.

## What already exists (do not rebuild)

| Piece | Owner | Fact |
|-------|-------|------|
| Clean-path LED source | `hasNoteOnInRangeForLed` | When `!visualCacheDirty`, scans `visualCache.notes` |
| Dirty-path LED source today | `prepareLedNoteLookup` | Full gather into `ledNoteLookupEvents_` |
| Live capture | `hasNoteOnInRangeForLed` | Already scans `capture.store` when `captureActive()` |
| Stale mark | `Loop::markDisplayCachesStale` | Sets `visualCacheDirty` and dirty bars; **does not clear** `visualCache.notes` |
| Undo contract | DEC-036 Layer D | Mark stale only. Idle `slice_clean` catches display up. |
| Resumable rebuild | `Track::processDeferredIdleMaintenance` → `Loop::rebuildVisualCacheIdleSlice` | PLAYING: 2–4 bars/slice, playhead-priority |
| LCR on that idle path | `rebuildVisualCacheIdleSlice` | `LoopContentResolution::tryResolvePreparedWindow`; else `gatherCommittedEventsInWindow` |
| Display stale-while-revalidate | `DisplayManager` resolve | Uses non-empty `visualCache.notes` while dirty |

LCR is already the idle visual-cache source when a window is prepared. The LED BAR path is a **second full-loop flatten** of the same committed content.

## Why not call LCR from `prepareLedNoteLookup`

DEC-037 §10: production consumers swap only after three gates. CURRENT_WORK: do not wire resolution onto MIDI/display until those gates pass. LED presence is a display-class consumer.

`tryResolvePreparedWindow` from `updateLeds` would run on the BAR `loop()` prefix (same class as “do not put resolution on `handleMidiInput`”). Device 6C consume on this branch has been **31 ms** and **313 ms** ([`210508`](../../captures/session_20260815_210508.log)). That is not a MIDI-safe BAR query.

Do **not** start playback gather ([`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md)). That is a different consumer.

## Rule this change must keep

**`updateLeds` must never trigger a committed-content gather merely because `visualCacheDirty` is true.**

MIDI Input Gap on the BAR prefix must not include a full committed flatten. Long derived work stays in the existing idle slice and can stop/resume across `loop()` iterations. No new Session, no new Manager, no LCR call from `MidiLedManager`, no drain change.

`ledNoteLookupEvents_` is not the invariant. If it becomes dead after Stage 1, remove it in a follow-up cleanup.

## Chosen source

**Read `visualCache.notes` for LED presence whenever the list is non-empty. Stop calling `gatherCommittedEventsWithCapture` from `prepareLedNoteLookup`.**

```text
undo / commit
   │
   └─ markDisplayCachesStale()     notes kept, dirty=1
          │
          ▼
   BAR updateLeds
          │
          ├─ capture.store if capturing     (already)
          └─ visualCache.notes if non-empty (stale OK)
          │
          ▼
   idle rebuildVisualCacheIdleSlice         (already; LCR window when prepared)
          │
          └─ slice_clean → dirty=0
                 │
                 ▼
            next bar updateLeds
                 └─ same notes, now current
```

Resume is the idle owner that already exists. The next `updateLeds` after `slice_clean` uses the same notes, now current. `updateLeds` only refreshes on bar change, so the first current LED frame is the next bar boundary after `slice_clean`. Stage 2 (force refresh) is rejected.

While dirty, bar/16th LEDs can still show the **pre-undo** overdub until idle replaces those bars. That matches OLED stale-while-revalidate and the Layer D undo contract.

If `visualCache.notes` is empty (never built), presence is false. Do not gather to fill it on the BAR path.

## Architecture checkpoint

1. Ownership change? **NO** — `MidiLedManager` already reads `visualCache.notes` when clean. Loop remains derivation owner. Idle remains rebuild owner.
2. State transition change? **NO** — do not re-arm drain. Do not add an LED session. Do not change undo.

Reuse: extend `hasNoteOnInRangeForLed` / `prepareLedNoteLookup`. Do not add `tryResolvePreparedWindow` on this path.

## Lookup order (Stage 1)

```text
hasNoteOnInRangeForLed()
    ├── capture active?
    │     └── scan capture.store
    │
    └── visualCache.notes non-empty?
          └── scan visualCache.notes
```

Not: dirty or empty cache → `gatherCommittedEventsWithCapture`. Empty notes → false.

## Stages

### Stage 1 — stop the BAR gather (approved)

`hasNoteOnInRangeForLed` uses the order above. `prepareLedNoteLookup` does not gather. Consumer-path cleanup only — not LCR, not overdub architecture.

Device gate: same undo → PLAYING → bar wrap cluster as [`113804`](../../captures/session_20260816_113804.log). Scored: [`114736`](../../captures/session_20260816_114736.log).

Pass:

- no `midi_led_gather` rem
- `midi_led_lookup` / `midi_led_phase` below the 50 ms one-shot
- 5 s `midi_gap` in that window is not a 225–400 ms lookup
- `clockrate` holds 47–48 while PLAYING
- idle still emits `slice_clean`; bar/16th LEDs eventually match the undone content

## Stage 1 — device [`114736`](../../captures/session_20260816_114736.log) PASS

Seventeen `Overdub undone` while PLAYING. No `midi_led_gather`, `midi_led_lookup`, `midi_led_phase`, `midi_leds`, or `loop_prefix` rem (all < 50 ms). Rem spans in this capture are only `idle_maint` and `load_frame`.

First undo 13.285 s: `VCACHE,stale` keeps **1643** notes, `dirty=1`. `slice_clean` at 15.305 s. Next BAR `2320,3` at 14.195 s → LED 3.7 ms later (14.199 s). [`113804`](../../captures/session_20260816_113804.log) was 225 ms of gather on that hole.

PLAYING `clockrate` **47–48** from 15.565 s through 75.719 s.

PLAYING 5 s `midi_gap` is **110–127 ms**, not 225–400 ms. Those windows contain `load_frame` 60–74 ms and `idle_maint` 46–55 ms (5 s max). Boot `midi_gap` 835 ms at 10.554 s is `load_frame` 802 ms + `idle_maint` 122 ms before the first undo.

Idle `slice_clean` continues after later undos (notes stay 1643–1677). Stage 2 stays rejected.

### Stage 2 — refresh on `slice_clean` (**rejected**)

Do not reset `lastUpdateBar` on dirty→clean. Reopen only if Stage 1 HITL shows unacceptable one-bar LED lag after timing is fixed.

### Not this change

- Re-arm PLAYING drain
- `tryResolvePreparedWindow` / `resolveWindow` / `resolveState` from `MidiLedManager`
- Sliced gather session inside `MidiLedManager` (parallel flatten)
- Playback gather ([`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md))
- Visual-cache idle grain / LoopPersist CRC
- Splitting `gatherCommittedEvents` vs merge (measurement closed; that function leaves this path)

## Pre-implementation review (for the implementing session)

### Ready
- Dirty gather owner is proven [`113804`](../../captures/session_20260816_113804.log).
- Clean LED path and idle LCR window path already exist.

### Resolved
| Topic | Decision |
|-------|----------|
| Source while dirty | `visualCache.notes` if non-empty |
| LCR | Idle slice only, not `updateLeds` |
| Resume | `rebuildVisualCacheIdleSlice` |
| Drain | Do not re-arm |

### Open before coding
None.

### Proceed?
YES — Stage 1 only. Stage 2 rejected.

## Consumer-path pattern

Derived content requested on a MIDI-sensitive path, not where it is owned:

```text
                    derived content
                         │
          ┌──────────────┼──────────────┐
          ↓              ↓              ↓
       display          LED          overdub
          │              │              │
       idle slice     BAR path       prepared consume
          │              │              │
       bounded        Stage 1        bounded/
       rebuild        no gather      prepared
```

Keep this a `MidiLedManager` lookup cleanup. Do not fold it into LCR or overdub architecture.

Successor inventory (no firmware): [`runtime_scheduler_lcr_consumer_grooming_refinement.md`](runtime_scheduler_lcr_consumer_grooming_refinement.md).
