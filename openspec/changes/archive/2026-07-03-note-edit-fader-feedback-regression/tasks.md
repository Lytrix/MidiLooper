# Tasks — note-edit-fader-feedback-regression

**Status:** **Bug-fix scope complete** (2026-07-03). HITL `note_edit_select_dependent_faders` **PASS** — `captures/host_midi_hitl_note_edit_select_dependent_faders_20260703_122216.json` (60 clusters, `motor_misses=0`, perceptual F2/F4 80%/88%). §7.24 geometry guard shipped (`3348857`). **Ready to archive** — Phase 12–13 dead-code cleanup + RC11 §8.3–8.4 deferred post-archive.

**Gate:** Phase **12.1–12.4** (zero-caller / test-only helper removal): may start when `pio test -e native` is green — no behavior change. Phase **12.3** (dead outbound triggers) + **12.5** (verification): after §12.1–12.2 land. Phase **13** (index → `EditorSelection` migration): after §12.1–12.4 minimum; coordinate with `note-edit-stable-note-id`. Phase 8.3 capture still gates **8.4** firmware validation, not §12.1 dead-wrapper removal.

**Evidence:** [BUG.md](./BUG.md) — § Stale code inventory (Phase 12–13)  
**Design:** [design.md](./design.md) — live path: `requestFaderOutbound` / `processFaderOutbound` (Phase 3).  
**Phase A handoff:** [note_edit_stable_note_id_phase_a_handoff.md](../../../docs/Plans/note_edit_stable_note_id_phase_a_handoff.md)

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
- [ ] 4.2 Manual verify: NOTELEN + full fader2 travel changes length ≥1 bar on 2-bar loop — **parked** Phase 11 §11.3

## 5. Tests

- [x] 5.1 Native: `test_note_edit_fader_feedback` — `lengthEditLoopTickToCoarsePitchbend` / `lengthEditCoarsePitchbendToLoopTick` round-trip; invariant "step 3 never without step 2" (header-level or documented sync step enum).
- [x] 5.2 HITL: edit baseline serial gate — **superseded** §7.23.6 preset PASS 2026-07-03

## 6. Verification and docs

- [x] 6.1 `pio test -e native` — all pass.
- [x] 6.2 Regression: NOTE_EDIT entry from LOOP_EDIT — bracket not pulled by stale loop-edit fader2 — **PASS** §7.17.8 (single F4 MO on entry; no spurious pitch edit)
- [x] 6.3 Update [PROJECT_STATE.md](../../docs/Runtime/PROJECT_STATE.md) active OpenSpec table when shipped; archive change after §7.18.5 / §8.3 gates pass — **bug-fix gates pass** 2026-07-03; RC11 §8.3–8.4 deferred; archive when user runs `/opsx:archive`

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
- [x] 9.2 HITL: rapid fader1 multi-select → verify single complete F2/F3/F4 sequence after quiet — **superseded** §7.23.6 preset PASS 2026-07-03
- [x] 9.3 Verify gates in `scripts/hitl/verify/note_edit_fader_select_refresh.py` + [HITL_TEST_SCENARIOS.md](../../docs/Guides/HITL_TEST_SCENARIOS.md)

## 10. Native tests (Phase 2)

- [x] 10.1 Test coalesce policy: NoteSelect while outbound active → defer, not cancel
- [x] 10.2 Test stability timer reset on repeated schedule calls

## 11. Phase 2 verification

- [x] 11.1 `pio test -e native` — all pass
- [x] 11.2 Manual: fader1 across 3+ notes, wait 1 s, confirm F2/F3/F4 motors move — **superseded** §7.23.6 preset PASS 2026-07-03
- [ ] 11.3 NOTELEN regression (Phase 1 task 4.2 still applies) — **parked** Phase 11
- [x] 11.4 Update [PROJECT_STATE.md](../../docs/Runtime/PROJECT_STATE.md) when Phase 2 shipped — **N/A** (Phase 2 superseded; PROJECT_STATE updated for Phase 3 + §7.24)

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
- [x] 18.2 Manual: fast-then-slow + fader1-min scenarios on hardware — **superseded** §7.23.6 (59-slot sweep + toggle PASS 2026-07-03)

## 19. Capture verification

- [x] 19.1 Fresh capture after Phase 3 flash — HITL **PASS** on `session_20260630_191718` (0 clusters missing F2, max gap 3.1 s)

## 20. Phase 3 verification and docs

- [x] 20.1 `pio test -e native` — all pass (305/305)
- [ ] 20.2 NOTELEN regression (task 4.2) — **parked** Phase 11
- [x] 20.3 Update [PROJECT_STATE.md](../../docs/Runtime/PROJECT_STATE.md) when Phase 3 shipped — updated 2026-07-02 (Phase 3 + §7.24; archive pending §6.3)

---

## Phase 7 — Bracket / send-path (partial)

**Design:** [design.md](./design.md) Phase 7 — D31–D38  
**Handoff:** [phase7 handoff](../../../docs/Plans/note_edit_fader_feedback_phase7_handoff.md)

- [x] 7.1 D31 — Bracket-tick → F1 pitchbend (shipped)
- [x] 7.2 D36 — `commitBracketTickFromGeometry` session + legacy bracket (shipped)
- [x] 7.3 D37 — Geometry F1 feedback without touching nav state (shipped)
- [x] 7.4 D34 — Send-path honesty + single motor trigger owner — **shipped** §7.6.3–7.6.4
- [ ] 7.5 D35 / D32 / D33 — Fine throttle, display refresh, rate-limit SEND_F1 (parked → Phase 11)

