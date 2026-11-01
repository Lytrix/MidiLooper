# Tasks — playback tick is consume-only, served by the windowed lane

Commit boundary: the working branch (`feature/unified-rolling-playback-path`), never `dev`; one reviewable commit per
group; no PR unless requested. Phases 4–8 are **blocked** until phase 3 records its classification. The order (owner, 2026-11-01): prove one lane
for every length → remove the length gates → live recovery after restart → route all playback through the lane →
retire the length-specific readers → consume-only on both clocks. No new buffer and no optimization before that.

## 0. Authorization and gates

- [x] 0.1 Add a `## Now implementing` entry to `docs/Runtime/CURRENT_WORK.md` naming this change, its phase gating
      (no new buffering before phase 3) and its non-goals; verify no active declaration is overwritten.
- [x] 0.2 Fill `docs/Templates/DECISION_REVIEW.md` into `evidence/DECISION_REVIEW.md` (search: windowed lane, route
      selection, merged fallback, paged delivery, bridge, readiness, DEC-193, DEC-197, DEC-241); verify it cites the
      main-spec requirements this change makes measurable.

## 1. Telemetry (report-only)

- [x] 1.1 Write `evidence/EXECUTION_PACKET-phase1.md`.
- [x] 1.2 Native RED: a fixture ticks a slot through each route reachable in the harness (served view, merged fallback,
      contained miss under `PlaybackTickContext::InterruptScope`, contained miss on the main-loop tick once phase 5
      exists) and asserts the per-route and per-clock counters; verify it fails before the counters exist.
- [x] 1.3 Add the RAM2 counters beside `PlaybackTickContext::gState` (cleared by `initAtBoot()`), the single indexed
      increment after route selection in `Track::playCommittedLoopMidi`, and the miss split by clock; verify 1.2 passes.
- [x] 1.4 Emit `DIAG,tick_route,…` from the 5 s telemetry owner (deltas per window), format literal in a carved TU,
      prefix in `isTierATextLine`; pin it in `test_capture_ring`; verify `check_marker_contract.py 'DIAG,tick_route,'`
      reports OK and `--audit` exits 0.
- [x] 1.5 Builds: default RAM1 variables 96,192 and padding ≥ 840 (record the ITCM delta), no-grain and profile envs
      link; full native green.
- [x] 1.6 Decoder: totals and the external/internal interval split in `scripts/parse_tick_routes.py` (a small
      sibling of the scorer), with unit tests for complete, missing-rows and Tier-A-loss captures.
- [x] 1.7 Device baseline: one owner chain with a 45 s internal-clock hold; record route totals and misses per clock in
      `evidence/<date>-baseline-routes.md`.

## 2. Routing characterization

- [x] 2.1 Read route selection and the windowed-lane admission chain from `Track::playMidiEvents` down to the selected
      source; write the `PlaybackRouteReject` enum with one value per predicate, in decision order, citing each
      predicate's function; verify no value is a generic "not usable".
- [x] 2.2 Compute the first failed predicate in flash only when the merged fallback is selected; emit on change per
      slot (DEC-193 pattern); native case per reachable value; verify served ticks never compute it.
- [x] 2.3 Device: one owner chain; record the named reason(s) for the long loop and when they change, in
      `evidence/<date>-route-reject.md`.

## 3. Capability proof — one lane, every length

- [x] 3.1 Write `evidence/EXECUTION_PACKET-phase3.md` (fixture shape, oracle, the test-only gate bypass, what a RED means).
- [x] 3.2 Native fixture on the production producer (`serviceRollingPlaybackWindow` / `stepPlaybackViewMaintenance`) and
      the production tick, **1-bar, 8-bar and 20-bar** loops through the **same** windowed lane, with the merged fallback
      forbidden by assertion. The two length gates are bypassed by a native-only switch so capability is measured apart
      from them; the same fixture with the switch off records today's refusal per length. Cases, each across ≥ 10 seams
      where the loop has them:
      - cold / restored slot entering PLAYING with no stopped-time publication;
      - transport restart (new clock epoch) mid-loop;
      - source revision change (an overdub publication) while playing;
      - steady playback.
      Oracle: every tick served by a published window that covers it (design D3), note-on/off closure across each seam
      matches committed content, no build on a tick, and the requested window is `clamp(lookahead span, loop extent)`.
