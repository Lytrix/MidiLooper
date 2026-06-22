# M8 edit — task status

**Change:** `m8-edit` — **NoteEditSession** + **editPass** op-lists (retire edit-collapse bridge).

**Last updated:** 2026-06-20 (after `4deec9f` — timeline-pass-model + SD exit flush).

| Gate | Status |
|------|--------|
| Prerequisite **`m8-rename`** | **Done** — archived [`2026-06-18-m8-rename`](../archive/2026-06-18-m8-rename/) |
| Prerequisite **`timeline-pass-model`** | **Done** — archived [`2026-06-20-timeline-pass-model`](../archive/2026-06-20-timeline-pass-model/); main spec [`timeline-passes`](../../specs/timeline-passes/spec.md) |
| **`note-edit-modification-session`** (overlap A1+B1) | **Done** — archived [`2026-06-20-note-edit-modification-session`](../archive/2026-06-20-note-edit-modification-session/) |
| **This change (`m8-edit`)** | **In progress** — core edit storage shipped; test matrix + archive gate remain |
| Overlap HITL (insert/reorder, mover home) | **Separate** — evidence in archived bug changes; does **not** block M8 storage/undo |

---

## Shipped (firmware + docs)

| Area | State |
|------|--------|
| **LoopPasses** | `recordPass`, `overdubPasses`, `editPasses[]` on `Loop`; `LoopPasses::materialize` |
| **Note edit commit** | **`saveNoteEditPass()`** → one **editPass** row per completed action |
| **Edit pass boundary** | **`closeNoteEditPass()`** → **`NoteEditPassClosed`** global undo (batch of **editPassId** values) |
| **Capture undo** | **`RecordPassAdded`**, **`OverdubPassAdded`** (replaces **TakeCommitted**) |
| **NoteEditSession** | `store`, **`NoteEditSessionUndoStack`**, **`editPassIndex`** on `EditManager` |
| **Live edit path** | Mutate **NoteEditSession.store** before **saveNoteEditPass**; overlap **EditChange** at commit |
| **SD v4** | **`StorageLoopIo`** persists **passes** per pool slot (**editPasses** tail) |
| **SD on exit** | **`exitEditMode`** → **`requestUrgentEditSave`** + **synchronous `processEditAutosave`** (survives reboot while playing) — HITL `20260620_154517` |
| **Collapse on exit** | Removed — edit exit no longer flushes session into capture passes |
| **Native** | **110/110** (`test_edit_apply`, `test_storage_loop_io`, `test_loop_take_survival`, …) |
| **HITL M8 pass verify** | **`NoteEditPassClosed`**, in-edit undo/redo, post-exit undo/redo — **OK** (`20260620_154517`) |
| **Docs** | [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) passes model |

---

## Residual bridge (post-§3.3b)

| Item | Location | Target |
|------|----------|--------|
| **`editFlat_`** | `Loop.h` / `Loop.cpp` | **Done** — derived cache behind `midiEvents()` only; bridge APIs removed |
| **Periodic autosave while playing** | `main.cpp` gates `processEditAutosave` when transport active | Exit path is synchronous; periodic path still idle-only (see §4.3) |

**§3.3a done:** **`NoteEditCommit`** / **`pushUndoSnapshot`** removed from `GlobalUndoStack.h`, `TrackUndo`, docs.

**Do not mark M8 archived** until §4–§5 gates below are satisfied.

---

## 0. Prerequisites — done

- [x] 0.1 **m8-rename** — Take/Capture vocabulary (archived)
- [x] 0.2 **timeline-pass-model** — **LoopPasses**, **saveNoteEditPass**, pass undo kinds (archived)
- [x] 0.3 **note-edit-modification-session** — **NoteEditFocus**, overlap **EditChange** at commit (archived)

---

## 1. Edit + NoteEditSession — done

- [x] 1.1 **EditChange**, **EditChangeType**, **NoteRef**, **EditPass** / **editPassId** on **LoopPasses**
- [x] 1.2 **NoteEditSession** on `EditManager`
- [x] 1.3 **LoopPasses::materialize** → playback/display + revision bump
- [x] 1.4 **saveNoteEditPass()** — append **editPass**; **markEditStateDirty** on change
- [x] 1.5 **closeNoteEditPass()** — **NoteEditPassClosed** for closed **noteEditPass** batch
- [x] 1.6 Edit SD autosave + **synchronous flush on note-edit exit** when dirty

## 2. Edit pass boundaries + overdub during note edit — done (firmware)

