# Proposal — note edit modification session (one owner)

**Change:** `note-edit-modification-session`  
**Status:** Design locked (2026-06-19)  
**Supersedes coordination for:** `note-move-pitch-overlap-flaky`, `lengthen-overlap-neighbor-restore`

## Problem

Note geometry edit (move, length, pitch) is split across three overlap engines, three neighbor
ledgers, and parallel truth in `MovingNoteIdentity` scratch vs `NoteEditSession.store` vs Take
rematerialize. HITL edit baseline fails on overlap round-trip, pitch restore, and rematerialize
parity.

## Decision summary (locked)

| Topic | Choice |
|-------|--------|
| Owner | **One** — `NoteMovementUtils::applyGeometryChange` + `NoteEditSession` focus block |
| Baseline timing | **Fader-1 note select** — snapshot from materialized `NoteEditSession.store` |
| Source of truth | **`NoteEditSession.store`** live; **commit baseline** frozen at select; **neighbor ledger** session-only |
| Span vs commit | **A1** — split **overlap footprint** and **commitBaseline** |
| Rematerialize | **B1** — `applyEdits` replay; **pre-commit resolve** ledger → store before `saveEdit` |
| Update scope | **Impacted notes only** — moving note + ledger entries; no full-loop rewrite on each fader tick |

## Scope

- Unified move + length + pitch neighbor logic during **NoteEditSession**
- Baseline map, neighbor ledger, pre-commit resolve, incremental `EditChange` emission
- Native + HITL gates from child bug specs

## Out of scope

- CC / velocity edit (future **ControlChangeEditSession** or focus-only `EditChange` — no neighbor ledger)
- Take capture / commit storage layout
- Full `editFlat_` retirement (m8-edit Decision 7 — parallel track)

## Child changes

Implementation tasks live in [tasks.md](./tasks.md). Child folders keep **BUG evidence** and
scenario-specific AC; they link here for architecture and sequencing.
