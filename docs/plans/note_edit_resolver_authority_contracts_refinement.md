# Note edit resolver authority contracts — refinement plan

**Status:** architectural migration in progress — Stages 0–2, 4–8 shipped; Stage 7.5 **A–E** shipped
(HITL `024301` PASS). **§11 step 5 in progress** (5.1 latch/geometry query pin). Stage 3 **shipped**.
**Remaining work:** §11 step 5 cleanup slices; orthogonal model §12 (conceptual; code post–Stage 8).
**Purpose:** evidence-backed migration toward an explicit participating-note edit-session model —
not a parallel bugfix sequence or an upfront state-machine rewrite.
**OpenSpec disposition:** no new change. Contracts plan enforces `note-edit-current-state` without
ownership transfer. `/opsx:sync` when Stage 2 shipped.
**Supersedes:** patch-by-patch RC10 fixes in
[note_edit_leave_restore_current_state_bugfix.md](note_edit_leave_restore_current_state_bugfix.md)
(RC10h and the RC10g sidebar remainder map onto stages below).
**Authority basis:** OpenSpec `note-edit-current-state` (design + tasks) — `NoteEditCurrentState`
owns editable geometry and presence; everything else is a derived reader or a gated writer.
**Evidence:** `captures/session_20260807_151441.log`, `captures/session_20260807_153739.log`,
`captures/session_20260807_162713.log`, `captures/session_20260807_163621.log`,
commit `d29abf7`.

---

## 1. End-state pipeline (authority chain)

Each stage lists its owner and the only inputs it is allowed to trust in the end state.

| # | Stage | Owner | Allowed inputs (end state) |
|---|-------|-------|---------------------------|
| 1 | Identity | `EditorSelection` (`primaryNote`, `selectedTick`) | user select events |
| 2 | State authority | `NoteEditCurrentState` (`currentSpan`, `committedSpan`, `presence`) | see **writers** below — not “Stage 8 only” for all fields |
| 3 | Derived latch | `NoteEditFocus` (`movingNoteId`, `last`, `commitBaseline`, `baselineMap`, `changedOverlapNoteIds`) | stages 1 + 2 — **transitional cache** until Stage 8; then derived query |
| 4 | Display projection | `projectNoteEditDisplayNotes` / `resolveParticipantDisplaySpan` | stages 2 + 3, committed passes |
| 5 | Selectable inventory | `selectableDisplayNotesForEditUi` | stage 4, filtered to editable rows |
| 6 | Driver gate | `isLiveEditDriverValidFromCurrentState` / `ensureNoteEditFocusForLiveEdit` | stages 1 + 2 + 3 |
| 7 | Geometry resolution | `NoteGeometryResolver` → `buildEditSessionActions` | participating-note input + `baselineMap` |
| 8 | Apply / write | `applyEditSessionActions` → current-state mutation → projection refresh | stage 7 actions only |
| 9 | Commit | macro commit / `commitNoteEditPass` | stage 2 vs committed baseline |

### `NoteEditCurrentState` writers (split)

| Field | Writer | Examples |
|-------|--------|----------|
| **`currentSpan`** | Interactive edit-session **apply** pipeline (Stage 8 path) | `HideNote`, `ShortenNote`, `MoveNote`, `RestoreNote` via `applyEditSessionActions` |
| **`committedSpan`** | **Commit / handoff sealing** (separate lifecycle) | `commitNoteEditPass`, `syncCommittedSpan` after macro commit (`200656`) |

Interactive geometry mutation goes through apply; commit updates the committed baseline. `syncCommittedSpan` seals `committedSpan` on handoff and aligns `currentSpan` to that sealed geometry (Stage 7.5.C4 / `013500`) so projection (`currentSpan` only) cannot flash a stale stub or previous length.

### Identity terminology (do not conflate)

| Concept | Meaning | Typical carrier |
|---------|---------|-----------------|
| **Selection identity** | What the user selected | `EditorSelection.primaryNote`, `selectedTick` |
| **Driver identity** | Which selected/participating note supplies the **interaction anchor** for geometry | `focus.movingNoteId`, `focus.last` — **not** always `primaryNote` |
| **Participant identity** | All notes involved in the edit session | `ParticipatingNoteSession` membership, `NoteEditCurrentState` rows |

Handoff bugs (`200354` → `200656`) conflated driver sealing with selection identity. **`primaryNoteId` is not necessarily the geometry driver** — it is selection identity mapped into the participating session.

Inventory consumers (all read stage 5): `SelectFaderInput`, `GeometryFaderInput`,
`FaderMotorSync`, `FaderDependentSnapshot`, `EditEventFeedback`, `NoteEditButtonInput`,
`EditSelectNoteState`, `EditNoteStateCoordinator`, `NoteEditFocusRebuild`, `SidebarAndInfo`.

Whatever stage 5 offers **will** become a driver — that is the contract pressure point.

---

## 2. Architectural migration

The codebase holds several representations of edit-session state: `NoteEditCurrentState`,
`NoteEditFocus`, selectable inventory, live-store geometry, and derived display spans.

The **participating-note model** is introduced incrementally at the authority boundary. It does
**not** replace the geometry resolver or UI projection in one step.

### Migration order

1. Define participating-note states and invariants (**Stage 3** — design, no behavior change).
2. Represent current session participants explicitly (`ParticipatingNoteSession`).
3. Keep existing resolver behavior unchanged — new model is an **input contract**, not a replacement resolver.
4. Route resolver inputs through participating-note state (**Stage 4**).
5. Remove live-store inference of session semantics.
6. **Post–Stage 8 cleanup:** remove `changedOverlapNoteIds` as **stored semantic state**; derive from participating-note model (see Stage 4 vs cleanup below).
7. Convert display/inventory to derived projections (**Stage 8**).
8. Delete obsolete edge-case helpers only after equivalent invariant assertions exist (**§11 step 5**).

### Implementation rule

**Do not** add another helper that answers a question about participating-note state if that
question can be answered by the explicit state model or an invariant.

| Stop and use the model | Likely valid resolver logic |
|------------------------|-----------------------------|
| `isHiddenButStillParticipating(...)` | `shouldRestoreBecauseNoLongerOverlapping(...)` |
| `isActuallyStillAnOverlapParticipantDespiteNotProjecting(...)` | constrained geometry from interactions |

### Current code → target model