### 7.6 Dependent outbound tuning (unified handoff — 2026-06-30)

**Handoff:** [note_edit_fader_dependent_outbound_unified_handoff.md](../../../docs/Plans/note_edit_fader_dependent_outbound_unified_handoff.md)  
**Base:** `3adb27b`

- [x] 7.6.1 Pace-skip removed from `paceDroidUsbHostBeforeSend`; LED bypass kept (`droidMotorOutboundPriority_`)
- [x] 7.6.2 `processFaderSelectQuiet`: skip duplicate refresh when slot/pb unchanged; log `QUIET_REFRESH` only when outbound scheduled
- [x] 7.6.3 D34 send-path honesty: send helpers return `bool`; pipeline skips `SEND_F*` + trigger on no-op
- [x] 7.6.4 D20 single motor trigger owner: pipeline `TriggerCoarse/Fine/NoteValue` only (removed inline triggers from send helpers)
- [x] 7.6.5 Capture verify: no duplicate `QUIET_REFRESH`; no trigger-only `SEND_F2/3/4`; one `MO,224,14` per F2 stage — **superseded** (quiet pipeline removed §7.12.4; motor path is §7.23 debounced `SELECT_SYNC`)

### 7.7 F2-drag F1 cross-talk (2026-06-30)

- [x] 7.7.1 Phase 9: `armSelectFaderFeedbackIgnore` at `SendCoarse` / `TriggerCoarse` / post-`DONE`
- [x] 7.7.2 Geometry driver lockout in `handleSelectFaderInput` + `applyNoteSelectFromFader1Pitchbend`
- [x] 7.7.3 D37: geometry `sendFader1BracketFeedback(track, false)` — no nav-state overwrite
- [x] 7.7.4 Rate-limit geometry F1 bracket send (150 ms)
- [x] 7.7.5 Capture: zero `Select fader: selected note` during F2-only drag — **superseded** §7.24 echo guard + smoke PASS `session_20260702_222845`

### 7.8 Selection-driven dependent refresh (stall-fix steps 1–4)

- [x] 7.8.1 Feedback geometry snapshot fields
- [x] 7.8.2 Dirty flags + `planForSelectDependent`
- [x] 7.8.3 Selection-driven schedule + coalesce re-eval at `DONE`
- [x] 7.8.4 Narrow `processFaderSelectQuiet` path
- [x] 7.8.5 Capture: fast vs slow — **superseded** §7.23.6 preset PASS 2026-07-03

### 7.9 Empty-step position feedback (RC-A/B)

- [x] 7.9.1 `sendCoarseFaderPosition` / `sendFineFaderPosition` bracket anchor when `noteIdx < 0`
- [x] 7.9.2 `stampFeedbackPositionFromSelection` from bracket when empty
- [x] 7.9.3 Pipeline always `DONE`; `SKIP_SEND` capture log
- [x] 7.9.4 Capture: `BEGIN` == `DONE`; empty steps show `SEND_F2` + `mode=EMPTY_STEP` — **superseded** §7.23.6 (empty-step motor sync in sweep log)

### 7.10 Option D aggressive refresh — superseded

**Superseded by:** [note_edit_fader_feedback_selection_driven_refactor.md](../../../docs/Plans/note_edit_fader_feedback_selection_driven_refactor.md) (2026-07-01)  
**Prior handoff:** [note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md](../../../docs/Plans/note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md)

Option D timing levers (quiet gate, stale-echo relax, dirty flags) replaced by selection-driven synchronous `sendDependentFaderFeedbackNow` on bracket/note change.

- [x] 7.10.1 **RC-D4** — Finish RC-B: every `BEGIN` reaches `DONE` (preempt/cancel paths)
- [x] 7.10.2 **RC-D1** — Reduce `kFader1QuietMs` (shipped; removed in 7.12)
- [x] 7.10.3 **RC-D2** — Stale-echo relax (shipped; removed in 7.12)
- [x] 7.10.4 **RC-D3** — N/A: dirty flags removed in 7.12 instead of rollback lever
- [x] 7.10.5 Capture pass: max inter-`SEND_F2` gap — **superseded** §7.23.6 preset PASS 2026-07-03
- [x] 7.10.6 **RC-D5** — Parked; superseded by 7.12 synchronous send

### 7.12 Selection-driven fader refresh (Plan A)

**Handoff:** [note_edit_fader_feedback_selection_driven_refactor.md](../../../docs/Plans/note_edit_fader_feedback_selection_driven_refactor.md)

