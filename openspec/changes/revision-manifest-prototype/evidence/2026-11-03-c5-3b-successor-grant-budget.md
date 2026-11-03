# C5.3b: the deadline successor's grant budget (owner ruling 2026-11-03)

## Rule

A rolling deadline successor's next grant is **due** at:

```
due = from + (N.end - guard - from) / remaining
  from      = its last grant (or its request)
  remaining = bounded remaining grants:
                before admission: G(E) of the manifest's densest bound, less the grants served
                after admission:  the grants at admission + the priced remainder, less the grants served
  guard     = 64 ticks (one measured lane-turn gap is 32-56)
```

- A due successor is served at that opportunity. Neither another holder nor the competing owner (the device
  injector) may withhold it (`PlaybackHolder::grantDue`, `selectPlaybackHolderBounded`, `playbackHolderMustBeServed`).
- Slack earned by early grants lets a later gap be longer. A successor that falls behind tightens its own next due.
- The 3-denial cap stays as a secondary bound for now; the budget subsumes it.
- Successor pipeline: measurement image only, as before.

## Host

- `test_5_1_a_due_successor_grant_is_never_withheld`: a due successor takes the opportunity from a rotation as far
  from it as possible, and the competing owner cannot withhold it. Not due: ordinary round-robin.
- `test_5_1_a_tight_successor_is_served_by_its_grant_budget`: `main.cpp`'s lane (bounded selection, the injector,
  opportunity notes), a routed successor at a 192-tick lead under deny 7.
  - **With the budget:** 52 of 53 handed, 0 missed, 1,033 grants withheld.
  - **Mutation (cap only):** 0 of 7 handed, 7 missed.
- FULL (escalated by `MidiServiceDrain.h`): 147/147 suites, 3,961 cases, 0 failed, 0 errored.

## Device

succprobe HEX sha256 `aa9578e3011ef79fcd98aab76604b1eef45ac2827a5b86575e9e4b6337c379c5`,
`captures/session_20261103_112121.log`, boot `crash=0`. 32 dense bars, a 32-bar overdub, stop, restart, then
`0:1536:20` and `7:1536:20`. Routed `win_blocked` 0, bridge ticks 0. The first window after the restart served at
13.3 s.

| point | handed / missed | min margin | grants withheld | grants served because due | lateness vs due | lane-opportunity gap while owed |
|---|---|---|---|---|---|---|
| deny 0 | 20 / 0 | 848 | 0 | 0 | 0 | 40-48 |
| deny 7 | 21 / 0 | **24** | 796 | 16-20 of 16-20 | **23-46** | 40-64 |

New per-seam row: `succ_due,<outcome>` — actual lead, estimated grants, grants served, max lateness against the due
tick, grants served because due, max opportunity gap while owed, N.end.

**Reading.**

- **Deny 7, the case the 3-denial cap alone cannot hold, passes at a full lead.** Every grant was served because it
  was due, and none was more than one lane-opportunity gap late. The 64-tick guard absorbs that lateness, which is why
  the margin bottoms out at 24.
- **The outer bound is the lane turn itself.** Before the restart (the post-overdub commit phase, probe on from boot,
  deny 0) two seams missed with 376-384-tick opportunity gaps: the lane was not offered for about 0.4 s, so no rule
  could serve it.
  - 111.2 s: lead 1,160, 4 of 20 grants, lateness 353.
  - 125.1 s: lead 1,168, 5 of 20 grants, lateness 318.
  - This is the evidence that a one-window lookahead needs either earlier preparation or an outer scheduling guarantee
    while that phase's work runs.
  - The previously unexplained pre-restart `succ_seam,missed` rows are very likely the same shape.

## The outer gap: what owns the main loop between window-lane turns (measurement, 2026-11-03)

- **Method.** In the measurement image only (`main.cpp` `noteLaneGapIteration`, flash; ITCM padding 856, still above
  the 840 floor), every loop iteration records its deferred owner, whether the turn was allowed, its idle-maintenance
  time and its load/display-frame time. At the next allowed `PlaybackViewMaintenance` turn, a gap above 96 ticks while
  playing emits two immediate-path rows:
  - `lane_gap,turn`: the gap, iterations, disallowed turns, the longest iteration, the longest idle turn and its
    owner, and the top owners;
  - `lane_gap,frame`: summed iteration, idle-maintenance and load/display-frame time.
  - A first run that queued the rows into the capture ring lost them while the transport ran, so it is not used.
