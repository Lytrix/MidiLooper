## Why

M1–M7 shipped **Take**-equivalent storage as `Epoch`. **m8-rename** aligns vocabulary
(**Take**, **Capture**, **TakeCommitted**) without behavior change. M8 then replaces note-edit
collapse (`editFlat_`) with **Edit** + **NoteEditSession**.

## Prerequisite

**`m8-rename`** MUST complete first (`/opsx:apply` → validate → merge). Do not start this change
while product code still uses **Epoch** / **CaptureLayer**.

## What Changes

- **NoteEditSession** with **`store`** + **NoteEditSessionUndoStack**; future **LoopEditSession**, **ControlChangeEditSession**; playback/jam session name **TBD** (**JamSession** / **PerformanceSession**)
- **`saveEdit()`** → **Edit** + **EditChange** list + **EditId**; not stored as Takes
- **Span boundaries:** **`closeNoteEditSpan()`**; global **`NoteEditSessionCommitted`** (all Edits in span)
- **`applyEdits(takes, edits)`** for playback/display
- SD v4 **`edits[]`**; autosave + post-MIDI exit flush
- Retire **`editFlat_`** / `syncEditFlatToEpochs`
- **`EditState`** enum on struct **Edit** (Active | Disabled) — name freed by **m8-rename**

## Capabilities

### Modified Capabilities

- `timeline-epochs` (legacy folder name until archive → **`timeline-takes`**)

## Impact

- **Code:** **Edit**/**EditChange**; **NoteEditSession**; bridge removal; **DebugSessionCapture** (if not done in rename)
- **Tests:** `test_edit_session`, take-named survival tests
- **Depends on:** **`m8-rename`** merged
