# Handoff — StorageSession state refactor (Tier 0–3)

**Date:** 2026-06-29  
**Decision:** [DEC-012](../DECISION_LOG.md#dec-012-storagesession-persistence-state-model)  
**Branch:** `load-save-sets-loops`  
**Parent handoff:** [set_revision_persistence_handoff.md](set_revision_persistence_handoff.md)  
**Architecture:** extend `StorageManager` only (DEC-008) — no `SaveSessionManager`, no `StorageHandler`

---

## One-line goal

Colocate scattered persistence RAM into **`StorageSession`** on **`StorageManager`**, align revision-load naming with **request / held / dispatched**, derive overlay coordination phase (Tier 2), then split translation units before `transport.bin` / `global.bin` work.

---

## Background (why now)

Last ~30 commits added revision commit/load FSMs and overlay UX. State lives in ~80 anonymous statics in `StorageManager.cpp` (~6.7k lines). Sprint slang (**pipeline**, **prompt**, **saveThenLoad**, **Gate**) is misaligned with repo vocabulary (**request**, **pending**, **inProgress**, **stage**, **job**, **confirm**, **cancel**).

Front/back split must stay:

| Layer | Owns |
|-------|------|
| **Backend** | `StorageSession` jobs, hold/confirm, SD FSM stages |
| **Display** | Overlay pixels, list selection/scroll, caches (`DisplayManager`) |
| **Policy** | Pure derivation (`SetBrowserOverlayPolicy`, `RevisionLoadPolicy`) |

---

## Locked naming

### Aggregate

- **`StorageSession`** — RAM aggregate owned by `StorageManager` (struct, not Manager/namespace owner)

### Jobs inside `StorageSession`

| Member | Replaces (conceptually) |
|--------|-------------------------|
| `currentWorkspaceSave` | `deferredSave*` anonymous statics |
| `revisionCommit` | `revisionCommit*` statics |
| `revisionLoad` | `revisionLoad*` + dirty/staged/pipeline statics |
| `setBrowserNavigation` | `setBrowserOverlayNavigation` (`SetBrowserOverlayPolicy::NavigationState`) |
| `bootRecovery` | `bootRevisionRecovery*` |

**Not used:** `StorageOverlaySession`, `*Intent*`, `*Pipeline*` in struct/API names.

### `RevisionLoadJob` lifecycle

```text
requested  →  held (workspace dirty)  →  dispatched (pending / inProgress / stage)
```

| Field (target) | Replaces |
|----------------|----------|
| `requested`, `requestedSetId`, `requestedRevisionId` | `revisionLoadRequestStaged`, `revisionLoadStaged*` |
| `heldForWorkspaceDirty` | `revisionLoadDirtyPromptActive` |
| `confirmChoice` (`RevisionLoadConfirmChoice`) | `revisionLoadDirtyPromptSelection`, `DirtyPromptChoice` |
| `loadAfterRevisionCommit` | `revisionLoadSaveThenLoadPipeline` |
| `pending`, `inProgress`, `sdIoActive`, `stage`, … | unchanged semantics |
| *(remove)* | `revisionLoadPipelineActive` — folded into Tier 2 derivation |

### API renames (backend)

| Today | Target |
|-------|--------|
| `confirmRevisionLoadDirtyPromptSaveThenLoad` | `confirmRevisionLoadAfterCommit` |
| `confirmRevisionLoadDirtyPromptDiscard` | `confirmRevisionLoadDiscardWorkspace` |
| `cancelRevisionLoadDirtyPrompt` | `cancelRevisionLoadRequest` |
| `isRevisionLoadDirtyPromptActive` | `isRevisionLoadHeldForWorkspaceDirty` |
| `dispatchStagedRevisionLoad` | `dispatchRequestedRevisionLoad` |
| `clearRevisionLoadPromptAndPipelineState` | `clearRevisionLoadRequestState` |

HITL/serial wire strings (`rev_load_dirty_yes`, etc.) — keep for compat; map at capture layer if API renames land.

### Policy

- Drop `LoadRequestGate` → `RevisionLoadPolicy::shouldHoldRevisionLoadRequest(bool workspaceDirty)`
- Drop `RevisionLoadPolicy::isMinimalLoadingOverlayActive` (test-only wrapper; Tier 0)
- `shouldDispatchStagedLoadAfterCommitComplete` → `shouldDispatchRequestedLoadAfterCommitComplete`

### Derived coordination (Tier 2 — not stored on session)

Replace imperative `setBrowserOverlayPersistencePhase` assignments with policy output:

| Old `PersistencePhase` | Target name |
|------------------------|-------------|
| `AwaitingCommitThenLoad` | `AwaitingRevisionCommit` |
| `LoadInProgress` | `RevisionLoadActive` |
| `CommitOnlyBackground` | `RevisionCommitActive` |
| `Idle` | `Idle` |

`SetBrowserOverlayPolicy::Mode` (`DirtyPrompt`, `MinimalLoading`, …) may stay for **display routing**; backend uses job + hold flags.

### Display (unchanged names OK)

- `drawLoadSaveDirtyPromptView`, `loadSaveListSelection_`, `isLoadSaveModeActive()` — frontend only

### LoopPick

- `NavigationState` / policy stubs only until loop-picker task; no new behavior in this refactor.

---

## Tier sequence (implementation order)

### Tier 0 — this sprint (start here)

1. **`StorageActivitySnapshot`** + single `buildStorageActivitySnapshot()` in `StorageManager.cpp`
2. Contract doc section in this file + native matrix tests (`test_set_revision_persistence`)
3. Remove `RevisionLoadPolicy::isMinimalLoadingOverlayActive` + tests that only cover the wrapper
4. `pio test -e native`

**No FSM step logic changes.**

### Tier 2 — this sprint (after Tier 0 green)

1. `resolvePersistencePhase(snapshot)` (or renamed enum) — derived, delete 8 imperative assignments
2. Fold `revisionLoadPipelineActive` into derivation rules
3. Introduce `StorageSession` / job structs; migrate fields incrementally
4. HITL overlay scenarios + manual overlay pass if display gates change

### Tier 1 — interleaved with Tier 2

- Move anonymous statics into job structs; `reset*JobState()` touch struct members

### Tier 3 — in progress

**Shipped (2026-06-29):**

- `include/StorageManagerInternal.h` — stage enums, `kMaxRevisionLoopIndexEntries`, extern job state
- `src/StorageManager/Internal.cpp` — `StorageSession` + deferred save / revision commit / revision load RAM
- `src/StorageManager/Overlay.cpp` — `buildStorageActivitySnapshot()`, overlay mode/navigation/dirty-prompt UI, catalog-read gate, load display status
- `src/StorageManager/FileIo.cpp` — `writeRaw`/`readRaw`, `storageIoFromFile*`, deferred stage-name strings
- Step 2 job FSM TUs: `src/StorageManager/WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp` — **not yet extracted**; see [`storage-session-state-refactor` tasks](../../openspec/changes/storage-session-state-refactor/tasks.md)

**Remaining (before `transport.bin` / `global.bin`):** see **[storage_session_state_refactor_open_items_handoff.md](archive/handoff/storage_session_state_refactor_open_items_handoff.md)** (closed) and OpenSpec **`storage-session-state-refactor`** archive.

---

## State map (layers)

| Layer | Location | Examples |
|-------|----------|----------|
| Durable workspace facts | `CurrentWorkspaceStorage` / epoch statics | `currentWorkspaceEpoch`, dirty |
| Active jobs | **`StorageSession`** | save, commit, load FSMs |
| Browser drill | **`setBrowserNavigation`** | `NavigationState` |
| Derived views | Policy headers | phase, `Mode`, `DeferredSaveDisplayPhase` |
| Wire format | `StorageLoopIo`, `RevisionPackedBlob` | not session |

Loop MIDI validate on stop (`LoopStopFinalize`) — **not** `StorageSession`.

---

## Key files

| Area | Path |
|------|------|
| Session aggregate (new) | `include/StorageSession.h` |
| Activity snapshot (Tier 0) | `include/StorageActivitySnapshot.h`, `src/StorageActivitySnapshot.cpp` |
| Internal shared (Tier 3) | `include/StorageManagerInternal.h`, `src/StorageManager/{Internal,Overlay,FileIo}.cpp` |
| Owner + drain | `src/StorageManager.cpp`; job FSMs → `src/StorageManager/{WorkspaceSave,RevisionCommit,RevisionLoad}.cpp` (Step 2) |
| Overlay policy | `include/SetBrowserOverlayPolicy.h`, `src/SetBrowserOverlayPolicy.cpp` |
| Load policy | `include/RevisionLoadPolicy.h`, `src/RevisionLoadPolicy.cpp` |
| Catalog read gate | `include/OverlayCatalogReadPolicy.h` |
| Display | `src/DisplayManager.cpp` (getters only in Tier 2) |
| Input | `src/MidiButtonActions.cpp` |
| Tests | `test/test_set_revision_persistence/`, `test/test_save_status_display/` |
| Contract | this file |

---

## Verification gates

| Tier | Gate |
|------|------|
| 0 | Native matrix: snapshot → phase → `Mode` → catalog-read allowed |
| 2 | Same matrix after derived phase; HITL overlay presets if touched |
| 3 | `pio test -e native`; firmware build `teensy41-capture-serial` |

---

## Explicitly NOT in scope

- New `*Manager` for persistence (DEC-008)
- `StorageHandler` (wrong suffix — event router, not owner)
- `SaveSession` namespace as owner
- Renaming `SetBrowserOverlayPolicy` module (optional later)
- `transport.bin` / `global.bin` split (follows Tier 3)
- LoopPick behavior

---

## Suggested builder prompt

```text
Read docs/Plans/storage_session_state_refactor_handoff.md (DEC-012).
Implement Tier 0: StorageActivitySnapshot, contract tests, remove RevisionLoadPolicy::isMinimalLoadingOverlayActive wrapper.
pio test -e native. Do not change FSM step logic yet.
```

---

## Tier 0 contract — `StorageActivitySnapshot`

Read path only; FSM writers unchanged until Tier 2.

### Snapshot fields

| Field | Source (today) |
|-------|------------------|
| `deferredSavePending/InProgress/SdIoActive` | deferred save job statics |
| `revisionCommitPending/InProgress/SdIoActive` | revision commit job statics |
| `revisionLoadPending/InProgress/SdIoActive` | revision load job statics |
| `revisionLoadDirtyPromptActive` | dirty-prompt hold |
| `overlayOpen` | `looperState.isLoadSaveModeActive()` |
| `persistencePhase` | `setBrowserOverlayPersistencePhase` (imperative until Tier 2) |
| `navigation` | `setBrowserOverlayNavigation` |

Populated by `buildStorageActivitySnapshot()` in `StorageManager.cpp`.

### Derivation chain (native-tested)

```text
StorageActivitySnapshot
  → isMinimalLoadingOverlayActive(snapshot)     // phase + overlayOpen + commit flags
  → resolveOverlayMode(snapshot)                // dirty prompt > minimal > drill nav
  → isOverlayCatalogReadAllowed(snapshot)       // job in-progress or SD slice active
```

`overlayCatalogReadInputsFromSnapshot()` maps job flags to `OverlayCatalogReadInputs`.
`*Pending` alone does not block catalog reads; `*InProgress` and `*SdIoActive` do.

### Matrix scenarios (`test_set_revision_persistence`)

| Scenario | Phase | overlayOpen | dirty | load in progress | Expected mode | Catalog read |
|----------|-------|-------------|-------|------------------|---------------|--------------|
| Idle, closed | Idle | false | — | — | Root | allowed |
| Dirty prompt during load | LoadInProgress | true | yes | yes | DirtyPrompt | blocked |
| Save-then-load pre-dispatch | AwaitingCommitThenLoad | true | — | commit pending | MinimalLoading | allowed |
| Active load | LoadInProgress | true | — | yes | MinimalLoading | blocked |
| Background commit + pending | CommitOnlyBackground | true | — | commit pending | MinimalLoading | allowed |
| Drill, idle pipeline | Idle | true | — | — | RevisionHistory | allowed |
| SD IO slice | Idle | true | — | any `*SdIoActive` | — | blocked |

### Tier 2 contract — derived `resolvePersistencePhase`

`persistencePhase` is **not stored**; `buildStorageActivitySnapshot()` fills job flags and
`resolvePersistencePhase(snapshot)` derives:

| Phase | Derivation |
|-------|------------|
| `AwaitingRevisionCommit` | `loadAfterRevisionCommit` && commit pending/in progress && load not pending/in progress |
| `RevisionLoadActive` | `revisionLoadPending` \|\| `revisionLoadInProgress` |
| `RevisionCommitActive` | `revisionCommitOverlayBackground` && commit pending/in progress |
| `Idle` | otherwise |

`revisionLoadPipelineActive` removed — `isRevisionLoadDisplayPipelineActive()` derives from
`loadAfterRevisionCommit` \|\| load pending/in progress.

`StorageSession` (`include/StorageSession.h`) owns revision load request/hold/dispatch flags,
`revisionCommit.overlayBackgroundCommit`, and `setBrowserNavigation`. FSM stage statics remain
file-scope until Tier 1/3 migration.

---

## Session closeout

Architecture session 2026-06-29. DEC-012 appended. No firmware shipped in planning session.
