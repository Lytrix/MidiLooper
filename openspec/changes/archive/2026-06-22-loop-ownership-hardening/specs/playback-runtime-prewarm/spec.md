## ADDED Requirements

### Requirement: Playback runtime preallocated before transport

Per-slot **LoopPlaybackRuntime** objects and per-loop **playbackOrder** storage SHALL be allocated
during setup or state load prewarm — not on the first **playMidiEvents** call after transport start.

#### Scenario: First play tick does not allocate runtime

- **WHEN** firmware completes setup (or successful **loadState**)
- **AND** prewarm has run for tracks with enabled slots or published passes
- **AND** transport starts and **playMidiEvents** runs for the first time
- **THEN** no new **LoopPlaybackRuntime** heap allocation occurs on that tick

#### Scenario: Prewarm touches enabled slots only

- **WHEN** prewarm runs at boot with default slot 0 enabled
- **THEN** **playbackRuntime.slot(0)** is constructed for each track
- **AND** slots that are disabled and empty MAY skip prewarm
