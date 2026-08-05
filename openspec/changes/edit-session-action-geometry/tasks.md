# Tasks — edit-session-action-geometry

> **Active** — UIP Phases 1–5 code shipped ([`unified-interval-projection`](../unified-interval-projection/)); Phase 6 doc sync complete (2026-08-04). Pre-analysis (D20) uses shipped **Edit projection** (`buildEditProjectionContext`, `projectEditIntervalsForAnalysis`) — not a new `normalizeWrapToLinear` module.

## 0. OpenSpec and docs

- [x] 0.1 `proposal.md`, `design.md`, delta specs, `tasks.md` (this change)
- [x] 0.2 Handoff [`docs/plans/note_edit_session_action_geometry_enhancement.md`](../../docs/plans/note_edit_session_action_geometry_enhancement.md)
- [x] 0.3 Mark [`note_edit_overlap_invariant_matrix_enhancement.md`](../../docs/plans/note_edit_overlap_invariant_matrix_enhancement.md) superseded
- [x] 0.4 User approval of D1–D22 + **`EditSessionAction`** / **`InteractionType`** vocabulary (2026-08-04)
- [x] 0.5 Append DECISION_LOG when implementation starts
- [ ] 0.7 Prior art decisions → [`edit_session_action_geometry_prior_art_refinement.md`](../../docs/plans/edit_session_action_geometry_prior_art_refinement.md)
- [ ] 0.8 LOOP_MIDI guide NOTE_EDIT pairing paragraph (Q10)
- [ ] 0.9 Park follow-on OpenSpec **`capture-pass-boundary-materialization`** (Q9 + Q16 NoteMinLength hot stop)
- [x] 0.10 Brownfield interim fixes documented in `design.md` § Brownfield interim (NOTELEN fader block, empty-step F2–F4, playback audition, `edit_minimal` HITL smoke) — preserve on pipeline wire

## 1. Types + relationship analysis (Phase 1)

- [x] 1.1 Add `include/EditSessionAction.h` — **`InteractionType`** (no **None**), **`EditSessionInteraction`**, **`TargetNoteInteractionGroup`**, **`EditSessionInteractionsByTarget`**, **`ConstrainedNoteGeometry`**, action types
- [x] 1.2 Add D17 **orchestrator** helpers: **`isSelectedNote`**, **`isIntraSelectionPair`**, **`geometryChangedThisTick`**, **`determineChangedCausingNotes`**, **`determineEligiblePairs`** — NOT inside analyze
- [x] 1.2a Wire **Edit projection** (D20) — **`buildEditProjectionContext`** + **`projectEditIntervalsForAnalysis`** ([`IntervalProjection`](../../../include/Utils/IntervalProjection.h)) for linear causing/target spans before analyze; no new `normalizeWrapToLinear` module
- [x] 1.3 Add `analyzeEditSessionInteractions(changedCausingNotes, …)` — pure geometry; one **`InteractionType`** per eligible **(causing, target)** pair; positive graph only
- [x] 1.4 Add `groupEditSessionInteractionsByTarget()` in `EditSessionInteraction.cpp` — ephemeral; full regroup each tick (D15)
- [x] 1.5 Add `determineConstrainedGeometryTargetNoteIds()` + `resolveConstrainedGeometry(..., noteMinLengthTicks, noteMinLengthRemoveEnabled)` — **constrained geometry target notes** scope + **combine precedence**; no action types
- [x] 1.6 Native `test/test_edit_session_interaction/` — D17; D20; D21 pitch-lane scope; Add=Move logic; geometry parity fixtures; same-tick boundary; no **None** enum
- [x] 1.7 Native `test/test_resolve_constrained_geometry/` — B+C then B gone (rebuild, not remove)
- [x] 1.8 No storage mutation in analyze / group-by-target / resolver

## 2. Action builder (Phase 2)

- [x] 2.1 Add `buildEditSessionActions(constrainedGeometry, editedGeometry, transactionBaseline, liveStore)` — **edit session action builder** + **omit actions that would not change live store**; **Invariant — Constrained Geometry Authority**
- [x] 2.2 Deterministic **edit session action apply** order: **RestoreNote** → **ShortenNote** → **HideNote** → causing note geometry actions
- [x] 2.3 Native `test/test_edit_session_action_builder/`
- [x] 2.4 Log hook (debug): relationship → action mapping when `SESSION_CAPTURE`

## 3. Apply (Phase 3)

