# C5.3b — how much lead does one successor window need? (measurement, host)

Date: 2026-11-02. Host only; nothing flashed. Owner rulings: DEC-246 amendment 24 (measure before changing
admission), amendment 23 (continuity primitive before C5.4).

## Method

**Probe.** `include/RollingSuccessorProbe.h`. It is compiled on the host only: `PLAYBACK_ROLLING_SUCCESSOR_PROBE` is
0 on the target, so the device images are unchanged.

- The successor is opened only while the active slot's frame is `Serving` window N (`[N.start, N.end)`, absolute).
- **The demand.** The requester (`Track::serviceRollingSuccessorDemand`, from the `win_serving` exit) opens one demand:
  - interval `[N.end, N.end + span)`;
  - absolute deadline `N.end`, derived from N's own start and span, never from the playhead;
  - opened no earlier than `lead` ticks before that deadline.
- **Keying and stamping.** One demand per served window, keyed by `N.end` (a second one would be counted as
  `duplicateStarts`). The deadline is written once and never repositioned or restamped.
- **The production path it goes through:** retry, qualification, admission, build, completion.
- **The only rule skipped is the DEC-147 lead price.** It is charged twice, so it is waived at both sites, and only
  for this demand:
  - `leadTooShort` in `Track::stepCommittedPlaybackRetry`;
  - `canCompleteWithinLead` in `LoopPlaybackRuntime::beginQualifiedCoverageBuild`.

  The deadline, the sibling term, the qualification and every other admission check still apply.
- **Completion.** The completed build is published as the **ready successor** and never as current. N stays current
  until `N.end`.
- **At `N.end` exactly.** The resolver re-keys the frame to the successor and takes the one existing handoff
  (`rollingCandidateReadyAtBoundary`, `adoptPreparedPlaybackWindow`). It is either adopted at that tick, or it is a named
  miss:
  - the handoff rejects and blocks the re-keyed frame;
  - the requester then cancels it by name, and the lane positions a fresh window as it does after any expiry.
- **Discarding.** A successor that cannot publish (late, or a moved frame or transport) is discarded by name. None of
  this is tick work: fixed metadata only.

**Fixture.** `test/test_rolling_successor_lead`.
- The C5.3a harness (`test/test_revision_manifest_lane/manifest_lane_harness.h`) runs the real Track/Loop/storage
  closure with rolling on and the production holder lane: requesters, offers, round-robin selection,
  `stepPlaybackViewMaintenance` and `playMidiEvents`.
- **Dimensions:**
  - shape: 1-, 4-, 8- and 20-bar loops, sparse (one note per bar) and dense (32 notes per bar, 128 events per two-bar
    window);
  - a production Delete edit (Hide) before playback;
  - the owner cadence `A` (ticks per owner turn);
  - forced denials: `deny7` denies track 0's next seven holder grains after each one served, i.e. the declared
    `maximumConsecutiveDeniedTurns`;
  - real arbitration: `press7` plays the same shape on seven more tracks through the same round-robin lane;
  - a source move (Delete edit) while a successor is being built;
  - both sources: snapshot (no reader) and manifest (the routed track 0 with the host reader owner).
- **Sweep.** For each case: one span, halved down to 48 ticks (a sixteenth), then a bisection below the last passing
  point, down to one tick. A lead passes when every successor of a 20-seam run is handed off with no miss and no
  cancel.

**Units.** The host has no wall clock: one owner turn is `A` ticks. A measured lead is a count of owner turns, scaled
by the device's real advance per turn. The host cannot measure that advance; see "What this does not show".

## Results

**With the declared price in force** (bypass off), every successor at `N.end` is refused by name
(`PriceRefused`): 12 of 12, on both sources. The lane keeps requesting one per served window, and no refused demand
outlives its refusal. The admission's own price for the window, once qualified (`pricedGrains`), is:
- **sparse:** 4–5 grains;
- **dense:** 55 grains, i.e. a lead of 55 x 8 x 64 + 768 = 28,928 ticks, against one span of 1,536.

**Measured: the smallest lead at which every successor of a 20-seam run is handed off.** Units are owner turns; ticks
equal turns at A = 1.

