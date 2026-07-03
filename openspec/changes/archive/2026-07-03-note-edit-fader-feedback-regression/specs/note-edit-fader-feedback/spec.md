## ADDED Requirements

### Requirement: NOTE_EDIT session entry fader feedback

When a NOTE_EDIT session opens and a note is selected, the system SHALL send outbound DROID feedback so fader1 reflects the select bracket and faders 2–3 reflect the selected note **start** position (when `!lengthEditingMode`) within a bounded latency of **≤3200 ms** from session open (fader1 by ≤1600 ms, fader2+fader3 by ≤3200 ms).

#### Scenario: Entry with selected note

- **WHEN** `openNoteEditSession` completes with `selectedNoteIdx >= 0` and `lengthEditingMode == false`
- **THEN** serial logs include `Session fader sync: sent fader 1 to select bracket` within 1600 ms
- **AND** serial logs include `Session fader sync: sent fader 2 coarse to selected note` before or with fader3 fine sync
- **AND** fader2 and fader3 motors reflect the selected note start position

#### Scenario: Entry without selected note

- **WHEN** `openNoteEditSession` completes with `selectedNoteIdx < 0`
- **THEN** the system SHALL send fader1 bracket feedback only
- **AND** SHALL NOT schedule fader2/fader3 position sync

### Requirement: Fader1 note select refreshes coarse and fine together

When the user changes note selection via fader1 during NOTE_EDIT and `lengthEditingMode == false`, the system SHALL refresh fader2 coarse and fader3 fine **together** to match the newly selected note start after fader1 has settled. Fader1 bracket feedback MAY update immediately on select; F2–F4 dependent refresh is **deferred** (see deferred dependent refresh requirement). The system SHALL NOT send fader3 fine feedback without a corresponding fader2 coarse update for the same selection event.

#### Scenario: Select different note in position mode

- **WHEN** fader1 select changes `selectedNoteIdx` and `lengthEditingMode == false`
- **AND** fader1 has had no user-classified input for ≥400 ms since the last select change (`kFader1QuietMs`)
- **THEN** outbound feedback updates fader2 and fader3 to the new note start
- **AND** serial does not show fader3-only position sync without fader2 coarse for that selection

#### Scenario: Rapid fader1 select coalesces to final note

- **WHEN** the user moves fader1 across multiple notes within 400 ms
- **THEN** the system SHALL coalesce dependent refresh to the final selected note
- **AND** SHALL run one complete F2 → F3 → F4 outbound pipeline after fader1 quiet period

#### Scenario: Note select resets length mode

- **WHEN** fader1 select changes the selected note
- **THEN** `lengthEditingMode` SHALL be `false`
- **AND** fader2/fader3 feedback SHALL use note **start** anchoring

### Requirement: NOTELEN enable sends end-position coarse feedback

When the user enables length editing via NOTELEN (`toggleLengthEditingMode` true), the system SHALL set `lengthFineAnchorEndTick` from `liveNote.endTick % loopLength`, SHALL send fader2 coarse feedback mapped to that end tick, and SHALL send fader3 fine feedback relative to the anchor **before** accepting coarse length inbound.

#### Scenario: NOTELEN on selected note

- **WHEN** NOTELEN enables length mode with a note selected and `loopLength > 0`
- **THEN** serial logs include `Length editing mode ENABLED`
- **AND** coarse feedback uses `LENGTH EDIT` mapping with anchor `endTick % loopLength`
- **AND** moving fader2 across full travel can change note end across the usable loop range (subject to min-duration rules)

#### Scenario: NOTELEN coarse inbound uses full loop range

- **WHEN** length mode is active and the user moves fader2 coarse after NOTELEN feedback completed
- **THEN** pitchbend maps to tick `0..loopLength-1` via linear coarse mapping
- **AND** `applyLengthEndTargetRules` enforces minimum note duration without collapsing range to a tiny delta solely due to stale motor position

### Requirement: Session fader sync input gating

During NOTE_EDIT session open outbound (`SessionOpen` trigger), the system SHALL block fader input only as needed to prevent motor-echo feedback. Fader1 input SHALL NOT remain blocked for the full ch15 pipeline after fader1 feedback has been sent and `FEEDBACK_IGNORE_PERIOD` has elapsed.

#### Scenario: Fader1 usable after its ignore window

