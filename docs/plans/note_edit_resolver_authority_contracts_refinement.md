# Note edit resolver authority contracts — refinement plan

**Status:** architectural migration in progress — Stages 0–2, 4 (code), and 5 **shipped**; Stage 3
(participating-note model) **shipped**; Stages 6–8 migrate behavior behind participant discovery.
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
| 2 | State authority | `NoteEditCurrentState` (`currentSpan`, `committedSpan`, `presence`) | writer stage 8 only |
| 3 | Derived latch | `NoteEditFocus` (`movingNoteId`, `last`, `commitBaseline`, `baselineMap`, `changedOverlapNoteIds`) | stages 1 + 2 — pure cache, reconstructible |
| 4 | Display projection | `projectNoteEditDisplayNotes` / `resolveParticipantDisplaySpan` | stages 2 + 3, committed passes |
| 5 | Selectable inventory | `selectableDisplayNotesForEditUi` | stage 4, filtered to editable rows |
| 6 | Driver gate | `isLiveEditDriverValidFromCurrentState` / `ensureNoteEditFocusForLiveEdit` | stages 1 + 2 + 3 |
| 7 | Geometry resolution | `NoteGeometryResolver` → `buildEditSessionActions` | participating-note input + `baselineMap` |
| 8 | Apply / write | `applyEditSessionActions` → current-state mutation → projection refresh | stage 7 actions only |
| 9 | Commit | macro commit / `commitNoteEditPass` | stage 2 vs committed baseline |

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
6. Remove Focus-owned semantic state (`changedOverlapNoteIds` → derived query).
7. Convert display/inventory to derived projections (**Stage 8**).
8. Delete obsolete edge-case helpers only after equivalent invariant assertions exist.

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
| `focus.movingNoteId` | derived selection / driver state (not fundamental — multi-select ready) |
| `NoteEditCurrentNoteState.currentSpan` | `ParticipatingNoteState.currentSpan` |
| `NoteEditCurrentNoteState.committedSpan` | `ParticipatingNoteState.committedSpan` |
| `NoteEditPresenceType` / span diff | `ParticipatingNotePhase` + `shortenedVsCommitted` |
| `changedOverlapNoteIds` | derived participating set (Stage 4) |
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
| 5 (`8255fd7`) | `162713`, `163621` coarse | stale inventory index after hide |

---

## 3. Participating-note invariants

Define before migrating code — not “which helper to replace?” first.

### Per-note invariants

1. Every participating note has exactly one session state row.
2. `currentSpan` is the authoritative editable geometry.
3. `committedSpan` does not change during the edit session.
4. A **Hidden** note may be non-projecting but remains a participant.
5. A **shortened** note (same start, shorter end vs committed) remains a participant.
6. A non-projecting participant cannot become a selectable driver (Stage 1).
7. A participating note cannot disappear because focus was rebuilt without current-state change.
8. A restore transition restores from session **committed** baseline — never from a prior constrained/stub span alone (Stages 7–6).

### Session invariants

1. `selectedNoteIds ⊆ participatingNotes` (primary driver must project when driving geometry).
2. Every interaction endpoint in the overlap closure is a participant.
3. Every emitted overlap action targets a participating note.
4. Resolver overlap action choice cannot depend on whether a participant happens to project into the live store (Stage 6).
5. Rebuilding focus without changing `NoteEditCurrentState` cannot change the participant set or resolver result (Stage 4).

**Code anchor (Stage 3):** `include/ParticipatingNoteSession.h`,
`verifyParticipatingNoteInvariants`, `verifyParticipatingSessionInvariants`,
`test_note_edit_participating_note`.

---

## 4. Contract audit — proven violations

### V1 — Selectable inventory offers non-projecting rows as editable drivers — **fixed Stage 1**

### V2 — Driver gate validates span only, never projection/presence — **fixed Stage 1**

### V3 — Causing-note action builder live-store authoritative — **fixed Stage 2**

