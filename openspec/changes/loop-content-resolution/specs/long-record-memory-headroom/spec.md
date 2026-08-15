## ADDED Requirements

### Requirement: Normal overdub path does not rematerialize all history

After `LoopContentResolution` three gates pass, committing a new overdub or edit pass MUST NOT rematerialize all historical passes. Resolution cost after indexing and checkpointing SHALL track candidate events and affected state, not historical pass count.

Until those gates pass, Layer D 3b (copy clean `visualCache.notes` at overdub entry; mark stale only on undo) SHALL remain the production overdub-entry path. Full `materializeToEventVector` on the normal post-commit path is a known defect this change is proving a replacement for; it MUST NOT be treated as acceptable long-term behavior.

#### Scenario: New pass does not flatten P0 through P(N-1)

- **WHEN** an additional overdub pass is committed on a loop that already has many Active passes
- **THEN** the normal path MUST NOT call full-loop `materializeToEventVector` of all prior passes
- **AND** candidate work is limited to indexed affected regions

#### Scenario: Overdub start does not cold-build LoopContentResolution

- **WHEN** overdub starts or stops
- **THEN** firmware MUST NOT create or rebuild `LoopContentResolution` indexes, call `resolveWindow` / `resolveState` as a prerequisite, materialize, reconstruct, or `markDisplayCachesStale` on that path
- **AND** overdub consumes already-prepared derived state (authoritative `visualCache.notes` copy, or LCR that idle/background already completed)
- **AND** if LCR is not ready, the existing non-LCR 3b dirty fallback (`CommittedEventRange::inWindow` + edit apply) is used instead of a synchronous LCR build