| Case | Source | Request → ready (turns) | Qualification (turns) | Grains served | Grains denied | **Minimum lead** |
|---|---|---|---|---|---|---|
| 4-bar sparse | snapshot | 5 | 2 | 6 | 0 | **6** |
| 4-bar sparse | manifest | 5 | 2 | 6 | 0 | **6** |
| 20-bar sparse | manifest | 5 | 2 | 6 | 0 | **6** |
| 4-bar dense | snapshot | 13 | 4 | 14 | 0 | **14** |
| 8-bar dense | snapshot | 13 | 4 | 14 | 0 | **14** |
| 20-bar dense | snapshot | 13 | 4 | 14 | 0 | **14** |
| 4-bar dense + Hide | snapshot | 13 | 4 | 14 | 0 | **14** |
| 4-bar sparse + Hide (index built) | manifest | 5 | 2 | 6 | 0 | **6** |
| 4-bar dense, 7 denials per grain | snapshot | 111 | 39 | 14 | 98 | **119** |
| 20-bar dense, 7 denials per grain | snapshot | 111 | 39 | 14 | 98 | **119** |
| 4-bar sparse, 7 denials per grain | manifest | 47 | 23 | 6 | 42 | **48** |
| 4-bar dense, 8 real tracks | snapshot | 104–111 | 32–39 | 14 | 91–98 | **112** |
| 20-bar dense, 8 real tracks | snapshot | 111 | 39 | 14 | 98 | **112** |
| 4-bar sparse, 8 real tracks | manifest | 41–47 | 18–23 | 6 | 35–42 | **42** |
| 4-bar dense, A = 8 ticks per turn | snapshot | 104 ticks | 32 ticks | 14 | 0 | **112 ticks (14 turns)** |
| 4-bar dense, 7 denials, A = 8 | snapshot | 888 ticks | 312 ticks | 14 | 98 | **952 ticks (119 turns)** |

- **Physical minimum:** 6 owner turns for a sparse window and 14 for a dense one (128 events). It is
  **independent of loop length**: the work is bounded by the window, not the loop.
- **Qualified minimum:** **119 owner turns** under the declared worst pattern (seven denials after every grain), and 112
  under real eight-track round-robin arbitration.
- **Why the worst run is worst:** it is the arbitration, not the content. The cost is exactly
  `grains x (denied + 1)` plus the qualification turn: 14 x 8 = 112, and 119 with the qualification's own turns. Content
  only sets the grain count (6 sparse, 14 dense), and length does not enter at all.
- **Variance:** none across 20 seams of a steady run (deterministic host scheduler). The bisection boundary is
  sharp to one turn. Two real-arbitration runs (8 tracks, lead 108 and 111) handed off 19 of 20, with one cancelled at
  the boundary: the round-robin phase relative to the seam is the only variable.
- **Linear in the owner cadence:** A = 8 multiplies every tick figure by 8 and leaves the turn counts unchanged.

**Continuity gate** (amendment 24): the successor is requested at N's own start (lead = one span), over 50
consecutive seams.

| Case | Source | Seams | Handed off | Lane-owned merged ticks | Gaps (non-view ticks) | Duplicate starts | Stale serves | Leaked demands |
|---|---|---|---|---|---|---|---|---|
| 4-bar sparse | snapshot | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 4-bar sparse | manifest | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 4-bar dense | snapshot | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 8-bar dense | snapshot | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 20-bar dense | snapshot | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 20-bar sparse | manifest | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 4-bar dense, 8 tracks | snapshot | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 4-bar sparse, 8 tracks | manifest | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| 1-bar | both | (one whole-loop window) | — | 0 | 0 after the first served tick | — | — | — |

The same 4-bar slot was 83 % merged fallback before C5.3a (7,680 of 9,216 ticks). With the successor demand it is
0 %, on both sources.

