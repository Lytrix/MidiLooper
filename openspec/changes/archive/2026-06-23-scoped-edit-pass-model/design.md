## Context

`timeline-pass-model` shipped **LoopPasses** with `recordPass`, `overdubPasses[]`, and `editPasses[]`. Current note edit storage writes **EditPass** rows with **EditPassKind::NoteEdit** and an ordered **EditChange** list. **EditPassKind::ControlChange** and **ControlChangeEditPassClosed** exist as reserved stubs, but no CC edit UI or CC edit storage is implemented.

The next edit family needs a generic row shape before control-change or audio edits are added. A CC edit targets a CC event (`channel`, `controller`, `tick`, `value`) and does not use **NoteRef**, note overlap handling, or note length semantics. Extending **EditChange** with CC fields would mix note-specific and CC-specific payload in one legacy structure.

## Goals / Non-Goals

**Goals:**

- Keep **EditPass** as the stored edit row in **LoopPasses.editPasses[]**.
- Replace future-facing **EditPassKind** semantics with **EditSessionType** on **EditPass**.
- Use nested category enums:
  - **EditSessionType** — `Note`, `ControlChange`, future `Audio`
  - **EditActionType** — `Create`, `Update`, `Delete`
  - **EditPropertyType** — `None`, `Pitch`, `Length`, `StartTick`, `EndTick`, `Tick`, `Value`
- Keep **Property** distinct from controller config `parameter`.
- Keep undo/redo as pass-state logic, not stored edit actions.
- Keep record/overdub capture as one time-ordered MIDI stream.

**Non-Goals:**

- No CC edit UI.
- No audio edit implementation.
- No split capture storage for notes vs CC.
- No Teensy upload.
- No broad rename of current **EditChange** unless the implementation task explicitly migrates note rows to the scoped **EditPass** payload.

## Decisions

### D1 — Keep `EditPass` as the nested row

**Choice:** **EditPass** remains the stored edit row. It gains scoped fields instead of introducing a standalone broad **Edit** container.

```cpp
enum class EditSessionType : uint8_t { Note, ControlChange };
enum class EditActionType : uint8_t { Create, Update, Delete };
enum class EditPropertyType : uint8_t {
  None,
  Pitch,
  Length,
  StartTick,
  EndTick,
  Tick,
  Value,
};

struct EditPass {
  PassId id;
  EditSessionType sessionType;
  uint8_t editPassIndex;
  EditPassState state;
  EditActionType actionType;
  EditPropertyType propertyType;
  // Target and payload are scoped by sessionType/propertyType.
};
```

**Rationale:** The codebase already uses **editPass** as the row concept and **noteEditPass** as a batch boundary. A separate **Edit** row would make **EditPass** a container for another broad word. Keeping the nested fields on **EditPass** preserves existing ownership and makes the row self-describing.

**Alternative rejected:** separate **NoteEditPass** and **ControlChangePass** arrays. This splits the overlay layer before implementation proves materialize or SD needs separate arrays. Capture already records a unified MIDI stream, and edit rows still share pass state, pass id, storage order, SD tail placement, and undo toggling.

### D2 — Use `EditSessionType` instead of `EditPassKind`

**Choice:** Future scoped rows use **EditSessionType** to identify the edit domain.

**Rationale:** The field answers “what session family owns this edit row?” (`Note`, `ControlChange`, future `Audio`). That is clearer than **EditPassKind**, which came from the earlier “four pass kinds” framing.

**Migration:** Current **EditPassKind** remains until this change is implemented. Existing persisted rows with **EditPassKind::NoteEdit** migrate to `EditSessionType::Note`. **EditPassKind::ControlChange** is reserved only; no shipped CC edit rows are produced today.

### D3 — Use CRUD-style action names

**Choice:** Stored actions use **EditActionType** with `Create`, `Update`, `Delete`.

**Rationale:** A broad `Change` action does not say what happened. `Update` plus **EditPropertyType** gives the action and the edited field. Move becomes `Update` of tick/range properties; it is not a separate top-level action in stored edit vocabulary.

