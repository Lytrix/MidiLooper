# Handoff — StorageSession open items (DEC-012)

**Date:** 2026-06-29  
**Branch:** `load-save-sets-loops` (commits `9ef4bd0`, `43d996b`)  
**Parent:** [storage_session_state_refactor_handoff.md](storage_session_state_refactor_handoff.md)  
**OpenSpec (authoritative tasks):** [`openspec/changes/storage-session-state-refactor/tasks.md`](../../openspec/changes/storage-session-state-refactor/tasks.md)  
**Decision:** [DEC-012](../DECISION_LOG.md#dec-012-storagesession-persistence-state-model)  
**Runtime scope:** [CURRENT_WORK.md](../runtime/CURRENT_WORK.md)

---

## One-line goal

Finish DEC-012: export shared persistence helpers, split FSM translation units, complete `StorageSession` job migration and backend API renames — then proceed to `transport.bin` / `global.bin`.

---

## Shipped (do not redo)

| Tier | Deliverable |
|------|-------------|
| **0** | `StorageActivitySnapshot`, `buildStorageActivitySnapshot()`, contract tests; removed `RevisionLoadPolicy::isMinimalLoadingOverlayActive` wrapper |
| **2** | `resolvePersistencePhase()`; renamed `PersistencePhase` values; removed `revisionLoadPipelineActive` + imperative phase assignments; partial `StorageSession` (`revisionLoad`, `revisionCommit.overlayBackgroundCommit`, `setBrowserNavigation`) |
| **3 partial** | `StorageManagerInternal.h` + `src/StorageManager/{Internal,Overlay,FileIo,RuntimeBundleFooter,WorkspaceSave,RevisionCommit,RevisionLoad}.cpp`; FSM TUs (§1–§6); overlay load dispatch + dirty-prompt paths in `Overlay.cpp` (§7.1–§7.5) |

**Verification (green on 2026-06-29):** `pio test -e native` (292 tests), `pio run -e teensy41-capture-serial`.

---

## Open items (priority order)

### 1. Tier 3 — `StorageSession` job struct migration (next)

**OpenSpec:** tasks **§8** + spec [`storage-session-jobs`](../../openspec/changes/storage-session-state-refactor/specs/storage-session-jobs/spec.md).

Overlay TU **shipped** (§7.1–§7.5). **Pending hardware:** §7.6 HITL overlay presets.

`StorageSession` today holds only revision load request/hold/dispatch + `revisionCommit.overlayBackgroundCommit` + navigation. Still **extern** in `StorageManagerInternal` (not nested structs):

| Target job member | Still extern statics (examples) |
|-----------------|----------------------------------|
| `currentWorkspaceSave` | `deferredSavePending`, `deferredSaveStage`, `deferredSaveFile`, cursors, … |
| `revisionCommit` | `revisionCommitPending`, `revisionCommitStage`, `revisionCommitFile`, paths, slot entries, … |
| `revisionLoad` (extend) | `revisionLoadStage`, `revisionLoadSetId`, reload RAM cursors, source/dest files, … |
| `bootRecovery` | `bootRevisionRecoveryPending`, `bootRevisionRecoverySetId`, `bootRevisionRecoveryRevisionId` |

Migrate incrementally per FSM TU split: when moving deferred save FSM, colocate `deferredSave*` into `storageSession.currentWorkspaceSave` (struct fields TBD — extend `StorageSession.h`).

**Do not** migrate durable workspace facts (`currentWorkspaceEpoch`, `lastCommittedWorkspaceEpoch`, …) into `StorageSession`.

---

### 4. Backend API renames (after struct migration stable)

**OpenSpec:** tasks **§9** + delta [`revision-load/spec.md`](../../openspec/changes/storage-session-state-refactor/specs/revision-load/spec.md).

Public `StorageManager` API still uses legacy names. Rename per parent handoff (keep HITL wire strings):

| Current | Target |
|---------|--------|
| `confirmRevisionLoadDirtyPromptSaveThenLoad` | `confirmRevisionLoadAfterCommit` |
| `confirmRevisionLoadDirtyPromptDiscard` | `confirmRevisionLoadDiscardWorkspace` |
| `cancelRevisionLoadDirtyPrompt` | `cancelRevisionLoadRequest` |
| `isRevisionLoadDirtyPromptActive` | `isRevisionLoadHeldForWorkspaceDirty` |
| `dispatchStagedRevisionLoad` (internal) | `dispatchRequestedRevisionLoad` |
| `clearRevisionLoadPromptAndPipelineState` | `clearRevisionLoadRequestState` |

Update callers: `DisplayManager.cpp`, `MidiButtonActions.cpp`, HITL serial in `StorageManager.cpp`, tests.

---

### 5. Policy renames (low risk, after Step 4 or anytime)

**OpenSpec:** tasks **§10**.

| Current | Target |
|---------|--------|
| `RevisionLoadPolicy::LoadRequestGate` | Remove; use `shouldHoldRevisionLoadRequest(bool)` |
| `resolveLoadRequestGate` | `shouldHoldRevisionLoadRequest` |
| `shouldDispatchStagedLoadAfterCommitComplete` | `shouldDispatchRequestedLoadAfterCommitComplete` |

---

### 6. Verification not yet done for Tier 3

**OpenSpec:** tasks **§11**.

- [ ] HITL overlay presets after FSM / overlay TU changes (`scripts/hitl/scenarios/`, serial capture)
- [ ] Manual load/save overlay on hardware (dirty prompt, save-then-load, save-row background commit)
- [ ] Confirm `PROJECT_STATE.md` + `CURRENT_WORK.md` at session close

---

## File map (current)

| Path | Role |
|------|------|
| `src/StorageManager.cpp` | Public API + orchestrator (`processDeferredSaveState`); FSM bodies until Step 2 extract |
| `include/StorageManager.h` | Public `StorageManager` API |
| `include/StorageManagerInternal.h` | Stage enums, extern job state, shared helper + FSM entry declarations |
| `src/StorageManager/Internal.cpp` | Job RAM definitions; workspace meta / telemetry / budget helpers |
| `src/StorageManager/Overlay.cpp` | Snapshot, overlay navigation, load request + dirty-prompt dispatch (shipped) |
| `src/StorageManager/FileIo.cpp` | `writeRaw`/`readRaw`, `storageIoFromFile*`, deferred stage-name strings |
| `src/StorageManager/WorkspaceSave.cpp` | *(Step 2)* `currentWorkspaceSave` FSM |
| `src/StorageManager/RevisionCommit.cpp` | *(Step 2)* `revisionCommit` FSM |
| `src/StorageManager/RevisionLoad.cpp` | `revisionLoad` FSM (shipped) |

---

## Explicitly NOT in this handoff

- `transport.bin` / `global.bin` workspace split (after Tier 3 FSM split complete)
- `SetBrowserOverlayPolicy` module rename
- LoopPick behavior
- New `*Manager` classes (DEC-008)

---

## Suggested builder prompt

```text
Read openspec/changes/storage-session-state-refactor/tasks.md (DEC-012 Tier 3).
Implement the next unchecked task only (export-before-extract per design D2).

Current slice: Step 2a — export WorkspaceSave private helpers (tasks §1), then 2b extract WorkspaceSave.cpp.

pio test -e native after each slice. Do not change FSM step semantics.
```

---

## Session notes (2026-06-29)

- Full FSM extract in one pass failed: split TUs could not link to anonymous-namespace helpers or `dispatchStagedRevisionLoad` from `StorageManagerOverlay.cpp`.
- State extract to `src/StorageManager/Internal.cpp` succeeded; `using namespace StorageManagerInternal` in `StorageManager.cpp` + overlay TU.
- `buildStorageActivitySnapshot()` lives in `src/StorageManager/Overlay.cpp` under `StorageManagerInternal` namespace.
- Implementation TUs grouped under `src/StorageManager/` to avoid flat `StorageManager*` proliferation; job FSM filenames `WorkspaceSave` / `RevisionCommit` / `RevisionLoad` (DEC-012).
