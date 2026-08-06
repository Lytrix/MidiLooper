---
name: EditManager TU split
overview: Shrink `src/EditManager.cpp` (~2602 LOC) into a thin session coordinator by moving cohesive NOTE_EDIT domains into `src/EditManager/*.cpp`, one phase per PR, behavior-preserving, with state remaining on `EditManager`.
todos:
  - id: phase-0-scaffold
    content: "Phase 0: EditManagerInternal.h + NoteEditCommitColdHelpers.cpp (anon namespace pre-commit helpers)"
    status: completed
  - id: phase-1-display
    content: "Phase 1: Extract NoteEditDisplayProjection.cpp (display cache, deferred refresh, live display note)"
    status: completed
  - id: phase-2-focus
    content: "Phase 2: Extract NoteEditFocusRebuild.cpp (focus rebuild, index sync, cancel pending delete)"
    status: completed
  - id: phase-3-commit
    content: "Phase 3: Extract NoteEditSessionCommit.cpp (commitAllPending, commitEditAction, bake) — protected"
    status: pending
  - id: phase-4-lifecycle
    content: "Phase 4: Extract NoteEditSessionLifecycle.cpp (open/close/fold/persist) — protected"
    status: pending
  - id: phase-5-undo
    content: "Phase 5: Extract NoteEditSessionUndo.cpp (session undo/redo, kind-boundary warm)"
    status: pending
  - id: phase-6-selection
    content: "Phase 6: Extract NoteEditSelection.cpp (applySelectNav, nav slots, session state sync)"
    status: pending
  - id: phase-7-geometry
    content: "Phase 7: NoteGeometryResolver (relocate TU, merge driver header) + extract NoteEditGeometryOps.cpp"
    status: pending
  - id: phase-8-fsm
    content: "Phase 8: Extract EditNoteStateCoordinator.cpp (encoder FSM routing, bracket nav)"
    status: pending
  - id: phase-9-depart
    content: "Phase 9: Extract EditSessionDepart.cpp (track/slot depart, length mode, session cycle)"
    status: pending
isProject: false
---

# EditManager translation-unit extraction

**Authoritative plan:** [docs/plans/editmanager_translation_unit_extraction_refinement.md](../../docs/plans/editmanager_translation_unit_extraction_refinement.md)

**Branch:** `refactor/editmanager` (from `dev`)

**Baseline:** `EditManager.cpp` **~2602 LOC** → target **~250–400 LOC** after Phases 0–9.

**Shipped siblings (Phase 7):** `RunEditSessionGeometryPipeline.cpp` → `src/EditManager/NoteGeometryResolver.cpp` + `include/NoteGeometryResolver.h`. **Do not re-move bodies from:** `EditApply.cpp`, `EditSessionActionBuilder.cpp`, `EditSessionInteraction.cpp`, `EditSessionLiveStoreSpan.cpp`, `EditSessionStoreInvariant.cpp`, `EditStates/*.cpp`.

**PR stack:** 0 → 1 → 2 → 3 → 4 → 5; 6 after 2; 7 after 3+6; 8 parallel after 6; 9 last.
