# NoteEditFocus translation-unit extraction

**Kind:** refinement  
**Branch:** `refactor/noteditfocus` (from `dev`)  
**Parent context:** [editmanager_translation_unit_extraction_refinement.md](editmanager_translation_unit_extraction_refinement.md) (EditManager split — **in progress**), [note_edit_control_surface_split_refinement.md](note_edit_control_surface_split_refinement.md) (shipped)  
**Naming authority:** [NAMING.md](../00-authority/NAMING.md)  
**Workflow:** [Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc)  
**Pattern reference:** [loop_translation_unit_extraction_refinement.md](loop_translation_unit_extraction_refinement.md) (shipped on `dev`)

---

## One-line goal

Shrink [`src/NoteEditFocus.cpp`](../../src/NoteEditFocus.cpp) (~1554 LOC) by moving free functions into **domain-owned modules** under [`src/EditManager/`](../../src/EditManager/), colocated with [`NoteEditFocusRebuild.cpp`](../../src/EditManager/NoteEditFocusRebuild.cpp), behavior-preserving, **no ownership or lifecycle changes**.

**Success criteria:** domain ownership primary. LOC bands are review heuristics only ([Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc)).

---

## Two axes (implementation ≠ architecture)

| Axis | Optimizes for |
|------|---------------|
| **Implementation phases** | Reviewability, bisect, incremental PRs |
| **Destination architecture** | Focus / overlap / baseline / pre-commit / display projection cohesion |

**Guiding principles:**

1. Phases are extraction slices; **file names are architectural responsibilities**.
2. **Extend `src/EditManager/`** — do not create a parallel top-level folder; `NoteEditFocusRebuild` already lives here.
3. **EditManager orchestration stays in EditManager** — `rebuildNoteEditFocus*` / `ensureNoteEditFocus*` remain in `NoteEditFocusRebuild.cpp`.
4. **No new domain nouns** without user approval ([NAMING.md](../00-authority/NAMING.md)).

---

## Baseline (2026-08-06)

| Artifact | LOC / status |
|----------|----------------|
| [`src/NoteEditFocus.cpp`](../../src/NoteEditFocus.cpp) | **~1554** (root TU — orphan next to EditManager/) |
| [`include/NoteEditFocus.h`](../../include/NoteEditFocus.h) | **~372** — types + inline select-navigation templates (stay) |
| [`src/EditManager/NoteEditFocusRebuild.cpp`](../../src/EditManager/NoteEditFocusRebuild.cpp) | **~238** — **already extracted** EditManager orchestration |
| [`include/EditManagerInternal.h`](../../include/EditManagerInternal.h) | exists — do not duplicate; add `NoteEditFocusInternal.h` for focus-only cold helpers |

### Target end state

| Artifact | Target |
|----------|--------|
| Root `NoteEditFocus.cpp` | **removed** — all bodies under `src/EditManager/NoteEditFocus*.cpp` |
| Four domain TUs | **~1200–1400** moved (see modules below) |
| `include/NoteEditFocus.h` | **stable public API** — declarations unchanged unless Phase 0 naming pass |
| Ownership / transitions | **unchanged** |

---

## Phase 0 — prior splits inventory ✅ (planning)

| Already shipped in this domain | Location | This plan |
|--------------------------------|----------|-----------|
| `rebuildNoteEditFocusAtSelect` / `ForDisplayNote` / `FromStore` callers | `NoteEditFocusRebuild.cpp` | **Keep** — orchestration only |
| `ensureNoteEditFocusForLiveEdit` | `NoteEditFocusRebuild.cpp` | **Keep** |
| `syncNoteEditFocusLastFromSessionStore` | `NoteEditFocusRebuild.cpp` | **Keep** |
| `materializedLoopEventsForNoteEditFocus` | `EditManager` (root or sibling TU) | **Do not move** in this plan |
| Geometry resolution | `NoteEditFocusResolver.cpp` / `NoteGeometryResolver` (EditManager Phase 7) | **Out of scope** — separate plan phase |

| Sibling pattern to copy | Detail |
|-------------------------|--------|
| Loop split | `LoopInternal.h` + `src/Loop/*.cpp` + `LoopCaptureTestDeps.cpp` |
| EditManager split | `EditManagerInternal.h` + `EDIT_MANAGER_IMPL_MEM` / `NOTE_EDIT_MEM` on moved bodies |
| Native tests | `#include "../../src/NoteEditFocus.cpp"` in 8 suites — update paths + optional `NoteEditFocusTestDeps.cpp` |

