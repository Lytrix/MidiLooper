# Scoped edit pass rows — tasks

**Apply after:** `edit-session-state` ( **`EditPassType`** rename must land first).

**Do not re-read:** `scoped-edit-pass-model/design.md` for row semantics — use this file +
`design.md` D1–D7 only.

## Implementation map (read once)

| Area | Primary files | Key symbols today |
|------|---------------|-------------------|
| Row struct | `include/EditPass.h` | `EditPass`, `EditChangeList` → row fields on `EditPass` |
| Commit | `src/EditManager.cpp`, `include/NoteEditFocus.h` | `buildPreCommitEditChanges`, `buildPreCommitOverlapEditChanges`, `commitEditAction`, `closeNoteEditPass` |
| Save pass | `src/Loop.cpp`, `include/Loop.h` | `saveNoteEditPass(EditChangeList)`, `replaceNoteEditPass` |
| Apply | `src/EditApply.cpp`, `src/LoopPasses.cpp` | `applyEditChangeList` → `applyNoteEditPass` |
| Session undo | `include/NoteEditSessionUndo.h`, `src/NoteEditSessionUndo.cpp` | `SessionUndoEntry.changes`, `buildSessionStoreEditChanges`, `applySessionUndoEntry` |
| SD | `src/StorageLoopIo.cpp`, `src/StorageManager.cpp` | `STORAGE_VERSION`, `writePersistedEditPassScoped`, legacy helpers (delete) |
| Tests | `test/test_edit_apply`, `test/test_note_edit_session_undo`, `test/test_storage_loop_io` | all `EditChangeList` fixtures |

**Target `EditPass` note row (RAM + v5 wire — fixed layout, `propertyType` selects apply):**

```cpp
// On EditPass after edit-session-state rename:
EditPassType passType;
EditActionType actionType;
EditPropertyType propertyType;  // add NoteRange, Velocity; drop StartTick/EndTick
NoteRef target;
uint32_t startTick, endTick;    // NoteRange, Length, Create
uint8_t pitch, velocity;
MidiEventVec addedEvents;       // Create only
// DELETE: EditChangeList changes, EditPassKind kind, noteEditPassIndex (use editPassIndex)
```

**`saveNoteEditPass` signature:** replace `EditChangeList` param with populated `EditPass` row
(or builder returning row fields). One row per pass (overlap batching stays in commit path).

**v5 edits tail wire (per row, after `nextPassId` + tail marker + editCount):**  
`id`, `passType`, `editPassIndex`, `state`, `actionType`, `propertyType`,  
`target` (NoteRef 4 fields), `startTick`, `endTick`, `pitch`, `velocity`,  
`addedEventCount`, `addedEventCount × MidiEvent`.  
Bump tail marker from `EPT2` → `EPT3` (or new constant) so v4 transitional tails are rejected
with file version check.

## 1. Row model (no payload blob)

- [x] 1.1 Extend **EditPropertyType**: add **NoteRange**, **Velocity**; **remove** **StartTick** /
      **EndTick**; move commits use **NoteRange** + both ticks.
- [x] 1.2 **`EditPass`**: add row fields (see map); delete **`EditChangeList changes`** and legacy
      **`EditPassKind`** / **`noteEditPassIndex`**.
- [x] 1.3 Replace **`buildPreCommitEditChanges`** / **`buildPreCommitOverlapEditChanges`** output
      with row builder(s); update **`commitEditAction`**, **`closeNoteEditPass`** /
      **`replaceNoteEditPassOnClose`**.

## 2. Apply / materialize

- [x] 2.1 Add **`applyNoteEditPass(MidiEventVec&, const EditPass&, uint32_t loopLength)`**;
      dispatch on `actionType` + `propertyType` (**NoteRange** vs **Length** semantics).
- [x] 2.2 Delete **`applyEditChangeList`**, **`EditChange`**, **`EditChangeType`**, SD legacy
      helpers listed in design D7.

## 3. Session undo

- [x] 3.1 **`SessionUndoEntry`**: replace **`EditChangeList`** with same fields as one **`EditPass`**
      row (+ existing focus/selection/**editPassIdsAtPush**); update **`buildSessionUndoEntry`** /
      **`applySessionUndoEntry`** / redo path.
- [x] 3.2 Update **`test_note_edit_session_undo`** (stale pass, add+move undo, visual cache) and
      **`test_edit_apply`** matrices; drop **`EditChangeList`** fixtures.

## 4. SD (v5 canonical wire, no migration)

- [x] 4.1 **`STORAGE_VERSION 5`** in `StorageManager.cpp`; **`loadState`** `version != 5` → false.
- [x] 4.2 Implement v5 row wire (fixed layout above); new tail marker; **`writePersistedEditPass`** /
      **`readPersistedEditPass`** only (rename from `*Scoped`).
- [x] 4.3 Delete **`readPersistedEditPassLegacyV4`**, **`readPersistedEditChange`**,
      **`writePersistedEditChange`**, **`derive*FromLegacyChanges`** from `StorageLoopIo.cpp`.
- [x] 4.4 **`test_storage_loop_io`**: v5 round-trip; feed v4 bytes → version check fails; truncated
      tail → **`readPersistedEditsTail`** false; remove **`test_dual_read_legacy_edit_tail_v4_rows`**.

## 5. Delete legacy (firmware RAM)

- [x] 5.1 Grep cleanup: zero remaining **`EditChange`** / **`EditChangeList`** / **`EditChangeType`**
      references (includes **`estimatedEditPassBytes`**, pool-budget tests).

## 6. Validation

- [x] 6.1 `pio test -e native` (all suites touching **EditPass**).
- [x] 6.2 Edit HITL baseline (`host_midi_automation_edit_baseline.py`); archive change.

## Deferred

- Tick **delta** SD compaction (m8-edit §6.1).
- **ControlChange** session UI.
- **Velocity** HITL until UI control ships (enum + apply land in this change).
