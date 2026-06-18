## MODIFIED Requirements

### Requirement: Published events live in active takes

The system SHALL store committed loop MIDI as one or more **Active** **Takes** with
chunk-backed `LoopEventStore` references, ordered by `mergeSequence`. Live capture SHALL
use **Capture** on the loop until **commitTake** appends a **Take**.

#### Scenario: Playback reads takes without full flatten

- **WHEN** the transport plays a loop with committed takes
- **THEN** playback uses take/chunk indexed access (`eventAt`, `readStore`)
- **AND** does not flatten the full loop on each tick

#### Scenario: Record stop commits capture into a take

- **WHEN** recording stops with non-empty capture
- **THEN** capture is committed as a new Active take
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

### Requirement: Global undo records take commit

When a record or overdub stop commits a **Take**, the system SHALL push a
**TakeCommitted** entry on `GlobalUndoStack` (replacing epoch-published wording).

#### Scenario: Overdub stop pushes TakeCommitted

- **WHEN** overdub stops and capture commits to a new take
- **THEN** **TakeCommitted** is pushed for that take
- **AND** undo restores the prior take stack state
