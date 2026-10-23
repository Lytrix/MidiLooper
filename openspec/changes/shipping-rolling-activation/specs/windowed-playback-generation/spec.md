## ADDED Requirements

### Requirement: Shipping playback is activated and compatibility emitters are retired in stages

The shipping image SHALL serve playback through the unified rolling reader only after an explicit activation step that
proves parity against the cold legacy reference, and compatibility emission families SHALL be removed one family at a
time only after that activation is proven and each family's routes are device-requalified. Activation and removal SHALL
NOT be performed as one indivisible step.

#### Scenario: Activation preserves the cold reference parity

- **WHEN** `PLAYBACK_WINDOW_ROLLING_ENABLED` is enabled on the shipping image without removing any compatibility branch
- **THEN** every supported route emits the same ordered MIDI and ledger as the same source built with the flag disabled
  (the cold reference), and the device run records its own readiness/content/ring/high-water verdict
- **AND** a named missed deadline or refusal is a failure, not a fallback pass

#### Scenario: Rollback exists until the last family is removed

- **WHEN** a compatibility family has not yet been removed
- **THEN** disabling the rolling flag restores that family's legacy serving path, and the exact rollback HEX pair is
  recorded
- **AND** after a family is removed the rollback is the prior HEX, which is why removals are ordered and one at a time

#### Scenario: One family per removal slice

- **WHEN** a compatibility family is removed
- **THEN** exactly one of the prepared pair, fixed/bridge, first-Record/Record-return, paged emission selection, or
  merged-fallback/overdub-echo families is deleted in that slice
- **AND** the slice carries its own RED→GREEN, native and device evidence, and the cold parity comparison is re-run

#### Scenario: No supported route reaches a removed family

- **WHEN** a family has been removed
- **THEN** every supported route's production path serves through the unified reader, the family's tests are retired or
  rewritten to the new route, and no compatibility selection is silently left reachable
- **AND** `ExecutionBounds.md` row 2 is reclassified only after the last family is removed, with evidence

#### Scenario: Deferred routes are not silently activated

- **WHEN** simultaneous-track Record, layered/jam, or short-loop Record/overdub is not in the owner's supported-route
  scope
- **THEN** neither activation nor removal claims those routes, and enabling one requires its own acceptance evidence