- [x] 2.1 Overdub start: **closeNoteEditPass()**; allow capture
- [x] 2.2 Overdub stop: **OverdubPassAdded**; rematerialize **NoteEditSession.store**; **editPassIndex++**
- [x] 2.3 Note edit exit: **closeNoteEditPass()**; urgent SD when dirty (sync flush)
- [x] 2.4 Overdub during note edit (guards audited)
- [x] 2.5 MIDI undo: in note edit → **NoteEditSessionUndoStack**; else global

## 3. Edit paths — mostly done

- [x] 3.1 Mutate **NoteEditSession.store** only before **saveNoteEditPass**
- [x] 3.2 **saveNoteEditPass** on completed edit action (not per CC tick)
- [x] 3.3 Retire **`editFlat_`** as public canonical API — keep derived cache behind `midiEvents()` / document only
- [x] 3.3a Remove **`NoteEditCommit`** / **`pushUndoSnapshot`** dead undo path
- [x] 3.4 SD v4: persist **passes** (capture + **editPasses**)

## 4. Test matrix — open

- [ ] 4.1 **NoteEditSession** per-op undo native tests — geometry **kind-boundary** push via **`pushSessionUndoOnKindChange`** + **`NoteEditSessionState`** owner (see **`note-edit-session-undo-gpio`**); stack smoke test only (`test_note_edit_session_undo_stack`)
- [ ] 4.3 Native or host test: SD dirty + **exit flush while transport playing** (behavior shipped; test TBD)
- [ ] 4.4 Native test: overdub during note edit + 3-step global undo (**NoteEditPassClosed** + **OverdubPassAdded** + **NoteEditPassClosed**)
- [x] 4.5 Save/reload v4 with capture passes + **editPasses** (`test_storage_loop_io`)
- [x] 4.6 `pio test -e native` — all green
- [ ] 4.7 **Full** HITL edit baseline green — M8 pass checks **OK**; overlap/insert scenarios still fail (Track C — propose separate change or extend archived bug specs)

## 5. Docs and archive — open

- [x] 5.1 **LOOP_MIDI_STORAGE_AND_VALIDATION.md** — passes / **NoteEditSession**
- [x] 5.2 Session family documented (**LoopEditSession**, **ControlChangeEditSession** TBD)
- [ ] 5.3 `openspec validate m8-edit`; **`/opsx:archive`** → merge delta into **`timeline-passes`** (not legacy `timeline-epochs`)

## 6. Deferred (out of M8 close)

- [ ] 6.1 Edit compaction by tick/bar window; velocity / CC / paste **EditChangeType** values
- [ ] 6.2 **Parked** display + length-mode UX — [`2026-06-20-parked-edit-record-display-length-mode`](../archive/2026-06-20-parked-edit-record-display-length-mode/)

---

## What to work on next (recommended order)

### A — Close M8 test gate (this change)

1. **§4.1** — `test_note_edit_session` (or extend `test_edit_apply`): per-op session undo before **saveNoteEditPass**
2. **§4.4** — native overdub-during-edit + 3-step global undo
3. **§4.3** — host/SD round-trip test for exit flush while **PLAYING** (assert `State saved successfully (v4)` without stopping transport)
4. **§5.3** — `openspec validate m8-edit` + archive when §4.1–§4.4 done (§4.7 full HITL optional for archive if overlap tracked separately)

### B — Parallel / follow-up (not required to archive m8-edit)

| Item | Where | Notes |
|------|--------|-------|
| Insert restore / move-over-insert | `NoteMovementUtils`, HITL Track C | Archived [`note-move-pitch-overlap-flaky`](../archive/2026-06-20-note-move-pitch-overlap-flaky/) |
| M0 home after overlap round-trip | overlap commit + rematerialize | Same family |
| Split-overlap mover home | HITL `split_overlap_note_*` | [`change-length-commit-rematerialize`](../archive/2026-06-20-change-length-commit-rematerialize/) Track C |
| Display + length-mode UX | Parked change | D1–D3 not started |

### C — After M8 archive (timeline plan)

1. **Hardening** — pool-budget, playback-window polish (propose new change)
2. **JamRecorder** — `jam-recorder` (JamAction / D3)
3. **M10** — Jam entity + Scenes + SD

---

## Suggested `/opsx:apply` slice

Pick **one row** from §A per PR:

```
/opsx:apply m8-edit — §4.1 session undo native matrix
/opsx:apply m8-edit — §4.4 overdub-during-edit undo native test
/opsx:apply m8-edit — §4.3 SD exit flush while playing test
/opsx:apply m8-edit — §3.3 editFlat_ + NoteEditCommit cleanup
```