| Current | Target |
|---------|--------|
| `EditorSelection.primaryNote` | `ParticipatingNoteSession.primaryNoteId` |
| `EditorSelection.selectedNotes` | `ParticipatingNoteSession.selectedNoteIds` |
| `focus.last` | derived driver geometry |
| `focus.movingNoteId` | derived **driver** identity (interaction anchor — not always `primaryNote`) |
| `NoteEditCurrentNoteState.currentSpan` | `ParticipatingNoteState.currentSpan` |
| `NoteEditCurrentNoteState.committedSpan` | `ParticipatingNoteState.committedSpan` |
| `NoteEditPresenceType` / span diff | visibility + lifecycle + derived geometry predicates (§12) |
| `changedOverlapNoteIds` | **Stage 4:** membership authority from current state when rebuilding focus; **post–Stage 8:** derived query, cache removed |
| `focus.baselineMap` | explicit baseline decision at session boundary |
| `selectableDisplayNotesForEditUi` | derived UI inventory |
| live session store | persistence representation — **not** session state |

### Resolver position (initial)

```text
NoteEditCurrentState + EditorSelection
            │
            ▼
   ParticipatingNoteSession   ← Stage 3 read model
            │
            ▼
     EXISTING resolver        ← unchanged in Stage 3–4
            │
            ▼
          Actions
```

Gradually remove `readStoreLinearBaseline`, `rowProjectsToStore`, and
`editableRowProjectsToStore` from places that infer **session semantics** (not storage I/O).

### Shipped migration steps (keep commits)

| Stage | Evidence | What it reduced |
|-------|----------|-----------------|
| 1 (`182317b`) | `153739` | paint-only rows as drivers |
| 2 (`497a072`) | causing-note path | live-store authority on mover |
| 3 (`06c0bdb`) | invariant tests | implicit session semantics → `ParticipatingNoteSession` read model |
| 4 (`941d87a`) | `161329`, HITL `175858` | live-store overlap closure inference on reselect |
| 5 (`8255fd7`) | `162713`, `163621` coarse | stale inventory index after hide |
| 6 (`a5cad55`, `e402b19`) | `181859`, `163621` | leave-restore + L→R overlap vs committed span |
| 6.5 (`643fee2`–`c4136fd`) | §8 shipped table + HITL below | closure latch, handoff commit, paint vs inventory |
| 7 (code) | `163621`, `175858` | leave-restore display paints `committedSpan` (HITL 7.4 open) |

---

## 3. Participating-note invariants

Define before migrating code — not “which helper to replace?” first.

### Per-note invariants

1. Every participating note has exactly one session state row.
2. `currentSpan` is the authoritative editable geometry.
3. `committedSpan` does not change during an **active edit interval**; it may be replaced when that interval is sealed by commit/handoff.

```text
edit interval
    committedSpan ───────────── immutable
                         ↓
                     macro commit / handoff seal
                         ↓
                  new edit interval
                  committedSpan = sealed geometry (syncCommittedSpan — 200656)
```

4. A **Hidden** note may be non-projecting but remains a participant.
5. A **shortened** note (same start, shorter end vs committed) remains a participant.
6. A non-projecting participant cannot become a selectable driver (Stage 1).
7. A participating note cannot disappear because focus was rebuilt without current-state change.
8. A restore transition restores from session **committed** baseline — never from a prior constrained/stub span alone (Stages 7–6).

### Session invariants

1. `selectedNoteIds ⊆ participatingNotes`.
2. Any note used as an **active geometry driver** must be projecting and pass the driver gate (C1/C2) — selection identity ≠ driver identity.
3. Every interaction endpoint in the overlap closure is a participant.
4. Every emitted overlap action targets a participating note.
5. Resolver overlap action choice cannot depend on whether a participant happens to project into the live store (Stage 6).
6. Rebuilding focus without changing `NoteEditCurrentState` cannot change the participant set or resolver result (Stage 4).

**Code anchor (Stage 3):** `include/ParticipatingNoteSession.h`,
`verifyParticipatingNoteInvariants`, `verifyParticipatingSessionInvariants`,
`test_note_edit_participating_note`.

---

## 4. Contract audit — proven violations

### V1 — Selectable inventory offers non-projecting rows as editable drivers — **fixed Stage 1**

### V2 — Driver gate validates span only, never projection/presence — **fixed Stage 1**

### V3 — Causing-note action builder live-store authoritative — **fixed Stage 2**

### V4 — Overlap closure membership from live-store diff — **fixed Stage 4** (reselect); display path cleanup open

### Stage 4 vs `changedOverlapNoteIds` cleanup (do not conflate)

| Phase | What | Status |
|-------|------|--------|
| **Stage 4 (shipped)** | **Membership authority** — participant set from `NoteEditCurrentState` so focus rebuild does not drop Hidden overlap participants (`161329`, `941d87a`) | **DONE** |
| **Transitional** | `changedOverlapNoteIds` remains a **reconstructible Focus cache** filled from current state when non-empty — still touched by geometry/display until Stage 8 | **IN PROGRESS** |
| **Post–Stage 8 cleanup (§11 step 5)** | Remove cache as stored semantic state; derive overlap-participant queries from participating-note model; drop `readStoreLinearBaseline` semantic inference | **In progress** (5.1 shipped; full removal blocked on 5.3 design) |

Stage 4 **done** does **not** mean “do not touch `changedOverlapNoteIds`” — display projection (step 1) still migrates consumers; **removal** of the field waits until Stage 8 convergence.

### V5 — Sidebar `DNTE` span split — **Stage 8 target**

Sidebar `drawNoteInfo` / `#CAP DNTE` must use the same participant span as grid paint (`projectNoteEditDisplayNotes` / `resolveParticipantDisplaySpan`), not a parallel `focus.last` or committed-base read. Evidence: `151441`.

---

## 5. Open trace — post-deselect re-edit (`161329`)

| Boundary | Divergence signal | `161329` result |
|----------|-------------------|-----------------|
| Reselect rebuild | membership dropped for Hidden rows | **Yes — ~25.259s:** `changedOverlapNoteIds count=0` |
| First move | overlap side effects absent | some `changed=0 interactions=0` move-only |

**Stage 4 shipped:** membership authority from current state on reselect. **`changedOverlapNoteIds` cleanup** (derived query, cache removal) is **post–Stage 8** — see table above.

---

## 6. Target contracts

| ID | Contract (end state) |
|----|----------------------|
| C1 | Selectable inventory: projecting rows only (Stage 1). |
| C2 | Driver gate: `rowProjectsToStore` + span match (Stage 1). |
| C3 | Action builder reads current state first (Stage 2). |
| C4 | Participant **membership authority** from `NoteEditCurrentState` on reselect (Stage 4). `changedOverlapNoteIds` → derived query + cache removal **after Stage 8**. |
| C5 | One **participant projection contract** for grid + sidebar + snapshot (Stage 8); separate rendering consumers. |
| C6 | Coarse/fine driver survives inventory shrink (Stage 5). |
| Leave-restore span | Overlap participant restores full `committedSpan` only when mover has cleared overlap interaction — not while closure still active (Stage 7 native; Stage **7.5** geometry hardening parked). |
| C8 | Hide/shorten from current constrained geometry while overlap classified — no stale stub restore (Stage 6). |
| **C9** | **Projection/inventory independence:** selectable inventory is a **filtered consumer** of display projection; removing a row from inventory must **never** cause display projection to lose a **visible** participant (`202147` → `2aeb175`). |

