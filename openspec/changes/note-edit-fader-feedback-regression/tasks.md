# Tasks — note-edit-fader-feedback-regression

**Status:** Ready for `/opsx:apply`  
**Gate:** Fix scheduler dedupe (§1) before NOTELEN gates (§4). Run `pio test -e native` before push. HITL edit baseline on hardware after firmware fix.

**Evidence:** [BUG.md](./BUG.md)  
**Design:** [design.md](./design.md) — Option A (single coordinator) unless TBD resolves to Option B.

---

## 0. Spike / evidence

- [x] 0.1 Repro on hardware per [BUG.md](./BUG.md) manual steps; capture serial with `teensy41-capture-serial`.
- [x] 0.2 Grep capture for `Session fader sync`, `Skipping fader 2`, `Length editing mode ENABLED`, `LENGTH EDIT` — attach log path to BUG.md patch history.

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

## Phase 2 — Deferred refresh pipeline (RC5)

**Status:** In progress (2026-06-30)  
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

- [ ] 19.1 Fresh capture after Phase 3 flash — run verify script; no F1 cluster >3 s without `SEND_F2`/`MO,224,14`

## 20. Phase 3 verification and docs

- [x] 20.1 `pio test -e native` — all pass (305/305)
- [ ] 20.2 NOTELEN regression (task 4.2)
- [ ] 20.3 Update [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md) when Phase 3 shipped
