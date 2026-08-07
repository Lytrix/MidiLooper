# Note edit resolver authority contracts — refinement plan

**Status:** plan approved — tasks spec'd below (§6). Stage 0 (trace) must complete before Stage 3 code.
**OpenSpec disposition:** no new change. This plan enforces contracts already decided in the
**active** OpenSpec change `note-edit-current-state` (no ownership transfer, no transition
change, no new abstraction — no formal trigger). Tasks live in §6 of this doc; the tasks
4.4 / 7.5 drift is reconciled in the existing change via `/opsx:sync` when Stage 2 ships.
**Supersedes:** patch-by-patch RC10 fixes in
[note_edit_leave_restore_current_state_bugfix.md](note_edit_leave_restore_current_state_bugfix.md)
(RC10h and the RC10g sidebar remainder map onto stages below).
**Authority basis:** OpenSpec `note-edit-current-state` (design + tasks) — `NoteEditCurrentState`
owns editable geometry and presence; everything else is a derived reader or a gated writer.
**Evidence:** `captures/session_20260807_151441.log`, `captures/session_20260807_153739.log`,
commit `d29abf7`.

---

## 1. End-state pipeline (authority chain)

Each stage lists its owner and the only inputs it is allowed to trust in the end state.

| # | Stage | Owner | Allowed inputs (end state) |
|---|-------|-------|---------------------------|
| 1 | Identity | `EditorSelection` (`primaryNote`, `selectedTick`) | user select events |
| 2 | State authority | `NoteEditCurrentState` (`currentSpan`, `committedSpan`, `presence`) | writer stage 8 only |
| 3 | Derived latch | `NoteEditFocus` (`movingNoteId`, `last`, `commitBaseline`, `baselineMap`, `changedOverlapNoteIds`) | stages 1 + 2 — pure cache, reconstructible |
| 4 | Display projection | `projectNoteEditDisplayNotes` / `resolveParticipantDisplaySpan` | stages 2 + 3, committed passes |
| 5 | Selectable inventory | `selectableDisplayNotesForEditUi` | stage 4, filtered to editable rows |
| 6 | Driver gate | `isLiveEditDriverValidFromCurrentState` / `ensureNoteEditFocusForLiveEdit` | stages 1 + 2 + 3 |
| 7 | Geometry resolution | `NoteGeometryResolver` stages: `determineChangedCausingNotes` → `collectEvaluationScopeNoteIds` → `projectTransactionBaselineForEvaluationScope` → `determineEligiblePairs` → `analyzeEditSessionInteractions` → `resolveConstrainedGeometry` → `buildEditSessionActions` | stages 2 + 3 (`baselineMap`), edited span |
| 8 | Apply / write | `applyEditSessionActions` → current-state mutation → projection refresh | stage 7 actions only |
| 9 | Commit | macro commit / `commitNoteEditPass` | stage 2 vs committed baseline |

Inventory consumers (all read stage 5): `SelectFaderInput`, `GeometryFaderInput`,
`FaderMotorSync`, `FaderDependentSnapshot`, `EditEventFeedback`, `NoteEditButtonInput`,
`EditSelectNoteState`, `EditNoteStateCoordinator`, `NoteEditFocusRebuild`, `SidebarAndInfo`.
Whatever stage 5 offers **will** become a driver — that is the contract pressure point.

---

## 2. Contract audit — proven violations

### V1 — Selectable inventory offers non-projecting rows as editable drivers

**Evidence (`153739`):** select sweep lands `note_idx=8`, `DNTE,88,2640,2640,47` (the committed
leave-restore stub). Coarse fader logs `Coarse fader using focus.last: pitch=88, start=2640`,
`Overlap move bridge: ... end=2687`. Then 87× `pipeline did not apply` with
`GEOM_APPLY,pipeline,…,0,3,0` and **no** `GeometryPipeline:` debug line — the resolver returned
at its actions-empty check, before that log.