- **WHEN** `SessionOpen` pipeline has sent fader1 bracket feedback and `FEEDBACK_IGNORE_PERIOD` has elapsed
- **AND** ch15 dependent feedback (F2–F4) is still pending or in progress
- **THEN** fader1 inbound SHALL be accepted (subject to normal select stability thresholds)
- **AND** ch15 faders MAY remain blocked until coarse+fine sync completes

#### Scenario: No duplicate schedulers on session open

- **WHEN** `sendNoteEditSessionFaderFeedback` starts `SessionOpen` outbound
- **THEN** the system SHALL NOT schedule a competing note-select outbound for the same geometry
- **AND** `syncNoteEditSessionStateToUi` SHALL NOT restart outbound while `SessionOpen` pipeline is active

### Requirement: Coarse outbound feedback timestamps

All outbound fader2 coarse feedback paths (`sendCoarseFaderPosition`, `processFaderOutbound` `SendCoarse`, NOTELEN enable) SHALL update the coarse fader state's `lastSentTime` and `lastSentPitchbend` consistently so smart feedback ignore operates correctly.

#### Scenario: Coarse lastSentTime after dependent refresh

- **WHEN** `processFaderOutbound` sends fader2 coarse on `SendCoarse`
- **THEN** `midiFaderManager` coarse fader state `lastSentTime` is set to the send time
- **AND** channel 15 coarse ignore is armed via `armCoarseFaderFeedbackIgnore` or equivalent

### Requirement: Slot-index navigation on fader1 select

When user-classified fader1 input maps to a different select navigation slot index than the prior sample, the system SHALL apply note selection immediately. The system SHALL NOT suppress navigation based on pitch delta alone (`SELECT_MOVEMENT_THRESHOLD`).

#### Scenario: Fast then slow — slot reached via micro-moves

- **WHEN** the user sweeps fader1 quickly then creeps into the final slot with per-sample pitch delta below feedback threshold
- **AND** the mapped slot index changes on a user-classified sample
- **THEN** selection updates immediately for that slot change
- **AND** dependent F2–F4 refresh runs after user-classified quiet (see deferred dependent refresh)

#### Scenario: Fader1 at minimum pitch

- **WHEN** user-classified fader1 pitch maps to slot index 0
- **THEN** `selectedNoteIdx` and bracket reflect slot 0 before dependent refresh
- **AND** fader2 coarse feedback after quiet matches the selected note at slot 0

### Requirement: Deferred dependent refresh after fader1 select

When note selection changes via fader1 during NOTE_EDIT and `lengthEditingMode == false`, F2–F4 outbound SHALL NOT start until fader1 has had **no user-classified input** for **≥400 ms** (`NoteEditFaderOutbound::kFader1QuietMs`). Motor-echo samples SHALL NOT reset the quiet timer. Fader1 bracket feedback on GPIO/bar-step select MAY use `NoteSelectWithFader1` trigger immediately.

#### Scenario: Dependent refresh after fader1 quiet

- **WHEN** fader1 select changes `selectedNoteIdx` and the user stops user-classified fader1 input for ≥400 ms
- **THEN** the system SHALL re-apply selection from final pitch and begin the ch15 outbound pipeline (F2 coarse → motor trigger → F3 fine → motor trigger → F4 note value → motor trigger)
- **AND** serial logs include `#DBG outbound_step` transitions through completion

#### Scenario: Quiet timer not reset by motor echo

- **WHEN** motor echo fader1 samples arrive during the quiet window (classified as feedback)
- **THEN** the quiet timer SHALL NOT reset
- **AND** dependent refresh SHALL still run within 400 ms of the last user-classified sample

### Requirement: Non-preemptive outbound execution

An active ch15 outbound sequence SHALL run from its first step through `Done` without cancellation by a subsequent NoteSelect refresh request. A pending refresh SHALL coalesce to the latest note geometry and run immediately after the current pipeline completes.

#### Scenario: Coalesce while pipeline active

- **WHEN** a NoteSelect dependent refresh is requested while `outboundStep_` is on a ch15 step
- **THEN** the system SHALL set `pendingRefresh` with the latest trigger
- **AND** SHALL NOT call `cancelActiveFaderOutbound()` for NoteSelect
- **AND** SHALL start the coalesced refresh when the current pipeline reaches `Done`

