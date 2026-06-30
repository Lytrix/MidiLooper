# Handoff — NOTE_EDIT fader feedback Phase 8+

**Date:** 2026-06-30  
**Branch:** `load-save-sets-loops` (2 commits ahead of origin at handoff time)  
**OpenSpec change:** [`openspec/changes/note-edit-fader-feedback-regression/`](../../openspec/changes/note-edit-fader-feedback-regression/)  
**Prior handoff:** [`docs/plans/note_edit_fader_feedback_phase7_handoff.md`](note_edit_fader_feedback_phase7_handoff.md)  
**Cursor plan:** `.cursor/plans/openspec_fader_phase_8_1d4ac442.plan.md`  
**Build env:** `teensy41-capture-serial`  
**Capture port:** `/dev/cu.usbmodem154944801`

---

## Status summary

| Milestone | Status |
|-----------|--------|
| Phase 3 outbound coordinator (slot select + quiet refresh) | **Shipped** — HITL verifier **PASS** on latest capture |
| F1↔F2 timing (user acceptance) | **Stable for first time** (2026-06-30 session) |
| F2/F3 motor **absolute** alignment | **Open** — RC11 loop-relative tick bug |
| Diagnostics (`#DBG outbound_ctx` + analyzer) | **Shipped** — commit `2ecf25d` |
| Phase 8–12 OpenSpec doc update | **Not started** |
| Phase 8.1 firmware (relative tick fix) | **Not started** |

**Commits (fader track on branch):**

| Commit | Summary |
|--------|---------|
| `ab1e3b0` | WIP: Phases 1–3 outbound coordinator + capture diagnosis |
| `2ecf25d` | F2 outbound capture diagnostics + `scripts/analyze_fader2_select_feedback.py` |

---

## What improved (capture-backed)

[`captures/session_20260630_191718.log`](../../captures/session_20260630_191718.log) — fader1 0→100→back, then fader2 manual test:

| Check | Result |
|-------|--------|
| HITL `note_edit_fader_select_refresh` | **PASS** — 0 clusters missing F2, max gap 3.1 s |
| F1 inbound | Full −8192..8191, slots 0..29 |
| F1→F2 dependent refresh | 57 F2 value changes; responsive |
| F2→F1 bracket follow | 91 F1 outbound during F2 manual phase |
| `#DBG outbound_ctx` | Confirms RC11 — see below |

Earlier baseline for comparison: [`captures/session_20260630_190256.log`](../../captures/session_20260630_190256.log) (pre-`outbound_ctx`, large F1/F2 delta).

**Analyze:**

```bash
python3 scripts/analyze_fader2_select_feedback.py captures/session_20260630_191718.log --after 51.0
rg '#DBG outbound_ctx|#DBG select_slot|#DBG outbound_step' captures/session_20260630_191718.log
```

---

## RC11 — F2 outbound coordinate bug (next fix)

**Symptom:** F2 motor ~50% offset from expected note start when `loopStartTick ≠ 0`; updates responsively.

**Cause:** [`sendCoarseFaderPosition`](../../src/NoteEditManager.cpp) uses storage tick (`liveNote.startTick % loopLength`). F1 select and F2 **inbound** use [`SelectNavigation::noteRelativeTick`](../../src/Utils/SelectNavigation.cpp) with `loopStartTick`.

**Capture proof** (`loop_start=424`, `loop_len=768`):

| Storage tick | Rel tick | Sent F2 | Expected (rel) |
|-------------|----------|---------|----------------|
| 0 | 344 | 0.0% | ~47.8% |
| 448 | 24 | 62.2% | ~3.3% |
| 509 | 85 | 70.7% | ~11.8% |

After flash with `2ecf25d`, every `#DBG outbound_ctx` row shows `pb != expected_pb_rel` when `anchor_tick != rel_tick`.

**Same class on F3 (Phase 10):** [`sendFineFaderPosition`](../../src/NoteEditManager.cpp) ~1039 uses `startTick % loopLength` for position-mode fine offset.

**Architecture checkpoint:** No ownership or session-mode change — tick input fix only in send helpers.

---

## Recommended order (do not clean firmware first)

