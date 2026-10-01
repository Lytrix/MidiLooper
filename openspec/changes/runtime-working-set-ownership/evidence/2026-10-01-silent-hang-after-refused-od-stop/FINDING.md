# Silent hang after a refused 24-bar overdub stop (2026-10-01)

## What was run, and why

A re-run of the S1.5 shape on the **fixed** image
(`eca0b0139d864734ae5a3164126c20e18cbde60eb2c6b437df851126f988fc5a`): track 5 Loop 1, 24-bar record +
24-bar overdub, through the authoritative wrapper — `scripts/hitl_stop_interruption.py --track-number 5
--loop-slot 1 --record-bars 24 --overdub-bars 24 --overdub-low-note 24 --overdub-high-note 39
--overdub-step-clocks 12 --overdub-gate-clocks 12 --tempo-bpm 120 --normal-stop` →
`captures/s15-24bar-02`.

The purpose was to exercise the **publication** side of `LoopSnapshotChangeSet`. The previous finding
([`../2026-10-01-boot-crash-loop-24bar-take/FINDING.md`], §5b) established that the *onset* fault of the original
regression was the wrap commit's publication, not a load, so the loaded-body verification did not cover it.

## Result 1 — no crash, but the wrap was never reached

- **0** `#CAPTURE_RECONNECT` in the whole run: no reboot, no fault.
- The record pass published and persisted its own work:
  `PERS,work,…,LoopPersist,slot:4:0,done,ok`, and the leg reported `persistence handoff: result=ok duration_us=43371`.
- `[923.583] Recording stopped @ tick 378 (recStart=0 length=18432)` — the 24-bar record.
- `[934.327] Overdubbing started @ tick 3771`.
- `[992.884] Overdub stop refused: stop_job_pending slot=0 interval=2 phase=16 dest=0 closure=0 recycle_owner=0
  recycle_started=0 undo_ready`
- **No `Overdub wrap committed` row.** Per DEC-069 a pending stop defers the seal and emits nothing
  (`src/Track/TrackOverdubWrap.cpp:32-40`), so the overdub was never sealed and its events stayed in the live
  capture store.
- The leg's own verdict was `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on a shed, with `overdub on/off/open/out/seq:
  0/0/0/0/0` and `Abort reason: !STOP_STATUS: response fence missing`. Per the standing rule the finding is read from
  the device's rows above, not from the verdict — which here gives the same answer.

**Consequence: the publication side of a 24-bar overdub seal is still unverified. This run is not evidence about it.**

## Result 2 — the record stop job reached its terminal phase and never retired

- `#STOP_STATUS,state=OVERDUBBING,pending=1,phase=16,dest=1` on **all 18** polls, 1009.1 s → 1039.1 s.
- `phase 16` is `RecordPublished` in `PendingStopPhase` (`include/TrackPlaybackRuntime.h:33+`) — a **terminal
  outcome** phase of the record route, not a step on the way to one.
- At the same instant: `#STOP_PREFIX,phase=0,window=0,attached=0,inputs=0,apply=1,passes=1,build_active=1,index=255,
  current=2,ready=255,build=0,retained=255` and `#STOP_HISTORY,phase=1,storage=5,current=5,history=0,
  current_history=0,rows=0,prewarmed=1,pass=0,pass_id=0,chunks=0`.
- That pending job is what refused the overdub stop and deferred the wrap.
- `interval=2` in the refusal: whether it labels that same job or the overdub interval is **not settled here**.

## Result 3 — the hang, and its class

- The last device row is `[Memory] ext,1042380989,phase=tick,live=6448KB,…,chunks_free=388,snap=1156KB,heap_free=152KB,
  heap_min=152KB,accounting=2` — uptime **1042.4 s**. After it: **0 bytes** in repeated 8 s / 20 s probes, with
  `/dev/cu.usbmodem154944801` **still enumerated**.
- No boot banner followed and no reboot occurred, so this was **not a fault**.
- A reset with `teensy_reboot` (no re-flash) then reported `#CAP,3005015,DIAG,boot_reset,crash=0` — **no
  `CrashReport`** — and `!CRASH_LOG`'s newest entry is `(system time) 19:12:45`, i.e. the *pre-fix* boot loop. The
  19:56 hang appended nothing.
