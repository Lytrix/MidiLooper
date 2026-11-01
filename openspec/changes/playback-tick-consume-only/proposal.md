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

## What Changes

1. **Telemetry first (report-only, default capture image).**
   - A permanent, cheap `tick_playback_miss` count, split by internal (ISR) and external (main-loop) clock.
   - A per-tick count of the consumed route: served window, bridge, paged delivery, merged fallback, none/contained.
   - Both are emitted in the 5 s telemetry family, with state in RAM2 and literals in a carved TU.
2. **Routing characterization.** For the long-loop shape, the route decision records the **first failed predicate**
   that keeps the windowed lane out, as a named reason (never a generic "not usable"). It is emitted on change.
3. **Capability proof.** A native fixture drives the existing windowed lane alone on the same 20-bar loop: 2-bar moving
   coverage, publication ahead of the cursor, and seam transitions with note closure. It decides between two causes:
   - **routing/admission bug**: the lane can serve the shape but isn't selected;
   - **capability gap**: the lane cannot serve it.
4. **Repair, gated by 3.** Fix demand, admission or publication so the windowed lane is selected and stays one window
   ahead. A merged `ready` buffer requires an explicit, recorded proof that the lane cannot satisfy the contract.
5. **Unify both clocks.** Once the source is reliably ahead, the external-clock tick follows the same consume-only rule
   as the ISR tick (DEC-241's owed + containment path stays as the exceptional failure mode).
6. **Qualification.**
   - On a long device run across many window seams, on both clocks:
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

- Which predicate keeps the windowed lane out for this shape: route selection, view admission, the rolling lane's
  dispatch, or a missing producer turn.
- Whether the lane's publication lead covers a 2-bar seam at 170 BPM under the measured owner-turn latency.

## Non-goals

- A second merged-fallback buffer (blocked unless task 3 records a capability failure).
- Removing the merged fallback in this change (its removal trigger is the qualification in task 6).
- Display / C6.4 tuning (parked).
- The profiler attach-request bug.

## Scope authorization

Owner-requested (2026-11-01). A `## Now implementing` entry in `CURRENT_WORK.md` is task 0.1.
