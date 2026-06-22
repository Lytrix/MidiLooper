## ADDED Requirements

### Requirement: Long recordPass finalize is completion-bounded
Record stop for long capture SHALL complete pass finalize and publish steps within bounded wall-clock time so stop-path state progression can continue.

The synchronous stop path SHALL NOT perform display-only rebuilds, full-loop validation, or capture verification sweeps before allowing state progression to `PLAYING`.

#### Scenario: 64-bar recordPass publishes at stop
- **WHEN** a long record capture is stopped
- **THEN** `recordPass` publish/finalize completes
- **AND** the slot can proceed from stop state into play state without hanging in finalize work

#### Scenario: Display and verification work is deferred
- **WHEN** record stop publishes a long `recordPass`
- **THEN** display-only visual cache rebuild and record verification event emission are deferred or processed in bounded slices
- **AND** the track may transition to `PLAYING` before those deferred tasks complete

### Requirement: Long stop-path failure returns explicit outcome
If long stop finalize cannot complete, stop-path code SHALL return an explicit failure outcome that is surfaced in verification evidence.

#### Scenario: Finalize failure is surfaced
- **WHEN** stop-path finalize fails during record stop
- **THEN** the stop-path emits an explicit failure outcome
- **AND** verification output reports this finalize failure instead of only downstream transition gaps