### V4 — Overlap closure membership from live-store diff — **Stage 4 target**

### V5 — Sidebar `DNTE` span split — **Stage 8 target**

---

## 5. Open trace — post-deselect re-edit (`161329`)

| Boundary | Divergence signal | `161329` result |
|----------|-------------------|-----------------|
| Reselect rebuild | membership dropped for Hidden rows | **Yes — ~25.259s:** `changedOverlapNoteIds count=0` |
| First move | overlap side effects absent | some `changed=0 interactions=0` move-only |

**Stage 4 target:** participant set from `NoteEditCurrentState`; `changedOverlapNoteIds` → derived query.

---

## 6. Target contracts

| ID | Contract (end state) |
|----|----------------------|
| C1 | Selectable inventory: projecting rows only (Stage 1). |
| C2 | Driver gate: `rowProjectsToStore` + span match (Stage 1). |
| C3 | Action builder reads current state first (Stage 2). |
| C4 | Participant set from `NoteEditCurrentState` — `changedOverlapNoteIds` is a query (Stage 4). |
| C5 | One span-resolution path for grid + sidebar + snapshot (Stage 8). |
| C6 | Coarse/fine driver survives inventory shrink (Stage 5). |
| C7 | Full baseline leave-restore + display when mover clears committed span (Stage 7). |
| C8 | Hide/shorten from current constrained geometry while overlap classified — no stale stub restore (Stage 6). |

Shared invariant: **participant state and resolver inputs must agree on editability without
inferring session semantics from live-store projection.**

---

## 7. Stages

### Stage 0 — evidence / traces — **DONE**

### Stage 1 — driver + inventory (C1, C2) — **DONE** (`182317b`)

### Stage 2 — current-state action authority on causing path (C3) — **DONE** (`497a072`)

### Stage 3 — participating-note state model — **DONE** (read model; no behavior change)

No firmware behavior change. `ParticipatingNoteSession` read model + invariant tests.

### Stage 4 — participant discovery / reselect (C4) — **DONE** (code; HITL 4.4 pending)

Route participation through current state; `changedOverlapNoteIds` membership from current-state rows when `noteEditCurrentState` is non-empty. Evidence: `161329`.

### Stage 5 — geometry driver / inventory sync (C6) — **DONE** (`8255fd7`)

### Stage 6 — action semantics from participant state (C8)

Hide/shorten beats stale restore while overlap classified. Evidence: `163621` ~44.212s. Blocked on Stage 4.

### Stage 7 — full leave/restore transition (C7) — **IN PROGRESS**

Hidden → full committed baseline restore → Visible via participating-note leave-restore contract. Evidence: `163621`, `175858`.

### Stage 8 — unified display projection (C5)

Sidebar `DNTE` via `resolveParticipantDisplaySpan`. Evidence: `151441`.

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

### Stage 6 — action semantics (was interim “Stage 7”)

- [ ] 6.1–6.5 `appendOverlapTargetActions` + fixtures + HITL `163621`.

### Stage 7 — leave/restore transition — **IN PROGRESS**

- [x] 7.1 Participating leave-restore helpers (`participatingNoteNeedsFullCommittedLeaveRestore`, committed span).
- [x] 7.2 `constrainedGeometryFromRestoreCandidate` uses committed span for hidden/shortened participants.
- [x] 7.3 Display projection paints `committedSpan` when mover left overlap zone.
- [ ] 7.4 HITL `163621` / `175858` full restore → Visible.

### Stage 8 — display projection (was interim Stage 4 sidebar)

- [ ] 8.1–8.4 sidebar + snapshot + HITL `151441`.

**Priority:** 6 → 7 (HITL) → 8.

---

## 9. Verification

- Per stage: `pio test -e native`; firmware build when behavior changes; ask before upload.
- HITL after Stages 4, 6, 7, 8.
- Anchors: `153739`, `151441`, `161329`, `162713`, `163621`.
