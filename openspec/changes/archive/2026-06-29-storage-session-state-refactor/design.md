## Context

`StorageManager.cpp` (~6.3k lines) owns public API, deferred-save orchestration, three persistence
FSMs, boot paths, and HITL capture hooks. DEC-012 introduced:

- `StorageSession` (`include/StorageSession.h`) — partial job flags (`revisionLoad` request/hold,
  `revisionCommit.overlayBackgroundCommit`, `setBrowserNavigation`)
- `StorageManagerInternal` namespace — shared stage enums, extern job RAM, exported I/O helpers
- `src/StorageManager/{Internal,Overlay,FileIo}.cpp` — job RAM defs, snapshot/overlay routing, SD
  primitives

FSM **entry points** are already in `StorageManagerInternal` namespace but **bodies and private
step helpers** remain in `StorageManager.cpp` anonymous namespaces. Session 2026-06-29 proved a
one-shot extract fails without exporting helpers first.

**Authority:** DEC-012, DEC-008 (`StorageManager` sole persistence owner). No ownership moves.

## Goals / Non-Goals

**Goals:**

1. Three job FSM TUs with clear symbol boundaries and zero FSM semantic change.
2. Overlay request/dispatch paths colocated in `Overlay.cpp`.
3. Incremental `StorageSession` job struct migration (one job per PR where practical).
4. Backend API rename pass after struct layout is stable.
5. Each PR: `pio test -e native` green; firmware builds.

**Non-Goals:**

- `transport.bin` / `global.bin` split
- Changing `DeferredSaveStage` / `RevisionCommitStage` / `RevisionLoadStage` transitions
- Moving `FLASHMEM`/`DMAMEM` HITL backup block out of `StorageManager.cpp`
- Migrating durable workspace facts (`currentWorkspaceEpoch`, `lastCommittedWorkspaceEpoch`, …) into
  `StorageSession`

## Decisions

### D1 — TU layout under `src/StorageManager/`

| File | Owns |
|------|------|
| `StorageManager.cpp` | Public `StorageManager::` API, `processDeferredSaveState` orchestrator, boot/load paths, HITL `SESSION_CAPTURE` block |
| `Internal.cpp` | `storageSession` instance, extern job RAM definitions, workspace meta / budget helpers |
| `FileIo.cpp` | `writeRaw`/`readRaw`, `storageIoFromFile*`, deferred stage-name strings |
| `Overlay.cpp` | `buildStorageActivitySnapshot`, overlay mode/navigation getters, **after Step 3:** load request + dirty-prompt confirm/cancel |
| `WorkspaceSave.cpp` | `currentWorkspaceSave` FSM (`resetDeferredSaveJobState`, `beginDeferredSaveJob`, `stepDeferredSaveJob` + private step helpers) |
| `RevisionCommit.cpp` | `revisionCommit` FSM (`resetRevisionCommitJobState`, `stepRevisionCommitJob` + private step helpers) |
| `RevisionLoad.cpp` | `revisionLoad` FSM (`clearRevisionLoadRequestState`, `dispatchRequestedRevisionLoad`, `resetRevisionLoadJobState`, `stepRevisionLoadJob` + private step helpers) |

**Rationale:** Matches DEC-012 job names; avoids flat `StorageManager*.cpp` proliferation at repo root.
Public API stays `include/StorageManager.h`.

### D2 — Export-before-extract rule (link failure mitigation)

For each FSM TU, **declare in `StorageManagerInternal.h` and define in the target TU** (or shared
`Internal.cpp`/`FileIo.cpp` when used by multiple jobs) all symbols the FSM body calls that are
today `static` or in an anonymous namespace in `StorageManager.cpp`.

**Do not** call across TUs through anonymous namespaces. **Do not** leave forward declarations in
anonymous namespaces in `StorageManager.cpp` that the new TU cannot link.

### D3 — Helper export inventory (remaining blockers)

#### WorkspaceSave private helpers (export to `StorageManagerInternal` before `WorkspaceSave.cpp`)

| Symbol | Approx. lines in `StorageManager.cpp` | Notes |
|--------|----------------------------------------|-------|
| `resetDeferredLoopWriteState` | ~799 | Sub-state reset |
| `resetDeferredUndoWriteState` | ~806 | Sub-state reset |
| `writeCurrentSetMetaHeaderToOpenFile` | ~2684 | Meta header write |
| `finalizeDeferredMetaTempFile` | ~2693 | Temp rename |
| `finalizeDeferredLoopSlotTemp` | ~2756 | Per-slot finalize |
| `stepDeferredLoopPersist` | ~2952 | Loop body streaming |
| `stepDeferredEmptyLoopPersist` | ~3024 | Empty slot path |
| `stepDeferredLoopSnapshotPersist` | ~3055 | Undo snapshot loops |
| `stepDeferredUndoStackPersist` | ~3157 | Undo stack streaming |

Entry points already exported: `resetDeferredSaveJobState`, `beginDeferredSaveJob`,
`stepDeferredSaveJob`.

#### RevisionCommit private helpers

| Symbol | Approx. lines | Notes |
|--------|---------------|-------|
| `resolveMaxPersistenceMicros` | ~917 | Budget helper (or keep inline via `PersistenceBudget`) |
| `resolveRevisionCommitSourceEpoch` | ~923 | Snapshot epoch |
| `slotSourceFileReadableForRevisionCommit` | ~930 | Slot eligibility |
| `prepareRevisionCommitLayout` | ~970 | Slot index build |
| `beginRevisionCommitSnapshot` | ~1021 | Snapshot stage |
| `stepRevisionCommitWrite` | ~1300 | Write stage |
| `stepRevisionCommitValidate` | ~1516 | Validate stage |
| `stepRevisionCommitCatalogUpdate` | ~1642 | Catalog stage |
| `stepRevisionCommitComplete` | ~1734 | Complete stage |

