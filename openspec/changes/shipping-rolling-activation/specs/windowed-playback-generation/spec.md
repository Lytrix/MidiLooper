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

#### Scenario: Dense committed successor is positioned at a paid future occurrence

- **WHEN** a committed rolling successor's derived bounded work cannot fit the first handoff occurrence
- **THEN** the existing bounded occurrence search selects a later absolute handoff that satisfies the unchanged
  service contract, or refuses when the bounded search or timestamp range is exhausted
- **AND** pending, frozen, admitted and ready work preserve that selected deadline without re-granting it
- **AND** an earlier occurrence of the same loop phase cannot adopt the private successor; existing coherent
  fallback and source-currency, sibling-deadline, cancellation and missed-deadline policies remain in force

#### Scenario: Partial visual-cache publication is observable without viewport movement

- **WHEN** a successful bounded visual-cache slice publishes new committed display notes while other bars remain dirty
- **THEN** the existing cache revision advances so a stationary draw-source consumer cannot reuse a stale prefix
- **AND** publication-gap held-frame/content-interval coherence and source-selection policy remain unchanged

#### Scenario: Snapshot publication accounts for allocation tiers without bypassing safety

- **WHEN** snapshot publication admits a content-sized construction turn on an external-only slab target
- **THEN** it charges the internal working margin and safety floor without charging an external slab to internal heap
- **AND** low internal headroom or recent pool refusal keeps the old root selected and publication pending;
  allocation failure retains the existing park/recovery protocol and does not publish partial content

#### Scenario: Odd-bar committed playback uses a non-wrapping terminal window

- **WHEN** a supported committed loop contains an odd whole-bar count, including the 33-bar reproducer
- **THEN** launch and ordinary rolling requests are not excluded merely for two-bar non-divisibility
- **AND** terminal coverage is clamped at the loop end, its work is priced, adoption and expiry use the exact span,
  and positioning across wrap uses the loop-local window grid starting at phase zero
- **AND** source, role, storage-generation and paid absolute-deadline checks remain unchanged;
  no whole-loop reservation or continuous-coverage claim is introduced

#### Scenario: Completed stop bridge publishes without an avoidable emission gap

- **WHEN** the final reserved bridge preparation grain completes and the unchanged live ledger/cursor/source
  preflight accepts the candidate
- **THEN** the stop owner enters existing atomic publication on that turn without yielding across another MIDI emission
- **AND** stale candidates still fail closed; no second preparation or cleanup grain executes on that turn

#### Scenario: Record/Overdub intent survives private first-Record publication

- **WHEN** the active slot's first Record pass is still private after Record stop and the user presses Record/Overdub
- **THEN** the existing TrackManager route immediately attempts ordinary overdub admission against the
  identity-qualified private Record predecessor, without muting, re-recording or waiting for publication
- **AND** within the admitted handoff envelope B accepts input before A publishes, through the existing
  sealed-custody/private-source contract in `immediate-record-overdub-handoff`
- **AND** resource refusal retains one slot/LoopId/interval-qualified Start for ordinary recovery; actual start
  consumes it once, without bypassing staging/memory/source checks
- **AND** clear, active-slot exit, Stop/non-Playing reset, replaced identity or a denied Start with no remaining
  base cannot cause stale restart; changing track focus alone does not retarget the request
- **AND** final Record geometry/master-length adoption does not wait for an unrelated retained Start
- **AND** service budgets are unchanged; physical ingress/output timing remains a separate qualification gate