- [x] 7.12.1 `sendDependentFaderFeedbackNow` — synchronous F2+F3+F4 burst on selection change; no `armSelectFaderFeedbackIgnore` on dependent sends
- [x] 7.12.2 `handleSelectFaderInput` — `shouldApplySelectionOnTargetChange` gate → apply → `sendDependentFaderFeedbackNow`
- [x] 7.12.3 Remove grace/stale-echo lockout from `applyNoteSelectFromFader1Pitchbend`; simplify signature
- [x] 7.12.4 Remove `processFaderSelectQuiet`, `kFader1QuietMs`, `SelectPhase`, dirty flags, `NoteSelectDependent` trigger, coalesce
- [x] 7.12.5 Pipeline retained for `SessionOpen`, `NoteSelectWithFader1`, `LengthModeEnter/Exit`, `Fader1BracketOnly` only
- [x] 7.12.6 Native tests updated (`pio test -e native` pass)
- [x] 7.12.7 Capture: dwell motor gap = 0 on slow F1 sweep (`fader_select_dwell_gap_ok`); `phase_a_slow_fader_sweep_20260702_011229` — 59 slots, `select_ignored_rate=0`
- [x] 7.12.8 **Plan B fallback** — `NoteSelectDependent` pipeline for dependent refresh (replaces synchronous burst)
- [x] 7.12.9 **Plan C** — restart-on-selection (no `NoteSelectDependent` coalesce); delta partial plans; focus-aware `resolveNoteIdxAtSlot`
- [x] 7.12.10 **Sync drain** — **superseded dead code** (zero callers); removal §12.1.3. Historical: drain after `NoteSelectDependent` apply.
- [x] 7.12.11 **Same-tick sibling select** — `SelectNavigation::resolveNoteIdxAtSlot` trusts `slot.noteIdx`; slot-index apply gate; `same_tick_sibling` capture reason
- [x] 7.12.12 **Geometry driver override** — superseded: geometry block removed from F1 select (dwell-gap fix)
- [ ] 7.12.13 **F4 duplicate diagnostic** — **cancelled** (`duplicate=1` not implemented; drop in Phase 12 doc sweep or add if still needed)
- [x] 7.12.14 Capture: fast adjacent-16th sweep — **superseded** §7.23.6 preset PASS 2026-07-03
- [x] 7.12.15 **Nav-slot apply gate** — **superseded** by NoteId apply gate §7.16.1 (`reason=note_changed`); `prior_slot` logging wired (`logSelectApplyDecision`)
- [x] 7.12.16 Capture: no `apply=0` when `prior_slot != slot` — **superseded** NoteId gate + §7.23.6 (`select_ignored_rate` 2.8%)

### 7.13 Dwell-gap fix (inline motor sync)

**Plan:** dwell-gap fix — rip fast-bypass layers + `syncMotorsFromSelectTarget`

- [x] 7.13.1 Remove geometry driver block from `handleSelectFaderInput`
- [x] 7.13.2 F1 `shouldIgnoreFaderInput` — echo-only (`NoteEditFaderSelectSync`); no time walls
- [x] 7.13.3 Remove F4→F1 1600 ms echo wall in `handleMidiPitchbend`
- [x] 7.13.4 `syncMotorsFromSelectTarget` on every accepted F1 pitchbend — **superseded path** fixed §7.18.1 + §7.21.1 (`requestFaderSync=false` + debounced `scheduleSelectDependentMotorSync` §7.23)
- [x] 7.13.5 Native tests: echo accept/reject, motor sync rate-limit (`pio test -e native`)
- [x] 7.13.6 Analyzer: `scripts/hitl/verify/fader_select_dwell_gap.py` + `analyze_fader2_select_feedback.py` integration
- [x] 7.13.7 Capture A: slow fader-1 sweep on 2+2 + second overdub loop — `fader_select_dwell_gap_ok`, `select_ignored_rate=0` (`run_phase_a_slow_fader_sweep.py`, 2026-07-02)
- [x] 7.13.7b Capture B: fast adjacent-16th sweep — **superseded** §7.23.6 preset PASS 2026-07-03

### 7.13.8 Select-dependent settle window + forced motor sync

**Plan:** [`select_settle_window_fix`](../../.cursor/plans/select_settle_window_fix_c2960581.plan.md)

- [x] 7.13.8.1 `SELECT_DEPENDENT_SETTLE_MS` (450) + `selectDependentSettleUntilMs_` + `armSelectDependentSettle`
- [x] 7.13.8.2 `handleFaderInput` validation-only gate for F2/F3/F4 during settle (no move/pitch edits)
- [x] 7.13.8.3 `syncMotorsFromSelectTarget(track, target, forceSync)` — `forceSync` on `navChanged`; apply before sync
- [x] 7.13.8.4 Arm settle from `syncMotorsFromSelectTarget` + pipeline `SendCoarse`/`SendFine`/`SendNoteValue`
- [x] 7.13.8.5 Native tests: force-sync state mark, settle constant sanity (`pio test -e native`)
- [x] 7.13.8.6 Capture: zero move/pitch edit within settle — **superseded** §7.23.6 preset PASS 2026-07-03

### 7.13.9 Note-changed-only apply + motor sync

**Plan:** [`motor_sync_skip_logging`](../../.cursor/plans/motor_sync_skip_logging_c477e9ea.plan.md)

- [x] 7.13.9.1 `shouldApplySelectionOnNoteChange` in `NoteEditFaderOutboundPlan.h`
- [x] 7.13.9.2 `handleSelectFaderInput` — note-only gate; empty `noteIdx=-1` ignored
- [x] 7.13.9.3 Remove live `forceSync` / `shouldSyncMotorsOnSelectTarget` from `syncMotorsFromSelectTarget`
- [x] 7.13.9.4 Native tests: note-only gate + empty ignore (`pio test -e native`)

### 7.13.10 Display-driven motor sync (remove step/slot gating)

