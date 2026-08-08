# Handover — note-edit session undo/GPIO + pool-budget

**Date:** 2026-06-22  
**Branch:** `refactor/timeline-data-model`  
**Git:** **`loop-ownership-hardening` shipped** — commit `4851881`, archived at `openspec/changes/archive/2026-06-22-loop-ownership-hardening/`  
**Next action:** `/opsx:apply` on **`note-edit-session-undo-gpio`** first, then **`pool-budget`** (groups 1–6, then 9)

**Supersedes:** [`post_m8_openspec_hardening_handoff.md`](post_m8_openspec_hardening_handoff.md) (ownership stack complete)

---

## What the prior session accomplished

### Shipped — loop-ownership-hardening (archived 2026-06-22)

| Area | Status |
|------|--------|
| Overdub idempotency | `Track::startOverdubbing`, `TrackManager::startOverdubbingTrack` |
| Slot resolution | `LoopPool::findById` → nullptr; `Track::loopForSlot` pool-index fallback |
| SD integrity | `startLoopTick` restore; `readPersistedEditsTail` fail-hard; corrupt `slotLoopId` repair |
| Playback prewarm | `TrackManager::prewarmPlaybackRuntime()` at boot + after `loadState` |
| Deferred validate | `Config::deferredValidateMaxDelayMs`, `DeferredValidatePolicy.h`, idle in `main.cpp` |
| Display (same commit) | Overdub piano-roll wrap fixes in `DisplayManager.cpp` |
| HITL | Second overdub pass flags in `host_midi_automation_baseline.py` |
| Native tests | `test_capture_state_guards`, `test_playback_prewarm`, `test_deferred_validate_policy` + extended storage/pool |
| Specs | `capture-state-guards`, `loop-temporal-persistence`, `playback-runtime-prewarm`, `storage-loop-io`; `multi-loop-slots` updated |

**Verification:** `pio test -e native` 119/119; HITL canonical baseline PASS (`captures/host_midi_automation_baseline_20260622_003012.json`).

### OpenSpec planning — not yet committed

These change folders exist locally but were **left untracked** at `4851881`:

```
openspec/changes/note-edit-session-undo-gpio/
openspec/changes/pool-budget/
```

Commit them before or with the first implementation PR if the team wants planning artifacts in git.

```bash
openspec validate note-edit-session-undo-gpio   # OK
openspec validate pool-budget                    # OK
```

---

## Next two OpenSpec changes

| Order | Change | Path | Purpose |
|-------|--------|------|---------|
| **1** | **note-edit-session-undo-gpio** | `openspec/changes/note-edit-session-undo-gpio/` | **`E:`** / **`U:`** display, kind-boundary session undo, **`NoteEditSessionState`**, GPIO **`GpioButtonManager`**, **`cycleNoteEditType`** |
| **2** | **pool-budget** | `openspec/changes/pool-budget/` | Memory-driven pass/undo admission, **`reclaimUnreferencedDisabledPasses`**, global **`U:`** trim (**`PREFERRED_UNDO_DEPTH = 99`**), task 9 small **`E:`** snapshots |

**Apply order (locked):**

```
note-edit-session-undo-gpio  →  pool-budget (tasks 1–6, then 9)
```

**Why gpio before pool-budget §9:** kind-boundary **`pushSessionUndoOnKindChange`** and stable **`EditManager`** commit path must land before replacing **`cloneShared`** session stacks with **EditChange + focus** entries (design D10).

---

## Cross-cutting decisions (locked — do not re-litigate without user)

1. **Two undo layers:**
   - **`E:`** = **`NoteEditSessionUndoStack`** (in-session, pre-commit)
   - **`U:`** = **`GlobalUndoStack`** / **`TrackUndo`** (pass-level after **`closeNoteEditPass`**)

2. **Session undo push policy (gpio):** push only on transitions between **geometry** kinds (add, delete, move, pitch, length). Not on select/nav, same-kind repeats, or length-mode toggle alone. **Add** then **move** = two **`E:`** steps.

3. **Session undo landing (gpio):** after **`sessionUndo`** / **`sessionRedo`** → restore store; **`applyUndoRedoLanding`** → **Select** kind; **`lengthEditingMode = false`**; **`syncNoteEditSessionStateToUi`**.

4. **Display (gpio):** in note edit overlay show sidebar **`E:nn`**; after exit show global **`U:nn`**. **`E:00`** = no geometry undo steps.

5. **Fixed caps removed (pool-budget target):** `MAX_UNDO_HISTORY = 25` and `MAX_CAPTURE_PASSES_PER_LOOP = 25` are **not** the long-term model. Admission = chunk pool + heap reserves; trim under pressure.

