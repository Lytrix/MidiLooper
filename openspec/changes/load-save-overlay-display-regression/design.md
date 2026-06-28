## Context

### Shipped overlay model (set-revision-persistence §3.7)

The load/save UI is specified as explicit modes (`ROOT`, `DIRTY_PROMPT`, `REVISION_HISTORY`,
`LOOP_PICK`, `MINIMAL_LOADING`) with save-then-load keeping **minimal mode** until commit **and**
load finish, then **exit overlay**. Save row exits overlay immediately while commit continues in
background.

Implementation today spreads state across:

| State | Owner |
|-------|--------|
| Overlay open/closed | `LooperState::loadSaveOverlayActive` |
| Drill navigation | `StorageManager::setBrowserOverlayNavigation` |
| Dirty prompt | `revisionLoadDirtyPromptActive`, staged ids |
| Load pipeline | `revisionLoadPipelineActive`, `revisionLoadPending/InProgress`, `revisionLoadSdIoActive` |
| Commit pipeline | `revisionCommitPending/InProgress`, `revisionCommitSdIoActive` |
| Deferred epoch save | `deferredSavePending/InProgress`, `deferredSaveSdIoActive` |
| Minimal mode predicate | `RevisionLoadPolicy::isMinimalLoadingOverlayActive(5 booleans)` |

`DisplayManager::getSetBrowserOverlayMode()` (via `StorageManager`) composes mode at read time.
`DisplayManager::update()` additionally calls `resetSetBrowserOverlayNavigation()` on overlay
enter and may call `refreshLoadSaveListCache()` which performs multi-file SD reads.

### Observed regression (uncommitted WIP)

Local diffs attempted mitigation:

- `persistenceBusy` guard in `resolveLoadSaveWorkspaceDetail` only (detail panel).
- `commitPending` added to `isMinimalLoadingOverlayActive` so minimal view appears before commit
  dispatch (not only during `commitInProgress`).
- Skip list refresh on overlay re-enter when `loadSaveListCacheValid_`.

These do **not** cover list/history cache refresh, overlay enter on first open during pipeline, or
load-complete lifecycle. User reports crashes persist on load/save and minimal overlays.

### Main-loop ordering

`main.cpp` runs `displayManager.update()` **before** `processDeferredSaveState()`. Display may
therefore initiate SD reads in the same iteration persistence later opens SD files — Teensy
`SD`/SPI is not safely reentrant across concurrent callers.

## Goals / Non-Goals

**Goals:**

- Eliminate overlay-initiated SD reads during persistence work (queued, in-progress, or active SD
  slice).
- Guarantee minimal overlay covers the full save-then-load and clean-load pipelines until
  completion, then auto-exit and refresh piano-roll caches.
- Replace fragile multi-boolean minimal-mode predicate with one **overlay persistence phase**
  derived from pipeline ownership (StorageManager).
- Keep display updates allocation-free and cheap during minimal mode (text + dots only).
- Regress with native + HITL overlay load scenarios.

**Non-Goals:**

- Rewriting deferred save/commit/load FSM stages.
- Moving catalog indexing off SD or background thread.
- Full overlay UI state machine for drill navigation (keep `SetBrowserOverlayPolicy::NavigationState`
  for ROOT/REVISION_HISTORY/LOOP_PICK).

## Decisions

### Decision 1: Central overlay SD gate in StorageManager

Add:

```cpp
bool StorageManager::isOverlayCatalogReadAllowed();
```

Returns `false` when **any** of:

- `deferredSavePending || deferredSaveInProgress`
- `revisionCommitPending || revisionCommitInProgress`
- `revisionLoadPending || revisionLoadInProgress`
- `deferredSaveSdIoActive || revisionCommitSdIoActive || revisionLoadSdIoActive`

**Rationale:** Display must not open SD files while persistence owns or is about to use the bus.
Using `has*Work()` alone misses the slice window; using `*SdIoActive` alone misses queued commit
before dispatch. Combine both (same condition as current partial `persistenceBusy` plus SD-active).

**Apply at all overlay SD touch points:**

- `refreshLoadSaveListCache`
- `refreshLoadSaveRevisionHistoryCache`
- `resolveLoadSaveWorkspaceDetail` (replace inline `persistenceBusy` with gate API)
- Overlay enter path in `DisplayManager::update` (defer refresh until gate allows)