**Source move during preparation** (a Delete edit while seam 4's successor is being built), on both sources:
- **The moved candidate is never restamped and never served.** It finishes for the old source, is refused at `N.end`
  (`CandidateRejected`), and is named `Missed`. Stale adoptions: 0.
- **The lane resumes on the new source:** 19 of 20 seams were handed off.
- **Recovery is not free.** After the miss, the fresh window is positioned with the full DEC-147 lookahead:
  - snapshot source: 32,256 ticks (21 bars) on the merged fallback;
  - manifest source: 4,608 ticks of named silence.

  This is the recovery-after-revision-change cost the Undo/Redo fixture will meet.

## Defects found and fixed in the probe (independent review plus traces)

- **A cancelled successor came back as a pending request.** `retireCoverageSource` hands an active frozen demand back
  as pending. The frozen record is now cleared first, as `cancelRollingPlaybackWindow` does.
  - Mutation: the price case fails with 1,533 leaked-demand ticks, and only one successor is ever requested.
- **A missed or cancelled successor left the lane without a build role.** With a retained view, current and successor
  alternate over two views, and the successor's publication took the build role. A revision change can also reset the
  ready candidate to a shell, which `retireSupersededReady` refuses. The successor's view is now handed back to the
  build role, as `handoff` reclaims a retired view.
  - Mutation: the source-move case stalls after the miss (4 handoffs, then `win_busy` forever).
- **Two more probe fixes:**
  - a duplicate start now marks the window as having no demand (it previously armed a phantom demand);
  - a frame blocked while serving now discards its successor by name.
- The suite's `main` ran a debug case when the review read it; it now registers all five cases.

## Found, not fixed: the manifest route cannot acquire a dense take (C5.3a defect, owner question)

On the routed track, a dense 4-bar take is never served.
- **Mechanism.** The routed slot prices its first window from the declared contract only, because it must never read
  the root: 3/3/1 = 7 grains. The window really costs about 14 grains.
- **What happens.** The admission refuses it (`win_repositioned`) twice, and the DEC-147 traversal budget then blocks
  the frame for good (`win_blocked_by_budget`).
- **The successor primitive is not involved:** the first window never serves.
- **Impact.** On the device, a dense take on the designated track would stay silent. The manifest half of this
  measurement is therefore on sparse shapes only.
- **Repair options:**
  - the reader owner records a densest-span bound when it imports (metadata, off-clock), and the lane prices from it;
  - a conservative bound: the manifest's row count clamped to the window admission limit.

## What this does not show

- **Ticks per owner turn on the device.** The host shows the work is 6–14 owner turns, and 112–119 under worst
  arbitration. Whether one span (1,536 ticks) covers that depends on the device's real advance per owner turn:
  - the qualified 119 turns fit within a span only if the worst turn is at most 12.9 ticks (1,536 / 119), about 34 ms at
    120 BPM (384 ticks/s);
  - the declared price assumes 64 ticks per turn.
  - Earlier device rows put the lane's refusal cadence at 40–68 ms. At 40 ms, 112 turns is about 4.5 s, more than a
    2-bar span at 120 BPM (4 s). The single-successor question is therefore **not settled by the host**.
- **Not exercised on the host:** display and save owners competing for the main loop. The host has no such pressure;
  `deny7` and `press7` stand in for it.
- **Not covered:** manifest dense shapes (the defect above), production Undo/Redo (the next fixture), and the device.

## Decision inputs for the owner (amendment 24, "afterwards")

- **In owner turns,** the measured bound (119) is far below one span. That is the "measured bound < window span"
  branch, but only once the device's ticks per owner turn are known.
- **Recommended next measurement:** a device probe of track 0's holder-grain interval under the eight-track load (ticks
  between served grains, maximum and distribution).
  - This needs a device run.
  - It would turn the 119-turn figure into ticks and decide between an evidence-based successor price and a deeper
    lookahead.

## Verification

- **Full native suite** (final tree): 3,930 cases, 8 skipped, 3,922 passed, 0 failed. C5.3a had 3,925 cases; the
  difference is the 5 new `test_rolling_successor_lead` cases.
- **One earlier full run** had 22 suites that did not compile: suites built with rolling off reached probe blocks whose
  helpers are rolling-only. Every probe block in `TrackPlaybackWindowBuild.cpp` is now guarded by both macros.
- **Device images.** `HEAD` (`1bb853e7`) and the final tree were built from one identical path, with an isolated
  `PLATFORMIO_BUILD_DIR` and `SOURCE_DATE_EPOCH=1793000000`. The HEX files are byte-identical, so the probe compiles out
  on the device:

  | Env | HEX (both) | RAM1 code + padding | variables / locals |
  |---|---|---|---|
  | `teensy41-capture-serial` | `b4ee9921cf45347c…` | 392,008 + 1,208 | 96,192 / 34,880 |
  | `teensy41-capture-serial-revstore-probe` (flag 0) | `37e5ebc15cfb05f9…` | 392,072 + 1,144 | 96,192 / 34,880 |
  | `teensy41-capture-serial-revstore-reader` (flag 1) | `3f43e95fe9d0feec…` | 392,280 + 936 | 96,224 / 34,848 |

  The ITCM figures differ from C5.3a's (391,944 for the default). That is a different build path, and the same
  difference shows for `HEAD`. Nothing was flashed.
- **Suite runtime:** about 4 minutes (`test_rolling_successor_lead`, mostly the sweep). `C53B_ONLY=price|move` runs
  one case.
