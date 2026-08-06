# StorageManager translation-unit extraction

**Kind:** refinement  
**Branch:** `refactor/storagemanager` (from `dev`)  
**Parent context:** [storage_session_state_refactor_handoff.md](storage_session_state_refactor_handoff.md), [deferred_storage_commit_parse_split_enhancement.md](deferred_storage_commit_parse_split_enhancement.md), firmware audit P1-3  
**Prerequisite merged:** PR #8 — loop-slot SD payload RAM cache (`hasLoopSlotPayloadOnSdInRam`)

---

## One-line goal

Shrink `src/StorageManager.cpp` from a ~4.4k-line monolith into a thin façade by moving cohesive domains into `src/StorageManager/*.cpp`, **one phase per PR**, behavior-preserving, no ownership or lifecycle changes.

---

## Baseline (2026-08-06)

| Artifact | LOC / status |
|----------|----------------|
| `src/StorageManager.cpp` | **3062** (root TU; was 4411) |
| `src/StorageManager/LoopSlotRestoreQueue.cpp` | **321** (Phase 3) |
| Already extracted under `src/StorageManager/` | revision commit/load, workspace save, LoadLoopJob, CurrentSetBootLoad, deferred save, … |
| Phases 0–3 on `refactor/storagemanager` | committed (`63cce36` … Phase 3) |

### Target end state

| Artifact | Target |
|----------|--------|
| `StorageManager.cpp` | **~1200–1500** — public static API + thin delegates |
| New TUs (this plan) | **~2800** moved out in 8 phases |
| Ownership / transitions | **unchanged** — hygiene only |

---

## Rules (every phase)

1. **Behavior-preserving** — no new call sites on capture stop / commit / undo paths.
2. **Architecture checkpoint** — ownership change **NO**, state transition change **NO** before first edit.
3. **Pre-implementation review** — trace symbols with `rg`; post gate table in PR description.
4. **Verification gate** — `pio test -e native` (828/828); `pio run -e teensy41-capture-serial`; phase-appropriate manual smoke when load/save/boot touched.
5. **One phase per session / PR** — do not mix unrelated extractions.
6. **State stays on `StorageManager`** — move **functions**, not a new manager class; anonymous statics become `namespace StorageManagerInternal` members in the new TU (same pattern as `RevisionLoad.cpp`).
7. **Headers** — declare moved internals in [`include/StorageManagerInternal.h`](../../include/StorageManagerInternal.h); keep [`include/StorageManager.h`](../../include/StorageManager.h) public surface stable unless renaming is already scoped elsewhere.

### Do not split (yet)

| Area | Reason |
|------|--------|
| `saveState` sync drain loop | Coupled to scheduler + `SyncDrainBudget`; move only with explicit drain-owner phase |
| Capture stop / `commitCapturePass` hooks | Protected paths — out of scope |
| `StorageSession` struct redesign | Separate track ([DEC-012](storage_session_state_refactor_handoff.md)); extraction first |

---

## Phase map (highest ROI first)

```text
Phase 0  Deferred save extract     (~650 LOC)  [WIP on branch]
Phase 1  LoadLoopJob               (~780 LOC)
Phase 2  CurrentSetBootLoad        (~670 LOC)
Phase 3  LoopSlotRestoreQueue      (~200 LOC)
Phase 4  SavedSetIo                (~550 LOC)
Phase 5  BootRecovery              (~200 LOC)
Phase 6  LegacyMonolithLoad        (~330 LOC)
Phase 7  PersistenceAdmit          (~270 LOC)
Phase 8  Wall-clock + browser      (~400 LOC)
         ─────────────────────────────────────
         StorageManager.cpp  4411 → ~1200–1500
```

Milestone after phases **0–4**: root TU **~2800 LOC**.  
Milestone after phases **0–7**: root TU **~1500 LOC**.

---

## Phase 0 — Deferred workspace save extract

**Status:** Done on `refactor/storagemanager` (`63cce36`).

**New files**

| File | Owner |
|------|--------|
| `src/StorageManager/DeferredSaveJobStages.cpp` | `stepDeferredSaveJob*` — one function per `DeferredSaveStage` |
| `src/StorageManager/DeferredSaveScheduler.cpp` | `StorageManager::processDeferredSaveState` |

**Thin remaining in root / `WorkspaceSave.cpp`**

- `stepDeferredSaveJob` — stage dispatcher only
- `hasPersistenceWorkPending`, `deferredSaveBlockedByActiveSlotLoadSd`, `deferredSaveBlockedByPostLoadCommitHoldoff` — stay in internal header; implementations may move with Phase 1 if slot-load gates move with `LoadLoopJob`

**Verify:** native 828/828; firmware build; optional deferred-save serial smoke (`#CAP` save stages).