### Primary projection contract (C5 + §8)

```text
participant
    ├── visible == false  → no display row (regardless of currentSpan, committedSpan, visualCache)
    └── visible == true   → project currentSpan
```

`resolveParticipantDisplaySpan` end state: committed-span restoration happens in **mutation/apply** before projection — not by projection reading `committedSpan` when `visible == false`.

### Display data flow (target — not inventory → paint)

```text
CurrentState
    ↓
Display projection ──────────→ paint (grid / sidebar / snapshot)
    ↓
inventory filtering ─────────→ selectable drivers
```

**Wrong:** `CurrentState → inventory → paint` (caused `202147` leak).

Shared invariant: **participant state and resolver inputs must agree on editability without
inferring session semantics from live-store projection.**

---

## 7. Stages

### Stage 0 — evidence / traces — **DONE**

### Stage 1 — driver + inventory (C1, C2) — **DONE** (`182317b`)

### Stage 2 — current-state action authority on causing path (C3) — **DONE** (`497a072`)

### Stage 3 — participating-note state model — **DONE** (read model; no behavior change)

No firmware behavior change. `ParticipatingNoteSession` read model + invariant tests.

### Stage 4 — participant discovery / reselect (C4) — **DONE** (code; HITL 4.4 done)

Establishes **current-state authority for participant membership** — focus rebuild does not drop participants. `reconcileChangedOverlapNoteIdsFromLiveStore` uses current-state rows when `noteEditCurrentState` non-empty. Evidence: `161329`, HITL `175858`.

`changedOverlapNoteIds` remains a **transitional reconstructible cache** until Stage 8 convergence; **post–Stage 8** it becomes a derived query and the cache is removed (§11 step 5). Further display consumers may still migrate to current-state membership before removal.

### Stage 5 — geometry driver / inventory sync (C6) — **DONE** (`8255fd7`)

### Stage 6 — action semantics from participant state (C8) — **DONE** (code; HITL 6.5 pending)

Hide/shorten beats stale restore while overlap classified. Evidence: `163621` ~44.212s, `181859`.

### Stage 7 — full leave/restore transition — **DONE** (code; HITL 7.4 partial)

Hidden → full committed baseline restore → Visible via participating-note leave-restore contract. Evidence: `163621`, `175858`. Projection contracts shipped (`220917`, `221717`). **Remaining geometry bugs** → Stage **7.5** (not projection).

### Stage 7.5 — overlap restore and macro-commit geometry — **partial** (A–E native; E HITL next)

Extension of Stage 7. Geometry/apply failure modes — **not** Stage 8 display projection. **Do not start §11 step 5 until slice E HITL passes.**

| Slice | Symptom | Captures | Primary fix owner |
|-------|---------|----------|-------------------|
| **A — premature leave-restore** | `RestoreNote` fires on pipeline `interactions=0` while overlap closure still active → stationary note jumps to full `committedSpan` | `222418`, `224633`, `223447` | `determineConstrainedGeometryTargetNoteIds`; `EditSessionActionBuilder` |
| **B — macro commit seals elongated span** | F1 macro commit writes overlap-elongated `currentSpan` onto stationary participant | `225025` | `buildCommitOverlapRowsFromCurrentState` |
| **C — select-fader seal** | Empty-step deselect skipped seal / painted previous length | `004532`, `013500`, `020050` PASS | `isMacroCommitAlignedWithSelectTarget`; `syncCommittedSpan`; projection `currentSpan` only |
| **D — leave without restore** | Pitch + time-axis leave after Hide/Shorten: **fixed** (`RestoreNote` to `committedSpan`). Slice A still defers while closure active. | `020050`, `021407`, `022151` | `determineConstrainedGeometryTargetNoteIds` |
| **E — multi-note hide + post-deselect restore** | Elongated mover CompleteCover of shortened stubs: **HideNote** each; sealed Deleted after deselect: no reinsert/Restore | `022849` | `appendOverlapTargetActions`; `markRowDeleted` on commit; leave-restore Hidden-only |

**Native test homes:**

| Suite | Slice | Fixture target |
|-------|-------|----------------|
| `test_resolve_constrained_geometry` | A | No restore target while closure active |
| `test_edit_session_action_builder` | A | No `RestoreNote` on `interactions=0` + active closure |
| `test_note_edit_current_state` | B/C | Macro seal / deselect contracts |
| `test_resolve_constrained_geometry` | **D** | Pitch leave after Hide/Shorten → vacated restore; LTR time-axis Shorten leave → restore when closure cleared |
| `test_edit_session_action_builder` / interaction / participating | **E** | CompleteCover Hide×2; no reinsert for Deleted; scope excludes Deleted |

HITL gate: A/B pending; C `020050` PASS; D `021407`/`022151`; E `024301` PASS — step 5 unblocked.

### Stage 8 — unified display projection (C5) — **DONE**

One **participant projection contract** (same participant, visibility gate, authoritative `currentSpan`) for grid, sidebar `DNTE`, and snapshot — **separate rendering consumers**. Evidence: `151441`.

**End-state projection rule:** primary contract — visibility gates projection; spans are authoritative for geometry — see §6 and §12.

---

## 8. Tasks

### Stage 0 — **DONE** (`2012fef`)

### Stage 1 — **DONE** (`182317b`)

### Stage 2 — **DONE** (`497a072`)

### Stage 3 — participating-note state model

- [x] 3.1 `ParticipatingNoteSession`, `ParticipatingNoteState`, `ParticipatingNotePhase` types.
- [x] 3.2 `buildParticipatingNoteSession` from `EditorSelection` + `NoteEditCurrentState`.
- [x] 3.3 Invariant verify helpers + `test_note_edit_participating_note`.
- [x] 3.4 Invariants documented in §3.
- [x] 3.5 `pio test -e native` green (no firmware behavior change).

### Stage 4 — participant discovery / reselect — **DONE** (`941d87a`; HITL `175858`)

- [x] 4.1 Derive participant set from current state (`currentStateRowIsOverlapParticipant`, `collectOverlapParticipantNoteIdsFromCurrentState`).
- [x] 4.2 `reconcileChangedOverlapNoteIdsFromLiveStore` uses current state when non-empty; rebuild passes `noteEditCurrentState`.
- [x] 4.3 Native reselect fixture (`161329` hidden overlap + empty store).
- [x] 4.4 HITL `175858` — reselect keeps `changedOverlapNoteIds` note 17.

