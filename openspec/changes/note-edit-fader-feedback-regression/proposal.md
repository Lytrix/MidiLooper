## Why

Commit `d49e4c8` (2026-06-29) added deferred DROID fader feedback on NOTE_EDIT entry to prevent stale loop-edit motor positions from pulling the select bracket. That change regressed outbound sync: **fader1 may not reach the bracket motor**, **fader2 coarse often does not update on entry or note select** while **fader3 does**, and **NOTELEN length edit** is unusable when fader2 still reflects a prior loop-edit position. Inbound fader2 still moves notes — the failure is feedback scheduling, not edit store logic.

This blocks trust in NOTE_EDIT on hardware and is orthogonal to active persistence/overlay work ([CURRENT_WORK.md](../../docs/runtime/CURRENT_WORK.md)).

## What Changes

- **Single outbound coordinator** in `NoteEditManager`: cancel duplicate `sendSelectnoteFaderUpdate` when session fader sync is active; always send fader2 coarse and fader3 fine together.
- **Session entry sync:** Reliable fader1 bracket + fader2/3 note-start feedback within bounded latency; narrow input blocking so fader1 is usable after its feedback ignore period.
- **Note select sync:** `performSelectnoteFaderUpdate` and session sync step 2 SHALL refresh coarse and fine together when `!lengthEditingMode`.
- **NOTELEN:** Coarse motor feedback to note end before accepting length inbound; serial log anchor tick + pitchbend for HITL gates.
- **Coarse feedback consistency:** All coarse outbound paths set `lastSentTime` (via `sendFaderUpdate` or equivalent).
- **Regression tests:** Native helpers for length coarse round-trip; HITL serial gates for fader2 sync after select and NOTELEN range.

## Capabilities

### New Capabilities

- `note-edit-fader-feedback`: Outbound DROID fader sync on NOTE_EDIT session entry, fader1 note select, and NOTELEN enable; input gating during sync.

### Modified Capabilities

- *(none)* — no requirement changes to archived `note-edit-modification-session` or `note-edit-session-state` storage contracts.

## Impact

- **Firmware:** [`NoteEditManager.cpp`](../../src/NoteEditManager.cpp), [`NoteEditManager.h`](../../include/NoteEditManager.h), [`EditManager.cpp`](../../src/EditManager.cpp) (`syncNoteEditSessionStateToUi` guard).
- **Tests:** New native suite `test_note_edit_fader_feedback` (mapping + sync invariants); HITL edit baseline serial gates.
- **Scripts:** `scripts/analyze_fader2_select_feedback.py`, `scripts/hitl/verify/note_edit_fader_select_refresh.py`.
- **Brownfield:** [BUG.md](./BUG.md); related archived `edit-record-display-length-mode` (D3 length lifecycle — do not regress).
- **Docs:** Update [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md) when shipped.

## Non-Goals

- ~~Full fader state-machine rewrite or migration to `MidiFaderProcessor` for NOTE_EDIT path.~~ *(Phase 3: selection-driven outbound state machine in `NoteEditManager` — not `MidiFaderProcessor` migration.)*
- Unifying encoder `NoteEditKind::Length` with `lengthEditingMode` (NOTELEN path only for this bug).
- Jam / multi-loop / persistence overlay changes.

## Open Decisions (TBD)

- **Sync latency budget:** Resolved in Phase 3 — frame-stepped coordinator, 400 ms user-classified quiet before dependent refresh.
- **HITL gate strictness:** Timing verifier **PASS** on `session_20260630_191718`; coordinate gate `pb == expected_pb_rel` pending Phase 8.

---

## Phase 2 — Deferred refresh pipeline (2026-06-30) — superseded

**Status:** Partially implemented then reverted on hardware; diagnostic immediate+settle path replaced Phase 2 coordinator. See **Phase 3**.

### Why (Phase 2)

