# Handover — note-edit session undo/GPIO, then pool-budget §9

**Date:** 2026-06-22  
**Branch:** `refactor/timeline-data-model` (ahead of origin by 2 commits)  
**Git:** **`pool-budget` groups 1–6 shipped** — commit `5ef60c9`, archived at `openspec/changes/archive/2026-06-22-pool-budget/`  
**Next action:** `/opsx:apply` on **`note-edit-session-undo-gpio`**, then pool-budget **task group 9** (from archive)

**Supersedes:** [`post_ownership_gpio_pool_budget_handoff.md`](post_ownership_gpio_pool_budget_handoff.md)

---

## Shipped this session — pool-budget (groups 1–6)

| Area | What changed |
|------|----------------|
| Admission | `CHUNK_RESERVE`, `HEAP_RESERVE_BYTES`; `PoolExhausted`; no fixed pass/undo caps of 25 |
| Global undo | `PREFERRED_UNDO_DEPTH=99`, `trimUndoStackForMemory`, `ABSOLUTE_MAX_UNDO_ENTRIES=512` |
| Reclaim | `PassReclaim`, `Loop::reclaimUnreferencedDisabledPasses`, idle + post-trim orchestration |
| Edit commit | Heap admission in `saveNoteEditPass`; reclaim + one retry in `commitEditAction` |
| Seal retry | `finalizeCommitSideEffects` reclaim + one `commitCapturePass` retry on `SealFailed` |
| Specs | `loop-event-pool-admission`, `pass-reclaim`, `undo-memory-trim`; `timeline-passes` updated |
| Tests | `test_pool_budget` (10 cases); native **129/129** |
| HITL | Canonical baseline PASS — `captures/host_midi_automation_baseline_20260622_004752.json` |

**Not shipped (deferred in archive):** task **§9** `SessionUndoEntry` (EditChange + focus **E:** stack); **§8.1** dynamic `POOL_CHUNK_COUNT` growth.

---

## Apply order (locked)

```
note-edit-session-undo-gpio  →  pool-budget §9 (from archive)
```

**Why gpio first:** `pushSessionUndoOnKindChange` and stable **`NoteEditSessionState`** must exist before replacing **`cloneShared`** session undo with small **EditChange + focus** entries (archive design D10).

---

## Next OpenSpec change — `note-edit-session-undo-gpio`

- **Path:** `openspec/changes/note-edit-session-undo-gpio/`
- **Tasks:** 28 tasks, 7 groups — all unchecked
- **Skill:** `.cursor/skills/openspec-apply-change/SKILL.md`

| Group | Focus |
|-------|--------|
| 1 | `NOTE_EDIT_MODE` rename; GPIO pin map in `Globals.h` |
| 2 | **`NoteEditSessionState`** — single owner for kind/selection |
| 3 | Kind-boundary **`pushSessionUndoOnKindChange`**; **E:** vs **U:** display |
| 4 | **`cycleNoteEditType`**; encoder path cleanup |
| 5 | **`GpioButtonManager`** → **`MidiButtonActions`** (compile flag) |
| 6–7 | Native matrix + HITL edit baseline; validate + archive |

**Key files:** `EditManager.*`, `NoteEditManager.cpp`, `EditStates/*`, `DisplayManager.cpp`, `ButtonManager.*` → `GpioButtonManager.*`, `main.cpp`, `MidiButtonActions.cpp`

---

## Then — pool-budget §9 (from archive)

- **Spec delta:** `openspec/changes/archive/2026-06-22-pool-budget/specs/note-edit-session-undo/spec.md`
- **Tasks:** archive `tasks.md` §9.1–9.8
- **`SessionUndoEntry`** = `EditChangeList` + `NoteEditFocus` + selection; undo = `rematerializeEditView` + apply chain
- **Gate:** native parity clone vs EditChange before removing **`cloneShared`** push path

---

## Locked decisions (do not re-litigate)

1. **`E:`** = `NoteEditSessionUndoStack` (pre-commit); **`U:`** = `GlobalUndoStack` (after `closeNoteEditPass`).
2. Push **E:** only on **geometry** kind boundaries (add/delete/move/pitch/length) — not every fader tick.
3. After session undo/redo → **Select** kind, `lengthEditingMode=false`, `syncNoteEditSessionStateToUi`.
4. GPIO **Option B:** no parallel edit logic in GPIO layer — route to **`MidiButtonActions`**.
5. No `MAX_EDIT_PASSES_PER_LOOP` row cap — heap admission only (already shipped).

---

## Known bugs / gaps (gpio scope)

| Issue | Location |
|-------|----------|
| Dense session undo (every fader move) | `pushSessionUndoBeforeMutation` — `EditManager`, `NoteEditManager`, `EditSelectNoteState` |
| Sidebar shows **U:** during note edit | `DisplayManager.cpp`, `EditManager::getDisplayUndoCount` |
| FSM vs fader kind drift | Fader paths vs encoder FSM |
| GPIO dormant | `ButtonManager` not wired in `main.cpp` |
| **E:** still clones full store | Until §9 — bad on 128-bar loops |

---

## Local workspace notes

**Uncommitted (not in `5ef60c9`):**

- `.cursor/rules/HITL-Test-Flow.mdc` — second overdub pass now default in canonical flow
- `scripts/host_midi_automation_baseline.py` — `--second-overdub-bars` default `2`
- Untracked: `captures/`, `.cursor/commands|plans|skills/`, `scripts/__pycache__/`

**Config constants (post pool-budget):**

| Constant | Value | File |
|----------|-------|------|
| `PREFERRED_UNDO_DEPTH` | 99 | `Globals.h` |
| `MIN_UNDO_DEPTH` | 8 | `Globals.h` |
| `CHUNK_RESERVE` | 16 | `LoopPasses.h` |
| `HEAP_RESERVE_BYTES` | 32 KiB | `Globals.h` |
| `POOL_CHUNK_COUNT` | 512 | `LoopEventStore.h` |

---

## Verification gates

| Change | Native | HITL |
|--------|--------|------|
| **gpio** | `NoteEditSessionState` transitions; geometry-kind **E:** matrix; undo/redo landing | Edit baseline: **E/U** + kind checkpoints (`.cursor/rules/HITL-Edit-Test-Flow.mdc` if present) |
| **pool-budget §9** | Clone vs EditChange+focus parity; 128-bar four **E:** steps | Edit baseline overlap round-trip |

**Before push:** `pio test -e native`  
**Firmware default env:** `teensy41-capture-serial`; ask before upload

---

## Suggested first commands

```bash
cd /Users/eelkejager/Documents/PlatformIO/Projects/250513-215524-teensy41
openspec status --change note-edit-session-undo-gpio
# Read openspec/changes/note-edit-session-undo-gpio/tasks.md
# /opsx:apply — start task group 1 or 2
pio test -e native
```

---

## Out of scope

- Jam D13 / `jam-recording`
- Dynamic `POOL_CHUNK_COUNT` growth (pool-budget §8.1)
- `m8-edit` §3.3 `editFlat_` cleanup (parallel, not blocked)
