## 0. Shipped prerequisites (DEC-012 Tier 0–2 + Step 1)

- [x] 0.1 `StorageActivitySnapshot` + `buildStorageActivitySnapshot()` + native matrix tests
- [x] 0.2 `resolvePersistencePhase()`; removed imperative phase assignments + `revisionLoadPipelineActive`
- [x] 0.3 Partial `StorageSession` (`revisionLoad` request/hold, `revisionCommit.overlayBackgroundCommit`, `setBrowserNavigation`)
- [x] 0.4 `src/StorageManager/{Internal,Overlay,FileIo}.cpp` + `StorageManagerInternal.h` stage enums and extern job RAM
- [x] 0.5 Export `writeRaw`/`readRaw`, `storageIoFromFile*`, deferred stage-name helpers, `fillSlotSummariesForTrack`, `writeWorkspaceMetaAfterDeferredSave`, `resolvePersistenceSliceBudgetUs`, `emitDeferredSaveSliceTelemetry`
- [x] 0.6 FSM entry points in `StorageManagerInternal` namespace (`resetDeferredSaveJobState`, `beginDeferredSaveJob`, `stepDeferredSaveJob`, `resetRevisionCommitJobState`, `stepRevisionCommitJob`, `clearRevisionLoadPromptAndPipelineState`, `dispatchStagedRevisionLoad`, `resetRevisionLoadJobState`, `stepRevisionLoadJob`)
- [x] 0.7 `pio test -e native` + `pio run -e teensy41-capture-serial` green (2026-06-29)

## 1. Step 2a — Export WorkspaceSave private helpers

- [x] 1.1 Add declarations to `StorageManagerInternal.h` for: `resetDeferredLoopWriteState`, `resetDeferredUndoWriteState`, `writeCurrentSetMetaHeaderToOpenFile`, `finalizeDeferredMetaTempFile`, `finalizeDeferredLoopSlotTemp`, `stepDeferredLoopPersist`, `stepDeferredEmptyLoopPersist`, `stepDeferredLoopSnapshotPersist`, `stepDeferredUndoStackPersist`
- [x] 1.2 Move definitions from `StorageManager.cpp` anonymous namespace into `namespace StorageManagerInternal` (in `WorkspaceSave.cpp`; also exported `persistenceWriteRaw`, `storageIoFromFileWriteWithRevisionPayloadCrc`, `appendRevisionCommitPayloadCrc` for cross-TU link)
- [x] 1.3 Remove duplicate forward declarations in anonymous namespace; keep call sites compiling (fixed stray `}` at `beginRevisionCommitSnapshot` that broke anonymous-namespace scope)
- [x] 1.4 `pio test -e native` — no FSM semantic changes

## 2. Step 2b — Extract `WorkspaceSave.cpp`

- [x] 2.1 Create `src/StorageManager/WorkspaceSave.cpp` with includes matching `Internal.cpp` pattern (`StorageManagerInternal.h`, `Globals.h`, …)
- [x] 2.2 Move `resetDeferredSaveJobState`, `beginDeferredSaveJob`, `stepDeferredSaveJob` bodies from `StorageManager.cpp` into `WorkspaceSave.cpp`
- [x] 2.3 Move WorkspaceSave private helpers (task 1.2) into same TU
- [x] 2.4 Verify PlatformIO picks up `src/StorageManager/*.cpp` (default `+<*>` filter)
- [x] 2.5 Confirm `StorageManager::processDeferredSaveState` still calls `stepDeferredSaveJob()` via `using namespace StorageManagerInternal` or qualified name — orchestrator stays in `StorageManager.cpp`
- [x] 2.6 `pio test -e native` + `pio run -e teensy41-capture-serial`

## 3. Step 2c — Export RevisionCommit private helpers

- [x] 3.1 Add declarations to `StorageManagerInternal.h` for: `resolveMaxPersistenceMicros`, `resolveRevisionCommitSourceEpoch`, `slotSourceFileReadableForRevisionCommit`, `prepareRevisionCommitLayout`, `beginRevisionCommitSnapshot`, `stepRevisionCommitWrite`, `stepRevisionCommitValidate`, `stepRevisionCommitCatalogUpdate`, `stepRevisionCommitComplete`
- [x] 3.2 Move definitions into `namespace StorageManagerInternal` (merged revision-commit anonymous block through `stepRevisionCommitComplete`; sibling helpers remain in `StorageManager.cpp` until §4)
- [x] 3.3 `pio test -e native`

## 4. Step 2d — Extract `RevisionCommit.cpp`

- [x] 4.1 Create `src/StorageManager/RevisionCommit.cpp`
- [x] 4.2 Move `resetRevisionCommitJobState`, `stepRevisionCommitJob`, and RevisionCommit private helpers into TU
- [x] 4.3 `pio test -e native` + `pio run -e teensy41-capture-serial`

## 5. Step 2e — Export RevisionLoad private helpers

