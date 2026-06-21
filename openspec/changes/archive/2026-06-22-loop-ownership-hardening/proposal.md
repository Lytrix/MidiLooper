## Why

An architecture review (2026-06-21) found seven ownership, temporal, and lifecycle gaps in the
post-M8 **passes** model: overdub re-entry can wipe in-flight capture, SD load drops
`startLoopTick`, invalid slot `loopId` values alias to pool index 0, **editPasses** grow without
bound, playback paths allocate on first use during transport, deferred full MIDI validate may
never run under continuous playback, and partial SD tail reads succeed with edits silently cleared.

These are correctness and determinism issues — not style. They block the planned
**pool/playback hardening** track in [`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md)
and undermine HITL confidence in multi-slot + edit persistence.

## What Changes

- **Capture state guards:** overdub start is idempotent when already overdubbing; no second
  `beginCapture` that clears live capture store.
- **Slot loop resolution:** resolve `Slot.loopId` → `LoopPool` without silent fallback to slot 0;
  validate or repair IDs on SD load.
- **Temporal persistence round-trip:** restore persisted `startLoopTick` on load (today forced to 0).
- **editPasses memory:** deferred to **`pool-budget`** (heap admission + reclaim — not fixed row cap).
- **Deferred validate completion:** full `validateAndCleanupMidiEvents` runs within a bounded
  policy even when transport stays active (not only when all tracks idle).
- **Playback runtime prewarm:** allocate per-slot playback runtime and lazy `Loop` caches at setup
  or slot enable — not on first `playMidiEvents` tick.
- **Storage I/O integrity:** `readPersistedEditsTail` and related partial-read paths return failure
  instead of success with empty **editPasses**.
- **Native tests** for each fix; HITL only where timing/SD behavior changes.

**Non-goals (this change):**

- Jam capture (D13) — parked.
- Chunk pool budget / pass reclaim / undo memory trim — **`pool-budget`** change (separate).
- **`note-edit-session-undo-gpio`** — session **`E:`** / GPIO (separate; recommended before **`pool-budget`**).
- Full **LooperStateManager** / legacy `Looper` class cleanup.
- Removing `TRACK_OVERDUBBING → TRACK_OVERDUBBING` from the state machine if guard makes it unreachable
  (optional follow-up; default is guard-only).

## Capabilities

### New Capabilities

- `capture-state-guards`: Idempotent overdub entry; capture buffer ownership during track state transitions.
- `loop-temporal-persistence`: `startLoopTick` and deferred full-validate completion guarantees.
- `playback-runtime-prewarm`: Eager allocation of playback runtime and derived caches off hot paths.
- `storage-loop-io`: Fail-hard SD v4 tail reads; temporal field round-trip on load.

### Modified Capabilities

- `multi-loop-slots`: Slot-to-**Loop** resolution without silent pool-index-0 alias.
- _(none — **editPasses** admission moved to **`pool-budget`** `timeline-passes` delta)_

## Impact

| Area | Files (primary) |
|------|-----------------|
| Capture / state | `TrackStateMachine.cpp`, `Track.cpp`, `TrackManager.cpp`, `MidiButtonActions.cpp` |
| Slot resolution | `Track.cpp`, `LoopPool.cpp`, `StorageManager.cpp` |
| Persistence | `StorageLoopIo.cpp`, `StorageManager.cpp` |
| Pass lifecycle | `Loop.cpp` (no edit cap in this change — see **pool-budget**) |
| Idle / validate | `Track.cpp`, `main.cpp` |
| Playback alloc | `TrackPlaybackRuntime.h`, `Track.cpp`, `Loop.h`, `main.cpp` / `TrackManager::setup` |
| Tests | `test_storage_loop_io`, new `test_loop_ownership` or extend existing suites |
| Docs | `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` (delta only where behavior changes) |

Depends on shipped **m8-edit** / **timeline-pass-model**; does not block M8 archive.
