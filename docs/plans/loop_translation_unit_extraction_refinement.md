# Loop translation-unit extraction

**Kind:** refinement  
**Branch:** `refactor/loop` (from `dev`)  
**Parent context:** [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md), [runtime_process_building_blocks_overview.md](runtime_process_building_blocks_overview.md), [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)  
**Naming authority:** [NAMING.md](../00-authority/NAMING.md)  
**Pattern reference:** [track_translation_unit_extraction_refinement.md](track_translation_unit_extraction_refinement.md) (shipped PR #13), [editmanager_translation_unit_extraction_refinement.md](editmanager_translation_unit_extraction_refinement.md), [storagemanager_translation_unit_extraction_refinement.md](storagemanager_translation_unit_extraction_refinement.md)

---

## One-line goal

Shrink [`src/Loop.cpp`](../../src/Loop.cpp) from a ~1644-line monolith into a thin per-slot coordinator (~150–250 LOC) by moving method bodies into **cohesive destination modules** under [`src/Loop/`](../../src/Loop/), using **small implementation phases** (one PR each), behavior-preserving, **no ownership or lifecycle changes**.

---

## Two axes (implementation ≠ architecture)

| Axis | Optimizes for | What it looks like |
|------|---------------|-------------------|
| **Implementation phases** | Reviewability, risk reduction, incremental change | Small PRs; each phase moves a slice of methods |
| **Destination architecture** | Cohesion, discoverability, long-term navigation | Modules answer one architectural question each |

**Guiding principles:**

1. Phases are extraction slices; **file names are architectural responsibilities** — do not let phase boundaries become permanent module boundaries.
2. **Do not pre-commit to a catch-all module** (`*Maintenance*`, `*Management*`, `*Utilities*`, `*Helpers*`, `*Misc*`). Those names mean the true process owner is not yet identified ([NAMING.md](../00-authority/NAMING.md)).
3. **Extend existing responsibilities before introducing new translation units** — grow cohesive modules (`LoopCapture`, `LoopMaterialization`, …) rather than creating new files when a symbol fits an established process. Reusable across future extraction plans in this repo.
4. **Phase 10 is evidence-driven** — complete primary extractions first; review what remains in `Loop.cpp`; place by architectural ownership; introduce a fifth TU only if a genuine process emerges that does not fit the modules below.

---

## Primary architectural modules

The four destination modules below are the **initial** architecture — expected to hold most of `Loop.cpp` after extraction. Phase 10 may refine placement or add a process-named TU if evidence requires it.

```text
src/Loop.cpp                      thin coordinator (~150–250 LOC)
src/Loop/LoopCapture.cpp          capture lifecycle (~400–500 LOC)
src/Loop/LoopMaterialization.cpp  playable / editable views (~400–450 LOC)
src/Loop/LoopEditPasses.cpp       edit-pass rows + snapshots (~280 LOC)
src/Loop/LoopVisualCache.cpp      display note cache (~200 LOC)
include/LoopInternal.h            shared cold helpers / declarations
```

| Module | Architectural question |
|--------|------------------------|
| **`Loop.cpp`** | What is the public slot API, cross-domain invalidation routing, and slot-wide counters? |
| **`LoopCapture.cpp`** | How does capture progress from live events to committed passes? |
| **`LoopMaterialization.cpp`** | How are playable and editable views produced? |
| **`LoopEditPasses.cpp`** | How are edit passes created, replaced, enabled, and restored? |
| **`LoopVisualCache.cpp`** | How is derived display data maintained? |

**Cohesion over file size:** `LoopCapture.cpp` at ~420 LOC is preferable to three ~150 LOC capture slices.

### `Loop.cpp` coordinator

These symbols are expected to remain in root `Loop.cpp` because they **route across domains** or own **slot-wide** state — not because no other module was found.

| Symbol | Ownership |
|--------|-----------|
| `invalidateCaches` | Cross-domain coordination |
| `invalidatePlaybackCaches` | Cross-domain coordination |
| `markPassDerivedStale` | Cross-domain coordination |
| `allocateNoteId` | Slot-wide ID counter |
| `resetPassTimeline` | Slot-wide reset |
| `reclaimUnreferencedDisabledPasses` | Cross-domain delegation |
| `hasData()` (inline) | Public slot API composition |

### Phase 10 — ownership guide (pre-extraction analysis)

Current analysis (2026-08-06) suggests the symbols below fit the primary modules. **Confirm or revise at Phase 10** after inventorying what remains in `Loop.cpp`.

| Symbol | Destination | Ownership |
|--------|-------------|-----------|
| `hasCommittedPasses`, `findLastCommittedEventTick`, `activeCapturePassCount` | `LoopCapture.cpp` | Capture pass presence |
| `reconcileLoopLengthWithCommittedPasses` | `LoopCapture.cpp` | Capture-derived loop geometry |
| `setCapturePassState` | `LoopCapture.cpp` | Capture pass lifecycle |
| `freeActiveCapturePassChunks` | `LoopCapture.cpp` | Capture pass lifecycle |
| `reclaimDisabledCapturePass`, `reclaimUnreferencedDisabledCapturePasses` | `LoopCapture.cpp` | Capture pass lifecycle |
| `assignMissingNoteIdsInStore` | `LoopCapture.cpp` | Capture ownership |
| `liveEventCount` | `LoopCapture.cpp` | Capture / live content |
| `reclaimUnreferencedDisabledEditPasses` | `LoopEditPasses.cpp` | Edit-pass lifecycle |
| `assignMissingNoteIds` (vector overloads) | `LoopEditPasses.cpp` | Edit-pass lifecycle |
| `invalidateCaches`, `invalidatePlaybackCaches`, `markPassDerivedStale` | `Loop.cpp` | Cross-domain coordination |
| `allocateNoteId` | `Loop.cpp` | Slot-wide ID counter |
| `resetPassTimeline` | `Loop.cpp` | Slot-wide reset |
| `reclaimUnreferencedDisabledPasses` | `Loop.cpp` | Cross-domain delegation |

**Phase 10 process** (ownership is the decision criterion):

```text
Remaining responsibilities (after Phases 2–9)
        │
        ▼
Review ownership (rg call sites, confirm process fit)
        │
        ▼
Place into existing architectural modules
        │
        ▼
Only introduce a new translation unit if a genuine process emerges
```

**Fifth TU:** no additional translation unit is **currently expected**. If Phase 10 inventory finds a cohesive process that does not fit the four modules, name it for that process ([NAMING.md](../00-authority/NAMING.md)) and get user approval for any new domain noun. Do **not** default to `*Maintenance*` / `*Utilities*`.

**Success criterion:** every TU represents a clear process. A contributor locates behavior by asking *how capture works*, *how views materialize*, *how edit passes change*, or *how the visual cache rebuilds*.

### Module dependency (read direction)

```text
                    Loop.cpp (coordinator)
                   /    |    \         \
                  /     |     \         \
         LoopCapture   |   LoopEditPasses  invalidate* routers
              |        |        |
              v        v        v
      LoopMaterialization ←── gather / midiEvents
              |
              v
      LoopVisualCache  (rebuildVisualCache* → gatherCommittedEventsInWindow)
```

Capture commit and edit-pass mutation call `markPassDerivedStale` (coordinator) → materialized + visual stale. Visual rebuild **calls into** Materialization; it does not duplicate gather logic.

### Naming note — `LoopPasses.cpp` already exists

[`src/LoopPasses.cpp`](../../src/LoopPasses.cpp) owns **`LoopPasses::materialize`** (struct-level merge/replay). Optional Phase 11 colocate under `src/Loop/` — unchanged responsibility.

---

## Baseline (2026-08-06)

| Artifact | LOC / status |
|----------|----------------|
| [`src/Loop.cpp`](../../src/Loop.cpp) | **~1644** (root TU) |
| [`include/Loop.h`](../../include/Loop.h) | **~231** |
| `LoopInternal.h` | **does not exist yet** |
| [`src/LoopPasses.cpp`](../../src/LoopPasses.cpp) | **~154** — `LoopPasses::materialize` |
| [`src/LoopEventStore.cpp`](../../src/LoopEventStore.cpp) | **~960** |
| [`src/LoopPool.cpp`](../../src/LoopPool.cpp) | **~133** |

### Target end state

| Artifact | Target |
|----------|--------|
| `Loop.cpp` | **~150–250** — coordinator after Phase 10 |
| Four primary destination modules | **~1200** moved in Phases 2–9 |
| Phase 10 remainder | Evidence-driven placement per **Phase 10 ownership guide**; fifth TU not currently expected |
| Ownership / transitions | **unchanged** |

---

## Rules (every phase)

1. **Behavior-preserving** — no changes to `commitCapturePass`, seal/finalize wrap-window, gather policy (DEC-016), or visual-cache semantics.
2. **Architecture checkpoint** — ownership change **NO**, state transition change **NO** ([architecture-checkpoint-bugfix](../../.cursor/rules/architecture-checkpoint-bugfix.mdc)).
3. **Pre-implementation review** — trace symbols with `rg` ([Plan-Pre-Implementation-Review](../../.cursor/rules/Plan-Pre-Implementation-Review.mdc)).
4. **Verification gate** — `pio test -e native`; `pio run -e teensy41-capture-serial`; HITL when capture stop / gather / playback touched.
5. **One phase per session / PR**.
6. **Append to destination file** — later phases add to the same architectural TU (e.g. Phases 7 and 8 both grow `LoopCapture.cpp`).
7. **State stays on `Loop`** — method bodies only; no new top-level manager classes without user approval.
8. **No catch-all destination** — do not create `*Maintenance*` / `*Utilities*` TUs in Phases 0–9; extend primary modules first.
9. **Member access** — `Loop::method` out-of-line; preserve `LOOP_COLD_MEM`.

### Protected paths (extra scrutiny)

| Area | Owner methods |
|------|----------------|
| Capture seal / commit | `sealCapture`, `commitCapturePass`, `commitPendingCapturePass`, `discardPendingCapturePass` |
| Stop wrap window | `finalizeCaptureWrapWindowAtStop`, `commitStopFinalizeFromStore` |
| Hot gather | `gatherCommittedEvents`, `gatherCommittedEventsInWindow`, `gatherCommittedEventsWithCapture` |
| Live capture append | `appendCaptureEvent`, `ensureCaptureEventsSorted` |
| Undo snapshot | `sharePassesSnapshot`, `restorePassesSnapshot`, `adoptPersistedSnapshot` |

Read [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) before Phases 2 (gather slice), 5–6 (capture).

---

## Implementation phases → destination map

**Primary extractions first** — leftover methods stay in `Loop.cpp` until Phase 10.

```text
Phase   Implementation slice                    → Destination file
────────────────────────────────────────────────────────────────────
 0      LoopInternal scaffold                    → LoopInternal.h
 2      Materialize + merge active capture       → LoopMaterialization.cpp
 3      Edit pass mutation                       → LoopEditPasses.cpp
 4      Gather committed events                  → LoopMaterialization.cpp   ← protected
 5      Snapshot adopt / restore                 → LoopEditPasses.cpp
 7      Capture live input                       → LoopCapture.cpp
 8      Capture stop / seal / commit             → LoopCapture.cpp           ← protected
 9      Visual cache rebuild                     → LoopVisualCache.cpp
10      Remaining responsibilities               → Phase 10 ownership guide (+ coordinator in Loop.cpp)
        ────────────────────────────────────────────────────────────────────
        Loop.cpp  1644 → ~150–250 after Phase 10
```

**Phases 1 and 6 (old plan)** — pass presence and pass reclaim — **not separate early extractions**. They remain in `Loop.cpp` through Phase 9 and are assigned in **Phase 10**.

**PR stack:** **0 → 2 → 3 → 4 → 5 → 7 → 8 → 9 → 10**. Phase **4** before **9** (visual cache calls gather). Phases **7 → 8** sequential.

**Milestones:** after Phase **4** root ~**900 LOC**; after Phase **8** root ~**500 LOC** (still includes Phase 10 candidates); after Phase **10** root in target band.

---

## Phase 0 — `LoopInternal` scaffold ✅

**Creates:**

| File | Role |
|------|------|
| [`include/Utils/LoopMem.h`](../../include/Utils/LoopMem.h) | `LOOP_COLD_MEM` (flash on Teensy) — shared by `Loop.cpp` and `src/Loop/*` |
| [`include/LoopInternal.h`](../../include/LoopInternal.h) | Declarations for cold helpers extracted from root `Loop.cpp` |
| [`src/Loop/LoopInternalColdHelpers.cpp`](../../src/Loop/LoopInternalColdHelpers.cpp) | Capture dedup/preview, snapshot deep-clone, edit-pass heap gate, stop telemetry |

Cold helpers for later phases (merge, gather, visual) remain in `Loop.cpp` until Phases 2–9.

**PR title:** `refactor(loop): Phase 0 LoopInternal scaffold`

---

## Phase 2 — materialize + merge → `LoopMaterialization.cpp` ✅

| Symbol | Role |
|--------|------|
| `mergeSortedLoopCaptureLayers` / `mergeActiveCapturePassesInto` | Merge helpers |
| `mergeActiveCapturePasses` | Active capture pass replay |
| `materializeEditViewFromPasses` / `rematerializeEditView` | Passes → materialized store |
| `midiEvents` / `midiEvents` const | Materialized view accessors |
| `mergeMaterializedPassesWithCapture` | Gather alias with capture layer |
| `discardPassesMaterializedCache` / `tryDiscardPassesMaterializedCache` | Materialized store cache drop |

**PR title:** `refactor(loop): Phase 2 materialize and merge → LoopMaterialization`

---

## Phase 3 — edit pass mutation → `LoopEditPasses.cpp` ✅

| Symbol | Role |
|--------|------|
| `saveNoteEditPass` / `replaceNoteEditPass` | Edit row append / replace |
| `disableEditPasses` / `enableEditPasses` | Edit pass state toggles |
| `materializeExcludingEditPassIds` | Scoped materialize for undo preview |
| `deepClone*`, `estimatedEditPassBytes`, `canHeapAdmitEditPass` | Helpers (if not in Phase 0) |

**PR title:** `refactor(loop): Phase 3 edit pass mutation → LoopEditPasses`

---

## Phase 4 — gather → `LoopMaterialization.cpp` (protected) ✅

Appends to **`LoopMaterialization.cpp`**.

| Symbol | Role |
|--------|------|
| `mergeCaptureStoreIntoMaterializedEvents` | Capture overlay on gathered view |
| `hasActiveEditPasses` / `collectActiveCommittedChunkLists` / `sortMidiEventsByTick` | Gather helpers |
| `gatherCommittedEvents` (+ overload) | Full-loop gather (DEC-016) |
| `gatherCommittedEventsInWindow` (+ overload, `WithCapture`) | Windowed gather |
| `shouldAvoidFullVisualRebuild` | Long-loop policy gate |
| `gatherCommittedEventsForDerivedView` / `gatherCommittedEventsWithCapture` | Playback/display aliases |

### Architecture gate (required)

| Question | Answer |
|----------|--------|
| Owner module | `Loop` gather policy; `LoopPasses::materialize`; `LoopEventStore` chunk refs |
| Primary invariant | Full-loop sync materialize avoided on long loops after LoadLoopJob Commit |
| Ownership / transition change? | NO |

**PR title:** `refactor(loop): Phase 4 gather → LoopMaterialization`

---

## Phase 5 — snapshots → `LoopEditPasses.cpp` ✅

| Symbol | Role |
|--------|------|
| `sharePassesSnapshot` | O(1) undo snapshot ref |
| `adoptPersistedSnapshot` / `restorePassesSnapshot` | SD load / undo restore (calls materialized/visual invalidation hooks) |

**PR title:** `refactor(loop): Phase 5 snapshot lifecycle → LoopEditPasses`

---

## Phase 7 — capture live → `LoopCapture.cpp` ✅

**Creates** [`src/Loop/LoopCapture.cpp`](../../src/Loop/LoopCapture.cpp).

| Symbol | Role |
|--------|------|
| `beginCapture` / `discardCapture` / `clearCaptureOnNewPass` | Capture phase entry/exit |
| `appendCaptureEvent` / `removeOpenCaptureNoteOn` | Live capture store mutation |
| `captureHasNoteOffAfter` | Open-tail probe |
| `liveEventCount` | Count hint (includes live capture; also materializes passes — optional follow-up) |
| `captureActive` / `ensureCaptureEventsSorted` | Capture state |
| `shiftActiveCapturePassTicks` | Origin snap after record stop |
| Capture dedup / preview helpers | From anonymous namespace |

**PR title:** `refactor(loop): Phase 7 capture live → LoopCapture`

---

## Phase 8 — capture stop / seal / commit → `LoopCapture.cpp` (protected) ✅

Appends to **`LoopCapture.cpp`**.

| Symbol | Role |
|--------|------|
| `finalizeCaptureWrapWindowAtStop` | Wrap-window finalize on store |
| `sealCapture` | Chunk detach + pending pass |
| `commitCapturePass` | Seal + publish orchestration |
| `commitPendingCapturePass` / `discardPendingCapturePass` | Pending pass publish/discard |
| `commitStopFinalizeFromStore` / `seedRecordPassFromStore` | Record-stop side paths |
| Stop telemetry helpers | From anonymous namespace |

### Architecture gate (required)

| Question | Answer |
|----------|--------|
| Owner module | `Loop::commitCapturePass` seals; `Track` orchestrates stop |
| Primary invariant | Stop path uses wrap-window finalize only — no full validate on hot path |
| Ownership / transition change? | NO |

**PR title:** `refactor(loop): Phase 8 capture stop commit → LoopCapture`

---

## Phase 9 — visual cache → `LoopVisualCache.cpp`

| Symbol | Role |
|--------|------|
| `totalVisualBarsForLoop` / `markAllVisualCacheBarsDirty` / `findNextDirtyBar` | Visual bar bookkeeping |
| `removeDisplayNotesOverlappingBars` / `filterMidiEventsToTickWindow` | Slice helpers |
| `rebuildVisualCacheIdleSlice` / `rebuildVisualCacheFromPasses` | Idle + full visual rebuild |
| `ensureVisualCacheBuilt` | Sync ensure |
| `displayEventCountHint` | Uses `materializedEventCount_` + live capture size |
| `markDisplayCachesStale` / `invalidateDisplayCaches` | Display cache epochs (may call `invalidatePlaybackCaches`) |

**PR title:** `refactor(loop): Phase 9 visual cache → LoopVisualCache`

---

## Phase 10 — remaining responsibilities (evidence-driven)

**Goal:** After Phase 9, inventory what remains in root `Loop.cpp`, confirm ownership against the **Phase 10 ownership guide** (above), and extract into existing modules. **No catch-all module.**

### Process

1. **Inventory** — `rg` remaining `Loop::` method bodies in `src/Loop.cpp`.
2. **Review ownership** — for each symbol, confirm process fit (capture, materialization, edit passes, visual cache, or coordinator). Revise the ownership guide if evidence differs.
3. **Place** — extend the matching primary module (principle: extend before adding TUs).
4. **Fifth TU** — only if a cohesive process remains that does not fit the four modules.

### Suggested sub-slices (implementation convenience — adjust at Phase 10)

| Sub-slice | Destination | Symbols (current analysis) |
|-----------|-------------|----------------------------|
| **10a** | `LoopCapture.cpp` | Pass presence, reconcile length, `setCapturePassState`, capture reclaim, `assignMissingNoteIdsInStore`, `liveEventCount` |
| **10b** | `LoopEditPasses.cpp` | Edit reclaim, `assignMissingNoteIds` (vector overloads) |
| **10c** | `Loop.cpp` (stay) | `allocateNoteId`, invalidation routers, `resetPassTimeline`, `reclaimUnreferencedDisabledPasses` |

Implementation details (call paths, symbols already moved in earlier phases) belong in the **Phase 10 PR description**, not this architecture section.

After 10a–10c, root `Loop.cpp` target: **~150–250 LOC**.

**PR titles:** `refactor(loop): Phase 10a → LoopCapture`, `Phase 10b → LoopEditPasses`, `Phase 10c coordinator verify` (10c may be verify-only).

---

## Optional Phase 11 — colocate sibling TUs

| From | To |
|------|-----|
| `src/LoopPasses.cpp` | `src/Loop/LoopPasses.cpp` |
| `src/LoopPool.cpp` | `src/Loop/LoopPool.cpp` |

---

## Native test include policy

Many suites `#include "../../src/Loop.cpp"` directly. Update when symbols move (Track pattern). Full include remains valid until root TU shrinks.

---

## Per-phase checklist (copy into PR)

```markdown
## Architecture gate
- Owner: Loop (unchanged)
- Destination: src/Loop/<Module>.cpp (Phase 10: document per-symbol placement)
- Ownership / transition change: NO
- No catch-all *Maintenance* / *Utilities* module (Phases 0–9)

## Pre-implementation review
- [ ] `rg <symbol>` — all call sites listed
- [ ] Appending to existing destination TU (Phases 2–9)
- [ ] Phase 10: ownership confirmed per guide (revise guide in PR if evidence differs)

## Tests
- [ ] `pio test -e native`
- [ ] `pio run -e teensy41-capture-serial`
- [ ] HITL / manual: <phase-specific smoke>
```

---

## Related plans (do not merge scope)

| Plan | Relationship |
|------|----------------|
| [unified_capture_stop_driver_refinement.md](unified_capture_stop_driver_refinement.md) | Behavioral DRY on `Track` stop — **after** this hygiene |
| [memory_scalability_refactor_enhancement.md](memory_scalability_refactor_enhancement.md) | Pool / chunk policy — separate |
| [storagemanager_translation_unit_extraction_refinement.md](storagemanager_translation_unit_extraction_refinement.md) | `StorageLoopIo` split — parallel track |
