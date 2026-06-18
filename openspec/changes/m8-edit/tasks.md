# M8 edit — task status

**Change:** `m8-edit` — Edit op-lists + **NoteEditSession** (replace `editFlat_` bridge).

| Gate | Status |
|------|--------|
| Prerequisite **`m8-rename`** | **Done** — archived [`2026-06-18-m8-rename`](../archive/2026-06-18-m8-rename/) |
| **This change (`m8-edit`)** | **Not started** — no `Edit`, `saveEdit()`, `NoteEditSession`, or `NoteEditSessionCommitted` in firmware |
| Overlap bug (`note-move-pitch-overlap-flaky`) | **Separate** — may land before or in parallel; does **not** complete M8 |

**Shipped today (bridge, not M8):** note edit mutates `Loop::editFlat_` / `mutEditStore()`; exit paths call `flushEditStoreToTakes()`. Global edit undo uses `TrackUndo::pushUndoSnapshot()` → `UndoEntryKind::NoteEditCommit` (full loop snapshot). In-move overlap victims use `EditManager::movingNote.deletedNotes` (session restore only — not undo).

**Do not mark M8 done** until §1–§5 below are complete and `editFlat_` bridge is removed (§3.3).

---

## 0. Prerequisites (m8-rename) — done

Archived 2026-06-18. Unblocks **m8-edit**; out of scope for this checklist.

- [x] 0.1 **Take** / **Capture** vocabulary (`Take`, `TakeType`, `takes[]`, `commitTake()`, `Capture`)
- [x] 0.2 **`TakeCommitted`** global undo kind (replaces `EpochPublished`)
- [x] 0.3 UI: `EditNoteState` base; **`EditState`** name free for struct **Edit**
- [x] 0.4 **`DebugSessionCapture`** rename
- [x] 0.5 Native + teensy41 build green under Take naming

---

## 0b. Interim bridge (M7 + rename — not M8 credit)

Present in firmware; **replace** when §1–§3 ship. Do not extend as the long-term edit model.

| Interim | Location | M8 replacement |
|---------|----------|----------------|
| `editFlat_` / `mutEditStore()` | `Loop.h`, `Loop.cpp` | **NoteEditSession.store** |
| `flushEditStoreToTakes()` / `syncEditFlatToTakes()` | `Loop.cpp`, edit exit paths | **saveEdit()** + `applyEdits()`; no take collapse on every tweak |
| `pushUndoSnapshot()` → **`NoteEditCommit`** | `TrackUndo.cpp`, edit fader enters | **`NoteEditSessionUndoStack`** (in session) + **`NoteEditSessionCommitted`** (on span close) |
| `movingNote.deletedNotes` | `EditManager.h`, move/pitch overlap | Overlap **EditChange** records at **saveEdit**; session restore stays in movement utils until ported |

**Related work (open, not M8):** unified move+pitch overlap (`openspec/changes/note-move-pitch-overlap-flaky/`) — targets bridge + `movingNote`; does not add `edits[]` or span undo.

---

## 1. Edit + NoteEditSession

- [ ] 1.1 `Edit`, `EditChange`, `EditChangeType`, `EditId`, `NoteRef`; `edits[]` on `Loop`
- [ ] 1.2 **`NoteEditSession`** (`store`, `NoteEditSessionUndoStack`, `spanIndex`) on `EditManager`
- [ ] 1.3 `applyEdits(takes, edits)` → playback/display + revision bump
- [ ] 1.4 **`saveEdit()`** — append **Edit** with **EditChange** list; dirty on change
- [ ] 1.5 **`closeNoteEditSpan()`** — add `UndoEntryKind::NoteEditSessionCommitted` (all **Edit** ids in span)
- [ ] 1.6 `autosaveIntervalMs` + edit SD autosave (post-MIDI urgent flush on note-edit exit)

## 2. Span boundaries + overdub during note edit

- [ ] 2.1 Overdub start: `closeNoteEditSpan()`; allow capture
- [ ] 2.2 Overdub stop: `TakeCommitted`; rematerialize **NoteEditSession.store**; `spanIndex++`
- [ ] 2.3 Note edit exit: `closeNoteEditSpan()`; urgent SD if dirty
- [ ] 2.4 Allow overdub during note edit (audit guards)
- [ ] 2.5 MIDI undo: in note edit → **NoteEditSessionUndoStack**; else global

## 3. Edit paths (no Take collapse)

- [ ] 3.1 Mutate **NoteEditSession.store** only before **saveEdit**
- [ ] 3.2 **saveEdit** on completed edit action (not per control-change tick)
- [ ] 3.3 Remove `editFlat_` bridge / `flushEditStoreToTakes()` / `syncEditFlatToTakes()`
- [ ] 3.4 SD v4: persist `edits[]` alongside `takes[]`

## 4. Native test matrix

- [ ] 4.1 NoteEditSession undo: select, add, delete, move coarse/fine, pitch, length (before **saveEdit**)
- [ ] 4.2 **EditChange** + **NoteRef** — multi-change **saveEdit**, no index drift
- [ ] 4.3 SD autosave + post-MIDI exit flush during overdub
- [ ] 4.4 Overdub during note edit + 3-step global undo (**NoteEditSessionCommitted** + **TakeCommitted**)
- [ ] 4.5 Save/reload v4 with `takes` + `edits`
- [ ] 4.6 `pio test -e native` — all green
- [ ] 4.7 HITL edit baseline — `scripts/host_midi_automation_edit_baseline.py`; fixture 2-bar record; combined overlap/add/delete/pitch/exit+undo; capture-serial build; assert **`NoteEditSessionCommitted`** once on exit (see `docs/plans/m8_edit_note_edit_hitl_automation_refinement.md`)

## 5. Docs and archive

- [ ] 5.1 Update `LOOP_MIDI_STORAGE_AND_VALIDATION.md` — Take/Capture/Edit/NoteEditSession
- [ ] 5.2 Document session family: **LoopEditSession**, **ControlChangeEditSession**; playback/jam session **TBD** (M8 implements **NoteEditSession** only)
- [ ] 5.3 `openspec validate m8-edit`; archive → **`timeline-takes`**

## 6. Deferred

- [ ] 6.1 Change compaction by tick/bar span; velocity / control-change / paste **EditChangeType** values

---

## Suggested implementation order (after prerequisites)

1. §1.1–1.4 — types, **NoteEditSession.store**, **saveEdit**, **applyEdits** (keep bridge until §3.3)
2. §1.5, §2 — span close + **NoteEditSessionCommitted** + overdub-during-edit
3. §3 — retire bridge; §3.4 SD
4. §4–§5 — tests, HITL M8 assertions, docs, archive

**Overlap fix:** implement in `NoteMovementUtils` / `NoteEditManager` on current bridge first; wire overlap **EditChange** types when §1.1 lands — avoid rewriting overlap rules twice.
