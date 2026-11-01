# Playback tick is consume-only, served by the windowed lane

## Why

The main spec `windowed-playback-generation` already requires that **active playback is served from an immutable
window** through one off-clock owner, and that *"the MIDI input boundary, the transport tick and the clock path SHALL
only read readiness, and SHALL NOT allocate, build or block for a window"*. It names any merged branch as *"a named
temporary migration fallback with a removal trigger"*.

Device evidence from 2026-11-01 contradicts that on the owner chain, a 20-bar take, which is a long loop past
`kMaxDetailedWindowBars = 16`:

- **Every tick takes the merged fallback route.** `TickMergedFallback_build` counts 35,856 tick entries per chain.
  `PLAYBACK_WINDOW_ROLLING_ENABLED=1` in the default capture env, so the windowed lane is compiled in and bypassed
  anyway.
- **The merged window moves with the playhead.** For a long loop it is 2 bars (`kMergedWindowTicks`), and its reuse
  rule holds only while the playhead is inside it. A new window is therefore demanded *at* each seam, every 2 bars,
  and built there: about 114 builds per run, 5 ms average and up to ~18 ms.
- **Tick timing is that build's cost.**
  - On the internal clock the build ran inside the PIT ISR. DEC-241 (`b260d4d0`) stopped that: the ISR now consumes or
    owes, and contains the turn.
  - DEC-241 is containment only. Each seam under the internal clock still costs a contained tick, which is audible at
    a seam NoteOn: one 24 PPQN clock is 31 ms at 80 BPM, 21 ms at 120, 15 ms at 170.
  - On the external clock the build still runs on the tick, on the main loop.

Adding a second `current`/`ready` buffer to the merged fallback would create a second playback representation next to
the windowed lane, with its own generation, order, closure, overdub, stop and seam parity to prove. The existing lane is
purpose-built for look-ahead. This change therefore first proves **why the windowed lane is bypassed**, and repairs
admission or routing so that lane owns the workload. New buffering is allowed only against a recorded capability
failure.

**Phase 2 finding (2026-11-01, `evidence/2026-11-01-route-reject.md`, `evidence/2026-11-01-length-split.md`).** The misses
are one architectural split, not three problems: some playback sources can become ready only in particular transport or
length states, while the only producer that stays live during playback is restricted by loop length. The 20-bar loop is
blocked for the session by a transport restart (`clock_epoch_changed`); the restored 1- and 8-bar loops are merged from
their first tick because their readers are published only while STOPPED and the lane refuses loops of 16 bars or fewer.
The owner already decided the fix (DEC-200 / DEC-202, 2026-10-29: "length should not be the discriminator"); its final
slice — the keystone flip — never landed. This change finishes that unification before anything else.

## What Changes

1. **Telemetry first (report-only, default capture image).**
   - A permanent, cheap `tick_playback_miss` count, split by internal (ISR) and external (main-loop) clock.
   - A per-tick count of the consumed route: served window, bridge, paged delivery, merged fallback, none/contained.
   - Both are emitted in the 5 s telemetry family, with state in RAM2 and literals in a carved TU.
2. **Routing characterization.** For the long-loop shape, the route decision records the **first failed predicate**
   that keeps the windowed lane out, as a named reason (never a generic "not usable"). It is emitted on change.
3. **Capability proof, every length.** A native fixture drives the existing windowed lane alone on 1-, 8- and 20-bar
   loops: cold/restored entry into PLAYING, a transport restart, a revision change, seam transitions with note closure.
   It decides, per length and case, between two causes:
   - **routing/admission bug**: the lane can serve the shape but isn't selected;
   - **capability gap**: the lane cannot serve it.
4. **Remove the length gates, gated by 3.** One request policy, `clamp(lookahead span, loop extent)`; DEC-200's keystone
   and its producer twin stop refusing short loops. A merged `ready` buffer requires a recorded capability gap.
5. **Live recovery.** A missing or stale view recovers while transport runs; an epoch change re-requests instead of
   blocking.
6. **Route all playback through the lane**, and retire the length-specific readers (1-bar cold frame, stopped-oracle
   full-loop view / bridge as a playback route).
7. **Unify both clocks.** Once the source is reliably ahead, the external-clock tick follows the same consume-only rule
   as the ISR tick (DEC-241's owed + containment path stays as the exceptional failure mode).
8. **Qualification.**
   - On a long device run across many window seams, with 1-, 8- and 20-bar slots and a transport restart, on both clocks:
     - `tick_playback_miss == 0`;
     - merged-fallback route use == 0, or explicitly justified per occurrence;
     - zero `PlaybackWindow_build` entered from the ISR.
   - MIDI timing reported separately for the external and internal clock intervals.

No storage format, no persistence or undo change, no new playback representation.

## Capabilities

### Modified Capabilities

- `windowed-playback-generation` — adds telemetry and qualification requirements that make its existing
  "active playback is served from an immutable window" and "one off-clock owner" requirements measurable on device,
  and states the tick contract for **both** clock sources.

### New Capabilities

- None.

## Impact

- **Firmware:**
  - Report-only counters in the playback hot path (`Track::playCommittedLoopMidi`), with state in RAM2.
  - A route-rejection reason at the selection point.
  - Later (gated), admission/demand changes in the windowed lane owner (`Track::stepPlaybackViewMaintenance` and
    route selection), and external-clock tick containment.
- **Tests:** native telemetry and route-reason fixtures; the capability-proof fixture; RED/GREEN for the repair.
- **Tooling:** decoder support for the new rows, in the existing parse/scorer scripts or a small new parser.
- **Memory:** RAM1 variables must stay at the approved `96,192`; the ITCM floor is 840 (default padding 1,272 now).

## Known behavior (evidence-backed)

- DEC-241 trace and verification:
  - `openspec/changes/runtime-profiling-layer/evidence/2026-11-01-isr-merged-build-trace.md`
  - `openspec/changes/runtime-profiling-layer/evidence/2026-11-01-isr-consume-only.md`
- `include/PlaybackMergedMidiEvents.h` `mergedPlaybackWindowLengthTicks`: the whole loop when it fits, otherwise 2 bars
  for a long loop.
- The internal-clock interval keeps a flat ~126 ms `late_on` after DEC-241 (225–277 ms before). Unexplained; tracked
  here as a measurement, not assumed to share this cause.

## UNKNOWN

- Whether the lane serves every length with the gates bypassed (phase 3); phase 2 already named the predicates
  (`clock_epoch_changed` for the long loop, the length gates for the short loops).
- Whether the lane's publication lead covers a 2-bar seam at 170 BPM under the measured owner-turn latency.

## Non-goals

- A second merged-fallback buffer, or any optimization, before phases 3–6 (blocked unless phase 3 records a capability
  failure).
- Removing the merged fallback in this change (its removal trigger is the qualification in phase 8).
- Display / C6.4 tuning (parked).
- The profiler attach-request bug.

## Scope authorization

Owner-requested (2026-11-01). A `## Now implementing` entry in `CURRENT_WORK.md` is task 0.1.
