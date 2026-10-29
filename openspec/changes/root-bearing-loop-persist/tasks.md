## 1. Slice 1 — the settle wait covers an owed publication

- [x] 1.1 Fixture first (`test_storage_loop_io`): an owed publication for the current committed state with a
  one-revision-behind root decides `AwaitSettled` while the budget remains, and `PassesAuthoritative` once it does
  not; an uncovered root with nothing owed decides `PassesAuthoritative` with budget remaining. Red before the
  predicate change.
- [x] 1.2 `decideLoopPersistBodyFormat`: drop the `publishedSnapshotIsCurrent()` requirement from the settle branch;
  keep the three outcomes and the writer's equivalence gate untouched.
- [x] 1.3 Replace the 128-turn settle counter with a wall-clock window (`kLoopPersistSettleWaitMs`) on
  `CurrentWorkspaceSaveJob`; arm it on the first `Header` turn of a loop write and reset it with the loop-write state.
- [x] 1.4 A deferral closes the slot temp file (guarded by `loopFileOpen`) so no SD handle is held across the wait.
- [x] 1.5 Focused gates: `test_storage_loop_io`, `test_storage_drain_restore`; build and RAM1 comparison.

## 2. Slice 2 — a rootless body is re-persisted once its root settles

- [x] 2.1 Fixture first (`test_storage_drain_restore`): production save of a committed-passes/rootless slot writes a
  passes-authoritative body and sets the record; the owner turn admits nothing until the root settles; after the
  production publication settles it admits exactly that slot once and the reloaded slot reports a current root. Red
  before the record exists.
- [x] 2.2 Record at slot finalize from the pinned `loopWriteUsesLsv1` decision; clear it on a root-bearing body.
- [x] 2.3 `StorageManager::serviceRootlessSlotRepersist`: admit flagged slots whose root is settled through
  `markLoopSlotMaterialDirty` + `admitLoopSlotPersist` + `requestDeferredSaveState`; drop the record for slots without
  committed passes. Call it once per turn beside the deferred persistence turn.
- [x] 2.4 Focused gates: `test_storage_drain_restore`, then full `pio test -e native`; build and RAM1 comparison.

## 3. Closeout

- [x] 3.1 DEC entry recording the extended settle predicate, the wall-clock bound, and the rootless-body record.
- [x] 3.2 Update `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`, `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`,
  `docs/Authority/Architecture/ExecutionBounds.md` (persistence row) and `docs/Runtime/{PROJECT_STATE,CURRENT_WORK}.md`.
- [ ] 3.3 Device leg only on owner-authorized flash: record/stop, power cycle, confirm the restored slot has
  `root_rev` non-zero and plays without a new take.
