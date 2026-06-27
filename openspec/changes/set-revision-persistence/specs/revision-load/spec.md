## ADDED Requirements

### Requirement: Uncommitted Current must be saved before loading another Set

The system SHALL require user confirmation before loading another Set or revision when workspace is
dirty (`lastCommittedEpoch != currentEpoch`). The system SHALL NOT replace Current without
resolving uncommitted work via the **"Save Current?"** prompt or explicit discard.

Continuous deferred persistence to `/current/` does **not** count as an immutable revision commit.

#### Scenario: Reboot then load still prompts when never committed

- **WHEN** the user recorded material into Current since boot
- **AND** never ran `commitRevision`
- **AND** the user initiates load of Set `S0003`
- **THEN** the "Save Current?" prompt is shown

### Requirement: Dirty-load Yes runs save-then-load pipeline

When the user single-presses **Yes** on "Save Current?", the system SHALL:

1. Run deferred `commitRevision` — new Set `v0001` if `derivedFromSetId == 0`; else `v000N+1` on
   **derived Set** (including when pending target is the same Set or an older revision).
2. After validated commit succeeds, run deferred `loadRevisionIntoCurrent` for the pending target.
3. Keep the overlay in **minimal mode** until **both** save and load complete, then exit.
4. Update `derivedFromSetId` / `derivedFromRevisionId` only after load is **100% complete**.

#### Scenario: Derived Set dirty — revision then load different Set

- **WHEN** Current derives from `S0001` `v0003` with uncommitted changes
- **AND** the user loads `S0005` and presses **Yes**
- **THEN** `S0001/revisions/v0004.bin` is committed and validated
- **AND** then `S0005` latest revision loads into Current
- **AND** provenance updates to `S0005` only after load completes

#### Scenario: Same Set — save revision then load older revision

- **WHEN** Current derives from `S0001` `v0003` with uncommitted changes
- **AND** the user selects `S0001` `v0002` from revision history and presses **Yes**
- **THEN** `S0001/revisions/v0004.bin` is committed first
- **AND** then `v0002` loads into Current

### Requirement: Dirty prompt applies only to Set and revision load

The **Save Current?** prompt (`DIRTY_PROMPT`) SHALL apply only to **full workspace** Set or revision
load. Loop copy from the loop overlay SHALL NOT trigger the dirty prompt.

#### Scenario: Loop import while dirty does not prompt

- **WHEN** Current is dirty and the user imports a loop into a slot from the loop overlay
- **THEN** no **Save Current?** prompt is shown
- **AND** import proceeds directly

### Requirement: Dirty-load No discards with single press

**No** on "Save Current?" SHALL load the pending target without commit on a **single** press (no
double-press confirm). The overlay SHALL stay in minimal mode until load completes, then exit.

#### Scenario: User discards uncommitted work

- **WHEN** the user single-presses **No** on "Save Current?"
- **THEN** Current is replaced from the pending revision without a new commit
- **AND** the overlay exits after load completes

### Requirement: Clean load uses deferred load and minimal overlay

When Current has no uncommitted changes, load SHALL use chunk-bounded deferred
`loadRevisionIntoCurrent` (same non-interruption rules as commit). The overlay SHALL stay in minimal
mode until load is **100% complete**, then exit.

#### Scenario: Clean load while PLAYING

- **WHEN** Current is clean and the user loads Set `S0003` during **PLAYING**
- **THEN** load progresses in deferred slices without a multi-second MIDI gap
- **AND** provenance updates only after load completes

### Requirement: Current remains mutable after load

After load completes, **Current** SHALL remain the live editable workspace.

#### Scenario: Record after load continues in Current

- **WHEN** the user loads `S0003` `v0009` into Current
- **AND** records a new loop
- **THEN** deferred save writes to `/current/` paths
