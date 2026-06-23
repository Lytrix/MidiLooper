# Edit session type — tasks

**Apply before:** `scoped-edit-pass-payload`.  
**Apply after:** archive `scoped-edit-pass-model` (task 8.2).

**Do not re-read:** full `design.md` — use D1–D6 tables below.

## Implementation map

| Area | Files | Rename / add |
|------|-------|----------------|
| Pass enum | `include/EditPass.h`, `src/LoopPasses.cpp`, `src/StorageLoopIo.cpp` | `EditSessionType`→`EditPassType`, `sessionType`→`passType` |
| Session owner | new `include/EditSession.h`, retire `include/NoteEditSession.h` | `EditSession`, live `EditSessionType` (`Loop`,`Note`,`ControlChange`) |
| Edit owner | `include/EditManager.h`, `src/EditManager.cpp` | `noteEditSession`→`editSession`; `passTypeForSession()` |
| Mode toggle | `src/EditManager.cpp`, `src/NoteEditManager.cpp`, `include/LoopEditManager.h` | delete `MainEditMode`/`cycleMainEditMode`; add `cycleEditSession` |
| Undo | `src/TrackUndo.cpp`, `include/GlobalUndoStack.h` | `NoteEditPassClosed` paths unchanged |
| Tests | `test/test_edit_apply`, `test/test_note_edit_session_undo`, `test/test_storage_loop_io` | enum renames only — **no v5 SD in this change** |
| HITL | `scripts/host_midi_automation_edit_baseline.py` | serial marker strings if any reference old names |

**SD note:** wire field is still `passType` byte (same numeric values as today's `sessionType`).
**STORAGE_VERSION** bump to **5** is **`scoped-edit-pass-payload`** only.

## 1. Enum rename on **EditPass**

- [x] 1.1 Rename **`EditSessionType`** → **`EditPassType`**; **`sessionType`** → **`passType`** on
      **EditPass** (files in map).
- [x] 1.2 Update SD serializers field name only; numeric values unchanged (Note=0, CC=1, Audio=2).

## 2. Live **`EditSession`** owner

- [x] 2.1 Add **`EditSession.h`**: `EditSessionType sessionType`, `store`, undo stack,
      `editPassIndex`, `editPassIds`, `replaceEditPassOnClose`, `noteFocus`, `noteState`.
- [x] 2.2 **`EditManager`**: replace **`NoteEditSession noteEditSession`** with **`EditSession editSession`**;
      update all `noteEditSession.` call sites (~`EditManager.cpp`, `NoteEditManager.cpp`, `TrackUndo.cpp`).
- [x] 2.3 **`saveNoteEditPass`**: set `passType = passTypeForSession(editSession.sessionType)`.

## 3. Remove duplicate scope owners

- [x] 3.1 Delete **`MainEditMode`**, **`MAIN_MODE_*`**, **`cycleMainEditMode`**,
      **`getCurrentMainEditMode`** (`EditManager`, `NoteEditManager`, GPIO/MIDI button paths).
- [x] 3.2 Add **`cycleEditSession()`** (toggle `Loop`↔`Note` only; **no** `closeNoteEditPass`) and
      **`getEditSessionType()`**.
- [x] 3.3 **`LoopEditManager`**: read **`editManager.getEditSessionType()`** instead of main mode.

## 4. Wire callsites and docs

- [x] 4.1 Firmware + HITL script renames.
- [x] 4.2 **Naming-Vocabulary-Teensy-Looper.mdc**: **EditSessionType** vs **EditPassType** table.
- [x] 4.3 **LOOP_MIDI_STORAGE_AND_VALIDATION.md**, README glossary.

## 5. Validation

- [x] 5.1 `pio test -e native`.
- [x] 5.2 Edit HITL baseline.
- [x] 5.3 `openspec validate edit-session-state` then archive.

## Deferred

- **`EditSessionType::ControlChange`** UI — after **long-loop-piano-roll-window**.
- **`EditPassType::Audio`** / **`EditSessionType::Audio`** — when audio edit is specified.
