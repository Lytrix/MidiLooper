# Handoff — scoped edit pass payload (step 2)

**Date:** 2026-06-23  
**Branch:** `refactor/timeline-data-model`  
**Tip:** `839553b` — **3 commits ahead** of `origin/refactor/timeline-data-model` (not pushed at handoff time)  
**Status:** **`edit-session-state` shipped** — **start step 2 in a new chat**.

Use this doc to continue without re-reading the full prior thread.

---

## Shipped (do not redo)

| Item | Commit / evidence |
|------|-------------------|
| **`scoped-edit-pass-model`** | Archived `openspec/changes/archive/2026-06-23-scoped-edit-pass-model/` |
| **`edit-session-state`** | `839553b`; archived `openspec/changes/archive/2026-06-23-edit-session-state/`; spec `openspec/specs/note-edit-session-state/spec.md` |
| **`EditSession`** on **EditManager** | `include/EditSession.h`; `editSession` replaces `NoteEditSession` |
| **`EditPassType`** / **`passType`** on **EditPass** | Renamed from `EditSessionType` / `sessionType` on stored rows |
| **`cycleEditSession`** / **`getEditSessionType()`** | `MainEditMode` / `cycleMainEditMode` removed |
| Note-edit regressions (pre-payload) | `f946d82` |
| Native tests | **155/155** at last run |
| Edit HITL | **PASS** `captures/host_midi_automation_edit_baseline_20260623_194854.json` |

### Behavior locks (still true after `839553b`)

| Path | Behavior |
|------|----------|
| **`exitEditMode`** | commit pending → **`closeNoteEditPass`** → `sessionType → Loop` → deferred save |
| **`cycleEditSession`** | `Loop ↔ Note` toggle only; **no** `closeNoteEditPass` from toggle alone |
| **`sendEditSessionChange`** | **must** set `editSession.sessionType` (fixed in same session as HITL) |
| **`isNoteEditActive()`** | `editSession.active` (store open) — **not** `sessionType == Note` |
| **`getEditSessionType()`** | loop vs note **UI** routing |

### Still in firmware (step 2 removes)

- **`EditChange`**, **`EditChangeType`**, **`EditChangeList`**
- **`applyEditChangeList`** and dual-read v4 SD helpers in **`StorageLoopIo.cpp`**
- Legacy **`EditPassKind`**, **`noteEditPassIndex`** on **EditPass**

---

## Locked apply order (remaining)

| Step | Change | Gate |
|------|--------|------|
| **2** ← **start here** | **`scoped-edit-pass-payload`** | `pio test -e native` after §2 + §4; edit HITL; archive |
| **3** | **`scoped-edit-pass-persistence`** | investigation closeout only — no payload duplication |

**One OpenSpec change per chat.** Read only that change’s `tasks.md` + `design.md` (D1–D7 for payload).

---

## Step 2 — `scoped-edit-pass-payload`

### Read first

1. `openspec/changes/scoped-edit-pass-payload/tasks.md` — **implementation map**
2. `openspec/changes/scoped-edit-pass-payload/design.md` — **D1–D7 only**

### Goal

Single **EditPass** row shape (RAM + SD); delete **EditChange** everywhere.

### Target note row (RAM + v5 wire)

```cpp
EditPassType passType;
EditActionType actionType;
EditPropertyType propertyType;  // add NoteRange, Velocity; drop StartTick/EndTick
NoteRef target;
uint32_t startTick, endTick;
uint8_t pitch, velocity;
MidiEventVec addedEvents;       // Create only
// DELETE: EditChangeList changes, EditPassKind kind, noteEditPassIndex
```

### Locked row semantics

| User edit | actionType | propertyType | Fields |
|-----------|------------|--------------|--------|
| Add | Create | None | `addedEvents` |
| Delete | Delete | None | `target` |
| Move | Update | **NoteRange** | `target`, `startTick`, `endTick` |
| Length | Update | **Length** | `target`, `startTick`, `endTick` |
| Pitch | Update | **Pitch** | `target`, `pitch` |
| Velocity | Update | **Velocity** | `target`, `velocity` |

No “payload” blob vocabulary.

### Locked SD policy

- **`STORAGE_VERSION 5`**; `loadState` rejects v1–v4 → empty start (**no migration**, **no dual-read**)
- New edits tail marker (**EPT3** or successor); delete legacy SD helpers (design **D7** list)
- Canonical wire per row: `id`, `passType`, `editPassIndex`, `state`, `actionType`, `propertyType`, `target`, ticks, pitch, velocity, `addedEventCount` × **MidiEvent**

### Task order (`tasks.md`)

| § | Work | Native gate |
|---|------|-------------|
| **1** | Row model — **EditPropertyType**, **EditPass** fields, commit builders | — |
| **2** | **`applyNoteEditPass`**; delete **`applyEditChangeList`** / **EditChange** | **`pio test -e native`** |
| **3** | Session undo row shape (**SessionUndoEntry**) | — |
| **4** | SD v5 read/write; delete v4 helpers | **`pio test -e native`** |
| **5** | Grep cleanup — zero **EditChange** refs | — |
| **6** | Full native + edit HITL + validate + archive | both |

### Primary files

`include/EditPass.h`, `include/NoteEditFocus.h`, `src/EditManager.cpp`, `src/Loop.cpp`,
`src/EditApply.cpp`, `src/LoopPasses.cpp`, `src/StorageLoopIo.cpp`, `src/StorageManager.cpp`,
`include/NoteEditSessionUndo.h`, `src/NoteEditSessionUndo.cpp`,
`test/test_edit_apply`, `test/test_note_edit_session_undo`, `test/test_storage_loop_io`.

### Out of scope for step 2

- **`EditSessionType`** / **`cycleEditSession`** — shipped in step 1
- CC edit UI/apply
- Tick delta SD compaction (m8-edit §6.1)
- Velocity HITL (enum + apply land; UI control deferred)

---

## Step 3 — `scoped-edit-pass-persistence` (after step 2)

Investigation closeout only. Regression fixes already in **`f946d82`**.  
See `openspec/changes/scoped-edit-pass-persistence/tasks.md` **Done when** section.  
Do **not** duplicate payload row work.

---

## Recommended new-chat prompt (step 2 only)

```text
Continue from docs/plans/edit_session_state_payload_handoff.md — step 2 only.

/opsx:apply scoped-edit-pass-payload — follow tasks.md section by section (§1→§6).
edit-session-state is merged (839553b); EditPassType rename is done.

Rules:
- One change this session (scoped-edit-pass-payload) only.
- STORAGE_VERSION 5, no migration, no dual-read v4.
- Run pio test -e native after §2 apply and after §4 SD.
- Delete EditChange / EditChangeList / applyEditChangeList when §2 lands.
- Match existing code patterns; minimal diff.

After native pass, run edit HITL per .cursor/rules/HITL-Edit-Test-Flow.mdc if firmware changed.
Do not upload Teensy unless I confirm.
```

### Next chat after step 2 (persistence only)

```text
Continue from docs/plans/edit_session_state_payload_handoff.md — step 3 only.

/opsx:apply scoped-edit-pass-persistence — closeout tasks only; payload is merged.
Do not re-implement EditPass row shape or SD v5.
```

---

## Related docs

- Vocabulary: `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`
- Storage constraints: `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- Naming drift (separate track): `docs/plans/naming_drift_scoped_edit_pass_handoff.md`
- Edit HITL flow: `.cursor/rules/HITL-Edit-Test-Flow.mdc`
