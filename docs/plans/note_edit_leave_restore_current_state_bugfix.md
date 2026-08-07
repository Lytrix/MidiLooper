# Note edit leave-restore current-state bugfix (RC10)

**Status:** RC10a/b/e **shipped** | RC10f **in progress** | RC10g **open** | RC10c deferred

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
| **RC10f** | Selecting a second note to move resets the **previous** mover to `baselineMap` committed span | `141920` ~49.06s | `rebuildNoteEditFocusAtSelect` + geometry pipeline inactive-mover retention |
| **RC10g** | Hidden overlap not painted while moving past; **full baseline** on deselect; select fader flickers baseline ↔ shortened length | `143654` user report | `DisplayNoteResolve` paint authority + projection cache fingerprint |

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

## RC10e — Inner overlap stays removed (shipped — HITL pending)

### Shipped (native + firmware)

1. `NoteEditCurrentState::applyEditSessionAction(ShortenNote)` — tail shorten sets `Hidden`.
2. **`RestoreNote` leave-restore** — shortened-tail restore keeps `Hidden` (does not promote inventory-masked overlap to `Visible`; `142819` root cause).
3. `noteEditCurrentStateOverlapRowIsDisplayMasked` display projection mask (defense in depth).

### HITL gate — `session_20260807_141920` replay

**Pre-fix proof — overlap tail still selectable after hide+shorten:**

| Time | Log | Meaning |
|------|-----|---------|
| ~29.68s | `type=1 noteId=10 start=2640 end=2735` then `type=3 noteId=17 …` | Inner overlap hide under mover 17 |
| ~29.96s | `type=2 noteId=10 start=2640 end=2783` | Shorten overlap 10 |
| ~37.75s | `#CAP,…,DNTE,88,2640,2640,143,11` after `empty_step` + re-select at 2640 | **143-tick tail still in selectable inventory** |
| ~44.58s | `#CAP,…,DNTE,88,2640,2640,143,10` on `note_changed` at 2640 | Same after later overlap pass |

**Why prior partial RC10e was insufficient:**

1. Inner overlap often ended as **`Visible` shortened tail** (`currentSpan` 2640–2783, length 143), not `Hidden` — RC10b/e masks only applied to `Hidden`/`Deleted`.
2. `type=2` without a preceding `type=1` on the same pass still created a **Visible** shortened row (`141920` ~32.4s, ~40.8s — `ShortenNote` only).
3. Selectable/display projection still surfaced closure participants with **Visible** shortened spans from `committedBase` + participant overlay.

### RC10e acceptance

- [x] Native: hide then shorten → row stays `Hidden` (promotion path)
- [x] Native: shorten-only tail → `Hidden` + projection omits overlap (`141920` shape)
- [x] Native: leave-restore shortened tail stays `Hidden` (`142819` shape)
- [x] HITL: no `DNTE` / selectable row at overlap tick after inner commit + deselect (`143654` **passed**; `142819` **failed** pre-fix)

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

## RC10g — Display authority on deselect / select sweep (open)

### Symptoms

1. Overlap hidden under mover **not restored on piano roll** while moving past (mover left overlap zone).
2. Same overlap **reappears at full committed length** after deselect (empty-step select).
3. Select fader sweep over that region **alternates** baseline length vs shortened length on sidebar / roll.

### Root cause (code-backed)

| Layer | Behavior |
|-------|----------|
| **Active move** | `projectedNoteEditDisplayNotes` masks inventory-hidden overlap rows (RC10e) → overlap absent from paint. |
| **Deselect** | `DisplayNoteResolve` with `!focus.active` can fall back to **`loop.visualCache.notes`** (committed passes, full baseline) instead of session projection. |
| **Select sweep** | Empty step clears `focus.active` → visualCache paint; note step sets `focus.active` → projection — length oscillates. |
| **Cache** | `noteEditDisplayCacheFingerprint` omits `NoteEditCurrentState` presence → stale selectable list between steps. |

RC10e correctly fixed **selectable inventory** (`143654`: no `DNTE` len 143). RC10g splits **paint authority** from **inventory mask**.

### Planned fix (paint vs inventory)

1. **Paint** — While `NoteEditSession` is open, piano roll always uses `projectedNoteEditDisplayNotes`; never raw `visualCache` fallback when session has current-state overlap mutations.
2. **Leave-restore display** — When mover has **left** overlap zone, restore **full `baselineMap` span** for paint (RC10a). Keep inventory-masked `Hidden` only for inner stub at overlap tick under mover (RC10e).
3. **Fingerprint** — Include current-state overlap mask in display cache fingerprint / invalidate on presence change.
4. **Native** — hide overlap → move past → projection shows full baseline outside mover; deselect no visualCache leak; select sweep stable length.

### RC10g acceptance

- [ ] Move past hidden overlap: overlap visible at committed span outside mover (paint)
- [ ] Deselect: paint matches session projection, not committed visualCache alone
- [ ] Select sweep: no baseline ↔ shortened length flicker on same `NoteId`
- [ ] RC10e inventory gate still holds (no `DNTE` len 143 at inner overlap tick)
- [ ] Native fixture from `143654` shape

---

## RC10c — Authority trim (deferred)

Derive overlap participants from current state vs session baseline; retire geometry-path `overlapNotes` scratch.

---

## Capture validation summary

| Capture | RC10a leave | RC10b deselect | RC10e inner | RC10f multi-mover |
|---------|-------------|----------------|-------------|-----------------|
| `140022` | pass | fail (pre-RC10b) | — | — |
| `141218` | pass | pass | fail (`DNTE` 143) | not isolated |
| `141920` | pass | pass | pass (post-fix) | **fail** (`type=2 noteId=17` → 3600) |
| `143654` | pass | n/a | **pass** (no `DNTE` 143) | n/a |
