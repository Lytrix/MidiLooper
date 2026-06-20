# Handover — timeline pass model + vocabulary

**Branch:** `refactor/timeline-data-model`  
**Last pushed commit:** `fe188eb` — note-edit HITL focus restore (B1/B2), overlap verifier fixes  
**Date:** 2026-06-20  
**Next action:** `/opsx:apply` on [`tasks.md`](./tasks.md) — **not started** (OpenSpec only)

---

## What this chat accomplished

### Shipped / pushed (`fe188eb`)

- **note-edit-hitl-focus-restore** archived — B1 delete/select drift, B2 hidden overlap restore on pitch
- **`scripts/verify_overlap_hidden_ac.py`** — AC1/AC5 tick-tolerant; AC1 counts `Restoring hidden overlap note`
- Native tests 110/110 before push

### In progress locally (uncommitted)

**`m8-pass-vocabulary`** — partial code rename, **not committed**:

- `editPassIndex`, `closeNoteEditPass()`, `noteEditPassIndex` on undo entry (partial)
- `Note edit pass undone/redone` serial strings
- HITL script `--require-m8-pass-verify`
- Docs: `LOOP_MIDI_STORAGE_AND_VALIDATION.md`, `HITL-Edit-Test-Flow.mdc`

**Do not land m8-pass-vocabulary alone** — merge into **timeline-pass-model** apply per tasks §0.1.

### OpenSpec created (validated)

**Change:** [`openspec/changes/timeline-pass-model/`](.)

| Artifact | Purpose |
|----------|---------|
| [proposal.md](./proposal.md) | Remove **Take**; **passes[]** model |
| [design.md](./design.md) | Locked vocabulary + storage shape |
| [specs/timeline-passes/spec.md](./specs/timeline-passes/spec.md) | Requirements delta |
| [tasks.md](./tasks.md) | Implementation checklist |
| This file | Handover |

Run: `openspec validate timeline-pass-model`

---

## Locked pass vocabulary (do not re-litigate without user)

### Lifecycle

```text
Capture  →  pendingCapturePass  →  commitRecordPass / commitOverdubPass  →  passes[]
NoteEditSession  →  saveNoteEditPass (×N)  →  closeNoteEditPass  →  editPass rows in passes[]
```

- **Only “pending” is qualified** — no **committed** on pass nouns, containers, or undo kinds.
- **`passes[]`** = final home (`LoopPasses`). Membership = settled pass.

### Four pass kinds, two storage families

| Kind (prose/logs) | Storage |
|-------------------|---------|
| **recordPass** | `passes.recordPass` optional 0–1, chunk-backed |
| **overdubPass** | `passes.overdubPasses[]`, `mergeSequence` |
| **noteEditPass** | **`editPass`** row, **`EditPassKind::NoteEdit`** |
| **controlChangeEditPass** | **`editPass`** row, **`EditPassKind::ControlChange`** (stub only) |

**Not used:** `Take`, `LoopTimeline`, `committedPasses`, `*PassCommitted`, generic `capturePass` struct, four separate edit arrays.

### Undo kinds (GlobalUndoStack)

- **RecordPassAdded**, **OverdubPassAdded**, **NoteEditPassClosed**
- **ControlChangeEditPassClosed** — enum stub; no UI in this change

### Verbs

- **commitRecordPass** / **commitOverdubPass** (capture)
- **saveNoteEditPass** / **closeNoteEditPass** (note edit; was `saveEdit` / `Edit`)
- **LoopPasses::materialize()** replaces `applyEdits(takes, edits)` — **two-phase** (capture merge then edit overlay); O2 chronological replay **deferred**

### Inner types (unchanged names)

- **EditChange**, **EditChangeType**, **NoteRef** inside **editPass**
- **Capture** struct — live append writer (not renamed to OpenCapture)

---

## Resolved apply defaults (tasks §0)

| Topic | Decision |
|-------|----------|
| PR scope | **One PR**: m8-pass-vocabulary leftovers + timeline-pass-model |
| SD | **Extend v4 in place** + **EditPassKind** byte on edit tail; dual-read if needed |
| PassId | Unified **nextPassId_** |
| Rename | **Edit** → **EditPass**; **editPassId**; **editPassIndex** → **noteEditPassIndex**; **passEditIds** → **noteEditPassIds** |

---

## Soft / follow-up (document, don’t block rename PR)

1. **Record arm vs overdub on empty bar** — at most one **recordPass**; empty-bar capture → **overdub**; defer record arm when recordPass exists (tasks 0.6.1–0.6.2). Full **Track** FSM may need follow-up.
2. **Empty loop, edit without recordPass** — storage MAY omit recordPass; UX deferred (0.6.3).
3. **SD v4 layout spike** — verify **EditPassKind** byte fits before claiming SD task done.
4. **HITL** — run edit baseline once after apply with new undo log strings.

---

## Other active OpenSpec (unchanged this chat)

| Change | Status |
|--------|--------|
| **m8-edit** | Open — undo tests, overdub-during-edit, archive |
| **m8-pass-vocabulary** | Code partial locally; fold into timeline-pass-model |
| **change-length-commit-rematerialize** | Track C — parent `edit.ok` deferrals |
| **note-edit-modification-session** | Partial |
| **edit-focus-selection-drift** | Candidate archive |

See [`.cursor/rules/OpenSpec-Workflow.mdc`](../../../.cursor/rules/OpenSpec-Workflow.mdc).

---

## Key files (today → target)

| Area | Today | Target |
|------|-------|--------|
| Capture storage | `Take` / `takes[]` | `recordPass` + `overdubPasses[]` in **passes** |
| Edit storage | `Edit` / `edits[]` | **editPass** in **passes.editPasses[]** |
| Materialize | `applyEdits()` | **LoopPasses::materialize()** |
| Pending seal | `pendingTake_` | **pendingCapturePass** |
| Undo | `TakeCommitted`, `NoteEditSessionCommitted` | **RecordPassAdded**, **OverdubPassAdded**, **NoteEditPassClosed** |

Primary touch: `include/Loop.h`, `src/Loop.cpp`, `src/EditApply.cpp`, `src/Track.cpp`, `src/TrackUndo.cpp`, `src/StorageLoopIo.cpp`, `include/GlobalUndoStack.h`, `include/Edit.h` → **EditPass.h**.

---

## Verification gates (after apply)

```bash
pio test -e native
# grep: no Take, TakeCommitted, *PassCommitted, committedPasses, struct Edit in product code
openspec validate timeline-pass-model
# HITL: edit baseline per .cursor/rules/HITL-Edit-Test-Flow.mdc (Teensy, user confirms upload)
```

Default firmware env: **teensy41-capture-serial**. Ask before upload.

---

## Suggested first message in new chat

> Continue **timeline-pass-model**: run `/opsx:apply` on [`openspec/changes/timeline-pass-model/tasks.md`](openspec/changes/timeline-pass-model/tasks.md). Merge uncommitted **m8-pass-vocabulary** renames into the same PR. Read [`HANDOFF.md`](openspec/changes/timeline-pass-model/HANDOFF.md) and [`design.md`](openspec/changes/timeline-pass-model/design.md) first.
