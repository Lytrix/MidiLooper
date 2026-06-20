## Why

M1–M7 shipped capture storage; **m8-rename** and **timeline-pass-model** aligned vocabulary
and **`LoopPasses`**. M8 completes the **note-edit** layer: **NoteEditSession** RAM editing,
**saveNoteEditPass** persistence, **NoteEditPassClosed** undo, and SD survival on exit — without
collapsing edits into capture passes on every tweak.

## Prerequisites (done)

| Change | Status |
|--------|--------|
| **`m8-rename`** | Archived [`2026-06-18-m8-rename`](../archive/2026-06-18-m8-rename/) |
| **`timeline-pass-model`** | Archived [`2026-06-20-timeline-pass-model`](../archive/2026-06-20-timeline-pass-model/) |
| **`note-edit-modification-session`** | Archived [`2026-06-20-note-edit-modification-session`](../archive/2026-06-20-note-edit-modification-session/) |

See [`tasks.md`](tasks.md) for shipped vs open checklist.

## What shipped (2026-06-20)

- **NoteEditSession** (`store`, **NoteEditSessionUndoStack**, **editPassIndex**)
- **`saveNoteEditPass()`** → **`passes.editPasses[]`** with **EditChange** + **NoteRef**
- **`closeNoteEditPass()`** → **NoteEditPassClosed** global undo
- **SD v4** **passes** persistence; **synchronous SD flush on note-edit exit** when dirty
- Edit exit no longer collapses session store into capture passes
- Native **110/110**; HITL **M8 pass verify** OK (`20260620_154517`)

## What remains (this change)

- Per-op **NoteEditSession** undo native matrix (§4.1)
- Native overdub-during-edit + 3-step undo test (§4.4)
- SD exit-flush-while-playing test (§4.3)
- **`editFlat_`** / **NoteEditCommit** cleanup (§3.3)
- Archive → merge delta into **`timeline-passes`** (§5.3)

Full HITL edit baseline overlap/insert failures are **tracked separately** (archived bug evidence); not required to close M8 storage/undo.

## Capabilities

### Modified Capabilities

- **`timeline-passes`** (merge target on archive; supersedes legacy `timeline-epochs` delta folder)

## Impact

- **Code:** mostly shipped; remaining §3.3 cleanup + tests
- **Tests:** §4.1, §4.3, §4.4
- **Depends on:** prerequisites above (done)
