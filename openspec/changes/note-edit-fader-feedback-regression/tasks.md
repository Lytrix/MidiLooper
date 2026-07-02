# Tasks — note-edit-fader-feedback-regression

**Status:** Phase 3 **shipped** (2026-06-30); HITL timing **PASS**; RC11 open — **Phase 8** next. Phase 8–12 OpenSpec doc reconciliation **complete**. Phase A NoteRef selection refactor **shipped** (2026-07-02, `d3d5798`) — see §7.16.

**Gate:** Phase 8 firmware before Phase 12 cleanup. Phase 12 MUST NOT start until Phase 8.3 capture passes (`pb == expected_pb_rel`). Run `pio test -e native` before push.

**Evidence:** [BUG.md](./BUG.md)  
**Design:** [design.md](./design.md) — live path: `requestFaderOutbound` / `processFaderOutbound` (Phase 3).  
**Phase A handoff:** [note_edit_stable_note_id_phase_a_handoff.md](../../../docs/plans/note_edit_stable_note_id_phase_a_handoff.md)

---

## 0. Spike / evidence

- [x] 0.1 Repro on hardware per [BUG.md](./BUG.md) manual steps; capture serial with `teensy41-capture-serial`.
- [x] 0.2 Grep capture for `Session fader sync`, `Skipping fader 2`, `Length editing mode ENABLED`, `LENGTH EDIT` — attach log path to BUG.md patch history.

> **Historical — Phase 1 implementation (§1–§6).** APIs below (`deferSelectFaderSyncToBracket`, `sessionFaderSyncStep_`, `isSessionFaderSyncActive`, `sendStartNotePitchbend`, `sendFaderUpdate`) were removed in Phase 3. Kept for traceability.

## 1. Scheduler dedupe and paired sync (RC1, RC2)

- [x] 1.1 `deferSelectFaderSyncToBracket`: cancel `pendingSelectnoteFaderUpdate` when session sync starts.
- [x] 1.2 `EditManager::syncNoteEditSessionStateToUi`: skip `sendSelectnoteFaderUpdate` when `noteEditManager.isSessionFaderSyncActive()` (add accessor if needed).
- [x] 1.3 Merge session sync steps 2+3: step 2 calls `sendStartNotePitchbend` (coarse+fine); step 3 sends fader4 note-value only.
- [x] 1.4 On sync complete (`sessionFaderSyncStep_ = 0`): ensure `startEditingEnabled` and run `enableStartEditing` fallback if coarse+fine were skipped.

## 2. Input gating (RC1, RC4)

- [x] 2.1 `handleFaderInput`: replace blanket `sessionFaderSyncStep_ != 0` return with D3 rules — block fader1 only during step 1 ignore; block ch15 during steps ≥2 until sync completes.
- [x] 2.2 Verify fader1 select stability thresholds still prevent motor echo after ignore expires.

## 3. Coarse feedback consistency (RC2, RC4)

- [x] 3.1 Route session sync coarse through `sendFaderUpdate(FADER_COARSE)` or set `lastSentTime` in `sendCoarseFaderPosition`.
- [x] 3.2 Ensure `sendStartNotePitchbend` / `performSelectnoteFaderUpdate` arm channel 15 ignore consistently.

## 4. NOTELEN coarse-first (RC3)

- [x] 4.1 `toggleLengthEditingMode(true)`: verify `sendFaderUpdate` coarse+fine order; add INFO log with anchor tick and pitchbend.
- [ ] 4.2 Manual verify: NOTELEN + full fader2 travel changes length ≥1 bar on 2-bar loop.

## 5. Tests

- [x] 5.1 Native: `test_note_edit_fader_feedback` — `lengthEditLoopTickToCoarsePitchbend` / `lengthEditCoarsePitchbendToLoopTick` round-trip; invariant "step 3 never without step 2" (header-level or documented sync step enum).
- [ ] 5.2 HITL: edit baseline serial gate — `Session fader sync: sent fader 2 coarse` after entry/select; NOTELEN length change threshold (TBD warn vs fail per design open question).

## 6. Verification and docs

- [x] 6.1 `pio test -e native` — all pass.
- [ ] 6.2 Regression: NOTE_EDIT entry from LOOP_EDIT — bracket not pulled by stale loop-edit fader2 (`d49e4c8` alignment).
- [ ] 6.3 Update [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md) active OpenSpec table when shipped; archive change after gates pass.

