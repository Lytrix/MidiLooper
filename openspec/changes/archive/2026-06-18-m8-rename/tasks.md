## 1. Take + Capture rename

- [x] 1.1 `Epoch` → `Take` (`TakeId`, `TakeType`, `TakeState`, `takes[]`, `commitTake()`)
- [x] 1.2 `CaptureLayer` → `Capture` on `Loop`
- [x] 1.3 `EpochPublished` → `TakeCommitted` in `GlobalUndoStack` + SD v4 identifiers
- [x] 1.4 Remove `EpochKind::Edit` if present; grep gate: no `Epoch` / `CaptureLayer` in product code

## 2. UI + debug rename

- [x] 2.1 Resolve **`class EditState`** FSM base vs concrete **`EditNoteState`** collision; free **`EditState`** name for **m8-edit**
- [x] 2.2 `SessionCapture` → **`DebugSessionCapture`**

## 3. Tests + verification

- [x] 3.1 Rename test dirs/files (`test_epoch_*`, `test_loop_epoch_*`) to **take** naming where applicable
- [x] 3.2 Update native test strings/assertions to **Take** / **Capture** vocabulary
- [x] 3.3 `pio test -e native` — all green
- [x] 3.4 `pio run -e teensy41` — build succeeds
- [x] 3.5 `openspec validate m8-rename`

## 4. Docs (minimal)

- [x] 4.1 Touch comments / log prefixes that still say epoch in hot paths (no full storage guide rewrite — **m8-edit** §5)

## Out of scope (m8-edit)

- `edits[]`, **Edit**, **saveEdit()**, **NoteEditSession**, span undo
- Remove `editFlat_` / `syncEditFlatToEpochs`
- Archive **`timeline-epochs` → `timeline-takes`**
