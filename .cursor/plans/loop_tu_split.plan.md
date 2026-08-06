---
name: Loop TU split
overview: Shrink Loop.cpp via phases 0, 2–10. Four primary architectural modules + Loop.cpp coordinator. Phase 10 is evidence-driven; fifth TU not currently expected.
todos:
  - id: phase-0-scaffold
    content: "Phase 0: LoopInternal.h scaffold"
    status: completed
  - id: phase-2-materialize
    content: "Phase 2 → LoopMaterialization.cpp: merge, materializeEditView, midiEvents, discardPassesMaterializedCache"
    status: completed
  - id: phase-3-edit-pass
    content: "Phase 3 → LoopEditPasses.cpp: save/replace/disable edit passes"
    status: completed
  - id: phase-4-gather
    content: "Phase 4 → LoopMaterialization.cpp: gatherCommittedEvents* (protected)"
    status: completed
  - id: phase-5-snapshot
    content: "Phase 5 → LoopEditPasses.cpp: share/adopt/restore snapshot"
    status: completed
  - id: phase-7-capture-live
    content: "Phase 7 → LoopCapture.cpp: beginCapture, appendCaptureEvent"
    status: completed
  - id: phase-8-capture-stop
    content: "Phase 8 → LoopCapture.cpp: sealCapture, commitCapturePass (protected)"
    status: pending
  - id: phase-9-visual
    content: "Phase 9 → LoopVisualCache.cpp: visual cache + displayEventCountHint"
    status: pending
  - id: phase-10-remainder
    content: "Phase 10: evidence-driven placement per ownership guide (10a/10b/10c sub-slices as needed)"
    status: pending
isProject: false
---

# Loop translation-unit extraction

**Authoritative plan:** [docs/plans/loop_translation_unit_extraction_refinement.md](../../docs/plans/loop_translation_unit_extraction_refinement.md)

## Primary modules + coordinator

```text
LoopCapture.cpp          Phases 7, 8; Phase 10 (capture ownership)
LoopMaterialization.cpp  Phases 2, 4
LoopEditPasses.cpp       Phases 3, 5; Phase 10 (edit-pass ownership)
LoopVisualCache.cpp      Phase 9
Loop.cpp                 coordinator (~150–250): cross-domain routers, slot-wide state
```

**Principle:** extend existing modules before introducing new TUs. Fifth TU not currently expected.

## PR stack

`0 → 2 → 3 → 4 → 5 → 7 → 8 → 9 → 10` (10 may split into 10a/10b/10c)

**Branch:** `refactor/loop` from `dev`
