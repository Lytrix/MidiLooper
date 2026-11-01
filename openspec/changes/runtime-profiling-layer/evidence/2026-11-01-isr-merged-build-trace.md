# 2026-11-01 — A content-sized playback build runs inside the internal-clock ISR (traced with the profiler)

## Finding

On the internal clock, `TrackManager::updateAllTracks` runs in the `IntervalTimer` ISR (as `include/Utils/EdgeEffects.h`
already records). The ISR reaches a **content-sized merged playback build** through the merged fallback route:

```
IRQ_PIT (exception 138 = 16 + IRQ 122)
  clockTimer → ClockManager::updateInternalClock            (src/ClockManager.cpp)
    TrackManager::updateAllTracks(currentTick)
      Track::playCommittedLoopMidi(slot, tick, …)           (src/Track/TrackPlaybackHotPath.cpp)
        useMergedFallback = fixedView == nullptr && !pagedEmission
        buildMergedPlaybackFallbackCold(…)                  (src/Track/TrackPlaybackWindowBuild.cpp)
          ensurePlaybackMergedMidiEventsBuilt(…)            → real build when the reuse check misses
```

The code's own comment at that call site calls it "the unbounded merged build". The paged-delivery work (task 3.7a)
was meant to keep it off the tick. It does that only when a paged delivery is admitted for the slot.

## Evidence (profiler, image `d692fffb13d12d8df1662ce12bfea940d93418c3458e9d2dea76837a06e2525b`)

**How it was measured.**
- The profile scope's IPSR guard refuses any scope entered in exception context.
- This session added per-identity ISR refusal counts and the last exception number, reported as
  `isr_last=<IPSR>:<id>` in the stats terminator.
- Two diagnostic identities were added at the two tick-path routes into the build:
  `TickMergedFallback_build` (25) and `TickSlotSwitch_build` (26).

**The run.**
- Owner chain: `captures/session_20261101_021255.log`. Valid: 12 overdubs, 0 `order_deny`.
- The rig's host clock then stopped, and the device switched to its internal clock with the transport still running.
- Read-backs: `captures/session_20261101_021654.log` and `…021657.log`, complete stats, about 3 s apart.

| Identity | calls (main lane) | `ref` (ISR entries) | note |
|---|---|---|---|
| `TickMergedFallback_build` | 35,857 (avg 21 µs, max 17,923 µs) | **242,179 → 250,429** | every internal-clock tick takes this route |
| `PlaybackWindow_build` | 74 (avg 4,964 µs, max 17,872 µs) | **55 → 57** | real builds past the reuse return, inside the ISR |
| `TickSlotSwitch_build` | 0 | 0 | not the route |

Terminator: `isr_refused=241058 … isr_last=138:25`.
- Exception 138 is `IRQ_PIT` (`imxrt.h`: `IRQ_PIT = 122`), the only PIT user being `clockTimer`.
- Identity 25 is `TickMergedFallback_build`.

Earlier chains show the same: `isr_refused=13–25`, `isr_last=138:6`.

**What it implies.**
- Each ISR-entered real build costs what the main-lane builds cost: 5 ms average, up to about 18 ms.
- So the PIT ISR can block for milliseconds, stalling USB, MIDI input and every lower-priority interrupt.
- It is also re-entrancy exposure: the build allocates and touches state the main loop also uses.
- About one such build every 1.5 s was observed while the device ran on its internal clock with no host.

## Not changed / repair directions (owner decision)

The profiler only observes. In profile builds the ISR-entered scope is refused; the build itself still runs.

Repair options:
1. **Never build on the tick.**
   - When the merged fallback is stale on the tick, emit nothing new this tick and request the build from the
     deferred lane off-clock: the same contract the paged delivery uses ("nothing is produced, allocated or waited
     for here").
   - The miss shows up as a counted, named event, not a silent stall.
2. **Make the fallback unreachable under the internal clock.** Ensure a served window or an admitted paged delivery
   exists for every playing slot before the internal clock takes over (for example at the clock-source switch), so
   `useMergedFallback` is false on the ISR.
3. Both: option 2 as the norm, option 1 as the guarantee.

## Tooling defect seen in passing

The first fenced request after `hitl_profile_dump.py --attach` opened a capture was sometimes lost: no fence and no
answer, so the request reported "no terminator". A re-run read the complete table. It is likely a stdin/ready race in
the attach path. Not fixed.