- **Runs.**
  - `captures/session_20261103_113649.log`: the turn rows. Two post-stop misses again.
  - `captures/session_20261103_114206.log`: the frame rows. Image
    `3baa6a9ed02a10051bbc229a062e6d9646a8713e1c5bd2bfd740db8af49d3907`, boot `crash=0`.

| phase | gaps > 96 ticks | mean gap | per gap: loop time | idle maintenance | load/display frame | rest of `loop()` |
|---|---|---|---|---|---|---|
| Record take (33-56 s) | 109 | 178 ticks | 185 ms | 3 ms | 2 ms | **180 ms** |
| After the overdub stop (92-137 s) | 180 | 235 ticks | 245 ms | 8 ms | 21 ms (max 50) | **216 ms** |
| After the restart | 0 | — | — | — | — | — |

- **Classification.** The successor misses come from the outer gap, but **not from a deferred owner**: not A, B or D.
  - The lane is offered every rotation of 8-13 iterations, and no turn is disallowed.
  - Idle maintenance, including the realtime unit rotation, costs only 3-8 ms per gap.
  - The longest single iteration is 42-100 ms.
  - About 88 % of the gap is spent in the rest of `loop()`, outside the deferred rotation: MIDI and clock handling,
    playback emission, persistence, display paint outside the load frame, and the capture's own serial output. So it is
    shape C, but across the whole loop rather than among the deferred owners.
  - After the restart the same image has no gap over 96 ticks, and successors hand off.
- **Open.** Which part of the rest of `loop()` takes the ~200 ms. The next measurement splits that residual by the
  loop's existing spans (`loop_prefix`, MIDI drain, display paint, `persist_save`, capture output) inside the same gap
  rows. Then: slice the owner, or give the window lane an outer turn deadline.

## The outer gap with a near-silent probe (owner ruling 2026-11-03)

- **Method.** Nothing about lanes is printed while the transport runs.
  - Flash callees add their wall time to RAM2 buckets through a scope timer (`include/Utils/LaneGapSpans.h`): MIDI
    input, capture flush, capture immediate rows, playback fallback, loop prefix, edge effects.
  - `main.cpp` folds each iteration with idle maintenance, the load/display frame and the rest ("other"), and keeps the
    16 worst iterations and the 8 worst lane gaps.
  - One `lane_spans` dump per transport stop. The stop trigger is the clock's running state: the epoch survives a stop,
    and a first version never dumped.
  - RAM1 and ITCM unchanged (padding 856).
- **Run.** succprobe HEX sha256 `b3350d180412eaed0076f5d71f13fb9077fa8911bb373895b1fae4f571c24d56`,
  `captures/session_20261103_122418.log`, boot `crash=0`. Same overdub workload; post-restart successors 10/10.

| segment | iterations | gaps > 96 | max gap | max iteration | MIDI input | capture flush | capture immediate | idle maint | load/display | other |
|---|---|---|---|---|---|---|---|---|---|---|
| record + overdub + post-stop | 26,052 | 161 | 5,160 ticks | 2,634 ms | **52.6 %** | 1.6 % | 0.0 % | 11.8 % | 10.3 % | 17.2 % |
| after the restart | 17,420 | **0** | 88 ticks | 41 ms | 12.8 % | 1.1 % | 0.0 % | 41.8 % | 17.2 % | 20.2 % |

- **The eight worst post-stop gaps** were 440-536 ticks, 417-557 ms. **MIDI input took 376-520 ms of each.** Capture
  flush was ≤ 1 ms, immediate rows 0, idle maintenance 9-15 ms, the load/display frame 0-59 ms, other 12-15 ms.
- **The worst gap, 5,160 ticks (5.4 s) around the overdub stop**, was 5.36 s inside MIDI input. Two single iterations
  took 2.6 s, almost all of it in `MidiHandler::handleMidiInput`.
- **Conclusion.**
  - The outer gap is **real production-shaped behaviour, not capture overhead**: capture output is about 1 % of the
    loop and at most 1 ms per gap.
  - It is not a deferred owner. It is `MidiHandler::handleMidiInput`, which includes everything a received message
    dispatches: external-clock ticks and their playback, the slot gestures and the capture of incoming notes.
  - After the restart the same image has no gap above 88 ticks.
