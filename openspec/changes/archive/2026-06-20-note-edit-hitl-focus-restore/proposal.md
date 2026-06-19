# Proposal — note edit HITL focus / restore bugs (post Phase 2d)

**Change:** `note-edit-hitl-focus-restore`  
**Kind:** bug  
**Status:** **Signed off — ready to archive**  
**Parent:** [note-edit-modification-session](../note-edit-modification-session/)  
**Supersedes:** [edit-focus-selection-drift](../edit-focus-selection-drift/)

## Why

Phase **2d** (`note-edit-focus-reads`) retired `MovingNoteIdentity`. Post-upload HITL baselines `001421` / `001758` showed:

1. **Delete / selection drift** — delete B removed wrong note (AC3/AC5 fail).
2. **Hidden overlap not restored on pitch** — P0 missing after lengthen + move + pitch up (`001758`).

## What (delivered)

- **B1 firmware:** D1+D2 — **NoteRef** delete + select-after-pitch tolerance
- **B2 firmware:** D4+D7 — overlap scratch retention + pitch restore without deselect
- **Verifier:** AC1 + AC5 tolerance / restore-log alignment in `verify_overlap_hidden_ac.py`
- **Sign-off:** AC1–AC5 pass on captures `013630`, `013835`, `014601`

## Non-Goals (unchanged / deferred)

- Parent `edit.ok=true` — Track C insert/reorder, M0 home, split-overlap home → [change-length-commit-rematerialize](../change-length-commit-rematerialize/)
- Reverting Phase 2d focus-only overlap utils

## Evidence

[BUG.md](./BUG.md)
