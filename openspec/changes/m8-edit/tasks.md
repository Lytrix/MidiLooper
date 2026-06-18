## 1. Edit + NoteEditSession

- [ ] 1.1 `Edit`, `EditChange`, `EditChangeType`, `EditId`, `NoteRef`; `edits[]` on `Loop`
- [ ] 1.2 **`NoteEditSession`** (`store`, `NoteEditSessionUndoStack`, `spanIndex`) on `EditManager`
- [ ] 1.3 `applyEdits(takes, edits)` → playback/display + revision bump
- [ ] 1.4 **`saveEdit()`** — append **Edit** with **EditChange** list; dirty on change
- [ ] 1.5 **`closeNoteEditSpan()`** — `UndoEntryKind::NoteEditSessionCommitted` (all **Edit** ids in span)
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
- [ ] 3.3 Remove `flushEditStoreToEpochs` / `syncEditFlatToEpochs`
- [ ] 3.4 SD v4: persist `edits[]` alongside `takes[]`

## 4. Native test matrix

- [ ] 4.1 NoteEditSession undo: select, add, delete, move coarse/fine, pitch, length (before **saveEdit**)
- [ ] 4.2 **EditChange** + **NoteRef** — multi-change **saveEdit**, no index drift
- [ ] 4.3 SD autosave + post-MIDI exit flush during overdub
- [ ] 4.4 Overdub during note edit + 3-step global undo (**NoteEditSessionCommitted** + **TakeCommitted**)
- [ ] 4.5 Save/reload v4 with `takes` + `edits`
- [ ] 4.6 `pio test -e native` — all green

## 5. Docs and archive

- [ ] 5.1 Update `LOOP_MIDI_STORAGE_AND_VALIDATION.md` — Take/Capture/Edit/NoteEditSession
- [ ] 5.2 Document session family: **LoopEditSession**, **ControlChangeEditSession**; playback/jam session **TBD** (M8 implements **NoteEditSession** only)
- [ ] 5.3 `openspec validate m8-edit`; archive → **`timeline-takes`**

## 6. Deferred

- [ ] 6.1 Change compaction by tick/bar span; velocity / control-change / paste **EditChangeType** values
