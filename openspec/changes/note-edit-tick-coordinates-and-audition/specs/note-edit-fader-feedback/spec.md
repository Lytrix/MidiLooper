## ADDED Requirements

### Requirement: Unified geometry-dependent fader snapshot (F2–F4)

During NOTE_EDIT, dependent fader outbound values for F2 coarse, F3 fine, and F4 note value SHALL be
derived from a single `NoteEditDependentFaderSnapshot` built from live note geometry. The builder
SHALL NOT use filtered list index alone as the motor-sync gate.

Wire encodings per slot:

| Fader | Protocol | Channel | Snapshot field |
|-------|----------|---------|----------------|
| F2 coarse | pitchbend | ch14 | `coarsePitchbend` |
| F3 fine | CC #2 | ch15 | `fineCc` |
| F4 note value | CC #3 | ch15 | `noteValueCc` |

Position mode (`!lengthEditingMode`) SHALL anchor F2/F3 from loop-relative **start** tick via
`noteRelativeTick`. Length mode SHALL anchor F2 from loop-relative **end** tick and F3 from
`lengthFineAnchorEndTick` offset. F4 SHALL use `liveEditDisplayNoteAtSelect` pitch when
`focus.active`; empty-step (`selectedIdx < 0`) SHALL produce valid F2/F3 bracket values and
`valid == false` for F4.

#### Scenario: Position mode with non-zero loop start

- **WHEN** `loopStartTick = 424`, `loopLength = 768`, a note is selected, and `lengthEditingMode == false`
- **THEN** `buildDependentFaderSnapshot` maps F2 pitchbend from loop-relative start tick
- **AND** F3 fine CC reflects sub-step offset from `referenceStep` using loop-relative start tick

#### Scenario: Length mode NOTELEN anchor

- **WHEN** NOTELEN enables length mode with a note selected
- **THEN** snapshot F2 coarse maps from loop-relative end tick
- **AND** snapshot F3 fine CC reflects offset from `lengthFineAnchorEndTick`

#### Scenario: Empty step bracket only

- **WHEN** `selectedIdx < 0` during NOTE_EDIT
- **THEN** snapshot provides bracket F2 pitchbend and F3 fine CC
- **AND** snapshot `valid` is false for F4 note value

### Requirement: Unified dependent fader outbound send

All NOTE_EDIT dependent fader outbound (select sync, session open coarse step, NOTELEN enter/exit,
geometry latch publish) SHALL route through `sendDependentFaderSnapshot` with
`DependentFaderSendMode::ValueOnly` or `ValueAndMotor`. Motor burst orchestration SHALL compare
planned snapshot against prior `lastSentPitchbend` (F2) and `lastSentCC` (F3/F4) for all three slots
before skipping motor movement.

#### Scenario: Select-dependent motor sync uses snapshot

- **WHEN** deferred dependent refresh runs after fader1 quiet
- **THEN** `processPendingSelectDependentMotorSync` builds snapshot from select target tick plus
  `liveEditDisplayNoteAtSelect`
- **AND** does not read geometry from filtered list index alone

#### Scenario: Motor skip when snapshot unchanged

- **WHEN** planned F2 pitchbend, F3 fine CC, and F4 note value CC match prior `lastSent*` values
- **THEN** motor burst for unchanged slots MAY be skipped
- **AND** value-only latch publish still updates `lastSent*` when explicitly requested

### Requirement: Stale-latch dependent fader feedback ignore

For F2–F4 inbound during an active geometry edit (`focus.active`), the system SHALL ignore motor echo
when the inbound value matches the latched `lastSentPitchbend` or `lastSentCC` **and** does not match
the live snapshot value from current geometry. This stale-latch rule SHALL apply regardless of elapsed
time since last outbound send; the system SHALL NOT bypass stale-latch solely because
`now - lastSentTime >= FEEDBACK_IGNORE_PERIOD`.

Within `FEEDBACK_IGNORE_PERIOD` and post-send grace (200 ms), existing smart echo ignore SHALL
still apply. F1 select fader (ch16) SHALL remain on the separate select feedback path.

#### Scenario: F4 pitch wrap — stale motor echo ignored