**PR title:** `refactor(storagemanager): extract deferred save scheduler and job stages`

---

## Phase 1 — `LoadLoopJob.cpp` (~780 LOC)

**Status:** Done on `refactor/storagemanager` (after Phase 0).

**Priority:** Highest — largest cohesive FSM; same deferred-job pattern as workspace save.

### Move

| Symbol | Role |
|--------|------|
| `LoadLoopJobPhase`, `LoadLoopJob` | Job struct + phase enum |
| `loadLoopJob_`, `parkedLoadLoopJob_` | Active + parked job state |
| `postLoadLoopCommitSaveHoldoffUntilMs_` | Post-commit save holdoff |
| `clearLoadLoopJobInstance`, `clearLoadLoopJob`, `swapLoadLoopJobs` | Job lifecycle |
| `shouldFinishLoadLoopJobBeforePreempt`, `parkActiveLoadLoopJob`, `resumeParkedLoadLoopJobIfFocus`, `demoteActiveLoadLoopJobForFocus` | Focus preemption |
| `beginLoadLoopJob`, `ensureActiveLoadLoopJobSelected` | Admission + focus selection |
| `stepLoadLoopJob`, `stepLoadLoopJobParse`, `commitLoadLoopJobPublish` | Read → parse → commit grains |
| `StorageManager::stepSubmittedLoadJobs` | `DeferredJobScheduler` hook |
| `deferredSaveBlockedByActiveSlotLoadSd` | Co-locate with job state (if not moved in Phase 0) |

### Keep in root (for now)

- `processDeferredLoopSlotRestore` — moves in Phase 3 (calls `beginLoadLoopJob` / `popNextDeferredLoopSlotRestore`)
- Boot sync path `loadLoopSlotFromCurrentSetSd` — Phase 2

### Dependencies

- [`MidPassChunkPersist.cpp`](../../src/StorageManager/MidPassChunkPersist.cpp) — parse grains
- [`LoadedTrackStateApply.cpp`](../../src/StorageManager/LoadedTrackStateApply.cpp) — `applySnapshotToLoop`
- `SlotLoadSession`, `LoadLoopBudget` — unchanged contracts

### Verify

- Native tests (`test_loop_event_store`, persistence suites if touched)
- Manual: slot switch during playback; focus restore (`#CAP,LLBG,*` telemetry)
- Reference: [deferred_storage_commit_parse_split_enhancement.md](deferred_storage_commit_parse_split_enhancement.md)

**PR title:** `refactor(storagemanager): extract LoadLoopJob deferred slot load FSM`

---

## Phase 2 — `CurrentSetBootLoad.cpp` (~670 LOC)

**Status:** Done on `refactor/storagemanager`.

**Priority:** Second — separates **boot / sync** slot load from **runtime deferred** `LoadLoopJob`.

### Move

| Symbol | Role |
|--------|------|
| `resetLoopSlotForBootManifest`, `resetLoopSlotToEmpty` | Boot empty/manifest slots |
| `hydrateLoopSlotMetadataFromCurrentSetSd` | Metadata-only hydrate |
| `loadLoopSlotFromCurrentSetSd` | Sync full slot load from current-set bundle |
| `readCurrentSetTrackSlotMetadata` | Track/slot header read |
| `applyLoadedTransportFooter` | Post-bundle transport + selection |
| `resetTracksAfterFailedLoad` | Failure cleanup |
| `StorageManager::loadCurrentSetBundleAndActiveLoopSlots` | Boot bundle entry |
| `loadCurrentSetFromDirectory` (if body still in root) | Directory boot path |

### Boundaries

- **Sync boot load** (`loadLoopSlotFromCurrentSetSd`) stays distinct from **deferred** `LoadLoopJob` — document in file header comment.
- `clearLoadLoopJob()` call at boot entry stays; include `StorageManagerInternal` load-job header.

### Verify

- Cold boot with existing current-set on SD
- `loadSetIntoCurrent` / revision load integration (no double-load)
- Native persistence tests

**PR title:** `refactor(storagemanager): extract current-set boot slot load`

---

## Phase 3 — `LoopSlotRestoreQueue.cpp` (~200 LOC) — **shipped** (`refactor/storagemanager`)

### Move

