## Why

Recent set-revision persistence work added load/save overlay modes (`ROOT`, `DIRTY_PROMPT`,
`MINIMAL_LOADING`) and save/load status dots, but overlay display logic still composes mode from
many independent booleans and performs synchronous SD catalog reads from `DisplayManager` while
deferred save, revision commit, and revision load jobs may be active. On device this manifests as
display freeze, blank/corrupt OLED output, or hard faults when opening the overlay, confirming a
load, or viewing the minimal save/load screen during a persistence pipeline.

This regression blocks reliable HITL for `load_save_overlay_load` and undermines shipped
`set-revision-persistence` overlay requirements. A focused fix is needed before extending overlay
features (revision history drill-down, loop pick).

Brownfield: [`docs/plans/set_revision_persistence_handoff.md`](../../../docs/plans/set_revision_persistence_handoff.md),
active change [`set-revision-persistence`](../set-revision-persistence/proposal.md).

## What Changes

- **Root-cause fix:** Gate **all** overlay SD reads (set list, revision history list, detail panel
  metadata) while any persistence job is queued, in progress, or holding SD I/O (`*SdIoActive`).
  Extend the partial `persistenceBusy` guard in `resolveLoadSaveWorkspaceDetail` to list caches and
  overlay enter paths.
- **Lifecycle fix:** Exit load/save overlay automatically when deferred load (and save-then-load
  commit+load) completes, then consume `revisionLoadDisplayRefreshPending` — matching shipped
  `revision-load` and `set-browser-overlay` requirements.
- **Mode coordination:** Replace ad-hoc boolean composition for minimal overlay with a single
  overlay persistence phase owned beside the revision load/commit pipeline (small FSM or enum +
  transition helpers), consumed by `getSetBrowserOverlayMode()` and `DisplayManager`.
- **Display polish:** Clamp persistence dot Y coordinates in minimal/dirty views (no negative row
  offsets).
- **Verification:** Native tests for overlay phase transitions + persistence SD gate; HITL
  `load_save_overlay_load` and dirty-load scenarios as regression gates.

**Non-goals:**

- Refactoring `StorageManager` deferred FSM core or revision on-disk format.
- Loop-pick / subtitle edit overlay features.
- Reintroducing global display skip during all of `hasDeferredSaveWork()` (sidebar spinner must
  remain visible).

## Capabilities

### New Capabilities

- `overlay-persistence-display-gate`: Rules for when overlay UI may touch SD catalog/metadata and
  how minimal save/load display behaves during persistence pipelines.

### Modified Capabilities

- `set-browser-overlay`: Load-complete overlay exit, unified minimal mode during full pipeline,
  SD-safe catalog preview (delta against [`set-revision-persistence`](../set-revision-persistence/specs/set-browser-overlay/spec.md)).
- `revision-load`: Signal overlay exit and display cache refresh when load completes while overlay
  is still open (delta against [`set-revision-persistence`](../set-revision-persistence/specs/revision-load/spec.md)).

## Impact

| Area | Files |
|------|-------|
| Display | `DisplayManager.cpp`, `DisplayManager.h` |
| Overlay mode / pipeline | `StorageManager.cpp`, `SetBrowserOverlayPolicy.*`, `RevisionLoadPolicy.*` |
| Looper overlay lifecycle | `LooperState.cpp`, possibly `stepRevisionLoadComplete` |
| Tests | `test_set_revision_persistence`, new overlay gate tests |
| HITL | `scripts/hitl/scenarios/load_save_overlay_load.py`, `revision_load_dirty.py` |
| Docs | `docs/plans/set_revision_persistence_handoff.md` (regression note on archive) |
