## Why

Every boot of the current image restores its slot bodies rootless, so playback has no root to serve and depends on a
rebuild that lands tens of seconds later. The owner's workaround is to record a take — which publishes a root — before
the restored material plays. That is the "I always need to create a root on boot" report.

The writer is not the cause. `writeLoopPersisted` already refuses to embed a root it cannot prove equivalent to the
passes it serializes (DEC-180), and the deferred save already has a bounded settle wait (DEC-074 RC2). The defect is
that the save is **admitted before the root is offered**:

- every record/overdub stop calls `markLoopSlotMaterialDirty` + `admitLoopSlotPersist` + `requestDeferredSaveState`
  from the commit, i.e. before the session-exit publication has run;
- `decideLoopPersistBodyFormat` only returns `AwaitSettled` when the root is **already** coverage-current
  (`publishedSnapshotIsCurrent() && hasPendingSnapshotPublish()`), and a commit that advanced
  `committedPassRevision()` makes that first term false;
- so the decision is `PassesAuthoritative` on the very first writer turn, the settle budget is never consumed, and the
  body is written without a root.

Measured on device (`captures/session_20261029_021341.log`): `PERS,request` 19.069 s → `PERS,result` 19.195 s →
`snapshot_unit,kind=publish` 19.619 s. The body is always finished ~0.4 s **before** the root it should carry exists.

## What Changes

- **The settle wait covers an owed publication, not only an already-current root.** `decideLoopPersistBodyFormat`
  waits while the loop has a snapshot publication owed for its current committed state, even though the selected root
  is one revision behind. The bound becomes a wall-clock window (`kLoopPersistSettleWaitMs = 1500`), because 128
  writer turns are milliseconds and cannot span a real publication; a turn cap (`kLoopPersistSettleMaxDeferrals`)
  remains as the secondary bound so a virtual-clock fixture or an unserviced publisher still terminates the wait. A
  deferral closes the slot temp file so no SD handle is held across the wait, and a synchronous drain never waits.
- **A body written without a root is remembered per slot and re-persisted once its root settles.** The deferred save
  records `currentSetLoopSlotRootRewritePending[track][slot]` when it finalizes a passes-authoritative body for a slot
  with committed passes, and clears it when it writes a root-bearing body. A new persistence-owner turn
  (`StorageManager::serviceRootlessSlotRepersist`) re-admits exactly that slot through the existing
  `markLoopSlotMaterialDirty` + `admitLoopSlotPersist` + `requestDeferredSaveState` path when the slot's root is
  settled. No new queue, owner, or file format.

Net effect on the owner's workflow: record → the root publishes → the body that is on the card carries it. The next
boot restores a root, so playback starts from the stored content with no new take.

## Non-goals

- DEC-180 is unchanged: a body still embeds a root **only** when that root is proven equivalent to the passes being
  serialized. Nothing here writes an unverified root.
- No boot-time root rebuild (a load-time rebuild was removed in DEC-192), no loader change, no body-format change, and
  no change to what is stored (the passes stay authoritative content).
- No playback-path change: the DEC-193 regression and DEC-194 rootless first window are untouched and must not depend
  on this change.
- No SD write at boot, no re-save of slots the user did not touch, and no re-save without a new content change or a
  settling publication.

## Capabilities

### New Capabilities

- `root-bearing-loop-persist`: the deferred loop-body write waits (bounded) for a publication owed for the current
  committed state, a passes-authoritative body is re-persisted once that slot's root settles, and every body that
  embeds a root still passes the loader's equivalence gate.

### Modified Capabilities

None.

## Impact

Production files: `src/StorageLoopIo.cpp` (body-format decision), `src/StorageManager/WorkspaceSave.cpp` (settle wait),
`src/StorageManager/DeferredSaveJobStages.cpp` (record/clear at slot finalize), `src/StorageManager/PersistenceAdmit.cpp`
and `include/StorageManagerInternal.h` / `include/StorageSession.h` / `include/StorageManager.h` (record + owner turn),
`src/main.cpp` (one call beside the existing persistence turn).

Verification: `test_storage_loop_io` (decision table) and `test_storage_drain_restore` (production save + reload).

OpenSpec specs affected on archive: new `openspec/specs/root-bearing-loop-persist/`.

See [design](design.md), [tasks](tasks.md).
