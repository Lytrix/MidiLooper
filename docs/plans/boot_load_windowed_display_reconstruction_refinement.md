# Bounded published reconstruction — refinement

**Kind:** refinement  
**Date:** 2026-07-17  
**Status:** Device fix 2026-07-17 — paint/gather window align + gather-margin cache  
**Cursor plan:** `.cursor/plans/display_hang_window-first_0f6f58f2.plan.md`  
**Parent:** [`prioritized_boot_load_isolation_refinement.md`](prioritized_boot_load_isolation_refinement.md) Phase 1C + 2B

## Device gate follow-up (`session_20260717_234050`)

| Symptom | Evidence | Fix |
|---------|----------|-----|
| OLED hangs after first piano-roll paint | 3 boots in one capture; first `DISP` then ~6–10s restore drain; DFRAME keeps counting (~10ms) while panel stuck | Do not OLED-SPI immediately before deferred SD read; reuse live display notes while restore/undo pressure is active; short loops still `ensureVisualCacheBuilt` once under that pressure |
| `shouldAvoidFullVisualRebuild` forced true during undo hydrate | Skipped short-loop cache build; `syncDetailedPaintWindow` cannot run &lt;16 bars → empty roll after invalidate | Separate long-loop avoid from transient `shouldDeferHeavyDisplayRebuild` |

## Architectural goal

> Replace synchronous full reconstruction with bounded published reconstruction.

Stopping full rebuilds and windowed published gather are **one feature**.

## Canonical API

```text
Published event gathering
        │
        ├── Full     → Loop::gatherPublishedEvents(...)
        └── Windowed → Loop::gatherPublishedEventsInWindow(...)
                 │
                 ▼
          PublishedEventRange  →  LoopEventStore
```

- Consumers request published events; they do not invent gather helpers.
- `PublishedEventRange` is a thin view (wrap-aware chunk/window intersect on `firstTick`/`lastTick` + event filter). Display does not call store append APIs directly.
- Policy: `shouldAvoidFullVisualRebuild(loop, loopLength)` — not hard-coded `>16 bars` at call sites.
- Terminology: **windowed reconstruction** / **bounded reconstruction** (not “provisional”).
- Idle: budget-driven visual-cache advance using windowed gather per slice.
- Normative: `mergeActiveCapturePasses()` is capture-time only; published display must not depend on Active capture state.

## Wrap-aware invariant

Chunk/window intersection must remain loop-wrap aware (e.g. note-on near loop end, note-off after wrap). Align with existing `DisplayWindowUtils` window filtering.

## Playback window vs display window

**Do not reuse `PlaybackWindow` / `ensurePlaybackWindowBuilt` for OLED viewport work.** That type is a full-loop (or session-preview) merge cache for MIDI send — not a tick-bounded paint window (see OpenSpec `long-loop-piano-roll-window` design).

**Do share:** `IntervalProjection` wrap math, and the new canonical `gatherPublishedEvents` / `gatherPublishedEventsInWindow` hierarchy. Display already owns viewport geometry in `DisplayWindowUtils`. Playback may later consume windowed gather if bar-sliced playback is ever designed; that is out of scope for this pass.

### Why playback merge is full-loop today (documented rationale)

The name suggests a tick window; the implementation does not. Documented reasons:

1. **Product need for send:** `playMidiEvents` walks a sorted order across the whole loop with play-ahead `nextEventIndex` and wrap reset. Audible playback must emit every note in time — a paint viewport is not sufficient ([`record_overdub_memory_display_timeline_enhancement.md`](record_overdub_memory_display_timeline_enhancement.md) § playback window + play-ahead).

2. **Scaffolding that never shipped:** `effectiveWindowBars` / `windowStartBar` / `PLAYBACK_WINDOW_MAX_BARS = 8` were set on the struct but **never sliced** `mergedEvents`. Unified-interval-projection **D22** deleted those dead fields; preload/bar-slice playback was deferred.

3. **Naming debt (acknowledged):** [`slot_playback_window_interaction_architecture.md`](slot_playback_window_interaction_architecture.md) plans rename today’s struct → **`PlaybackMergedMidiEvents`**, and reserves domain **`PlaybackWindow`** (`start`/`length`, `FullLoop`|`Window`) for a future musical working region — separate from the merge cache and from OLED `DetailedWindowContext`.

4. **DEC-016 intent vs brownfield:** Architecture wants interval × representation (a consumer can request a `TickInterval`). Playback’s derived rep today is still the full merged event list; windowed playback merge is a future representation/scheduling choice, not what shipped for hang-fix / chunk-ref merge (M6 Phase 1).

### Parked: smaller playback merge vs NextGrid (not this pass)

**Useful later — separate from display hang.**

| Goal | Does shrinking today’s merge cache help? |
|------|------------------------------------------|
| Faster OLED / boot display | **No** — display does not use `PlaybackWindow` |
| Lower PLAYING heap/CPU on 64-bar loops | **Yes** — unfinished `PLAYBACK_WINDOW_MAX_BARS` scaffolding |
| **NextGrid** (16th) slot launch/restart | **Indirect only** — commit timing is `SlotQuantization::NextGrid`, not merge size. What matters is **fast prep of the destination slot’s** send buffer before the grid tick (prewarm / pending merge). A play-ahead window on the *current* slot alone does not implement NextGrid |

Documented direction ([`slot_playback_window_interaction_architecture.md`](slot_playback_window_interaction_architecture.md), OpenSpec `slot-performance-interaction`): rename merge cache → **`PlaybackMergedMidiEvents`**; domain **`PlaybackWindow`** = musical `start`/`length`; short launch often **LoopEnd**, double **NextGrid**; real sliced merge deferred until that work.

**Risk if sliced early:** missed note-ons/offs at window edges, wrap-spanning notes, rebuild glitches — audible failures. Display windowed reconstruction is safer first (paint-only).

**This pass:** do not shrink `mergedEvents`. Land `gatherPublishedEventsInWindow` so playback can later reuse the same gather for play-ahead / destination prewarm.

**Follow-up plan (parked):** [`playback_merged_events_window_refinement.md`](playback_merged_events_window_refinement.md) — play-ahead engine merge + NextGrid destination prewarm. **Naming split is not re-planned there** — it already lives in OpenSpec `slot-performance-interaction` Phase −1 (`PlaybackMergedMidiEvents`) and Phase 4 (domain `PlaybackWindow`).


## Verification (architectural)

For loops where `shouldAvoidFullVisualRebuild` is true, `DisplayManager::resolveDisplayNotes` must not invoke `ensureVisualCacheBuilt`.

Evidence baseline: [`captures/session_20260717_220705.log`](../../captures/session_20260717_220705.log) — mid_pass=0; hang = sync full visual rebuild (`DISP` 1379 = note counts).

Full implementation map, todos, and out-of-scope: see Cursor plan file above.
