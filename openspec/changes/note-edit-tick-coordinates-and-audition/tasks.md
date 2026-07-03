# Tasks — note-edit-tick-coordinates-and-audition

## Tier 1 — tick coordinates + fader feedback

- [x] 1.1 `filterDisplayNotesByWindowInclusion` in `DisplayWindowUtils`
- [x] 1.2 `selectableDisplayNotesForEditUi` uses inclusion-only window filter
- [x] 1.3 Pitch resync via `syncSelectedNoteIdxToFilteredInventory` (remove `selectClosestNote` fallback)
- [x] 1.4 F3 position mode uses `noteRelativeTick` (Phase 10.1)
- [x] 1.5 F4 motor from `liveEditDisplayNoteAtSelect` when `focus.active`
- [x] 1.6 Geometry edit: suppress F2–F4 in `scheduleSelectDependentMotorSync`
- [x] 1.7 Native tests in `test_note_edit_fader_feedback`

## Tier 2 — session store playback audition

- [x] 2.1 `sessionPreviewRevision_` on `EditManager`
- [x] 2.2 Bump from `Track::invalidateCaches` when note edit active
- [x] 2.3 `ensurePlaybackWindowBuilt` uses session store when note edit active

## Tier 3 — unified geometry-dependent fader flow (F2–F4)

Implements deferred Phase 12 from `note-edit-fader-feedback-regression` plus stale-latch fix for F4 wrap regression (`captures/session_20260703_140021.log`). F1 (ch16 select) stays separate.

### 3.1 Snapshot builder

- [x] 3.1.1 Add `NoteEditDependentFaderSnapshot` + `buildDependentFaderSnapshot` in `include/Utils/NoteEditDependentFaderSnapshot.h` / `src/Utils/NoteEditDependentFaderSnapshot.cpp`
- [x] 3.1.2 Builder inputs: `lengthEditingMode`, `referenceStep`, `lengthFineAnchorEndTick`, empty-step (`selectedIdx < 0`), `loopStartTick` via `noteRelativeTick`, F4 from `liveEditDisplayNoteAtSelect` when `focus.active`
- [x] 3.1.3 Move length-edit mapping helpers into snapshot `.cpp` for native tests

### 3.2 Unified outbound

- [x] 3.2.1 Add `sendDependentFaderSnapshot(track, plan, snapshot, ValueOnly | ValueAndMotor)` — F2 pitchbend ch14, F3/F4 CC ch15
- [x] 3.2.2 Wire `processPendingSelectDependentMotorSync`, `processFaderOutbound` `SendCoarse`, `sendDependentFadersParallelTimedBurst`
- [x] 3.2.3 `motorValueChanged` compares F2 pitchbend + F3 fineCc + F4 noteValueCc vs prior `lastSent*`
- [x] 3.2.4 Preserve `armCoarseFaderFeedbackIgnore`, `armChannel15CcFaderFeedbackIgnore`, `armSelectDependentSettle` after motor sends

### 3.3 Unified inbound feedback

- [x] 3.3.1 Add `shouldIgnoreDependentFaderInput` — post-send grace, smart echo within `FEEDBACK_IGNORE_PERIOD`, **stale-latch** when `focus.active` (no early bypass after 1.5s)
- [x] 3.3.2 Wire from `handleFaderInput` for F2–F4 (preserve `selectDependentSettleUntilMs_`, `isChannel15OutboundStep`, post-select settle gates)

### 3.4 Latch publish

- [x] 3.4.1 Add `publishDependentFaderLatch` — `ValueOnly` snapshot after each F2/F3/F4 geometry mutation
- [x] 3.4.2 Call from `handleCoarseFaderInput`, `handleFineFaderInput`, `handleNoteValueFaderInput` post-mutation via `scheduleOtherFaderUpdates`

### 3.5 Length mode + NOTELEN

- [x] 3.5.1 `toggleLengthEditingMode` `LengthModeEnter` / `LengthModeExit` outbound through snapshot builder (NOTELEN spec)

### 3.6 Phase 12 cleanup

- [x] 3.6.1 Delete zero-caller wrappers: `sendFaderUpdate`, `sendFaderPosition`, `*FromSelectTarget`, `*TimedUpdate`, `plannedMotorSyncValuesFromSelectTarget`, `sendStartNotePitchbend`, `syncMotorsForDisplaySelection`, `drainDependentFaderOutboundUntilDone`, `sendSelectnoteFaderUpdate`, `performSelectnoteFaderUpdate`
- [x] 3.6.2 Thin `sendCoarseFaderPosition` / `sendFineFaderPosition` / `sendNoteValueFaderPosition` → snapshot + `ValueOnly`
- [ ] 3.6.3 Remove geometry F2–F4 suppress in `scheduleSelectDependentMotorSync` after latch publish ships (if redundant)

## Verification

- [x] `pio test -e native -f test_note_edit_fader_feedback` (Tier 1–2)
- [x] Native: snapshot builder, stale-latch ignore, length-mode snapshot, empty-step snapshot (Tier 3)
- [ ] `pio test -e native` after Phase 12 cleanup
- [ ] HITL: long-loop pitch edit through loop wrap — `session_20260703_140021.log` repro (F4 pitch must not revert at BAR wrap)
