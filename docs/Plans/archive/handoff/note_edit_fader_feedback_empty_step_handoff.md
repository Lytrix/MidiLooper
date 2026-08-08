# Handoff — NOTE_EDIT fader feedback: dirty flags + empty-step F2 gap

**Date:** 2026-07-01  
**Branch:** `load-save-sets-loops` (ahead of origin; local uncommitted dirty-flag work)  
**OpenSpec change:** [`openspec/changes/note-edit-fader-feedback-regression/`](../../openspec/changes/note-edit-fader-feedback-regression/)  
**Prior handoff:** [`docs/Plans/note_edit_fader_feedback_phase8_handoff.md`](note_edit_fader_feedback_phase8_handoff.md)  
**Next handoff:** [`docs/Plans/note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md`](note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md)  
**Cursor plans:** `.cursor/plans/fader_stall_root_options_8202644d.plan.md`, `.cursor/plans/empty-step_f2_gap_fix_e0e5252c.plan.md`, `.cursor/plans/option_d_fader_handoff_aeba362f.plan.md`  
**Build env:** `teensy41-capture-serial`  
**Capture port:** `/dev/cu.usbmodem154944801`

---

## Status summary

| Milestone | Status |
|-----------|--------|
| Phase 8.1 loop-relative F2 tick (`noteRelativeTick`) | **Shipped** (commit `43bc95b`) |
| Phase 9 F2-drag F1 cross-talk lockout | **Shipped** (commit `43bc95b`) |
| Stall-fix steps 1–4: geometry snapshot + dirty flags + partial plans | **Shipped** (`6e1dacf`) |
| Empty-step F2 motor send (RC-A) | **Shipped** (`ec4678f`) |
| Option A bracket-tick apply gate | **Shipped** (`23c2391`) — **insufficient** for same-slot dwell (see Option D handoff) |
| Pipeline always reaches `DONE` on send-fail (RC-B) | **Partial** — 11 orphaned `BEGIN` in `session_20260701_120425` |
| Option D aggressive refresh (D1–D4) | **Superseded** — [`note_edit_fader_feedback_selection_driven_refactor.md`](note_edit_fader_feedback_selection_driven_refactor.md) |
| Step 8 skip-when-feedback-current | **Parked** — after Option D |
| HITL verifier tick/pitch alignment (plan Option B) | **Not started** |

**Recent commits (fader track):**

| Commit | Summary |
|--------|---------|
| `43bc95b` | F2-drag F1 lockout; D37 geometry bracket; loop-relative coarse F2 |
| `6e1dacf` | Dirty flags + `planForSelectDependent` |
| `ec4678f` | RC-A empty-step F2/F3 send |
| `23c2391` | Bracket-tick apply gate + RC-B partial (`completeOutboundPipelineAtDone`, `SKIP_SEND`) |

**Post-Option A capture:** [`captures/session_20260701_120425.log`](../../captures/session_20260701_120425.log) — Option A runs (`select_apply apply=1` ×299) but same-slot dwell still dominates; see Option D handoff.

---

## What improved (capture-backed)

Compare [`captures/session_20260701_102036.log`](../../captures/session_20260701_102036.log) (pre dirty-flags) vs [`captures/session_20260701_112118.log`](../../captures/session_20260701_112118.log) (post flash):

| Metric | Pre-fix | Post-fix |
|--------|---------|----------|
| Slot index changes | 171 | 64 |
| `outbound_step=BEGIN` | 173 | 61 |
| `outbound_step=SEND_F2` | 170 | 55 |
| `dependent_refresh_skip` | n/a | 0 |
| `COALESCE` | n/a | 0 |

~65% fewer dependent bursts. Same-slot F1 dwell (709 `select_slot` on unchanged `idx`) produces **no** `dependent_refresh_schedule` — stall-fix intent working.

**Analyze:**

```bash
rg '#DBG dependent_refresh|#DBG select_slot|#DBG outbound_step|empty step' captures/session_20260701_112118.log
rg '#DBG outbound_ctx f2' captures/session_20260701_112118.log
```

---

## Fast vs slow — confirmed

**FAST** = slot crossing after `<500ms` dwell and `<10` pitchbend events on prior `idx`.  
**SLOW** = `≥500ms` dwell or `≥10` events before crossing.

| Crossing | Count | apply | schedule | SEND_F2 | empty step |
|----------|-------|-------|----------|---------|------------|
| FAST | 17 | 17/17 | 17/17 | 17/17 | 0 |
| SLOW | 47 | 42/47 | 42/47 | 36/42 note¹ | 6 |

¹ All scheduled **note** crossings get `SEND_F2` within ~2ms. All 6 scheduled **empty** crossings get no `SEND_F2`.

**Fast works better than slow** in this capture — not because note-slot scheduling fails on slow crosses, but because:

1. **RC-E — Same-slot slow crawl (by design):** 22 dwell runs ≥800ms on one `idx` (up to 9.0s on `idx=8`) with zero refresh during dwell. Dirty flags fire on slot-index change only.
2. **RC-A — Empty steps (bug):** 6/6 missing `SEND_F2` are empty 16th selections.
3. **RC-F — Stale-echo lockout:** 5 slow `idx` transitions blocked apply (`ignoring stale echo during edit sync`) while F2/F3 was recent driver.

**Not the problem:** coalesce starvation (0 `COALESCE`), dirty-flag misses on note slots (0 apply-without-schedule), skipped intermediate note 16ths during slow sweeps.

---

## RC-A — Empty 16th steps never move F2 — **shipped** (`ec4678f`)

**Symptom:** Slow F1 sweep across empty grid steps — F2 motor stays on previous note position until a **note** slot is selected.

**Capture proof (6/6 missing `SEND_F2`):**

