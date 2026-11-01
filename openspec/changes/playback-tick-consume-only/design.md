# Design — playback tick is consume-only, served by the windowed lane

See [proposal.md](proposal.md) for the evidence and [the spec delta](specs/windowed-playback-generation/spec.md) for
the contract. This design covers how each phase is measured and gated. The repair itself is designed only after phase 3.

## Context

- **Route selection** lives in `Track::playCommittedLoopMidi` (`src/Track/TrackPlaybackHotPath.cpp`). It tries, in
  order: a bridge (`fixedBridge`), a served fixed view (`fixedView`), an admitted paged delivery
  (`runtime.pagedEmission.admittedForEmission()`), and finally the merged catch-all (`useMergedFallback`).
  - `reportFirstBarRouteForSlot` already names the route for the first bar only: 1 bridge, 2 view, 3 paged, 4 merged,
    7/8 other.
  - `containPlaybackTurnIfNoRouteForSlot` handles "no route".
- **The windowed lane's producer** is `Track::stepPlaybackViewMaintenance` (`TrackPlaybackWindowBuild.cpp`), with
  readiness, deadline promises and grain service.
- **DEC-241** (`b260d4d0`) made the ISR tick consume-only for the merged fallback.
  - State: `PlaybackTickContext::gState` (RAM2): `owed`, `misses`, `servicing`, `inInterrupt`.
  - Owner: `TrackManager::serviceOwedTickPlaybackFallbacks`.
  - This change reuses that state for the miss counter instead of adding a second one.
- **Budgets:**
  - RAM1 variables are pinned at 96,192; any DTCM growth fails the build.
  - ITCM headroom is 1,272 − 840 = 432 B in the default image, and the hot path is ITCM-resident.
  - Hence: counters in RAM2, increments in the ITCM hot path kept to a few instructions, and emission and decoding in
    flash.

## Decisions

### D1 — Telemetry reuses existing owners and the 5 s family

- **Counters:** per-route tick counts (5 routes) and miss counts (2 clocks) as `uint32_t` in a `DMAMEM` struct beside
  `PlaybackTickContext::gState`, cleared by `PlaybackTickContext::initAtBoot()`.
- **Increment:** at the single point after route selection in `playCommittedLoopMidi`, as one indexed increment, so the
  ITCM cost is one load/store sequence.
- **Emission:**
  - The existing 5 s telemetry owner (`RuntimeTimingTelemetry`, `midi_gap`/`late_on` family) emits one Tier-A row,
    `DIAG,tick_route,served=,bridge=,paged=,merged=,none=,miss_int=,miss_ext=`, as deltas per window.
  - The prefix is registered in `isTierATextLine`.
  - The format literal lives in a carved TU.
- **Decoder:** `scripts/parse_overdub_chain_timing.py` gains totals, plus an external/internal interval split keyed on
  the existing clock-source log lines. This reuses the session's `clocksplit` logic, promoted into the scorer.

### D2 — The first failed predicate is a fixed enum, emitted on change

- `PlaybackRouteReject` is an enum covering every predicate between "slot sounds" and "windowed lane selected":
  - no runtime;
  - rolling lane not dispatched;
  - no view requested;
  - view building;
  - view stale (generation or revision);
  - view coverage misses the tick;
  - bridge not prepared;
  - paged not bound, or not admitted.
- It is computed in flash only when the merged fallback is about to be selected, never on a served tick.
- Emitted once per change per slot through the existing `reportPlaybackViewMaintenanceRow` / `reportDispatch` pattern
  (DEC-193 rows are already keyed by id and emitted on change).
- The enum is filled from the code during task 2.1. The list above is a starting hypothesis, not the contract.

### D3 — Capability fixture drives the lane alone

- **Harness:** a native suite on the shared capture/storage harness, which already links the real producer and hot
  path (as `test_runtime_profiler_sites` does).
- **Shape:** a 20-bar committed loop, transport running. It steps the production producer
  (`stepPlaybackViewMaintenance`) and the tick (`playMidiEvents`) alternately across at least 10 seams, with only the
  windowed lane allowed: the merged fallback is forbidden by assertion, not stubbed out.
- **Oracle:**
  - every tick is served by a published window covering it;
  - note-on/off closure across each seam matches the committed content;
  - no build happens on a tick.
- **Classification:**
  - pass → routing/admission;
  - a named failure → capability gap.

### D4 — Repair is chosen after D3, against the D2 reason

- If D3 passes, the repair changes demand, admission or route selection so the lane is selected for this shape, and so
  its producer stays one window ahead. The trigger is the lead to the next seam, not arrival at it.
- If D3 fails, the capability gap gets its own design note. Only then is a merged `ready` buffer considered, with the
  parity obligations listed in the proposal.

### D5 — External clock joins after the source is reliably ahead

- **Precondition:** the qualification metrics are clean on the internal clock with the lane serving.
- **Change:** extend DEC-241's consume-or-owe from `PlaybackTickContext::inInterrupt()` to every tick.
- **RED:** a main-loop tick with a stale source performs no build.

## Execution contract

Each phase gets its own G2 execution packet under `evidence/`.

| Phase | Gate | Device |
|---|---|---|
| 1 telemetry | native counters + row/tier tests; default RAM1 96,192 | one chain: route and miss totals recorded as the baseline |
| 2 reason | native reason fixture per predicate reachable in the harness | one chain: first failed predicate named for the long loop |
| 3 capability | the fixture result (pass/fail with the named gap) | — |
| 4 repair | RED/GREEN against the D2 reason; full native | chains on both clocks |
| 5 external clock | RED: main-loop tick never builds | chains on both clocks |
| 6 qualification | — | a long run (≥ 5 min, ≥ 50 seams) on both clocks with all acceptance metrics |

## Risks

- **Telemetry cost in the ITCM hot path.** → One indexed increment. Measured with the profiler's calibration and the
  ITCM line.
- **Reason enum drift.** → Each enum value has a native case where the harness can reach it. Values the harness can't
  reach are named as device-only.
- **Owner-turn latency too long for a 2-bar lead at high BPM.** → Phase 3 and 4 measure the lead. The profiler's
  `ViewPrewarm_unit` / `PlaybackViewService_grain` show owner cost.
- **The ~126 ms internal-clock `late_on`.** → Kept as a separate measurement. Not attributed to this cause without
  evidence.