---

> **Historical — Phase 2 deferred refresh (§7–§11).** `DeferredRefresh` and 1000 ms stability timer superseded by Phase 3. Kept for traceability.

## Phase 2 — Deferred refresh pipeline (RC5)

**Status:** Superseded by Phase 3 (2026-06-30)  
**Design:** [design.md](./design.md) Phase 2 — D6–D11  
**Gate:** Implement §7–§8 before HITL §9. Run `pio test -e native` before push.

## 7. Deferred refresh scheduler

- [x] 7.1 Add `DeferredRefresh` state (`pending`, `executeAt`, `trigger`) to `NoteEditManager.h`
- [x] 7.2 Add `scheduleDependentFaderRefresh()`; replace immediate ch15 start in `sendNoteSelectFaderFeedback` with immediate F1 + deferred F2–F4
- [x] 7.3 `processDeferredRefresh()` in `update()`: honor `executeAt` when outbound idle
- [x] 7.4 `requestFaderOutbound`: coalesce NoteSelect when ch15 pipeline active instead of cancel

## 8. Non-preemptive policy

- [x] 8.1 Remove unconditional `cancelActiveFaderOutbound()` for NoteSelect with `skipFader1Bracket`
- [x] 8.2 Keep preempt for `LengthModeEnter` / `LengthModeExit` and `SessionOpen`
- [x] 8.3 Route `EditManager::applySelectNav` requestFaderSync through deferred scheduler
- [x] 8.4 Watchdog (`OUTBOUND_WATCHDOG_MS`) unchanged

## 9. Instrumentation + HITL

- [x] 9.1 `#DBG outbound_step` lines on state transitions (`SESSION_CAPTURE` build)
- [ ] 9.2 HITL: rapid fader1 multi-select → verify single complete F2/F3/F4 sequence after quiet
- [x] 9.3 Verify gates in `scripts/hitl/verify/note_edit_fader_select_refresh.py` + [HITL_TEST_SCENARIOS.md](../../docs/Guides/HITL_TEST_SCENARIOS.md)

## 10. Native tests (Phase 2)

- [x] 10.1 Test coalesce policy: NoteSelect while outbound active → defer, not cancel
- [x] 10.2 Test stability timer reset on repeated schedule calls

## 11. Phase 2 verification

- [x] 11.1 `pio test -e native` — all pass
- [ ] 11.2 Manual: fader1 across 3+ notes, wait 1 s, confirm F2/F3/F4 motors move
- [ ] 11.3 NOTELEN regression (Phase 1 task 4.2 still applies)
- [ ] 11.4 Update [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md) when Phase 2 shipped

**Phase 2 status:** Superseded by Phase 3 (diagnostic path + partial coordinator reverted on hardware).

---

## Phase 3 — Selection-driven outbound state machine (RC7–RC10)

**Status:** Implemented (2026-06-30)  
**Design:** [design.md](./design.md) Phase 3 — D12–D16  
**Gate:** §13–§16 before HITL §18–§19. Run `pio test -e native` before push.

## 12. OpenSpec reconciliation

- [x] 12.1 Add Phase 3 to proposal, design, BUG (RC7–RC10), spec deltas
- [x] 12.2 Mark Phase 2 superseded; document diagnostic path retirement in tasks + plan doc

## 13. Outbound state machine

- [x] 13.1 Restore `NoteEditFaderOutboundPlan.h` + `processFaderOutbound()` frame-stepped pipeline
- [x] 13.2 `requestFaderOutbound()` with coalesce (NoteSelect) and preempt (SessionOpen, LengthMode)
- [x] 13.3 PC re-arm before ch15 burst; `#DBG outbound_step` instrumentation

## 14. Slot-index navigation

- [x] 14.1 `handleSelectFaderInput`: apply selection on slot-index change only (no pitch deadband nav)
- [x] 14.2 `#DBG select_slot` on user-classified / ignored fader1 input

## 15. User-classified quiet gate

- [x] 15.1 `processFaderSelectQuiet()`: 400 ms user-classified quiet → re-apply + NoteSelectDependent
- [x] 15.2 Remove settle timer, immediate dependent burst, `enableStartEditing` outbound sends

## 16. Single outbound path

- [x] 16.1 Route session/GPIO/NOTELEN through `requestFaderOutbound` triggers
- [x] 16.2 `scheduleOtherFaderUpdates`: inline `sendFader1BracketFeedback` for FADER_SELECT driver

