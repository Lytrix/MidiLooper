# Handoff — NOTE_EDIT fader feedback Phase 7

**Date:** 2026-06-30  
**Branch:** `load-save-sets-loops` (firmware work is local; not merged)  
**OpenSpec change:** [`openspec/changes/note-edit-fader-feedback-regression/`](../../openspec/changes/note-edit-fader-feedback-regression/)  
**Prior plan:** Phase 6 shipped ([`docs/Plans/note_edit_fader_feedback_phase6_hybrid_trigger_refinement.md`](note_edit_fader_feedback_phase6_hybrid_trigger_refinement.md))  
**Cursor plan:** `.cursor/plans/fader_feedback_phase_7_1ba3ebec.plan.md`  
**Build env:** `teensy41-capture-serial` (default per workspace rules)  
**Capture port:** `/dev/cu.usbmodem154944801`

---

## Status: Phase 7 not started

Phase 6 (D25–D30) is in firmware and passed **316/316** native tests at last run. Hardware still fails user acceptance. Phase 7 scope is defined below; **no Phase 7 code has landed yet**.

| Phase | Status |
|-------|--------|
| Phase 6 hybrid trigger + scoped PC | Shipped in tree |
| Phase 7 D36 session bracket on geometry | Shipped in tree |
| Phase 7 D37 geometry F1 feedback split | Shipped in tree |
| Phase 7 D31 bracket-tick F1 pitchbend | **Shipped in tree** |
| Phase 7 bracket/session + stale API audit | **In progress** — D34/D38 next |

---

## User symptoms (2026-06-30 hardware)

1. **F1 select** — F2–F4 motors do not follow (F1 motor moves on display bracket sometimes).
2. **F2 position edit** — F1 motor updates in time (acceptable).
3. **F3 fine edit** — F1 motor does not follow; heavy F3 use **freezes display** (exit/re-enter NOTE_EDIT revives).
4. **Bracket snap-back** (session 2) — after F2/F3 edit, bracket jumps to **F2 coarse 16th tick** (same class as `d49e4c8` loop-edit bracket pull).

---

## Capture evidence (keep for regression)

| Log | Role |
|-----|------|
| [`captures/session_20260630_160447.log`](../../captures/session_20260630_160447.log) | Post–Phase 6: wire OK on slot change; 103× `SEND_F1` vs 8× F2 motor PB; F2-heavy |
| [`captures/session_20260630_161022.log`](../../captures/session_20260630_161022.log) | Bracket snap-back repro; `SEND_F2` without `MO,224,14` (~579945 µs) |

**Analyze:**

```bash
rg 'OUTBOUND|SEND_F|OUTBOUND_CTX|MO,224|SELECT_SLOT|DNTE' captures/session_20260630_161022.log
```

---

## Root causes → fixes (implementation checklist)

Implement in this order. Architecture checkpoint: **no new ownership or session modes** — D36 closes existing session/legacy drift.

### 1. D36 — Session bracket on geometry moves (**P0**)

**Bug:** `EditManager::setBracketTick` updates legacy `bracketTick` only ([`include/EditManager.h:135`](../../include/EditManager.h)). `sessionState.selection.bracketTick` stays at last F1-select slot tick. `syncNoteEditSessionStateToUi` overwrites legacy from session ([`src/EditManager.cpp:769`](../../src/EditManager.cpp)) → bracket snaps to F2 coarse step.

**Do:**

- Add `EditManager::commitBracketTickFromGeometry(uint32_t tick)` — set **both** `sessionState.selection.bracketTick` and legacy `bracketTick`.
- Replace geometry-path `setBracketTick` in [`src/Utils/NoteMovementUtils.cpp`](../../src/Utils/NoteMovementUtils.cpp) (`finalReconstructAndSelect`, length-end paths), [`src/NoteEditManager.cpp`](../../src/NoteEditManager.cpp) (NOTELEN toggle), and other geometry `setBracketTick` call sites.
- **Do not** use for F1 nav — `applySelectNav` already sets session.

