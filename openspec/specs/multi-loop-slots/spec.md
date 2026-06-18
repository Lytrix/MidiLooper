## Purpose

Each track exposes up to **8 loop slots** (`MAX_LOOPS_PER_TRACK`). Slots support
independent record, overdub, clear, mute, quantized switching, and per-slot undo.
Jam **playback** state exists; jam **capture** into a new slot is not implemented
(see change `jam-recording`).

## Requirements

### Requirement: Eight slots per track

The system SHALL provide `MAX_LOOPS_PER_TRACK` (8) loop slots per track, each backed
by a pooled `Loop` timeline (`LoopPool`).

#### Scenario: Slot zero holds migrated content

- **WHEN** legacy save format is loaded
- **THEN** prior single-loop content appears in slot 0 and other slots are empty

### Requirement: Per-slot record and overdub lifecycle

Each slot SHALL support the same record/overdub/clear semantics as the historical
single-loop track model, scoped to that slot's `Loop`.

#### Scenario: Active slot drives playback and capture target

- **WHEN** a slot is selected as active
- **THEN** playback and default record/overdub target that slot's loop data

### Requirement: Output channel is authoritative on Track

MIDI output routing for a track SHALL use `Track::midiChannel` unless a future
per-loop override is explicitly added.

#### Scenario: All slots on a track share channel strip

- **WHEN** the user changes a track's MIDI channel
- **THEN** all slots on that track follow the track channel for output

### Requirement: Jam playback state is separate from loop storage

Jam regions (`jamStartTick`, `jamLength`, `jamTick`, `jamPlaybackActive`) SHALL
control performance playback view and timing without implying jam capture is recorded.

#### Scenario: Bar and 16th buttons adjust jam window

- **WHEN** jam playback is active and bar/16th controls are used
- **THEN** jam phase and display follow jam tick semantics per `jam-bar-step-phases.md`
- **AND** no new slot capture is created until jam recording (D13) is implemented
