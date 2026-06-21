## 1. Capture state guards

- [x] 1.1 Add early return in `Track::startOverdubbing` when already overdubbing with open capture
- [x] 1.2 Guard `TrackManager::startOverdubbingTrack` (no-op when idempotent condition met)
- [x] 1.3 Audit `MidiButtonActions::OVERDUB_FOR_SLOT` — no double-start side effects
- [x] 1.4 Native test: second overdub start preserves `capture.store` event count

## 2. Storage temporal round-trip

- [x] 2.1 Fix `applySnapshotToLoop` to set `loop.startLoopTick` from snapshot
- [x] 2.2 Make `readPersistedEditsTail` return false on any `ioRead` failure (remove success-on-empty branches)
- [x] 2.3 Extend `test_storage_loop_io` — `startLoopTick` save/load equality
- [x] 2.4 Extend `test_storage_loop_io` — truncated edit tail fails `readLoopPersisted`

## 3. Slot loop resolution

- [x] 3.1 Change `LoopPool::findById` — no silent `at(0)`; expose not-found to callers
- [x] 3.2 Update `Track::loopForSlot` to use pool index when id invalid/unknown
- [x] 3.3 On `StorageManager::loadState`, repair `slotLoopId` outside `0..7` to slot index + log
- [x] 3.4 Native test: unknown `loopId` resolves via slot index, not slot 0 data

## 4. editPasses memory — deferred to `pool-budget`

Task group removed. Fixed **editPass** row cap and capture pass count gate ship in
**`openspec/changes/pool-budget/`** (heap/chunk admission + **`reclaimUnreferencedDisabledPasses`**).

- [ ] 4.0 _(Parked)_ — implement via **`pool-budget`** tasks 3, 6, 7; do not add `MAX_EDIT_PASSES_PER_LOOP` here

## 5. Playback runtime prewarm

- [x] 5.1 Add `TrackManager::prewarmPlaybackRuntime()` — touch runtime + playback order for enabled/data slots
- [x] 5.2 Call prewarm from `main.cpp` setup after `allocateLoopsEarly`
- [x] 5.3 Call prewarm after successful `StorageManager::loadState`
- [x] 5.4 Native or host test: document/prevent first-play allocation (test hook or compile-time flag)

## 6. Deferred validate completion

- [x] 6.1 Add `Config::deferredValidateMaxDelayMs` and per-track queue timestamp in `Track`
- [x] 6.2 Extend `processDeferredIdleMaintenance` — run validate when PLAYING-only exceeds delay
- [x] 6.3 Keep deferral during RECORDING/OVERDUBBING on that track
- [x] 6.4 Native test: queued flag ages out under simulated PLAYING-only idle policy

## 7. Docs and verification

- [x] 7.1 Update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` — startLoopTick load, deferred validate policy (not edit cap)
- [x] 7.2 Run `pio test -e native` — full suite green
- [x] 7.3 HITL canonical baseline if firmware uploaded (user confirms upload)
- [x] 7.4 `/opsx:archive` after tasks complete and specs merged