- **WHEN** the user edits pitch down via F4 inbound so live geometry pitch is 28
- **AND** `lastSentCC` for F4 still reflects prior select value 39 from an earlier motor sync
- **AND** at loop BAR wrap the DROID motor echoes CC=39
- **THEN** inbound CC=39 is ignored as stale latch
- **AND** session store pitch remains 28

#### Scenario: F2 stale pitchbend after long hold

- **WHEN** `focus.active` during move edit and inbound F2 pitchbend matches stale `lastSentPitchbend`
- **AND** live snapshot `coarsePitchbend` differs
- **THEN** inbound is ignored without applying geometry mutation

#### Scenario: Genuine user move after latch refresh accepted

- **WHEN** `publishDependentFaderLatch` has updated `lastSentCC` to match live pitch 28
- **AND** the user moves F4 to a new pitch
- **THEN** inbound differing from latched 28 is accepted as user edit

### Requirement: Dependent fader latch publish after geometry mutation

After each F2, F3, or F4 geometry driver mutation (`handleCoarseFaderInput`,
`handleFineFaderInput`, `handleNoteValueFaderInput`), the system SHALL call
`publishDependentFaderLatch` to send `ValueOnly` snapshot for all three dependent slots and update
`lastSentPitchbend` / `lastSentCC` without motor movement. Geometry drivers SHALL NOT rely on
`scheduleOtherFaderUpdates` for latch refresh.

#### Scenario: F4 pitch edit refreshes latch

- **WHEN** the user changes pitch via F4 inbound during NOTE_EDIT
- **THEN** firmware updates session store pitch
- **AND** sends F2/F3/F4 value-only feedback reflecting current geometry
- **AND** F4 `lastSentCC` matches the new pitch before the next loop wrap

#### Scenario: F2 move step refreshes latch

- **WHEN** the user moves note position via F2 coarse inbound
- **THEN** firmware applies move mutation
- **AND** publishes dependent fader latch for all three slots

## MODIFIED Requirements

### Requirement: Coarse outbound feedback timestamps

All outbound fader2 coarse feedback paths (`sendDependentFaderSnapshot` with `plan.coarse`,
`processFaderOutbound` `SendCoarse`, NOTELEN enable) SHALL update the coarse fader state's
`lastSentTime` and `lastSentPitchbend` consistently so smart feedback ignore and stale-latch operate
correctly. F3 and F4 value-only and motor paths SHALL update `lastSentCC` on the respective fader
state when those slots are sent.

#### Scenario: Coarse lastSentTime after dependent refresh

- **WHEN** `processFaderOutbound` sends fader2 coarse on `SendCoarse`
- **THEN** `midiFaderManager` coarse fader state `lastSentTime` is set to the send time
- **AND** channel 14 coarse ignore is armed via `armCoarseFaderFeedbackIgnore` or equivalent

#### Scenario: Latch publish updates F4 lastSentCC

- **WHEN** `publishDependentFaderLatch` runs after pitch mutation
- **THEN** F4 fader state `lastSentCC` and `lastSentTime` reflect the published note value CC

### Requirement: Single motor trigger owner (Phase 12)

Each dependent fader stage SHALL emit at most one motor trigger per outbound pipeline step. Duplicate
triggers from removed send wrappers SHALL be eliminated. Phase 12 ships as part of Tier 3 of change
`note-edit-tick-coordinates-and-audition`.

#### Scenario: One trigger per F2 stage after cleanup

- **WHEN** Phase 12 cleanup is complete and dependent refresh runs
- **THEN** each `SEND_F2` / parallel burst step emits exactly one motor cluster for that stage
- **AND** removed dead wrappers (`sendFaderUpdate`, `sendFaderPosition`, `performSelectnoteFaderUpdate`,
  `*FromSelectTarget`, `*TimedUpdate`, `plannedMotorSyncValuesFromSelectTarget`, `sendStartNotePitchbend`)
  have zero call sites

### Requirement: Dead outbound API removal (Phase 12)

Firmware SHALL NOT retain zero-caller NOTE_EDIT fader outbound wrappers after Phase 12 ships as part
of Tier 3. Native tests SHALL pass after removal.

#### Scenario: Native build after dead code removal

- **WHEN** Tier 3 Phase 12 tasks are complete
- **THEN** `pio test -e native` passes
- **AND** dependent refresh uses `NoteEditDependentFaderSnapshot` + `sendDependentFaderSnapshot` only
