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

### Requirement: Slot loopId resolves without silent alias

Resolving a **Slot** to its **Loop** SHALL NOT fall back to pool index 0 when **loopId** is unknown
or invalid. Callers SHALL use the slot's pool index when **loopId** is **kInvalidLoopId** or fails
validation.

#### Scenario: Unknown loopId uses pool index

- **WHEN** **slots_[s].loopId** is not found in **LoopPool**
- **THEN** **loopForSlot(s)** returns **loopPool_.at(s)**
- **AND** no other slot's **Loop** is returned

#### Scenario: Valid loopId uses findById

- **WHEN** **slots_[s].loopId** equals pool index id for slot s (1:1 stable ids)
- **THEN** **loopForSlot(s)** returns the **Loop** at that pool index

### Requirement: SD load repairs invalid slotLoopId

When loading v4 state, if a persisted **slotLoopId** is outside `0 .. MAX_LOOPS_PER_TRACK-1`, the
loader SHALL rewrite it to the pool index for that slot and continue load.

#### Scenario: Corrupt slotLoopId repaired

- **WHEN** SD contains **slotLoopId** = 0xFFFFFFFF for track T slot S
- **THEN** after load **slots_[S].loopId** = S (stable 1:1 id)
- **AND** load completes successfully
- **AND** a warning is logged

### Requirement: Slot select may request hydration

Selecting or focusing a loop slot that still needs SD load SHALL request slot hydration so the slot can reach COMMITTED. Interactive ready at boot SHALL NOT require every slot on every track to be COMMITTED.

#### Scenario: Focus unloaded slot requests load

- **WHEN** the user focuses a slot that `needsSlotLoad` reports as needing SD
- **AND** transport is idle (or the request is queued per lazy-slot-hydration MVP rules)
- **THEN** deferred or sync restore is requested for that track/slot
- **AND** after Commit the slot has committed passes available for playback and display

### Requirement: Boot interactive ready is audible-scoped

`bootInteractiveReady` (or its successor) SHALL become true when the boot playback set is COMMITTED and boot load work for that set is complete — not when the entire SD payload set is COMMITTED.

#### Scenario: Ready with unloaded non-audible slots

- **WHEN** audible slots are COMMITTED after boot
- **AND** other SD payload slots remain unloaded
- **THEN** boot interactive ready is true
- **AND** finish-boot USB / piano-roll entry MAY proceed
