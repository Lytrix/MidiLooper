# Note Edit Select Relatch After Geometry Driver Hold

**Status:** Implemented (2026-08-05)

**Follow-on (deselect / leave-mover):** After geometry hold, relatch motor-to-note only when divergent target is a note slot; empty-step divergent uses suspend-only (450 ms settle, no F1 motor). Successful select apply ends geometry-hold on F1 (`currentDriverFader = FADER_SELECT`) and clears relatch cycle without re-arm until next geometry-fader edit.

**Gate consolidation (182453):** F1 `selectionChanged` preempts geometry hold and relatch; hold window uses coarse idle (`COARSE_STABILITY_TIME`); relatch arm does not arm settle; coarse blocked when F1 diverges from logical selection.

**Cursor plan:** [`.cursor/plans/select_relatch_after_geometry_4412f6a9.plan.md`](../../.cursor/plans/select_relatch_after_geometry_4412f6a9.plan.md)  
**Evidence:** [`captures/session_20260805_174222.log`](../../captures/session_20260805_174222.log)

## Architectural Decision

This capture demonstrates a selection-transition issue following geometry-driver hold expiration. It does not invalidate the canonical commit architecture or imply that previous NOTE_EDIT issues share the same root cause.

**Architectural principle:** Maintain a single canonical source of truth, with all other systems acting as independent consumers of that state.

**Interaction model: Option A — Immediate Relatch.** During Selection Relatch, divergent select input is suspended until the physical controller and logical `EditorSelection` are synchronized.

## Ownership

| Responsibility | Owner |
|----------------|-------|
| Canonical edit state | Apply |
| Persistent serialization | Commit |
| Display generation | Projection |
| Logical note selection | EditorSelection |
| Physical controller synchronization | ControlSurface Synchronization |

```text
                   Apply
                     │
                     ▼
        Canonical NOTE_EDIT State
          │          │          │
          ▼          ▼          ▼
    Projection    Commit   EditorSelection
                                 │
                                 ▼
                 ControlSurface Synchronization
```

Projection visualizes canonical state. EditorSelection independently consumes canonical state. ControlSurface Synchronization consumes the logical selection. Projection does not participate in selection ownership.

Ownership direction: `EditorSelection` → ControlSurface Synchronization. Relatch synchronizes the controller to logical selection; it does not redefine logical selection.

**Ownership invariant:** No NOTE_EDIT component (Apply, Projection, Commit, Undo) observes or depends upon the relatch state. Relatch must never create, modify, or cancel NOTE_EDIT operations.

**Synchronization invariant:** Controller synchronization may delay user input, but must never reinterpret user intent — no rewriting logical selection, no reinterpreting controller movement, no modifying NOTE_EDIT behavior.

## State Machine

```text
Geometry Driver Active
        │
        ▼
Selection Relatch
        │
        ▼
Selection Synchronized
```

## Capture Conclusion

The capture shows canonical commit output matching the intended edit state. The observed behavior occurs during the subsequent selection transition.

## Reference Implementation

The architecture defines the behavior. The current implementation is one realization of that architecture (full detail in the Cursor plan).

## Out of Scope

Canonical commit, projection, overlap analyze math, and overlap diagnostics cleanup (separate follow-on).

## Gate consolidation follow-up (session_20260805_183125)

Coarse block exempts active geometry drivers when F1 motor lags logical bracket (`shouldBlockCoarseForPendingSelectNavigation`). Pitch-lane overlap fills missing `baselineMap` entries from the live store before analyze (`ensureBaselineMapEntriesForEvaluationScope`).
