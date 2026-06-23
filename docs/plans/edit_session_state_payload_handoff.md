# Handoff — scoped edit pass payload shipped; persistence closeout next

**Date:** 2026-06-23  
**Branch:** `refactor/timeline-data-model`  
**Tip:** `be370f4` — **pushed** to `origin/refactor/timeline-data-model`  
**Status:** **`scoped-edit-pass-payload`** and **`scoped-edit-pass-persistence`** archived — see **`docs/plans/scoped_edit_pass_persistence_handoff.md`**.

Use this doc to continue without re-reading the full prior thread.

---

## Shipped (do not redo)

| Item | Commit / evidence |
|------|-------------------|
| **`edit-session-state`** | `839553b`; archived `openspec/changes/archive/2026-06-23-edit-session-state/` |
| **`scoped-edit-pass-payload`** | `be370f4` firmware + HITL; change folder `openspec/changes/scoped-edit-pass-payload/` — **§1–§6.2 done**, **§6.3 validate + archive open** |
| **EditPass row shape** | `include/EditPass.h` — `target`, `startTick`, `endTick`, `pitch`, `velocity`, `addedEvents`; no **EditChange** / **EditChangeList** |
| **Apply** | `applyNoteEditPass` / `applyNoteEditPassSequence` in `src/EditApply.cpp` |
| **Session undo** | `SessionUndoEntry.editRows` / `redoEditRows`; `buildSessionStoreEditPasses` |
| **SD v5** | `STORAGE_VERSION 5`; tail marker **EPT3**; `writePersistedEditPass` / `readPersistedEditPass` only; v1–v4 rejected |
| **Edit HITL script** | `scripts/host_midi_automation_edit_baseline.py` — arm-then-record prelude (transport off → EMPTY→ARMED → RECORDING starts transport) |
| Native tests | **155/155** at last run (`pio test -e native`) |
| Edit HITL | **PASS** `captures/host_midi_automation_edit_baseline_20260623_232352.json` (`loop_start=0`, `loop_length=1536`, `serial edit verification ok=True`) |

### Behavior locks (still true)

| Path | Behavior |
|------|----------|
| **`exitEditMode`** | commit pending → **`closeNoteEditPass`** → `sessionType → Loop` → deferred save |
| **`cycleEditSession`** | `Loop ↔ Note` toggle only; **no** `closeNoteEditPass` from toggle alone |
| **`isNoteEditActive()`** | `editSession.active` (store open) — **not** `sessionType == Note` |
| Record from EMPTY | **Arm** with transport **stopped** (EMPTY→ARMED), then **record** press starts transport (ARMED→RECORDING, `RECA` loop_start=0) |

### Deleted (grep should stay clean)

- **`EditChange`**, **`EditChangeType`**, **`EditChangeList`**, **`applyEditChangeList`**
- SD v4 edit helpers (`readPersistedEditPassLegacyV4`, `readPersistedEditChange`, …)
- Legacy **`EditPassKind`**, **`noteEditPassIndex`** on **EditPass**

(`NoteMovementUtils::NoteEditChangeKind` is unrelated — live UI move/length/pitch only.)

---

## Locked apply order (remaining)

| Step | Change | Gate |
|------|--------|------|
| **A** ← **start here** | **`scoped-edit-pass-payload` §6.3** | `openspec validate scoped-edit-pass-payload` → **`/opsx:archive`** |
| **B** | **`scoped-edit-pass-persistence`** | **Closed** — archived 2026-06-23 |
| **C** (parallel) | **`m8-edit`** §4 test matrix + archive | native + optional HITL |
| **D** (after B or parallel) | **pool-budget §9** revisit | **Closed** 2026-06-23 — `openspec/specs/note-edit-session-undo/spec.md`; no session **`cloneShared`** push |

**One OpenSpec *implementation* change per chat** unless §6.3 archive only.

---

## Step A — `scoped-edit-pass-payload` closeout (§6.3)

Firmware, native, and edit HITL are done. **Remaining on this change:**

| Task | Status | Command / action |
|------|--------|------------------|
| **6.2** Edit HITL | **Done** | `captures/host_midi_automation_edit_baseline_20260623_232352.json` |
| **6.3** Validate + archive | **Open** | `openspec validate scoped-edit-pass-payload` then `/opsx:archive` |

```bash
pio test -e native   # confirm still green
openspec validate scoped-edit-pass-payload
# /opsx:archive scoped-edit-pass-payload
```

