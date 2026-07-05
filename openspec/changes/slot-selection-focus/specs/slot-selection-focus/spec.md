## ADDED Requirements

### Requirement: Canonical focus lifecycle for slot selection

Slot selection changes SHALL follow the three-phase lifecycle **Departure → Transition → Arrival**:

1. **Departure** — commit pending edit session work and release current bindings
2. **Transition** — update `selectedSlotIndex`; optionally sync `activeLoopIndex` per caller `SyncPlayback` policy
3. **Arrival** — rebind edit session for current `EditSessionType`, invalidate display caches, refresh LEDs, request deferred save of indices

v1 SHALL implement this via `TrackManager::setSelectedSlotIndex` orchestrating `EditManager` and `DisplayManager` hooks. Track selection SHALL reuse the same internal depart/arrive helpers.

Full departure → transition → arrival SHALL run only when the slot change is on the **currently selected track**. Slot index updates on other tracks SHALL update `selectedSlotIndex` only (transition) without edit/display arrival hooks until that track becomes selected.

#### Scenario: Slot change runs all three phases

- **WHEN** the user selects a different slot on the selected track
- **THEN** departure commits or releases the current edit session binding before the index changes
- **AND** transition updates `selectedSlotIndex`
- **AND** arrival rebinds the edit session and invalidates display caches for the previous and new slot

#### Scenario: Background track slot index only

- **WHEN** `setSelectedSlotIndex` is called for a track that is not the selected track
- **THEN** `selectedSlotIndex` is updated for that track
- **AND** edit session rebind and display cache invalidation do not run until that track is selected

#### Scenario: No-op when slot unchanged

- **WHEN** `setSelectedSlotIndex` is called with the current slot index
- **THEN** no departure, transition, or arrival side effects run

### Requirement: Session-type-agnostic edit rebind on slot change

On slot arrival, the system SHALL dispatch edit session rebind by `EditSessionType`:

| Type | Arrive behavior |
|------|-----------------|
| `Loop` | Reopen loop edit session for **selected** slot (baseline + fader feedback) |
| `Note` (active) | Rematerialise note edit session store from **selected** slot passes |
| `ControlChange` | Stub enter hook (no crash; display caches invalidated) |

#### Scenario: Loop edit slot switch refreshes display context

- **WHEN** `EditSessionType::Loop` is active and the user selects a different slot on the selected track
- **THEN** loop edit session baseline and fader feedback target the new selected slot
- **AND** the piano roll display resolves notes for the new selected slot on the next display update

#### Scenario: Display invalidates without active note edit session

- **WHEN** slot selection changes and no note edit session is active (`editSession.active == false`)
- **THEN** arrival still calls `DisplayManager::invalidateForSlotChange`
- **AND** the piano roll reflects the new selected slot on the next display update

#### Scenario: Note edit slot switch rematerialises session store

- **WHEN** `EditSessionType::Note` is active and the user selects a different slot
- **THEN** `NoteEditSession.store` is rematerialised from the new selected slot passes
- **AND** display note list matches the new slot content

#### Scenario: Control Change edit stub on slot change

- **WHEN** `EditSessionType::ControlChange` is active and the user selects a different slot on the selected track
- **THEN** the stub enter hook runs without error
- **AND** display caches are invalidated for the slot change

### Requirement: Display cache invalidation owned by DisplayManager

Slot arrival SHALL call `DisplayManager::invalidateForSlotChange(track, previousSlot, newSlot)` which SHALL:

- Invalidate live and note-edit display cache keys
- Mark visual caches stale on previous and new slot loops
- When transport is playing or overdubbing on the selected track, re-center the detailed piano-roll window on the playhead for the new slot (no synchronous `display()`)

Playhead tick for the **active playing slot** SHALL follow **`unified-interval-projection`** D25 — storage phase from **`projectionCycleStartTick`** when `displaySlot == activeLoopIndex`, not record-time **`startLoopTick`** alone.