| Step | Phase | Action |
|------|-------|--------|
| 1 | Docs | Update OpenSpec `tasks.md`, `BUG.md` (RC11), `spec.md`, `design.md`, `proposal.md` — Phase 8–12 |
| 2 | **8** | `noteRelativeTick` in `sendCoarseFaderPosition` + native test (`loopStartTick=424`) |
| 3 | Verify | Flash, capture, `pb == expected_pb_rel`; HITL verifier still PASS |
| 4 | **9** | Arm F1 ignore during F2 outbound (`SendCoarse`…`TriggerCoarse`) |
| 5 | **10** | F3 relative tick + verify F2/F3/F4 single pipeline burst |
| 6 | **12** | Remove dead code, ghost `faderHandler`, double motor triggers |
| 7 | **11** | Bracket snap / NOTELEN / display — only if still reproducing |

Doc-only cleanup (step 1) can run in parallel anytime. **Do not** remove firmware or fix double triggers before step 3 passes.

---

## Phase 8 — F2 relative tick (implement first)

**Change** in `sendCoarseFaderPosition` (position mode):

```cpp
const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;
const uint32_t storageTick = liveNote.startTick;  // or endTick in length mode
const uint32_t relTick =
    SelectNavigation::noteRelativeTick(storageTick, loopStartTick, loopLength);
anchorTick = relTick;  // then existing loopTickToCoarsePitchbend(anchorTick, loopLength)
```

**Tasks:**

- [ ] 8.1 Firmware fix above (length mode: relative end tick)
- [ ] 8.2 Native test in [`test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp`](../../test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp)
- [ ] 8.3 Capture: all `outbound_ctx` rows `pb == expected_pb_rel`
- [ ] 8.4 Manual: fader1 sweep with non-zero loop start — F2 motor matches note start

---

## Phase 9 — Block F1 during F2 outbound

**Goal:** Ignore F1 pitchbend while F2 motor updates (echo / jitter must not re-trigger select).

**Asymmetry today:**

- F1 bracket send → `selectFaderFeedbackIgnoreUntilMs_` (~544)
- F2 coarse send → `armCoarseFaderFeedbackIgnore` only (~737)

**Do:**

- Arm F1 ignore at `processFaderOutbound` `SendCoarse` (or start of `sendCoarseFaderPosition` when called from pipeline)
- Keep user override via `SELECT_MOVEMENT_THRESHOLD` in `shouldIgnoreFaderInput`
- Capture gate: no spurious `#DBG select_slot` during `SEND_F2`/`TRIGGER_F2` window

---

## Phase 10 — F3/F4 like F2 in one update

- [ ] 10.1 `sendFineFaderPosition`: loop-relative tick (mirror 8.1)
- [ ] 10.2 Native fine CC round-trip with `loopStartTick=424`
- [ ] 10.3 Optional `outbound_ctx_f3` capture line
- [ ] 10.4 One `NoteSelectDependent` → F2 + F3 + F4 MO lines; no fader3-only path
- [ ] 10.5 Manual: F3 motor aligned; if display freeze on heavy F3 → see Phase 11 / D35

F4 note-value: no tick fix; stays in same pipeline step.

---

## Phase 12 — Stale code cleanup (after 8.4)

Investigation 2026-06-30 — remove only after coordinate fix verified.

### Dead outbound wrappers (zero callers in `src/`)

| Remove | Live replacement |
|--------|------------------|
| `sendSelectnoteFaderUpdate` | `scheduleNoteSelectFaderSync` |
| `performSelectnoteFaderUpdate` | duplicate of above |
| `sendStartNotePitchbend` | `processFaderOutbound` |
| `sendFaderUpdate` / `sendFaderPosition` | pipeline + direct send helpers |

### Dead / ghost state

| Item | Action |
|------|--------|
| `lastSelectnoteSentTime` | Remove (write-only) |
| `PITCHBEND_IGNORE_PERIOD` | Remove (unused) |
| `NoteEditManager::faderHandler` | Remove — ghost second `MidiFaderManager`, never `setup()` |
| `faderProcessor` pointer | Remove — set in `main.cpp`, never read |
| `markFaderSent` | Remove — no callers |
| `MidiFaderProcessor::scheduleOtherFaderUpdates` | Already no-op; trim Manager wrapper |

### Live bug to fix in cleanup pass