After archive: delta lands in `openspec/specs/timeline-passes/`; active folder moves under `openspec/changes/archive/`.

---

## Step B — `scoped-edit-pass-persistence` (closeout only)

**Read:** `openspec/changes/scoped-edit-pass-persistence/tasks.md` + `design.md`

**Goal:** Document and evidence-gate exit vs toggle paths — **not** firmware feature work unless a scenario fails.

| § | Work |
|---|------|
| **1.2** | Vocabulary note: **`EditSessionType`** (live) vs **`EditPassType`** (stored row) |
| **2** | Exit matrix: **`exitEditMode`** vs **`cycleEditSession`**; scenario entries for pass close + undo/redo |
| **3–4** | Trace merge + deferred-save boundaries (`saveNoteEditPass`, `closeNoteEditPass`, `PERS,result,ok`) |
| **5.2** | Map HITL markers: `NoteEditPassClosed`, scoped post-exit undo/redo |
| **6** | `openspec validate` + native reference tests |

**Do not:** re-implement **EditPass** fields, SD v5 wire, or **applyNoteEditPass**.

**HITL reference (post-payload):** `captures/host_midi_automation_edit_baseline_20260623_232352.json`

---

## Parallel tracks (pick one per session)

### `m8-edit` — test gate + archive

Open: `openspec/changes/m8-edit/tasks.md` §4–§5.

| Task | Notes |
|------|--------|
| **§4.1** | Session undo native matrix — **`note-edit-session-undo-gpio`** landed; extend `test_note_edit_session_undo` if gaps remain |
| **§4.3** | SD exit flush while **PLAYING** — update for **v5** wire (was v4 in task text) |
| **§4.4** | Overdub during note edit + 3-step global undo |
| **§5.3** | `openspec validate m8-edit` + archive |

**§4.7** full HITL optional if overlap Track C tracked separately.

### `note-edit-session-undo-gpio`

Tasks **complete**; folder still under `openspec/changes/note-edit-session-undo-gpio/` — archive when convenient (can batch with m8-edit).

### `long-loop-piano-roll-window`

Display + LOOP_EDIT controls — see `docs/plans/long_loop_piano_roll_overview_enhancement.md`.

### `edit-record-display-length-mode`

Reopened D1–D3 — `openspec/changes/edit-record-display-length-mode/tasks.md`.

### pool-budget §9 (deferred)

**§9 closed 2026-06-23:** main spec **`openspec/specs/note-edit-session-undo/spec.md`**; firmware uses **`SessionUndoEntry.editRows`**; no session-undo **`cloneShared`** push path in production.

---

## Edit HITL — script gotchas (for future runs)

Canonical command: `.cursor/rules/HITL-Edit-Test-Flow.mdc`

| Mistake | Symptom | Fix |
|---------|---------|-----|
| Skip clear when **ARMED** | Stale undo, wrong note count / merges | Clear unless **EMPTY** only |
| Start **transport** before first record press | EMPTY→RECORDING at wrong tick (`loop_start≠0`) | Arm with transport off; second press starts transport |
| `_ensure_midi_clock` before fixture stream | First note ~1 beat late (`m0_tick=192`) | Stream fixture immediately after RECORDING |

---

## Recommended new-chat prompts

### Archive only (quick)

```text
Close scoped-edit-pass-payload §6.3 per docs/plans/edit_session_state_payload_handoff.md.
Run pio test -e native, openspec validate scoped-edit-pass-payload, then /opsx:archive scoped-edit-pass-payload.
```

### Persistence closeout

```text
Continue from docs/plans/edit_session_state_payload_handoff.md — step B only.

/opsx:apply scoped-edit-pass-persistence — investigation closeout tasks (§1.2–§6).
Payload + SD v5 shipped in be370f4; do not redo EditPass row shape.
```

### M8 test gate

```text
/opsx:apply m8-edit — §4.1 session undo native matrix (or §4.3 v5 exit-flush test).
Read openspec/changes/m8-edit/tasks.md; STORAGE_VERSION is 5.
```

---

## Related docs

- Vocabulary: `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`
- Storage: `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- Edit HITL: `.cursor/rules/HITL-Edit-Test-Flow.mdc`
- Naming drift (separate): `docs/plans/naming_drift_scoped_edit_pass_handoff.md` (Track 1 largely shipped in `5fb37a3`)
- Piano roll: `docs/plans/long_record_memory_headroom_piano_roll_handoff.md`
