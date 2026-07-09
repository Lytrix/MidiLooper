## ADDED Requirements

### Requirement: PlaybackWindow domain object

The system SHALL treat **PlaybackWindow** as the shared domain object defining the active musical subset over a **Loop** for playback, editing, and display projection.

A PlaybackWindow SHALL be defined by a tick range (`start` + `length`, or equivalent `TickInterval`).

**Slot** SHALL be a container holding a `LoopId` reference and persisted PlaybackWindow metadata (`loopStartTick`, `loopLengthTicks`).

Multiple slots MAY reference the same `LoopId` with different PlaybackWindow metadata.

#### Scenario: Slot metadata is persisted PlaybackWindow

- **WHEN** a slot has `loopStartTick` and `loopLengthTicks` set
- **THEN** those values define that slot's persisted PlaybackWindow over its referenced Loop
- **AND** launch and restart use that window for `queuePlaybackStartAtGrid`

### Requirement: Performance editing and management categories

Slot button gestures SHALL be classified into three categories:

| Category | Gestures | SHALL NOT require overlay |
|----------|----------|---------------------------|
| **Performance** | Short, double on loop slots (50–57) | Yes |
| **LoopTriggerSequence** | Long press on loop slots | Chain build / cue per UIP Phase 7 |
| **Editing** | Record (36), edit mode, encoder, faders | N/A — arm/record/overdub/undo/redo on Record only |
| **Management** | Triple, very long on loop slots | Triple opens overlay; very long deletes |

Loop slot buttons (50–57) SHALL NOT invoke arm, record, overdub, undo, or redo.

Record (36) SHALL own arm, record, overdub, undo, redo, and clear for the **selected** slot per existing [`MidiButtonConfig`](../../../src/Utils/MidiButtonConfig.cpp).

#### Scenario: Empty slot short press selects only

- **WHEN** the user short-presses an empty loop slot
- **THEN** that slot becomes selected for display and edit context
- **AND** no record arm or capture starts until the user presses Record (36)

#### Scenario: Record does not use slot double for undo

- **WHEN** the user double-presses Record (36) outside an overlay
- **THEN** track-scoped undo runs per existing record gesture map
- **AND** loop slot double-press enqueues restart or launch (not undo)

### Requirement: Quantised slot action queue

Each track SHALL maintain at most one pending **performance action** with an associated **quantisation rule**.

| Action | Typical enqueue | Quantisation |
|--------|-----------------|--------------|
| `LaunchSlot` | Short on non-selected filled slot while playing | `LoopEnd` |
| `LaunchSlot` | Double on non-selected filled slot while playing | `NextGrid` |
| `RestartSlot` | Double on selected filled slot while playing | `NextGrid` |
| `MuteSlot` | Short on selected slot when unmuted | `LoopEnd` |
| `UnmuteSlot` | Short on selected slot when muted | `LoopEnd` |

`TrackManager` SHALL own enqueue and commit. `MidiButtonActions` SHALL only request actions; it SHALL NOT commit performance side effects directly.

#### Scenario: Action commits at loop boundary

- **WHEN** a `LaunchSlot` action is queued while transport is playing
- **AND** the active loop reaches its wrap point (tick phase returns to loop start)
- **THEN** the launch side effects run exactly once
- **AND** the pending action is cleared

#### Scenario: Pending action shows LED feedback

- **WHEN** a performance action is queued
- **THEN** the target slot LED SHALL indicate pending state (pulse) until commit or cancel

#### Scenario: No duplicate pending action

- **WHEN** a new performance action is queued for a track that already has a pending action
- **THEN** the new action replaces the previous pending action
- **AND** LED feedback updates to the latest action

### Requirement: Short press on non-selected slot launches at loop boundary

While transport is playing and the pressed slot has loop data and is not the playing slot, the system SHALL:

1. Set **preview slot** immediately for display and LOOP_EDIT fader context (SHALL NOT move **playing slot** / `activeLoopIndex`)
2. Queue a `LaunchSlot` action for loop-boundary commit (or `NextGrid` for double)
3. Set the destination slot's active **PlaybackWindow** to that slot's persisted window (default: full loop)
4. Treat launch as a no-op when the destination slot is already the sole audible playing slot

At commit, the system SHALL set playing slot to the target, enable playback layer as required, apply `queuePlaybackStartAtGrid` with the target slot's `loopStartTick`, and `commitQueuedPlaybackStart` per unified-interval-projection D14.

Edit session depart/commit SHALL run **immediately** when preview slot is set (existing `beforeSelectedSlotChange` lifecycle). SHALL NOT be deferred to loop boundary.

#### Scenario: Verse to chorus launch with preview

- **WHEN** slot 0 is playing and the user short-presses slot 1 (filled)
- **THEN** the piano roll and LOOP_EDIT context show slot 1 immediately (preview)
- **AND** the display cursor is at slot 1's `loopStartTick` and flashes while preview ≠ playing
- **AND** bar/16th LEDs and phase grid remain on slot 0 until loop boundary commit
- **AND** slot 1 audio begins at the next loop boundary of the previously playing material