### Stage 5 — **DONE** (code `8255fd7`)

- [ ] 5.4 HITL `162713`; `163621` coarse pass confirmed.

### Stage 6 — action semantics (was interim “Stage 7”) — **DONE** (code; HITL pending)

- [x] 6.1 `overlayAnalysisBaselineForSessionMovedOverlaps` uses committed span for leave-restore qualifying overlap participants.
- [x] 6.2 L→R into overlap classifies `OverlapNoteOff` against committed geometry (not stub `BoundaryTouch`).
- [x] 6.3 Native fixtures `181859` (interaction + action builder).
- [x] 6.4 `pio test -e native` green (910).
- [ ] 6.5 HITL shorten/closure/handoff — **`203805` shorten PASS**; full-overlap commit paint **step 1 native shipped** → [note_edit_full_overlap_commit_display_bugfix.md](note_edit_full_overlap_commit_display_bugfix.md) (HITL pending)

#### Stage 6.5 follow-up — overlap closure transition (sequenced; do not bundle)

| Step | Scope | Status |
|------|--------|--------|
| **(2)** | Participating-note **overlap closure** rule: while mover intersects participant committed closure, Hidden/Shortened stay constrained by active interaction; committed/session baseline drives Restore only when `participatingNoteOverlapInteractionCleared`. Helpers: `participatingNoteOverlapClosureActive`, overlay + `determineConstrainedGeometryTargetNoteIds` + action builder. Native: `test_overlap_closure_active_and_cleared`, `test_closure_active_suppresses_leave_restore_target_193632`, `test_builder_advance_with_overlap_closure_shorten_not_restore_193632`, updated `111955` (leave-restore when cleared). | **DONE** — native 915; HITL **`195514` PASS** |
| **(3)** | Mover handoff: `resolveMacroCommitSelectTargetNoteId` + macro commit on different `NoteId`; seal prior mover before focus rebuild. **Follow-up:** `syncCommittedSpan` after macro commit so leave-restore does not use stale `committedSpan` (`200656`). Native: handoff tests + `test_sync_committed_span_leave_restore_uses_sealed_position_200656`. | **DONE** — HITL `201057` handoff @1344 clean |
| **(1)** | Display/projection: Shortened participant stays semantically Shortened (`Visible` + inventory-masked tail); inventory mask separate from selectable projection; paint shortened stub while overlap active. **Follow-ups:** paint/inventory cache split (`202147` → `2aeb175`); Hidden→Visible on overlap-tail Shorten (`202538` → `c4136fd`). | **DONE** — HITL **`203805`** shorten paint PASS |

#### Shipped commits (Stage 6.5)

| Commit | Step | Owner / change | Native | HITL anchor |
|--------|------|----------------|--------|-------------|
| `643fee2` | **(2)** | `participatingNoteOverlapClosureActive`, `participatingNoteOverlapInteractionCleared`; overlay + `determineConstrainedGeometryTargetNoteIds` + action builder | 915 | `195514` Shorten-not-Restore vs `193632` |
| `bd2f53a` | **(3)** | `resolveMacroCommitSelectTargetNoteId`; `SelectFaderInput` macro commit when bracket owns another `NoteId` | 916 | `200354` — no bracket mismatch |
| `ad9215e` | **(3)** | `NoteEditCurrentState::syncCommittedSpan` after macro commit (mover + overlap rows) | 917 | `200656` stale `committedSpan` revert |
| `7c21e73` | **(1)** | Overlap-tail `ShortenNote` stays `Visible`; `rowIncludedInSelectableInventory`; inventory mask vs paint stub | — | `201057` handoff @1344 |
| `2aeb175` | **(1)** | Split paint vs selectable caches — `noteEditPaintDisplayCacheNotes_`, `projectedNoteEditDisplayNotes`, `filteredSelectableDisplayNotesForNoteEdit`; `NoteMovementUtils` uses filtered API | — | `202147` geometry OK, paint leak |
| `c4136fd` | **(1)** | Hidden→Visible on overlap-tail Shorten; builder skips Hide when closure active + shortened; leave-restore paint for visible shortened; closure uses `baselineMap` when row missing | 921 | `202538`, `203805` shorten |

**HITL `session_20260807_203805` (post `c4136fd`)** — L→R/R→L **ShortenNote** chains + stub paint **PASS**. New issue: **full overlap HideNote** on short note — inventory drops but grid still paints committed span after commit → [note_edit_full_overlap_commit_display_bugfix.md](note_edit_full_overlap_commit_display_bugfix.md). **Fix direction:** visibility projection contract + C4 display participant routing (not `visualCache` patch). Roadmap §11; matrix §10.