**Test:** F2 move + F3 fine + `syncNoteEditSessionStateToUi` → `getBracketTick()` unchanged at fine start.

### 2. D37 — Geometry F1 feedback must not touch nav state (**P0**)

**Bug:** [`sendFader1BracketFeedback`](../../src/NoteEditManager.cpp) overwrites `lastUserSelectFaderValue` / `lastSelectFaderTime` (lines 546–547). Quiet refresh / motor echo can re-apply slot-quantized selection.

**Do:**

- Add flag or split: geometry driver → motor MIDI only; navigation path may update tracked F1 pitch.
- Call geometry variant from [`scheduleOtherFaderUpdates`](../../src/NoteEditManager.cpp).

### 3. D34 + D38 — Send path honesty + dead API cleanup (**P1**)

**Bug:** `processFaderOutbound` logs `SEND_F2` and runs motor trigger even when `sendCoarseFaderPosition` early-returns. Several fader helpers have **zero callers** but look live in OpenSpec.

**Do:**

- Return `bool` from `sendCoarseFaderPosition`, `sendFineFaderPosition`, `sendNoteValueFaderPosition`; skip trigger + `#CAP` step when `false`.
- Remove or deprecate orphans (no callers in `src/`): `sendFaderUpdate`, `sendFaderPosition`, `sendStartNotePitchbend`, `sendSelectnoteFaderUpdate`, `performSelectnoteFaderUpdate`, `markFaderSent`.
- Reconcile OpenSpec — drop references to `sendChannel15NotePositionFeedback`, `isSessionFaderSyncActive` (removed from code).
- Fix dual API: `MidiFaderManager::scheduleOtherFaderUpdates` → no-op processor; live path is `NoteEditManager::scheduleOtherFaderUpdates`.

### 4. D31 — Bracket-tick → F1 pitchbend (**P1**)

**Bug:** [`EditSelectNoteState::sendTargetPitchbend`](../../src/EditStates/EditSelectNoteState.cpp) uses discrete slot index via `findSlotIndexForSelection` — fine edits within a slot do not move F1 motor.

**Do:**

- Add `SelectNavigation::mapBracketTickToSelectPitchbend(...)` (interpolate along slot spine).
- Use in `sendTargetPitchbend` for outbound feedback; keep inbound F1 `map(pitch → slot)` unchanged.

**Tests:** [`test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp`](../../test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp)

### 5. D35 + D38 — Fine throttle + display refresh (**P1**)

**Bug:** `handleFineFaderInput` has no movement threshold (unlike coarse). `requestNoteInfoRefresh` is a stub ([`src/DisplayManager.cpp:2464`](../../src/DisplayManager.cpp)) but called every move from `NoteMovementUtils`.

**Do:**

- Mirror coarse: `FINE_MOVEMENT_THRESHOLD` + `FINE_STABILITY_TIME`; skip no-op tick targets.
- Implement minimal coalesced `requestNoteInfoRefresh` (`SC_DNTE` after invalidate) or remove call sites — pair rate limit with D32.

### 6. D32 — Rate-limit geometry `SEND_F1` (**P2**)

103× `SEND_F1` vs 8× F2 motor PB in session 160447. Reuse coarse threshold; skip when bracket-derived pitchbend unchanged.

### 7. D33 — Motor priority during dependent outbound (**P2**)

Set `droidMotorOutboundPriority_` during `NoteSelectDependent` burst in [`src/MidiHandler.cpp`](../../src/MidiHandler.cpp) so LED drain does not interleave.

---

## Primary files