- **Next.** Split the MIDI-input bucket one level deeper: clock-tick processing (with its playback), the control
  gesture that stops the overdub, and live note capture. Then decide between slicing that work and an outer lane-turn
  deadline. No deeper lookahead yet.

## MIDI input split: per-message cost, not backlog (owner ruling 2026-11-03)

- **Method.** In the measurement image `dispatchMidiBatch` moves to flash, which frees ITCM (padding 984), and times
  each dispatched message. Per transport phase (play, record, overdub, post-stop) and per message kind, it keeps counts,
  total and longest dispatch; per `handleMidiInput` call, the totals and the 16 worst calls. RAM2, printed only at the
  transport stop.
- **Run.** succprobe HEX sha256 `6e2496e50f7d8ac6909ffe52f0762886290ae4db2dc9fd4a6edbe291312ff7ef`,
  `captures/session_20261103_123542.log`, boot `crash=0`. Same overdub workload; after the restart the first window
  served at 12.4 s and successors went 10/10.

| phase | Clock: n / avg / max | Note: n / avg / max | Start/Stop |
|---|---|---|---|
| record take | 3,131 / **0.60 ms** / 11 ms | 1,026 / 0.28 ms / 3.7 ms | — |
| overdub | 3,587 / 1.41 ms / **3,045 ms** | 1,026 / **18.4 ms** / 87 ms | — |
| after the overdub stop (45 s) | 5,927 / **6.71 ms** / 52 ms | 4 / 0.19 ms | 95 ms |
| playback after the restart | 4,110 / **0.97 ms** / 31 ms | — | 95-109 ms |

- **It is per-message cost (case B), and it depends on the phase.** The batches are bounded and are not the driver.
  1. **After the overdub stop every Clock costs 6.7 ms**, against 0.6 ms while recording and 1.0 ms in playback after
     the restart. At 300 BPM (120 clocks/s), Clock dispatch alone took 39.8 s of the 45 s phase, about 88 % of the CPU.
     That is the outer starvation of the window lane.
  2. **During the overdub every NoteOn/NoteOff costs 18 ms** (max 87 ms), against 0.28 ms during the record take. That
     backs input up: the worst calls drained 85 clocks + 29 notes and took 1.0-1.2 s.
  3. **One Clock message took 3.04 s** during the overdub: the single synchronous stall inside the 5.4 s gap.
- **Next.** Follow the synchronous call chain of:
  - a Clock dispatch in the post-overdub-stop phase;
  - the 3 s Clock;
  - an overdub note dispatch.

  Whatever builds, scans or commits there moves to bounded deferred ownership. No lane deadline or deeper lookahead
  until then.

## The repeated post-stop Clock work, named from the existing timing rows (2026-11-03)

Same capture, `captures/session_20261103_123542.log`, using the image's existing runtime-timing rows. No new code.

| phase | `merged_build` rows | `playback_order` rows |
|---|---|---|
| record take (26 s) | 30 | 30 |
| overdub (30 s) | 87 | 86 |
| **after the overdub stop (45 s)** | **6,110** | **1,539** |
| playback after the restart (34 s) | 43 | 43 |

- **Who.** After the overdub stop the six **unrouted** tracks each rebuild their merged playback fallback about 873
  times in 45 s, about 19 per second per track. That is about 114 builds per second, roughly one per Clock.
  - Tracks 0, 1, 2, 4, 5, 6, slot 0; the windows are their whole loops, 768-12,288 ticks.
  - Every row is `path=2` (`CompatibilityPasses`: the committed passes serve the window), `pst=5` (that source is
    current), `root=0` (no current published root), 68-448 events per build.
  - The routed track is not among them.
- **Where.** `Clock` → `ClockManager::onMidiClockPulse` → `TrackManager::updateAllTracks` → per-track merged-fallback
  build (`TrackPlaybackWindowBuild.cpp`, the `mergedFallbackReuseHolds` reuse test failing, then
  `copyEffectiveCommittedEventsInRange` over the window). That is synchronous, content-sized work on the clock path.
- **Instrumentation share.** In capture images the witness row of every such build (`reportMergedBuildWitness`) also
  gathers a whole-window oracle copy, so the measured 6.7 ms per Clock is roughly one build plus one oracle copy. Even
  so, about 114 builds per second of up to 448 events is real production work.
- **Not yet known: why reuse fails about 19 times per second per track only after the overdub stop.** The reuse key is
  the loop's `playbackRevision`, its `publishedSnapshotIdentity()`, and the window covering the playhead. For a
  whole-loop window the coverage term always holds, so the revision or the identity must be moving.