**HITL `session_20260807_202147` (post `7c21e73`, pre `2aeb175`)** — **ShortenNote** chains on overlap participant noteId=13 (`interactions=1`, `type=1` from ~`21.0s`). Inventory masking OK. **DISP** repeatedly `frameNotes=12` **visualCache=13` — shortened stub not painted because `projectedNoteEditDisplayNotes` still returned **filtered selectable** notes. Fixed: `2aeb175` — dedicated paint cache in `NoteEditDisplayProjection.cpp`.

**HITL `session_20260807_200354` (post `bd2f53a`, pre `ad9215e`)** — vs `195514`: **no** `NOTE_EDIT macro commit skipped: select bracket mismatch` lines. Handoff path runs macro commit (`pre-commit` note 9 @ ~`42.7s`). Confirms `resolveMacroCommitSelectTargetNoteId` bracket gate; `200656` later exposed stale `committedSpan` → `ad9215e`.

**HITL `session_20260807_202538` (post paint/inventory split)** — ShortenNote chains OK; L→R still hidden: first overlap frame emits **HideNote** full baseline then Shorten on **Hidden** row (presence stayed Hidden). R→L shorten paint OK. Re-entry **HideNote** reset `currentSpan` to full length. Fixed: Shorten promotes Hidden→Visible shortened; builder skips Hide when closure active + already shortened; leave-restore paints committed when mover left overlap zone.

**HITL `session_20260807_195514` (post-(2) firmware)** — vs `193632`:

| Check | `193632` | `195514` |
|-------|----------|----------|
| R→L into overlap | RestoreNote stubs on note 17 | **ShortenNote** chain on note 17 (`interactions=1`) |
| L→R back into overlap | Broken | ShortenNote resumes (~22.8s) |
| Leave-restore when cleared | Mixed with active overlap | RestoreNote only when `interactions=0` and mover past committed end (~22.1s, ~23.5s) |
| Mover handoff | Prior mover resets | **FAIL** — `macro commit skipped: select bracket mismatch` (×4); stale `changedOverlapNoteId=9` on rebuild; `RestoreNote` on note 9 at ~35s |
| Display inventory | Hidden not shortened | DNTE count=1 at overlap entry — **(1)** still open |

**HITL `session_20260807_193632` (pre-(2))** — overlap shows hidden; R→L advance emitted RestoreNote not ShortenNote; handoff reset.

**HITL `session_20260807_200656` (post-(3) bracket fix)** — bracket gate pass; prior mover still jumped to original baseline: macro commit sealed note 9 @2544 (`201.7s`) but `committedSpan` stayed 2208 → `RestoreNote` @203.8s on new mover cleared overlap. Fixed: `NoteEditCurrentState::syncCommittedSpan` after macro commit.

### Stage 7 — leave/restore transition — **DONE** (native); HITL 7.4 partial

- [x] 7.1 Participating leave-restore helpers (`participatingNoteNeedsFullCommittedLeaveRestore`, committed span).
- [x] 7.2 `constrainedGeometryFromRestoreCandidate` uses committed span for hidden/shortened participants.
- [x] 7.3 Display projection: visible shortened paints **stub** while `participatingNoteOverlapClosureActive`; paints **`committedSpan`** when `participatingNoteOverlapInteractionCleared` (uses `focus.last` as causing span — same rule as geometry). Hidden/Deleted: **no** projection-side leave-restore.
- [ ] 7.4 HITL `163621` / `175858` on device with step-3 firmware. Native contracts: `test_visible_shortened_paints_committed_length_after_ltr_overlap_cleared_220917`, `test_visible_shortened_paints_stub_while_overlap_closure_active_221717`.

#### Step 3 projection ship log (2026-08-07)

| Commit / slice | Invariant | Native | HITL |
|----------------|-----------|--------|------|
| `8596950` | Hidden overlap: no grid row regardless of `visualCache` | step 1 tests | `203805` |
| `ea39f1c` | C9 paint ≠ inventory | C9 contracts | — |
| `9248a70` | Hidden: no projection leave-restore; paint after `RestoreNote` apply only | step 3 tests | `215621` partial |
| `a2ddd90` | 8.2 fader/snapshot paint-cache span | 928 tests | — |
| `fc84652` | 8.3 paint base from materialized passes (not `visualCache`) | 928 tests | — |
| *(224633)* | 8.4 HITL — V5/C5 DISP paint≠visualCache; DNTE stub/mover split | — | `224633` PASS |
| *(225025)* | Stage 7.5 slice B — macro commit `2544–2831` then select DNTE len 287 (no `RestoreNote`) | — | parked → § Stage 7.5 |

**HITL `session_20260807_220917` (~35s):** L→R shorten on overlap 17 while mover 13 advanced — grid dropped overlap note when mover cleared committed closure; fixed by painting `committedSpan` once `participatingNoteOverlapInteractionCleared`.

**HITL `session_20260807_221717` (~35.7s):** Short mover 9 over long overlap 17 — premature committed-length paint during active shorten chain; fixed by gating leave-restore paint on `participatingNoteOverlapInteractionCleared` (inclusive closure vs `focus.last`), not `linearStorageSpansOverlapLocal` on `movingNoteRange`.

### Stage 7.5 — overlap restore and macro-commit geometry — **SHIPPED** (native); HITL pending

Geometry/apply layer — **not** Stage 8 projection. See §7 Stage 7.5 summary table.

**Slice A — premature leave-restore** (`RestoreNote` on `interactions=0` while closure active):

| Capture | Anchor |
|---------|--------|
| `222418` | note 9 `3216–3750` ~44s / ~51s |
| `224633` | note 9 `RestoreNote` @ 24.3s; note 13 @ 51s / 64s |
| `223447` | note 13 `RestoreNote` @ 23.9s, 51.2s, 64.5s |

- [x] 7.5.A1 `participatingNoteVisibleOverlapTailInProgress` gates `determineConstrainedGeometryTargetNoteIds`.
- [x] 7.5.A2 `EditSessionActionBuilder` — no `RestoreNote` while visible same-start tail in progress.
- [x] 7.5.A3 Native: `test_leave_restore_deferred_visible_overlap_tail_224633`, `test_builder_skips_restore_visible_overlap_tail_224633`.
- [ ] 7.5.A4 HITL replay on `222418` / `224633` overlap chains.

**Slice B — macro commit seals overlap-elongated span** (no `RestoreNote`; commit writes wrong length):

| Capture | Anchor |
|---------|--------|
| `225025` | macro commit @ 26.3s seals note 9 `2544–2831`; F1 select @ 36.9s / 48.7s → DNTE `len=287` (stub was **47**) |

- [x] 7.5.B1 `buildCommitOverlapRowsFromCurrentState` skips overlap length/range while visible tail + closure active.
- [x] 7.5.B2 Native: `test_commit_skips_overlap_length_while_visible_tail_active_225025`.
- [ ] 7.5.B3 HITL: overlap shorten → macro commit → select stationary note (`225025` chain).

**Slice C — select-fader seal on every F1 navigation** (empty-step deselect skipped overlap commit):

| Capture | Anchor |
|---------|--------|
| `004532` | second overlap shorten pass → geometry `noteId=9 end=1774` (**287 ticks**); empty-step deselect showed stale **47** until re-select |
| `010657` | post-seal correct @1296 (**143**); F1 scrub @1440 briefly flashed stale stub **47** from `currentSpan` after `syncCommittedSpan` |
| `013500` | empty-step deselect skipped seal (`macro commit skipped` @38.2s / @54.3s); projection leave-restore painted previous `committedSpan` → brief old-length flash |

**Invariant:** every F1 select move (note **or** empty step) runs `macroCommitPendingEditsBeforeSelectNav` **before** `clearVisibleOverlapParticipationBeforeDeselect` / focus rebuild. Projection paints **`currentSpan` only** (C5) — never leave-restores `committedSpan` on the paint path.

- [x] 7.5.C1 `macroCommitPendingEditsBeforeSelectNav` shared by note and empty-step branches (`SelectFaderInput.cpp`).
- [x] 7.5.C2 Native: `test_second_overlap_shorten_commits_before_deselect_clears_participation_004532`.
- [x] 7.5.C4 `syncCommittedSpan` aligns `currentSpan` + `refreshNoteEditSessionProjection` after macro commit — native `test_macro_sealed_sync_committed_aligns_current_span_on_reselect_010657`. **RC:** `commitEditAction` session overlay re-applied pre-commit stub from `sessionSnapshot` (`011855`).
- [x] 7.5.C6 Empty-step deselect seals when `focus.last` matches mover `currentSpan` (`isMacroCommitAlignedWithSelectTarget` + current state); projection drop of sealed leave-restore paint (`013500`). Native: focus handoff empty-deselect cases + stub-after-clear projection contracts.
- [ ] 7.5.C3 HITL: `004532` second-pass shorten → any F1 move away → DNTE **287** without re-select.
- [ ] 7.5.C5 HITL: `010657` post-seal F1 scrub — no stub **47** flash on overlap note grid/sidebar.
- [x] 7.5.C7 HITL: `020050` PASS vs `013500` — empty deselect seals (`pre-commit` @21.5s / @42.7s); **0** `macro commit skipped` (was 2); post-seal DNTE sealed lengths **335** then **191** with no previous-length flash.

**Not the fix location:** Stage 8 sidebar/paint (`SidebarAndInfo`, `liveEditDisplayNoteAtSelect`) — symptom in `225025` is wrong committed span after macro commit, not projection read path.

**Slice D — leave without restore** (**shipped native**; HITL next):

Constrain works; leave (pitch or time) must restore. Evidence: `020050` / `021407` (pitch), `022151` (LTR).

#### D1 — Pitch leave after Hide (vacated lane) — shipped

| Capture | Evidence |
|---------|----------|
| `020050` fail → D1 fix | Hide on lane 88 then pitch-away had no `RestoreNote`; HITL `021407`/`022151` Hide restore OK |
| Time-axis contrast | Same-pitch Hide leave already restored (`020050` @24.992s) |

#### D2 — Pitch leave after Shorten (vacated lane) — shipped (`021407`)

Vacated-lane path accepts Visible + `shortenedVsCommitted`; restore geometry = `committedSpan`.

#### D2b — LTR time-axis leave after Shorten — shipped (`022151`)

| Time (`022151`) | Pipeline | Actions |
|-----------------|----------|---------|
| 123.928s | LTR shorten | `ShortenNote` type=1 note **9** end→2868 + `MoveNote` |
| 124.241s (pre-fix) | `interactions=0 constrained=0 actions=1` | **only** `MoveNote` — **no `RestoreNote`** |

Root: third leave-restore branch skipped unsealed Visible shortened (prior seal-owned decision). Fix: remove that skip; keep slice A `visibleOverlapTailInProgress` deferral while closure active.

#### Root cause (code — proven)

**Shared owner:** `determineConstrainedGeometryTargetNoteIds` (`ResolveConstrainedGeometry.cpp`).

**Pitch:** vacated-lane path (Hidden/Deleted or Visible shortened) after `ChangePitch` away. Without `currentState`, `011115` still excludes vacated spam.

**Time-axis:** third branch now leave-restores Visible shortened once interaction cleared; sealed path still restores to sealed `committedSpan`.

#### Intended contract (slice D)

```text
HideNote / ShortenNote applied by mover M on participant P
  → when M leaves P’s overlap closure (time clear OR pitch vacate of P’s lane)
  → emit RestoreNote for P to committedSpan