- [x] 3.3 Record the classification per length and per case — **routing/admission** (passes with the gates bypassed) or
      **capability gap** (named failure) — in `evidence/<date>-capability.md`; cite the phase-2 reasons
      (`clock_epoch_changed` on the 20-bar loop, `frame_idle` on the short loops).

## 4. Remove the length gates (blocked on 3.3)

- [x] 4.1 Design note and `DEC-###`: the repair against the phase-2 reasons and phase-3 classes; one window request
      policy, `requested window = clamp(lookahead span, loop extent)`, length as geometry and never routing. State
      explicitly how DEC-200 / DEC-202 (owner direction, 2026-10-29) supersede or narrow DEC-193's parked window-first
      material for this lane. A merged `ready` buffer only with a recorded capability gap.
- [x] 4.2 RED/GREEN: `requestCommittedPlaybackWindowForSlot` no longer answers `NotServable` by length (DEC-200's
      keystone), and `serviceRollingPlaybackWindow` no longer exits `win_window_ineligible` by length; the interval slice
      lands with it (`playbackWindowInterval` loses `demoted`; its two discriminator-shaped fixtures re-derived against
      the final policy, never number-bumped).
- [ ] 4.2b **(blocked: owner decision, DEC-242 "Open")** RED/GREEN for capability defect B (`evidence/2026-11-01-capability.md`): the successor window is requested
      **while the current one serves**, positioned at its end, so a steady loop of any length stays on the lane across
      every seam (today: one two-bar window in four, the merged fallback covering the rest).
- [x] 4.3 Full native; three env builds; device chain with both clocks: route and reason rows recorded per length.

## 5. Live recovery after transport start / restart (blocked on 4)

- [ ] 5.0 RED (device finding, `evidence/2026-11-01-phase4-gates.md`): a restored slot whose boot prewarm reserved no
      view set and whose source is pass-only (no current root) becomes servable **while playing** — the producer acquires
      the view set and retains the source off-clock, bounded, instead of exiting `win_not_servable` for the session.
- [ ] 5.1 RED: after a transport restart (new epoch) a blocked or stale frame recovers **while transport runs** — the
      20-bar `frame_blocked(clock_epoch_changed)` shape (capability defect C) and the revision-change
      `frame_blocked(candidate_rejected)` shape (defect D) — and a slot with no stopped-time publication is served by the
      lane once PLAYING, within a stated acquisition bound (today 6.7–7.7 bars from a cold entry).
- [ ] 5.2 GREEN in the producer (an epoch-invalidated frame re-requests instead of blocking permanently); mutation check.
- [ ] 5.3 Device: chain with a transport restart; no slot stays on the merged fallback after the restart.

## 6. Route all playback through the lane (blocked on 5)

- [ ] 6.1 RED: after entering PLAYING, no route depends on a publication opportunity that exists only in STOPPED; every
      sounding slot of every length is selected on the windowed lane.
- [ ] 6.2 GREEN: route selection; the merged fallback becomes the exceptional contained path only.
- [ ] 6.3 Retire or deprecate the length-specific readers (the 1-bar cold whole-loop frame, the stopped-oracle full-loop
      view / bridge as a playback route), each with a removal record; display contracts untouched.

## 7. Both clocks consume-only (blocked on 6)

- [ ] 7.1 RED: a main-loop (external-clock) tick with a stale source performs no build and owes it.
- [ ] 7.2 Extend DEC-241's consume-or-owe to every tick; GREEN; mutation check (restore the build on the main-loop tick).
- [ ] 7.3 Device: chains on both clocks.

## 8. Qualification

- [ ] 8.1 Long device run (≥ 5 min, ≥ 50 window seams) on both clocks, with 1-, 8- and 20-bar slots sounding and a
      transport restart: `tick_playback_miss == 0` on each clock; merged-fallback route use 0 or justified per
      occurrence; no route reason left open after a restart; profiler `PlaybackWindow_build` from the ISR = 0; MIDI timing
      per clock interval; record in `evidence/<date>-qualification.md`.

## 9. Closeout

- [ ] 9.1 Update `docs/Runtime/PROJECT_STATE.md`, `CURRENT_WORK.md`, `docs/DELIVERABLE_TRACKING.md`; DEC-200's census
      updated (keystone and producer twin gone); the merged fallback's removal trigger status against the main spec's
      migration rule.
- [ ] 9.2 `openspec validate playback-tick-consume-only --strict`; archive only after phase 8 passes.