## 17. Native tests (Phase 3)

- [x] 17.1 Policy: slot-change apply, coalesce while pipeline active, quiet detection
- [x] 17.2 Plan flags per trigger (SessionOpen, NoteSelectDependent, LengthMode)

## 18. HITL (Phase 3)

- [x] 18.1 `scripts/hitl/verify/note_edit_fader_select_refresh.py` — capture gates (F2 within 3 s of F1 cluster, no 30 s gaps)
- [ ] 18.2 Manual: fast-then-slow + fader1-min scenarios on hardware

## 19. Capture verification

- [x] 19.1 Fresh capture after Phase 3 flash — HITL **PASS** on `session_20260630_191718` (0 clusters missing F2, max gap 3.1 s)

## 20. Phase 3 verification and docs

- [x] 20.1 `pio test -e native` — all pass (305/305)
- [ ] 20.2 NOTELEN regression (task 4.2)
- [ ] 20.3 Update [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md) when Phase 3 shipped

---

## Phase 7 — Bracket / send-path (partial)

**Design:** [design.md](./design.md) Phase 7 — D31–D38  
**Handoff:** [phase7 handoff](../../../docs/plans/note_edit_fader_feedback_phase7_handoff.md)

- [x] 7.1 D31 — Bracket-tick → F1 pitchbend (shipped)
- [x] 7.2 D36 — `commitBracketTickFromGeometry` session + legacy bracket (shipped)
- [x] 7.3 D37 — Geometry F1 feedback without touching nav state (shipped)
- [ ] 7.4 D34 — Send-path honesty + single motor trigger owner (partial — see §7.6)
- [ ] 7.5 D35 / D32 / D33 — Fine throttle, display refresh, rate-limit SEND_F1 (parked → Phase 11)

### 7.6 Dependent outbound tuning (unified handoff — 2026-06-30)

**Handoff:** [note_edit_fader_dependent_outbound_unified_handoff.md](../../../docs/plans/note_edit_fader_dependent_outbound_unified_handoff.md)  
**Base:** `3adb27b`

- [x] 7.6.1 Pace-skip removed from `paceDroidUsbHostBeforeSend`; LED bypass kept (`droidMotorOutboundPriority_`)
- [x] 7.6.2 `processFaderSelectQuiet`: skip duplicate refresh when slot/pb unchanged; log `QUIET_REFRESH` only when outbound scheduled
- [x] 7.6.3 D34 send-path honesty: send helpers return `bool`; pipeline skips `SEND_F*` + trigger on no-op
- [x] 7.6.4 D20 single motor trigger owner: pipeline `TriggerCoarse/Fine/NoteValue` only (removed inline triggers from send helpers)
- [x] 7.6.5 Capture verify: no duplicate `QUIET_REFRESH`; no trigger-only `SEND_F2/3/4`; one `MO,224,14` per F2 stage — **pre-fix baseline** `session_20260630_222821`; post-fix capture pending after flash

### 7.7 F2-drag F1 cross-talk (2026-06-30)

- [x] 7.7.1 Phase 9: `armSelectFaderFeedbackIgnore` at `SendCoarse` / `TriggerCoarse` / post-`DONE`
- [x] 7.7.2 Geometry driver lockout in `handleSelectFaderInput` + `applyNoteSelectFromFader1Pitchbend`
- [x] 7.7.3 D37: geometry `sendFader1BracketFeedback(track, false)` — no nav-state overwrite
- [x] 7.7.4 Rate-limit geometry F1 bracket send (150 ms)
- [ ] 7.7.5 Capture: zero `Select fader: selected note` during F2-only drag; DNTE continuous through session end

### 7.8 Selection-driven dependent refresh (stall-fix steps 1–4)

- [x] 7.8.1 Feedback geometry snapshot fields
- [x] 7.8.2 Dirty flags + `planForSelectDependent`
- [x] 7.8.3 Selection-driven schedule + coalesce re-eval at `DONE`
- [x] 7.8.4 Narrow `processFaderSelectQuiet` path
- [ ] 7.8.5 Capture: fast vs slow — document RC-E/RC-F; empty-step pass after RC-A

### 7.9 Empty-step position feedback (RC-A/B)