```

Must **preserve** RC9i / `000657` and slice A premature-restore deferral.

#### Solution (implemented)

Vacated-lane + same-lane cleared Visible shortened targets; `constrainedGeometryFromRestoreCandidate` returns `committedSpan` for Visible shortened leave-restore candidates.

#### Slice D checklist

- [x] 7.5.D0 Documented (`020050` / `021407` / `022151` anchors)
- [x] 7.5.D1 Native: pitch Hide → pitch-away → leave-restore — `test_pitch_vacated_lane_hidden_leave_restore_target_020050`
- [x] 7.5.D2 Native: pitch Shorten → pitch-away → leave-restore — `test_pitch_vacated_lane_visible_shortened_leave_restore_021407`
- [x] 7.5.D2b Native: LTR Shorten leave → restore — `test_ltr_time_axis_visible_shortened_leave_restore_022151`
- [x] 7.5.D3 Legacy `011115` without currentState still excludes vacated spam
- [x] 7.5.D4 Builder `RestoreNote` — Hidden / pitch shortened / LTR shortened fixtures
- [x] 7.5.D5 HITL pitch leave-restore (`021407` / `022151`); LTR leave-restore (`022151`)

**Slice E — multi-note hide + post-deselect restore** (**shipped native**; HITL next):

#### E1 — Multi-participant CompleteCover Hide (shipped)

| Time (`022849`) | Pre-fix | Fix |
|-----------------|----------|-----|
| 70.446s | `interactions=2 constrained=2 actions=1` — only `MoveNote` | `appendOverlapTargetActions`: when CompleteCover + already-shortened live stub → `HideNote` (keep `202538` skip for partial cover) |

#### E2 — No restore after deselect seal (shipped)

| Time (`022849`) | Pre-fix | Fix |
|-----------------|----------|-----|
| 88.061s → 92.045/92.329 | commit Delete left presence Hidden; reselect Shorten+Restore | `markRowDeleted` on committed overlap Deletes; evaluation scope skips Deleted; leave-restore Hidden-only; builder skips reinsert for Deleted |

**Intended contract (slice E):**

```text
When mover M completely covers participants P1…Pn → HideNote each Pi (even if already shortened)
When M leaves a Pi that was only session-constrained (unsealed Hidden) → RestoreNote to committedSpan
When deselect seals Pi as Deleted → later reselect + move past must NOT Shorten/Restore Pi
```

#### Slice E checklist

- [x] 7.5.E0 Documented (`022849` anchors)
- [x] 7.5.E1 Native: `test_builder_complete_cover_hides_two_shortened_stubs_022849`
- [x] 7.5.E2 Native: sealed Deleted — builder skip + leave-restore + evaluation scope fixtures
- [x] 7.5.E3 HITL `024301` PASS vs `022849` fail (multi-Hide + sealed baselineMap=10, no Restore 11/12)

### Stage 8 — display projection (was interim Stage 4 sidebar) — **DONE** (code; HITL 8.4 logged)

- [x] 8.1 Sidebar `DNTE` / LEN via paint-cache participant span for `selection.primaryNote` (fixes **V5** flicker — `SidebarAndInfo::drawNoteInfo`).
- [x] 8.2 Fader / snapshot consumers — `liveEditDisplayNoteAtSelect` + F1 motor `makeDependentFaderBuildInput` use paint-cache participant span (same contract as grid).
- [x] 8.3 Committed paint base from `materializedLoopEventsForNoteEditFocus` + `reconstructDisplayNotes` (not `loop.visualCache`); `bumpSessionPreviewRevision` after geometry apply.
- [x] 8.4 HITL `151441` / V5 convergence — `224633` PASS; `225025` Stage 7.5 slice B logged.

**Primary projection contract:**

```text
participant
    ├── visible == false  → no display row
    └── visible == true   → project currentSpan
