## MODIFIED Requirements

### Requirement: Display refreshes after load completes

When deferred load reaches **COMPLETE**, the system SHALL signal the display layer to invalidate live
display caches and per-loop visual caches so the piano roll reflects restored loop data without
requiring a manual track change.

If the load/save overlay is open when load completes, the system SHALL exit the overlay as part of
load completion handling so display refresh pending is consumed on the next full display update.

#### Scenario: Piano roll updates after HITL load

- **WHEN** `loadRevisionIntoCurrent` completes for a revision with occupied LoopSlots
- **THEN** the overlay exits if it was open
- **AND** the next display update rebuilds notes from restored pass storage

#### Scenario: Overlay exit unblocks cache refresh flag

- **WHEN** load completes while the overlay is open
- **THEN** `revisionLoadDisplayRefreshPending` is consumed after overlay exit
- **AND** per-loop `markDisplayCachesStale` runs before piano-roll draw resumes
