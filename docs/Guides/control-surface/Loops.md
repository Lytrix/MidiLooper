# Loops (control surface)

**Applies to:** v3 (`dev`) — DROID 8×8 grid, loop row Channel 16 notes 50–57.

**Loops row**: eight loop slots for the selected track (default mapping: Channel 16 notes 50-57).

Primary implementation paths:
- Mapping: [`src/Utils/MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp)
- Gesture actions: [`src/MidiButtonActions.cpp`](../../../src/MidiButtonActions.cpp) (`handleToggleRecord`, `handleToggleRecordForSlot`, `beginSlotLayerHold`, `endSlotLayerHold`)
- Slot selection orchestrator: [`TrackManager::setSelectedSlotIndex`](../../../src/TrackManager.cpp) — preview slot (`selectedSlotIndex`); playing slot (`activeLoopIndex`) commits separately while transport runs
- **Playing / preview / pending:** preview = piano roll + edit (immediate); playing = audible MIDI + bar/16th LED phase; pending = queued launch target until commit
- Playback sync policy: default `SyncPlayback::Yes` when transport is stopped; while transport is running, preview updates immediately and **playing** slot commits at **loop boundary** for performance launch (`SlotQuantization::LoopEnd`)
- Pending switch logic: [`src/SlotStateMachine.cpp`](../../../src/SlotStateMachine.cpp)

## Button A (Record)

**Short press** on the main Record button uses the **selected slot** (preview focus), not track-level `TRACK_EMPTY`:

- **Selected slot has no published MIDI**: same arm / queue / punch-in rules as a **loop-row short press** on that slot (`handleToggleRecordForSlot`).
- **Selected slot has data, track playing**: live overdub (not a playback switch — use loop-row short press for that).
- **Selected slot has data, track stopped**: toggle play/stop.
- **Recording / overdubbing**: stop capture (unchanged).

Immediate record sets **`activeLoopIndex`** to the target slot before capture starts when preview and playing slots differ.

## Short press

### While playing

- **Pressed slot has data, slot is selected**: toggle mute for that slot (track keeps running).
- **Pressed slot has data, slot is not selected**:
  - Departure commits pending edit work (NOTE_EDIT / LOOP_EDIT) before the UI focus index changes.
  - While transport is running (`clockManager.shouldQuantizeRecordStart()`): `setSelectedSlotIndex(..., SyncPlayback::No)` updates **preview** immediately; **playing** slot switches at **loop boundary** via `requestSlotSwitch(LoopEnd)`.
  - Piano roll and edit commit follow preview immediately; bar/16th LEDs and phase grid stay on the **playing** slot until commit.
  - At LoopEnd commit the enabled set becomes **only the queued slot**. Previously playing slots stop. A leftover layered set does not keep sounding.
  - Adding more slots to the same queued start (hold the current playing slot, short-press others) is not this gesture.
  - On grid commit: **`projectionCycleStartTick`** resets and **`queuedStartTick`** applies once (target slot's **`loopStartTick`**).
  - When transport is not running: immediate `SyncPlayback::Yes` sync.
- **Pressed slot is empty**: use queued/immediate record flow (bar/phase quantized when configured).

### While not playing

- **Pressed slot is empty**: start recording. Enabled set becomes **only that slot** (a leftover layered set from a previous play does not come back after record-stop).
- **Pressed slot has data, LOOP_EDIT or NOTE_EDIT, different slot than selected**: change **selected** focus only (no play/stop toggle). Enabled set becomes **only that slot**.
- **Pressed slot has data** (otherwise): toggle play/stop on that slot; if global transport is stopped, it starts automatically so the clock advances. `toggleTransport()` may already start the track — play start is not double-toggled off. Enabled set becomes **only that slot**.
- Slot-switch and record **queues** are playing-only. Transport stop and track stop discard pending slot switch, pending record, pending multi-hold commit, and queued playback start. The committed enabled set is not a queue; selecting a slot while stopped replaces it.

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

Use hold **while playing** to build a pending enabled-slot set:

- Hold one or more slot buttons to mark target slots.
- Release the last held slot to queue commit.
- Commit occurs on next 16th boundary.
- At commit, enabled set is replaced by held selection, playback indices are realigned for enabled audible slots, and LEDs are refreshed.
- Hold is ignored while stopped, recording, or overdubbing. Transport/track stop discards an in-progress hold.

## Capture safety

If recording/overdubbing and you select another slot, capture is finalized first (`TrackManager::finalizeCaptureAndSelectSlot`) before switching target slot.

## vs Jams

Loop slots store per-track MIDI loop data and slot state (enabled/muted/active/selected). Jam-region behavior remains separate; see [`Jams.md`](Jams.md).