```

Projection does **not** classify shortened / moved / restored — that is resolver and session semantics. Committed-span restoration for leave-overlap runs in **apply/mutation** before projection.

End state for `resolveParticipantDisplaySpan`:

```cpp
if (!participant.visible) return {};  // no row
return participant.currentSpan;
```

**Priority:** §11 sequence — full-overlap paint fix and 7.4 HITL precede Stage 8 convergence.

---

## 9. Verification

- Per stage: `pio test -e native`; firmware build when behavior changes; ask before upload.
- HITL after Stages 4, 6, 7, 8.
- Anchors: `153739`, `151441`, `161329`, `162713`, `163621`, `203805`.

---

## 10. Authority test matrix

Contract matrix for implementation and native tests — not a bug anthology. **Today:** map `visible` from `presence != Hidden && != Deleted` until §12 lands in code.

| Scenario | Participant | Visible | Current geometry | Paint | Inventory |
|----------|-------------|---------|------------------|-------|-----------|
| Normal | yes | yes | committed | yes | yes |
| Moved | yes | yes | moved | yes | yes |
| Shortened overlap | yes | yes | shortened (stub) | **shortened span** | **masked** / tail excluded |
| Hidden overlap | yes | no | constrained | **no** | **no** |
| Hidden + full geometry retained | yes | no | committed (unchanged spans) | **no** | **no** |
| Leave overlap | yes | yes | committed (restored) | yes | yes |
| Rebuilt focus | unchanged | unchanged | unchanged | unchanged | unchanged |
| Nonparticipant store row | no | — | — | no | no |

**Heart of the architecture (latest confusion):**

| State | Paint | Inventory |
|-------|-------|-----------|
| Hidden + committed geometry | **NO** | **NO** |
| Visible + shortened geometry | **PAINT SHORTENED** | **MAY MASK** |

Step 1 native test must prove the hidden row with: `currentSpan != committedSpan`, `committedSpan` present, `visualCache` contains note, participant exists, inventory excludes — **grid paint still excludes** (independent of `visualCache`). Sidebar projection same rule when step 4 unifies paths.

---

## 11. Remaining roadmap (approved sequence)

**Do not refactor scaffolding while the behavioral contract is still being proved.** Helper removal and latch cleanup are **step 5 only** — after Stage 8.

| # | Step | Prove / deliver | Resolver / action semantics |
|---|------|-----------------|-----------------------------|
| **1** | **Fix full-overlap Hide paint** | `visible == false` → **projection emits no row** — regardless of `currentSpan`, `committedSpan`, `visualCache`, or inventory membership. Inventory also excludes. | **No change** | **Shipped** (`8596950`) — leave-restore re-show → step 3 |
| **2** | **Contract test C9** — projection/inventory independence | Visible shortened: **paint shortened stub**; inventory **may mask**. Complements step 1 (hidden: neither paints). | Proves paint ≠ inventory | **Shipped** (native) |
| **3** | **Stage 7.4 HITL** | Hidden → overlap cleared → geometry restored to `committedSpan` → **visible** → **paint restored** | Hidden = not displayable, not deleted | **Shipped** (native) — HITL `163621`/`175858` pending |
| **4** | **Stage 8** | Grid + sidebar + snapshot — one **participant projection contract** (**C5**); separate rendering consumers | Final convergence | **Done** (code + HITL `224633`) |
| **4.5** | **Stage 7.5** overlap restore + macro-commit geometry | A–E shipped; E HITL `024301` PASS. | Geometry/apply only — not projection | **Done** |
| **5** | **Semantic cleanup** (refactor phase — not behavioral migration) | Latch/geometry query pin (5.1); full latch removal needs design session (5.3) | No new behavior in 5.1–5.2 | **In progress** |

### Step 1 — projection gate (strong invariant)

```text
visible == false
    → no display row
    (NOT: if (presence == Hidden) … while another path paints committedSpan)