- [x] 7.9.1 `sendCoarseFaderPosition` / `sendFineFaderPosition` bracket anchor when `noteIdx < 0`
- [x] 7.9.2 `stampFeedbackPositionFromSelection` from bracket when empty
- [x] 7.9.3 Pipeline always `DONE`; `SKIP_SEND` capture log
- [ ] 7.9.4 Capture: `BEGIN` == `DONE`; empty steps show `SEND_F2` + `mode=EMPTY_STEP`

### 7.10 Option D aggressive refresh — superseded

**Superseded by:** [note_edit_fader_feedback_selection_driven_refactor.md](../../../docs/plans/note_edit_fader_feedback_selection_driven_refactor.md) (2026-07-01)  
**Prior handoff:** [note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md](../../../docs/plans/note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md)

Option D timing levers (quiet gate, stale-echo relax, dirty flags) replaced by selection-driven synchronous `sendDependentFaderFeedbackNow` on bracket/note change.

- [x] 7.10.1 **RC-D4** — Finish RC-B: every `BEGIN` reaches `DONE` (preempt/cancel paths)
- [x] 7.10.2 **RC-D1** — Reduce `kFader1QuietMs` (shipped; removed in 7.12)
- [x] 7.10.3 **RC-D2** — Stale-echo relax (shipped; removed in 7.12)
- [x] 7.10.4 **RC-D3** — N/A: dirty flags removed in 7.12 instead of rollback lever
- [ ] 7.10.5 Capture pass: max inter-`SEND_F2` gap < 1.0s; `clusters_missing_f2_within_3s` = 0
- [x] 7.10.6 **RC-D5** — Parked; superseded by 7.12 synchronous send

### 7.12 Selection-driven fader refresh (Plan A)

**Handoff:** [note_edit_fader_feedback_selection_driven_refactor.md](../../../docs/plans/note_edit_fader_feedback_selection_driven_refactor.md)

- [x] 7.12.1 `sendDependentFaderFeedbackNow` — synchronous F2+F3+F4 burst on selection change; no `armSelectFaderFeedbackIgnore` on dependent sends
- [x] 7.12.2 `handleSelectFaderInput` — `shouldApplySelectionOnTargetChange` gate → apply → `sendDependentFaderFeedbackNow`
- [x] 7.12.3 Remove grace/stale-echo lockout from `applyNoteSelectFromFader1Pitchbend`; simplify signature
- [x] 7.12.4 Remove `processFaderSelectQuiet`, `kFader1QuietMs`, `SelectPhase`, dirty flags, `NoteSelectDependent` trigger, coalesce
- [x] 7.12.5 Pipeline retained for `SessionOpen`, `NoteSelectWithFader1`, `LengthModeEnter/Exit`, `Fader1BracketOnly` only
- [x] 7.12.6 Native tests updated (`pio test -e native` pass)
- [x] 7.12.7 Capture: dwell motor gap = 0 on slow F1 sweep (`fader_select_dwell_gap_ok`); `phase_a_slow_fader_sweep_20260702_011229` — 59 slots, `select_ignored_rate=0`
- [x] 7.12.8 **Plan B fallback** — `NoteSelectDependent` pipeline for dependent refresh (replaces synchronous burst)
- [x] 7.12.9 **Plan C** — restart-on-selection (no `NoteSelectDependent` coalesce); delta partial plans; focus-aware `resolveNoteIdxAtSlot`
- [x] 7.12.10 **Sync drain** — `drainDependentFaderOutboundUntilDone` after `NoteSelectDependent` apply; `liveEditDisplayNoteAtSelect` for F4; `pendingOutboundPlan_` on idle drain
- [x] 7.12.11 **Same-tick sibling select** — `SelectNavigation::resolveNoteIdxAtSlot` trusts `slot.noteIdx`; slot-index apply gate; `same_tick_sibling` capture reason
- [x] 7.12.12 **Geometry driver override** — superseded: geometry block removed from F1 select (dwell-gap fix)
- [x] 7.12.13 **F4 duplicate diagnostic** — `#DBG outbound_ctx f4 duplicate=1` on unchanged CC reselect
- [ ] 7.12.14 Capture: fast adjacent-16th sweep after F4 burst — no geometry-block gap without override; `duplicate=1` rows on reselect
- [x] 7.12.15 **Nav-slot apply gate** — `lastAppliedSelectNavSlotIndex_` vs pitch `posIndex`; `reason=nav_slot`; FULL plan on every slot change; `resolveNoteIdxAtSlot` → `slot.noteIdx` only; `#DBG select_apply prior_slot=`
- [ ] 7.12.16 Capture: no `apply=0` when `prior_slot != slot` in adjacent-slot sweep