It SHALL NOT call synchronous `display()` or block the focus-transition path. Redraw SHALL occur on the normal `DisplayManager::update()` scheduler tick.

#### Scenario: Invalidate without synchronous redraw

- **WHEN** slot selection changes
- **THEN** display cache keys are cleared and loop visual caches marked stale
- **AND** no synchronous frame render is triggered inside the selection handler

### Requirement: SyncPlayback is Phase 1 orchestrator playback policy

`TrackManager::setSelectedSlotIndex` SHALL accept `SyncPlayback::Yes | No` with **default `SyncPlayback::Yes`** when the caller omits the argument:

- **`Yes`** — after transition, set `activeLoopIndex` to the new slot when changed
- **`No`** — transition updates `selectedSlotIndex` only

Queued playback restart (NextGrid, LoopEnd) SHALL remain a separate caller policy via existing `requestSlotSwitch` and SHALL NOT be embedded in the orchestrator.

#### Scenario: Playing UI chains SyncPlayback No with requestSlotSwitch

- **WHEN** the user short-presses a non-selected filled slot while playing (including NOTE_EDIT or LOOP_EDIT)
- **THEN** departure commits pending edit work before the index changes
- **AND** `setSelectedSlotIndex(..., SyncPlayback::No)` updates UI focus immediately
- **AND** `requestSlotSwitch(..., NextGrid)` queues active switch and `queuedStartTick` at grid commit

#### Scenario: Default SyncPlayback Yes when omitted

- **WHEN** a caller invokes `setSelectedSlotIndex(track, slot)` without the third argument
- **THEN** `activeLoopIndex` is synchronised to the new slot when changed

### Requirement: getSelectedLoop read API

`TrackManager` SHALL expose `getSelectedLoop(trackIndex)` returning the `Loop` for `selectedSlotIndex`. A `const` overload SHALL be provided. Read-only callers SHOULD use the `const` overload.

#### Scenario: Display resolves selected slot loop

- **WHEN** the piano roll renders for the selected track
- **THEN** loop geometry and materialized notes are read from `getSelectedLoop(selectedTrackIndex)` or equivalent selected-slot index

### Requirement: Selection is observational

Slot selection changes SHALL NOT directly modify loop pass data, rewrite note storage, or transform musical content in `Loop` passes. Selection MAY rebind edit session RAM, invalidate caches, update UI indices, and request persistence of index metadata.

#### Scenario: Repeated slot toggling preserves pass counts

- **WHEN** the user alternates selected slot between slot 1 and slot 2 five times without editing
- **THEN** each slot's pass event counts remain identical to counts before the sequence

### Requirement: Independent persistence of selected and active slot indices

The SD transport footer SHALL persist per track:

- `activeLoopIndex` (playback/capture focus)
- `selectedSlotIndex` (UI/editor focus)

Load SHALL restore both independently. Legacy footers without `selectedSlotIndex` SHALL default `selectedSlotIndex[t] = activeLoopIndex[t]`.

When revision load reads a bundled transport footer, indices SHALL be restored from that footer (extension or legacy default). When revision transport is synthesised without a saved footer, both indices SHALL fall back to the first occupied slot per track (same slot for active and selected).

#### Scenario: Footer round-trip restores both indices

- **WHEN** workspace is saved with track 2 selected slot 3 and active slot 1
- **AND** the device reboots and loads the workspace
- **THEN** track 2 `selectedSlotIndex` is 3 and `activeLoopIndex` is 1

#### Scenario: Legacy footer defaults selected to active

- **WHEN** a pre-extension footer is loaded
- **THEN** `selectedSlotIndex[t]` is set to `activeLoopIndex[t]` for each track

#### Scenario: Revision load without footer uses first occupied slot

- **WHEN** revision import synthesises default transport without a saved footer
- **THEN** `activeLoopIndex` and `selectedSlotIndex` for each track are set to the first occupied slot in the revision directory
- **AND** both indices match for that track