- [x] 5.1 Add declarations to `StorageManagerInternal.h` for: `resetRevisionLoadReloadRamState`, `findRevisionLoadSlotEntry`, `beginRevisionLoadValidate` (validate stage entry; tasks doc name `stepRevisionLoadValidate`), `stepRevisionLoadWrite`, `stepRevisionLoadReloadRam`, `stepRevisionLoadComplete`
- [x] 5.2 Export `readSetLatestRevisionIdFromSd` (required by overlay dispatch in Step 3)
- [x] 5.3 Move definitions into `namespace StorageManagerInternal` (merged revision-load anonymous blocks through `readSetLatestRevisionIdFromSd`)
- [x] 5.4 `pio test -e native`

## 6. Step 2f — Extract `RevisionLoad.cpp`

- [x] 6.1 Create `src/StorageManager/RevisionLoad.cpp`
- [x] 6.2 Move `clearRevisionLoadPromptAndPipelineState`, `dispatchStagedRevisionLoad`, `resetRevisionLoadJobState`, `stepRevisionLoadJob`, and RevisionLoad private helpers into TU
- [x] 6.3 `pio test -e native` + `pio run -e teensy41-capture-serial`
- [x] 6.4 Grep `StorageManager.cpp` — no remaining `stepRevisionLoad*` / `stepRevisionCommit*` / `stepDeferredSaveJob` bodies (orchestrator + boot paths only)

## 7. Step 3 — Complete overlay TU

- [x] 7.1 Move `StorageManager::requestLoadRevision` and `requestLoadLatestRevisionForSet` from `StorageManager.cpp` to `Overlay.cpp`
- [x] 7.2 Move `confirmRevisionLoadDirtyPromptSaveThenLoad`, `confirmRevisionLoadDirtyPromptDiscard`, `cancelRevisionLoadDirtyPrompt` (+ `SESSION_CAPTURE` `*ForHitl` wrappers if colocated)
- [x] 7.3 Move or verify `hasRevisionLoadWork`, `isRevisionLoadActive`, `getDeferredLoadDisplayStatus`, `getRevisionLoadDisplayTarget*` in overlay TU
- [x] 7.4 Keep `requestCommitRevision` in `StorageManager.cpp` (public API) — overlay confirm paths call it
- [x] 7.5 `pio test -e native` + `pio run -e teensy41-capture-serial`
- [ ] 7.6 HITL overlay presets: `scripts/hitl/scenarios/` load/save + dirty-load scenarios with serial capture

## 8. Step 4 — `StorageSession` job struct migration

- [ ] 8.1 Extend `include/StorageSession.h`: `CurrentWorkspaceSaveJob`, expand `RevisionCommitJob`, expand `RevisionLoadJob`, add `BootRecoveryJob`
- [ ] 8.2 Migrate `deferredSave*` externs → `storageSession.currentWorkspaceSave` (with `Internal.cpp` definitions updated)
- [ ] 8.3 Migrate `revisionCommit*` stage externs (except durable epoch facts) → `storageSession.revisionCommit`
- [ ] 8.4 Migrate `revisionLoad*` stage externs → `storageSession.revisionLoad`
- [ ] 8.5 Migrate `bootRevisionRecovery*` → `storageSession.bootRecovery`
- [ ] 8.6 Trim migrated symbols from `StorageManagerInternal.h` extern list
- [ ] 8.7 Update `buildStorageActivitySnapshot()` and policy inputs to read struct members only
- [ ] 8.8 `pio test -e native`

## 9. Step 5 — Backend API renames

- [ ] 9.1 Rename symbols per `specs/revision-load/spec.md` in `StorageManager.h` / `.cpp` and `StorageManagerInternal.h`
- [ ] 9.2 Update callers: `DisplayManager.cpp`, `MidiButtonActions.cpp`, HITL serial handlers in `StorageManager.cpp`
- [ ] 9.3 Update native tests if they reference legacy names
- [ ] 9.4 HITL wire strings unchanged — grep `rev_load_dirty_` still mapped
- [ ] 9.5 `pio test -e native`

## 10. Step 6 — Policy renames

- [ ] 10.1 Remove `RevisionLoadPolicy::LoadRequestGate`; add `shouldHoldRevisionLoadRequest(bool workspaceDirty)`
- [ ] 10.2 Rename `resolveLoadRequestGate` → `shouldHoldRevisionLoadRequest`
- [ ] 10.3 Rename `shouldDispatchStagedLoadAfterCommitComplete` → `shouldDispatchRequestedLoadAfterCommitComplete`
- [ ] 10.4 Update `requestLoadRevision` path in overlay TU to use new policy names
- [ ] 10.5 `pio test -e native`

## 11. Verification and closeout

- [ ] 11.1 Manual hardware pass: dirty prompt, save-then-load, save-row background commit
- [ ] 11.2 Update `docs/runtime/PROJECT_STATE.md` + `docs/runtime/CURRENT_WORK.md`
- [ ] 11.3 Update `docs/plans/storage_session_state_refactor_open_items_handoff.md` — mark items done
- [ ] 11.4 Update `docs/DELIVERABLE_TRACKING.md` when Tier 3 ships
- [ ] 11.5 `/opsx:archive` after all gates pass

**Apply order:** 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11. Do not skip helper export (1, 3, 5) before extract (2, 4, 6).

**Explicitly NOT in this change:** `transport.bin` / `global.bin`, `SetBrowserOverlayPolicy` module rename, LoopPick, new `*Manager` classes.