6. **Session undo memory (pool-budget D10, task 9):** replace **`cloneShared`** full-store stack entries with **`SessionUndoEntry`** = **`EditChangeList`** + **`NoteEditFocus`** (+ selection). Undo = **`rematerializeEditView`** + **`applyEditChangeList`** + restore focus. Native parity vs clone **before** removing clone path.

7. **Committed vs live edit storage (D9):**
   - **Committed:** **`editPasses[]`** hold **EditChange** deltas (small)
   - **Live session:** **`NoteEditSession.store`** = full **`passes.materialize`** (scales with loop length)
   - **Today’s bug:** **`E:`** push clones full store on every fader move — bad for 128-bar loops

8. **GPIO Option B:** **`GpioButtonManager`** routes to **`MidiButtonActions`** — no parallel edit logic in GPIO layer. Wire behind compile flag for hardware bring-up.

9. **Ownership task group 4** remains **parked** in archive — do **not** add `MAX_EDIT_PASSES_PER_LOOP = 25`; that ships only via **pool-budget** heap admission (no row cap).

---

## Current firmware bugs / facts (starting point)

| Issue | Today | Location |
|-------|-------|----------|
| Session undo too dense | **`pushSessionUndoBeforeMutation`** on every fader move + onEnter | `EditManager.cpp`, `NoteEditManager.cpp`, `EditSelectNoteState.cpp` |
| **`E:`** mislabeled **`U:`** | Sidebar always draws **`U:`**; **`getDisplayUndoCount`** only switches to session stack in start/length/pitch FSM states — not select/move | `DisplayManager.cpp` ~951, `EditManager::getDisplayUndoCount` |
| FSM vs fader drift | Fader move works with **`currentState == nullptr`**; encoder FSM may disagree | `EditManager`, fader handlers |
| GPIO dormant | **`ButtonManager`** never **`setup`/`update`** in `main.cpp` | `main.cpp`, `ButtonManager.*` |
| Fixed pass cap | **`sealCapture`** rejects when **`capturePassCount >= 25`** | `Loop.cpp`, `LoopPasses.h` |
| Fixed undo cap | **`MAX_UNDO_HISTORY = 25`** | `Globals.h`, `TrackUndo.cpp` |
| Session stack clones | **`pushBeforeMutation`** → **`store.cloneShared()`** | `NoteEditSession.h` |
| Disabled passes retain memory | Undo trim does not reclaim disabled pass chunks | — (new in pool-budget) |

| Constant | Value | Location |
|----------|-------|----------|
| `MAX_UNDO_HISTORY` | 25 | `Globals.h` |
| `MAX_CAPTURE_PASSES_PER_LOOP` | 25 | `LoopPasses.h` |
| `POOL_CHUNK_COUNT` | 512 | `LoopEventStore.h` |
| `NoteEditSessionUndoStack::kMaxDepth` | 32 | `NoteEditSession.h` |
| `Config::deferredValidateMaxDelayMs` | (shipped) | `Globals.h` |

---

## Implementation pointers

### 1 — note-edit-session-undo-gpio (start here)

- **Tasks:** `openspec/changes/note-edit-session-undo-gpio/tasks.md` (28 tasks, 7 groups)
- **Skill:** `.cursor/skills/openspec-apply-change/SKILL.md`
- **Design:** D1 **`NoteEditSessionState`**, D2 kind-boundary push, D4 **E/U** display, D5 Option B routing, D6 **`cycleNoteEditType`**, D7 pin map
- **Key files:**

| Area | Files |
|------|-------|
| Session state + undo | `EditManager.*`, `include/NoteEditSession.h` (or new `NoteEditSessionState.h`) |
| Fader/encoder paths | `NoteEditManager.cpp`, `src/EditStates/*`, `MidiFaderActions.*` |
| Display | `DisplayManager.cpp` — **`E:`** vs **`U:`** label + count |
| GPIO | `ButtonManager.*` → **`GpioButtonManager.*`**, `main.cpp` |
| MIDI config | `MidiConfig.h`, `MidiButtonConfig.*`, `MidiButtonActions.cpp` |
| DROID B2.31 | **`NoteEditManager::cycleEditMode`** unchanged (NOTE_EDIT ↔ LOOP_EDIT) |

- **Still uses `cloneShared` for `E:`** until pool-budget §9 — this change only fixes push **frequency** and display/UX
- **Tests:** native transition matrix (task 6.1–6.3); extend HITL edit baseline for **E/U** + kind checkpoints (6.4)
- **Closeout:** task 7.1 validate; 7.2 update **`m8-edit`** task wording

### 2 — pool-budget (second)