---

## Phase 0 — naming pass (mandatory before Phase 2)

| Current | Proposed | Rationale | Phase |
|---------|----------|-----------|-------|
| `filterSelectableDisplayNotes` (alias) | Keep alias; add `[[deprecated]]` comment in header pointing to `projectNoteEditDisplayNotes` | Surfaces canonical display projection name | 7 |
| `buildPreCommitOverlapEditPasses` | Keep (legacy stub); document in module header as **deprecated path** only | Tests + nullptr fallback | 6 |
| File-static pair finders (`findLinearOff*`, …) | Move to `NoteEditFocusInternal.h` declarations | Reveal module boundary; not public API growth | 2 |
| `makeNoteEditRow` | Keep name (action+scope OK) or inline into pre-commit module only | Small helper | 6 |

No renames that change `#CAP` log tokens or HITL matchers.

---

## Phase 0 — cross-domain placement table

| Symbol / area | Owner module | Rationale | Misleading? |
|---------------|--------------|-----------|-------------|
| `NoteEditFocus` struct | `include/NoteEditFocus.h` | Live edit RAM on `EditSession.focus` | — |
| `rebuildNoteEditFocusFromStore` | `NoteEditFocusState.cpp` | Focus state from loop MIDI | Not `EditManager` orchestration |
| `projectNoteEditDisplayNotes` | `NoteEditFocusDisplayProjection.cpp` | Display overlay — not `DisplayManager` paint | Name OK |
| `buildPreCommitEditPasses` | `NoteEditFocusPreCommit.cpp` | Emits **editPass** rows — not `Loop::saveNoteEditPass` | — |
| `resolveOverlapNotesForPreCommit` | `NoteEditFocusOverlap.cpp` | Session store mutation before commit | Not Loop capture |
| `findLinearNoteSpanForNoteId` | `NoteEditFocusLinearSpan.cpp` | MIDI pair resolution — shared helper | Could be `NoteUtils` later — **stay in focus** for this plan |
| `EditManager::rebuildNoteEditFocus*` | `NoteEditFocusRebuild.cpp` | **Already placed** — session owner calls focus primitives | — |

---

## Primary architectural modules

```text
src/EditManager/NoteEditFocusLinearSpan.cpp    MIDI linear pair / span resolution
src/EditManager/NoteEditFocusBaseline.cpp      baselineMap, baseline resolution, closure
src/EditManager/NoteEditFocusOverlap.cpp         overlapNotes scratch, pre-commit materialize
src/EditManager/NoteEditFocusState.cpp           focus apply + pending + rebuild from store
src/EditManager/NoteEditFocusPreCommit.cpp       editPass row emission
src/EditManager/NoteEditFocusDisplayProjection.cpp  projectNoteEditDisplayNotes
include/NoteEditFocusInternal.h                  cold helper decls shared across focus TUs
src/EditManager/NoteEditFocusRebuild.cpp          (existing) EditManager orchestration
```

| Module | Architectural question | Est. LOC |
|--------|------------------------|----------|
| **`NoteEditFocusLinearSpan.cpp`** | How do we find linear note-on/off pairs and sync focus.last from the session store? | ~400 |
| **`NoteEditFocusBaseline.cpp`** | How does baselineMap represent committed vs live geometry? | ~350 |
| **`NoteEditFocusOverlap.cpp`** | How are overlap notes hidden, shortened, and restored around commit? | ~300 |
| **`NoteEditFocusState.cpp`** | How does the moving note apply length/move/pitch and rebuild from loop events? | ~200 |
| **`NoteEditFocusPreCommit.cpp`** | How are pre-commit **editPass** rows built from focus + live store? | ~200 |
| **`NoteEditFocusDisplayProjection.cpp`** | How are **DisplayNote** lists projected for NOTE_EDIT UI? | ~200 |

**Cohesion over file size:** prefer ~300–500 LOC domain files over six ~150 LOC slices.

---

## Rules (every phase)