| Area | Files |
|------|--------|
| Coordinator | [`src/NoteEditManager.cpp`](../../src/NoteEditManager.cpp), [`include/NoteEditManager.h`](../../include/NoteEditManager.h), [`include/Utils/NoteEditFaderOutboundPlan.h`](../../include/Utils/NoteEditFaderOutboundPlan.h) |
| Session bracket | [`src/EditManager.cpp`](../../src/EditManager.cpp), [`include/EditManager.h`](../../include/EditManager.h) |
| Geometry moves | [`src/Utils/NoteMovementUtils.cpp`](../../src/Utils/NoteMovementUtils.cpp) |
| F1 pitchbend | [`src/EditStates/EditSelectNoteState.cpp`](../../src/EditStates/EditSelectNoteState.cpp), [`src/Utils/SelectNavigation.cpp`](../../src/Utils/SelectNavigation.cpp) |
| Display | [`src/DisplayManager.cpp`](../../src/DisplayManager.cpp) |
| USB/MIDI | [`src/MidiHandler.cpp`](../../src/MidiHandler.cpp) |
| Dead processor API | [`src/MidiFaderProcessor.cpp`](../../src/MidiFaderProcessor.cpp), [`src/MidiFaderManager.cpp`](../../src/MidiFaderManager.cpp) |
| OpenSpec | [`openspec/changes/note-edit-fader-feedback-regression/BUG.md`](../../openspec/changes/note-edit-fader-feedback-regression/BUG.md) (add RC24–RC28), `design.md`, `tasks.md` §38+ |
| HITL verify | [`scripts/hitl/verify/note_edit_fader_select_refresh.py`](../../scripts/hitl/verify/note_edit_fader_select_refresh.py) |

---

## Verification gates

```bash
# Host tests (required before push)
pio test -e native

# Firmware build (ask user before upload)
pio run -e teensy41-capture-serial
```

**Hardware** (with `scripts/capture_session.py` on capture port):

1. **Bracket hold:** F2 move → F3 fine (+few ticks) → encoder tap → bracket stays at fine tick; `DNTE` monotonic.
2. **F1 select:** slot change → `SEND_F2` + `MO,224,14` within 200 ms; counts track slot changes.
3. **F2 edit:** `SEND_F1` rate lower than Phase 6 capture; F1 motor still tracks bracket.
4. **F3 flood:** display stays live; `SEND_F1` tracks sub-step bracket.

**Pass criteria for D34:** every `OUTBOUND,SEND_F2` has preceding `OUTBOUND_CTX` + `MO,224,14` in capture (no trigger-only bursts).

---

## Phase 6 context (already shipped — do not revert)

- **D25** Immediate `NoteSelectDependent` on F1 slot change; quiet refresh deduped supplement.
- **D26** PC re-arm only on `SessionOpen` / length mode — **not** per dependent burst (RC21).
- **D27** Inline `sendFader1BracketFeedback` on F2/F3/F4 drivers (no outbound preempt).
- **D29** Skip redundant `setBracketTick` when move `delta == 0`.

Reverting D27 spam without D32/D31 makes F3→F1 worse. Reverting D26 reintroduces DROID motor reset churn.

---

## Out of scope (needs DEC / design session)

- **Soft motor re-arm** without full PC `startvalue=0.5` — only if post–D34 capture shows `MO,224,14` with no `MI,H,224,14` echo.
- **Continuous F2–F4 update during F1 drag** (pre–D18 immediate per-sample) — Phase 6 intentionally uses slot boundary + 400 ms quiet.
- **`MidiFaderProcessor::commitMovingNote`** full implementation — confirm overlap ownership before expanding.

---

## Session notes

- Native **316/316** after Phase 6; HITL task §37.3 still open pending Phase 7 flash.
- `docs/Runtime/CURRENT_WORK.md` still lists persistence track — fader regression is parallel OpenSpec work on same branch.
- Do **not** edit `.cursor/plans/fader_feedback_phase_7_1ba3ebec.plan.md` per user rule; this handoff is the operational copy under `docs/Plans/`.
- After implementation: update `PROJECT_STATE.md`, OpenSpec `tasks.md`, run native tests, ask user before Teensy upload.
