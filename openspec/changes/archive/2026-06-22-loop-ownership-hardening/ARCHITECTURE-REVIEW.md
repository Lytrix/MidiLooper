# Architecture review — loop ownership hardening

**Change:** `loop-ownership-hardening`  
**Date:** 2026-06-21  
**Source:** Cursor architecture review (ownership, transitions, memory lifecycle, temporal guarantees)

This change closes all findings below. See [proposal.md](proposal.md), [design.md](design.md), and
[tasks.md](tasks.md) for implementation.

---

## Finding → capability map

| Severity | Finding | Capability / task group |
|----------|---------|-------------------------|
| Critical | `TRACK_OVERDUBBING → OVERDUBBING` + `beginCapture` wipes live capture | `capture-state-guards` §1 |
| High | `applySnapshotToLoop` forces `startLoopTick = 0` | `storage-loop-io`, `loop-temporal-persistence` §2 |
| High | `LoopPool::findById` silent fallback to slot 0 | `multi-loop-slots` §3 |
| Medium | Unbounded **editPasses** growth | **`pool-budget`** (`pass-reclaim`, heap admission) — not §4 here |
| Medium | Lazy `LoopPlaybackRuntime` alloc on play | `playback-runtime-prewarm` §5 |
| Medium | Deferred validate only when all tracks idle | `loop-temporal-persistence` §6 |
| Medium | `readPersistedEditsTail` success on read failure | `storage-loop-io` §7 |

---

## Evidence anchors (code)

| Finding | Primary location |
|---------|------------------|
| Overdub re-entry | `TrackStateMachine.cpp`, `Track::startOverdubbing`, `TrackManager::startOverdubbingTrack` |
| startLoopTick drop | `StorageLoopIo::applySnapshotToLoop` |
| Slot alias | `LoopPool::findById`, `Track::loopForSlot`, `StorageManager::loadState` |
| editPasses growth | `Loop::saveNoteEditPass`, `TrackUndo::disableEditPasses` |
| Playback alloc | `TrackPlaybackRuntime::slot`, `Track::playMidiEvents` |
| Deferred validate | `Track::processDeferredIdleMaintenance`, `main.cpp` idle gate |
| SD tail | `StorageLoopIo::readPersistedEditsTail` |

---

## Verification matrix (post-implementation)

| Finding | Native test | HITL |
|---------|-------------|------|
| Overdub idempotent | `test_capture_state_guards` | Optional: double overdub slot press |
| startLoopTick | `test_storage_loop_io` round-trip | Save/load session |
| Slot resolution | `test_loop_pool_slot_resolution` | — |
| editPasses cap | `test_edit_pass_cap` | Edit until cap log |
| Prewarm | `test_playback_prewarm` (alloc hook or flag) | — |
| Deferred validate | `test_deferred_validate_policy` | Long play session |
| SD tail fail | `test_storage_loop_io` corrupt tail | — |

Gate: `pio test -e native` before archive; HITL canonical baseline if capture/SD paths change.
