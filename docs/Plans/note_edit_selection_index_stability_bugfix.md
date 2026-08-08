# Note edit selection index stability — RC10 (shipped)

**Status:** Shipped — geometry mover hold in `syncSelectedNoteIdxToFilteredInventory`; removed redundant sync from `applySelectionFromGeometryEdit`.

**Prerequisite:** RC6 projection shipped. RC7/RC7b macro commit guards shipped.

**Primary captures:**

- `captures/session_20260806_223833.log` — `Note selection changed: 1→2→3` during overlap geometry; re-select shortens wrong row (~582s)
- `captures/session_20260806_222937.log` — same index churn during shorten/hide cycles (~30.5s)

**Debugging boundary:** `EditorSelection.primaryNote` + `focus.last` bracket are identity; `selectedNoteIdx` is a derived OLED/F1 cursor only.

---

## Symptom

During Move geometry over overlapping pitch-65 notes:

1. `Note selection changed: N→M` fires while **`primaryNote` / mover `NoteId` unchanged** (list reorder from hide/shorten).
2. Fader-1 re-select targets a **stale display index** or wrong row at an old bracket → wrong macro commit / shorten (RC7b-adjacent; `223833` ~582s).

## Root cause

1. **`applySelectionFromGeometryEdit`** calls `syncSelectedNoteIdxToFilteredInventory` on every bracket tick change — contradicts shipped D40 (`syncGeometrySelectionToUi` only).
2. **`syncSelectedNoteIdxToFilteredInventory`** remaps `selectedNoteIdx` whenever `resolveNoteEditHighlightIndex` returns a new index, even when the current index still points at `primaryNote`.
3. **`noteIdOnlyIdx` fallback** picks first matching `NoteId` in the filtered list (wrong when multiple same-pitch rows exist after overlap edits).

## Invariant (target)

During active geometry on the mover (`Move` / `Length` / `Pitch`, `focus.movingNoteId == selection.primaryNote`):

- Update `selectedNoteIdx` only when the current index is **invalid** (out of range or wrong `NoteId`).
- Do **not** remap index solely because overlap hide/shorten changed list sort order.
- Bracket tick on `EditorSelection` may still track `focus.last` (display phase).

Fader-1 select continues to resolve **`NoteId` + bracket tick** (`filteredDisplayNoteIndexForNoteIdAndStart`).

## Implementation

| Change | Owner |
|--------|--------|
| Remove `syncSelectedNoteIdxToFilteredInventory` from `applySelectionFromGeometryEdit` | `NoteEditSelection.cpp` |
| Geometry mover hold in `syncSelectedNoteIdxToFilteredInventory` | `NoteEditFocusRebuild.cpp` |
| Drop `noteIdOnlyIdx` fallback during geometry mover hold | `NoteEditFocusRebuild.cpp` |

## Tests

- Native: `test_selection_index_geometry_hold_when_primary_note_at_current_idx` — list reorder simulation; policy documented at highlight resolver level.
- HITL: overlap move → no `Note selection changed` spam when `primaryNote` unchanged; re-select short note does not shorten wrong row.

## Out of scope

- RC9 overlap restore span (full baseline restore on move-away)
- RC8 recon parity
- Replacing `selectedNoteIdx` with `NoteRef` everywhere (see `note_edit_stable_note_id_enhancement.md`)

## Related

- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md)
- [`note_edit_geometry_f1_selection_guard_bugfix.md`](note_edit_geometry_f1_selection_guard_bugfix.md) — D40 `syncGeometrySelectionToUi`
- [`note_edit_select_commit_bracket_bugfix.md`](note_edit_select_commit_bracket_bugfix.md) — RC7b (defense in depth)
