# Note edit leave-restore current-state bugfix (RC10)

**Status:** RC10a **shipped** (native) | RC10b open | RC10c deferred

**Index:** [`note_edit_overlap_restore_span_bugfix.md`](note_edit_overlap_restore_span_bugfix.md) (RC9g/h/i) · [`note_edit_overlap_resolution_map_refinement.md`](note_edit_overlap_resolution_map_refinement.md) (case map)

---

## Debugging boundary (frozen)

```text
NoteMovementUtils / coarse fader
  → NoteGeometryResolver::resolve
  → analyze → resolveAllConstrainedGeometry (leave-restore targets)
  → buildEditSessionActions → applyEditSessionActions
  → NoteEditCurrentState → projectToSessionStore
```

Projection paint, macro commit, deselect display = **RC10b**. Authority consolidation = **RC10c**.

---

## RC10a — Leave-restore via presence (session_20260807_113010)

**Invariant:** For a leave transition, only note IDs in `changedOverlapNoteIds` on the mover's same-pitch lane are eligible. Within that scope, `Hidden`/`Deleted` presence **or** span ≠ session baseline → restore candidate.

**Restore span:** `focus.baselineMap` / `storageTransactionBaseline` — not `row.committedSpan` unless proven equal.

**Log anchor:** ~48.992s — `changed=3 interactions=0` → expect `RestoreNote` during edit.

**Code:** `determineConstrainedGeometryTargetNoteIds` — `isRowHiddenOrDeleted` inside existing sticky + lane gates.

### Pre-implementation review

| Topic | Decision |
|-------|----------|
| Ownership change | NO — extend `ResolveConstrainedGeometry` |
| Transition change | NO — fix detection gap |
| Sticky scope | Keep `isChangedOverlapParticipant` gate |
| Restore authority | Session baseline (`baselineMap`) |

### RC10a acceptance

- [x] Hidden + `currentSpan == baselineMap` in sticky scope → target
- [x] `resolveAllConstrainedGeometry` uses baselineMap ticks for leave-restore
- [x] Native tests pass (886/886)
- [x] Firmware build (`teensy41-capture-serial`)
- [ ] HITL 113010 leave shows `type=0`

---

## RC10b — Deselect display authority (open)

While NOTE_EDIT active and overlaps hidden in current state, deselect must not show committed-pass ghost restore.

Prefer option A: session-authoritative display when overlap state pending.

---

## RC10c — Authority trim (deferred)

Derive overlap participants from current state vs session baseline; retire geometry-path `overlapNotes` scratch.
