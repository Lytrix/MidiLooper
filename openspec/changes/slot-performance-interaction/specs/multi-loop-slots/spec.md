## ADDED Requirements

### Requirement: Quantised performance actions on loop slots

Each track's eight loop slots SHALL support quantised **launch**, **restart**, **mute**, and **unmute** performance actions committed at the **loop boundary**, in addition to existing quantized switching semantics.

Performance actions SHALL NOT require opening a management overlay.

#### Scenario: Launch does not mutate pass data

- **WHEN** a `LaunchSlot` action commits
- **THEN** only playback routing, enabled/muted flags, PlaybackWindow context, and projection anchors change
- **AND** `Loop` pass data in the target slot is unchanged

### Requirement: Silence on slot audibility loss

Whenever a slot stops contributing audible MIDI because of mute, disable, delete, or replacement launch, the system SHALL send all notes off on the track output MIDI channel before suppressing further NoteOn from that slot.

#### Scenario: Mute does not leave hanging notes

- **WHEN** a slot is muted at loop boundary commit
- **THEN** no note started by that slot's playback remains sounding on the track output channel

#### Scenario: Clear slot silences output

- **WHEN** a slot is cleared or deleted through management or very-long hold
- **THEN** all notes on the track output channel are silenced as part of the clear transition

### Requirement: Slot stores Loop reference and PlaybackWindow metadata

Each slot SHALL hold a `LoopId` reference to pooled `Loop` storage and persisted **PlaybackWindow** metadata (`loopStartTick`, `loopLengthTicks`).

Slots MAY reference the same `LoopId` with different PlaybackWindow metadata.

Transient bar/step windows SHALL NOT create new `recordPass` or `overdubPass` rows until jam recording (D13) is implemented.

#### Scenario: Transient window does not create pass rows

- **WHEN** a transient PlaybackWindow is active via bar/step gestures
- **THEN** no new `recordPass` or `overdubPass` rows are created solely by entering the window
- **AND** record/overdub still target the active capture path on the selected slot when the user presses Record

### Requirement: Selected slot remains edit and record target

Regardless of performance action queue state, **selected slot index** SHALL remain the target for:

- Display and encoder/fader context
- NOTE_EDIT and LOOP_EDIT sessions
- Record button arm/record/overdub when scoped to the selected track
- Active **PlaybackWindow** context (persisted or transient)

Performance commits at loop boundary SHALL NOT change `selectedSlotIndex` except when the enqueued action explicitly selects a different slot (launch).

#### Scenario: Launch updates selection before boundary

- **WHEN** the user short-presses a non-selected slot to launch
- **THEN** `selectedSlotIndex` updates immediately for UI and edit context
- **AND** active playback may remain on the prior slot until loop boundary commit

### Requirement: Engine merge cache naming

The real-time merged MIDI event cache SHALL be named **`PlaybackMergedMidiEvents`** and SHALL NOT share the domain type name **PlaybackWindow**.

`Track::invalidatePlaybackMergedMidiEvents` SHALL invalidate the merge cache (rename from `invalidatePlaybackWindow`).

#### Scenario: Domain PlaybackWindow is tick geometry after rename

- **WHEN** the merge cache type is renamed to `PlaybackMergedMidiEvents`
- **THEN** domain `PlaybackWindow` may be introduced for `{start, length}` tick geometry
- **AND** no identifier named `PlaybackWindow` refers to merged MIDI storage
