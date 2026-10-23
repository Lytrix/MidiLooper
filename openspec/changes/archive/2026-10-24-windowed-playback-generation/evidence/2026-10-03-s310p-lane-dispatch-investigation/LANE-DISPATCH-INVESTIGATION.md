# PlaybackViewMaintenance lane — dispatch path, starvation, observability (read-only report)

Repo: /Users/eelkejager/Documents/PlatformIO/Projects/250513-215524-teensy41
Branch: feature/windowed-playback-generation, HEAD b67804c7

## 1. Dispatch path(s) and every skip guard

Lane identity/order: `MidiServiceDrain::DeferredIdleOwner` Snapshot=0, VisualCache=1,
VisualCacheWrapAppend=2, Persistence=3, LoadAndDisplay=4, SlotProvision=5,
PlaybackViewMaintenance=6 (include/Utils/MidiServiceDrain.h:14-29), kDeferredIdleOwnerCount=7 (:114).

Call sites (VERIFIED by grep over src/ + include/):
- The ONLY production caller of the lane body is src/main.cpp:499, inside the
  `deferredOwner == PlaybackViewMaintenance` block src/main.cpp:477-503.
- The offer read: src/main.cpp:484 (`playbackWindowServiceOffer`).
- `Track::stepPlaybackViewMaintenance` def src/Track/TrackPlaybackWindowBuild.cpp:1806.
- `Track::playbackWindowServiceOffer` def :1763; `Track::fillPlaybackHolders` :1730.
- `Track::hasPendingPlaybackViewMaintenance` def :1718 — **no production caller** (tests only).
- `Track::requestPlaybackViewMaintenance` def :1659 — callers only `requestPlaybackWindow` (:1914/:1950)
  and `requestPreviewWindow` (:1954/:1986); **neither has a production caller** (tests only).
- The rotation state is one static: src/main.cpp:38, taken once per iteration at :865 via
  `takeDeferredIdleOwnerTurn` (:78-83).

Cross-lane coupling (VERIFIED code-level): eligibility is `coverageBuildNeedsService() == viewBuild.active()`
(include/TrackPlaybackRuntime.h:812/834; used at TrackPlaybackWindowBuild.cpp:1722/1739). `viewBuild` is the
single shared build slot (include/TrackPlaybackRuntime.h:438) and is ALSO begun/steps by two other drivers:
`serviceFirstRecordingBar` (src/Track/TrackPlaybackRuntime.cpp:1905-1943, called from
src/Track/TrackDeferredMaintenance.cpp:1005) and `serviceFrozenRecordWindow`
(src/Track/TrackPlaybackRuntime.cpp:1804-1842, called from src/Track/TrackPlaybackHotPath.cpp:531).
`fillPlaybackHolders` does not test the fill scope (RawCoverage/RecordCoverage/Coverage), so a record-lane
build makes the *window* lane eligible and the window lane steps its grains. This is the most plausible
reason the `win_gap` sampler produced ~500-1250 opportunities in device runs 1-6 with no production window
requester. Whether that sharing is intended is UNKNOWN from code alone.

Guards/branches that can SKIP the lane for one iteration (each VERIFIED):
1. src/main.cpp:806 `if (StorageManager::bootRecoveryBlocked()) return;` — whole iteration.
2. Rotation: exactly one owner per iteration (include/Utils/MidiServiceDrain.h:171-189, cursor advanced at
   :180-181). The lane is 6-of-7, plus an **extra Snapshot iteration** inserted when
   `snapshotBacklogPending && selected == LoadAndDisplay` (:186) or when
   `sessionExitDrainPending && selected != Snapshot` (:182-183) — each delays the lane by one more iteration.
3. src/main.cpp:878-879 `deferredIdleOwnerAllowed = allowDeferredIdleOwner(owner, wrapAppendPending)`;
   false only for VisualCache/LoadAndDisplay (header :199-209) — the lane is never gated by it.
4. src/main.cpp:893 `if (deferredIdleOwnerAllowed) { runDeferredIdleMaintenance(...) }`.
5. Inside the lane block: src/main.cpp:493-501 — the body is called **only when the round-robin selection
   returns `Service`**. `None` (nothing owed) and `AllBlocked` skip the call entirely (no sample, no
   progress). `AllBlocked` is currently unreachable because `holder.canProgress = true` is hard-coded
   (TrackPlaybackWindowBuild.cpp:1759, policy at header :346-370).