**Double motor triggers:** `sendCoarseFaderPosition` / `sendFineFaderPosition` / `sendNoteValueFaderPosition` call `send*MotorTrigger()` internally **and** `processFaderOutbound` runs `TriggerCoarse` / `TriggerFine` / `TriggerNoteValue`. Pick one owner (Phase 7 D34).

### OpenSpec stale references to reconcile

Remove or rewrite: `deferSelectFaderSyncToBracket`, `sessionFaderSyncStep_`, `sendChannel15NotePositionFeedback`, `DeferredRefresh`, `isSessionFaderSyncActive`.

**Live API names:** `requestFaderOutbound`, `processFaderOutbound`, `scheduleNoteSelectFaderSync`, `sendNoteEditSessionFaderFeedback`.

---

## Phase 11 — Parked (after 8–10)

From Phase 7 handoff — only if still reproducing:

- D36 session bracket on geometry (`commitBracketTickFromGeometry`)
- D37 geometry F1 feedback without touching nav state
- D35 fine throttle + display refresh (`requestNoteInfoRefresh` stub)
- NOTELEN tasks 4.2 / 6.2
- D31 bracket-tick → F1 pitchbend (if offset remains after RC11 fix)

---

## Primary files

| Area | Files |
|------|--------|
| Outbound coordinator | [`src/NoteEditManager.cpp`](../../src/NoteEditManager.cpp), [`include/NoteEditManager.h`](../../include/NoteEditManager.h), [`include/Utils/NoteEditFaderOutboundPlan.h`](../../include/Utils/NoteEditFaderOutboundPlan.h) |
| Relative tick | [`src/Utils/SelectNavigation.cpp`](../../src/Utils/SelectNavigation.cpp) |
| F1 pitchbend | [`src/EditStates/EditSelectNoteState.cpp`](../../src/EditStates/EditSelectNoteState.cpp) |
| Capture / HITL | [`scripts/analyze_fader2_select_feedback.py`](../../scripts/analyze_fader2_select_feedback.py), [`scripts/hitl/verify/note_edit_fader_select_refresh.py`](../../scripts/hitl/verify/note_edit_fader_select_refresh.py) |
| Stale processor API | [`src/MidiFaderProcessor.cpp`](../../src/MidiFaderProcessor.cpp), [`src/MidiFaderManager.cpp`](../../src/MidiFaderManager.cpp), [`src/main.cpp`](../../src/main.cpp) |
| OpenSpec | [`openspec/changes/note-edit-fader-feedback-regression/`](../../openspec/changes/note-edit-fader-feedback-regression/) |
| Tests | [`test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp`](../../test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp) |

---

## Verification gates

```bash
# Required before push
pio test -e native

# Build (ask user before upload)
pio run -e teensy41-capture-serial

# Capture (separate terminal)
.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801

# Analyze
python3 scripts/analyze_fader2_select_feedback.py captures/session_YYYYMMDD_HHMMSS.log
rg '#DBG outbound_ctx' captures/session_*.log | head -20
```

**Phase 8 pass:** every position-mode `outbound_ctx` line has `pb == expected_pb_rel`.

**Regression:** HITL verifier stays PASS; F1↔F2 timing must not regress.

---

## Untracked local docs (optional to commit)

- [`docs/plans/note_edit_fader_feedback_phase6_hybrid_trigger_refinement.md`](note_edit_fader_feedback_phase6_hybrid_trigger_refinement.md)
- [`docs/plans/note_edit_fader_dependent_outbound_regression_bugfix.md`](note_edit_fader_dependent_outbound_regression_bugfix.md)
- This handoff file

---

## Session notes

- **`docs/runtime/CURRENT_WORK.md`** still lists persistence track — fader regression is parallel OpenSpec work on same branch.
- Inbound NOTE_EDIT path: `MidiHandler` → `noteEditManager.handleMidiPitchbend` → `handleFaderInput` → `NoteEditManager::shouldIgnoreFaderInput` (not `MidiFaderProcessor` for ch14/16).
- Do **not** revert Phase 3 coordinator — it fixed RC5 timing; RC11 is a separate coordinate bug.
- After each phase: update OpenSpec `tasks.md`, run `pio test -e native`, ask user before Teensy upload.