**Proof of mechanism (code):** `appendCausingNoteActions` emits `MoveNote` whenever
`readStoreLinearBaseline` finds a span and `causing.span.startTick` differs. Zero actions across
87 distinct target ticks therefore proves the driven row had **no note-on/off pair in the
projected session store** and no orphan-on at `focus.last`
(`causingNoteHasOrphanOnForAction`). A row that does not project to store was selected,
latched as `movingNoteId`, and silently dropped by every downstream stage.

### V2 — Driver gate validates span only, never projection/presence

**Code:** `isLiveEditDriverValidFromCurrentState` calls `readCurrentSpan` and compares against
`focus.last`. It never calls `rowProjectsToStore` and never reads `presence`. After `d29abf7`,
`rebuildNoteEditFocusForDisplayNote` sets `focus.last` **from** `currentSpan`, so the gate passes
by construction for any row that exists in current state — including rows the action builder
cannot act on. The gate and the builder disagree about what "valid driver" means.

### V3 — Causing-note action builder is still live-store authoritative

**Code:** `appendCausingNoteActions` (in `EditSessionActionBuilder.cpp`) compares the edited span
against `readStoreLinearBaseline` and falls back to `focus.last`. It never consults
`NoteEditCurrentState`, unlike the overlap-target path which already has
`readEditableCurrentSpan` / `editableRowProjectsToStore`.
**Spec drift:** OpenSpec `note-edit-current-state` tasks 4.4 ("route action-builder comparisons
through current-state spans and presence") and 7.5 ("remove remaining live-store geometry
authority in … action builder") are checked as done, but this reader was not migrated.
Reconcile via `/opsx:sync` when Stage 2 ships.

### V4 — Overlap closure membership derives from live-store diff, not current state

**Code:** `reconcileChangedOverlapNoteIdsFromLiveStore` diffs `focus.baselineMap` against the
live store span. For a Hidden row (absent from store) membership survives only via
`focus.overlapNotes` scratch or prior `changedOverlapNoteIds` — and the Phase-4 geometry
pipeline hides **without** writing scratch (documented in `baselineMapPitchLaneNeedsRestore`).
`rebuildNoteEditFocusForDisplayNote` clears and rebuilds focus on every reselect, so this
membership is exactly what a deselect → reselect round trip can lose. Current state carries the
authoritative membership signal (`presence != Visible` or `currentSpan != committedSpan`) and is
never consulted here.

### V5 — Sidebar `DNTE` / `noteToShow` reads a different span source than session projection

**Evidence (`151441`):** `DNTE,88,2640,2640,143` ↔ `DNTE,88,2640,2640,47` flicker during the
select sweep — session projection tail (143) alternating with committed stub (47). This is the
RC10g remainder; `SidebarAndInfo` / `FaderDependentSnapshot` do not route their span through
`resolveParticipantDisplaySpan`.

---

## 3. Open trace — post-deselect re-edit does not trigger overlap logic

HITL observation after `d29abf7`. **Partial evidence in `session_20260807_161329`** (Stage 0 instrumentation).

**Scenario:** overlap edit (hide/shorten) → empty-step deselect → reselect the same mover →
move it back into the same overlap zone.

**Read at each boundary in the capture:**

| Boundary | What to read | Divergence signal | `161329` result |
|----------|--------------|-------------------|-----------------|
| Deselect | `changedOverlapNoteIds` size, `focus.active`, pending diff | closure list emptied | Not isolated in this capture |
| Reselect rebuild | `movingNoteId`, `focus.last` source, `baselineMap` overlap rows, `changedOverlapNoteIds` after reconcile | membership dropped for Hidden rows | **Yes — ~25.259s:** reselect lands `noteId=17`, `changedOverlapNoteIds count=0` (was `count=1` + `changedOverlapNoteId=17` on prior selects of other notes at ~24.073s) |
| First move | `GeometryPipeline:` — `changed`, `candidates`, `pairs`, `interactions`, `actions` | overlap row missing from analysis | Moves still emit (`actions>=1`); some moves show `changed=0 interactions=0` (move only, no hide/shorten) — overlap side effects may be absent |
| Apply | `EditSessionAction` rows | Hide/Shorten absent where expected | Needs targeted re-run of full §3 scenario |

**Stage 3 verdict (provisional):** closure membership loss on **reselect of the mover** is
confirmed in `161329`. Stage 3 is **go** — derive membership from current state, not live-store
reconcile alone. Full §3 scenario still worth one dedicated capture to confirm overlap side
effects fail after reselect.

---

## 4. Target contracts

| ID | Contract (end state) |
|----|----------------------|
| C1 | Every `DisplayNote` returned by `selectableDisplayNotesForEditUi` refers to a row with `rowProjectsToStore == true` (Visible/Added). Leave-restore baseline paint of Hidden rows is display-only and never enters the selectable inventory. |
| C2 | `isLiveEditDriverValidFromCurrentState` requires: row exists in current state, `rowProjectsToStore == true`, and `currentSpan` matches `focus.last`. A row the action builder cannot move can never validate as driver. |
| C3 | All span comparisons in `EditSessionActionBuilder` read current state first. The causing-note path uses the same `readEditableCurrentSpan` / `editableRowProjectsToStore` helpers the overlap-target path already uses; live store is fallback only when current state is empty. |
| C4 | `changedOverlapNoteIds` membership is derived from current-state row diff (`presence != Visible` or `currentSpan != committedSpan`); the live-store diff in `reconcileChangedOverlapNoteIdsFromLiveStore` remains only as the empty-current-state fallback. |
| C5 | One span-resolution function (`resolveParticipantDisplaySpan`) feeds grid paint, sidebar `DNTE` / `noteToShow`, and `FaderDependentSnapshot`. No consumer re-derives a participant span from committed passes while a session is active. |

Shared invariant: **stages 3–7 must agree on the answer to "can this row be edited right now",
and that answer is a function of `NoteEditCurrentState` alone.**

---

## 5. Stages

One commit per stage (Multi-Stage-Bugfix-Workflow). Architecture checkpoint for all stages:
ownership change **no** (contracts already assign these owners in OpenSpec
`note-edit-current-state`; stages enforce, not move), state-transition change **no**.

### Stage 0 — trace instrumentation (investigation only)

- Add SESSION_CAPTURE lines: presence value at `rebuildNoteEditFocusForDisplayNote`
  (selected noteId, `presence`, `rowProjectsToStore`), and `changedOverlapNoteIds` size after
  `reconcileChangedOverlapNoteIdsFromLiveStore`.
- Run the §3 scenario, fill in the §3 table, and record the first divergence here.
- No behavior change; gate for Stage 3.

### Stage 1 — driver gate + inventory (C2, C1) — fixes the `153739` dead driver (RC10h)

- Owner: `isLiveEditDriverValidFromCurrentState` (`NoteEditFocusState.cpp`) and the selectable
  filter in `filteredSelectableDisplayNotesForNoteEdit` / `projectNoteEditDisplayNotes`.
- Invariant: a selected row is always actionable — driver validity implies the action builder
  can emit for it.
- Test: native fixture — Hidden row with matching `currentSpan`/`focus.last` must fail driver
  validation; selectable inventory over a projection containing a paint-only Hidden row must not
  return it.
- Log anchor: re-run the `153739` sweep scenario; select must land on a projecting row and moves
  must log `GeometryPipeline: … actions>=1`.
- **Decision (user, 2026-08-07):** when the sweep bracket falls on a paint-only stub, selection
  **snaps to the nearest selectable row** (by display-tick distance; tie-break: lower display
  tick — implementer pins the tie-break in the fixture).

### Stage 2 — action builder current-state migration (C3)

- Owner: `appendCausingNoteActions` in `EditSessionActionBuilder.cpp`.
- Invariant: skip/emit decisions for the causing note match what current state says the row
  currently is; live store consulted only when current state is empty.
- Test: existing `test_note_edit_current_state` fixtures plus a causing-note fixture where store
  and current state disagree; parity run of `test_edit_apply`.
- Follow-up: `/opsx:sync` OpenSpec `note-edit-current-state` tasks 4.4 / 7.5 wording to match
  the shipped reality.

### Stage 3 — overlap closure membership from current state (C4)

- Owner: `reconcileChangedOverlapNoteIdsFromLiveStore` (`NoteEditFocusOverlap.cpp`) and its
  call site in focus rebuild.
- Blocked on Stage 0 confirming membership loss is the divergence in §3.
- Invariant: deselect → reselect preserves overlap closure — a row hidden or shortened by the
  session stays a closure member until commit or session end.
- Test: native fixture — hide overlap via pipeline, rebuild focus for the mover (simulating
  reselect), assert `changedOverlapNoteIds` still contains the hidden row and the next resolve
  produces the expected Hide/Restore actions.

### Stage 4 — sidebar span authority (C5) — RC10g remainder

- Owner: `SidebarAndInfo` `DNTE` / `noteToShow` and `FaderDependentSnapshot`, routed through
  `resolveParticipantDisplaySpan`.
- Invariant: one span per (noteId, paint epoch) across grid, sidebar, and snapshot — kills the
  143 ↔ 47 flicker in `151441`.
- Test: native projection fixture asserting sidebar span == grid span for a session-shortened
  overlap row; HITL re-run of the `151441` sweep.

### Deferred

- Macro-commit handoff when selecting a different note with a pending prior mover
  (`macro commit skipped` in `151441`) — re-evaluate after Stages 1–3; the skip guard may
  already be correct once inventory and driver contracts hold.

---

## 6. Tasks

One commit per stage. Check off here; do not start a stage before its blockers are checked.

### Stage 0 — trace instrumentation (investigation only)

- [x] 0.1 Add SESSION_CAPTURE line in `rebuildNoteEditFocusForDisplayNote`: selected `noteId`,
      `presence`, `rowProjectsToStore` result.
- [x] 0.2 Add SESSION_CAPTURE line after `reconcileChangedOverlapNoteIdsFromLiveStore`:
      `changedOverlapNoteIds` count + ids.
- [x] 0.3 `pio run -e teensy41-capture-serial`; ask before upload.
- [x] 0.4 Run the §3 scenario (overlap edit → empty-step deselect → reselect mover → move back
      into overlap zone) with managed capture; fill in the §3 table in this doc. **Partial:**
      `session_20260807_161329` — closure membership trace filled; full deselect→reselect path
      not isolated.
- [x] 0.5 Record the first divergence in §3 and mark Stage 3 as confirmed or eliminated.
      **Provisional go** — reselect mover clears `changedOverlapNoteIds` (~25.259s).

### Stage 1 — driver gate + inventory (C2, C1) — RC10h

- [x] 1.1 Extend `isLiveEditDriverValidFromCurrentState`: require `rowProjectsToStore(noteId)`
      in addition to the current-span match.
- [x] 1.2 Exclude paint-only rows (painted Hidden/Deleted leave-restore spans) from
      `filteredSelectableDisplayNotesForNoteEdit` so `selectableDisplayNotesForEditUi` never
      returns them; grid paint keeps them (display-only).
- [x] 1.3 Snap-to-nearest via **existing** machinery — no new snap mechanism. The sweep and
      encoder both iterate `SelectNavigation::buildSelectNavigationSlots` over
      `selectableDisplayNotesForEditUi`, so the 1.2 exclusion makes landing on a stub impossible
      by construction; bracket-resolution call sites (`selectNoteAtBracket`, post-commit
      reselect, session undo) already snap via `selectClosestNote` (circular display-tick
      distance, first-in-display-order tie-break). Verify these paths cover the decision; only
      add code if a call site resolves a bracket without the `selectClosestNote` fallback.
      **Sweep semantics (default):** the excluded stub's tick becomes an ordinary empty 16th
      step slot — sweeping onto it deselects, consistent with existing deselect-by-empty-step
      UX; snap applies at bracket-resolution call sites, not mid-sweep.
- [x] 1.4 Native tests: (a) Hidden row with matching `currentSpan`/`focus.last` fails driver
      validation; (b) inventory over a projection containing a paint-only row excludes it;
      (c) snap picks the nearest selectable row including the tie-break case.
- [x] 1.5 `pio test -e native`; firmware build; ask before upload; HITL re-run of the `153739`
      sweep — select lands on a projecting row, every move logs `GeometryPipeline: … actions>=1`.
      **`session_20260807_161329`:** 0× `pipeline did not apply`, 0× `GEOM_APPLY,pipeline,…,0`;
      at tick 2640 coarse moves emit `actions>=1` (e.g. ~37.19s `MoveNote noteId=17`); sweep at
      ~48s lands `note_idx=8` on pitch **84** at 2640 (not the 88 stub).
- [x] 1.6 Update this doc + mark RC10h shipped in
      [note_edit_leave_restore_current_state_bugfix.md](note_edit_leave_restore_current_state_bugfix.md).

### Stage 2 — action builder current-state migration (C3)

- [x] 2.1 Rework `appendCausingNoteActions` to read the causing row via
      `readEditableCurrentSpan` / `editableRowProjectsToStore` (current state first, live store
      fallback only when current state is empty); keep the orphan-on emit path.
- [x] 2.2 Native fixture: causing note where store span and `currentSpan` disagree — emit/skip
      decision must follow current state.
- [x] 2.3 Parity: `test_edit_apply` and `test_note_edit_current_state` green; `pio test -e native`.
- [x] 2.4 `/opsx:sync` OpenSpec `note-edit-current-state` — reconcile tasks 4.4 / 7.5 wording
      with shipped reality.

### Stage 3 — overlap closure membership from current state (C4) — blocked on 0.5

- [ ] 3.1 Derive `changedOverlapNoteIds` membership in
      `reconcileChangedOverlapNoteIdsFromLiveStore` from current-state row diff
      (`presence != Visible` or `currentSpan != committedSpan`); keep the live-store diff as the
      empty-current-state fallback.
- [ ] 3.2 Native fixture: hide overlap via pipeline → rebuild focus for the mover (reselect) →
      `changedOverlapNoteIds` still contains the hidden row → next resolve emits the expected
      Hide/Restore actions.
- [ ] 3.3 `pio test -e native`; firmware build; HITL re-run of the §3 scenario — overlap logic
      triggers on the post-deselect re-edit.

### Stage 4 — sidebar span authority (C5) — RC10g remainder

- [ ] 4.1 Route `SidebarAndInfo` `DNTE` / `noteToShow` span through
      `resolveParticipantDisplaySpan`.
- [ ] 4.2 Route `FaderDependentSnapshot` span through the same function.
- [ ] 4.3 Native projection fixture: sidebar span == grid span for a session-shortened overlap
      row.
- [ ] 4.4 `pio test -e native`; firmware build; HITL re-run of the `151441` sweep — no
      143 ↔ 47 `DNTE` alternation; mark RC10g shipped in the RC10 plan.

---

## 7. Verification

- Per stage: `pio test -e native`, firmware build `pio run -e teensy41-capture-serial`, ask
  before upload.
- HITL edit flow per [HITL-Edit-Test-Flow.mdc](../../.cursor/rules/HITL-Edit-Test-Flow.mdc)
  after Stages 1 and 4.
- Capture anchors per stage recorded in this doc before marking a stage shipped.