6. Offer emptiness: no runtime for the slot (:1738), or `!coverageBuildNeedsService()` (:1739), or
   `viewSetIfPrewarmed() == nullptr || lifecycle.build >= kViewCount` (:1742-1745).
7. Offer cap: src/main.cpp:481 + `kWindowHolderOffers = 8` (:51) — tracks with index >= 8 are never
   offered, i.e. structurally unserviceable (no lane starvation of others).
8. Cursor normalisation src/main.cpp:491 `sWindowHolderCursor.next %= offerCount`.
9. Earlier returns inside `runDeferredIdleMaintenance` (:465-468 SlotProvision, :504-507 Persistence/
   LoadAndDisplay) are keyed on other owner values and cannot skip the lane on its own turn.
10. The lane returns at src/main.cpp:502 **before** the all-stopped guard (:524-530) and before the
    stopped/realtime routing — so it is reachable in every track state (playing, recording, stopped).

## 2. Paths that can hold the loop between two lane dispatches (ranked)

Structural fact: the lane's opportunity is 1 iteration in 7 and the rotation advances one owner per
iteration regardless of how long the iteration took (header :180-181). Every iteration pays the whole loop
body AFTER the lane turn (src/main.cpp:805-1064), so N seconds unserved = N seconds of loop body.

1. Per-iteration spans measured on device (run 6, s310m §6): `load_frame` 501 188 us,
   `idle_maint` 125 529 us, `persist_save` 44 559-120 693 us. 6-7 such iterations between two lane turns
   is 3-7 s of lost opportunity. Sources:
   - `idle_maint` = src/main.cpp:893-899 (whole deferred-idle call).
   - `load_frame` = src/main.cpp:919-929 -> `runDeferredLoadAndDisplayFrame` (:258-455), including
     `DeferredJobScheduler::runFrame(budgetUs)` (:379; src/DeferredJobScheduler.cpp:15-19 ->
     `StorageManager::selectSubmittedLoadJobs` / `stepSubmittedLoadJobs`,
     src/StorageManager/LoadLoopJob.cpp:1017/1028) and boot-commit block (:422-449).
   - `persist_save` = src/main.cpp:955-988 -> `StorageManager::processDeferredSaveState`
     (src/StorageManager/DeferredSaveScheduler.cpp:44), whose work loop is
     `while (!sliceBudgetExhausted())` (:116) with SD I/O inside; also reached every iteration during a
     take through `queuedCaptureChunks` (src/main.cpp:972-980) -> `flushOneQueuedCaptureChunkDuringTransport`
     (DeferredSaveScheduler.cpp:69-72).
   - Budgets are per-slice, not per-grain: LoadLoopBudget::FocusRestoreUs 1200 us / BackgroundRestoreUs 250
     / BootTitleRestoreUs 5000 (include/LoadLoopBudget.h:12-16); a single SD grain can exceed the budget
     (hence the measured 501 ms frame).
2. Blocking USB serial writes: src/main.cpp:847 `SC_CAPTURE_FLUSH(timingCriticalTrackActive ? 8 : 64)` ->
   `flushCaptureBuffer` bounded only by record count (src/Utils/DebugSessionCapture.cpp:1795-1840,
   `while (flushed < maxRecords ...)` :1823). The code's own comment records that `availableForWrite` does
   not prevent `usb_serial_write` from blocking and that a soft-lock followed (DebugSessionCapture.cpp:
   1799-1805). Instrumented builds also emit per-second rows (src/main.cpp:1034-1037 ->
   MemoryMonitor.cpp:409-435) and a 4-second owner census (src/main.cpp:1041-1046 ->
   `logWorkingSetOwners`, MemoryMonitor.cpp:400-435, a ~200-char `Serial.printf_P`). This path is
   unbounded by construction (host-paced).
3. External-clock catch-up on resume (ClockManager external path `currentTick += TICKS_PER_CLOCK`):
   measured `clk_batch` max 42 pulses = 336 ticks = 44% of the declared 768 (s310m §1/§3) — an amplifier of
   a stall in the measured quantity, not a cause of losing lane turns.
4. Display: `DisplayManager::update` (src/DisplayManager.cpp:320-371) runs one paint phase per call
   (Resolve/Draw/Transfer; `pollDisplayTransfer` :323) — ms-scale, already sliced by
   DisplayWindowUtils; also called in the loop prefix (:95, :118) and in the load frame (:347, :359, :415).