Phase 1 shipped the frame-stepped outbound coordinator (`NoteEditFaderOutboundPlan`, `processFaderOutbound`) but retained preempt-on-request: every `requestFaderOutbound()` cancels in-flight ch15 sequences. During fader1 note-select movement, dependent faders (F2–F4) update once then starve — see [BUG.md RC5](./BUG.md).

### What Changes (Phase 2)

- **Deferred dependent refresh:** After fader1 note select, F2–F4 outbound waits until fader1 stable for ≥1000 ms; each new select resets the timer.
- **Non-preemptive execution:** Active ch15 pipeline runs to `Done`; pending refresh coalesces (latest wins) and runs immediately after completion.
- **Fader1 immediate, dependents deferred:** Fader1 bracket feedback on note select remains immediate; only F2–F4 use the deferred scheduler.
- **Centralized scheduling:** All note-select dependent refresh routes through `scheduleDependentFaderRefresh()` instead of immediate `requestFaderOutbound(NoteSelect)`.
- **Instrumentation:** Temporary `#DBG outbound_step` serial lines for HITL diagnosis.
- **HITL:** Rapid fader1 multi-select scenario verifying one complete F2/F3/F4 sequence after quiet period.

### Phase 2 non-goals (unchanged)

- No `MidiFaderProcessor` migration.
- No persistence/overlay scope.

---

## Phase 3 — Selection-driven outbound state machine (2026-06-30)

### Why (Phase 3)

Diagnostic path (immediate F2–F4 + PC re-arm + millis settle + pitch deadband) improved motors intermittently but remained erratic: fast-then-slow gestures left selection lagging pitch; settle timers fought motor echo; multi-minute stalls were policy conflicts, not MIDI TX queue buildup — see [BUG.md RC7–RC10](./BUG.md) and capture `session_20260630_113422.log`.

### What Changes (Phase 3)

- **Slot-index navigation:** User-classified fader1 input applies selection immediately on mapped slot-index change (no pitch deadband for navigation).
- **Outbound state machine:** `processFaderOutbound()` frame-stepped pipeline; `FaderSelectPhase` tracks user motion vs quiet; dependent F2–F4 refresh after **400 ms** user-classified quiet (`NoteEditFaderOutboundPlan.h`).
- **Non-preemptive coalesce:** NoteSelect dependent refresh coalesces while ch15 pipeline active; SessionOpen / LengthModeEnter / Exit may preempt.
- **Single outbound path:** Remove settle timer and duplicate immediate burst path; keep DROID PC re-arm before ch15 burst.
- **Instrumentation:** `#DBG outbound_step` + `#DBG select_slot` under `SESSION_CAPTURE`.
- **Tests:** Native policy tests on `NoteEditFaderOutboundPlan`; HITL capture gates for fast-then-slow and fader1-min scenarios.

### Phase 3 non-goals

- No `MidiFaderProcessor` migration.
- No persistence/overlay scope.

### Phase 3 status (2026-06-30)

- **HITL timing PASS** — `note_edit_fader_select_refresh` verifier on `session_20260630_191718` (0 clusters missing F2, max gap 3.1 s).
- **Absolute motor alignment open** — RC11 loop-relative tick bug; F2 motor ~50% offset when `loopStartTick ≠ 0`.

---

## Phase 7 — Bracket / send-path hygiene (2026-06-30)

### What Changes (Phase 7)

- **D31, D36, D37 shipped:** Bracket-tick F1 pitchbend; `commitBracketTickFromGeometry` for session + legacy bracket; geometry F1 feedback without touching nav state.
- **D34 deferred to Phase 12:** Send-path honesty (bool return from send helpers); single motor-trigger owner.
- **D35, D32, D33 parked to Phase 11:** Fine throttle, display refresh, rate-limit geometry SEND_F1.

See [phase7 handoff](../../../docs/plans/note_edit_fader_feedback_phase7_handoff.md).

---

## Phase 8 — F2 loop-relative outbound tick (2026-06-30)

### Why (Phase 8)