- **Next.** One silent counter per reuse-failure term (revision, identity, empty, coverage) per track, for the
  post-stop phase. Then remove the cause, so a stable committed source is built once.

## Why the six tracks rebuild: their cache is cleared, not their source moved (2026-11-03)

- **Method.** In the measurement image, one counter per failed reuse term per track, plus the first three failures of
  each track with old→new values. RAM2, reset at each transport start (RAM2 is not zeroed at boot: a first run's
  pre-restart dump held stale bytes and is discarded). Printed after the transport stops.
- **Run.** succprobe HEX sha256 `91693fce8b81e9df61567a28a3221bccc6ef03b0240336b66e0da2f74526b6a4`,
  `captures/session_20261103_125956.log`, boot `crash=0`.

| segment | tracks 0, 1, 2, 4, 5, 6 | track 3 | track 7 (routed) |
|---|---|---|---|
| record + overdub + post-stop | **revision 922 each**, nothing else | revision 922, coverage 72 | 1 / 1 / coverage 28 |
| playback after the restart | 0 (track 0: 1 identity) | coverage 73 | — |

- **The failing term is the revision, but the loop's revision is not moving.** Every transition is
  `builtFromRevision 0 → playbackRevision 1`, with identity null → null.
  - The built window's revision is 0: **the cache was cleared** (`mergedMidiEvents.clear()` resets it) and rebuilt from
    the unchanged source.
  - It happens on **all six tracks at the same ticks** (52,128, 55,200, 57,704, ...), so one pass clears every track's
    cache at once, about 20 times per second during that phase.
- **Leading suspect, not yet proven:** the memory-pressure reclaim in `loop()`
  (`TrackManager::tryReclaimDerivedViewCachesUnderPressure` → `Track::tryReleasePlaybackMergedMidiEventsMemory`).
  - It walks every track and clears a merged cache when its track is not `isPlaying()` or its window is stale.
  - It runs on every loop iteration while the advisory pressure is Low or higher, and the heap is at 40 KB free.
  - A whole-loop rebuild on the next Clock and a clear in the next loop iteration would be the thrash measured
    (about 114 builds per second).
  - Open: the heap is also 40 KB after the restart, where no churn occurs. So which of its terms admits the clear only
    in this phase (`isPlaying()`, the overdub-stop guard, or pressure) has to be counted at the clear site itself.
- **Classification so far.** Not source-revision churn (that would be an end-state identity defect), and not
  snapshot-identity churn. It is a legacy derived-cache release fighting a legacy synchronous rebuild on the Clock
  path. Next: count the clear site and its admitting term; then contain it minimally (never release a cache a
  playing track will rebuild on the next Clock). Migrating these tracks to the manifest reader is the end state.

## Fix: a Clock-consumed merged cache is not reclaimed (owner ruling 2026-11-03)

- **Confirmed admitting term** (`captures/session_20261103_131309.log`): after the overdub stop the reclaim cleared
  each of the six tracks' merged windows **919 times as "not PLAYING and silent"**. The tracks stayed stopped through
  the capture, while their enabled slots' engines consumed the window on every Clock. No clear was stale or audible.
- **Invariant.** A derived playback cache is not reclaimable while the Clock path consumes it.
  `Track::mergedWindowConsumedOnClockPath(slot)` uses the same gates as `playMidiEvents`: no stop barrier, the capture
  interval permits playback, not a stopped recording, the slot enabled, a playable source. `isPlaying()` is not
  consulted. `tryReleasePlaybackMergedMidiEventsMemory` keeps such a window unless it is stale. Legacy containment only;
  the merged fallback itself is unchanged.
- **Host RED.** `test_5_1_a_clock_consumed_merged_cache_is_not_reclaimed`: a stopped unrouted track whose enabled
  slot's engine runs, with a reclaim after every pulse.
  - Before the fix: 1,536 rebuilds and 1,536 clears over 1,536 pulses.
  - After: 1 rebuild, 0 clears. Mutating the new term out restores the thrash exactly.
  - Control `test_5_1_an_unconsumed_merged_cache_is_still_reclaimed`: a disabled slot's cache is still released.
  - FULL: 147/147 suites, 3,963 cases, 0 failed, 0 errored.