1. **Behavior-preserving** — no changes to pre-commit semantics, overlap restore, or display projection output.
2. **Architecture checkpoint** — ownership **NO**, transition **NO**.
3. **Per phase:** commit + `pio run -e teensy41-capture-serial`.
4. **Batch (low/med phases):** single `pio test -e native` at end; run `test_note_edit_focus`, `test_edit_apply` if overlap/pre-commit touched.
5. **Preserve `NOTE_EDIT_MEM`** on moved hot-path symbols ([`NoteEditMem.h`](../../include/Utils/NoteEditMem.h)).
6. **Templates:** keep explicit instantiations for `InternalHeapFirstAllocator` and `ExternalMemoryFirstAllocator` in the same TU as definitions.

### Protected paths (extra scrutiny)

| Area | Risk |
|------|------|
| `buildPreCommitEditPasses` / `buildPreCommitBaselineLiveDiffOverlapPasses` | **high** — edit pass emission |
| `resolveOverlapNotesForPreCommit` | **medium** — session store mutation |
| `projectNoteEditDisplayNotes` | **medium** — piano-roll display |
| Linear span helpers | **low** — pure resolution |

---

## PR stack

`0 → 2 → 3 → 4 → 5 → 6 → 7 → 10`

Phase **1** skipped (no separate coordinator file — root TU deleted at Phase 10).

---

## Phase 0 — `NoteEditFocusInternal` scaffold

**Risk:** low

**Creates:**

| File | Role |
|------|------|
| [`include/NoteEditFocusInternal.h`](../../include/NoteEditFocusInternal.h) | Declarations for file-static helpers moved out of root TU |
| [`test/test_support/NoteEditFocusTestDeps.cpp`](../../test/test_support/NoteEditFocusTestDeps.cpp) | Optional aggregate `#include` for native tests (mirror `LoopCaptureTestDeps.cpp`) |

**PR title:** `refactor(noteditfocus): Phase 0 NoteEditFocusInternal scaffold`

---

## Phase 2 — linear span → `NoteEditFocusLinearSpan.cpp`

**Risk:** low

| Symbol | Role |
|--------|------|
| `findLinearOffForNoteOnLifo` / `findPlausibleOffForNoteOn` / `findLinearOffForNoteId` | Off resolution |
| `findLinearNoteSpanForNoteId` | Span resolution |
| `findNoteOnAtChannelPitchTick` / `findNoteOnForNoteId*` / `findNoteOnForMovingNoteEdit` | Note-on lookup |
| `syncNoteEditFocusLinearFromSessionStore` | focus.last sync |
| `stampNoteIdsOntoPairedNoteOffs` | Session-open pairing |
| `eraseNoteEndpoint` / `eraseNotePairAtBaseline` / `findNoteOnAt` / `findNoteOffForOn` / `insertNotePair` | Store pair edit helpers |
| `eventMatchesNoteEndpoint` | Endpoint match |

**PR title:** `refactor(noteditfocus): Phase 2 linear span → NoteEditFocusLinearSpan`

---

## Phase 3 — baseline → `NoteEditFocusBaseline.cpp`

**Risk:** low

| Symbol | Role |
|--------|------|
| `baselineFromDisplayNote` / `baselineForDisplayNote` / `findBaselineNoteIdForDisplay` | Baseline lookup |
| `projectCanonicalBaselineForEdit` / `linearBaselineForOverlapRestore` | Canonical vs linear |
| `resolveLinearNoteSpanForOverlap` | Overlap span gate |
| `isPlausibleStorageSpan` / `isInflatedDisplaySpan` / `isDisplayWrappedBaseline` | Span validation |
| `populateBaselineMapForEditClosure` / `buildEditClosureNoteIds` | Closure population |
| `linearStorageSpansOverlapLocal` / `readLiveBaselineForOverlapDiff` | Diff helpers |

**PR title:** `refactor(noteditfocus): Phase 3 baseline → NoteEditFocusBaseline`

---

## Phase 4 — overlap scratch → `NoteEditFocusOverlap.cpp`

**Risk:** medium

| Symbol | Role |
|--------|------|
| `findOverlapNoteEntry` / `hasChangedOverlapNote` / `recordChangedOverlapNote` / `forgetChangedOverlapNote` | Overlap scratch |
| `applyCommittedOverlapUpdateToFocus` / `clearCommittedOverlapDeleteIdsFromFocus` | Post-commit focus |
| `evictOverlapScratchForSelectedNote` / `isMovingNoteOverlapScratchEntry` | Driver boundary |
| `materializeShortenedOverlap` / `resolveOverlapNotesForPreCommit` / `pruneOverlapNotesBeforePreCommit` | Pre-commit store |
| `recordBaselinePitchLaneRestoreOverlapCandidates` / `canApplySimplePitchChange` | Pitch lane |
| `buildPreCommitOverlapEditPasses` (legacy stub) | Test fallback |
| `overlapNoteEffectiveEnd` | Overlap end tick |