RC11 — `sendCoarseFaderPosition` uses `startTick % loopLength` while F1 select and F2 inbound use `noteRelativeTick`. Capture `session_20260630_191718` confirms `pb != expected_pb_rel` on every `#DBG outbound_ctx` row when `loop_start ≠ 0`.

### What Changes (Phase 8)

- **`noteRelativeTick` in `sendCoarseFaderPosition`:** Position mode (start tick) and length mode (end tick) before pitchbend mapping.
- **Native test:** `loopStartTick=424` round-trip in `test_note_edit_fader_feedback`.
- **Capture gate:** All position-mode `outbound_ctx` rows have `pb == expected_pb_rel`.

---

## Phase 9 — Block F1 during F2 outbound (2026-06-30)

### What Changes (Phase 9)

- Arm `selectFaderFeedbackIgnoreUntilMs_` at `SendCoarse` (symmetric with F1 bracket send).
- Preserve user override via `SELECT_MOVEMENT_THRESHOLD`.
- Capture gate: no spurious `#DBG select_slot` during `SEND_F2` / `TRIGGER_F2` window.
- **§7.24 (2026-07-02):** Inbound F1 also honors `selectFaderFeedbackIgnoreUntilMs_`; kind-scoped geometry F1 guard (D38–D40).

---

## Phase 10 — F3/F4 unified dependent pipeline (2026-06-30)

### What Changes (Phase 10)

- **`sendFineFaderPosition`:** Loop-relative tick for position-mode fine offset (mirror Phase 8).
- **Native test:** Fine CC round-trip with `loopStartTick=424`.
- **Optional `#DBG outbound_ctx_f3`** capture line.
- **One `NoteSelectDependent` burst** for F2 + F3 + F4; no fader3-only path.
- F4 note-value: no tick fix; same pipeline step.

---

## Phase 11 — Parked (after Phases 8–10)

Only if still reproducing:

- D35 display freeze on heavy F3 use
- D36/D37 bracket regressions
- NOTELEN tasks 4.2 / 6.2
- D31 if F1 offset remains after RC11 fix

---

## Phase 12 — Stale code cleanup

**Gate:** §12.1–12.4 (zero-caller removal) when `pio test -e native` is green. §12.3 trigger trim after §12.1–12.2. Phase 8.3 still gates §8.4 RC11 capture, not dead-wrapper deletion.

### What Changes (Phase 12)

**Dead outbound wrappers** (zero callers): `sendStartNotePitchbend`, `performSelectnoteFaderUpdate`, `sendSelectnoteFaderUpdate`, `sendFaderUpdate`, `sendFaderPosition`, `syncMotorsForDisplaySelection`, `drainDependentFaderOutboundUntilDone`.

**Ghost state:** `lastSelectnoteSentTime`, duplicate `PITCHBEND_IGNORE_PERIOD`, `NoteEditManager::faderHandler`, `faderProcessor`, `markFaderSent`.

**Dead coordinator paths:** `Trigger::NoteSelectDependent`, `Trigger::Fader1BracketOnly`, unreachable `scheduleOtherFaderUpdates(FADER_SELECT)`; trim `MidiFaderProcessor::scheduleOtherFaderUpdates` no-op chain.

**Test-only index gates:** Remove `shouldApplySelectionOnNoteChange`, `shouldApplySelectionOnTargetChange`, slot/nav variants from production header; tests use `NoteId` gates only.

**Doc sweep:** Historical references to `deferSelectFaderSyncToBracket`, `sessionFaderSyncStep_`, `NoteSelectDependent` live path.

---

## Phase 13 — EditorSelection-only motor paths

### What Changes (Phase 13)

- Motor send helpers resolve geometry from `EditorSelection.primaryNote`, not `selectedNoteIdx` alone.
- Migrate `setSelectedNoteIdx`-only call sites to `applySelectNav` or derived-index sync.
- Dedupe `enterDefaultNoteEditSessionState` selection rebuild.
- Cross-ref `note-edit-stable-note-id` motor-sync on `primaryNote` delta.

**Gate:** After Phase 12.1–12.4 minimum.
