# Slot 2 overdub projection offset — bugfix

**Status:** Ready to implement (Option A)  
**Evidence:** [`captures/session_20260713_182830.log`](../captures/session_20260713_182830.log)  
**Branch:** `continuous-saving-stable`  
**Cursor plan:** `.cursor/plans/slot2_overdub_offset_fix_490ebce4.plan.md`

## Symptom

Slot 2 (index 1) overdub capture is **768 ticks (1 bar)** ahead of playhead/storage; bar 1 never receives notes. Slot 1 alone works.

```
COORD,abs,960,storage,1728,proj,1728,display,1728,projStart,2304,...
```

`projStart=2304` = slot 1 loop length; expected storage phase at abs 960 is **960**.

## Root cause

Track-level `projectionCycleStartTick` is advanced on wrap in **`playMidiEventsForSlot`** using each secondary slot’s length. With slot 1 (2304) still enabled during slot 2 (3072) overdub, the shared anchor is corrupted. [`capturePhaseTick`](../../src/Track.cpp) (mapper B) reads the wrong anchor.

## Fix — Option A (approved)

1. **`playMidiEventsForSlot`** — remove `projectionCycleStartTick = advanceProjectionCycleStartTickOnWrap(...)`; keep per-slot `nextEventIndex` reset on wrap.
2. **`startOverdubbing`** — re-anchor: `projectionCycleStartTick = currentTick - tickPhaseInLoop(...)` (same as record-stop).
3. **`startRecording`** — hygiene: `projectionCycleStartTick = currentTick` after `startLoopTick` stamp.
4. Display playhead — only if HITL still shows mismatch after 1–3.

## Cross-links

| Plan | Link |
|------|------|
| Multi-slot overdub **out of scope** | [`slot-performance-interaction` D22](../../openspec/changes/slot-performance-interaction/design.md) |
| Dimmed playing-slot overlay (separate) | D23 + [`slot_playback_window_interaction_architecture.md`](slot_playback_window_interaction_architecture.md) |
| Mapper B unchanged | [`capture_coordinate_canonical_decision_refinement.md`](capture_coordinate_canonical_decision_refinement.md) |

Layered **playback** (legacy hold) remains; only **active-slot overdub** is in scope.

## Pre-implementation review

**Proceed: YES.** ~15–25 lines in `Track.cpp` + native policy test + HITL.

See full pin-down table in Cursor plan file.

## Verification

- **Pre-edit / post-merge:** `rg 'projectionCycleStartTick\s*='` — confirm compliance with authorized-writer list (see plan).
- Native: simulate anchor advance by 2304 vs no advance; assert active-slot phase at abs 960.
- **HITL A:** slot 1 enabled → record slot 2 → overdub slot 2; `COORD` storage ≈ playhead; bar 1 usable.
- **HITL B (inverse):** record both slots (asymmetric lengths) → layered playback → slot 2 wraps ≥2× → overdub slot 1; no offset.

## Ownership invariant

> **`projectionCycleStartTick` represents the active projection cycle only. Layered playback may consume this anchor but must never modify it. Only active-slot lifecycle transitions and active-slot playback (`playMidiEvents`) may update this value.**

## Authorized writers after Option A

Architectural ownership contract — any future writer requires review.

| Function | Authorized |
|----------|------------|
| `startRecording`, `stopRecording`, `startOverdubbing` | Yes |
| `resetPlaybackState`, `resetPlaybackStateForSlot` (active only), `reanchorPlaybackProjection`, `commitQueuedPlaybackStart` | Yes |
| `playMidiEvents` (active wrap advance) | Yes |
| `playMidiEventsForSlot` | **No** |
| Display / storage / editor | **No** (read-only today) |

> **Any new assignment to `projectionCycleStartTick` outside this authorized-writer list is considered an ownership violation until it has been explicitly reviewed and documented.**

Pre-edit and post-merge: `rg 'projectionCycleStartTick\s*=' src/ include/` — confirm compliance.