5. O(tracks x slots) scans per iteration: src/main.cpp:59-66 `anySnapshotPublicationBacklog` (called at
   :81 every iteration), :68-76 `anyPendingSessionExitSnapshot`, :835-840, :857-863, :868-877,
   :908-911; `trackManager.drainEdgeEffects()` :824; `notePostRemainderWindows` :142-147 (all tracks, two
   per-track notes, SESSION_CAPTURE) at :906.
6. Heap/pool walks: src/main.cpp:1018 `MemoryMonitor::logStatus()` (60 s, gated on
   `!timingCriticalTrackActive`; the 593 ms pool walk is named at MemoryMonitor.cpp:457-462); per-second
   `logExternalPoolAccounting` (:1034); 4-second `logWorkingSetOwners` (:1041). Per PSRAM alloc/free,
   `ExternalPoolAccounting::recordPoolAllocation/recordPoolFree` walk up to kShadowCapacity = 8192 probes
   (src/Utils/ExternalPoolAccounting.cpp:114/182).
7. Pressure reclaim per iteration: src/main.cpp:945-953 ->
   `TrackManager::tryReclaimDerivedViewCachesUnderPressure` (src/TrackManager/TrackManagerMemoryPressure.cpp:
   25-69: O(NUM_TRACKS^2) priority sort + NUM_TRACKS x MAX_LOOPS_PER_TRACK slot walk) and
   `reclaimUnreferencedDisabledPasses` (:71).
8. MIDI drain: src/MidiHandler.cpp:218-, bounded batches (`while (count < midiInputBatchMax && usbMIDI.read())`
   :239, DIN :272, `usbHost.Task()` :293), called 5x per iteration (src/main.cpp:813, 888, 903, 935, 992).
9. Per-bar: `SC_UPDATE` emits one BAR row per bar (src/main.cpp:830; DebugSessionCapture `update`) — cheap.
Non-causes: `Looper::update()` is empty (src/Looper.cpp:61-62).

## 3. Is starvation possible by construction?

VERIFIED: yes. There is no deadline, no catch-up and no bound on iteration duration. The lane's turn is
one of seven per iteration (header:171-189) and the cursor advances by one per iteration irrespective of
elapsed wall-clock (:180-181), so a long iteration, or a run of slow ones, directly removes lane turns with
nothing to compensate. Because 6 other iterations lie between two lane turns, a per-iteration cost of up to
~0.5 s (measured `load_frame` 501 ms) accumulates to seconds before the lane is reached again (s310m §6
records exactly this: the largest single span is ~20x smaller than the observed gap).

No within-iteration competition: one owner per iteration, so no other owner can "eat" the lane's turn; it
can only occupy the iteration, and the lane block returns at src/main.cpp:502 before the stopped/realtime
branching.

Within the lane (not lane starvation, but per-track): the track round-robin serves one offer per turn
(src/main.cpp:486-494; policy header:346-370). With H owing tracks a given track's grain waits H lane turns
(H <= 8); multiplied by the 7-owner rotation that is up to 56 iterations per grain — the undeclared `H x K`
contention. `AllBlocked` cannot latch (header:339-342, 368). Also: the *sampler* only runs on a serving
turn, so "no work owed" is invisible (see §4).

## 4. What is already observable / what is NOT

Observable today:
- Lane served at phase P: `#CAP,<micros>,DIAG,win_gap,<maxDeltaTicks>,<opportunities>,<phase>,<running>,<resets>`
  (src/Utils/DebugSessionCapture.cpp:1530-1545), sampler at src/Track/TrackPlaybackWindowBuild.cpp:1813-1840,
  cadence 1 row / 16 opportunities (:1804, :1835), Tier-A (DebugSessionCapture.cpp:297).
- Per-iteration duration of the deferred-idle block: `idle_maint` (src/main.cpp:897-899), plus `load_frame`
  (:924-928) and `persist_save` (:985-987) spans.
- Work-remains-pending (event-shaped, not opportunity-shaped): `DIAG,win_readiness` receipt
  (`Track::reportPlaybackWindowReadinessReceipt`, TrackPlaybackWindowBuild.cpp:2471; emitter
  DebugSessionCapture.cpp:1547) and the maintenance path rows — "grain_denied" (:2409), "build_complete"
  (:2413), "lag_breach" (:1194), "one_pass_late" (:1202), "last_resort_slot_silence" (:1221).

NOT observable today — what a row sampled at the lane dispatch boundary (src/main.cpp:477-503) must add to
separate "not dispatched" from "dispatched but no progress":
(a) a lane-dispatch counter incremented BEFORE the `Service` branch (before :493): a dispatch that serves
    nothing emits no row at all today, so "never dispatched" and "dispatched, nothing owed" are identical
    in the data;