- That is this repo's already-recorded class: `docs/Runtime/CURRENT_WORK.md`, 2026-09-17 — *"0 bytes in a 6 s probe
  with the USB port still enumerated … The hang is `abort()`, and it leaves no crash record."*

## The abort sites, and why the site is not yet identified

| site | on failure |
|---|---|
| `include/Utils/ExternalMemoryFirstAllocator.h:65` | `Serial.println(FATAL … exhausted!)`, `Serial.flush()`, then `abort()` |
| `include/Utils/InternalHeapFirstAllocator.h:82` | `Serial.println(FATAL … exhausted!)`, `Serial.flush()`, then `abort()` |
| `src/Loop/LoopSnapshot.cpp:233` (`slab_allocate`) | `emitSnapshotAllocFailure(…)` → **capture ring only**, then `abort()` |

- Neither FATAL line appears in any capture of this session, and both flush before aborting, so a plain
  pool-exhaustion abort through the first two is **not supported**.
- The third is **silent over serial by construction**: its only report is a ring row
  (`src/Loop/LoopSnapshot.cpp:1490-1504` calls `DebugSessionCapture::appendCaptureTextLine` and nothing else), the ring
  is RAM, and after `abort()` nothing ever drains it again. This run also shed the ring twelve times
  (`RING,shed` at 1/2/4/11/38/54/111 entries, one of them at 1038.5 s). **Absence of `snapshot_slab_fail` is therefore
  not evidence of absence.** It is, however, the only abort in the path this run exercises: the run's own rows show
  `DIAG,snapshot_unit,…` active and `snap=1156KB`.
- A blocking or spinning loop in the stop path is **not excluded** either; the class is identified, the site is not.
- **The diagnostic defect:** the one abort whose report cannot reach the host is the one that matters most. Recommended
  (not implemented here) — `emitSnapshotAllocFailure` should write its **already-built** `line` to `Serial` as well as
  the ring. That adds no new string literal, so the RAM1 guard's 496 B of headroom is untouched.

## Cost, and the device's state after the reset

- The hung core held this run's unsealed overdub content; it was lost with the reset.
- After the reset the device is healthy and the library loads: `Current workspace loaded successfully`, `BOOT,load,ok`,
  `crash=0`, no shutdown latch (`captures/.device-shutdown-incomplete.json` absent).
- `!CAPTURE_CONTENT` on track 5 Loop 1: `CAPTURE_CONTENT,begin,track=5,slot=0,length=18432`,
  `rows,record,0,1144`, **no `overdub` row**, `rows,effective,0,1144`. So the slot holds this run's 24-bar record pass
  and no overdub — the same shape as the artifact it replaced.

## Why the job never retired (diagnosed 2026-10-01, read-only)

The device row names the predicate. `RecordPublished`'s own doc comment says the job "awaits bounded retirement"
(`include/TrackPlaybackRuntime.h:66-68`). Retirement is `Track::stepDeferredRecordRetirement`, and it returns `Pending`
on exactly one wait (`src/Track/TrackOverdubLifecycle.cpp:1815-1818`):

```
const bool intervalHasGeometry = loop.loopLengthTicks != 0;
if (runtime != nullptr && runtime->frozenWindowBuildActive() && intervalHasGeometry) {
  return LoopEventStoreRewriteResult::Pending;
}
```

`frozenWindowBuildActive()` is literally `viewBuild.active()` (`src/Track/TrackPlaybackRuntime.cpp:1878-1880`) — **the
same field `#STOP_PREFIX` prints as `build_active`**. Every poll reported `build_active=1`, so the device's own row
states the blocking predicate directly, and `loop.loopLengthTicks` is 18432, not the zero of a filtered-out empty take
that this guard exists for.

The build's only advancer is `serviceFirstRecordingBar`, and its only caller gates on `isRecording()`
(`src/Track/TrackDeferredMaintenance.cpp:991-1000`):

```
if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache && isRecording()) {
  ...
  if (runtime->serviceFirstRecordingBar(activeLoop, source, Config::TICKS_PER_BAR, ...)) return;
}
```

`Track::isRecording()` is `trackState == TRACK_RECORDING` exactly (`src/Track/TrackStateFacade.cpp:124-126`), and
`TRACK_OVERDUBBING` is a different state (`include/TrackState.h:18-21`). The device's state at every poll was
`OVERDUBBING`. So once the record has stopped, the advancer can never run again, `viewBuild.active()` can never clear,
and the retirement returns `Pending` **forever** — for all 30 s it was observed, and for as long as that state lasts.

