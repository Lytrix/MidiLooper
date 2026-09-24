# Persistence immediacy and CLEAR latency

**Type:** Refinement ([DELIVERY_RULES](../Authority/DELIVERY_RULES.md) work type "Bugfix / refinement").
**Owner:** `StorageManager` deferred-save scheduler, `PersistenceQueue`/`MidPassChunkPersist`, and the
CLEAR action in `MidiButtonActions`.
**Requested by the owner (2026-09-23):** *"any recording should persist asap in case of a hang or power
failure just right after so it can preserve part of the recording"* and *"clear should be immediate, so
anything that can speed it up would help"*.
**Status (2026-09-23, final): BOTH PARTS LANDED AND DEVICE-VERIFIED.** Part 1 (persist a take while it is
recorded) is verified on device — **13 `PERS,mid_pass` rows**, including inside live `OVERDUBBING` and
`PLAYING` windows, each `ok:t4:s0:cNN:n256`, with `overflow_free=True`; the mid-document "not yet effective"
notes below are the earlier rounds, kept for the diagnosis. Part 2 (CLEAR latency) was resolved by
**DEC-081** — the owner ruled the all-tracks-silent gate comes off rather than that every track must stop —
and is device-verified with five tracks audibly playing (the clear's blocking drain completed in **<833 ms**,
0 `Clear aborted` / `drain stuck`). The content pass also PASSES (see Part 3 CORRECTED).
**Process gap:** this work landed on plan docs + code with **no OpenSpec change**; the *restore*-liveness
half of the same defect cluster was handled by the change **archived 2026-09-24** as
[`2026-09-24-deferred-slot-restore-liveness`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/proposal.md)
(reduced scope), and
**no OpenSpec change yet covers this save/immediacy half** — recorded in
[PROJECT_STATE](../Runtime/PROJECT_STATE.md) and [CURRENT_WORK](../Runtime/CURRENT_WORK.md).

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

### RESOLVED by DEC-081 (2026-09-23): the gate is removed

The owner ruled that the "all tracks silent" gate is **superseded** by *never require stopping, always keep
playing*: a slot may be stopped on the selected track as it is being emptied, but every other playing track
keeps playing. Implemented:

- `isTransportActiveForPersistence()` → **`isCaptureRecordingForPersistence()`**, testing `track.isRecording()`
  only; all four call sites updated.
- `BlockedByTransport` → **`BlockedByCapture`** in the drain-gate classifier.
- **The slice budget is deliberately retained:** `resolvePersistenceSliceBudgetUs` still receives "anything
  sounding or recording" as `transportBudgetActive`, so playback keeps `maxPersistenceMicrosActive`
  (**300 us**) instead of the idle fallback (**3,000 us**). Removing the gate without this would have made SD
  bursts ten times longer during playback and reintroduced the display stalls the active budget caps.
- The clear still stops the *selected* track before resetting its slot, which the owner explicitly allows;
  other tracks are untouched and `restorePlaybackAfterSlotClear` restarts the cleared track if another
  enabled slot has content.

**Verification:** built (SUCCESS, `code 392,328 / padding 888`); full native 2,289 / 1 skipped / 0 failures;
harness suites OK. Device: **0** `Clear aborted` / `Persistence drain stuck` / `Persistence drain blocked`
across five runs, and a content pass reports `Result: PASS` with `overflow_free=True`.

**The discriminating case IS demonstrated (2026-09-23, final round).** Earlier attempts failed for two
recorded reasons — a transport tap that *stopped* an already-running transport (so the gate was satisfied for
the old reason), and a sample in which no other track happened to be sounding. Both were fixed by toggling
the transport until serial confirms `Transport started` before measuring. With **five tracks audibly playing**
(1-based channels 1, 2, 3, 4, 6) and the approved scratch slot selected, two clears were issued so the second
meets the save the first one owed:

```
[283.028] [INFO] Clear waiting for deferred save completion      <- the drain block engages: a save IS owed
[StorageManager] Draining persistence work queue to SD card...   <- the blocking drain runs
[StorageManager] State saved successfully (v4).                  <- it COMPLETES
[283.861] [DEBUG] Clear ignored — track is empty                 <- the clear proceeds
aborts: 0
```

- The drain **completed** in **<833 ms** (press-release to completion) where the old gate produced
  `Persistence drain stuck after 33 iterations` followed by
  `Clear aborted: could not complete deferred save first`. **0** `Clear aborted` / `drain stuck` /
  `drain blocked` in the window.