#### Scenario: LengthModeEnter may preempt

- **WHEN** NOTELEN enables length mode during a pending or active NoteSelect refresh
- **THEN** the system MAY preempt with `LengthModeEnter` outbound
- **AND** SHALL send fader2 coarse to note end before accepting length inbound

### Requirement: Sequential motor stages

Each dependent fader SHALL receive pitchbend or CC feedback followed by a motor trigger before the next fader stage begins. The system SHALL NOT skip motor trigger steps for F2, F3, or F4 during a complete refresh pipeline.

#### Scenario: Complete F2–F4 pipeline on note select

- **WHEN** deferred dependent refresh executes after fader1 quiet
- **THEN** serial shows sequential stages: coarse pitchbend → coarse trigger → fine CC → fine trigger → note-value CC → note-value trigger
- **AND** motors move to positions matching the selected note start

### Requirement: Outbound instrumentation (capture-serial)

Under `teensy41-capture-serial` build, the system SHALL emit `#DBG outbound_step` lines on outbound state transitions for HITL diagnosis.

#### Scenario: Healthy pipeline trace

- **WHEN** a complete dependent refresh runs after fader1 quiet
- **THEN** serial includes step transitions such as `BEGIN`, `ARM`, `SEND_F2`, `TRIGGER_F2`, `SEND_F3`, `TRIGGER_F3`, `SEND_F4`, `TRIGGER_F4`, `DONE`, `QUIET_REFRESH`
- **AND** does not show repeated `SEND_F1` without intervening ch15 stages (restart loop pattern)

### Requirement: Select slot instrumentation (capture-serial)

Under `teensy41-capture-serial` build, the system SHALL emit `#DBG select_slot` lines on user-classified fader1 select input with slot index and pitch value.

#### Scenario: Ignored feedback logged

- **WHEN** fader1 input is classified as feedback and ignored
- **THEN** serial MAY include `#DBG select_slot idx=-1 ignored=1`

### Requirement: Loop-relative outbound tick (F2 coarse)

When sending fader2 coarse outbound during NOTE_EDIT, the system SHALL map pitchbend from the loop-relative tick, not storage tick modulo loop length alone. Position mode (`!lengthEditingMode`) SHALL use `SelectNavigation::noteRelativeTick(startTick, loopStartTick, loopLength)`. Length mode SHALL use `noteRelativeTick(endTick, loopStartTick, loopLength)`.

#### Scenario: Non-zero loop start — position mode

- **WHEN** `loopStartTick = 424`, `loopLength = 768`, selected note `startTick = 0`, and `lengthEditingMode == false`
- **THEN** outbound F2 coarse pitchbend maps to relative tick 344 (~47.8% of loop)
- **AND** serial `#DBG outbound_ctx` shows `pb == expected_pb_rel`

#### Scenario: Length mode end anchor

- **WHEN** NOTELEN enables length mode with non-zero `loopStartTick`
- **THEN** fader2 coarse feedback uses loop-relative end tick before pitchbend mapping

### Requirement: Loop-relative outbound tick (F3 fine) — Phase 10

When sending fader3 fine outbound in position mode, the system SHALL compute fine offset from loop-relative start tick, not `startTick % loopLength` alone.

#### Scenario: Fine offset with non-zero loop start

- **WHEN** `loopStartTick ≠ 0` and position-mode fine feedback runs after F2 coarse
- **THEN** fine CC reflects offset from reference step using loop-relative start tick
- **AND** F3 motor aligns with selected note start on piano roll

### Requirement: F1 ignore during F2 outbound

While the ch15 outbound pipeline is on `SendCoarse` through `TriggerCoarse`, user-classified fader1 samples SHALL NOT re-trigger note selection. Motor-echo classification SHALL remain subject to `SELECT_MOVEMENT_THRESHOLD`.

#### Scenario: No spurious select during F2 send window

- **WHEN** `processFaderOutbound` executes `SendCoarse` and `TriggerCoarse` for dependent refresh
- **THEN** serial SHALL NOT show new `#DBG select_slot` with `ignored=0` caused by F2 motor echo during that window
- **AND** `selectFaderFeedbackIgnoreUntilMs_` is armed at `SendCoarse`

### Requirement: Geometry F1 motor echo guard during geometry edit

