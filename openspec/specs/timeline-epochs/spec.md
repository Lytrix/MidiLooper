## Purpose

Published loop MIDI is stored in **epochs** on each `Loop`. Record and overdub use a
mutable **capture** layer that seals into a new epoch on stop. This spec documents
behavior shipped through milestone M7 on `refactor/timeline-data-model`.

## Requirements

### Requirement: Published events live in active epochs

The system SHALL store committed loop MIDI as one or more **Active** epochs with
chunk-backed `LoopEventStore` references, ordered by `mergeSequence`.

#### Scenario: Playback reads epochs without full flatten

- **WHEN** the transport plays a loop with published epochs
- **THEN** playback uses epoch/chunk indexed access (`eventAt`, `readStore`)
- **AND** does not flatten the full loop on each tick

#### Scenario: Record stop seals capture into an epoch

- **WHEN** recording stops with non-empty capture
- **THEN** capture data is sealed and published as a new Active epoch
- **AND** capture buffer is cleared for the next session

### Requirement: Overdub capture dedupes within active capture only

During overdub, duplicate capture events SHALL be rejected only when an equivalent
event already exists in the **active capture store** for the same tick window.

#### Scenario: Second loop pass does not block overdub against committed record

- **WHEN** overdub runs across multiple loop cycles with repeating phase content
- **THEN** new overdub note-ons at the same loop phase are stored unless already
  captured in the current overdub session's capture store

### Requirement: Stop path uses wrap-window finalize only

Record and overdub stop SHALL run `finalizeLoopAtStop` / `LoopStopFinalize` on the
wrap window only, not full-loop `validateAndCleanupMidiEvents`.

#### Scenario: Full validate is deferred

- **WHEN** record or overdub stops
- **THEN** stop-path validation is limited to the finalize wrap window
- **AND** full validation runs only via idle maintenance or SD load

### Requirement: Undo snapshots are deep-copied on restore

Undo SHALL push O(1) shared store references and MUST `cloneShared()` on restore.

#### Scenario: Overdub undo before clear-slot undo

- **WHEN** the user triggers undo
- **THEN** overdub undo is attempted before clear-slot undo
- **AND** restored state does not share live chunk IDs with the snapshot stack

### Requirement: Note edit uses materialized flat bridge until M8

Until M8 ships, note-edit paths MUST be allowed to materialize epochs into `editFlat_`
via `Loop::midiEvents()` / `mutEditStore()`. After M8, this bridge MUST be removed or
limited to explicit migration tooling.

#### Scenario: Flat edit invalidates caches

- **WHEN** note edits mutate the flat edit store
- **THEN** epoch/visual caches are invalidated so playback and display stay consistent