- The other tracks **kept playing**: musical note-ons on 1-based channels 1, 0-based, 5, 3, 2 sampled before
  and after the clears (only channel 15's LED feedback is non-musical).
- `VERIFY MIDI-boundary timing: … overflow_free=True` on the same build.

**Timing observation, pre-existing and worth its own look.** `DIAG,persist_save` (the per-loop time inside
`processDeferredSaveState`) still reaches **~40 ms** maxima: 39.6 ms before this change
(`content-pass-02`) and 43.3 ms after (`dec081-01`), i.e. the change did **not** introduce it — a single
persist *grain* is not bounded by the slice budget, only the loop is. That is the family the 90–598 ms
display-gap history belongs to, so it is recorded as a standing item, not a DEC-081 regression.

### Options that were considered before the ruling

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

## Part 1 VERIFIED ON DEVICE (2026-09-23, later) — chunks do persist during a live take

The earlier "still 0 `PERS,mid_pass`" result was a **fixture artifact**: `--record-only` never commits the
pass, so its chunks are released and never admitted. With the authoritative full shape
(`captures/content-pass-02/`, `--record-bars 4 --overdub-bars 2`) there are **13 `PERS,mid_pass` rows**,
and they land *inside live transport windows*:

| phase window (us) | flushes inside it |
|---|---|
| `RECORDING` 569,394,225 – 578,086,677 | - |
| **`OVERDUBBING` 579,411,336 – 597,402,732** | **584,590,680 · 584,617,581** |
| **`PLAYING` 558,459,317 – 598,411,707** | **598,393,317 · 598,402,101** |

Each row is `PERS,mid_pass,0,<sealSequence>,<chunkId>,ok:t4:s0:cNN:n256` — for the slot under test, 256
events per chunk — and the run still reports
`VERIFY MIDI-boundary timing: intervals={'OVERDUBBING': 2, 'OVERDUBBING_STOPPING': 2, 'PLAYING': 17,
'RECORDING': 1} stopping=2 overflow_free=True call_sites=True`. So a take now reaches SD **while it is being
recorded**, which is the owner's ask.

**So no further diagnostic is needed for part 1**: `PERS,backlog` already carries the chunk queue depth
(3rd field: `workQ,writingItems,chunkQ,...`), and the phase windows plus `PERS,mid_pass` answer the question
directly.

## Part 3 CORRECTED — the content pass PASSES, and the owner's read was right

The owner observed that `stored overdub span … count=0/15 … count_ok=False` looked like a logging/validation
error rather than a storage failure. **That is exactly right, and it is now proven.**

`captures/content-pass-02/` (full shape, 4-bar record + 2-bar overdub) reported
`VERIFY stored overdub span (SEVT ticks): count=0/15 … count_ok=False` and
`Result: FAIL (FIRMWARE_CONTRADICTION)`. The authoritative `!CAPTURE_CONTENT` dump of the same slot says
otherwise:

```
CAPTURE_CONTENT,begin,track=5,slot=0,length=3072
CAPTURE_CONTENT,rows,record,0,188
CAPTURE_CONTENT,rows,overdub,0,48
CAPTURE_CONTENT,rows,overdub,1,24
CAPTURE_CONTENT,rows,effective,0,260
```

The content-verification pipeline agrees:

```
result = PASS
storage_matches_effective = true
effective_rows_all = expected_note_cc_rows = 260
cycles: 3  (each rows=260, inventory_equal=true, exact_order=true, missing=0, extra=0)
```

Evidence: `captures/cp02-playback/content-review.json`.

**Why the base run said otherwise.** `stored_overdub_span` carries `sevt_missing: true` — the harness knows
the **SEVT telemetry dump** is missing, and derives `count_ok/span_ok/bars_match_ok` from a dump that never
arrived. The harness itself documents this as temporary
(`legacy_record_baseline.py`: *"TEMPORARY (2026-09-16): the stored-MIDI (SEVT) dump is not reliably armed by
the canonical base flow … a missing SEVT dump must not by itself turn an otherwise valid run into
INCONCLUSIVE_CAPTURE"*, `sevt_gate_required = False`), and the firmware carries a matching temporary note
(`TrackDeferredMaintenance.cpp`: *"TEMPORARY DEVICE DIAGNOSTIC … once SEVT is restored to a required gate"*).
This run's own `VERIFY issues` are `record_stored_revt_missing` and
`persistence_active_snapshot_unavailable_during_save` — **also telemetry/observation classes, not content**.
So the `FIRMWARE_CONTRADICTION` verdict for this run is a **telemetry false negative**, and the standing
guidance applies: prove content with the content-verification pipeline, not the base run's own verdict.

## Part 3 (original) — the earlier content pass (2026-09-23)

`host_midi_hitl.py run --preset base --track 5 --loop-slot 1 --midi-channel 5` →
`captures/content-pass-01/`. Result **INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)**: `record loop length
1536/1536` and `persistence handoff result=ok duration_us=74940` (`loop_persist=691 sliced=True ok=True`)
are healthy, but `stored overdub span count=0/15 … count_ok=False`, i.e. the overdub phase did not
register. Per the change's standing rule this class is **retried, never reinterpreted**, so no content
verdict is claimed here and the pass needs a re-run.

## Safety

Device SD is user data: nothing outside the approved scratch slot (track 5 / slot 1, and the approved
track 6 range) has been written, renamed or deleted.