- **Also fixed.** Since `62a3199f` the default image (`teensy41-capture-serial`) had not compiled:
  `Track::reestablishRollingFrameDemand` was declared inside the `REVISION_MANIFEST_READER` block. It is moved out; the
  default image builds with RAM1 at baseline (code 392,008, padding 1,208).
- **Device.** succprobe HEX sha256 `3669f33b47af3844b9d05159135f5ac1bc65eccaeb28179564523b1c73c1e0d4`,
  `captures/session_20261103_132725.log`, boot `crash=0`. Same overdub workload.

| after the overdub stop (45 s) | before (`_123542` / `_131309`) | after the fix |
|---|---|---|
| Clock dispatch avg / max | 6.71 ms / 52 ms | **0.53 ms / 28 ms** |
| reclaim clears per track | 919 | **1** |
| `merged_build` rows | 6,110 | **59** |
| lane gaps > 96 ticks (whole pre-restart segment) | 161 | **25**, all inside the overdub |

  - Post-restart successors: deny 0 20/20. Deny 7: one miss in the first 14, then 20/20 on the repeat; minimum margin
    24, the grant budget's guard.
- **Remaining, all inside the overdub phase.** The 25 gaps (1.0-1.4 s each, 6.8 s at the stop) are MIDI input:
  - overdub NoteOn/NoteOff still cost 17.9 ms each (record take: 0.27 ms);
  - one Clock took 3.0 s at the overdub stop.

  These are the next two named defects.

## The overdub-phase MIDI cost, split by child (2026-11-03)

- **Method.** Per dispatched message, a separate RAM2 child set (`LaneGapSpans::MessageScope`, flash callees only, so
  ITCM is unchanged): `Track::stopOverdubbing`, `TrackManager::stopRecordingTrack`, the record-stop geometry service,
  `Track::snapshotOverlapHoldCandidates`, `Track::recordMidiEvents`. Totals per phase and kind, plus the 8 worst
  messages with their children; printed at the transport stop.
- **Run.** succprobe HEX sha256 `026ae5c1b168c52180dd8a498484b1a05194ee887f9fa11ebaff22bfd2851101`,
  `captures/session_20261103_135139.log`, boot `crash=0`. Overdub 57.5-88.4 s.

| phase | Note: n / avg | of which `recordMidiEvents` | of which overlap hold |
|---|---|---|---|
| record take | 1,009 / 0.31 ms | 0.23 s total | 0 |
| **overdub** | 1,026 / **18.7 ms** | **18.37 s of 19.15 s (96 %)** | 0.68 s |

- **Overdub notes.** The cost is `Track::recordMidiEvents` on the overdub branch: about 18 ms per event, against
  0.23 ms per event during the record take. The worst messages are a burst of 76 ms notes (ticks 50,304-50,592),
  entirely inside it.
- **The 3.57 s Clock** (tick 49,920, mid-overdub, not at the stop) has **no named child**: not the overdub stop, not
  the record-stop service, not capture. It is elsewhere in the Clock's synchronous chain (`updateAllTracks`:
  per-track playback / overlay / pass work on the overdubbing track).
- After the stop the Clock stays at 0.51 ms (the reclaim fix holds).
- **Next.** Split `recordMidiEvents`' overdub branch one level deeper, and time `updateAllTracks`' per-track children
  for the Clock (needs flash-side timers only, or an ITCM budget check).

## The overdub-phase MIDI cost, named (2026-11-03)

- **Method.** Two more flash-resident message children:
  - `Loop::accumulatePendingNoteChangesForIncomingNote`, the overdub overlap evaluation of a completed note against
    the overdub source view (inside `recordMidiEvents`' NoteOff branch);
  - `Track::commitOverdubWrap`.
  The image's existing runtime rows already pointed there (`notechg` up to 92 ms per 5 s window; `noteappend`
  0.6-4.4 ms).
- **Run.** succprobe HEX sha256 `a4c234c84117ce06dbd59c53391c4d73edd15aa7321ab4bc8d19997f461ad565`,
  `captures/session_20261103_141004.log`, boot `crash=0`. The harness reported and exited on its own (deferred MIDI
  close).

| overdub phase | total | named child | share |
|---|---|---|---|
| Note (1,026, avg 6.83 ms, max 76 ms) | 7.01 s | `accumulatePendingNoteChangesForIncomingNote` **6.35 s** (of `recordMidiEvents` 6.63 s) | 91 % |
| Clock (3,363, avg 1.04 ms, max 1,408 ms) | 3.51 s | `commitOverdubWrap` **1.33 s**, in one Clock at the loop wrap (tick 50,688) | 38 % |

