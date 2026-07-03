## ADDED Requirements

### Requirement: Set window drives fader range and selectable notes

The **set window** (bar-quantized edit viewport ticks) SHALL drive:

- F1 select and F2 coarse **position range** (minimum/maximum ticks the faders represent)
- The filtered **NoteId** set available for select navigation within the window
- Display / piano-roll view extent for NOTE_EDIT

Fader tick mapping SHALL use loop-relative coordinates within the set window, not unbounded absolute loop ticks.

#### Scenario: Fader range matches set window when window equals loop length

- **WHEN** set window span equals `loopLength` (full-loop edit view)
- **THEN** F1/F2 coarse range covers `[0, loopLength)` (bar-quantized)
- **AND** moving F2 past the maximum wraps note position within the loop

#### Scenario: Selectable notes filtered to window

- **WHEN** set window covers ticks `[768, 1536)` on a `loopLength = 3072` loop
- **THEN** select navigation and F1 bracket only consider notes whose projected segments intersect that window
- **AND** `NoteId`s outside the window are not selectable until the window includes them

#### Scenario: Partial window slide (future — out of Phase 2)

- **WHEN** set window is a proper subset of loop length (e.g. 2 bars of a 64-bar loop)
- **AND** user moves F2 past the window maximum while a further bar exists to the right
- **THEN** the set window shifts one bar right and selectable notes refresh (future enhancement)
- **NOTE:** Phase 2 implements full-loop window wrap only; partial-window slide is documented but not required to ship linear ticks

### Requirement: Unified geometry-dependent fader snapshot (F2–F4)

During NOTE_EDIT, dependent fader outbound values for F2 coarse, F3 fine, and F4 note value SHALL be derived from a single `NoteEditDependentFaderSnapshot` built from live note geometry within the set window context.

Position mode (`!lengthEditingMode`) SHALL anchor F2/F3 from loop-relative **start** tick. Length mode SHALL anchor F2 from loop-relative **display end** (projection from invariant 7 when `NoteOff > loopLength`).

#### Scenario: Position mode with wrapped linear span

- **WHEN** `loopLength = 1536`, canonical `NoteOn@1344`, `NoteOff@1536`, full-loop set window, position mode
- **THEN** F2/F3 snapshot uses loop-relative start `1344` via set-window mapping
- **AND** storage off tick `1536` is not passed through `% loopLength` for anchor

#### Scenario: Length mode with linear off beyond loop end

- **WHEN** NOTELEN is active and `NoteOff.tick > loopLength`
- **THEN** F2 coarse maps from projected display end within the set window
- **AND** F3 fine uses `lengthFineAnchorEndTick` from that projected end

### Requirement: Dual normalization ordering for fader outbound

Geometry fader commit SHALL follow:

1. Staged pipeline completes (linear off writes).
2. `normalizeWindow(editClosureSet)` — micro boundary.
3. `publishDependentFaderLatch` — fader outbound from post-window-normalize geometry.

Macro `normalizeAll` at `commitAllPendingNoteEditActions` runs later; fader latch does not replace macro canonical commit.

#### Scenario: Fader latch after micro normalize

- **WHEN** `publishDependentFaderLatch` runs after a move edit
- **THEN** `normalizeWindow` has already run on the edit closure set
- **AND** `buildDependentFaderSnapshot` reflects linear ticks for notes in the closure

#### Scenario: Mid-pipeline fader reads transaction state

- **WHEN** geometry edit is in progress before latch (pipeline incomplete)
- **THEN** snapshot MAY reflect non-canonical transaction-state ticks

### Requirement: Stale-latch dependent fader feedback ignore

For F2–F4 inbound during an active geometry edit (`focus.active`), stale-latch ignore SHALL apply when inbound matches `lastSent*` but differs from live snapshot geometry. F1 select fader (ch16) remains on the separate select feedback path.

#### Scenario: Stale latch with non-canonical mid-transaction store

- **WHEN** `focus.active` during move before latch and inbound F2 matches stale `lastSentPitchbend`
- **AND** live snapshot differs
- **THEN** inbound is ignored without applying geometry mutation
