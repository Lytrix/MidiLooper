# Note edit leave-restore current-state bugfix (RC10)

**Status:** RC10a/b/e/f/h **shipped** | RC10g **partial** (native + `DFRAME` stable; HITL flicker open) | RC10c deferred

**Handoff:** RC10g sidebar remainder owned by the contracts plan —
[`note_edit_resolver_authority_contracts_refinement.md`](note_edit_resolver_authority_contracts_refinement.md)
(Stage 4 sidebar span authority). RC10h shipped in `182317b` — validated
`session_20260807_161329` (0× `pipeline,0`; sweep no longer lands on 88 stub at 2640).

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
| **RC10g** | Select sweep at overlap tick: `DNTE` alternates session tail (143) ↔ committed stub (47); move-past full baseline paint still open | `151441` ~306–311s | `SidebarAndInfo` / `FaderDependentSnapshot` + projection fingerprint follow-up |

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

## RC10f — Prior mover resets to baseline on second-note select (shipped)

### Root cause

`overlayAnalysisBaselineForSessionMovedOverlaps` already fed **analysis** into `resolveAllConstrainedGeometry` / `analyzeEditSessionInteractions`, but `buildEditSessionActions` was called with **stale `transactionBaselineAfterEnsure`** (committed `baselineMap`) for the projected baseline — emitting `ShortenNote` on the prior mover at committed 3600–4127 when the second mover resolved.

### Fix

`NoteGeometryResolver::resolve` — pass `analysisBaseline` as projected baseline to `buildEditSessionActions` (matches constrained geometry input).

**Follow-up (`145011`):** `RestoreNote` (type=0) still snapped prior mover via leave-restore when `NoteEditCurrentState` had session-moved span — `determineConstrainedGeometryTargetNoteIds` current-state path omitted `spanQualifiesForOverlapLeaveRestore` gate (live-store path had it via `020105`).

### RC10f acceptance

- [x] Native: `test_builder_overlay_baseline_blocks_prior_mover_baseline_snap_141920`
- [x] Native: `test_determine_targets_excludes_session_moved_current_state_145011`
- [x] HITL: `145433` — move 17 off 3600, select 13 at 35.35s — no `type=0`/`type=2` on note 17; last paint `DNTE,88,3264,…,527`

---

## RC10g — Display authority on deselect / select sweep (partial)

### Shipped (native + partial HITL)

1. **Session projection** — `projectNoteEditDisplayNotes` runs when `NoteEditCurrentState` is non-empty (deselect flicker / stale `visualCache` span).
2. **Leave-restore paint** — while `focus.active`, hidden overlap paints full `baselineMap` span once overlap baseline no longer intersects mover range; still masked while intersecting (RC10e inventory stub unchanged on deselect).
3. **Fingerprint** — `noteEditDisplayCacheFingerprint` mixes in current-state row presence/spans; `filteredSelectableDisplayNotesForNoteEdit` passes `noteEditCurrentState`.

**HITL `151441`:** `DFRAME` holds 17 rows post-workflow; overlap tick `2640` no longer interleaves len **527** on select sweep (143 ↔ 47 only).

### RC10g acceptance

- [ ] Move past hidden overlap: overlap visible at committed span outside mover (paint)
- [x] Deselect: `DFRAME` stable (17 rows; no post-workflow 29-row blow-up)
- [ ] Select sweep: no session tail ↔ committed stub flicker on same overlap tick (`143` ↔ `47` still open)
- [x] RC10e inventory gate on `151441` (masked tail len 143 expected while intersecting)
- [x] Native fixtures (`143654` shape + leave-restore paint)

### Next fix target (RC10g follow-up)

Wire **sidebar `DNTE` / `noteToShow`** to the same session-authoritative span as display projection (`NoteEditFocusDisplayProjection`), or stop emitting `DNTE` from stale `visualCache` / committed leave-restore stub rows during select settle (`empty_step` ↔ `note_changed`). Today `SidebarAndInfo` length comes from whichever display row `noteToShow` resolves to — session projection tail (143 ticks) vs committed restore stub (47 ticks) — while `FaderDependentSnapshot` motor sync follows the same split.

