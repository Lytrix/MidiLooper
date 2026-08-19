# Playback merged events window — follow-up refinement

**Kind:** refinement (follow-up)  
**Date:** 2026-07-17  
**Status:** Parked as a standalone follow-up. Engine play-ahead first path is the **per-event MIDI deadline vs 2-bar rebuild hypothesis** on [`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md) (A→B usable region→C LCR; 2-bar LCR not default after A). Domain `PlaybackWindow` and W2 pending-slot queue stay here / `slot-performance-interaction`.  
**Depends on:** [`boot_load_windowed_display_reconstruction_refinement.md`](boot_load_windowed_display_reconstruction_refinement.md) (`gatherPublishedEventsInWindow` / `PublishedEventRange`)  
**Does not fix:** OLED / boot display hang (separate path)  
**Does not own naming:** Name split is already planned elsewhere (see below)

---

## Naming duplication — already planned (do not re-specify)

The engine vs domain name collision is **already decided and tasked**. This follow-up must **not** invent a parallel rename plan.

| Layer | Name | Authority |
|-------|------|-----------|
| Engine merge cache (today misnamed `PlaybackWindow`) | **`PlaybackMergedMidiEvents`** | OpenSpec [`slot-performance-interaction`](../../openspec/changes/slot-performance-interaction/) **Phase −1** (D12); tasks −1.1…−1.5 |
| Domain musical working region | **`PlaybackWindow`** + `PlaybackWindowMode` | Same change **Phase 4** (tasks 6.1–6.3); architecture [`slot_playback_window_interaction_architecture.md`](slot_playback_window_interaction_architecture.md) |
| OLED detailed viewport | `DetailedWindowContext` / `DisplayWindowUtils` | Unchanged — UI only |

Implementation order for naming (locked in that OpenSpec):

1. **Phase −1 first** — rename today’s `include/PlaybackWindow.h` struct → `PlaybackMergedMidiEvents` (behavior-preserving).
2. Later **Phase 3–4** — domain `PlaybackWindow` lifecycle + struct (musical `start`/`length`), after gestures / SlotActionQueue as tasked there.

This document only adds **play-ahead / windowed fill** of the **engine** buffer and **NextGrid destination prewarm**, consuming the shared published gather from the display plan.

---

## Intent (what windowed *merge* was meant to do)

Constants **`Config::PLAYBACK_WINDOW_MIN_BARS` (2) / `PLAYBACK_WINDOW_MAX_BARS` (8)** and removed scaffolding (`effectiveWindowBars` / `windowStartBar`) were meant to **slice the engine merge buffer** (play-ahead), not to invent the domain type.

| Artifact | Intended | Current state |
|----------|----------|---------------|
| Engine buffer | Play-ahead merge near playhead | Full-loop `mergedEvents` via `ensurePlaybackWindowBuilt` |
| Bar metadata fields | Drive that slice | **Removed** (UIP D22) — set, never read for send |
| [`prewarmPlaybackForSlot`](../../src/Track.cpp) | Ready send path | Allocates playback order only — does **not** build `mergedEvents` |

Historical notes: [`record_overdub_memory_display_timeline_enhancement.md`](record_overdub_memory_display_timeline_enhancement.md); UIP D22; DEC-016 interval × representation.

---

## Why full-loop merge remains (rationale)

1. Audible correctness — edge misses are product failures.
2. M6 fixed materialize cost into the same full buffer; slice deferred.
3. Rename (Phase −1) was gated under `slot-performance-interaction`, which is not the active boot-display track.

---

## Relation to NextGrid

NextGrid is already **`SlotQuantization::NextGrid`** on [`SlotStateMachine`](../../src/SlotStateMachine.cpp) / [`TrackManager::requestSlotSwitch`](../../src/TrackManager.cpp). OpenSpec `slot-performance-interaction` owns gesture → LoopEnd vs NextGrid.

Windowed engine merge helps NextGrid only via **cheap destination (pending) prewarm** before grid commit — not by shrinking the current slot alone.

---

## Scope of *this* follow-up (behavior only)

**Prerequisite:** Display plan lands `gatherPublishedEventsInWindow`. Prefer Phase −1 rename already done or done in the same PR as first slice (call OpenSpec tasks −1.x; do not fork naming).

### W1 — Play-ahead fill of engine merge cache

1. Policy helper (e.g. `shouldUseWindowedPlaybackMerge`) — not raw bar literals at every call site; initial threshold may use `PLAYBACK_WINDOW_MAX_BARS`.
2. After rename (or behind aliases): build path fills via `gatherPublishedEventsInWindow` for playhead ± preload; short loops may stay full gather.
3. Slide / rebuild window as playhead advances; wrap-aware open notes; rebuild `playbackOrder` for buffer contents only.
4. Native + HITL: no dropped notes at window edges on 64-bar play.

### W2 — NextGrid destination prewarm

1. Extend `prewarmPlaybackForSlot` to build **windowed** merged events for target around upcoming commit phase.
2. Optional pending-slot buffer during `requestSlotSwitch(..., NextGrid)`.
3. Use existing pending-switch path — no second queue.

**Out of scope here:** Domain `PlaybackWindow` struct/lifecycle (OpenSpec Phases 3–4), gesture remap, SlotActionQueue — stay on `slot-performance-interaction`.

---

## Current codebase map

| Area | Symbol |
|------|--------|
| Merge cache (pre-rename) | [`include/PlaybackWindow.h`](../../include/PlaybackWindow.h), `LoopPlaybackRuntime::primaryWindow` |
| Build / send | `ensurePlaybackWindowBuilt`, `playMidiEvents*` |
| Prewarm | `prewarmPlaybackForSlot`, `prewarmPlaybackRuntime` |
| Constants | `Config::PLAYBACK_WINDOW_MIN_BARS` / `MAX_BARS` |
| Slot quantisation | `SlotStateMachine`, `requestSlotSwitch` |
| Naming tasks | `openspec/changes/slot-performance-interaction/tasks.md` § −1 and § 6 |

---

## Suggested order

```text
1. Bounded display reconstruction (windowed published gather)     ← current track
2. slot-performance-interaction Phase −1 rename                  ← existing OpenSpec
3. W1 play-ahead merge (this follow-up)
4. W2 NextGrid destination prewarm (this follow-up)
5. slot-performance-interaction Phase 3–4 domain PlaybackWindow  ← existing OpenSpec
```