(b) the selection outcome (Service / None / AllBlocked) and the chosen track index (:493-501);
(c) the loop-iteration count between two lane dispatches (loop() has no iteration counter) — turns
    "ticks elapsed" into "N iterations spent elsewhere";
(d) per-intervening-iteration owner identity and `deferredIdleOwnerAllowed` (:865-879, :893), including the
    extra-Snapshot turn (header:174-188);
(e) Δmicros between two dispatches (the row's micros is the emit time of a 16-opportunity summary, not the
    previous dispatch time);
(f) transport-state history across the interval, not just the instantaneous flag: the sampler accumulates
    only between two running samples and updates `sGapLastPhase` unconditionally (:1820-1833), so a
    non-running sample inside the window silently splits/erases a delta (recorded at s310m §7);
(g) resets seen only between running samples (`sGapResets`, :1826) — a reset during the interval is invisible;
(h) whether an obligation existed at dispatch and the in-flight build's fill scope
    (RawCoverage / RecordCoverage / Coverage) — needed to distinguish window-lane starvation from the
    record-lane build being stepped (§1);
(i) the turn result / grains served (TrackPlaybackWindowBuild.cpp:1858-1885) — "dispatched but made no
    progress" vs "dispatched and progressed".

## 5. Ranked candidate causes, with evidence and falsifier

1. **Cumulative slow/blocked iterations between two lane turns** (7-iteration rotation x up to ~0.5 s per
   iteration). Evidence: s310m §6 (`load_frame` 501 ms, `idle_maint` 126 ms, `persist_save` 45-121 ms);
   6-7 x 0.5 s = 3-3.5 s, worse when stacked. Per-iteration, not one call.
   Falsifier: a per-iteration span series summing the intervening iterations to >= the gap; or a
   lane-dispatch counter showing dispatches continued while the gap grew (then it is not starvation).
2. **Blocking USB serial writes during capture flush/telemetry** (DebugSessionCapture.cpp:1799-1805 admits
   blocking; src/main.cpp:847 flushes up to 64 records; per-second and 4-second rows). The only mechanism
   here that is unbounded by construction (host-paced). Falsifier: same leg without SESSION_CAPTURE, or
   with a fast host reader — multi-second gaps vanish; or a span instrumenting the flush itself.
3. **SD/load grains overrunning their slice budget** (include/LoadLoopBudget.h:12-16 vs 501 ms measured
   frame). Falsifier: a gap window containing a sustained run of >100 ms `load_frame` rows.
4. **External-clock catch-up on resume** (ClockManager external `+= TICKS_PER_CLOCK`; `clk_batch` <= 336
   ticks measured, s310m §3). Cannot explain 2138-8520 alone. Falsifier: gap episodes whose Δmicros matches
   Δphase x microsPerTick with `clk_batch 0,0` — run 5's shape already does this (s310m §1).
5. **Instrument artifact: the sampler counts only SERVED turns** (src/main.cpp:493): any interval with no
   owed build registers nothing, and the next sample's delta spans it (a 16-opportunity row's max can be
   dominated by the first sample after an idle interval). Evidence for: run 1 mean 279.5 / max 9367 while no
   production requester exists (§1). Falsifier: a lane-dispatch counter (4a) showing dispatches throughout
   the gap with no obligation present.
6. **Pressure reclaim / heap accounting walks** (TrackManagerMemoryPressure.cpp:31-68; ExternalPoolAccounting
   8192-probe hooks). Background weight; unlikely alone to reach seconds.
   Falsifier: a gap episode at Nominal pressure with low shadow-table occupancy.

Recorded-to-record non-causes: `Looper::update` empty (src/Looper.cpp:61-62); MIDI drain batch-bounded
(src/MidiHandler.cpp:239/272/293); the `while` in `processDeferredSaveState` is time-budget-bounded
(src/StorageManager/DeferredSaveScheduler.cpp:116) even though one SD write inside can exceed the budget.

## UNKNOWN / not closed by this investigation
- Whether Config::NUM_TRACKS equals 8 (cap interaction at src/main.cpp:51/481).
- Whether the `viewBuild` sharing between the record lanes and the window lane (§1) is intended or a defect.
- No device evidence was re-read beyond the recorded measurement files; the caller of the observed builds
  during runs 1-6 is inferred from code, not from a per-run row.
