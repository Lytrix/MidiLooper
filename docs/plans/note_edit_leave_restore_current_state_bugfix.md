# Note edit leave-restore current-state bugfix (RC10)

**Status:** RC10a/b **shipped** | RC10e **partial** (native only — HITL failed `141920`) | RC10f **open** | RC10c deferred

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

## Open bugs (active)

| ID | Symptom | Capture proof | Planned owner |
|----|---------|---------------|---------------|
| **RC10e** | Inner overlap still selectable / visible after hide+shorten+deselect (`DNTE` len 143 at overlap tick) | `141920` ~37.7s, ~44.6s | `NoteEditCurrentState` presence + `projectNoteEditDisplayNotes` overlap closure mask |
| **RC10f** | Selecting a second note to move resets the **previous** mover to `baselineMap` committed span | `141920` ~49.06s | `rebuildNoteEditFocusAtSelect` + geometry pipeline inactive-mover retention |

---

## RC10a — Leave-restore via presence (shipped)

**Invariant:** For a leave transition, only note IDs in `changedOverlapNoteIds` on the mover's same-pitch lane are eligible. Within that scope, `Hidden`/`Deleted` presence **or** span ≠ session baseline → restore candidate.

**Restore span:** `focus.baselineMap` / `storageTransactionBaseline` — not `row.committedSpan` unless proven equal.

**Log anchor:** ~48.992s — `changed=3 interactions=0` → expect `RestoreNote` during edit.

**Code:** `determineConstrainedGeometryTargetNoteIds` — `isRowHiddenOrDeleted` inside existing sticky + lane gates.

### RC10a acceptance

- [x] Hidden + `currentSpan == baselineMap` in sticky scope → target
- [x] `resolveAllConstrainedGeometry` uses baselineMap ticks for leave-restore
- [x] Native tests pass
- [x] HITL leave-restore (`140022`, `141218`, `141920`)

---

## RC10b — Deselect display authority (shipped)

While NOTE_EDIT active and overlaps hidden in current state, deselect must not show committed-pass ghost restore.

**Root cause:** `projectNoteEditDisplayNotes` returned raw `committedBaseNotes` when `!focus.active`.

**Fix:** Session-authoritative projection when current state has overlap display mask (`Hidden`/`Deleted` closure rows).

### RC10b acceptance

- [x] Native: inactive focus + hidden overlaps → committed ghosts omitted
- [x] HITL deselect frame count (`141218`, `141920` after overlap workflow: `DFRAME` 24–26, no post-workflow `29,29,29,29`)

---

## RC10e — Inner overlap stays removed (partial — HITL failed)

### Shipped (native)

`NoteEditCurrentState::applyEditSessionAction(ShortenNote)` no longer promotes `Hidden → Visible` after `HideNote` (fixes one promotion path).

### HITL failure — `session_20260807_141920`

RC10e did **not** solve “keep overlap removed on deselect / select sweep” for inner overlap committed under mover.

**Proof — overlap tail still selectable after hide+shorten:**

| Time | Log | Meaning |
|------|-----|---------|
| ~29.68s | `type=1 noteId=10 start=2640 end=2735` then `type=3 noteId=17 …` | Inner overlap hide under mover 17 |
| ~29.96s | `type=2 noteId=10 start=2640 end=2783` | Shorten overlap 10 |
| ~37.75s | `#CAP,…,DNTE,88,2640,2640,143,11` after `empty_step` + re-select at 2640 | **143-tick tail still in selectable inventory** |
| ~44.58s | `#CAP,…,DNTE,88,2640,2640,143,10` on `note_changed` at 2640 | Same after later overlap pass |

**Why RC10e was insufficient:**

1. Inner overlap often ends as **`Visible` shortened tail** (`currentSpan` 2640–2783, length 143), not `Hidden` — RC10b/e masks only apply to `Hidden`/`Deleted`.
2. `type=2` without a preceding `type=1` on the same pass still creates a **Visible** shortened row (`141920` ~32.4s, ~40.8s — `ShortenNote` only).
3. Selectable/display projection still surfaces closure participants with **Visible** shortened spans from `committedBase` + participant overlay.

**Remaining work (RC10e follow-up):**