- [x] 7.13.10.1 `syncMotorsForDisplaySelection` + hook in `applySelectNav` — **superseded** §7.23.2 `scheduleSelectDependentMotorSync`; dead wrapper removal §12.1.2
- [x] 7.13.10.2 `handleSelectFaderInput` — note-only apply; remove inline step/slot motor sync
- [x] 7.13.10.3 Remove `lastMotorSynced*`, `shouldSyncMotorsOnSelectTarget` from live path
- [x] 7.13.10.4 Native tests: display-driven gate (`pio test -e native`)
- [x] 7.13.10.5 Capture: snapshot-driven motor sync + same-bracket F4-only path — **superseded** §7.18.2 full F2+F3+F4 on `NoteId` change; verify via §7.18.5 / §7.23.6

### 7.14 Select motor sync diagnostic logging

- [x] 7.14.1 `#DBG select_motor_sync` — `sent`, `reason=sent|unchanged_note|empty_step_ignored`
- [x] 7.14.2 `#DBG select_dependent_settle_block` — first per settle window
- [x] 7.14.3 Analyzer: `fader_select_dwell_gap.py` parses `select_motor_sync`; dwell gap only when `sent=1` without MO
- [x] 7.14.4 Capture: 4-note slow glide — **superseded** §7.23.6 preset PASS 2026-07-03

### 7.15 Fader skip RCA — Phase 0–1 (MO→MI investigation)

**Plan:** fader skip RCA fix — Phase 0 + 1 only; Phase 2 deferred.

- [x] 7.15.1 `scripts/hitl/verify/fader_motor_echo_correlation.py` — MO→MI pairing + ch13 ack notes 80–87
- [x] 7.15.2 DROID `midilooper_v1.ini` — gatetool/quantizer/midiout ch13 motor-ack blocks
- [x] 7.15.3 `#DBG select_motor_sync` — `f2_pb`, `f4_cc`, `prior_f2_pb`, `prior_f4_cc`, `motor_value_changed`, `unchanged_motor_value` reason
- [x] 7.15.4 Analyzer: correlator in `analyze_fader2_select_feedback.py`; DNTE ±50 ms + MI hits in `fader_select_dwell_gap.py`
- [x] 7.15.5 BUG.md RC14 — correlator metrics on `164040` + `163558`
- [x] 7.15.6 DROID Forge reload + re-capture — **satisfied** §7.23.6 (`ack_misses=0` on triple motor verifier; ch13 blocks per §7.15.2)
- [x] 7.15.7 Phase 2 hybrid motor gate — **cancelled** (not needed; §7.23 debounced burst PASS)

### 7.16 Phase A NoteRef selection refactor (stable NoteId prerequisite)

**Shipped:** 2026-07-02 (`d3d5798`) — cross-change with [`note-edit-stable-note-id`](../note-edit-stable-note-id/) Phase A.  
**Handoff:** [note_edit_stable_note_id_phase_a_handoff.md](../../../docs/Plans/note_edit_stable_note_id_phase_a_handoff.md)

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
- [x] 7.17.8 Capture: single f4 MO on NOTE_EDIT entry after LOOP_EDIT f1 select; no spurious pitch edit within settle + grace — user HITL sweep PASS post-upload

### 7.18 EditorSelection motor sync wiring (capture regression 2026-07-02)

**Evidence:** [`captures/session_20260702_114249.log`](../../../captures/session_20260702_114249.log) — 0× `mode=SELECT_SYNC`, 0× `select_motor_sync`, 325× `outbound_step=BEGIN` on live F1 select; `select_ignored_rate=0.53`.  
**Plan:** [fader_feedback_analysis plan](../../../.cursor/plans/fader_feedback_analysis_c3b1e88b.plan.md)  
**Root cause:** `applyNoteSelectFromFader1Pitchbend` calls `applySelectNav(..., requestFaderSync=true)` → pipeline `NoteSelectWithFader1` instead of `syncMotorsForDisplaySelection` (`EditorSelection` path).

**Implement:**

- [x] 7.18.1 `applyNoteSelectFromFader1Pitchbend` → `applySelectNav(..., requestFaderSync=false)` for live F1 note select; keep `requestFaderSync=true` for GPIO / session entry only
- [x] 7.18.2 `planForSelectDependentFromNoteIdChange` — full F2+F3+F4 (`coarse`+`fine`+`noteValue`) on any `EditorSelection.primaryNote` change, including same `bracketTick`
- [x] 7.18.3 F3 loop-relative tick in `syncMotorsFromSelectTarget` (`noteRelativeTick` for fine offset; mirror §10.1 inline path)
- [x] 7.18.4 Native: same-bracket `NoteId` change → full motor plan; `requestFaderSync=false` path (`pio test -e native` 336 pass)
- [x] 7.18.5 Capture: re-run slow-sweep scenario — **PASS** `note_edit_select_dependent_faders` preset 2026-07-03 (`host_midi_hitl_note_edit_select_dependent_faders_20260703_122216.json`; §7.18.8 matrix via triple motor verifier)
- [x] 7.18.6 Close [`note-edit-stable-note-id`](../note-edit-stable-note-id/) §7.3 HITL gate — **closed** 2026-07-03 (same capture)
- [x] 7.18.7 Reconcile falsely `[x]` tasks (audit 2026-07-02) — **done 2026-07-02:**
  - §7.13.4, §7.13.10.1 — superseded by §7.18.1 + §7.23 (notes on tasks above)
  - §7.13.7 manual slow sweep — automated PASS §7.16.6; §7.18.5 adds ch13 triple-fader matrix
  - §7.13.10.5 — superseded §7.18.2
  - §7.12.10 — dead code; Phase 12.1.3
  - §7.12.13 — cancelled (not in firmware)
  - §7.12.15 `prior_slot` — wired in `logSelectApplyDecision`; NoteId gate is live apply owner
  - §9.1 — outbound arm at `SendCoarse`; inbound `selectFaderFeedbackIgnoreUntilMs_` on geometry path §7.24.2