Geometry-driven F1 bracket motor sync (`sendFader1MotorTimedBurst`) is outbound-only for selection UI: `applySelectionFromGeometryEdit` SHALL use `syncGeometrySelectionToUi`. User-driven F1 select during geometry edit kinds MAY commit geometry and run `applySelectNav`. Motor echo after geometry F1 send SHALL NOT apply select navigation within `FEEDBACK_IGNORE_PERIOD`.

#### Scenario: F1 motor echo after geometry flush does not change selection

- **WHEN** `geometry_motor_sync sent=1` has sent F1 via `sendFader1MotorTimedBurst`
- **AND** inbound fader1 pitchbend arrives within `FEEDBACK_IGNORE_PERIOD` (motor echo)
- **THEN** serial SHALL NOT show `#DBG select_apply ... apply=1` caused by that inbound sample
- **AND** serial SHALL NOT show `Exited EditStartNoteState` from fader1 echo during an active Move edit
- **AND** inbound F1 SHALL be ignored via `selectFaderFeedbackIgnoreUntilMs_` or value-echo check

#### Scenario: User F1 select during geometry edit commits and navigates

- **WHEN** the user moves fader1 to a different note while `NoteEditKind` is Move, Length, or Pitch
- **AND** inbound is not motor echo (outside ignore window or exceeds movement threshold)
- **THEN** firmware SHALL commit pending geometry edits and apply note select via `applySelectNav`
- **AND** `NoteEditKind` SHALL return to Select

#### Scenario: Geometry selection sync preserves moving note index

- **WHEN** `applySelectionFromGeometryEdit` updates `EditorSelection` from a geometry driver move
- **THEN** `syncGeometrySelectionToUi` updates bracket tick and requests display refresh
- **AND** `selectedNoteIdx` and the active geometry edit state (e.g. `EditStartNoteState`) are preserved

### Requirement: Outbound coordinate instrumentation (capture-serial)

Under `teensy41-capture-serial` build, the system SHALL emit `#DBG outbound_ctx` lines from `sendCoarseFaderPosition` with `anchor_tick`, `rel_tick`, `loop_start`, `loop_len`, `pb`, and `expected_pb_rel`.

#### Scenario: Position-mode coordinate pass gate

- **WHEN** position-mode F2 outbound runs with `loopStartTick ≠ 0`
- **THEN** every `#DBG outbound_ctx f2` line shows `pb == expected_pb_rel`
- **AND** `anchor_tick` used for send equals `rel_tick` after Phase 8 fix

### Requirement: Single motor trigger owner (Phase 12)

Each dependent fader stage SHALL emit at most one motor trigger per outbound pipeline step. Duplicate triggers from removed send wrappers SHALL be eliminated in Phase 12 (D20).

#### Scenario: One trigger per F2 stage after cleanup

- **WHEN** Phase 12 cleanup is complete and dependent refresh runs
- **THEN** each `SEND_F2` / parallel burst step emits exactly one motor cluster for that stage
- **AND** removed dead wrappers (`sendFaderUpdate`, `sendFaderPosition`, `performSelectnoteFaderUpdate`) have zero call sites

### Requirement: Dead outbound API removal (Phase 12)

Firmware SHALL NOT retain zero-caller NOTE_EDIT fader outbound wrappers listed in [design.md](../../design.md) D20 after Phase 12 ships.

#### Scenario: Native build after dead code removal

- **WHEN** Phase 12.1–12.4 tasks are complete
- **THEN** `pio test -e native` passes
- **AND** `Trigger::NoteSelectDependent` and `Trigger::Fader1BracketOnly` are removed from the live coordinator (tests updated)

### Requirement: EditorSelection motor geometry (Phase 13)

Dependent fader motor outbound SHALL resolve note pitch and position from `EditorSelection.primaryNote` when a note is selected. List index (`selectedNoteIdx`) SHALL be derived for display only and SHALL NOT be the sole motor-sync gate (D41; aligns with `note-edit-stable-note-id`).

#### Scenario: Same NoteId different filtered index

- **WHEN** filtered inventory rebuild shifts list index but `EditorSelection.primaryNote` is unchanged
- **THEN** live F1 path does not fire motor sync or `applySelectNav`
- **AND** motor send helpers do not use stale index-only geometry
