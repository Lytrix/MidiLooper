# Handoff — scoped edit pass persistence (step 3)

**Date:** 2026-06-23  
**Branch:** `refactor/timeline-data-model`  
**Tip:** `be370f4` + archive/spec sync (commit if not yet pushed)  
**Status:** **Closed** — **`scoped-edit-pass-persistence`** archived 2026-06-23. Spec merged to `openspec/specs/timeline-passes/spec.md`.

Use this doc to continue without re-reading the full prior thread.

---

## Shipped (do not redo)

| Item | Evidence |
|------|----------|
| **`edit-session-state`** | Archived `openspec/changes/archive/2026-06-23-edit-session-state/` |
| **`scoped-edit-pass-model`** | Archived `openspec/changes/archive/2026-06-23-scoped-edit-pass-model/` |
| **`scoped-edit-pass-payload`** | `be370f4`; archived `openspec/changes/archive/2026-06-23-scoped-edit-pass-payload/` |
| **EditPass row + SD v5** | `include/EditPass.h`, `STORAGE_VERSION 5`, **EPT3**, `applyNoteEditPass` |
| **Session undo rows** | `SessionUndoEntry.editRows` / `redoEditRows` |
| **`scoped-edit-pass-persistence`** | Archived `openspec/changes/archive/2026-06-23-scoped-edit-pass-persistence/` |
| **pool-budget §9** | **Closed** 2026-06-23 — `openspec/specs/note-edit-session-undo/spec.md`; no session **`cloneShared`** push |
| **`note-edit-session-undo-gpio`** | Archived `openspec/changes/archive/2026-06-23-note-edit-session-undo-gpio/` |
| **Main spec** | `openspec/specs/timeline-passes/spec.md` — **EditPassType**, row fields, v5 wire, exit + deferred-save boundaries |
| Native tests | **155/155** (`pio test -e native`) |
| Edit HITL | **PASS** `captures/host_midi_automation_edit_baseline_20260623_232352.json` |

### Behavior locks

| Path | Behavior |
|------|----------|
| **`exitEditMode`** | commit pending → **`closeNoteEditPass`** → `sessionType → Loop` → deferred/urgent save |
| **`cycleEditSession`** | `Loop ↔ Note` toggle only; **no** `closeNoteEditPass` from toggle alone |
| **`isNoteEditActive()`** | `editSession.active` — **not** `sessionType == Note` |
| Record prelude (HITL) | Transport **off** → record arm (EMPTY→ARMED) → record press starts transport (ARMED→RECORDING, `loop_start=0`) |

### Out of scope for this change

- **EditPass** row shape, **EditChange** removal, SD v5 wire — shipped in payload archive
- CC edit UI, velocity HITL, tick-delta SD compaction
- Firmware feature work **unless** investigation isolates a failing boundary

---

## This change — `scoped-edit-pass-persistence` (closed)

**Type:** investigation / closeout — **not** a default firmware refactor.  
**OpenSpec:** `openspec/changes/archive/2026-06-23-scoped-edit-pass-persistence/`

### Why it exists

Formalize **exit vs toggle** persistence boundaries and **deferred-save evidence** after NOTE_EDIT workflows. Original motivation: serial showed `NoteEditPassClosed` while some runs still failed replay checks. Post-payload HITL **PASS** (`20260623_232352`) — remaining work is **documentation, marker matrix, and native gates**, not redoing payload.

### Read first

1. `openspec/changes/scoped-edit-pass-persistence/tasks.md`
2. `openspec/changes/scoped-edit-pass-persistence/design.md` (D1–D5)
3. `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` — stop path, deferred save
4. Reference capture: `captures/host_midi_automation_edit_baseline_20260623_232352.json` (+ matching `_serial.log` if present)

### Done when (closeout criteria)

- [x] Exit matrix documented: **`exitEditMode`** vs **`cycleEditSession`**
- [x] HITL evidence maps **`NoteEditPassClosed`**, **`PERS,result,...,ok`**, scoped post-exit undo/redo
- [x] Native test reference in **`test_note_edit_session_undo`** cited (do not rewrite unless gap found)

### Task order

| § | Work |
|---|------|
| **1.2** | Vocabulary: **`EditSessionType`** (live **EditSession**) vs **`EditPassType`** (stored row) |
| **2** | Exit-path scenario matrix — full exit vs overlay toggle; in-edit + post-exit undo/redo |
| **3** | Merge boundary trace: `commitAllPendingNoteEditActions` → `saveNoteEditPass` → `closeNoteEditPass` |
| **4** | Persistence boundary trace: `requestUrgentEditSave` / `processEditAutosave` / `requestDeferredSaveState` → `writePersistedEditsTail` |
| **5.2–5.3** | Evidence mapping from HITL + native reproduction for isolated boundary |
| **6** | `openspec validate scoped-edit-pass-persistence`; `pio test -e native`; closeout |

### Evidence markers (design D4)

| Marker | Boundary |
|--------|----------|
| `NoteEditPassClosed` | Pass close / global undo push |
| `PERS,request` | Save requested |
| `PERS,result,ok` | Deferred writer completed |
| `Scoped edit pass undone` / `Scoped edit pass redone` | Post-exit global undo |

### Primary code paths

`src/EditManager.cpp` (`exitEditMode`, `cycleEditSession`, `closeNoteEditPass`)  
`src/NoteEditManager.cpp`  
`src/Loop.cpp` (`saveNoteEditPass`, `commitEditAction`)  
`src/StorageManager.cpp`, `src/StorageLoopIo.cpp` (v5 edits tail)  
`scripts/host_midi_automation_edit_baseline.py` (`_verify_session_undo_redo_routing`, persist wait)

### Regression fixes already shipped (reference only)

`f946d82` — visual cache, session undo pass-id filter, close-boundary pass replacement. Do not re-investigate unless HITL regresses.

---

## Recommended new-chat prompt (next track)

```text
Continue from docs/Plans/scoped_edit_pass_persistence_handoff.md.

pool-budget §9 closed — next: m8-edit §4 (native matrix + archive, v4→v5 task text).
```

---

## After persistence closeout

| Track | Notes |
|-------|--------|
| **`m8-edit`** §4 | Native test matrix + archive (update v4 → v5 in task text where needed) — **next** |
| **`long-loop-piano-roll-window`** | Display + LOOP_EDIT controls |
| **`edit-record-display-length-mode`** | D1–D3 live record / length-mode |

---

## Related docs

- Prior payload handoff (superseded): `docs/Plans/edit_session_state_payload_handoff.md`
- Vocabulary: `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`
- Edit HITL: `.cursor/rules/HITL-Edit-Test-Flow.mdc`
- Deferred save: `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`