---

## RC10h — Highlighted overlap selection cannot move (open)

### Symptom

User selects overlap row at tick 2640 (display index 9). Row is **highlighted** but **flickers** (`DNTE` 143 ↔ 47). Coarse fader does not move the selected span.

### Capture proof — `session_20260807_151441`

| Time | Log | Meaning |
|------|-----|---------|
| ~304.99s | `macro commit skipped: moving=17 bracket=2256 focus_last=1296-1823` | Handoff to second mover blocked — note 17 session move not committed |
| ~306–311s | Repeated `selected note 9 at tick 2640`; `DNTE,88,2640,2640,143` / `47` | Select inventory flicker at overlap tick |
| ~313.18s | `focus.last start=2640`; `Overlap move bridge … 2640–2687`; `pipeline,…,0,3,0` | Coarse fader targets overlap stub; **zero** geometry actions |
| ~314.98s | `EditSessionAction: type=1 noteId=17 start=1296 end=1727` | Large coarse move applies **HideNote on prior mover 17**, not `MoveNote` on overlap row |

### Root cause (capture-backed)

1. Macro commit skipped on second-mover select leaves **note 17** as `movingNoteId` with body at **1296–1823**.
2. Overlap closure / leave-restore projection still surfaces a **selectable row at overlap tick 2640** tied to the same mover lane (inventory tail or restore stub).
3. `focus.last` syncs to overlap bracket (**2640–2687**, 47 ticks) while `movingNoteId` remains **17** — `isLiveEditDriverValidFromCurrentState` can still pass when `NoteEditCurrentState` matches the overlap stub.
4. Coarse fader runs `NoteGeometryResolver` for **note 17** from overlap coordinates → overlap **hide** actions or empty pipeline, not a position move of the highlighted overlap tail.

### Planned owner

- Macro-commit / handoff: `SelectFaderInput` + `isMacroCommitAlignedWithSelectTargetForTrack` when overlap row select must transfer mover.
- Selectable vs mover identity: `rebuildNoteEditFocusForDisplayNote` / `findBaselineNoteIdForDisplay` — do not treat overlap inventory row as movable driver when body span disagrees.
- Depends on RC10g sidebar authority split (same 143 ↔ 47 flicker).

### Contract fix (first divergence — shipped)

**First divergence:** `resolveParticipantDisplaySpan` painted `focus.movingNoteId` from stale `focus.last` before reading `NoteEditCurrentState`; `rebuildNoteEditFocusForDisplayNote` then overwrote `focus.last` from session-store linear span instead of `currentSpan`.

**Fix:** Mover + overlap projection reads `currentSpan` first; focus rebuild + select `applySelectNav` bracket derive from current state when driver valid.

## RC10c — Authority trim (deferred)

Derive overlap participants from current state vs session baseline; retire geometry-path `overlapNotes` scratch.

---

## Capture validation summary

| Capture | RC10a leave | RC10b deselect | RC10e inner | RC10f multi-mover | RC10g flicker | RC10h move |
|---------|-------------|----------------|-------------|-----------------|---------------|------------|
| `140022` | pass | fail (pre-RC10b) | — | — | — | — |
| `141218` | pass | pass | fail (`DNTE` 143) | not isolated | — | — |
| `141920` | pass | pass | pass (post-fix) | **fail** (`type=2 noteId=17` → 3600) | — | — |
| `143654` | pass | n/a | **pass** (no `DNTE` 143) | n/a | user pass | — |
| `151441` | pass | pass (`DFRAME` 17) | pass (143 tail) | **pass** (no snap to 3600) | **partial** (143↔47) | **fail** (`pipeline,0` / Hide 17) |
