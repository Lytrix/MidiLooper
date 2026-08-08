# Note edit session action geometry — transaction baseline ownership bugfix

**Status:** In progress (2026-08-05)  
**OpenSpec:** [`openspec/changes/edit-session-action-geometry/`](../../openspec/changes/edit-session-action-geometry/)  
**Architecture review:** [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/edit-session-action-geometry/ARCHITECTURE-REVIEW.md)  
**Branch:** `bugfix/note-edit-transaction-baseline`

## Symptom

Moving notes works. Shortened / hidden / removed overlapping notes do not commit; overlapping notes keep original length after reselect. Across HITL captures the committed loop shrinks (`flatEvents` 172 → 148) and macro commit warns `non-canonical store (check=2)` (`LinearNoteOff`).

**Primary evidence:** [`captures/session_20260805_013428.log`](../../captures/session_20260805_013428.log), [`captures/session_20260805_011000.log`](../../captures/session_20260805_011000.log).

## Root causes (proven)

1. **Same-pitch closure** — `buildEditClosureNoteIds` seeds `baselineMap` from `focus.last.pitch` only; cross-pitch neighbours never become candidates.
2. **No pitch gate in analyze** — tick-only classify would hide unrelated pitches once baseline widens.
3. **Mutable baseline** — per-tick enrich + prune deletes hidden notes' baseline entries; apply path also writes into `baselineMap`.
4. **Mid-edit noteId mint** — ids allocated in the geometry pipeline are absent from capture materialize; Delete rows no-op on replay.
5. **Projection mismatch** — analyze uses projected spans; resolve uses unprojected baseline → inverted ends → `LinearNoteOff`.

## Agreed behaviour (user 2026-08-05)

- Overlap scope = **mover's current pitch lane** (follows pitch changes).
- Transaction baseline = **immutable full-loop snapshot** at edit-driver boundary; rebuild ownership properly (not a minimal prune-only patch).

## Phases

| Phase | Scope | Status |
|-------|-------|--------|
| 0 | Branch/commit WIP; ARCHITECTURE-REVIEW; this plan; Q14 in design.md | Done |
| 1 | Pitch-lane gate in `analyzeEditSessionInteractions` | Done |
| 2 | Immutable full-loop `baselineMap`; remove enrich/prune and apply writers | Done |
| 3 | noteId assign only at session open | Done |
| 4 | Resolve on projected baseline; clamp inverted spans | Done |
| 5 | Retire `overlapNotes` from commit/filter (4.5 / 4.5b); apply LIFO fallbacks kept behind regression tests | Done |
| 6 | Native + device verification | Native **742/742**; `teensy41-capture-serial` **SUCCESS** — device HITL pending user flash |

## Out of scope

- OpenSpec task 4.3a (Add/Delete through pipeline) remains open.

## Pre-implementation review

### Ready

- Call sites for pipeline, baseline enrich, pre-commit baseline-diff, and noteId assign traced.
- Capture evidence shows Move-only actions and shrinking `flatEvents`.

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Cross-pitch overlap | Same pitch lane only; lane follows edited causing pitch |
| Fix depth | Rebuild immutable baseline ownership (not minimal prune patch) |
| Q14 | Polyphonic shorten across pitches deferred; analyze pitch-gates |

### Open before coding

None for Phases 1–4. Phase 5 workaround removal is incremental behind tests.

### Proceed?

YES — ownership change approved via ARCHITECTURE-REVIEW Phase 2 gate.
