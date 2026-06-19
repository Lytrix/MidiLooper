# Proposal — note edit modification session (one owner)

**Change:** `note-edit-modification-session`  
**Status:** Design locked (2026-06-19)  
**Supersedes coordination for:** `note-move-pitch-overlap-flaky`, `lengthen-overlap-neighbor-restore`

## Problem

Note geometry edit (move, length, pitch) is split across three overlap engines, parallel
**overlapNotes** scratch on **MovingNoteIdentity**, and truth in `NoteEditSession.store` vs Take
rematerialize. HITL edit baseline fails on overlap round-trip, pitch restore, and rematerialize
parity.

## Decision summary (locked)

| Topic | Choice |
|-------|--------|
| Owner | **One** — `NoteMovementUtils::applyNoteEditChange` + `NoteEditSession` focus block |
| Baseline timing | **Fader-1 note select** — snapshot from materialized `NoteEditSession.store` |
| Source of truth | **`NoteEditSession.store`** live; **commitBaseline** frozen at select; **overlapNotes** on focus |
| Range vs commit | **A1** — split **movingNoteRange** and **commitBaseline** |
| Rematerialize | **B1** — `applyEdits` replay; **pre-commit resolve** overlapNotes → session store before `saveEdit` |
| Update scope | **Impacted notes only** — moving note + **overlapNotes** entries; no full-loop rewrite on each fader tick |

## Scope

- Unified move + length + pitch overlap logic during **NoteEditSession**
- Baseline map, **overlapNotes**, pre-commit resolve, incremental **EditChange** emission
- Native + HITL gates from child bug specs

## Out of scope

- CC / velocity edit (future **ControlChangeEditSession** or focus-only **EditChange** — no **overlapNotes**)
- Take capture / commit storage layout
- Full `editFlat_` retirement (m8-edit Decision 7 — parallel track; legacy Loop API names unchanged)

## Child changes

Implementation tasks live in [tasks.md](./tasks.md). Child folders keep **BUG evidence** and
scenario-specific AC; they link here for architecture and sequencing.

| Change | Focus |
|--------|--------|
| [change-length-commit-rematerialize](../change-length-commit-rematerialize/) | ChangeLength rematerialize Track A/B (**signed off**) |
| [overlap-hidden-note-select](../archive/2026-06-19-overlap-hidden-note-select/) | **`filterSelectableDisplayNotes`** Phase 1 shipped 2026-06-19; spec `openspec/specs/overlap-hidden-note-select/`; Phase 2+ in archived tasks |
| [note-move-pitch-overlap-flaky](../note-move-pitch-overlap-flaky/) | Overlap round-trip AC5–AC7 |
| [lengthen-overlap-neighbor-restore](../lengthen-overlap-neighbor-restore/) | Pitch restore vs move-back |
