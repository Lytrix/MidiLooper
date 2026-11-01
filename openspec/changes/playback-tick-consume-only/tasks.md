# Tasks — playback tick is consume-only, served by the windowed lane

Commit boundary: the working branch (`feature/unified-rolling-playback-path`), never `dev`; one reviewable commit per
group; no PR unless requested. Phases 4 and 5 are **blocked** until phase 3 records its classification.

## 0. Authorization and gates

- [ ] 0.1 Add a `## Now implementing` entry to `docs/Runtime/CURRENT_WORK.md` naming this change, its phase gating
      (no new buffering before phase 3) and its non-goals; verify no active declaration is overwritten.
- [ ] 0.2 Fill `docs/Templates/DECISION_REVIEW.md` into `evidence/DECISION_REVIEW.md` (search: windowed lane, route
      selection, merged fallback, paged delivery, bridge, readiness, DEC-193, DEC-197, DEC-241); verify it cites the
      main-spec requirements this change makes measurable.

## 1. Telemetry (report-only)

- [ ] 1.1 Write `evidence/EXECUTION_PACKET-phase1.md`.
- [ ] 1.2 Native RED: a fixture ticks a slot through each route reachable in the harness (served view, merged fallback,
      contained miss under `PlaybackTickContext::InterruptScope`, contained miss on the main-loop tick once phase 5
      exists) and asserts the per-route and per-clock counters; verify it fails before the counters exist.
- [ ] 1.3 Add the RAM2 counters beside `PlaybackTickContext::gState` (cleared by `initAtBoot()`), the single indexed
      increment after route selection in `Track::playCommittedLoopMidi`, and the miss split by clock; verify 1.2 passes.
- [ ] 1.4 Emit `DIAG,tick_route,…` from the 5 s telemetry owner (deltas per window), format literal in a carved TU,
      prefix in `isTierATextLine`; pin it in `test_capture_ring`; verify `check_marker_contract.py 'DIAG,tick_route,'`
      reports OK and `--audit` exits 0.
- [ ] 1.5 Builds: default RAM1 variables 96,192 and padding ≥ 840 (record the ITCM delta), no-grain and profile envs
      link; full native green.
- [ ] 1.6 Decoder: totals and the external/internal interval split in `scripts/parse_overdub_chain_timing.py` (or a
      small sibling), with unit tests for complete, missing-rows and Tier-A-loss captures.
- [ ] 1.7 Device baseline: one owner chain with a 45 s internal-clock hold; record route totals and misses per clock in
      `evidence/<date>-baseline-routes.md`.

## 2. Routing characterization

- [ ] 2.1 Read route selection and the windowed-lane admission chain from `Track::playMidiEvents` down to the selected
      source; write the `PlaybackRouteReject` enum with one value per predicate, in decision order, citing each
      predicate's function; verify no value is a generic "not usable".
- [ ] 2.2 Compute the first failed predicate in flash only when the merged fallback is selected; emit on change per
      slot (DEC-193 pattern); native case per reachable value; verify served ticks never compute it.
- [ ] 2.3 Device: one owner chain; record the named reason(s) for the long loop and when they change, in
      `evidence/<date>-route-reject.md`.

## 3. Capability proof

- [ ] 3.1 Native fixture: 20-bar committed loop, transport running, production producer and tick alternated across
      ≥ 10 seams with the merged fallback forbidden by assertion; oracle per design D3.
- [ ] 3.2 Record the classification — **routing/admission** (fixture passes) or **capability gap** (named failure) — in
      `evidence/<date>-capability.md`; verify it cites the phase-2 reason.

## 4. Repair (blocked on 3.2)

- [ ] 4.1 Design note for the repair against the phase-2 reason and phase-3 class; a merged `ready` buffer only with a
      recorded capability gap. Append a `DEC-###`.
- [ ] 4.2 RED/GREEN for the repair; full native; builds.
- [ ] 4.3 Device: chains on both clocks; misses and routes recorded.

## 5. External clock consume-only (blocked on 4)

- [ ] 5.1 RED: a main-loop (external-clock) tick with a stale source performs no build and owes it.
- [ ] 5.2 Extend DEC-241's consume-or-owe to every tick; GREEN; mutation check (restore the build on the main-loop tick).
- [ ] 5.3 Device: chains on both clocks.

## 6. Qualification

- [ ] 6.1 Long device run (≥ 5 min, ≥ 50 window seams) on both clocks: `tick_playback_miss == 0` on each clock,
      merged-fallback route use 0 or justified per occurrence, profiler `PlaybackWindow_build` from the ISR = 0, MIDI
      timing reported per clock interval; record in `evidence/<date>-qualification.md`.

## 7. Closeout

- [ ] 7.1 Update `docs/Runtime/PROJECT_STATE.md`, `CURRENT_WORK.md`, `docs/DELIVERABLE_TRACKING.md`; record the merged
      fallback's removal trigger status against the main spec's migration rule.
- [ ] 7.2 `openspec validate playback-tick-consume-only --strict`; archive only after phase 6 passes.
