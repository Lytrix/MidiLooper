# Loops (control surface)

**Loops row**: eight loop slots for the selected track (default mapping: Channel 16 notes 50-57).

Primary implementation paths:
- Mapping: [`src/Utils/MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp)
- Gesture actions: [`src/MidiButtonActions.cpp`](../../../src/MidiButtonActions.cpp) (`handleToggleRecordForSlot`, `beginSlotLayerHold`, `endSlotLayerHold`)
- Slot selection orchestrator: [`TrackManager::setSelectedSlotIndex`](../../../src/TrackManager.cpp) — preview slot (`selectedSlotIndex`); playing slot (`activeLoopIndex`) commits separately while transport runs
- **Playing / preview / pending:** preview = piano roll + edit (immediate); playing = audible MIDI + bar/16th LED phase; pending = queued launch target until commit
- Playback sync policy: default `SyncPlayback::Yes` when transport is stopped; while transport is running, preview updates immediately and **playing** slot commits at **loop boundary** for performance launch (`SlotQuantization::LoopEnd`)
- Pending switch logic: [`src/SlotStateMachine.cpp`](../../../src/SlotStateMachine.cpp)

## Short press

### While playing

- **Pressed slot has data, slot is selected**: toggle mute for that slot (track keeps running).
- **Pressed slot has data, slot is not selected**:
  - Departure commits pending edit work (NOTE_EDIT / LOOP_EDIT) before the UI focus index changes.
  - While transport is running (`clockManager.shouldQuantizeRecordStart()`): `setSelectedSlotIndex(..., SyncPlayback::No)` updates **preview** immediately; **playing** slot switches at **loop boundary** via `requestSlotSwitch(LoopEnd)`.
  - Piano roll and edit commit follow preview immediately; bar/16th LEDs and phase grid stay on the **playing** slot until commit.
  - In multi-slot mode: keep the enabled set; queue playing-slot switch at loop end.
  - In **LOOP_EDIT** or **NOTE_EDIT**: queue active switch with **single-slot** enabled set replacement so only the selected loop is audible for comparison.
  - In single-slot mode (non-edit): queue switch at the grid; when committed, enabled set can be replaced with that single slot.
  - On grid commit: **`projectionCycleStartTick`** resets and **`queuedStartTick`** applies once (target slot's **`loopStartTick`**).
  - When transport is not running: immediate `SyncPlayback::Yes` sync.
- **Pressed slot is empty**: use queued/immediate record flow (bar/phase quantized when configured).

### While not playing

- **Pressed slot is empty**: start recording.
- **Pressed slot has data, LOOP_EDIT or NOTE_EDIT, different slot than selected**: change **selected** focus only (no play/stop toggle).
- **Pressed slot has data** (otherwise): toggle play/stop on that slot; if global transport is stopped, it starts automatically so the clock advances. `toggleTransport()` may already start the track — play start is not double-toggled off.

## Long press

- **Pressed slot is selected and has data**: clear that slot.
- **Pressed slot is not selected and has data**:
  - If track is playing and transport is running: queue single-slot switch at **loop end**.
  - If track is playing but transport is not running: switch immediately.
  - If track is not playing: select that slot and start it immediately.

## Double / Triple

- **Double**: undo for the **selected loop** (applies only when the global undo stack tip matches that loop).
- **Triple**: redo for the **selected loop** (same gating).

Sidebar **`U:nn`** shows applied pass-undo depth for the **selected loop**, not the whole track. During note edit, **`E:nn`** shows session undo instead.

## Hold (multi-slot selection)

Use hold to build a pending enabled-slot set:

- Hold one or more slot buttons to mark target slots.
- Release the last held slot to queue commit.
- Commit occurs on next 16th boundary.
- At commit, enabled set is replaced by held selection, playback indices are realigned for enabled audible slots, and LEDs are refreshed.

## Capture safety

If recording/overdubbing and you select another slot, capture is finalized first (`TrackManager::finalizeCaptureAndSelectSlot`) before switching target slot.

## vs Jams

Loop slots store per-track MIDI loop data and slot state (enabled/muted/active/selected). Jam-region behavior remains separate; see [`Jams.md`](Jams.md).