- [x] 3.1 Add `applyEditSessionActions()` — **edit session action apply**; sole **live store** writer for geometry; includes **boundary split** sub-step (D10)
- [x] 3.2 Reuse pair helpers from `NoteEditFocus` / movement utils (extract, do not duplicate LIFO)
- [x] 3.3 Wire existing invariant gates after apply (micro closure + macro if test harness commits)
- [x] 3.4 Native `test/test_apply_edit_session_actions/` — 144458, 152335, hide+shorten+restore combinations
- [x] 3.5 **`EditorSelection`** sync after apply (replace index-only **`finalReconstructAndSelect`** paths incrementally)
- [x] 3.6 After **`applyEditSessionActions`**, call **`track.invalidateCaches()`** so **`sessionPreviewRevision_`** refreshes NOTE_EDIT playback audition (loop-wrap-projection Tier 2). **Brownfield today:** `NoteMovementUtils` geometry paths already call this; pipeline must not drop it

## 4. Wire NOTE_EDIT geometry (Phase 4)

- [x] 4.1 Replace `moveNoteWithOverlapHandling` body with pipeline
- [x] 4.2 Replace `changeLengthWithOverlapHandling` body with pipeline
- [x] 4.3 Replace pitch overlap path in `applyPitchChange` with pipeline
- [ ] 4.3a Wire Add/Delete geometry through pipeline (selected new note → causing; delete causing → restore via rebuild)
- [x] 4.4 Retire restore-first calls, adjacent merge, **`allowSharedEndCoexistence`**, **`movingNoteRange`** (restore-first + findOverlaps on move/length/pitch overlap; adjacent merge retained on pitch until follow-up)
- [x] 4.5 Remove **`overlapNotes`** as commit/filter authority; no persistent constraint store for geometry (D5, D15). Scratch member retained for legacy pitch-lane restore until pitch path is fully pipeline-only.
- [x] 4.5a Replace **`buildPreCommitEditPasses`** / **`buildPreCommitOverlapEditPasses`** with **transaction baseline compared to final live store** → one **`noteEditPass` batch**
- [x] 4.5b Update **`filterSelectableDisplayNotes`** / edit closure to derive hidden from live store compared to baseline, not **`overlapNotes`**
- [x] 4.6 `pio test -e native` full suite

## 5. HITL + archive (Phase 5)

- [ ] 5.1 Capture **base + 2× overdub** loop fixture; HITL per-interaction presets (D14)
- [ ] 5.2 HITL regression matrix (move, length, pitch, wrap, 144458 home move). **Interim smoke:** `edit_minimal` preset PASS (base seed + move/length/add/delete) — not a substitute for full matrix (D14)
- [ ] 5.2 Update NOTE_WRAPPING_LOGIC / modification-session guide pointers
- [ ] 5.3 `/opsx:archive` → `openspec/specs/edit-session-action-geometry/`

## Scenario matrix → test map

| Scenario | Interaction test | Builder test | Apply test | HITL |
|----------|------------------|--------------|---------------|------|
| Mover swallows short note | CompleteCover | Hide | pair removed | captured loop |
| Head overlap, note-on safe | OverlapNoteOff | Shorten | off → mover on − 1 | 231310 |
| Mover hits target note-on | OverlapNoteOn | Hide | pair removed | native |
| Hide wins over shorten | OverlapNoteOn + Off | complete hide precedence | — | native |
| Combined shortens below minimum length | 2× OverlapNoteOff | minimum note edit length hide | — | native |
| Move away → restore | (pair omitted) | Restore | pair reinserted | 144458 |
| Add note on overlap | OverlapNoteOn/Off | Hide/Shorten | pair removed/shortened | native |
| Delete causing note | (pair omitted) | Restore | hidden target restored | native |
| Wrap target pre-analyze | D20 Edit projection | same table | linear off | 152335 |
| Same-pitch multi-select lengthen | — | deferred (selected-to-selected overlap) | — | — |
| Selected-to-selected skip | skip all | no interaction | — | native |
| Selection derive not store | `isSelectedNote` | no extra fields | — | native |
| Multi-causing combine | Group-by-target regroup | resolve | order-independent | 144458 |
| Boundary touch | BoundaryTouch | Ignore + split | off at on−1 | native |
| Length into overlap | OverlapNoteOff / On | same table | same as move | edit preset |
| Pitch lane | OverlapNoteOn / Off / CompleteCover | same table | P0 restore | 144458 |
| Pitch change source lane restore | D21 | Restore | P0 targets | 144458 |
| Same-tick on/off boundary | BoundaryTouch | split | off → on−1 | native |
| Geometry parity fixtures | all types | — | — | native |
| Edit projection wrap parity | D20 | — | linear length | native |
| Poly shorten cross-pitch | — | deferred | — | — |

## Operator checklist (post wire)

1. Flash `teensy41-capture-serial`
2. `pio test -e native`
3. HITL note edit presets per [`HITL_TEST_SCENARIOS.md`](../../docs/Guides/HITL_TEST_SCENARIOS.md)
4. Serial: no `NOTE_EDIT macro commit: non-canonical store` during edit sweeps
