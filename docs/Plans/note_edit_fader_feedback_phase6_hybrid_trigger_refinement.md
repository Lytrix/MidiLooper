# NOTE_EDIT fader feedback — Phase 6 hybrid trigger

**Kind:** refinement  
**Status:** Implementing (2026-06-30)  
**OpenSpec:** `openspec/changes/note-edit-fader-feedback-regression/`  
**Baseline:** commit `ab1e3b0` (~70% motor update rate on hardware)

## Problem

Phase 4 **D18** (quiet-only dependent outbound) over-corrected RC11: motors stopped updating during F1 navigation (`session_20260630_154418.log`). Phase 4 **D20 25.5** removed F1 bracket follow on F2/F3 geometry moves.

## Decision summary

| ID | Decision |
|----|----------|
| D25 | Hybrid trigger: immediate `NoteSelectDependent` + inline F1 bracket on slot change; quiet refresh supplements with dedupe |
| D26 | PC re-arm (`armNoteEditDroidMotorBank`) only on `SessionOpen`, `LengthModeEnter`, `LengthModeExit` |
| D27 | Restore `scheduleOtherFaderUpdates` F1 bracket on F2/F3/F4 drivers (inline, no outbound preempt) |
| D28 | Keep D19 inbound block during `UserMovingFader1` |
| D29 | Skip `setBracketTick` when move `delta == 0` and bracket already matches |
| D30 | HITL: PC-per-session gate; relax sweep churn gate (immediate slot outbound is intentional) |

## Navigation

- Design detail: `openspec/changes/note-edit-fader-feedback-regression/design.md` § Phase 6  
- RC20–RC22: `openspec/changes/note-edit-fader-feedback-regression/BUG.md`  
- Tests: `test/test_note_edit_fader_feedback/`  
- Verify: `scripts/hitl/verify/note_edit_fader_select_refresh.py`
