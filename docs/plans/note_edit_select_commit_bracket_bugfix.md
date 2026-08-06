# Note edit select commit bracket mismatch — RC7b (shipped)

**Prerequisite:** RC7 Phase 1 (`start=0` / invalid `NoteRange`) shipped. RC6 projection shipped. Geometry through `EditSessionAction` is trusted.

**Primary capture:** `captures/session_20260806_220331.log` (~55.7s)

## Invariant

When fader-1 re-selects the **same** `NoteId` as `focus.movingNoteId`, macro commit runs only if the select bracket tick matches the live mover display bracket from `focus.last`. Otherwise skip commit and rebuild focus from the selected `DisplayNote`.

## Root cause

`SelectFaderInput` macro-commits **before** applying the new select. `isLiveEditDriverValid` passes when `focus.last` matches the store (e.g. 993–1187), but the user selects bracket **609** (stale inventory row). Pre-commit emits `mover_focus` at 993 while the user intended 609 → `M65@609 missing in recon`, selection collapse.

## Implementation

| Symbol | Owner |
|--------|--------|
| `isMacroCommitAlignedWithSelectTarget` | `NoteEditFocusState.cpp` |
| `isMacroCommitAlignedWithSelectTargetForTrack` | `NoteEditFocusRebuild.cpp` |
| Select apply gate | `SelectFaderInput.cpp` |

**Telemetry:** `NOTE_EDIT macro commit skipped: select bracket mismatch` (`SESSION_CAPTURE`).

**Native test:** `test_macro_commit_aligned_with_select_target_rejects_bracket_mismatch_220331`

## HITL validation

| Capture | Result |
|---------|--------|
| `220331` (~55.7s) | **Pre-fix failure** — `mover_focus` 993–1187 after select at 609; `M65@609 missing in recon` |
| `222937` | **Failure absent** — no wrong-bracket pre-commit; no `M65@609 missing` |
| `222937` RC7b gate | **Not exercised** — post-move select at 609 blocked by `geometry_driver_ignored`, not `bracket mismatch` telemetry |

**Open HITL:** move note 609→993, wait geometry hold (>750 ms), fader-1 re-select same `NoteId` at 609 → expect skip log + no `mover_focus` at 993.

## Out of scope (separate commits)

- Selection index stability during geometry (`Note selection changed: 1→2` same mover)
- RC8 reconstruction parity

## Related

- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md) — RC7/RC8 index
- [`.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc`](../.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc)
