---
name: NoteEditFocus TU split
overview: Shrink root NoteEditFocus.cpp (~1554 LOC) into domain modules under src/EditManager/, colocated with NoteEditFocusRebuild.cpp. Behavior-preserving; naming strengthen in-scope.
todos:
  - id: phase-0-scaffold
    content: "Phase 0: NoteEditFocusInternal.h + optional NoteEditFocusTestDeps.cpp"
    status: pending
  - id: phase-2-linear-span
    content: "Phase 2 → NoteEditFocusLinearSpan.cpp: pair/span resolution"
    status: pending
  - id: phase-3-baseline
    content: "Phase 3 → NoteEditFocusBaseline.cpp: baselineMap + closure"
    status: pending
  - id: phase-4-overlap
    content: "Phase 4 → NoteEditFocusOverlap.cpp: overlap scratch + pre-commit materialize"
    status: pending
  - id: phase-5-state
    content: "Phase 5 → NoteEditFocusState.cpp: apply + rebuild from store"
    status: pending
  - id: phase-6-precommit
    content: "Phase 6 → NoteEditFocusPreCommit.cpp: buildPreCommitEditPasses (high)"
    status: pending
  - id: phase-7-display
    content: "Phase 7 → NoteEditFocusDisplayProjection.cpp: projectNoteEditDisplayNotes"
    status: pending
  - id: phase-10-remove-root
    content: "Phase 10: delete root NoteEditFocus.cpp; update test includes"
    status: pending
isProject: false
---

# NoteEditFocus translation-unit extraction

**Authoritative plan:** [docs/plans/noteditfocus_translation_unit_extraction_refinement.md](../../docs/plans/noteditfocus_translation_unit_extraction_refinement.md)

**Workflow:** [Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc)

## Modules (under `src/EditManager/`)

```text
NoteEditFocusLinearSpan.cpp       MIDI pair / span resolution
NoteEditFocusBaseline.cpp         baselineMap + closure
NoteEditFocusOverlap.cpp            overlap scratch + pre-commit materialize
NoteEditFocusState.cpp              apply + pending + rebuild from store
NoteEditFocusPreCommit.cpp          editPass row emission (high risk)
NoteEditFocusDisplayProjection.cpp  DisplayNote projection
NoteEditFocusRebuild.cpp            (existing) EditManager orchestration
```

## PR stack

`0 → 2 → 3 → 4 → 5 → 6 → 7 → 10`

**Branch:** `refactor/noteditfocus` from `dev`
