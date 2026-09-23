# Persistence immediacy and CLEAR latency

**Type:** Refinement ([DELIVERY_RULES](../Authority/DELIVERY_RULES.md) work type "Bugfix / refinement").
**Owner:** `StorageManager` deferred-save scheduler, `PersistenceQueue`/`MidPassChunkPersist`, and the
CLEAR action in `MidiButtonActions`.
**Requested by the owner (2026-09-23):** *"any recording should persist asap in case of a hang or power
failure just right after so it can preserve part of the recording"* and *"clear should be immediate, so
anything that can speed it up would help"*.
**Status:** part 1 landed and timing-verified but **not yet effective** (one invocation gate remains);
part 2 diagnosed, not yet changed; a content pass was run and is inconclusive (setup class).

Sibling work: [`sd_temp_write_truncation_refinement.md`](sd_temp_write_truncation_refinement.md) (the
`index.bin` corruption fix that unblocked the catalog).

---

## Part 1 — persist a take as it is recorded

### The mechanism already exists; only its invocation was missing

- `LoopEventStore::sealChunk` admits every sealed capture chunk to `PersistenceQueue::admitSealedChunk`
  (`src/LoopEventStore.cpp`), so sealed chunks are queued **during** a take.
- `StorageManager::stepMidPassChunkPersist` writes one queued chunk and emits
  `PERS,mid_pass,<sealSequence>,<chunkId>,<outcome>` on every write
  (`src/StorageManager/MidPassChunkPersist.cpp`).
- That writer sits **inside** `StorageManager::processDeferredSaveState`, *behind* the transport gate:

  ```cpp
  if (isTransportActiveForPersistence()) { return; }   // was: blocks the writer during RECORDING
  ```

  and `isTransportActiveForPersistence()` is true whenever any track is audible **or recording**
  (`src/StorageManager/Internal.cpp`). So the incremental path could never run while a take was live —
  a take existed only in RAM until the stop, which is exactly the loss window the owner described.

### Landed

`src/StorageManager/DeferredSaveScheduler.cpp`: the transport gate now carries one deliberate exception —
a FLASH-resident `flushOneQueuedCaptureChunkDuringTransport()` that flushes **at most one already-sealed
capture chunk per turn**, keeping two guards: it does not run while a `SlotLoadSession` owns the SD session
(overlapping a load parse with a write hung once, 235129), and it is **not** starved by a workspace save
that is itself parked — that parked-save state is what made a take unrecoverable. The multi-frame
workspace/bundle writers stay parked, because those are the ones that produced the 90–598 ms display gaps
(`session_20260912_213530`) the gate was added for.

Firmware SUCCESS; `code 392,328 / padding 888` (48 B above the enforced floor, i.e. this cost 64 B of
ITCM against the previous 952). Device: the risk check passes — a 24-bar take reports
`VERIFY MIDI-boundary timing: … overflow_free=True call_sites=True`.

### NOT yet effective — the remaining gate is in `main.cpp`

`PERS,mid_pass` **never appeared** in two device attempts (an 8-bar take seals no chunk; a 24-bar take
seals one, and still nothing flushed). The reason is that the scheduler is not *invoked* during a take:

```cpp
// src/main.cpp
const bool deferredSavePending = StorageManager::hasDeferredSaveWork();
const bool runDeferredSaveState =
    deferredSavePending &&
    (allowPrioritizedWorkspaceSaveDrain ||
     (deferredIdleOwner == DeferredIdleOwner::Persistence && deferredIdleOwnerAllowed &&
      allowDeferredSaveAfterLoadFrame &&
      MidiServiceDrain::allowDeferredSave(postOverdubPlayingMidiDrain, deferredSavePending,
                                          overdubStoppingForMidiDrain)));
if (runDeferredSaveState) { StorageManager::processDeferredSaveState(...); }
```

So the flush needs its **own invocation reason** during a capture, with its own timing protection
(the one-chunk-per-turn bound is already in place). Next step, concretely:

1. Add a "queued capture chunks" predicate to the `runDeferredSaveState` condition (or a sibling call),
   so a non-empty `PersistenceQueue` can run the flush while a capture is live.
2. Keep the existing timing protection for *bundle* work; only the chunk flush is admitted.
3. Verify on device with a take long enough to seal a chunk mid-take (24 bars at the base pattern's
   ~16 events/bar seals one at ~16 bars): `PERS,mid_pass` must appear **between** `EMPTY->RECORDING` and
   `RECORDING->STOPPED_RECORDING`, with `overflow_free` still true.

### Invocation reason also landed — and the flush still does not fire

`hasDeferredSaveWork()` is `hasPersistenceWorkPending()`, i.e. the **workspace** save, so with only capture
chunks queued the scheduler was never entered. Added:

- `StorageManager::hasQueuedCaptureChunksToPersist()` (`include/StorageManagerStatusQueries.inl`,
  declared in `StorageManager.h`) — `PersistenceQueue::queueDepth() > 0 || writingChunkCount() > 0`,
  deliberately separate from the workspace predicate.