#### 7.18.8 Manual capture acceptance — select move + ch13 triple-fader proof

**Purpose:** After §7.18.1–7.18.4 ship, prove every **`EditorSelection.primaryNote`** change (`apply=1 reason=note_changed`) drives **F2 + F3 + F4** motor outbound and DROID **ch13** set-changed acks — not F1 (user-driven select fader).

**Prerequisite:** DROID `midilooper_v1.ini` motor-ack blocks on ch13 loaded (§7.15.2). Build **`teensy41-capture-serial`**.

**Manual capture procedure**

1. Terminal A — serial capture:

```bash
.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801
```

2. Hardware — enter **NOTE_EDIT** on a loop with **≥10 distinct notes** (include **≥1 same-bracket sibling pair** if available).
3. **Slow** continuous F1 select sweep: left → right, pause ~0.5 s per note, then right → left. Do **not** touch F2/F3/F4.
4. Ctrl+C capture → save as `captures/session_<date>_noteid_triple_motor_acceptance.log`.

**Analyze**

```bash
.venv/bin/python scripts/analyze_fader2_select_feedback.py captures/session_<date>_noteid_triple_motor_acceptance.log
# after §7.18.9 lands:
.venv/bin/python scripts/hitl/verify/note_edit_select_triple_motor_ack.py captures/session_<date>_noteid_triple_motor_acceptance.log
```

**Pass matrix (all required)**

| # | Gate | Target |
|---|------|--------|
| A | Live path | `mode=SELECT_SYNC` on every `apply=1 reason=note_changed`; **zero** `outbound_step=BEGIN` between session entry and session exit (except `SessionOpen` at NOTE_EDIT enter) |
| B | Motor sync log | `#DBG select_motor_sync sent=1` on every `apply=1 reason=note_changed` within **50 ms** |
| C | F2 outbound | `MO,224,14` pitchbend change within **300 ms** of each `apply=1` (distinct value when bracket changes; **re-send** even when value unchanged on same-bracket `NoteId` change per 7.18.2) |
| D | F3 outbound | `MO,176,15,2,<cc>` within **300 ms** of each `apply=1` (same re-send rule as C) |
| E | F4 outbound | `MO,176,15,3,<cc>` within **300 ms** of each `apply=1`; CC matches selected note pitch |
| F | ch13 F2 ack | Within **20 ms** of each F2 `MO,224,14`: `MI,H,144,13,83,127` (set_changed) |
| G | ch13 F3 ack | Within **20 ms** of each F3 `MO,176,15,2`: `MI,H,144,13,85,127` (set_changed) |
| H | ch13 F4 ack | Within **20 ms** of each F4 `MO,176,15,3`: `MI,H,144,13,87,127` (set_changed); clear **86** may precede |
| I | F1 echo | `select_ignored_rate` ≤ **0.05** (echo-only); no sustained `ignored=1 reason=echo` clusters during slow user sweep |
| J | Sibling | Same-bracket `NoteId` switches: gates C–H pass (all three motors + ch13 acks, not F4-only) |
| K | Negative | `apply=0 reason=unchanged_note` rows: **no** `select_motor_sync sent=1` and **no** F2/F3/F4 MO burst |

**ch13 ack map (NOTE_EDIT)** — from `fader_motor_echo_correlation.py`:

| Fader | clear | set_changed |
|-------|------:|------------:|
| F2 coarse | 82 | 83 |
| F3 fine | 84 | 85 |
| F4 note value | 86 | 87 |

**Manual spot-check (if verifier not yet shipped)**

```bash
rg '#DBG select_apply.*apply=1 reason=note_changed' captures/<session>.log | wc -l
rg 'mode=SELECT_SYNC' captures/<session>.log | wc -l   # must equal apply=1 count
rg 'outbound_step=BEGIN' captures/<session>.log        # SessionOpen only
rg '#DBG select_motor_sync sent=1' captures/<session>.log | wc -l
```

Per `apply=1` timestamp, confirm within 300 ms: `MO,224,14` + `MO,176,15,2` + `MO,176,15,3` and ch13 notes **83, 85, 87**.

- [x] 7.18.9 Verifier: `scripts/hitl/verify/note_edit_select_triple_motor_ack.py` — per-`note_changed` gates A–K; wired into `analyze_fader2_select_feedback.py`; pre-fix `session_20260702_114249` correctly FAIL

### 7.19 Motor trigger predelay (DROID USB round-trip — 2026-07-02)

**Evidence:** `session_20260702_142309` — ch13 acks lag MO ~11 ms in host recordings (verifier window only, not firmware delay). Firmware `BURST` (~1 ms) between pitch/CC and trigger is correct for DROID; HITL probe `PITCH_NOTE_OFF` (20 ms) remains a conservative lab default.

**Implement:**