- **Tasks:** `openspec/changes/pool-budget/tasks.md` — groups **1–6** then **9**; group **8** dynamic pool growth = **future**
- **Design:** D1 chunk admission, D2 heap admission for **editPass**, D3 config reserves, D4–D7 reclaim + trim, D9–D10 session undo entries
- **Key files:**

| Area | Files |
|------|-------|
| Chunk pool | `LoopEventStore.h`, `LoopEventStore.cpp`, `LoopPasses.h` |
| Pass lifecycle | `Loop.cpp`, `Loop.h` |
| Global undo | `TrackUndo.cpp`, `GlobalUndoStack.h` |
| Orchestration | `TrackManager.cpp`, `Track.cpp`, `main.cpp` |
| Edit commit retry | `EditManager.cpp` |
| Config | `Globals.h` |

- **Remove:** `AtPassCap` → **`PoolExhausted`**; fixed trim at 25
- **Task 9 depends on gpio task 3** (`pushSessionUndoOnKindChange`)
- **Tests:** extend `test_loop_event_store`, `test_edit_apply`, or add `test_pool_budget`; parity clone vs EditChange+focus (9.6–9.7)
- **Archive:** task 7.4 only after groups 1–9 complete

---

## Repo rules the next agent must follow

| Rule | Location |
|------|----------|
| HITL record/overdub baseline | `.cursor/rules/HITL-Test-Flow.mdc` |
| HITL edit baseline | `.cursor/rules/HITL-Edit-Test-Flow.mdc` (if present) |
| Native tests before push | `.cursor/rules/Testing-Workflow.mdc` — `pio test -e native` |
| Default firmware env | `teensy41-capture-serial`; ask before upload | `.cursor/rules/Teensy-*` |
| Loop MIDI constraints | `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` |
| Naming | `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc` — no new top-level nouns without user |
| OpenSpec workflow | `.cursor/rules/OpenSpec-Workflow.mdc` |

**Hot path:** no PSRAM pool walks on record/overdub stop; O(1) chunk counts only for admission checks.

---

## Suggested first commands

```bash
cd /Users/eelkejager/Documents/PlatformIO/Projects/250513-215524-teensy41
git status   # confirm branch; commit OpenSpec planning folders if desired

openspec status --change note-edit-session-undo-gpio
# Read openspec/changes/note-edit-session-undo-gpio/tasks.md
# /opsx:apply — task group 1 (naming/config) or 2 (NoteEditSessionState)

pio test -e native   # after any logic change, before push
```

Optional planning-only commit:

```text
Add OpenSpec plans for note-edit session undo/GPIO and pool-budget.
```

---

## Verification gates

| Change | Native | HITL |
|--------|--------|------|
| **gpio** | `NoteEditSessionState` transitions; geometry-kind undo matrix; session undo/redo landing | Edit baseline: **E/U** + interaction kind checkpoints |
| **pool-budget 1–6** | Admission >25 passes; `PoolExhausted`; reclaim pinning; undo trim to ~99 | Canonical record/overdub baseline if seal/undo behavior changes |
| **pool-budget 9** | Clone vs EditChange+focus parity; 128-bar four **E:** steps without N× clone | Edit baseline overlap round-trip after **E:** refactor |

---

## Model choice: Composer 2.5 vs Codex

| Use | Recommendation |
|-----|----------------|
| **Default implementation** (`/opsx:apply`, task checklists, repo rules, HITL flow) | **Composer 2.5** in Cursor |
| **Hard debugging** (session undo parity clone vs EditChange+focus, overlap restore, reclaim reference pinning, chunk accounting) | **GPT-5.3 Codex High** (or thinking-high subagent via Task) for isolated deep dives |
| **OpenSpec doc-only edits** | Either |

**Practical split:** Composer drives each change task-by-task; escalate to Codex when native parity tests fail or memory invariants are unclear.

---

## Out of scope (do not start)

- Jam D13 / `jam-recording`
- `MAX_EDIT_PASSES_PER_LOOP = 25` as a fixed row cap (use heap admission in pool-budget)
- Dynamic `POOL_CHUNK_COUNT` growth (pool-budget §8 — future)
- **`m8-edit`** §3.3 **`editFlat_`** cleanup (parallel, not blocked)
- Upload Teensy without user confirmation
- Retire hold-to-pitch until post-gpio HITL (open question in gpio design)

---

## Prior conversations

- Ownership hardening + display wrap: agent transcript `289ab05c-e87e-4bf6-90b8-f57b08e54245`
- Original hardening stack planning: agent transcript `d7259bd4-91a1-4eb7-8c72-d9fd6bf7a924`