### 7.13 Dwell-gap fix (inline motor sync)

**Plan:** dwell-gap fix — rip fast-bypass layers + `syncMotorsFromSelectTarget`

- [x] 7.13.1 Remove geometry driver block from `handleSelectFaderInput`
- [x] 7.13.2 F1 `shouldIgnoreFaderInput` — echo-only (`NoteEditFaderSelectSync`); no time walls
- [x] 7.13.3 Remove F4→F1 1600 ms echo wall in `handleMidiPitchbend`
- [x] 7.13.4 `syncMotorsFromSelectTarget` on every accepted F1 pitchbend; demote live `NoteSelectDependent`
- [x] 7.13.5 Native tests: echo accept/reject, motor sync rate-limit (`pio test -e native`)
- [x] 7.13.6 Analyzer: `scripts/hitl/verify/fader_select_dwell_gap.py` + `analyze_fader2_select_feedback.py` integration
- [x] 7.13.7 Capture A: slow fader-1 sweep on 2+2 + second overdub loop — `fader_select_dwell_gap_ok`, `select_ignored_rate=0` (`run_phase_a_slow_fader_sweep.py`, 2026-07-02)
- [ ] 7.13.7b Capture B: fast adjacent-16th sweep; manual motor follow (see also 7.12.14)

### 7.13.8 Select-dependent settle window + forced motor sync

**Plan:** [`select_settle_window_fix`](../../.cursor/plans/select_settle_window_fix_c2960581.plan.md)

- [x] 7.13.8.1 `SELECT_DEPENDENT_SETTLE_MS` (450) + `selectDependentSettleUntilMs_` + `armSelectDependentSettle`
- [x] 7.13.8.2 `handleFaderInput` validation-only gate for F2/F3/F4 during settle (no move/pitch edits)
- [x] 7.13.8.3 `syncMotorsFromSelectTarget(track, target, forceSync)` — `forceSync` on `navChanged`; apply before sync
- [x] 7.13.8.4 Arm settle from `syncMotorsFromSelectTarget` + pipeline `SendCoarse`/`SendFine`/`SendNoteValue`
- [x] 7.13.8.5 Native tests: force-sync state mark, settle constant sanity (`pio test -e native`)
- [ ] 7.13.8.6 Capture: zero `moveNoteWithOverlapHandling` / F4 pitch edit within 450 ms of `SELECT_SYNC`

### 7.13.9 Note-changed-only apply + motor sync

**Plan:** [`motor_sync_skip_logging`](../../.cursor/plans/motor_sync_skip_logging_c477e9ea.plan.md)

- [x] 7.13.9.1 `shouldApplySelectionOnNoteChange` in `NoteEditFaderOutboundPlan.h`
- [x] 7.13.9.2 `handleSelectFaderInput` — note-only gate; empty `noteIdx=-1` ignored
- [x] 7.13.9.3 Remove live `forceSync` / `shouldSyncMotorsOnSelectTarget` from `syncMotorsFromSelectTarget`
- [x] 7.13.9.4 Native tests: note-only gate + empty ignore (`pio test -e native`)

### 7.13.10 Display-driven motor sync (remove step/slot gating)

- [x] 7.13.10.1 `syncMotorsForDisplaySelection` + hook in `EditManager::applySelectNav` when `displayIdx` changes
- [x] 7.13.10.2 `handleSelectFaderInput` — note-only apply; remove inline step/slot motor sync
- [x] 7.13.10.3 Remove `lastMotorSynced*`, `shouldSyncMotorsOnSelectTarget` from live path
- [x] 7.13.10.4 Native tests: display-driven gate (`pio test -e native`)
- [x] 7.13.10.5 Capture: snapshot-driven motor sync + same-bracket F4-only path; `fader_select_sibling_sync.py`

### 7.14 Select motor sync diagnostic logging

- [x] 7.14.1 `#DBG select_motor_sync` — `sent`, `reason=sent|unchanged_note|empty_step_ignored`
- [x] 7.14.2 `#DBG select_dependent_settle_block` — first per settle window
- [x] 7.14.3 Analyzer: `fader_select_dwell_gap.py` parses `select_motor_sync`; dwell gap only when `sent=1` without MO
- [ ] 7.14.4 Capture: 4-note slow glide on `session_20260701_160025` scenario — note crossings `sent=1`, crawl `unchanged_note`