- [x] 7.19.1 `kCh13AckCorrelationWindowMs = 11` — capture/log ack pairing only (not firmware delay)
- [x] 7.19.2 Reverted firmware ms waits between pitch/CC and notegate trigger
- [x] 7.19.3 Reverted: §7.21 restores PC **before** burst (not after F2+F3+F4)
- [x] 7.19.4 Native tests updated (`pio test -e native`)
- [x] 7.19.5 Capture acceptance after corrected timing model — **PASS** §7.23.6 2026-07-03
- [x] 7.19.6 Verifier: ch13 ack paired on notegate MO — **satisfied** §7.18.9 cluster verifier (`ack_misses=0` on PASS capture)

### 7.20 DROID selectAt batch arm after F2+F3+F4 (2026-07-02) — **reverted**

**Evidence:** `session_20260702_151419` — PC-after-burst + resync queue: F2 lagged F1; only 38/174 selects applied motors; 52% perceptual F2 vs §7.18 `142309` era.

- [x] 7.20.1 Queue coalesce — **removed** in §7.21
- [x] 7.20.2 PC ch16 after burst — **failed** hypothesis; reverted
- [x] 7.20.3 Remove `kMotorTriggerPredelayMs` firmware waits (11 ms is verifier-only per user)
- [x] 7.20.4 Capture disproved PC-after-sequence (`session_20260702_151419`)

### 7.21 Revert to §7.18 synchronous motor sync (2026-07-02)

**Target:** `session_20260702_142309` behavior — `requestFaderSync=false` + `SELECT_SYNC` + **one PC ch16 before** F2/F3/F4 pitch→trigger burst; no async resync queue; no firmware ms delays.

- [x] 7.21.1 Restore `syncMotorsFromSelectTarget` — `armNoteEditDroidMotorBank()` first, then synchronous F2/F3/F4 send+trigger
- [x] 7.21.2 Remove `startDisplaySelectMotorSync` / resync queue / PC-after-burst
- [x] 7.21.3 Keep §7.18.2–7.18.4 (`NoteId` full plan, `noteRelativeTick` F3, `applySelectNav(..., false)`)
- [x] 7.21.4 `pio test -e native` — 337 pass
- [x] 7.21.5 Capture acceptance — slow F1 sweep; compare to `142309`
- [x] 7.21.6 Batch `selectAt` — **failed** `152946` (F2/F3 0% MI echo; F4 only partial)
- [x] 7.21.7 Capture: two-note alternation — **PASS** toggle slots 0↔1 in §7.23.6 preset 2026-07-03
- [x] 7.21.8 SessionOpen sequence + PC **127→1** reset prelude (`reset_arm=127_1 sequence=session_open`)
- [x] 7.21.9 Capture: validate 127→1 vs LOOP↔NOTE switch — **superseded** SessionOpen shipped §7.21.8; no regression in §7.23.6 sweep

### 7.22 HITL `note_edit_select_dependent_faders` (Phase 1 — host scenario)

**Plan:** [`docs/Plans/note_edit_select_dependent_faders_hitl_enhancement.md`](../../../docs/Plans/note_edit_select_dependent_faders_hitl_enhancement.md)

- [x] 7.22.1 Preset chain `base` → `note_edit_select_dependent_faders` (runner abort if base fails)
- [x] 7.22.2 Verifier `scripts/hitl/verify/note_edit_select_dependent_faders.py` — composes triple_motor_ack + echo + outbound value gates
- [x] 7.22.3 Host unit test `scripts/test_note_edit_select_dependent_faders_serial_verify.py`
- [x] 7.22.4 Preset `note_edit_select_dependent_faders` in `scripts/hitl/registry.py`; catalog row in `HITL_TEST_SCENARIOS.md`
- [x] 7.22.5 Hardware baseline capture on current firmware — **PASS** §7.23.6 2026-07-03

### 7.23 Select-dependent motor debounce + parallel burst

**Plan:** fader parallel debounce (600 ms F1 idle, parallel F2/F3/F4 burst)

- [x] 7.23.1 `scheduleSelectDependentMotorSync` + `processPendingSelectDependentMotorSync` in `NoteEditManager::update()`
- [x] 7.23.2 `applySelectNav` → schedule (not synchronous `syncMotorsForDisplaySelection`)
- [x] 7.23.3 `NoteEditFaderMotorTiming::runParallelMotorFaderBursts` + `syncMotorsFromSelectTarget` refactor
- [x] 7.23.4 SessionOpen / `NoteSelectWithFader1` — single `SendCoarse` parallel dependent step → `Done`
- [x] 7.23.5 Native + host verifier updates (cluster/dwell semantics, MO window ~400 ms)
- [x] 7.23.6 HITL capture: `note_edit_select_dependent_faders` — **PASS** 2026-07-03 (`59` nav slots, 60 motor clusters, `motor_misses=0`, outbound values OK)

### 7.24 Geometry F1 selection guard (2026-07-02)

**Evidence:** [`captures/session_20260702_183747.log`](../../../captures/session_20260702_183747.log) — `geometry_motor_sync sent=1` → F1 echo 11 ms later (`pb` diff 198 > 100) → `select_apply apply=1` → `Exited EditStartNoteState` → wrong note selected.  
**Plan:** [`docs/Plans/note_edit_geometry_f1_selection_guard_bugfix.md`](../../../docs/Plans/note_edit_geometry_f1_selection_guard_bugfix.md)