### Requirement: Short press on selected slot toggles mute at loop boundary

While transport is playing and the pressed slot is the selected slot with loop data, short press SHALL queue `MuteSlot` or `UnmuteSlot` (not an immediate toggle).

At commit, mute SHALL call `sendAllNotesOff` on the track output path before suppressing further MIDI from that slot.

At commit, unmute SHALL call `resetPlaybackStateForSlot` to re-align playback indices.

#### Scenario: Quantised mute silences hanging notes

- **WHEN** the user short-presses the selected playing slot to mute
- **AND** the loop boundary is reached
- **THEN** `slotMuted` becomes true for that slot
- **AND** all notes on the track output channel are silenced (CC 123 or equivalent)
- **AND** no further NoteOn events are sent from that slot until unmuted

#### Scenario: Unmute re-anchors slot playback

- **WHEN** a muted selected slot is short-pressed to unmute
- **AND** the loop boundary is reached
- **THEN** `slotMuted` becomes false
- **AND** `resetPlaybackStateForSlot` runs for that slot at the commit tick

### Requirement: Double press restart uses NextGrid

While transport is playing, double press on the **selected** filled slot SHALL queue `RestartSlot` with **`SlotQuantization::NextGrid`**.

Double press on a **non-selected** filled slot SHALL queue `LaunchSlot` with **`NextGrid`** (earlier commit than short press).

At commit, restart SHALL send all notes off, queue playback restart from the slot's `loopStartTick`, and reset playback indices.

#### Scenario: Restart aligns on next 16th

- **WHEN** the user double-presses the selected playing slot
- **AND** the next 16th grid boundary is reached
- **THEN** playback restarts from the slot loop startpoint
- **AND** heard MIDI aligns with display playhead per projection cycle rules

### Requirement: Long press starts LoopTriggerSequence chain workflow

Long press on loop slot buttons SHALL dispatch to the **`LoopTriggerSequence`** chain workflow defined in [`unified-interval-projection`](../unified-interval-projection/) Phase 7.

Long press SHALL NOT open the slot management overlay.

Long press SHALL NOT use legacy multi-hold enabled-set layering (`beginSlotLayerHold` / `endSlotSelectionHold` on loop slot buttons).

#### Scenario: Long A and short B enters build phase

- **GIVEN** the track is in Play, Loop Edit, or Note Edit
- **WHEN** the user long-presses slot A then short-presses slot B
- **THEN** `LoopTriggerSequence Edit Mode` begins the build phase
- **AND** idle timeout defaults to 2 bars per global config

#### Scenario: Long press during triggerSequencePlay cues sequence

- **GIVEN** `triggerSequencePlay` is active
- **WHEN** the user long-presses a loop slot
- **THEN** a different `LoopTriggerSequence` is cued
- **AND** playback switches after idle timeout while the current sequence continues until then

#### Scenario: Long press does not multi-hold layer

- **WHEN** the user long-presses loop slot buttons without entering LTS build phase
- **THEN** the legacy multi-hold enabled-set commit path does not run
- **AND** the slot management overlay does not open

### Requirement: Triple press opens slot management overlay

Triple press on a loop slot SHALL open the **slot management overlay** (shared with set-revision-persistence loop overlay).

Triple press SHALL NOT invoke redo or any capture action.

The overlay SHALL be the entry point for load, save, replace, clear, duplicate (future), rename (future), and export (future) for that slot scope.

#### Scenario: Triple press opens overlay without redo

- **WHEN** the user triple-presses loop slot 3
- **THEN** the slot management overlay opens for slot 3
- **AND** no global redo runs
- **AND** slot pass data remains until a management action is confirmed inside the overlay

### Requirement: Global undo on destructive slot operations

Very-long delete and overlay-confirmed replace or clear of slot data SHALL push an undo checkpoint on the **global undo stack** before mutation.

Undo restore SHALL use existing `handleUndo` / `TrackUndo` routing (same family as Record long-press clear).

#### Scenario: Delete slot is undoable

- **WHEN** the user holds loop slot 2 for the very-long delete threshold
- **THEN** a global undo checkpoint for clear-slot is pushed before data is cleared
- **AND** a subsequent Record double-press undo restores the prior slot content when applicable

### Requirement: Very long press deletes slot

Very long press (~4000ms) on a filled loop slot SHALL delete that slot's loop data after an intentional hold threshold.

Delete SHALL push an undo checkpoint before clearing slot data.

Delete SHALL silence audible notes on the track output path.

#### Scenario: Delete slot with undo checkpoint

- **WHEN** the user holds loop slot 2 for the very-long threshold
- **THEN** an undo checkpoint for clear-slot is pushed
- **AND** slot 2 loop data is cleared
- **AND** sounding notes on the track output channel are silenced

### Requirement: Transient PlaybackWindow from bar and step gestures

