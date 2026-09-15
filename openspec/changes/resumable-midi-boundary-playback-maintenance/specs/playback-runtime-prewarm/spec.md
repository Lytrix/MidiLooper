## MODIFIED Requirements

### Requirement: Playback runtime preallocated before transport

Per-slot **LoopPlaybackRuntime** storage SHALL be allocated during setup or
state-load prewarm — not on the first **playMidiEvents** call after transport
start. That prewarm SHALL reserve fixed external-memory capacity for three
long-loop playback views (current, complete next successor, and a separate
successor-after-next build view), two immutable source-change bridge buffers
(selected and candidate) sized through current/ready coverage, cycle-qualified
emission cursor, ledger-boundary summary, and generation-matched capture-source
admission staging. The staging SHALL include fixed `CaptureNoteReservation` and
`StopAdmissionContract` credits sufficient to build a frozen candidate before
every accepted active canonical capture-pass commit. The derived playback order
belongs to those runtime views; `Loop` SHALL retain canonical pass content,
revision, persistence, undo/redo, and invalidation ownership, but SHALL NOT
provide separately mutable per-loop `playbackOrder` storage to the active clock
path.

Prewarm at boot SHALL cover tracks/slots that are enabled for boot playback or
already COMMITTED. Prewarm SHALL NOT require every SD payload slot to be loaded
before transport may start after audible-first interactive ready.

#### Scenario: First play tick does not allocate runtime

- **WHEN** firmware completes setup (or successful **loadState**)
- **AND** prewarm has run for audible or COMMITTED slots that will play
- **AND** transport starts and **playMidiEvents** runs for the first time on
  those slots
- **THEN** no new **LoopPlaybackRuntime**, playback-view, bridge, or derived
  order heap allocation occurs on that tick
- **AND** no cycle cursor, ledger-boundary summary, or pending audible-source
  admission storage is allocated on that tick

#### Scenario: Prewarm reserves active long-loop continuity

- **WHEN** a prewarmed audible slot can use windowed long-loop playback
- **THEN** its runtime has fixed capacity for current coverage, its complete
  next contiguous successor, and a separate build interval after that
  successor, including wrap
- **AND** it has independent fixed capacity for both selected/candidate bridges
  across the full current/ready horizon and the source-change successor using
  the build view, including an idle retired-current view whenever the next
  successor remains pending at a handoff and its successor-after-next build
  only after that successor becomes ready
- **AND** an active capture pass uses source-admission staging that can freeze a
  generation-matched candidate before its first affected audible instance;
  preparation work may continue in later resumable turns and its length does
  not reject the pass
- **AND** each accepted NoteOn has reserved capacity for its matching closure
  and the continuous deferred-stop service

#### Scenario: Prewarm touches enabled slots only

- **WHEN** prewarm runs at boot with default slot 0 enabled
- **THEN** **playbackRuntime.slot(0)** is constructed for each track
- **AND** slots that are disabled and empty MAY skip prewarm

#### Scenario: Unloaded non-audible slots skip boot prewarm obligation

- **WHEN** audible-first boot leaves non-audible SD slots unloaded
- **THEN** boot prewarm is not required to construct playback runtime for those
  unloaded slots
- **AND** when such a slot later reaches COMMITTED, prewarm or equivalent
  allocation SHALL run before its first play tick


#### Scenario: First recording reserves its return window

- **WHEN** first recording starts on an admitted slot
- **THEN** prewarmed runtime storage can maintain bar-1 return coverage alongside rolling later-bar preparation
- **AND** incoming accepted recording events prepare that coverage before the final-bar boundary
- **AND** first-record launch does not wait for a snapshot or allocate playback storage at that boundary
