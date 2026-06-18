## Why

M1–M7 shipped loop storage as **Epoch** / **CaptureLayer**. M8 behavior (Edit op-lists,
**NoteEditSession**, span undo) needs stable domain vocabulary first. A rename-only pass
reduces review churn and frees names (`EditState` on persisted **Edit**) before semantic changes.

## What Changes

- **Epoch → Take** (`TakeId`, `TakeType`, `TakeState`, `takes[]`, `commitTake()`)
- **CaptureLayer → Capture**
- **EpochPublished → TakeCommitted** in `GlobalUndoStack` + SD v4 field names where applicable
- UI FSM base **`class EditState` → `EditNoteState`** (resolve collision with existing concrete
  `EditNoteState` subclass during implementation)
- **`SessionCapture` → `DebugSessionCapture`**
- Remove **`EpochKind::Edit`** if present
- Test suite / file renames (`test_epoch_*` → `test_take_*` where applicable)

## What Does NOT Change

- **`editFlat_`**, `syncEditFlatToEpochs`, note-edit collapse behavior
- New **`edits[]`**, **`saveEdit()`**, **NoteEditSession** storage model
- SD v4 layout extensions for edits
- HITL timing / span / overdub-during-edit behavior

## Capabilities

### Modified Capabilities

- `timeline-epochs` (terminology delta only; folder rename on archive with **m8-edit**)

## Impact

- **Code:** mechanical rename across `Loop`, `Track`, storage, undo, tests — no logic change
- **Tests:** `pio test -e native` green; optional HITL smoke
- **Next:** **`m8-edit`** after rename merges

## Dependency

**Blocks:** `m8-edit` — do not `/opsx:apply` edit behavior until rename exit criteria pass.