- Extend overlap display mask: closure participants with overlap-under-mover geometry should stay off inventory when `changedOverlapNoteIds` marks them and current state is shortened/hidden vs session baseline — not only `Hidden` presence.
- Or: inner overlap geometry should commit **`Hidden`** (full removal from inventory), not a Visible 143-tick stub, when user expectation is “removed”.
- Native fixture from `141920` hide+shorten+deselect at 2640.

### RC10e acceptance

- [x] Native: hide then shorten → row stays `Hidden` (promotion path)
- [ ] HITL: no `DNTE` / selectable row at overlap tick after inner commit + deselect (`141920` **failed**)

---

## RC10f — Prior mover resets to baseline on second-note select (open)

### Symptom

Move note A (e.g. `noteId=17`) away from baseline (3600 → ~1728–3264). Select note B (e.g. `noteId=10` or `20`) to move. Note A **jumps back** to committed `baselineMap` span (3600–4127) on display and in geometry actions.

### Proof — `session_20260807_141920`

**Note 17 moved off baseline (~33–42s):**

```text
[33.559] EditSessionAction: type=3 noteId=17 start=1728 end=2255 pitch=88
[41.902] EditSessionAction: type=3 noteId=17 start=2928 end=3455 pitch=88
[42.390] EditSessionAction: type=3 noteId=17 start=3264 end=3791 pitch=88
```

**Second mover selected (~46.5s):** `select_apply … note_idx=10 … reason=note_changed` at tick 2640 (`noteId=10` becomes focus).

**Baseline reset when moving note 10 (~49.06s):**

```text
[49.059] POSITION EDIT: … tick 3456 -> 3552  (focus.last on note 10)
[49.061] GeometryPipeline: … baselineMap=16 lane=88 changed=0 … actions=2
[49.061] EditSessionAction: type=2 noteId=17 start=3600 end=4127 pitch=88   ← prior mover → committed baseline
[49.061] EditSessionAction: type=3 noteId=10 start=3552 end=3695 pitch=88
[49.178] DNTE,88,3600,3600,143,15
```

`ShortenNote` on note 17 uses **committed** ticks `3600–4127` (baselineMap), not the live moved span (~3264+). Prior mover edit is overwritten when geometry runs for the new focus note.

**Not macro commit:** all `NOTE_EDIT macro commit` lines in this capture are `skipped: select bracket mismatch` — reset happens during **geometry apply** on the second mover, not on successful macro commit.

### Hypothesis (for implementer)

- `rebuildNoteEditFocusAtSelect` on `note_changed` rebuilds closure/`baselineMap` from committed passes while **prior mover `currentSpan` in `NoteEditCurrentState` is not retained** as session authority for inactive movers; or
- overlap geometry re-constrains **non-focus** lane participants against `baselineMap` when the new mover resolves, emitting `ShortenNote`/`RestoreNote` that snap inactive movers to committed spans.

### Owners (candidates)

- `rebuildNoteEditFocusAtSelect` — preserve prior mover rows in `NoteEditCurrentState` across focus handoff; do not rematerialize inactive movers from `baselineMap` alone.
- `buildEditSessionActions` / geometry pipeline — do not emit baseline snap actions for movers no longer in `focus.movingNoteId` unless explicit leave-restore scope says so.
- Macro commit path (if bracket mismatch is fixed later) must commit mover A before focus moves to B.

### RC10f acceptance

- [ ] Move note 17 off 3600, select note 10, move note 10 — note 17 **stays** at last moved tick (no `type=2 noteId=17 start=3600 end=4127`)
- [ ] Native: two-mover handoff fixture from `141920` shape
- [ ] HITL: `141920` repro cleared

---

## RC10c — Authority trim (deferred)

Derive overlap participants from current state vs session baseline; retire geometry-path `overlapNotes` scratch.

---

## Capture validation summary

| Capture | RC10a leave | RC10b deselect | RC10e inner | RC10f multi-mover |
|---------|-------------|----------------|-------------|-----------------|
| `140022` | pass | fail (pre-RC10b) | — | — |
| `141218` | pass | pass | fail (`DNTE` 143) | not isolated |
| `141920` | pass | pass | fail (`DNTE` 143) | **fail** (`type=2 noteId=17` → 3600) |
