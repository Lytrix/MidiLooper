# Note edit leave-restore current-state bugfix (RC10)

**Status:** RC10a **shipped** (native + HITL leave) | RC10b **shipped** (native) | RC10c deferred

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
- [x] HITL 113010 leave shows `type=0` (validated in session_20260807_140022)

---

## RC10b — Deselect display authority (shipped native)

While NOTE_EDIT active and overlaps hidden in current state, deselect must not show committed-pass ghost restore.

**Root cause:** `projectNoteEditDisplayNotes` returned raw `committedBaseNotes` when `!focus.active`; empty-step deselect clears focus via `rebuildNoteEditFocusFromStore(-1)` while current state still has `Hidden` overlap rows (`session_20260807_140022`: `DISP 26,29,26,26` → `29,29,29,29`).

**Fix:** When current state has overlap display mask (Hidden/Deleted overlap closure rows), run session-authoritative projection even if focus is inactive.

### RC10b acceptance

- [x] Native: inactive focus + hidden overlaps → committed ghosts omitted
- [ ] HITL deselect keeps frame note count (26 not 29)

---

## RC10c — Authority trim (deferred)

Derive overlap participants from current state vs session baseline; retire geometry-path `overlapNotes` scratch.
