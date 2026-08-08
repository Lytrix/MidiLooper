# Phase 8 closeout — note-edit-current-state

**Date:** 2026-08-08  
**Decision:** DEC-029  
**Firmware:** `dev` @ `15c5750` (includes playing-move audition fix)

## Verification gates

| Gate | Result | Evidence |
|------|--------|----------|
| 8.1 Targeted native NOTE_EDIT suites | **PASS** | `test_note_edit_current_state`, `test_note_edit_focus`, `test_edit_session_action_builder`, `test_edit_session_interaction`, `test_resolve_constrained_geometry`, `test_note_edit_session_undo`, `test_note_edit_participating_note`, … |
| 8.2 `pio test -e native` | **PASS** | 969/969 (2026-08-08) |
| 8.3 `pio run -e teensy41-capture-serial` | **PASS** | 2026-08-08 closeout build |
| 8.4 Firmware upload | **Skipped** | Device exercised today on `15c5750` lineage (`session_20260808_115120`); re-flash optional |
| 8.5 HITL edit retest (same-pitch moved-note overlap) | **PASS** | Capture matrix below |
| 8.6 Runtime docs + closeout artifact | **Done** | This file; `PROJECT_STATE.md`, `CURRENT_WORK.md`, `tasks.md` |

## HITL capture matrix (8.5)

Manual sessions on `teensy41-capture-serial` after Phases 1–7 + §12 orthogonal-state shipped.

| Scenario | Capture | Anchor |
|----------|---------|--------|
| §11 step 5.5 smoke — participation without `changedOverlapNoteIds` latch | [`session_20260808_032118`](../../../captures/session_20260808_032118.log) | DEC-030 completion |
| Same-pitch overlap moves + `actions=2` geometry (move + hide/shorten) | [`session_20260808_112202`](../../../captures/session_20260808_112202.log) | @49–58s lane 88/89 moves; @88.669 pitch-then-overlap `HideNote` |
| Repeated playing position moves on long same-pitch note (noteId 184, pitch 106) | [`session_20260808_115120`](../../../captures/session_20260808_115120.log) | @93s scrub; @102–103s `GEOM_APPLY,queue,1,*,1,0` |
| Playing span-crossing audition (Tier-2 preview) | [`session_20260808_115120`](../../../captures/session_20260808_115120.log) | User audible PASS @102–103s |

Pre-migration anchor captures (native fixtures derived from these):

- `session_20260807_021939` — move A then B; B edits A at current span
- `session_20260807_021022` — no stale-baseline restore after A moved

## Implementation review

- **Owner:** `NoteEditCurrentState` inside `EditSession` (DEC-029)
- **Invariant:** Current editable geometry is keyed by `NoteId`; `EditSession.store` is projection only
- **Compatibility removed:** `sessionMovedNoteSpans`, session-moved overlap skip guards, live-store geometry authority in resolver/action builder/commit paths (Phase 7)
- **Remaining track:** `NOTE_EDIT_PROJECTED_STORE_COMPAT` — separate from this change; not a Phase 8 blocker

## Archive prep (next step)

1. `/opsx:archive note-edit-current-state` — merge delta specs into `openspec/specs/` — **Done** 2026-08-08
2. Update `docs/DELIVERABLE_TRACKING.md` when archived — **Done** 2026-08-08
3. Optional: run automated `edit_full` layered HITL when Phase 3.3 device gate resumes
