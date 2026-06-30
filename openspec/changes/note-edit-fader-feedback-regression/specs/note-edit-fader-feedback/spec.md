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
- **AND** fader1 has been stable for ≥1000 ms since the last select change
- **THEN** outbound feedback updates fader2 and fader3 to the new note start
- **AND** serial does not show fader3-only position sync without fader2 coarse for that selection

#### Scenario: Rapid fader1 select coalesces to final note

- **WHEN** the user moves fader1 across multiple notes within 1000 ms
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

During deferred session fader sync, the system SHALL block fader input only as needed to prevent motor-echo feedback. Fader1 input SHALL NOT remain blocked for the full multi-step sync chain after fader1 feedback has been sent and `FEEDBACK_IGNORE_PERIOD` has elapsed.

#### Scenario: Fader1 usable after its ignore window

- **WHEN** session fader sync step 1 has sent fader1 feedback and `FEEDBACK_IGNORE_PERIOD` has elapsed
- **AND** session sync step is ≥ 2 (ch15 feedback pending)
- **THEN** fader1 inbound SHALL be accepted (subject to normal select stability thresholds)
- **AND** ch15 faders MAY remain blocked until coarse+fine sync completes

#### Scenario: No duplicate schedulers on session open

- **WHEN** `deferSelectFaderSyncToBracket` starts session fader sync
- **THEN** any pending `sendSelectnoteFaderUpdate` SHALL be cancelled or suppressed
- **AND** `syncNoteEditSessionStateToUi` SHALL NOT schedule a competing selectnote update while `sessionFaderSyncStep_ != 0`

### Requirement: Coarse outbound feedback timestamps

All outbound fader2 coarse feedback paths (`sendCoarseFaderPosition`, session sync step 2, `sendStartNotePitchbend`, NOTELEN enable) SHALL update the coarse fader state's `lastSentTime` and `lastSentPitchbend` consistently so smart feedback ignore operates correctly.

#### Scenario: Coarse lastSentTime after session sync

- **WHEN** session fader sync sends fader2 coarse
- **THEN** `midiFaderManager` coarse fader state `lastSentTime` is set to the send time
- **AND** channel 15 group ignore is armed via `armChannel15FaderFeedbackIgnore` or equivalent

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