**The wait is silent by design.** The other early-outs in `processDeferredOverdubStopGrain` were given a
`noteStallOnce(...)` diagnostic precisely for this wedge — DEC-070's own words: *"a predicate that can never become true
wedges the slot (no stop is ever admitted again) … an aborted HITL run left exactly this state"*
(`src/Track/TrackOverdubLifecycle.cpp:1845-1852`) — and this run emitted **no** `Deferred stop stalled:` row, which is
consistent with this wait having none. DEC-070's bounded fallback (`:1936-1943`) covers the *overdub* route only
(`retargetedFromPlaying && phase >= CanonicalTail`); the record route has **no bound** on this wait.

That is the documented consequence, observed here: the wedged job holds its raw range and the capture-stop owner, so
**no stop on the track is ever admitted again** — the overdub stop was refused `stop_job_pending`, and per DEC-069 the
wrap seal was deferred, so the overdub pass was never sealed (Result 1).

**Proven vs inferred.** Proven: the wait and its predicate, the device row that reports it, the advancer's
`TRACK_RECORDING` gate, the observed state, and the absent diagnostic. **Inferred, not proven:** *why a build was in
flight at the record stop at all.* The advancer is one grain per `VisualCache` idle turn against a take that grows to
24 bars (1,144 events) on a loaded device, so the build plausibly could not finish inside the take; that needs the
build's own telemetry or a targeted repro, and it is the remaining question.

**Attribution:** pre-existing. The wait is from `9b815bf5` and the `isRecording()` gate from `33027ca3` (`git log -S`),
and neither `TrackOverdubLifecycle.cpp` nor `TrackDeferredMaintenance.cpp` is touched by any commit after `61e08bda`.

## Open

1. **Why a build was still in flight when the record stopped** (the one inferred link), then the fix: either bound this
   retirement wait the way the overdub route bounds its fallback, or advance/discard `viewBuild` for a record job whose
   interval is no longer `TRACK_RECORDING`. A `noteStallOnce` on this wait is the cheap first step, because the wedge is
   currently invisible from both the host and the device log.
2. **The hang's abort site** — needs the loud diagnostic above, or a debugger. Not attributed to any of the three sites.
3. **The publication side of a 24-bar overdub seal** remains unverified (Result 1) — and this wedge is what prevents
   reaching it.

## Fix (2026-10-01, DEC-107)

The retirement wait is now bounded in DEC-070's shape. `Track::stepDeferredRecordRetirement` carries a grain budget
over the wait with an **unchanged frozen-window build phase**, read through the new
`LoopPlaybackRuntime::frozenWindowBuildGrain()` (`viewBuild.nextGrain()`) — a phase change is progress and resets the
count, so a build a writer is still feeding is never cut, while a build no writer can advance is bounded. Past the
bound the existing `releaseFrozenRecordWindow()` discards the stale build **before**
`retireJobCaptureSourceGrain` releases the raw range, so the use-after-free the S4f.19 gate exists to prevent is
still prevented; only the completion of a window that retirement discards anyway is given up.

The counter lives on `LoopPlaybackRuntime` (extmem) because `Track` is the pinned eight-way DTCM array
(`sizeof(Track) == 384`): the per-slot runtime's pin was re-based `6,352 -> 6,360` B with the reason recorded at the
assert, and DTCM `variables` stayed at 96,192.

**Verification.** New fixture `test_stalled_frozen_window_build_does_not_park_the_record_job` (same setup as
`test_published_record_interval_does_not_replay_the_return_window`, reaches `RecordPublished`, forces
`TRACK_OVERDUBBING`, starts a build nothing will advance, asserts retirement still yields once — the gate is not
weakened — then that it completes and drains the ring within a generous bound, with the build discarded).
**Red-proof:** replacing the bound with the pre-fix unconditional `Pending` fails it with
`Expected 1 Was 0. a frozen window build nothing advances parked the record job`; the file was restored
byte-identically (`8085d94b…`). Full native **2,668 cases / 2,661 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` SUCCESS, RAM1 `padding 1,272` (floor 840, 432 B headroom).

**Owed.** Device acceptance: the same 24-bar record + 24-bar overdub shape must now show the record job retiring
and the overdub pass present. And the inferred link above — why a build was still in flight at the record stop —
remains open.