- [x] 7.24.1 ~~Block `handleSelectFaderInput` when `isGeometryEditKind`~~ — **reverted** `3348857` (blocked user F1 after geometry moves; see §7.24.8)
- [x] 7.24.2 `shouldIgnoreFaderInput` FADER_SELECT — check `selectFaderFeedbackIgnoreUntilMs_` after value-echo (geometry motor landing off-threshold)
- [x] 7.24.3 `EditManager::syncGeometrySelectionToUi`; `applySelectionFromGeometryEdit` uses it (preserve `selectedNoteIdx` + edit state)
- [x] 7.24.4 Native: `test_geometry_edit_kind_blocks_f1_select_apply_policy`; `pio test -e native` 54 pass (`test_note_edit_fader_feedback`)
- [x] 7.24.5 Host serial: `verify_geometry_fader1_no_select_apply_after_flush` + capture-shaped tests in `scripts/test_note_edit_geometry_fader1_serial_verify.py`
- [x] 7.24.6 Docs: geometry F1 inbound guard in [`DROID_MOTORFADER_PITCHBEND.md`](../../../docs/Guides/DROID_MOTORFADER_PITCHBEND.md); guides index + [`FADER_STATE_SYSTEM.md`](../../../docs/Guides/FADER_STATE_SYSTEM.md) § NOTE_EDIT motor feedback, [`Faders.md`](../../../docs/Guides/control-surface/Faders.md), [`HITL_TEST_SCENARIOS.md`](../../../docs/Guides/HITL_TEST_SCENARIOS.md)
- [x] 7.24.7 Manual HITL — geometry move + F1 motor follow; moving note stays selected; kind stays Move (user PASS 2026-07-02)
- [x] 7.24.8 Refine D38 — remove blanket kind-scoped F1 block; echo window only (`3348857`, `session_20260702_221633`)
- [x] 7.24.9 Smoke PASS post-`3348857` — `captures/session_20260702_222845.log` (echo guard OK; user F1 after geometry; 0 `geometry_edit_active`)

**Note:** §7.13.1 removed driver-time geometry block for dwell-gap. §7.24.1 kind-scoped block shipped then **reverted** §7.24.8 — live guard is `selectFaderFeedbackIgnoreUntilMs_` (§7.24.2) + `syncGeometrySelectionToUi` (§7.24.3).

---

## Phase 8 — F2 loop-relative tick (RC11)

**Design:** [design.md](./design.md) D17  
**Gate:** §8.1–§8.3 before Phase 9. Run `pio test -e native` before push.

- [x] 8.1 `noteRelativeTick` in `sendCoarseFaderPosition` (position mode start tick; length mode end tick) — shipped in firmware; inline F3 still open (§7.18.3 / §10.1)
- [x] 8.2 Native test in `test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp` with `loopStartTick=424`
- [ ] 8.3 Capture: all `#DBG outbound_ctx f2` position-mode rows `pb == expected_pb_rel` — **deferred post-archive** (requires `loopStartTick ≠ 0` loop; base preset uses loop start 0)
- [ ] 8.4 Manual: fader1 sweep with non-zero loop start — **deferred post-archive** (RC11; not a regression blocker)

---

## Phase 9 — F1 ignore during F2 outbound

**Design:** [design.md](./design.md) D18

- [x] 9.1 Arm `selectFaderFeedbackIgnoreUntilMs_` at `processFaderOutbound` `SendCoarse` through `TriggerCoarse` + post-`DONE` tail; inbound F1 check on geometry motor path (§7.24.2)
- [x] 9.2 Capture: no spurious `#DBG select_slot` during `SEND_F2` — **superseded** geometry §7.24.2 + §7.23.6 PASS

---

## Phase 10 — F3/F4 unified dependent pipeline

**Design:** [design.md](./design.md) D19

- [ ] 10.1 `sendFineFaderPosition`: loop-relative tick for position-mode fine offset (mirror 8.1)
- [ ] 10.2 Native fine CC round-trip with `loopStartTick=424`
- [ ] 10.3 Optional `#DBG outbound_ctx_f3` capture line
- [ ] 10.4 Live F1 select → F2+F3+F4 via `scheduleSelectDependentMotorSync` + parallel burst (not legacy `NoteSelectDependent` pipeline trigger — remove in §12.7)
- [ ] 10.5 Manual: F3 motor aligned; if display freeze on heavy F3 → Phase 11 / D35

---

## Phase 11 — Parked (after Phases 8–10)

Only if still reproducing after coordinate fix:

- [ ] 11.1 D35 — Fine throttle + `requestNoteInfoRefresh` display refresh
- [ ] 11.2 D36/D37 bracket regression verify
- [ ] 11.3 NOTELEN tasks 4.2 / 6.2
- [ ] 11.4 D31 — Bracket-tick F1 pitchbend if offset remains after RC11 fix

---

## Phase 12 — Stale code cleanup

**Deferred post-archive** — not blocking bug-fix closeout. Track as follow-up PR.

**Design:** [design.md](./design.md) D20  
**Audit:** [BUG.md](./BUG.md) § Stale code inventory (Phase 12–13)

### 12.1 Dead wrappers (zero callers)

- [ ] 12.1.1 Remove `sendStartNotePitchbend`, `performSelectnoteFaderUpdate`, `sendSelectnoteFaderUpdate`, `sendFaderUpdate`, `sendFaderPosition` from `NoteEditManager`
- [ ] 12.1.2 Remove `syncMotorsForDisplaySelection` — superseded by `applySelectNav` → `scheduleSelectDependentMotorSync` (no callers)
- [ ] 12.1.3 Remove `drainDependentFaderOutboundUntilDone` (zero call sites; §7.12.10)

### 12.2 Ghost state and duplicate manager