**PR title:** `refactor(noteditfocus): Phase 4 overlap → NoteEditFocusOverlap`

---

## Phase 5 — focus state apply → `NoteEditFocusState.cpp`

**Risk:** low

| Symbol | Role |
|--------|------|
| `rebuildNoteEditFocusFromStore` | Focus rebuild from loop MIDI |
| `noteEditFocusApplyLengthEnd` / `ApplyMoveEnd` / `ApplyPitch` | Live geometry apply |
| `noteEditFocusHasPendingLengthChange` / `HasPendingBaselineMapDiff` / `HasPendingCommit` | Pending gates |
| `noteEditDisplayCacheFingerprint` / `movingNoteRangeDisplayEnd` / `isInnerOverlapNoteInMovingNoteRange` | Display fingerprint |
| `baselineMapPitchLaneNeedsRestore` | Pitch lane restore |

**PR title:** `refactor(noteditfocus): Phase 5 focus state → NoteEditFocusState`

---

## Phase 6 — pre-commit rows → `NoteEditFocusPreCommit.cpp`

**Risk:** high

| Symbol | Role |
|--------|------|
| `makeNoteEditRow` | Row factory |
| `buildPreCommitBaselineLiveDiffOverlapPasses` | Overlap diff rows |
| `buildPreCommitEditPasses` | Moving note + overlap orchestration |

### Architecture gate (required)

| Question | Answer |
|----------|--------|
| Owner | `NoteEditFocus` pre-commit emission; `EditManager` commit orchestration unchanged |
| Primary invariant | Pre-commit rows match baseline vs live store + focus.last vs commitBaseline |
| Ownership / transition change? | NO |

**PR title:** `refactor(noteditfocus): Phase 6 pre-commit → NoteEditFocusPreCommit`

---

## Phase 7 — display projection → `NoteEditFocusDisplayProjection.cpp`

**Risk:** medium

| Symbol | Role |
|--------|------|
| `collectProjectionParticipantNoteIds` | Participant set |
| `projectNoteEditDisplayNotes` | Display overlay |
| `displayNoteOrderBefore` / `sortNoteIdList` (anonymous) | Sort helpers |

**PR title:** `refactor(noteditfocus): Phase 7 display projection → NoteEditFocusDisplayProjection`

---

## Phase 10 — remove root TU + test deps

**Risk:** low

- Delete empty `src/NoteEditFocus.cpp`
- Point native suites at `NoteEditFocusTestDeps.cpp` or individual `src/EditManager/NoteEditFocus*.cpp` includes
- Update [unified_interval_projection_phase2_edit_projection_handoff.md](unified_interval_projection_phase2_edit_projection_handoff.md) paths if needed

**PR title:** `refactor(noteditfocus): Phase 10 remove root NoteEditFocus.cpp`

---

## Native test include policy

Suites that `#include` root `NoteEditFocus.cpp` today:

- `test_note_edit_focus`, `test_edit_apply`, `test_edit_session_action_builder`
- `test_note_edit_session_undo`, `test_note_edit_track_switch`, `test_slot_switch_edit_sessions`
- `test_note_edit_fader_feedback`, `test_apply_edit_session_actions`

Many also require `IntervalProjection.cpp` — keep that rule in test deps.

---

## Verification

| Gate | When |
|------|------|
| `pio run -e teensy41-capture-serial` | **Every phase** |
| `pio test -e native` | Batch end (phases 2–7 low/med) |
| `pio test -e native -f test_note_edit_focus` | After phases 4, 6, 7 |
| [mechanical_split_hardware_smoke_checklist.md](mechanical_split_hardware_smoke_checklist.md) | First session on device |
| HITL edit baseline | After Phase 6 if pre-commit touched |

---

## Per-phase checklist

```markdown
## Architecture gate
- Owner: NoteEditFocus / EditManager (unchanged)
- Ownership / transition change: NO
- Risk: …

## Verification
- [ ] pio run -e teensy41-capture-serial (this phase)
- [ ] LOC recorded per TU
- [ ] pio test -e native (batch end)
```