- `src/main.cpp`: `runDeferredSaveState` now also enters on that predicate. This is bounded by
  construction — with the transport active `processDeferredSaveState` flushes at most one chunk and
  returns, so no bundle work can run in a take window. Build clean at the **same** `padding 888`.

**Device: still 0 `PERS,mid_pass`.** A 24-bar take sent 384 notes + 384 CCs (768 events, ≥3 chunks) and
reports `overflow_free=True`, but no flush row appears inside the take window. What is established:

- `LoopEventStore` **does** seal chunks mid-append (`chunk(tailId).used >= CHUNK_CAPACITY` → `sealChunk`,
  and the post-append check), so sealing is not the blocker.
- The only branch that skips admission is `isEphemeralSeal()`, and `enterEphemeralSeal()` is used **only**
  by `Loop::rematerializeEditView` (`LoopMaterialization.cpp`) — not by the live capture. So a record
  pass's chunks should reach `PersistenceQueue::admitSealedChunk`.
- `stepMidPassChunkPersist()` returns early only when the queue is empty, otherwise it writes and emits
  `PERS,mid_pass`.

So the remaining question is narrow and testable, and it is the one to answer next: **does
`admitSealedChunk` actually accept a record pass's chunks, or does the flush guard
(`deferredSaveBlockedByActiveSlotLoadSd()`) return early every turn?** The second is the more likely and it
is the *same gate* that blocks CLEAR — a lingering/active `SlotLoadSession` would explain both symptoms at
once. The next diagnostic is to make the admission outcome observable (a counter on
`admitSealedChunk`'s result, or a `PERS` marker when the flush guard declines) rather than guess further.

**Coverage limit:** no native suite links `DeferredSaveScheduler.cpp` (`grep -rl DeferredSaveScheduler test/`
→ 0), so this path is device-verified only. A native fixture would need the scheduler plus the queue.

## Part 2 — make CLEAR immediate

### Diagnosis: the drain waits for **all** tracks, but CLEAR silences only one

`MidiButtonActions`'s clear path:

```cpp
if (StorageManager::hasDeferredSaveWork()) {
    const bool wasPlaying = track.isAudiblePlaybackActive();
    if (wasPlaying) track.stopPlaying();           // stops the SELECTED track only
    logger.info("Clear waiting for deferred save completion");
    if (!StorageManager::saveState(...)) { ... "Clear aborted: could not complete deferred save first" }
}
```

but the blocking drain runs `processDeferredSaveState`, whose first gate is
`isTransportActiveForPersistence()` — true while **any** track is audible or recording. With the restored
library's other tracks playing, the gate stays true no matter how long CLEAR waits, the drain spins its
whole stuck budget, and CLEAR aborts. That is the measured
`Persistence drain stuck after 33 iterations (max 33, expected ~1 slice steps, ~512 SD bytes)` followed by
`Clear aborted: could not complete deferred save first`, and it also explains the intermittency: the
outcome depends on whether anything else happened to be sounding.

### Options, with the trade-off stated

1. **Retire the guard for the cleared slot** (preferred product behaviour): CLEAR stops waiting for the
   global save, supersedes the pending work for the slot it is resetting, clears immediately, and lets the
   post-clear state be persisted by the normal path. The guard exists for one stated reason — *"so the
   writer cannot read a slot while it is being reset"* — so it has to be preserved as a **per-slot**
   supersession, not as a global wait. This is the option that makes CLEAR immediate without touching the
   timing gate.
2. **Silence the gate** (small, but product-visible): stop every audible track before the drain and restart
   after. Rejected as the primary fix because the architecture rule is that the looper never stops playing;
   it would trade a slow CLEAR for an audible gap on the other tracks.
3. **Speed the drain itself** (micro): a larger slice budget, or fewer/larger grains. It cannot help while
   the gate blocks, so it is only worth doing on top of option 1.

**Note the synergy:** the more a take is persisted as it is recorded (part 1), the less work is pending
when CLEAR runs, so part 1 also shortens CLEAR — but it cannot remove the gate, which is why option 1 is
still required for "immediate".

## Part 3 — the content pass (2026-09-23)

`host_midi_hitl.py run --preset base --track 5 --loop-slot 1 --midi-channel 5` →
`captures/content-pass-01/`. Result **INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)**: `record loop length
1536/1536` and `persistence handoff result=ok duration_us=74940` (`loop_persist=691 sliced=True ok=True`)
are healthy, but `stored overdub span count=0/15 … count_ok=False`, i.e. the overdub phase did not
register. Per the change's standing rule this class is **retried, never reinterpreted**, so no content
verdict is claimed here and the pass needs a re-run.

## Safety

Device SD is user data: nothing outside the approved scratch slot (track 5 / slot 1, and the approved
track 6 range) has been written, renamed or deleted.
