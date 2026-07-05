# Loops (control surface)

**Loops row**: eight loop slots for the selected track (default mapping: Channel 16 notes 50-57).

Primary implementation paths:
- Mapping: [`src/Utils/MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp)
- Gesture actions: [`src/MidiButtonActions.cpp`](../../../src/MidiButtonActions.cpp) (`handleToggleRecordForSlot`, `beginSlotLayerHold`, `endSlotLayerHold`)
- Slot switching + playback commit: [`src/TrackManager.cpp`](../../../src/TrackManager.cpp)
- Pending switch logic: [`src/SlotStateMachine.cpp`](../../../src/SlotStateMachine.cpp)

## Short press

### While playing

- **Pressed slot has data, slot is selected**: toggle mute for that slot (track keeps running).
- **Pressed slot has data, slot is not selected**:
  - In multi-slot mode (more than one enabled slot): keep enabled set, optionally toggle mute if slot is enabled, and queue active-slot focus switch at next 16th.
  - In single-slot mode: queue switch to that slot at next 16th; when committed, enabled set can be replaced with that single slot.
  - On grid commit: that track's **`projectionCycleStartTick`** resets to the commit tick and **`queuedStartTick`** applies once (default = target slot's **`loopStartTick`**).
- **Pressed slot is empty**: use queued/immediate record flow (bar/phase quantized when configured).

### While not playing

- **Pressed slot is empty**: start recording.
- **Pressed slot has data**: toggle play/stop on that slot.

## Long press

- **Pressed slot is selected and has data**: clear that slot.
- **Pressed slot is not selected and has data**:
  - If track is playing: queue single-slot switch at **loop end**.
  - If track is not playing: select that slot and start it immediately.

## Double / Triple

- **Double**: slot undo.
- **Triple**: slot redo.

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