Examples:

| User edit | sessionType | actionType | propertyType |
|-----------|-------------|------------|--------------|
| Add note | `Note` | `Create` | `None` |
| Delete note | `Note` | `Delete` | `None` |
| Move note | `Note` | `Update` | `NoteRange` (`startTick` + `endTick`) |
| Change length | `Note` | `Update` | `Length` (`startTick` + `endTick`) |
| Change pitch | `Note` | `Update` | `Pitch` |
| Add CC | `ControlChange` | `Create` | `None` |
| Move CC | `ControlChange` | `Update` | `Tick` |
| Change CC value | `ControlChange` | `Update` | `Value` |

### D4 — Use Property, not Parameter

**Choice:** Use **EditPropertyType** for the edited stored field.

**Rationale:** `parameter` already means controller/action argument in button and fader config (`withParameter(...)`). `Property` describes a stored editable field on the target. A property can be hidden from UI and still be editable in storage.

### D5 — Undo/redo stays outside edit action

**Choice:** Undo/redo toggles **EditPassState** and remains represented by **UndoEntryKind** entries such as **NoteEditPassClosed** and **ControlChangeEditPassClosed**.

**Rationale:** Undoing a delete-note pass is not a new `Create` edit. Current code disables edit pass rows on undo and re-enables the same rows on redo. The scoped model keeps that invariant.

### D6 — Container suffix convention

**Choice:** New vector aliases use `*Vec` when they are concrete `std::vector` aliases. Ordered behavior is named explicitly in comments and requirements.

**Rationale:** The repo already uses **MidiEventVec**, **EditPassVec**, **OverdubPassVec**, and **UndoEntryVec**. `List` exists in legacy aliases such as **EditChangeList**, but it is not a semantic guarantee of unordered data and is still backed by `std::vector`.

## Risks / Trade-offs

- **SD migration churn** -> Handle in one migration step from current v4 **EditPassKind** tail to scoped edit rows; keep dual-read until persisted loops are covered.
- **Generic fields can become too abstract** -> Keep target/payload scoped by **EditSessionType** and require tests for note and CC rows.
- **Hot path allocation risk** -> Do not add allocation to playback or record/overdub stop paths; materialize remains the edit replay boundary.
- **Naming drift between UI and storage** -> Keep **NoteEditKind** / **NoteEditSessionState** for live UI state; use **EditActionType** / **EditPropertyType** only for stored edit rows.

## Migration Plan

1. Add enums and scoped **EditPass** fields behind compile-safe migration helpers.
2. Map current **EditPassKind::NoteEdit** + **EditChange** rows to `EditSessionType::Note` rows.
3. Replace `applyEditChangeList` materialize dispatch with `applyNoteEditPass` for scoped rows.
4. Generalize undo metadata from `noteEditPassIds` toward scoped `editPassIds` plus session type.
5. Update SD read/write with dual-read for existing v4 edit tails.
6. Update docs/rules and native tests.

## Open Questions

- ~~Whether `Move note` stores `StartTick` + `EndTick` as two property updates or one note range.~~ **Resolved:** **NoteRange** — **`scoped-edit-pass-payload`**.
- ~~Length storage shape.~~ **Resolved:** **Length** with `startTick` + `endTick` (start-point length later) — **`scoped-edit-pass-payload`**.
- ~~Exact SD version bump strategy for scoped edit rows.~~ **Superseded:** **`scoped-edit-pass-payload`**
  bumps to **v5**, rejects v1–v4, no dual-read or edit-tail migration.
- **ControlChange** edit — after **long-loop-piano-roll-window** (not this change).

## Follow-up changes

- **`scoped-edit-pass-payload`** — retire **EditChange**; **NoteRef** + **EditPropertyType** + MIDI fields (no payload blob).
- **`edit-session-state`** — **`EditSession`** + **`EditSessionType`** (`Loop`|`Note`|`ControlChange`);
  pass-row **`EditSessionType`** → **`EditPassType`**.
