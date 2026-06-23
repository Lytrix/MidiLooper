# Scoped edit pass model — tasks

## 1. Data Model

- [x] 1.1 Add **EditSessionType**, **EditActionType**, and **EditPropertyType** to `include/EditPass.h`.
- [x] 1.2 Extend **EditPass** with `sessionType`, `editPassIndex`, `actionType`, and `propertyType` while preserving current note-edit fields during migration.
- [x] 1.3 Add **ControlChangeRef** as the CC target counterpart to **NoteRef**.
- [x] 1.4 Keep **EditPassState** as the undo/redo toggle state; do not model undo as **EditActionType**.
- [x] 1.5 Update naming comments in `include/EditPass.h` to mark **EditChange** / **EditChangeType** as legacy note-edit payload until scoped rows fully replace it.

## 2. Note Edit Migration

- [x] 2.1 Update **saveNoteEditPass** to create **EditPass** rows with `sessionType = EditSessionType::Note`.
- [x] 2.2 Map existing note edits to `EditActionType::Create`, `Update`, or `Delete`.
- [x] 2.3 Map note update edits to **EditPropertyType** values (`Pitch`, `Length`, `StartTick`, `EndTick`, or `None` when not applicable).
- [x] 2.4 Keep **NoteEditSession**, **NoteEditKind**, and **NoteEditSessionState** as live UI/session vocabulary; do not replace them with stored edit action names.

## 3. Materialize

- [x] 3.1 Replace **EditPassKind** materialize dispatch with **EditSessionType** dispatch in `src/LoopPasses.cpp`.
- [x] 3.2 Route `EditSessionType::Note` rows through the note edit pass apply path.
- [x] 3.3 Add a `ControlChange` dispatch stub that is explicit and no-op until CC edit behavior is implemented.
- [x] 3.4 Preserve materialize order: merge capture passes first, then apply active **editPasses[]** in storage order.

## 4. Undo / Redo

- [x] 4.1 Generalize undo metadata toward scoped **editPassId** rows plus **EditSessionType** while preserving **NoteEditPassClosed** behavior.
- [x] 4.2 Keep undo behavior as **EditPassState::Disabled** and redo as **EditPassState::Active**.
- [x] 4.3 Wire **ControlChangeEditPassClosed** to the same pass-state toggling path when CC edit rows exist.
- [x] 4.4 Ensure undo/redo does not append **EditActionType::Create** or **EditActionType::Delete** rows.

## 5. SD Persistence

- [x] 5.1 Design the scoped **editPass** SD tail layout for **EditSessionType**, **EditActionType**, **EditPropertyType**, and scoped target/payload.
- [x] 5.2 Add dual-read migration for current v4 **EditPassKind::NoteEdit** rows.
- [x] 5.3 Keep read failure fail-hard for truncated or corrupt **editPasses** tails.
- [x] 5.4 Add save/reload native coverage for scoped note edit rows.

## 6. Docs And Rules

- [x] 6.1 Update `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc` with scoped **EditPass** vocabulary.
- [x] 6.2 Update `README.md` loop storage vocabulary with **EditSessionType**, **EditActionType**, and **EditPropertyType**.
- [x] 6.3 Update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` to explain scoped edit rows and undo as pass-state toggling.
- [x] 6.4 Remove future-facing **EditPassKind** guidance from active docs; keep historical OpenSpec archives unchanged.

## 7. Tests

- [x] 7.1 Update `test/test_edit_apply` for scoped note edit rows and action/property mapping.
- [x] 7.2 Update `test/test_storage_loop_io` for scoped SD round-trip.
- [x] 7.3 Add undo/redo native coverage that proves pass-state toggling does not create new stored edit actions.
- [x] 7.4 Run `pio test -e native`.

## 8. Archive Gate

- [x] 8.1 Run `openspec validate scoped-edit-pass-model`.
- [x] 8.2 **`/opsx:archive`** — no firmware work left; payload + session-state are follow-ups.

## 9. Follow-up apply order (locked)

1. **`edit-session-state`** — rename only; no SD v5.
2. **`scoped-edit-pass-payload`** — row model + SD v5 + delete **EditChange**.
3. **`scoped-edit-pass-persistence`** — close investigation / evidence only.
4. **ControlChange** edit — after **long-loop-piano-roll-window**.
