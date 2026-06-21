# Handover — post-M8 OpenSpec hardening stack

**Date:** 2026-06-21  
**Branch:** (check `git branch` — likely `refactor/timeline-data-model` or main)  
**Git:** OpenSpec planning **uncommitted** — no firmware changes from this planning thread  
**Next action:** `/opsx:apply` on **`loop-ownership-hardening`** first (see apply order below)

---

## What the prior chat accomplished

### OpenSpec changes created (all artifacts complete; `openspec validate` OK except noted)

| Change | Path | Purpose |
|--------|------|---------|
| **loop-ownership-hardening** | `openspec/changes/loop-ownership-hardening/` | Architecture-review fixes: overdub idempotency, slot `loopId`, `startLoopTick` SD round-trip, SD tail fail-hard, playback prewarm, deferred validate |
| **pool-budget** | `openspec/changes/pool-budget/` | Memory-driven pass/undo limits, **`reclaimUnreferencedDisabledPasses`**, global **`U:`** trim (**`PREFERRED_UNDO_DEPTH = 99`**) |
| **note-edit-session-undo-gpio** | `openspec/changes/note-edit-session-undo-gpio/` | **`E:`** / **`U:`** UX, kind-boundary session undo, **`NoteEditSessionState`**, GPIO **`GpioButtonManager`** |

### Cross-cutting decisions (locked — do not re-litigate without user)

1. **Fixed caps removed (target):** `MAX_UNDO_HISTORY = 25` and `MAX_CAPTURE_PASSES_PER_LOOP = 25` are **not** the long-term model. Admission = chunk pool + heap reserves; trim under pressure.

2. **Two undo layers:**
   - **`E:`** = **`NoteEditSessionUndoStack`** (in-session, pre-commit)
   - **`U:`** = **`GlobalUndoStack`** / **`TrackUndo`** (pass-level after **`closeNoteEditPass`**)

3. **Session undo memory (D10 — `pool-budget`):** Replace **`cloneShared`** full-store stack entries with **`SessionUndoEntry`** = **`EditChangeList`** + **`NoteEditFocus`** (+ selection). Undo = **`rematerializeEditView`** + **`applyEditChangeList`** + restore focus. Parity tests vs clone **before** removing clone path.

4. **Committed vs live edit storage:**
   - **Committed:** **`editPasses[]`** hold **EditChange** deltas (small)
   - **Live session:** **`NoteEditSession.store`** = full **`passes.materialize`** (scales with loop length)
   - **Today’s bug:** **`E:`** push clones full store — bad for 128-bar loops

5. **`loop-ownership-hardening` task group 4** is **parked** — do **not** implement `MAX_EDIT_PASSES_PER_LOOP = 25`. Deferred to **`pool-budget`**.

6. **Apply order:**
   ```
   loop-ownership-hardening  →  note-edit-session-undo-gpio  →  pool-budget (tasks 1–6, then 9)
   ```
   **gpio** before **pool-budget** task 9 (kind-boundary push policy + stable **`EditManager`**).

### Also updated

- `.cursor/rules/OpenSpec-Workflow.mdc` — lists all three active changes

### Not committed (user declined commit this session)

```
openspec/changes/loop-ownership-hardening/
openspec/changes/pool-budget/
openspec/changes/note-edit-session-undo-gpio/
.cursor/rules/OpenSpec-Workflow.mdc
```

Do **not** commit `captures/`, `scripts/__pycache__/`, `.cursor/commands/` unless user asks.

---

## Validation status

```bash
openspec validate note-edit-session-undo-gpio   # OK
openspec validate pool-budget                    # OK
openspec validate loop-ownership-hardening       # FAIL — spec formatting (SHALL/MUST; timeline-passes DEFERRED stub)
```

Fix **`loop-ownership-hardening`** spec deltas before archive, or rewrite DEFERRED `timeline-passes` as proper REMOVED/parked note per OpenSpec schema.

---

## Implementation pointers

### loop-ownership-hardening (start here)

- **Tasks:** `openspec/changes/loop-ownership-hardening/tasks.md`
- **Skip:** §4 (parked → pool-budget)
- **Key files:** `Track.cpp`, `TrackStateMachine.cpp`, `LoopPool.cpp`, `StorageLoopIo.cpp`, `StorageManager.cpp`, `TrackPlaybackRuntime.h`, `main.cpp`
- **Evidence map:** `openspec/changes/loop-ownership-hardening/ARCHITECTURE-REVIEW.md`
- **Tests:** extend `test_storage_loop_io`; native full suite before push
- **HITL:** canonical baseline if SD/temporal behavior changes (user confirms upload)

