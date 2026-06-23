# Handoff — edit session state → scoped edit pass payload

**Date:** 2026-06-23  
**Branch:** `refactor/timeline-data-model` (ahead of origin; regression fix `f946d82` on tip)  
**Status:** OpenSpec artifacts drafted; **implementation starts at step 0 below**.

Use this doc to continue in a **new chat** without re-reading the full thread.

---

## Shipped (do not redo)

| Item | State |
|------|--------|
| `scoped-edit-pass-model` firmware | Done (metadata on **EditPass**, materialize, v4 dual-read SD, tests) |
| Note-edit regressions | Fixed in `f946d82` (exit display, in-session undo pass-id filter, close canonicalize) |
| Native tests | 155/155 at last run; edit HITL pass `captures/host_midi_automation_edit_baseline_20260623_174320.json` |
| Naming drift (merge/materialize, memory tiers) | `5fb37a3` |

**Still in firmware (payload change removes):** **`EditChange`**, **`EditChangeList`**, dual-read SD
helpers in `StorageLoopIo.cpp`.

---

## Locked apply order

| Step | Change | Agent | Gate |
|------|--------|-------|------|
| **0** | Archive **`scoped-edit-pass-model`** (task 8.2) | Auto | `openspec validate` + `/opsx:archive` |
| **1** | **`edit-session-state`** ← **start here (first code)** | Auto or Composer | `pio test -e native` + edit HITL |
| **2** | **`scoped-edit-pass-payload`** | Codex high / Sonnet thinking | native + edit HITL; **SD v5** |
| **3** | **`scoped-edit-pass-persistence`** | Auto | close investigation / evidence only |

**One OpenSpec change per chat.** Read only that change’s `tasks.md` + `design.md`.

---

## Step 0 — Archive `scoped-edit-pass-model` (~5 min)

Firmware and tests for this change are complete. Remaining task:

- [ ] `openspec/changes/scoped-edit-pass-model/tasks.md` **8.2** — `/opsx:archive scoped-edit-pass-model`

No firmware edits in this step.

---

## Step 1 — `edit-session-state` (first implementation)

### Goal

Split live session vs stored pass vocabulary; single **`EditSession`** owner on **EditManager**.

| Layer | Type | Field | Values |
|-------|------|-------|--------|
| Live RAM | **`EditSessionType`** | **`EditSession::sessionType`** | `Loop`, `Note`, `ControlChange` |
| Stored pass row | **`EditPassType`** | **`EditPass::passType`** | `Note`, `ControlChange`, `Audio` |

### Read first (only these)

1. `openspec/changes/edit-session-state/tasks.md` — **implementation map**
2. `openspec/changes/edit-session-state/design.md` — D1–D6 only

### Task checklist (summary)

1. **§1** — `EditSessionType` on **EditPass** → **`EditPassType`**, `sessionType` → `passType`
2. **§2** — Add **`EditSession.h`**; retire **`NoteEditSession`**; `EditManager::editSession`
3. **§3** — Delete **`MainEditMode`** / **`cycleMainEditMode`**; add **`cycleEditSession`**,
   **`getEditSessionType()`** (toggle does **not** call **`closeNoteEditPass`**)
4. **§4** — Docs/rules + HITL script string renames if needed
5. **§5** — `pio test -e native`, edit HITL, validate + archive

### Out of scope for step 1

- **`EditChange`** removal — step 2 only
- **`STORAGE_VERSION` 5** — step 2 only
- SD wire layout change — same byte values; rename comments/serializers only

### Primary files

`include/EditPass.h`, `include/EditManager.h`, `src/EditManager.cpp`, `src/NoteEditManager.cpp`,
`include/LoopEditManager.h`, `src/LoopPasses.cpp`, `src/StorageLoopIo.cpp`, `src/Loop.cpp`,
`src/TrackUndo.cpp`, tests under `test/test_edit_apply`, `test/test_note_edit_session_undo`,
`test/test_storage_loop_io`.

### Behavior locks

- **`exitEditMode`**: commit pending actions, **`closeNoteEditPass`**, `sessionType → Loop`, deferred save
- **`cycleEditSession`**: `Loop ↔ Note` toggle only; **no** pass close from toggle alone
- **`passTypeForSession`**: `Note → Note`, `ControlChange → ControlChange`; `Loop` invalid for save

---

## Step 2 — `scoped-edit-pass-payload` (after step 1 merges)

### Read first

1. `openspec/changes/scoped-edit-pass-payload/tasks.md` — **implementation map** (struct + v5 wire)
2. `openspec/changes/scoped-edit-pass-payload/design.md` — D1–D7 only

### Locked SD policy

- **`STORAGE_VERSION 5`**; reject v1–v4 on load → empty start (no migration, no dual-read)
- Canonical edit row on disk: **EditPassType** + **NoteRef** + property fields; **no EditChange**
- New edits tail marker (**EPT3** or successor); delete all legacy SD helpers (design D7 list)

### Locked row semantics

- **Move** → **Update** + **NoteRange** (`startTick`, `endTick`)
- **Length** → **Update** + **Length** (`startTick`, `endTick`)
- Add **Velocity**; **remove** **StartTick** / **EndTick** from **EditPropertyType**
- No “payload” blob vocabulary

Implement in **tasks.md §1→§6** order; run **`pio test -e native`** after §2 and §4.

---

## Step 3 — `scoped-edit-pass-persistence`

Investigation closeout only — regression fixes already shipped. Do **not** duplicate payload work.
See `openspec/changes/scoped-edit-pass-persistence/tasks.md` **Done when** section.

---

## Uncommitted workspace (commit when ready)

OpenSpec folders (not on origin yet):

- `openspec/changes/scoped-edit-pass-model/`
- `openspec/changes/edit-session-state/`
- `openspec/changes/scoped-edit-pass-payload/`
- `openspec/changes/scoped-edit-pass-persistence/`

Also modified: `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`,
`docs/plans/naming_drift_scoped_edit_pass_handoff.md`.

---

## Recommended new-chat prompt (step 0 + 1)

```text
Continue from docs/plans/edit_session_state_payload_handoff.md.

Step 0: archive scoped-edit-pass-model (task 8.2) if not already archived.
Step 1: /opsx:apply edit-session-state — follow tasks.md implementation map only.

Rules:
- One change this session (edit-session-state); no EditChange removal, no STORAGE_VERSION 5.
- Run pio test -e native after each task section (§1, §2, §3, §4).
- cycleEditSession toggles Loop↔Note only; exitEditMode commits+closes+save.
- Match existing code patterns; minimal diff.

After native pass, run edit HITL baseline per .cursor/rules/HITL-Edit-Test-Flow.mdc if firmware changed.
Do not upload Teensy unless I confirm.
```

### Next chat after step 1 (payload only)

```text
Continue from docs/plans/edit_session_state_payload_handoff.md — step 2 only.

/opsx:apply scoped-edit-pass-payload — follow tasks.md section by section (§1→§6).
edit-session-state is merged; EditPassType rename is done.

Run pio test -e native after §2 apply and after §4 SD. STORAGE_VERSION 5, no migration.
Use Codex-high-level reasoning for apply/session-undo/SD sections.
```

---

## Related docs

- Prior naming handoff: `docs/plans/naming_drift_scoped_edit_pass_handoff.md` (Track 1 separate)
- Storage constraints: `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- Vocabulary: `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`
