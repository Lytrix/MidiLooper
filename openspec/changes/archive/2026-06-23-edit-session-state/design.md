## Context

Four owners today for one live edit overlay (see prior design). Naming drift: **`EditSessionType`**
on **EditPass** describes what domain was **stored in the pass**, not the RAM session.

## Goals / Non-Goals

**Goals:**

- **`EditSession`** with **`EditSessionType sessionType`**: `Loop`, `Note`, `ControlChange`.
- **`EditPass`** with **`EditPassType passType`**: `Note`, `ControlChange`, `Audio` (rename from
  **`EditSessionType`** / **`sessionType`**).
- Commit mapping: `Note` → `Note`, `ControlChange` → `ControlChange`; **`Loop`** never writes an
  **editPass** row.
- **`cycleEditSession`**, **`getEditSessionType()`**.

**Non-goals:**

- **ControlChange** session UI (enum value reserved).
- **Audio** on **EditSessionType** until audio edit session is specified.
- Full **LoopEditManager** FSM merge (M1: scope query only).

## Decisions

### D1 — Two enums, shared vocabulary

```cpp
enum class EditSessionType : uint8_t { Loop, Note, ControlChange };

enum class EditPassType : uint8_t { Note, ControlChange, Audio };

struct EditSession {
  EditSessionType sessionType = EditSessionType::Loop;
  CowLoopEventStore store;
  EditSessionUndoStack undoStack;
  uint8_t editPassIndex = 0;
  EditPassIdList editPassIds;
  bool replaceEditPassOnClose = false;
  NoteEditFocus noteFocus;
  NoteEditSessionState noteState;

  bool isNote() const { return sessionType == EditSessionType::Note; }
};

struct EditPass {
  EditPassId id;
  EditPassType passType = EditPassType::Note;
  EditActionType actionType;
  EditPropertyType propertyType;
  // ...
};
```

**Rationale:** **EditSessionType** = what live edit session is active. **EditPassType** = what
domain a committed pass row edits. Values align where both exist (`Note`, `ControlChange`).

### D2 — Layer rules

| Value | **EditSessionType** (live) | **EditPassType** (stored) |
|-------|---------------------------|----------------------------|
| **Loop** | loop chrome / boundary | — (no **editPass** row) |
| **Note** | note RAM session + store | note edit rows |
| **ControlChange** | CC RAM session (future) | CC edit rows |
| **Audio** | — (until audio session) | audio edit rows (future) |

### D3 — Pass-row rename (from scoped-edit-pass-model)

| Legacy | Target |
|--------|--------|
| `enum class EditSessionType` on **EditPass** | `enum class EditPassType` |
| `EditPass::sessionType` | `EditPass::passType` |
| SD tail `sessionType` field | `passType` (same v4 wire if numeric values unchanged) |

### D4 — Session container rename

| Legacy | Target |
|--------|--------|
| `NoteEditSession` | `EditSession` |
| `NoteEditSession::active` / **MainEditMode** | `sessionType` |
| `cycleMainEditMode` | `cycleEditSession` |
| `getCurrentMainEditMode()` | `getEditSessionType()` |

### D5 — Commit helper

```cpp
EditPassType passTypeForSession(EditSessionType session);
// Note → Note, ControlChange → ControlChange, Loop → invalid for saveNoteEditPass
```

### D6 — Toggle vs full exit

- **`cycleEditSession`**: `sessionType` toggle `Loop` ↔ `Note` only; no **`closeNoteEditPass`** from
  toggle alone.
- **`exitEditMode`**: commit, close pass batch, `sessionType → Loop`.

## Risks

- **`EditSessionType`** rename on **EditPass** touches SD I/O and all scoped-edit-pass-model
  call sites — single mechanical PR with native + storage tests.
- Docs/rules must show both enums side-by-side to prevent swap bugs.