### note-edit-session-undo-gpio (second)

- **Tasks:** `openspec/changes/note-edit-session-undo-gpio/tasks.md`
- **Skill:** read `.cursor/skills/openspec-apply-change/SKILL.md` when applying
- **Key files:** `EditManager.*`, `NoteEditSession.h`, `DisplayManager.cpp`, `MidiButtonActions.cpp`, `ButtonManager` → **`GpioButtonManager`**, `main.cpp`
- **Still uses `cloneShared` for `E:`** until pool-budget §9 — gpio change only fixes push **frequency** and display

### pool-budget (third)

- **Tasks:** `openspec/changes/pool-budget/tasks.md` — groups **1–6** then **9** (group **8** = dynamic pool growth, future)
- **Design D9–D10:** session undo small entries
- **Spec:** `openspec/changes/pool-budget/specs/note-edit-session-undo/spec.md`
- **Key files:** `LoopEventStore.*`, `Loop.cpp`, `TrackUndo.cpp`, `TrackManager.cpp`, `Globals.h`, `EditManager.cpp`
- **Remove:** `AtPassCap`, fixed trim at 25

---

## Repo rules the next agent must follow

| Rule | Location |
|------|----------|
| HITL record/overdub baseline | `.cursor/rules/HITL-Test-Flow.mdc` |
| Native tests before push | `.cursor/rules/Testing-Workflow.mdc` — `pio test -e native` |
| Default firmware env | `teensy41-capture-serial`; ask before upload | `.cursor/rules/Teensy-*` |
| Loop MIDI constraints | `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` |
| Naming | `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc` — no new top-level nouns without user |
| OpenSpec workflow | `.cursor/rules/OpenSpec-Workflow.mdc` |

**Hot path:** no PSRAM pool walks on record/overdub stop; O(1) chunk counts only.

---

## Current firmware facts (for memory work)

| Constant | Value | Location |
|----------|-------|----------|
| `MAX_UNDO_HISTORY` | 25 | `Globals.h` |
| `MAX_CAPTURE_PASSES_PER_LOOP` | 25 | `LoopPasses.h` |
| `POOL_CHUNK_COUNT` | 512 | `LoopEventStore.h` |
| `NoteEditSessionUndoStack::kMaxDepth` | 32 | `NoteEditSession.h` |
| Session push | `store.cloneShared()` | `EditManager::pushSessionUndoBeforeMutation` |

Disabled capture passes **keep chunk refs**; undo trim does not free them today — **`reclaimUnreferencedDisabledPasses`** is new work in pool-budget.

---

## Suggested first commands for next agent

```bash
cd /Users/eelkejager/Documents/PlatformIO/Projects/250513-215524-teensy41
openspec status --change loop-ownership-hardening
# Read openspec/changes/loop-ownership-hardening/tasks.md
# /opsx:apply or implement task group 1 first
pio test -e native   # after any logic change, before push
```

Optional: commit planning artifacts only if user requests:

```text
Add OpenSpec plans for ownership hardening, pool-budget, and note-edit session undo/GPIO.
```

---

## Model choice: Composer 2.5 vs Codex

| Use | Recommendation |
|-----|----------------|
| **Default implementation** (`/opsx:apply`, task checklists, repo rules, HITL flow) | **Composer 2.5** in Cursor — integrated with rules, upload prompts, PlatformIO |
| **Hard debugging** (session undo parity clone vs EditChange+focus, overlap restore, reclaim reference pinning, chunk accounting) | **GPT-5.3 Codex High** (or thinking-high subagent via Task) for isolated deep dives |
| **OpenSpec doc-only edits** | Either; Composer is enough |

**Practical split:** Composer drives each change task-by-task; escalate to Codex when native parity tests fail or memory invariants are unclear. HITL remains manual regardless of model.

---

## Out of scope (do not start)

- Jam D13 / `jam-recording`
- `MAX_EDIT_PASSES_PER_LOOP = 25` in ownership-hardening
- Dynamic `POOL_CHUNK_COUNT` growth (pool-budget §8 — future)
- Upload Teensy without user confirmation

---

## Prior conversation

Full thread context: agent transcript `d7259bd4-91a1-4eb7-8c72-d9fd6bf7a924` (architecture review → pass limits → memory-driven pool-budget → gpio review → D10 small session snapshots).