### 7.15 Fader skip RCA — Phase 0–1 (MO→MI investigation)

**Plan:** fader skip RCA fix — Phase 0 + 1 only; Phase 2 deferred.

- [x] 7.15.1 `scripts/hitl/verify/fader_motor_echo_correlation.py` — MO→MI pairing + ch13 ack notes 80–87
- [x] 7.15.2 DROID `midilooper_v1.ini` — gatetool/quantizer/midiout ch13 motor-ack blocks
- [x] 7.15.3 `#DBG select_motor_sync` — `f2_pb`, `f4_cc`, `prior_f2_pb`, `prior_f4_cc`, `motor_value_changed`, `unchanged_motor_value` reason
- [x] 7.15.4 Analyzer: correlator in `analyze_fader2_select_feedback.py`; DNTE ±50 ms + MI hits in `fader_select_dwell_gap.py`
- [x] 7.15.5 BUG.md RC14 — correlator metrics on `164040` + `163558`
- [ ] 7.15.6 DROID Forge reload + re-capture 164040 — ch13 ack pairing within 20 ms of MO
- [ ] 7.15.7 Phase 2 hybrid motor gate — blocked on 7.15.6

### 7.16 Phase A NoteRef selection refactor (stable NoteId prerequisite)

**Shipped:** 2026-07-02 (`d3d5798`) — cross-change with [`note-edit-stable-note-id`](../note-edit-stable-note-id/) Phase A.  
**Handoff:** [note_edit_stable_note_id_phase_a_handoff.md](../../../docs/plans/note_edit_stable_note_id_phase_a_handoff.md)

- [x] 7.16.1 `shouldApplySelectionOnNoteRefChange` + `noteEditSelectionTargetChanged` — apply/motor sync gated on **NoteRef**, not list index alone
- [x] 7.16.2 Drop persisted `displayIdx` from `NoteEditSelection`; derive `selectedNoteIdx` via `NoteEditDisplaySnapshot`
- [x] 7.16.3 Windowed selectable inventory — `selectableDisplayNotesForEditUi` + `DisplayManager::resolveDetailedWindow`
- [x] 7.16.4 Encoder routing — `EditSelectNoteState` → `stepSelectNavSlot` → `applySelectNav`; remove `notesAtBracketTick`
- [x] 7.16.5 Native `test_note_edit_fader_feedback` — NoteRef gates, window filter, motor sync (`pio test -e native`)
- [x] 7.16.6 HITL pipeline — `run_phase_a_slow_fader_sweep.py` (base seed + LOOP_EDIT precondition + NOTE_EDIT enter + sweep); capture `phase_a_slow_fader_sweep_20260702_011229_serial.log`
- [x] 7.16.7 HITL helpers — `baseline_loop_inventory.py` (REVT+SEVT), `edit_mode_precondition.py`, `fader_select_sibling_sync.py`, `fader_motor_echo_correlation.py`

### 7.17 Phase B follow-up — F4 session entry + DisplayNote alignment

**Evidence:** `captures/phase_a_slow_fader_sweep_20260702_105038_serial.log` — duplicate f4 MO on NOTE_EDIT entry (`SELECT_SYNC` + SessionOpen `SEND_F4`); premature `startEditingEnabled` at outbound DONE.

- [x] 7.17.1 `DisplayNote.noteId` brace-init fix — `liveEditDisplayNoteAtSelect`, pitch targets (`EditPitchNoteState`, `handleNoteValueFaderInput`)
- [x] 7.17.2 `prepareNoteEditSessionOpen()` — suppress duplicate `SELECT_SYNC` f2/f3/f4 until SessionOpen outbound Done
- [x] 7.17.3 Defer `startEditingEnabled` until `NOTE_SELECTION_GRACE_PERIOD` after SessionOpen / `NoteSelectWithFader1` outbound (remove immediate enable at DONE)
- [x] 7.17.4 `syncReferenceStepFromBracketTick` on session open; correct fine CC on SessionOpen (was CC=127 from stale `referenceStep=0`)
- [x] 7.17.5 F4 post-select routing settle (`FEEDBACK_IGNORE_PERIOD`) in `handleNoteValueFaderInput`
- [x] 7.17.6 Arm f4 feedback ignore on `SEND_F4` + `SELECT_SYNC` sends (`armChannel15CcFaderFeedbackIgnore`)
- [x] 7.17.7 Native: `test_reference_step_from_bracket_tick`; `pio test -e native` 336 pass; firmware uploaded (`teensy41-capture-serial`)
- [ ] 7.17.8 Capture: single f4 MO on NOTE_EDIT entry after LOOP_EDIT f1 select; no spurious pitch edit within settle + grace

