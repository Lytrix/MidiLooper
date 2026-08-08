# Design — slot-selection-focus

**Date:** 2026-07-05  
**Status:** Ready for implementation — parallel to UIP Phase 5  
**Plan:** [`docs/Plans/slot_selection_orchestration_refinement.md`](../../docs/Plans/slot_selection_orchestration_refinement.md)

---

## Context

Each track has two slot indices today:

| Index | Owner | Role |
|-------|--------|------|
| `activeLoopIndex` | `Track` | Playback, capture, `getActiveLoop()` |
| `selectedSlotIndex` | `SlotStateMachine` via `TrackManager` | UI focus |

`setSelectedTrack` already orchestrates track focus (depart → index → arrive). Slot switching is fragmented: partial edit hooks, direct `setActiveLoopIndex` at many call sites, mode-specific display refresh (NOTE_EDIT only rematerialises reliably).

UIP Phase 4 shipped `queuedStartTick` and `requestSlotSwitch`. This change **consumes** that API; it does not modify `IntervalProjection`.

---

## Goals / Non-Goals

**Goals:**

- Canonical **Departure → Transition → Arrival** lifecycle for slot selection (application pattern for future focus kinds)
- Session-type-agnostic edit rebind (Loop, Note, ControlChange stub)
- `DisplayManager`-owned cache invalidation (no sync draw on focus path)
- Persist `selectedSlotIndex[]` independently in SD footer
- `SyncPlayback` Phase 1 parameter; queued start stays caller-composed

**Non-Goals:**

- UIP / interval projection changes
- Full ControlChange edit
- Generic `beforeFocusChange` API (document evolution path)
- Replacing multi-slot layered playback
- EEPROM / settings.json

---

## Decisions

### D1 — Orchestrator vs playback policy

**Decision:** `TrackManager::setSelectedSlotIndex(track, slot, SyncPlayback)` orchestrates focus lifecycle only. **Default parameter: `SyncPlayback::Yes`** (immediate active sync when caller omits the argument).

**Caller matrix (explicit overrides):**

| Context | SyncPlayback | Playback follow-up |
|---------|--------------|-------------------|
| Stopped / immediate pick | `Yes` (default) | `startPlaying` etc. as today |
| Playing short-press (incl. edit mode) | **`No`** | `requestSlotSwitch(NextGrid)` — departure commits edit pass first, then UI focus; active at grid |
| Playing long-press | **`No`** | `requestSlotSwitch(LoopEnd)` |
| Multi-slot focus-only | **`No`** | queue as today |

**Rationale for playing + edit → `No` + queue:** Departure runs `commitEditSessionOnDepart` (flush pending geometry / close edit pass batch) **before** index changes; queued active switch gives time for edit-pass save side effects to complete before playback context moves.

**Alternatives:** Always collapse active in orchestrator — rejected; blocks audition/compare/layered workflows.

**Future:** `PlaybackSelectionPolicy { None, Immediate, NextGrid, LoopEnd }` without changing lifecycle model.

### D1b — Capture finalize desync

**Decision:** `finalizeCaptureAndSelectSlot` may intentionally leave `selectedSlotIndex` on the capture slot while `activeLoopIndex` moves — system path, not user orchestrator. Both indices persisted as-is.

### D2 — Canonical lifecycle as application pattern

**Decision:** Shared internals:

```cpp
EditManager::commitEditSessionOnDepart(track, departingSlot);
EditManager::reenterEditSessionForFocusChange(track, previousSlot);
```

Slot hooks (`beforeSelectedSlotChange`, `onSelectedSlotChanged`) and track hooks call these. Evolve to `beforeFocusChange` / `afterFocusChange` when layer/window focus ships.

### D3 — Display ownership

**Decision:** `DisplayManager::invalidateForSlotChange(track, previousSlot, newSlot)` called from TrackManager **arrival** phase. Invalidates caches + `markDisplayCachesStale`; **no** `display()`.

```
Slot change → invalidate caches → DisplayManager::update() redraws
```

### D4 — Selection invariant

**Principle:** Selection is observational; editing is transformational.

Slot change may rebind session RAM and invalidate caches. It must not mutate `Loop` pass data. `commitEditSessionOnDepart` may flush uncommitted edit actions to session store (depart policy).

### D5 — getSelectedLoop const preference

**Decision:** `TrackManager::getSelectedLoop(trackIndex)` + `const` overload. Read paths (display, LED layout) use `const`.

### D6 — SD footer extension

**Decision:** Footer order after `activeLoopIndex[NUM_TRACKS]`:

1. If extension present: `uint32_t kFooterSelectedSlotExtensionToken` (`0x534C4F54` / `'SLOT'`)
2. Then `selectedSlotIndex[NUM_TRACKS]`
3. Then existing `kGlobalUndoStackToken` + per-track undo stacks

**Legacy read:** After reading `activeLoopIndex[]`, peek next `uint32_t`. If `'SLOT'`, read `selectedSlotIndex[]`. If `kGlobalUndoStackToken`, treat as legacy footer — set `selectedSlotIndex[t] = activeLoopIndex[t]`.

**Revision load:** When bundled transport footer is read (`RevisionLoadReloadRamStage::ReadFooter` → `applyLoadedTransportFooter`), restore indices from footer (extension or legacy default). When building default revision transport without saved footer (`writeDefaultRevisionLoadTransportBody`), fall back to **first occupied slot per track** for both indices (same value for active and selected).

Boot load: direct index write without edit hooks.

### D7 — OpenSpec tracking

**Decision:** Separate change `slot-selection-focus`; **not** UIP spike. One cross-link row in UIP proposal only.

---

## Call flow

```
Caller (MidiButtonActions)
  → TrackManager::setSelectedSlotIndex(track, slot, syncPlayback)
      [Departure]  EditManager::beforeSelectedSlotChange → commitEditSessionOnDepart
      [Transition] slotStateMachine.setSelectedSlotIndex
                     if (syncPlayback == Yes) setActiveLoopIndex
      [Arrival]    EditManager::onSelectedSlotChanged → reenterEditSessionForFocusChange
                   DisplayManager::invalidateForSlotChange
                   forceLedUpdate
                   requestDeferredSaveState
  → (optional) requestSlotSwitch when SyncPlayback::No && playing
```

---

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Missed call site still uses `getActiveLoop()` for UI | Route edit/display through `getSelectedLoop`; grep gate in tasks |
| Repeated slot stress leaks session state | Native stress test slot 1↔2×5 |
| Footer schema break | Extension token + legacy default |
| Sync draw on focus path | Explicit design rule + code review |

---

## Migration Plan

1. Land orchestrator + EditManager dispatch + DisplayManager invalidate
2. Update `MidiButtonActions` caller matrix (preserve Loops.md semantics)
3. Footer write/read extension
4. Native tests + manual NOTE_EDIT / LOOP_EDIT matrix
5. Archive to `openspec/specs/` after verification

Rollback: revert orchestrator; legacy footers still load (selected defaults to active).

---

## Open Questions

- None blocking v1. Playing + edit uses `SyncPlayback::No` + queue (decided). Default param `Yes` (decided).