```
Select fader: selected empty step at tick 144 (no note)
#DBG dependent_refresh_schedule pos=1 pitch=0
#DBG outbound_step=BEGIN → ARM
No note selected for coarse position
No note selected for fine position
(no SEND_F2, no DONE — 61 BEGIN vs 55 DONE)
```

**Cause:** [`sendCoarseFaderPosition`](../../src/NoteEditManager.cpp) / [`sendFineFaderPosition`](../../src/NoteEditManager.cpp) return false when `getSelectedNoteIdx() < 0`, even though `applySelectNav` updated bracket tick and dirty flags scheduled position refresh.

**Fix:** Anchor from `editManager.getBracketTick()` via `SelectNavigation::noteRelativeTick` when no note selected (mirror [`evaluateDependentFaderRefreshDirty`](../../src/NoteEditManager.cpp) empty path). Add `#DBG outbound_ctx f2 mode=EMPTY_STEP`.

---

## RC-B — Pipeline stuck on empty-step burst — **partial** (`23c2391`)

`SKIP_SEND` + `completeOutboundPipelineAtDone` shipped; 11 orphaned `BEGIN` remain in `session_20260701_120425`. Finish in Option D **RC-D4** — see [`note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md`](note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md).

---

## RC-D — Skip-when-feedback-current (plan step 8)

Not shipped. Send helpers do not yet compare target to `FaderState.lastSent*`; pipeline may still run motor trigger after failed/no-op send. Implement after RC-A.

---

## Shipped in local diff (steps 1–4, stall-fix plan)

| Item | Detail |
|------|--------|
| `lastFeedbackAnchorRelTick_` / `lastFeedbackNotePitch_` | Stamped on successful send + at `DONE` |
| `evaluateDependentFaderRefreshDirty` | Position = rel start tick; pitch = MIDI note |
| `planForSelectDependent(position, pitch)` | F4-only / F2+F3-only / full / skip |
| `requestDependentFaderRefreshFromSelection` | Schedule only when dirty; coalesce re-evaluates at `DONE` |
| `processFaderSelectQuiet` | Refresh only when apply + dirty (no slot-mismatch duplicate) |
| `nextEnabledStep` | Skips disabled plan flags (partial burst routing) |

**Commit + push** this block before or with RC-A fix.

---

## Recommended order

| Step | Status |
|------|--------|
| 1 | Commit dirty-flag / partial-plan work | **Done** (`6e1dacf`) |
| 2 | **RC-A** — empty-step coarse/fine send | **Done** (`ec4678f`) |
| 3 | **RC-B** — pipeline `DONE` + `SKIP_SEND` | **Partial** (`23c2391`) |
| 4 | Option A bracket-tick apply gate | **Done** — insufficient for same-slot dwell |
| 5 | **Option D** D1–D4 | **Next** — [Option D handoff](note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md) |
| 6 | Skip-when-feedback-current | Parked after Option D |
| 7 | HITL verifier tick/pitch alignment | Not started |

Option A does not replace Option D for RC-E same-slot slow crawl — aggressive refresh (quiet gate, stale-echo relax) is the documented next path.

---

## Phase tasks (OpenSpec)

Add to [`openspec/changes/note-edit-fader-feedback-regression/tasks.md`](../../openspec/changes/note-edit-fader-feedback-regression/tasks.md):

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

### 7.10 Option D aggressive refresh

See [`openspec/changes/note-edit-fader-feedback-regression/tasks.md`](../../openspec/changes/note-edit-fader-feedback-regression/tasks.md) §7.10 and [Option D handoff](note_edit_fader_feedback_option_d_aggressive_refresh_handoff.md).

### 7.11 Skip-when-feedback-current (step 8) — parked

- [ ] 7.11.1 Send helpers return false when target equals `lastSent*`
- [ ] 7.11.2 Pipeline skips motor trigger on no-send
- [ ] 7.11.3 Native tests

---

## Primary files

| Area | Files |
|------|--------|
| Outbound coordinator | [`src/NoteEditManager.cpp`](../../src/NoteEditManager.cpp), [`include/NoteEditManager.h`](../../include/NoteEditManager.h) |
| Partial plans | [`include/Utils/NoteEditFaderOutboundPlan.h`](../../include/Utils/NoteEditFaderOutboundPlan.h) |
| Relative tick | [`src/Utils/SelectNavigation.cpp`](../../src/Utils/SelectNavigation.cpp) |
| Tests | [`test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp`](../../test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp) |
| OpenSpec | [`openspec/changes/note-edit-fader-feedback-regression/`](../../openspec/changes/note-edit-fader-feedback-regression/) |

---

## Verification gates

```bash
pio test -e native
pio run -e teensy41-capture-serial
# ask user before upload
pio run -e teensy41-capture-serial -t upload

.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801
```

**Pass criteria after RC-A/B:**

| Check | Expect |
|-------|--------|
| `BEGIN` count | == `DONE` count |
| Empty-step select | `#DBG outbound_ctx f2 mode=EMPTY_STEP` + `SEND_F2` |
| Same-slot dwell | no `dependent_refresh_schedule` |
| Note-slot cross | `dependent_refresh_schedule` + `SEND_F2` within ~10ms |

**Regression:** note-slot fast crossings stay 100% schedule + F2.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | No |
| State transition change? | Minor — empty-step sends use bracket tick; pipeline must always reach `DONE` |

---

## Session notes

- User report: "tiny improvement" on slow sweeps — post-Option A capture `120425` confirms: note slots and empty steps improved; **same-slot dwell (RC-E)** remains dominant gap → Option D handoff.
- Symbol rename `*Outbound*` → fader-feedback vocabulary remains **gated** on capture pass per stall-fix plan.
- RC-A/B shipped; Option A insufficient — continue in Option D handoff, not here.
