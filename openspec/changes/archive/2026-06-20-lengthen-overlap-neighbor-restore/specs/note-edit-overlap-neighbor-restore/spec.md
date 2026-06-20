## ADDED Requirements

### Requirement: Contained neighbor notes preserve baseline geometry

While a note edit move session is active, any **neighbor note** **completely contained** by the moving note span SHALL have its `{pitch, startTick, endTick}` recorded in the session baseline before its MIDI pair is temporarily removed from the edit store.

#### Scenario: Same-pitch inner neighbor hidden on lengthened move-over

- **WHEN** a lengthened moving note is positioned such that a same-pitch inner neighbor is fully inside the mover span
- **THEN** the firmware SHALL store the neighbor geometry in the session baseline
- **AND** SHALL temporarily remove the neighbor's note-on/note-off pair from the active edit store (**hidden** state)
- **AND** SHALL NOT mutate the committed Record Take

#### Scenario: Neighbor gate matches record fixture after restore

- **WHEN** the mover uncovers a previously hidden inner neighbor or restores it after pitch lane change
- **THEN** the restored neighbor SHALL match the session baseline gate length within edit tick tolerance
- **AND** SHALL NOT stretch to loop end unless explicitly length-edited

### Requirement: Pitch lane change restores non-conflicting neighbors without losing inner-span tracking

When the moving note changes pitch during an active move session, neighbor notes whose pitch **no longer conflicts** with the mover SHALL become **visible** again without losing **inner** relationship tracking for subsequent position moves.

#### Scenario: P0 restored on mover pitch 60 to 67

- **WHEN** P0 (pitch 60) was hidden under mover pitch 60
- **AND** the mover pitch changes to 67 without fader-1 reselect
- **THEN** P0 (pitch 60) SHALL reappear in the edit store at its baseline ticks (**visible**)
- **AND** the session SHALL retain that P0 is still an **inner** neighbor under the mover span until uncovered by position

#### Scenario: Inner same-target-pitch neighbor not merged

- **WHEN** an inner neighbor at the target pitch lies under the mover's session span (e.g. A@67 under lengthened M0)
- **AND** the mover changes pitch to that target pitch
- **THEN** the firmware SHALL NOT adjacent-merge the inner neighbor into the mover span
- **AND** SHALL preserve the inner neighbor as a separate gate

### Requirement: Move-back restores neighbors from session ledger or baseline

Position moves during an active session SHALL restore temporarily **hidden** neighbors when the mover no longer overlaps them, using the session ledger first and the session baseline if event pairs are missing.

#### Scenario: Return home without fader-1 reselect

- **WHEN** the user moves the moving note back toward its home position without fader-1 reselect
- **AND** neighbors were previously hidden or pitch-restored to visible but still tracked as inner
- **THEN** neighbors still overlapped SHALL remain **hidden**
- **AND** neighbors no longer overlapped SHALL be restored at baseline geometry

#### Scenario: M0 home after overlap round-trip

- **WHEN** the overlap round-trip completes on the standard edit fixture
- **THEN** the moving note SHALL be at fixture home start tick
- **AND** fixture inner neighbors (A, P0) SHALL match HITL gate expectations in reconstruction

### Requirement: Rematerialize matches live overlap session

After EditChanges are committed or the session store is replayed, the materialized note inventory SHALL match the live edit store at the same session checkpoint for neighbor notes and the lengthened moving note.

#### Scenario: Post-pitch native parity snapshot

- **WHEN** a ChangePitch EditChange is committed after lengthened move-over overlap
- **THEN** rematerialized inventory SHALL contain exactly one M0-class mover at the expected pitch and span
- **AND** SHALL preserve P0 at fixture step 12 with record gate length

### Requirement: Shared release at mover start does not corrupt inner neighbor offs

When the moving note's start tick coincides with an inner neighbor's release tick, overlap handling SHALL preserve correct note-off ownership per pitch lane.

#### Scenario: Mover start at inner A note-off tick

- **WHEN** mover start is placed at tick T
- **AND** inner neighbor A has note-off at tick T
- **THEN** the firmware SHALL NOT pair the mover's note-on with A's note-off across pitches
- **AND** reconstruction SHALL show separate notes for A and the mover after the move