| Symbol | Role |
|--------|------|
| `pendingLoopSlotRestores_`, `loopSlotRestoreAttempted_` | Queue + attempt bitmap |
| `loopSlotPayloadOnSdInRam_` + probe helpers | RAM mirror (PR #8); `probeLoopSlotPayloadOnSdFromSd`, `loopSlotManifestExistsOnSd` |
| `hasLoopSlotPayloadOnSdInRam`, `setLoopSlotPayloadOnSdInRam`, `refreshLoopSlotPayloadOnSdInRam` | Public cache API implementations |
| `queueDeferredLoopSlotRestore`, `removeDeferredLoopSlotRestore`, `sortPendingLoopSlotRestoresByPriority`, `reprioritizeDeferredLoopSlotRestoreEntries`, `enqueueRemainingLoopSlotRestores` | Queue ops |
| `popNextDeferredLoopSlotRestore` | Scheduler pop |
| `StorageManager::processDeferredLoopSlotRestore` | `DeferredJobScheduler` hook |

### Verify

- Slot-switch HITL or manual: pending restore priority, focus slot first
- No `SD.exists` on clock path (regression vs PR #8)

**PR title:** `refactor(storagemanager): extract loop slot restore queue and SD payload cache`

---

## Phase 4 — `SavedSetIo.cpp` (~550 LOC)

**Priority:** High ROI, low risk — self-contained saved-set feature.

### Move (anonymous namespace today)

| Symbol | Role |
|--------|------|
| `writeSetIndexToSd`, `readSetIndexFromSd`, `reconcileSetIndexOnSd` | Set index on SD |
| `parseSavedSetSequence`, `formatSavedSetDirectoryPath`, `resolveSavedSetFolderNameBySequence` | Path helpers |
| `copyFileBinary`, `buildSavedSetMetadata` | Binary copy + metadata build |
| `copyCurrentSetMetaToSavedSet`, `copyCurrentSetLoopsToSavedSet` | Current → saved |
| `saveNewSetInternal`, `copySavedSetIntoCurrent` | Save/load set bodies |
| `patchCurrentSetAnchor` | Anchor patch after copy |

### Keep thin in root

- `StorageManager::saveNewSet`, `loadSetIntoCurrent` — one-line delegates

### Verify

- Save new set + load into current (UI or serial)
- Set index reconcile after SD folder delete
- Cross-check [`SavedSetCatalog.cpp`](../../src/SavedSetCatalog.cpp) — no duplicate ownership

**PR title:** `refactor(storagemanager): extract saved-set I/O`

---

## Phase 5 — `BootRecovery.cpp` (~200 LOC)

### Move

| Symbol | Role |
|--------|------|
| `StorageManager::attemptBootRecoveryChain` | Revision + saved-set recovery chain |
| `tryLoadLatestRecoveryPoint`, `tryLoadNewestSavedSet` | Recovery helpers |
| `StorageManager::loadState` | Top-level boot load entry |
| `loadCurrentWorkspaceAtBoot` (if still in root) | Workspace boot |
| `queueBootRevisionRecovery` wiring if body remains in root | Boot revision queue |

### Dependencies

- Phase 2 boot load TU
- [`RevisionLoad.cpp`](../../src/StorageManager/RevisionLoad.cpp) — revision FSM unchanged

### Verify

- Boot with workspace meta pointing at derived revision
- Boot with empty workspace → newest saved set
- Phase 5 longest-prefix recovery remains **parked** — do not expand scope

**PR title:** `refactor(storagemanager): extract boot recovery chain`

---

## Phase 6 — `LegacyMonolithLoad.cpp` (~330 LOC)

### Move

| Symbol | Role |
|--------|------|
| `StorageManager::loadV5MonolithIntoRam` | Legacy v6 monolith read |
| `StorageManager::migrateV5MonolithToCurrentSet` | One-shot migration to current-set layout |
| `quarantineStorageFile` (if only used here) | Legacy quarantine |

### Notes

- Keep `STORAGE_PERSIST_MEM` / FLASHMEM attribute — cold path, ITCM pressure (see existing comment at `loadV5MonolithIntoRam`).
- Rarely touched; good isolation for reviewers.

### Verify

- Native migration tests if present; otherwise compile-only + manual only when migration hardware available

**PR title:** `refactor(storagemanager): extract legacy v5 monolith load and migration`

---

## Phase 7 — `PersistenceAdmit.cpp` (~270 LOC)

### Move

| Symbol | Role |
|--------|------|
| `admitLoopPersist`, `admitLoopUndoHistory`, `admitSlotMeta`, `admitTrackMeta`, `admitWorkspaceFooter`, `admitGlobalMeta` | Work-queue admission |
| `markCurrentSetLoopSlotDirty*`, `markCurrentSetTrackDirty*`, `markAllCurrentSetLoopSlotsDirty*` | Dirty tracking (public + internal) |
| `markCurrentSetMaterialChange`, `anyAllocatedLoopEditStateDirty`, `clearAllocatedLoopEditStateDirty` | Material change + edit dirty |
| `processEditAutosave`, `requestDeferredSaveState`, `deferWorkspaceSaveDispatchDuringPlayback`, `requestUrgentEditSave` | Autosave + dispatch request |
| `requestWorkspaceFooterPersistWhenSafe`, `workspaceFooterPersistDeferred` | Footer defer |

### Keep in root

- Status queries (`isDeferredSaveActive`, `hasDeferredSaveWork`, …) — optional later trim

### Verify

- Edit autosave during NOTE_EDIT
- Undo snapshot admit after overdub stop
- [`PersistenceWorkQueue.cpp`](../../src/StorageManager/PersistenceWorkQueue.cpp) integration unchanged

**PR title:** `refactor(storagemanager): extract persistence admit and dirty tracking`

---

## Phase 8 — Wall-clock sync + browser metadata (~400 LOC)

Two small TUs or one `SetBrowserRead.cpp` — pick one PR if combined.

### 8a — `WallClockSdSync.cpp` (~176 LOC)

| Symbol | Role |
|--------|------|
| `considerSdWallClockFloor`, `applySdWallClockFloor`, `collectSdWallClockFloorFromWorkspaceAndCurrent` | Floor collection |
| `queueWallClockSdCatalogSync`, `syncWallClockFromSdTimestampsQuick` | Quick sync |
| `stepWallClockFromSdCatalogSyncAnon`, `stepWallClockFromSdCatalogSync` | Sliced SD catalog walk |

### 8b — Browser metadata (~400 LOC)

| Symbol | Role |
|--------|------|
| `readSavedSetMetadataFromMetaPath`, `readRevisionHeaderFromRevisionFile`, `applyRevisionSlotDirectoryToSavedSetMetadata` | Shared readers |
| `readSetRevisionCatalogMetaForFolder`, `readSetRevisionHistoryBrowserMetadata` | Revision browser |
| `readSavedSetMetadataForFolder`, `readCurrentSetBrowserMetadata` | Saved-set browser |
| `toggleSetRevisionCatalogFavorite` | Catalog favorite (if still in root) |

**Alternative:** extend [`Overlay.cpp`](../../src/StorageManager/Overlay.cpp) for browser reads if overlay is the sole consumer — pre-implementation review must list call sites.

### Verify

- Set browser overlay open/scroll
- Wall-clock floor after SD insert (if testable)

**PR title:** `refactor(storagemanager): extract wall-clock SD sync and set browser metadata readers`

---

## Optional follow-up (not in ROI order)

| Item | LOC | Notes |
|------|-----|-------|
| `saveState` drain → `SyncDrainBudget.cpp` | ~80 | Pair with scheduler ownership review |
| Deferred-save status queries | ~130 | Collapse to header inlines after root TU &lt; 1.5k |
| `toggleSetRevisionCatalogFavorite` + revision catalog | ~200 | Could merge with Phase 8b |
| `StorageSession` colocation ([DEC-012](storage_session_state_refactor_handoff.md)) | — | **After** extractions stabilize file boundaries |

---

## Per-phase checklist (copy into PR)

```markdown
## Architecture gate
- Owner: StorageManager (unchanged)
- Invariant: persistence FSM semantics unchanged
- Ownership change: NO
- Transition change: NO
- Reuse: YES — move symbols to `src/StorageManager/<Phase>.cpp`

## Pre-implementation review
- [ ] `rg <symbol>` — all call sites listed
- [ ] No new hooks on stop/commit path
- [ ] `StorageManagerInternal.h` declarations updated

## Tests
- [ ] `pio test -e native`
- [ ] `pio run -e teensy41-capture-serial`
- [ ] Manual: <phase-specific smoke>
```

---

## Suggested PR stack on `refactor/storagemanager`

| # | Phase | Base |
|---|-------|------|
| 1 | Phase 0 — deferred save | `dev` |
| 2 | Phase 1 — LoadLoopJob | Phase 0 merge |
| 3 | Phase 2 — CurrentSetBootLoad | Phase 1 merge |
| 4 | Phase 3 — LoopSlotRestoreQueue | Phase 2 merge |
| 5 | Phase 4 — SavedSetIo | `dev` or Phase 3 (parallel-safe) |
| 6 | Phase 5 — BootRecovery | Phase 2 merge |
| 7 | Phase 6 — LegacyMonolithLoad | `dev` (parallel-safe) |
| 8 | Phase 7 — PersistenceAdmit | After Phases 1–3 |
| 9 | Phase 8 — Wall-clock + browser | `dev` (parallel-safe) |

Phases **4, 6, 8** can run in parallel after their listed base; **1 → 2 → 3** should stay sequential (load job + restore queue ordering).

---

## References

- [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — stop path constraints
- [deferred_job_scheduler_architecture.md](deferred_job_scheduler_architecture.md) — `DeferredJobScheduler` hooks
- [firmware_ownership_lifetime_review.md](firmware_ownership_lifetime_review.md) — audit context; P0 SD cache shipped as PR #8
- [runtime_process_building_blocks_overview.md](runtime_process_building_blocks_overview.md) — StorageManager owner row