A **transient PlaybackWindow** SHALL be creatable via bar/16th step interaction (HOLD_ONE, HOLD_TWO, hold step + press step, bar boundaries) over the selected slot's Loop without mutating `Loop` pass data.

Implementation MAY use existing jam region fields (`jamStartTick`, `jamLength`, `jamPlaybackActive`) until migrated to `PlaybackWindow` on `Track`.

Release of the transient window SHALL return to the selected slot's persisted PlaybackWindow unless the user persists the window via destination-slot workflows.

#### Scenario: Hold two steps creates transient window

- **WHEN** the user holds two 16th step buttons in LOOP_EDIT or NOTE_EDIT timeline context
- **THEN** playback loops inside the selected tick window only
- **AND** underlying `Loop` pass data is unchanged

#### Scenario: Transient window visual feedback

- **WHEN** a transient PlaybackWindow is active
- **THEN** the UI SHALL indicate the restricted region (step LEDs and/or OLED braces)
- **AND** the user can distinguish transient window from full-slot playback

### Requirement: Persist PlaybackWindow via destination slots

The system SHALL support persisting a transient PlaybackWindow to a destination slot:

| Destination | Gesture | Action |
|-------------|---------|--------|
| Empty slot | Short press while transient window active | Persist window to slot; select slot |
| Filled slot | Triple → overlay → replace confirm | Replace with **global undo** checkpoint |

#### Scenario: Empty slot short persists window

- **WHEN** a transient PlaybackWindow is active
- **AND** the user short-presses an empty loop slot
- **THEN** the window is stored as that slot's persisted PlaybackWindow metadata
- **AND** the new slot becomes selected

#### Scenario: Filled slot replace uses overlay and undo

- **WHEN** a transient PlaybackWindow is active
- **AND** the user triple-presses a filled slot and confirms replace in the overlay
- **THEN** a global undo checkpoint is pushed before overwrite
- **AND** the destination slot receives the window as persisted metadata

### Requirement: LOOP_EDIT PlaybackWindow metadata change resyncs playback

When PlaybackWindow metadata (`loopStartTick` or loop length) changes on the **selected** slot during `TRACK_PLAYING` or `TRACK_OVERDUBBING`, the system SHALL:

1. Silence audible notes on the track output path
2. Queue playback restart at the new `loopStartTick` using `queuePlaybackStartAtGrid`
3. Commit at the next loop boundary when transport is running, or immediately when stopped
4. Re-anchor `projectionCycleStartTick` and event indices so heard MIDI matches display playhead

#### Scenario: Loop start fader move while playing

- **WHEN** the user moves the loop start fader in LOOP_EDIT during playback
- **AND** `loopStartTick` changes
- **THEN** sounding notes are silenced
- **AND** after commit, playback and display playhead agree on the new PlaybackWindow startpoint

## REMOVED Requirements

### Requirement: Loop slot double press is undo outside overlay

**Reason:** Double press on slot = restart (selected) or launch (other) at `NextGrid`.

**Migration:** Undo on Record double-press only.

### Requirement: Loop slot triple press redo

**Reason:** Triple press = slot management overlay; redo on Record triple-press.

**Migration:** Update [`loop-slot-buttons`](../set-revision-persistence/specs/loop-slot-buttons/spec.md) and HITL scripts.

### Requirement: Loop slot multi-hold layering removed

**Reason:** Long press reserved for `LoopTriggerSequence` chain workflow (UIP Phase 7).

**Migration:** Remove `beginSlotLayerHold` / multi-slot commit on loop slot buttons in `MidiButtonManager` and `MidiButtonActions`. Enabled-set layering is not replaced on slot buttons in v1.

### Requirement: Loop slot long press immediate clear

**Reason:** Long press routes to `LoopTriggerSequence` chain; clear via overlay or Record long-press.

**Migration:** Remove `CLEAR_TRACK_FOR_SLOT` immediate clear path in `MidiButtonActions`.

### Requirement: Preview display cursor while launch pending

When preview slot differs from playing slot, the system SHALL:

- Render the preview slot's loop on the piano roll
- Position the display cursor at the preview slot's `loopStartTick` (or queued playback start tick)
- Flash the cursor until playing slot equals preview slot at performance commit

#### Scenario: Flashing cursor during queued launch

- **WHEN** the user queues launch to a non-playing filled slot while transport is running
- **THEN** the piano roll shows the preview slot's loop content
- **AND** the cursor at `loopStartTick` flashes
- **WHEN** the launch commits at loop boundary
- **THEN** the cursor stops flashing and follows normal playback projection on the playing slot

### Requirement: Bar and 16th LEDs follow playing slot during preview

While preview slot differs from playing slot, bar-step and 16th-grid LED phase SHALL derive from the **playing** slot's loop phase, not the preview slot.

#### Scenario: Jam on 16ths while previewing next loop

- **WHEN** slot 0 is playing and slot 1 is previewed for launch
- **THEN** bar/16th performance LEDs remain aligned to slot 0's phase
- **AND** the user can use bar/16th buttons against the still-playing loop until launch commit