- [ ] 12.2.1 Remove `lastSelectnoteSentTime` (write-only), `NoteEditManager::PITCHBEND_IGNORE_PERIOD` (duplicate of `FEEDBACK_IGNORE_PERIOD`)
- [ ] 12.2.2 Remove `NoteEditManager::faderHandler` + `faderHandler.update()` from `update()` (never `setup()`)
- [ ] 12.2.3 Remove `faderProcessor` pointer + `setFaderProcessor` + `main.cpp` wiring (never read)
- [ ] 12.2.4 Remove `markFaderSent` from `MidiFaderProcessor` / `MidiFaderManager` (no callers)

### 12.3 Dead outbound triggers and unreachable branches

- [ ] 12.3.1 Remove `Trigger::NoteSelectDependent` from coordinator — live path is debounced `scheduleSelectDependentMotorSync` (§7.23); update `NoteEditFaderOutboundPlan.h` + native tests
- [ ] 12.3.2 Remove `Trigger::Fader1BracketOnly` handler — geometry F1 uses `pendingGeometryDriverMotorSync_` + `sendFader1MotorTimedBurst` (§7.24)
- [ ] 12.3.3 Remove unreachable `scheduleOtherFaderUpdates(FADER_SELECT)` branch; document `NoteEditManager::scheduleOtherFaderUpdates` as geometry-driver only (F2/F3/F4)
- [ ] 12.3.4 Trim or document `MidiFaderProcessor::scheduleOtherFaderUpdates` no-op + `MidiFaderManager` forwarder

### 12.4 Index-only selection gate helpers (test-only)

- [ ] 12.4.1 Remove from `NoteEditFaderOutboundPlan.h` (production unused): `shouldApplySelectionOnNoteChange`, `shouldApplySelectionOnTargetChange`, `shouldApplySelectionOnSlotChange`, `shouldApplySelectionOnNavChange`
- [ ] 12.4.2 Migrate `test_note_edit_fader_feedback` to `shouldApplySelectionOnNoteIdChange` + `editorSelectionTargetChanged` only

### 12.5 Verification and doc sweep

- [ ] 12.5.1 `pio test -e native` after §12.1–12.4
- [x] 12.5.2 Single motor trigger owner (D34/D20) — **shipped** §7.6.4; close after §12.1 removes dead send wrappers
- [ ] 12.5.3 OpenSpec / BUG stale reference sweep (`deferSelectFaderSyncToBracket`, `sessionFaderSyncStep_`, `DeferredRefresh`, `isSessionFaderSyncActive`, `sendChannel15NotePositionFeedback`, `NoteSelectDependent`, `syncMotorsForDisplaySelection`)

---

## Phase 13 — EditorSelection-only motor / selection paths

**Deferred post-archive** — coordinate with `note-edit-stable-note-id` follow-up.

**Design:** [design.md](./design.md) D41  
**Cross-ref:** `note-edit-stable-note-id` spec — motor sync on `primaryNote` delta, not list index alone  
**Gate:** After Phase **12.1–12.4** minimum (dead removal landed)

### 13.1 Motor outbound resolution

- [ ] 13.1.1 `sendCoarseFaderPosition` / `sendFineFaderPosition` / `sendNoteValueFaderPosition`: when `editorSelectionHasNote`, resolve geometry from `EditorSelection.primaryNote` (not `getSelectedNoteIdx()` gate alone)
- [ ] 13.1.2 `send*MotorPositionFromSelectTarget`: F4 pitch + F3 fine offset via `NoteId` lookup in filtered inventory (replace `target.noteIdx` array indexing where possible)
- [ ] 13.1.3 SessionOpen pipeline (`selectTarget == nullptr`): same `primaryNote` resolution for dependent burst

### 13.2 Selection mutation call sites

- [ ] 13.2.1 Audit `setSelectedNoteIdx` without `applySelectNav` — migrate or justify: `BarStepButtonHandler`, `EditLengthNoteState`, `EditSelectNoteState`, `NoteMovementUtils`, `NoteEditManager` pitch-fader path
- [ ] 13.2.2 `enterDefaultNoteEditSessionState`: remove redundant `sessionState.selection` rebuild after `selectClosestNote` / `applySelectNav`
- [ ] 13.2.3 `syncSelectionFromGeometryEdit`: require `focus.movingNoteId` when focus active; document or remove `selectedNoteIdx` fallback

### 13.3 Verification

- [ ] 13.3.1 Native tests: motor plan gates on `NoteId` / bracket only
- [ ] 13.3.2 HITL: slow F1 sweep + geometry move (§7.18.8 / §7.24.7 regression)

---

## Phase 8–12 doc reconciliation

- [x] 21.1 [BUG.md](./BUG.md) — RC11 + patch history (`session_20260630_191718`, `2ecf25d`)
- [x] 21.2 [design.md](./design.md) — context rewrite + D17–D20 + Phase 7
- [x] 21.3 [proposal.md](./proposal.md) — Phase 7–12 sections + Phase 3 HITL PASS
- [x] 21.4 [spec.md](./specs/note-edit-fader-feedback/spec.md) — 400 ms drift fix + new requirements
- [x] 21.5 [tasks.md](./tasks.md) — Phase 7–13 sections (this file)
- [x] 21.6 Phase 12–13 audit incorporated (2026-07-02) — dead code inventory in [BUG.md](./BUG.md); D41 in [design.md](./design.md); task reconciliation in this file
