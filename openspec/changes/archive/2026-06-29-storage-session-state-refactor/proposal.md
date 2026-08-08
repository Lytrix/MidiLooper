## Why

DEC-012 Tier 0–2 shipped (`StorageActivitySnapshot`, derived `resolvePersistencePhase`, partial
`StorageSession`), but ~6k lines of persistence FSM logic remain in `StorageManager.cpp` with job RAM
still split between `storageSession` and file-scope `extern` statics. A prior single-pass FSM extract
failed because anonymous-namespace helpers and dispatch entry points were not linkable from new TUs.

This structural debt blocks `transport.bin` / `global.bin` work and makes overlay/load-save changes
high-risk. OpenSpec is needed now to turn the handoff's coarse steps into ordered, verifiable tasks
with explicit helper-export prerequisites — **without changing FSM step semantics**.

Brownfield: [DEC-012](../../docs/DECISION_LOG.md#dec-012-storagesession-persistence-state-model),
[storage_session_state_refactor_open_items_handoff.md](../../docs/Plans/storage_session_state_refactor_open_items_handoff.md),
[set-revision-persistence](../set-revision-persistence/proposal.md) (behavior already shipped).

## What Changes

- **Shipped (mark done in tasks):** `StorageManagerInternal.h` shared I/O + stage-name exports;
  `Internal.cpp` / `Overlay.cpp` / `FileIo.cpp`; FSM entry points declared in
  `StorageManagerInternal` namespace.
- **Step 2 — FSM TU split:** Extract three job FSM translation units under `src/StorageManager/`:
  `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`. Export remaining anonymous helpers
  per job **before** each extract. Keep `StorageManager::processDeferredSaveState` as orchestrator in
  `StorageManager.cpp`.
- **Step 3 — Overlay TU completion:** Move `requestLoadRevision`, dirty-prompt confirm/cancel, and
  related getters into `Overlay.cpp` once `dispatchStagedRevisionLoad` is exported.
- **Step 4 — Job struct migration:** Colocate `deferredSave*`, `revisionCommit*` stage statics, and
  `revisionLoad*` stage statics into `StorageSession` job members (`currentWorkspaceSave`,
  `revisionCommit`, `revisionLoad`, `bootRecovery`). Durable workspace facts stay file-scope.
- **Step 5 — Backend API renames:** Rename `*DirtyPrompt*` / `*Staged*` / `*Pipeline*` surfaces per
  DEC-012. HITL serial wire strings unchanged.
- **Step 6 — Policy renames:** Drop `LoadRequestGate`; use `shouldHoldRevisionLoadRequest`.
- **Verification:** `pio test -e native` after each PR-sized slice; HITL overlay presets after
  overlay/dispatch moves; firmware compile `teensy41-capture-serial`.

**Non-goals:**

- FSM stage transition or slice-budget semantics
- `transport.bin` / `global.bin` workspace split
- New `*Manager` classes (DEC-008)
- `SetBrowserOverlayPolicy` module rename
- LoopPick behavior

## Capabilities

### New Capabilities

- `storage-session-layout`: Translation-unit ownership, helper export rules, and link boundaries for
  persistence FSM code under `src/StorageManager/`.
- `storage-session-jobs`: `StorageSession` job struct fields and migration rules for active
  persistence RAM (not durable workspace facts).

### Modified Capabilities

- `revision-load` (delta in active change only): Document backend API rename targets
  (`confirmRevisionLoadAfterCommit`, etc.) — **behavior unchanged**; HITL wire strings preserved.

## Impact

| Area | Files |
|------|-------|
| Session model | `include/StorageSession.h`, `include/StorageManagerInternal.h` |
| Job RAM | `src/StorageManager/Internal.cpp` |
| FSM bodies | `src/StorageManager/{WorkspaceSave,RevisionCommit,RevisionLoad}.cpp` (new) |
| Overlay routing | `src/StorageManager/Overlay.cpp` |
| Shared I/O | `src/StorageManager/FileIo.cpp` |
| Public API | `include/StorageManager.h`, `src/StorageManager.cpp` (orchestrator + HITL) |
| Policy | `include/RevisionLoadPolicy.h`, `src/RevisionLoadPolicy.cpp` |
| Callers | `src/DisplayManager.cpp`, `src/MidiButtonActions.cpp` |
| Tests | `test/test_set_revision_persistence/` |
| Runtime docs | `docs/Runtime/CURRENT_WORK.md`, `docs/Runtime/PROJECT_STATE.md` |
| Handoff | `docs/Plans/storage_session_state_refactor_open_items_handoff.md` |

Tracking: [docs/DELIVERABLE_TRACKING.md](../../docs/DELIVERABLE_TRACKING.md).