When gate is false, use last-valid cache or skip detail (existing cache fallback pattern).

**Alternative rejected:** Block entire `displayManager.update()` during persistence — breaks
minimal overlay and sidebar save spinner spec.

### Decision 2: Overlay persistence phase enum (small FSM)

Add `OverlayPersistencePhase` (name TBD; avoid new top-level domain noun — prefer
`SetBrowserOverlayPersistencePhase` in `SetBrowserOverlayPolicy` or `StorageManager`):

| Phase | Display mode | Enter | Exit |
|-------|--------------|-------|------|
| `Idle` | ROOT / drill per navigation | Default | Load/save pipeline starts |
| `AwaitingCommitThenLoad` | MINIMAL_LOADING | Dirty Yes or save-then-load armed | Commit complete + load dispatched |
| `LoadInProgress` | MINIMAL_LOADING | Clean load or post-commit load | `stepRevisionLoadComplete` |
| `CommitOnlyBackground` | *(overlay closed)* | Save row confirm | Commit completes |

`getSetBrowserOverlayMode()` maps:

- `DirtyPrompt` when `revisionLoadDirtyPromptActive` (unchanged priority).
- `MinimalLoading` when phase is `AwaitingCommitThenLoad` or `LoadInProgress` **and**
  `loadSaveOverlayActive`.
- Otherwise navigation drill mode.

Replace `RevisionLoadPolicy::isMinimalLoadingOverlayActive(bool×5)` with phase lookup + unit tests
for transitions. Keep `RevisionLoadPolicy` for dirty gate and save-then-load dispatch predicates.

**Rationale:** One derived phase eliminates desync between `commitPending` and
`revisionLoadPipelineActive` during the pre-dispatch window and documents transitions in one place.

### Decision 3: Auto-exit overlay on load complete

In `stepRevisionLoadComplete()` (or immediately after successful complete step in
`processDeferredSaveState`):

1. If `looperState.isLoadSaveModeActive()` → `exitLoadSaveMode()`.
2. Set `revisionLoadDisplayRefreshPending` (already done).
3. Reset overlay persistence phase to `Idle`; clear staged load flags via existing complete path.

DisplayManager continues to consume refresh pending when overlay closes (existing path) — works
after auto-exit.

**Rationale:** Shipped spec requires exit after load; staying in ROOT after pipeline end triggers
full catalog SD reads and blocks cache refresh (`consumeRevisionLoadDisplayRefreshPending` gated on
`!loadSaveActive`).

### Decision 4: Do not reset navigation on overlay enter during active pipeline

When `loadSaveOverlayActive` transitions false→true:

- If overlay persistence phase ≠ `Idle`, **skip** `resetSetBrowserOverlayNavigation()` and list
  selection reset (user re-opened during minimal load).
- Otherwise keep current enter behavior.

**Rationale:** Entering overlay during minimal load should not wipe drill/pipeline context.

### Decision 5: Clamp dot Y in overlay persistence views

Use the same dot row convention as dirty prompt (`rowY - 4` only when `rowY >= 4`) or fixed
`kSaveStatusDotY` for minimal view. Eliminate `0 - 4`.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Stale set list while pipeline runs | Accept stale cache during minimal mode; refresh once gate opens after exit |
| Phase enum drift from FSM | Transitions only in StorageManager load/commit complete + dirty confirm paths; native matrix |
| HITL expects overlay open through load | Verify `LDSV` transitions 1→0 on auto-exit; update verify script if needed |
| Double exitLoadSaveMode | Guard with `isLoadSaveModeActive()` |

## Migration Plan

1. Land gate API + apply to DisplayManager SD paths (safe incremental fix).
2. Add load-complete auto-exit.
3. Introduce persistence phase enum; retire 5-boolean minimal predicate.
4. Native + HITL regression pass; archive change into `set-revision-persistence` specs on green.

## Open Questions

| Question | Default |
|----------|---------|
| Should minimal view show during **Save row** commit when user re-opens overlay? | Yes — phase `CommitOnlyBackground` is overlay-closed; if user re-opens, show minimal until commit done (gate blocks catalog) |
| Emit `#CAP,OVERLAY_PHASE,<phase>` for HITL? | Optional in SESSION_CAPTURE; defer unless HITL needs it |
