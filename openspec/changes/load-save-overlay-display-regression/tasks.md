## 1. Investigation baseline

- [x] 1.1 Document root cause: overlay SD reads vs persistence concurrency; missing load-complete exit; boolean mode composition (see `design.md`).
- [x] 1.2 OpenSpec change `load-save-overlay-display-regression` with proposal, design, specs.

## 2. Persistence SD gate (minimal fix)

- [x] 2.1 Add `StorageManager::isOverlayCatalogReadAllowed()` (queued + in-progress + `*SdIoActive`).
- [x] 2.2 Guard `refreshLoadSaveListCache`, `refreshLoadSaveRevisionHistoryCache`, and `resolveLoadSaveWorkspaceDetail` with gate API.
- [x] 2.3 Defer overlay-enter list refresh in `DisplayManager::update` when gate is false; use stale cache if valid.
- [x] 2.4 Native unit tests: gate false during each job type; true when all idle.

## 3. Load-complete lifecycle

- [x] 3.1 On `stepRevisionLoadComplete`, call `exitLoadSaveMode()` when overlay is open.
- [x] 3.2 Verify `consumeRevisionLoadDisplayRefreshPending` + cache invalidation path runs after auto-exit.
- [x] 3.3 Extend native or HITL verify: `LDSV` 1→0 after `rev_load_complete` without manual overlay close.

## 4. Overlay persistence phase (FSM)

- [x] 4.1 Add `SetBrowserOverlayPersistencePhase` (or equivalent) with transitions in StorageManager dirty/load/commit paths.
- [x] 4.2 Wire `getSetBrowserOverlayMode()` minimal branch to phase instead of `isMinimalLoadingOverlayActive(bool×5)`.
- [x] 4.3 Skip `resetSetBrowserOverlayNavigation()` on overlay enter when phase ≠ Idle.
- [x] 4.4 Update `test_set_revision_persistence` phase/minimal-mode tests for new API.
- [x] 4.5 Remove or thin `RevisionLoadPolicy::isMinimalLoadingOverlayActive` if fully superseded.

## 5. Display polish

- [x] 5.1 Clamp persistence dot Y in `drawLoadSaveMinimalLoadingView` / dirty prompt (no negative coordinates).

## 6. Verification

- [ ] 6.1 `pio test -e native` — gate + phase tests green.
- [ ] 6.2 HITL: `load_save_overlay_load` (MIDI overlay load + dirty yes path).
- [ ] 6.3 HITL: `revision_load_dirty` regression.
- [ ] 6.4 Manual smoke: open overlay during PLAYING + confirm load; confirm no freeze/crash on minimal screen.

## 7. Closeout

- [ ] 7.1 Update `docs/plans/set_revision_persistence_handoff.md` regression note.
- [ ] 7.2 Archive or merge deltas into `set-revision-persistence` on green HITL.
