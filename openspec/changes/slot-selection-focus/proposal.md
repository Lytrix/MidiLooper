## Why

Loop slot switching today splits **UI focus** (`selectedSlotIndex`) from **playback/capture focus** (`activeLoopIndex`) without a single orchestrator. NOTE_EDIT rematerialises on slot change; LOOP_EDIT and future session types do not share one path — the piano roll can show stale MIDI. `selectedSlotIndex` is not persisted independently in the SD footer. This change introduces the canonical **Departure → Transition → Arrival** focus lifecycle for slot selection (extensible to track, editor, and window focus), parallel to UIP — it does not block UIP Phase 5 HITL.

Brownfield: [`docs/Plans/slot_selection_orchestration_refinement.md`](../../docs/Plans/slot_selection_orchestration_refinement.md), [`multi-loop-slots`](../../specs/multi-loop-slots/spec.md), UIP Phase 4 `queuedStartTick` ([`unified-interval-projection`](../unified-interval-projection/proposal.md)).

## What Changes

- **`TrackManager::setSelectedSlotIndex(track, slot, SyncPlayback)`** — orchestrates Departure → Transition → Arrival; default **`SyncPlayback::Yes`**; playing UI paths explicitly pass **`No`** and chain `requestSlotSwitch`
- **`TrackManager::getSelectedLoop(trackIndex)`** — slot-level read API (`const` overload preferred for read paths)
- **`EditManager`** — shared `commitEditSessionOnDepart` + `reenterEditSessionForFocusChange` dispatching Loop / Note / ControlChange (stub); slot and track hooks become thin wrappers
- **`DisplayManager::invalidateForSlotChange`** — cache invalidation only; redraw via normal `update()` loop (no synchronous `display()` on focus path)
- **SD footer extension** — persist `selectedSlotIndex[NUM_TRACKS]` alongside `activeLoopIndex[]`; backward-compatible read
- **Caller playback policy** — preserve existing `requestSlotSwitch` + `queuedStartTick` (NextGrid / LoopEnd) chained after `SyncPlayback::No` selection; document future `PlaybackSelectionPolicy` evolution

## Capabilities

### New Capabilities

- **`slot-selection-focus`**: Focus lifecycle for slot selection; index invariants; session-type-agnostic edit rebind; display cache invalidation; independent persistence of selected vs active slot indices

### Modified Capabilities

- **`multi-loop-slots`**: Clarify **selected** (UI/editor) vs **active** (playback/capture) slot indices; both persisted; selection is observational (does not mutate pass data)

## Impact

- **Primary files:** `TrackManager.cpp/h`, `EditManager.cpp/h`, `LoopEditManager.cpp/h`, `DisplayManager.cpp/h`, `MidiButtonActions.cpp`, `StorageManager/WorkspaceSave.cpp`, `StorageManager.cpp`
- **Tests:** new `test_slot_switch_edit_sessions` + repeated slot stress; footer round-trip
- **Docs:** `docs/Plans/slot_selection_orchestration_refinement.md`, `CURRENT_WORK.md` (parallel track)
- **Dependencies:** UIP Phase 4 `queuedStartTick` / `requestSlotSwitch` (consume only — no UIP engine changes)
- **Non-blocking:** UIP Phase 5.5 HITL; `edit-session-action-geometry` overlap pipeline

## Relationship to Active Work

| Change | Relationship |
|--------|--------------|
| `unified-interval-projection` | **Parallel** — uses Phase 4 queued start; not in UIP scope; does not gate Phase 5 HITL |
| `edit-session-action-geometry` | Orthogonal — not blocked or unblocked by this change (UIP remains gate) |
| `set-revision-persistence` | Footer extension follows same deferred-save pattern |

## Non-Goals

- Interval projection engine / `IntervalProjection` changes
- Full ControlChange edit implementation (stub dispatch only)
- Generic `beforeFocusChange` / `afterFocusChange` API (document evolution path in design)
- Removing multi-slot layered playback
- EEPROM / `settings.json`