Entry points already exported: `resetRevisionCommitJobState`, `stepRevisionCommitJob`.

#### RevisionLoad private helpers

| Symbol | Approx. lines | Notes |
|--------|---------------|-------|
| `resetRevisionLoadReloadRamState` | ~1831 | Reload RAM sub-FSM reset |
| `findRevisionLoadSlotEntry` | ~1910 | Slot directory lookup |
| `stepRevisionLoadValidate` | (in ~1908 block) | Validate stage |
| `stepRevisionLoadWrite` | ~2339 | Write stage |
| `stepRevisionLoadReloadRam` | ~2518 | Reload RAM stage |
| `stepRevisionLoadComplete` | ~2615 | Complete stage |

Entry points already exported: `clearRevisionLoadPromptAndPipelineState`,
`dispatchStagedRevisionLoad`, `resetRevisionLoadJobState`, `stepRevisionLoadJob`.

**Overlay dispatch dependency:** `readSetLatestRevisionIdFromSd` (~5835) must be exported before
moving `requestLoadLatestRevisionForSet` to `Overlay.cpp`.

### D4 — Extract order (one PR per job FSM recommended)

```text
2a WorkspaceSave helpers export → 2b WorkspaceSave.cpp extract → native green
2c RevisionCommit helpers export → 2d RevisionCommit.cpp extract → native green
2e RevisionLoad helpers export → 2f RevisionLoad.cpp extract → native green
3  Overlay dispatch paths → native + HITL overlay presets
4  StorageSession job struct migration (per job, interleaved with 2 if desired)
5  Backend API renames
6  Policy renames
7  Verification closeout
```

**Rationale:** Smallest link surface per PR; failed session tried all three at once.

### D5 — `StorageSession` job struct shapes (Tier 1)

Extend `include/StorageSession.h` incrementally:

```cpp
struct CurrentWorkspaceSaveJob {
  // deferredSavePending, deferredSaveStage, deferredSaveFile, cursors, …
};

struct RevisionCommitJob {
  bool overlayBackgroundCommit = false;
  // revisionCommitPending, revisionCommitStage, paths, slot entries, …
};

struct RevisionLoadJob {
  // existing request/hold flags + revisionLoadStage, paths, reload cursors, …
};

struct BootRecoveryJob {
  bool pending = false;
  uint16_t setId = 0;
  uint16_t revisionId = 0;
};

struct StorageSession {
  CurrentWorkspaceSaveJob currentWorkspaceSave;
  RevisionCommitJob revisionCommit;
  RevisionLoadJob revisionLoad;
  BootRecoveryJob bootRecovery;
  SetBrowserOverlayPolicy::NavigationState setBrowserNavigation;
};
```

Migrate reads/writes in the same PR as each FSM TU extract when practical. Keep `extern` shims in
`StorageManagerInternal.h` only if a single PR would touch >3 files — prefer direct struct access.

**Do not** move `currentWorkspaceEpoch`, `lastCommittedWorkspaceEpoch`, `workspaceDerivedFromSetId`,
or loop-slot dirty matrices into `StorageSession`.

### D6 — API rename pass (after Step 4 stable)

| Current | Target |
|---------|--------|
| `confirmRevisionLoadDirtyPromptSaveThenLoad` | `confirmRevisionLoadAfterCommit` |
| `confirmRevisionLoadDirtyPromptDiscard` | `confirmRevisionLoadDiscardWorkspace` |
| `cancelRevisionLoadDirtyPrompt` | `cancelRevisionLoadRequest` |
| `isRevisionLoadDirtyPromptActive` | `isRevisionLoadHeldForWorkspaceDirty` |
| `dispatchStagedRevisionLoad` | `dispatchRequestedRevisionLoad` |
| `clearRevisionLoadPromptAndPipelineState` | `clearRevisionLoadRequestState` |

HITL wrappers (`*ForHitl`) and serial tokens (`rev_load_dirty_yes`, etc.) unchanged.

### D7 — SESSION_CAPTURE block stays in `StorageManager.cpp`

`HitlRevisionCommitBackup` and related `FLASHMEM`/`DMAMEM` symbols remain in `StorageManager.cpp`
until a dedicated reassessment. FSM TUs must not duplicate this block.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Link errors from missed anonymous helpers | D2 export inventory + one-job-per-PR |
| Accidental FSM semantic drift | No edits inside `switch (stage)` bodies except namespace/struct accessor renames; native tests gate each PR |
| Struct migration touches many sites | Migrate one job per PR; grep `deferredSave` / `revisionCommit` / `revisionLoad` prefixes |
| Overlay TU calls private dispatch | Export `dispatchRequestedRevisionLoad` + `readSetLatestRevisionIdFromSd` before Step 3 |
| API rename breaks callers | Single rename PR; grep `StorageManager::` and tests |

## Migration Plan

1. Implement from `tasks.md` in order; mark `[x]` only after native green (and HITL when noted).
2. Update `docs/runtime/PROJECT_STATE.md` + `CURRENT_WORK.md` at each session close.
3. Archive change to `openspec/specs/` after Tier 3 complete and overlay HITL verified.
4. Then unblock `transport.bin` / `global.bin` OpenSpec (separate change).

## Open Questions

- None blocking Step 2. `CurrentWorkspaceSaveJob` field grouping can follow first export PR once
  grep shows exact extern list (tasks 4.1).
