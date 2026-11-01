# Design — playback tick is consume-only, served by the windowed lane

See [proposal.md](proposal.md) for the evidence and [the spec delta](specs/windowed-playback-generation/spec.md) for
the contract. This design covers how each phase is measured and gated. The repair itself is designed only after phase 3, in the order D3 → D4 → D5 → D6.

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

### D3 — Capability fixture drives one lane for every length

- **Why every length.** Phase 2 and `evidence/2026-11-01-length-split.md` showed one gap, not three. The 20-bar loop
  leaves the lane at a transport restart (`clock_epoch_changed`, blocked forever). The restored 1- and 8-bar loops are
  merged from the first tick, because their readers are published only while STOPPED and the lane refuses them by length
  (DEC-200's keystone and its producer twin). A fixture on 20 bars alone would miss the short-loop half.
- **Harness:** a native suite on the shared capture/storage harness, which links the real producer and hot path (as
  `test_runtime_working_set_ownership` does, `PLAYBACK_WINDOW_ROLLING_ENABLED 1`).
- **Shape:** 1-, 8- and 20-bar committed loops, transport running. It alternates the production producer and the tick
  across ≥ 10 seams where the loop has them, with only the windowed lane allowed: the merged fallback is forbidden by
  assertion, not stubbed. Cases: cold/restored entry into PLAYING with no stopped-time publication, a transport restart
  (new epoch) mid-loop, a source revision change while playing, and steady playback.
- **Gate bypass:** the two length gates are bypassed by a native-only switch, so capability is measured apart from
  them. The same cases with the switch off record today's refusal per length (the baseline the repair must flip).
- **Oracle:**
  - every tick is served by a published window covering it;
  - note-on/off closure across each seam matches the committed content;
  - no build happens on a tick;
  - the requested window is `clamp(lookahead span, loop extent)`, the same calculation for every length.
- **Classification, per length and case:** pass → routing/admission; a named failure → capability gap.

### D4 — One request policy; length is geometry, not routing

- **Policy.** `requested window = clamp(lookahead span, loop extent)`. A 1-bar loop yields a 1-bar view and a 20-bar
  loop a bounded span, from one calculation. No hidden per-length table inside the producer (no "1 bar → whole loop,
  2–16 → whole loop, > 16 → 2 bars").
- **Keystone.** `requestCommittedPlaybackWindowForSlot` no longer answers `NotServable` by length, and
  `serviceRollingPlaybackWindow` no longer exits `win_window_ineligible` by length. The interval slice
  (`playbackWindowInterval` without `demoted`) lands with them, against the final policy (DEC-200's reason for holding it).
- **Decision record.** The phase-4 `DEC-###` states how DEC-200 / DEC-202 (owner direction, 2026-10-29: "length should not
  be the discriminator") supersede or narrow DEC-193's parked window-first material for this lane, so the length split is
  not later read as intentional.
- **Buffering.** A merged `ready` buffer is considered only against a recorded capability gap from D3, with the parity
  obligations listed in the proposal.

### D5 — Liveness after transport start or restart

- **Invariant.** After transport start or restart, any missing or stale playback view is recoverable **while transport
  runs**. A frame invalidated by an epoch change re-requests; it is never blocked for the session.
- **Acceptance criterion.** After entering PLAYING, no route depends on a publication opportunity that exists only in
  STOPPED. This catches the short-loop defect directly, independent of which reader it used.
- **Then routing.** Once the lane serves every length and recovers live, route selection sends all playback to it. The
  1-bar cold whole-loop frame and the stopped-oracle full-loop view / bridge stop being playback routes (migration debt
  with removal records). The merged fallback remains only as DEC-241's contained exceptional path.

### D6 — External clock joins last

- **Precondition:** D5 holds on device: every length on the lane, live recovery after a restart.
- **Change:** extend DEC-241's consume-or-owe from `PlaybackTickContext::inInterrupt()` to every tick.
- **RED:** a main-loop tick with a stale source performs no build.

## Execution contract

Each phase gets its own G2 execution packet under `evidence/`.

| Phase | Gate | Device |
|---|---|---|
| 1 telemetry | native counters + row/tier tests; default RAM1 96,192 | one chain: route and miss totals (baseline) |
| 2 reason | native reason fixture per predicate reachable in the harness | one chain: first failed predicate named |
| 3 capability | the 1/8/20-bar fixture result per case (pass/fail with the named gap) | — |
| 4 length gates | RED/GREEN on both gates + the interval slice; full native | chain, both clocks, routes per length |
| 5 live recovery | RED: restart and no-stopped-publication shapes recover while playing | chain with a transport restart |
| 6 routing | RED: no PLAYING route depends on a STOPPED-only publication | chain, every length on the lane |
| 7 external clock | RED: main-loop tick never builds | chains on both clocks |
| 8 qualification | — | long run (≥ 5 min, ≥ 50 seams), both clocks, 1/8/20 bars, a restart |

## Risks

- **Telemetry cost in the ITCM hot path.** → One indexed increment. Measured with the profiler's calibration and the
  ITCM line.
- **Reason enum drift.** → Each enum value has a native case where the harness can reach it. Values the harness can't
  reach are named as device-only.
- **Owner-turn latency too long for the lookahead at high BPM.** → Phases 3 and 4 measure the lead. The profiler's
  `ViewPrewarm_unit` / `PlaybackViewService_grain` show owner cost.
- **The ~126 ms internal-clock `late_on`.** → Kept as a separate measurement. Not attributed to this cause without
  evidence.
