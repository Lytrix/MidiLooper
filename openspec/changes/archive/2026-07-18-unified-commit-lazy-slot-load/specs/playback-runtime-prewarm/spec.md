## MODIFIED Requirements

### Requirement: Playback runtime preallocated before transport

Per-slot **LoopPlaybackRuntime** objects and per-loop **playbackOrder** storage SHALL be allocated
during setup or state load prewarm — not on the first **playMidiEvents** call after transport start.

Prewarm at boot SHALL cover tracks/slots that are enabled for audible boot or already COMMITTED.
Prewarm SHALL NOT require every SD payload slot to be loaded before transport may start after
audible-first interactive ready.

#### Scenario: First play tick does not allocate runtime

- **WHEN** firmware completes setup (or successful **loadState**)
- **AND** prewarm has run for audible or COMMITTED slots that will play
- **AND** transport starts and **playMidiEvents** runs for the first time on those slots
- **THEN** no new **LoopPlaybackRuntime** heap allocation occurs on that tick

#### Scenario: Prewarm touches enabled slots only

- **WHEN** prewarm runs at boot with default slot 0 enabled
- **THEN** **playbackRuntime.slot(0)** is constructed for each track
- **AND** slots that are disabled and empty MAY skip prewarm

#### Scenario: Unloaded non-audible slots skip boot prewarm obligation

- **WHEN** audible-first boot leaves non-audible SD slots unloaded
- **THEN** boot prewarm is not required to construct playback runtime for those unloaded slots
- **AND** when such a slot later reaches COMMITTED, prewarm or equivalent allocation SHALL run before its first play tick
