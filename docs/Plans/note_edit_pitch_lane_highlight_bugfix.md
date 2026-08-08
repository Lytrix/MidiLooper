# Note Edit Pitch-Lane Highlight Bugfix

Handoff and refined plan for session_20260805_163142. Authoritative Cursor plan: [`.cursor/plans/pitch_overlap_highlight_fix_ce6e5f6c.plan.md`](../../.cursor/plans/pitch_overlap_highlight_fix_ce6e5f6c.plan.md).

## Phase 0 findings (session start)

| Check | Result |
|-------|--------|
| `primaryNote` survives commit? | Yes — commit path does not clear `sessionState.selection.primaryNote`; fader deselect is a separate step |
| Projection includes overlap 34 post-commit? | Likely yes — overlap shorten/hide rows were not in the single apply-owned mover row at 57.397; participant overlay may still show shortened span |
| Root cause for DNTE len 47 at idx 25 | Stale `selectedNoteIdx` + tick-based resolver without focus identity — not proven projection bug |

## Implementation status

- **Phase 1:** `commitAllPendingNoteEditActions` resyncs `selectedTick` + `syncSelectedNoteIdxToFilteredInventory` from `focus.last` after commit
- **Phase 2:** `NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex` shared by `syncSelectedNoteIdxToFilteredInventory`, `resolveDrawHighlightIndex`, `drawNoteInfo`
- **Phase 3:** Native tests added; full `pio test -e native` passed


## Invariants

- **Selection:** `primaryNote` is canonical; display index is derived.
- **Commit:** `primaryNote` unchanged when that `NoteId` still exists after commit.

## Evidence

[`captures/session_20260805_163142.log`](../../captures/session_20260805_163142.log) — DNTE flips from `...,95,25` to `...,47,25` at commit (~line 5967).