- **1. Overdub note cost = the overlap evaluation on NoteOff.** Every worst note (76 ms, ticks 51,488-52,112, just
  after the wrap) is entirely `accumulatePendingNoteChangesForIncomingNote`. It runs synchronously inside MIDI dispatch,
  and its cost grows with the overdub source view.
- **2. The multi-second Clock = the overdub wrap commit.** `commitOverdubWrap` ran 1.33 s inside a single Clock
  dispatch at the loop wrap (the earlier run's 3.57 s Clock at tick 49,920 is the same shape).
- Run-to-run size varies (overdub note average 18.7 ms → 6.8 ms, wrap Clock 3.57 s → 1.41 s), but the attribution is
  the same in both. Both are overdub-lifecycle transactions executed synchronously from MIDI dispatch, against the rule
  that a MIDI message may do only bounded realtime work.
- **Next (owner decision).** Move each into bounded deferred ownership:
  - the note records only what the evaluation needs, and the evaluation runs in grains off the MIDI path;
  - the wrap is accepted on the Clock and committed by a deferred owner in grains.
  Both touch the overlap-resolve and wrap owners documented in `docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`.

## NoteOff split by path and deferred-set occupancy (2026-11-03, `a6c74b63`)

Owner ruling 2026-11-03: the earlier children (scan, transforms, recount ≈ 0.05 s of 17.7 s) disproved the whole-loop
evaluator, so the probe splits only coverage mark / deferred retain / full-set fallback derive / remainder, with the
deferred-completion-set occupancy at entry. Image `5a8f54e5` (RAM2 +352 B against `49eccad3`), same overdub
`lane_cadence` sweep as before (track 8 slot 4, 32 overdub bars), `captures/session_20261103_154139.log`, boot
`crash=0`, harness exited on its own.

| path | NoteOffs | total | max | where the time is |
|---|---|---|---|---|
| prepared span (immediate consequence) | 128 | 0.044 s | 0.41 ms | remainder (the consequence itself) |
| retained obligation | 34 | 0.001 s | 0.06 ms | retain 0.23 ms in all |
| **full-set fallback** | **350** | **16.05 s** | **74.4 ms** | **derive 16.01 s (99.8 %)** |

- **Occupancy.**
  - 0: 128 NoteOffs.
  - 1-15: 2 each (15: 4), all cheap (max 62 µs).
  - **16: 352 NoteOffs, 16.05 s, max 74.4 ms.**
  - High-water 16. All 4 worst NoteOffs (73.5-74.4 ms) are occupancy 16, fallback path, ≈ 100 % derive.
- **Share of the overdub MIDI cost.** The overdub Note phase totals 16.78 s over 1,026 notes. The fallback derive is
  95.7 % of it.
- **Classification (owner decision tree): the full-set fallback dominates and occupancy = 16.**
  - The set fills within about 17 obligations and then stays full for the remaining ~350 NoteOffs.
  - So the worker discharges nothing during live overdub, and every later NoteOff resolves synchronously on the MIDI
    path.
  - The fallback's closure-window resolve (`resolveDisplayNotesInWindow` over `passes`) costs 46 ms on average.
- **Not concluded.**
  - Why the worker cannot discharge during live overdub is unproven: is it never scheduled, or never answerable because
    tiles are not prepared?
  - Whether the two visits per occupancy level are two overdub sessions or one partial drain.
  - The capacity (16) is left unchanged, by ruling.
- **Separately still open.** The wrap Clock: the overdub Clock maximum is 2.94 s (`commitOverdubWrap`).

## Why the deferred set never drains during live overdub (2026-11-03, `f6c6484d` + split `27154420`)

Worker counters at each decision point, split by whether the set was full when last observed. Image `0aa776f7` (RAM2
+544 B against `49eccad3`, RAM1 unchanged), same overdub sweep, `captures/session_20261103_160317.log`. The run
reproduces the cost: 376 fallbacks, 17.24 s, max 78 ms; the overdub Note phase totals 17.95 s.

| | set not full | **set full** |
|---|---|---|
| NoteOffs | ~132 | **378** |
| idle maintenance entered while overdubbing | 1,100 | **146** |
| entered per NoteOff (rate proxy) | 8.4 | **0.39** (≈ 22× less) |
| worker gate passed (VisualCache owner) | 360 | 37 |
| worker outcome: private yield / no owed tile / grain / tile published | 66 / 275 / 17 / 6 | **0 / 0 / 34 / 3** |
| discharge calls / discharged | 281 / 0 | 3 / **3** |
| generation changes during the session | 1 | 2 |
| idle owners (Snapshot / VisualCache / WrapAppend) | 380 / 360 / 360 | 73 / 37 / 36 |

- **The worker is never idle while the set is full, and it is starved.**
  - Every turn is a tile-prep grain or a publish: about 11 grains per tile (34 / 3), with an owed tile on every turn
    (`no owed` = 0).
  - Each publish discharges exactly one obligation, so the full period discharged 3 in all, against hundreds of
    NoteOffs that each find the set full.
- **Idle service collapses once the set is full.**
  - Idle maintenance reaches the overdub track about 22× less often per NoteOff.
  - That is consistent with the fallback itself (46-78 ms per NoteOff, synchronously in MIDI dispatch) consuming the
    main loop.
  - The worker gets only every third idle turn (owner rotation Snapshot / VisualCache / WrapAppend).
  - **This is a positive feedback loop.** A full set → synchronous fallbacks on the MIDI path → fewer idle turns →
    fewer worker grains → the set stays full.
- **What makes tiles owed again.** The generation changes clear all prepared coverage, so every required tile must be
  re-derived: 1 before the set filled, 2 during the full period.
  - The not-full period's 275 "no owed tile" turns with 0 discharged are turns while the set was empty, before any
    obligation existed.
  - Which owner moves the generation during an overdub session is **not identified** (no marker). Candidates are the
    `notifyCommittedContentChanged` callers: capture-pass state change, wrap/commit paths.
- **Classification (owner decision tree): the full-set fallback dominates at occupancy 16, and the worker cannot
  discharge.** Two causes, compounding:
  1. **Generation moves during the session** wipe prepared coverage, and re-preparing it costs about 11 grains per
     tile.
  2. **The worker receives too little service** (1 of 3 idle owners), and the set-full fallback then starves it
     further.
- The capacity (16) is unchanged, by ruling.

## Who moves the committed generation during the overdub session (2026-11-03, `976fff9b`)

`notifyCommittedContentChanged` records, while a session is active:
- its return address;
- the capture-pass commit's own caller, latched at `commitPreparedCapturePassAtomic`'s entry;
- the clock tick;
- the deferred-set occupancy.

Symbolized with `addr2line` against the flashed ELF. Image `ce587ea6`, same overdub sweep,
`captures/session_20261103_162105.log` (391 fallbacks, 17.58 s, max 75 ms).

| # | tick | set | Track owner | Loop path |
|---|---|---|---|---|
| 1 | 33,664 | **empty** | `Track::stepDeferredRecordPublication` | `commitPreparedCapturePassAtomic(transfer, pass)` |
| 2 | 50,688 | full | overdub wrap commit: `Loop::commitCapturePass` → `commitPendingCapturePass` | `commitPreparedCapturePassAtomic(transfer)` |
| 3 | 57,928 | full | deferred overdub stop: `processDeferredOverdubStopGrain` → `commitSupersededCausalBase` | `commitPreparedCapturePassAtomic(transfer, pass)` |

All three reach `completePreparedCapturePassAtomic` → `notifyCommittedContentChanged` →
`CoverageState::changeGeneration`, which clears every prepared tile of the live session.

- **Move 1 starts the failure.**
  - The preceding **Record** pass's deferred publication lands after the overdub session opened, with the set still
    empty.
  - It wipes the coverage the session's open prewarm had prepared, so the following NoteOffs find their spans
    unprepared, retain, and fill the set.
- **Move 2 is the session's own wrap.** Committing the overdub pass at the loop wrap (tick 50,688, the same tick as the
  multi-second `commitOverdubWrap` Clock) wipes coverage again while the set is full.
- **Move 3 is the session end** (the deferred overdub stop), so it is not part of the live failure.
- **Inference, not yet tested.** A generation move whose content is not the session's source of overlap decisions
  (move 1: the previous interval's own pass; move 2: the session's own captured notes) should not have to invalidate
  coverage the session already prepared. Whether either move actually changes the committed content that overlap
  decisions read is the open question for the owner.