---

## Phase 8 — F2 loop-relative tick (RC11)

**Design:** [design.md](./design.md) D17  
**Gate:** §8.1–§8.3 before Phase 9. Run `pio test -e native` before push.

- [ ] 8.1 `noteRelativeTick` in `sendCoarseFaderPosition` (position mode start tick; length mode end tick) — **shipped** 2026-06-30
- [x] 8.2 Native test in `test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp` with `loopStartTick=424`
- [ ] 8.3 Capture: all `#DBG outbound_ctx f2` position-mode rows `pb == expected_pb_rel`
- [ ] 8.4 Manual: fader1 sweep with non-zero loop start — F2 motor matches note start

---

## Phase 9 — F1 ignore during F2 outbound

**Design:** [design.md](./design.md) D18

- [x] 9.1 Arm `selectFaderFeedbackIgnoreUntilMs_` at `processFaderOutbound` `SendCoarse` through `TriggerCoarse` + post-`DONE` tail
- [ ] 9.2 Capture: no spurious `#DBG select_slot` during `SEND_F2` / `TRIGGER_F2` window

---

## Phase 10 — F3/F4 unified dependent pipeline

**Design:** [design.md](./design.md) D19

- [ ] 10.1 `sendFineFaderPosition`: loop-relative tick for position-mode fine offset (mirror 8.1)
- [ ] 10.2 Native fine CC round-trip with `loopStartTick=424`
- [ ] 10.3 Optional `#DBG outbound_ctx_f3` capture line
- [ ] 10.4 One `NoteSelectDependent` → F2 + F3 + F4 MO lines; no fader3-only path
- [ ] 10.5 Manual: F3 motor aligned; if display freeze on heavy F3 → Phase 11 / D35

---

## Phase 11 — Parked (after Phases 8–10)

Only if still reproducing after coordinate fix:

- [ ] 11.1 D35 — Fine throttle + `requestNoteInfoRefresh` display refresh
- [ ] 11.2 D36/D37 bracket regression verify
- [ ] 11.3 NOTELEN tasks 4.2 / 6.2
- [ ] 11.4 D31 — Bracket-tick F1 pitchbend if offset remains after RC11 fix

---

## Phase 12 — Stale code cleanup (after 8.4)

**Design:** [design.md](./design.md) D20  
**Gate:** Do NOT start until Phase 8.3 capture passes.

- [ ] 12.1 Remove dead wrappers: `sendStartNotePitchbend`, `performSelectnoteFaderUpdate`, `sendFaderUpdate`, `sendFaderPosition`
- [ ] 12.2 Remove ghost state: `lastSelectnoteSentTime`, `PITCHBEND_IGNORE_PERIOD`, `NoteEditManager::faderHandler`, `faderProcessor`, `markFaderSent`
- [ ] 12.3 Single motor trigger owner — pipeline OR send helpers, not both (D34/D20) — **shipped** in §7.6.4; keep task until dead wrappers removed in 12.1
- [ ] 12.4 OpenSpec stale reference sweep (`deferSelectFaderSyncToBracket`, `sessionFaderSyncStep_`, `DeferredRefresh`, `isSessionFaderSyncActive`, `sendChannel15NotePositionFeedback`)

---

## Phase 8–12 doc reconciliation

- [x] 21.1 [BUG.md](./BUG.md) — RC11 + patch history (`session_20260630_191718`, `2ecf25d`)
- [x] 21.2 [design.md](./design.md) — context rewrite + D17–D20 + Phase 7
- [x] 21.3 [proposal.md](./proposal.md) — Phase 7–12 sections + Phase 3 HITL PASS
- [x] 21.4 [spec.md](./specs/note-edit-fader-feedback/spec.md) — 400 ms drift fix + new requirements
- [x] 21.5 [tasks.md](./tasks.md) — Phase 7–12 sections (this file)
