## ADDED Requirements

### Requirement: Open notes allowed during capture

Live **Capture** store MAY contain NoteOn events without a paired NoteOff until stop. This does not violate canonical invariants 1–2 until stop commit.

#### Scenario: Capture buffer open note

- **WHEN** a note is held open during overdub before loop wrap
- **THEN** capture buffer MAY contain NoteOn without NoteOff
- **AND** invariants 1–2 apply after stop normalize and pass commit

### Requirement: Capture stop commit normalizes before readers

After record or overdub stop, capture pass events SHALL be normalized at the **stop commit boundary** via `normalizeWindow` before pass commit and before materialized readers observe the pass.

`LoopStopFinalize` SHALL stop emitting wrap-pair storage and synthetic `loopLength - 1` offs as the **sole** length authority where linear off can be computed at stop. Remaining legacy shapes in the capture buffer SHALL be converted by `normalizeWindow` at stop commit (not on load).

Open-tail linear off at stop SHALL use `openTailCloseTick` (stop playhead tick): derived length from `NoteOn.tick` to close tick inclusive of wrap, then `NoteOff.tick = NoteOn.tick + derivedLength`.

Record and overdub passes SHALL use the same normalize-at-stop rules.

#### Scenario: Wrapped capture normalizes at stop

- **WHEN** overdub captures `NoteOn@1400` with head `NoteOff@50`, `loopLength = 1536`, and recording stops
- **THEN** `normalizeWindow` runs on the wrap window at stop commit
- **AND** committed pass has `NoteOff@1586` (single linear span)

#### Scenario: Open tail at stop receives linear off

- **WHEN** overdub stops with `NoteOn` only at tail tick `24500` and `openTailCloseTick = 24550` on a `loopLength = 24576` loop
- **THEN** normalize assigns linear `NoteOff` from stop-derived length
- **AND** committed pass does not rely on synth `loopLength - 1` as sole authority

#### Scenario: Pass commit visibility

- **WHEN** `commitCapturePass` completes
- **THEN** committed pass chunk data is canonical before `LoopPasses::materialize` overlay

### Requirement: Same-pitch LIFO with linear offs

When multiple notes share pitch and channel and linear `NoteOff.tick` values exceed `loopLength`, playback order and pair resolution SHALL remain correct (LIFO at head, tail-on pairing via projection).

#### Scenario: Two linear spans same pitch

- **WHEN** canonical storage has two closed spans same pitch/channel with linear offs beyond `loopLength`
- **THEN** `validateLoopEvents` passes `NoteIdPairing`
- **AND** playback order index fires offs at correct wrapped phases

### Requirement: Edit pass overlay preserves canonical form at commit

When edit passes are applied to materialize loop MIDI, normalize SHALL run at the **edit pass commit boundary** when geometry fields change stored ticks.

#### Scenario: saveNoteEditPass boundary

- **WHEN** `saveNoteEditPass` commits note geometry changes
- **THEN** normalize runs at that boundary before undo routing or materialized cache readers observe the change