```

Native proof conditions (grid required; sidebar when unified in step 4):

- participant exists in session
- `currentSpan != committedSpan` (or full geometry retained)
- `committedSpan` present
- `visualCache` still contains note
- inventory excludes note
- **`visible == false` → grid projection excludes note**

Implementation: [note_edit_full_overlap_commit_display_bugfix.md](note_edit_full_overlap_commit_display_bugfix.md) — visibility gate + C4 participant source; **not** `visualCache` patch.

### Step 2 — contract test C9 (not “mere regression”)

**Projection/inventory independence:** selectable inventory is a filtered consumer of display projection; filtering a row from inventory must **never** cause display projection to lose a **visible** participant.

| State | Paint | Inventory |
|-------|-------|-----------|
| Visible shortened participant | ✅ shortened span | ❌ tail / row may be masked |
| Hidden participant | ❌ | ❌ |

Evidence: `202147` (inventory OK, paint failed) → `2aeb175` (split caches).

### Step 3 evidence

Native: `220917`, `221717` leave-restore projection contracts. HITL anchors: `163621`, `175858`, `181859` restore on note 9. **Parked → Stage 7.5 slice A:** `222418` — full `RestoreNote` on overlap clear while closure still active.

### Step 4 evidence

`151441` (sidebar span split **V5**). Requirement: same participant, visibility gate, authoritative `currentSpan` — not identical rendering implementation across grid, sidebar, and snapshot.

**8.1 shipped:** `SidebarAndInfo::drawNoteInfo` overrides `noteToShow` + `displayStartTick` from `editManager.projectedNoteEditDisplayNotes(track)` when `selection.primaryNote` is in the paint cache — sidebar LEN/DNTE match grid paint span.

**8.2 shipped:** `liveEditDisplayNoteAtSelect` prefers paint-cache span for `selection.primaryNote` when not geometry-driving; `makeDependentFaderBuildInput` select-target path uses paint span for F1 motor sync.

**8.3 shipped:** Committed paint base from `materializedLoopEventsForNoteEditFocus`; `bumpSessionPreviewRevision` after geometry apply.

**8.4 HITL `session_20260807_224633` — PASS** (V5/C5 convergence on post-8.3 firmware).

**HITL `session_20260807_223447` — Stage 8.1/8.2 PASS:**

| Check | Result |
|-------|--------|
| V5 sidebar DNTE vs paint | Note **13** selected @ 1728 → `DNTE,len=47` stub; mover **9** @ same bracket → `len=534` — correct primary split |
| Shorten chain ~28s | Note **13** mover: DNTE `len=47` throughout R→L shorten over note **9** |
| DISP paint vs `visualCache` | Overlap hide: `frameNotes=11,visualCache=12`; restore: `12,12,12,12` |
| Step 3 projection | No hidden-row paint leak during overlap (`paint=11` while `visualCache=12`) |
| Parked (7.5 A) | `RestoreNote` note **13** @ 23.9s, 51.2s, 64.5s on `interactions=0` — same class as `222418`; not projection |

Pitch edit tail (~68s): note **9** `2256–2591`, DNTE stable `len=335` across F4 changes.

**HITL `session_20260807_224633` — 8.4 PASS:**

| Check | Result |
|-------|--------|
| DISP decoupled from `visualCache` | `10/11/12` paint vs `visualCache=12` during overlap |
| V5 DNTE | Mover **13** shorten: `len=47`; note **9** @ 1728 after restore select: `len=335` (post-restore geometry) |
| Parked (7.5 A) | `RestoreNote` note **9** @ 24.3s (`interactions=0`) |

**HITL `session_20260807_225025` — Stage 7.5 slice B (parked):** No `RestoreNote`. Macro commit @ 26.3s seals note 9 `2544–2831`; F1 select @ 36.9s → `DNTE,len=287` (not stub 47). User: lengthened once, resolved after deselect/reselect. See §8 Stage 7.5 slice B.

### Step 5 — semantic cleanup (explicit refactor phase)

**Invariant (pinned, 5.3):** sticky clear sets
`NoteEditCurrentNoteState.overlapParticipation = Ended` without rewriting `currentSpan`.
`currentStateRowIsOverlapParticipant` is false for Ended rows even when geometry still differs.
Focus `changedOverlapNoteIds` is dual-write only until 5.5 — not participation authority.

| Slice | Deliver | Status |
|-------|---------|--------|
| **5.1** | Query aliases + pin latch≠geometry divergence; inventory mask uses latch query | **Shipped** (native) |
| **5.2** | Migrate display/inventory readers to named latch/geometry queries; dual-assert where safe | **Partial** → readers prefer current-state participation after 5.3 |
| **5.3** | Encode sticky end-of-participation as `NoteEditOverlapParticipationType::{Active,Ended}` on current state; clear → Ended; Shorten/Hide/Restore → Active | **Shipped** (native) — DEC-030 |
| **5.4** | Geometry pipeline + pre-commit use derived participation only | Next |
| **5.5** | Delete `changedOverlapNoteIds`; drop live-store membership reconcile; OpenSpec compat flag separate | Blocked on 5.4 |

- `readStoreLinearBaseline` — already removed from tree
- `NOTE_EDIT_PROJECTED_STORE_COMPAT` / `rowProjectsToStore` semantic uses — separate track after latch removal

**5.3 encoding (Option A):** `Ended` is orthogonal to `NoteEditPresenceType` so Visible shortened stubs still project. `ParticipatingNoteState` mirrors `overlapParticipation`.

---

## 12. Model refinement — orthogonal dimensions (design direction)

**Status:** conceptual contract for steps 1–4; **code representation** (`bool visible`, lifecycle enum) — **post–Stage 8** refactor. Do **not** mix §12 into step 1 implementation.

### Four orthogonal dimensions (not three)

| Dimension | Question |
|-----------|----------|
| **Participation** | Membership in `ParticipatingNoteSession` |
| **Visibility** | Should this participant **project**? |
| **Geometry** | `currentSpan` vs `committedSpan` |
| **Lifecycle** | Existing / **Added** / **Deleted** — mutation and commit semantics |

**Do not collapse `Deleted` into `visible == false`.** Deleted and temporarily hidden must stay distinguishable for mutation/commit. **Added** is lifecycle, not projection.

Eventually:

```cpp
bool visible;
LifecycleState lifecycle;  // Existing, Added, Deleted
```

### Visibility vs geometry

`NoteEditPresenceType` and `ParticipatingNotePhase` currently overload visibility and geometry. **Moved is not a presence state** — it is `currentSpan.start != committedSpan.start`.

Leave-overlap = two changes (often simultaneous):

```text
visibility:  hidden → visible
geometry:    current constrained span → committedSpan
```

### Primary invariant

- **Visibility** → whether a participant projects
- **`currentSpan`** → what geometry projects when visible
- **`committedSpan`** → session baseline for comparison and restoration; immutable within an edit interval, sealed on commit/handoff (per-note invariant 3)

### Target read model (proposed — post–Stage 8)

```cpp
struct ParticipatingNoteState {
    NoteId noteId;
    NoteBaseline currentSpan;
    NoteBaseline committedSpan;
    bool visible;
    LifecycleState lifecycle;
};
```

Derive geometry predicates — **do not store** when spans are authoritative:

```cpp
bool isRightTailShortened() const {
    return currentSpan.startTick == committedSpan.startTick &&
           currentSpan.endTick < committedSpan.endTick;
}
bool isMoved() const {
    return currentSpan.startTick != committedSpan.startTick;
}
bool hasShorterDuration() const {
    return (currentSpan.endTick - currentSpan.startTick) <
           (committedSpan.endTick - committedSpan.startTick);
}
bool hasOriginalLength() const {
    return (currentSpan.endTick - currentSpan.startTick) ==
           (committedSpan.endTick - committedSpan.startTick);
}
```

`isRightTailShortened` = overlap-tail shortening (current interaction). `hasShorterDuration` = broader “shorter than committed” (e.g. moved + shorter end). Do not encode interaction implementation into a single `isShortened()` name.

### Examples

| committed | current | visible | Meaning |
|-----------|---------|---------|---------|
| [100, 200] | [120, 220] | true | visible + moved + original duration |
| [100, 200] | [100, 150] | true | visible + right-tail shortened |
| [100, 200] | [120, 170] | true | visible + moved + shorter duration (not `isRightTailShortened`) |
| [100, 200] | [100, 150] | false | hidden + constrained geometry retained |
| [100, 200] | [100, 200] | false | hidden + full original geometry retained |

### Bugs in this framing

**Full-overlap Hide (`203805`):** `visible == false` → projection must emit nothing (step 1).

**Shortened overlap (`203805` shorten — step 2):** `visible == true`, shortened `currentSpan` → paint stub; inventory may mask.

### Migration policy

| When | Action |
|------|--------|
| Steps 1–4 | Keep `NoteEditPresenceType` + `ParticipatingNotePhase`; implement **visibility projection contract** in code |
| Stage 8 | Single projection path: `visible` + `currentSpan` |
| Step 5 | `bool visible` + `LifecycleState`; remove phase enum; derive predicates |
| Step 5 | Remove helpers that infer participation/presence/session semantics from live-store projection |

**Principle:** do not encode span relationships as state enums when spans are authoritative.

**Code anchors today:** `include/NoteEditCurrentState.h`, `include/ParticipatingNoteSession.h`, `NoteEditFocusDisplayProjection.cpp` (`projectNoteEditDisplayNotes`).

---
