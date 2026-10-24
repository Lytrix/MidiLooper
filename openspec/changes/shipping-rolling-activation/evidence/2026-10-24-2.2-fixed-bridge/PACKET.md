# G2 execution packet — 2.2 fixed/bridge (specified; no code)

Authority: `docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice`;
change spec `openspec/changes/shipping-rolling-activation/specs/windowed-playback-generation/spec.md`; family 2.

## Identity and scope

- Task: **2.2 fixed/bridge**. Status: **specified / fixture owed** (no production edit yet).
- Remove:
  - `Track::resolveFixedPlaybackSourceForTurn` (`src/Track/TrackPlaybackWindowBuild.cpp`, decl `include/Track.h`) and
    the hot-path `fixedBridge`/`fixedView` selection it feeds (`TrackPlaybackHotPath.cpp`).
  - The `FixedPlaybackBridgeStorage` **selection** and the whole-take bridge reservation it needs:
    `LoopPlaybackRuntime::selectedFullLoopBridge` / `fullLoopBridgeBase` and the view-set
    `bridges`/`bridgeBases` candidate/selection entries; the bridge builders' production callers
    (`publishStoppedCanonicalOracleView`/`beginCandidateBridge`/`selectCandidateBridge`, the overdub closure selection in
    `TrackOverdubLifecycle.cpp`).
  - `LoopPlaybackRuntime::boundLaunchView` (the 3.7e launch seed) and its callers/tests.
- Prove: the **committed routes serve from the window reader** (`RuntimePlaybackView` rolling/published window with its
  own source identity/coverage), not from the fixed full-loop bridge or the bound launch. The **full-take base** route
  (where the window lane refuses undemoted slots, the 1.4 scope split) falls to the **merged fallback** (2.5's family)
  like 2.1; the note-edit/overdub overlays fall to the live/preview window or the named fallback.
- Primary files: `TrackPlaybackWindowBuild.cpp`, `TrackPlaybackHotPath.cpp`, `TrackPlaybackRuntime.cpp`,
  `RuntimePlaybackBridge.{h,cpp}`, `RuntimePlaybackView.{h,cpp}`, `TrackOverdubLifecycle.cpp`, `Track.h`,
  `TrackPlaybackRuntime.h`; tests for prewarm/bridge, the launch seed, note-edit/overdub overlays and the hot-path
  source.
- Preconditions: 2.1 checked (family removed; base clean device pair); 3.15e live overlay and 3.15d preview window are
  the supported overlay routes; the rolling window demand is active. **Named limitation:** the accepted-sequence mixed
  switch is setup-limited; its ordered parity is not claimed.
- Mutation/postconditions: no `FixedPlaybackBridgeStorage` selection, no `boundLaunchView`, no fixed resolver on the hot
  path; the committed route's selected source is the rolling/published window; the full-take base and any overlay the
  window cannot serve fall to the merged fallback (still compiled; removed in 2.5). No second family removed.
- Non-goals: 2.3–2.5, the merged fallback/echo, the paged emission (2.4), `ExecutionBounds.md` row 2, any switch
  full-parity claim, any RAM1 re-base (the bridge storage is external; the runtime size/pins are re-measured, not
  re-based).
- Commit boundary: the fixed/bridge removal + the re-pointed oracles; independently buildable; `ROLLING=0` cold image
  and the compatibility branches untouched.

## Oracle fixed before behavior edits

- **Case A (committed route source).** Initial: a committed slot with a current published root and an active
  committed/rolling traversal. Ordered actions: the ordinary playback turns. Expected after: the selected source is the
  **rolling/published window** (its source identity/coverage; `win_*` rows), and the fixed resolver returns no bridge.
  Counterexample: the old `selectedFullLoopBridge`/`fullLoopBridgeBase` selection still serving after the removal.
- **Case B (launch seed) — SCOPE FINDING from the first edit attempt.** `boundLaunchView` is the **first target tick's
  fixed source**, not only a launch helper: the shared rolling helper asserts "launch and first target tick must reuse
  preparation, including empty coverage" (RWO line 3343), and removing the view makes the first tick open **3** window
  demands across ~20 rolling tests (`0→3`, `30→33`, …) because the rolling lane's own seed (`launchBoundTick`) runs on a
  later turn. The slice must therefore choose **before** the removal:
  - **(a) wire the lane seed into the first tick** (the `launchBoundTick`-based seed/adopt, no `boundLaunchView`), so
    the reuse and the demand counters survive; or
  - **(b) accept the first-tick demand** and re-point the helper invariant/counters (the 3.7e reuse optimization is
    intentionally given up).
  The 3.7e seed test stays the proof; a reinstated fixed-view seed (mutation) must fail the chosen assertion.
  **Refined finding from the second attempt (owner-lane seed is too broad):** the seed lives only in
  `serviceRollingPlaybackWindow`'s owner turn; the hot-path resolver cannot seed without changing the rolling suite's
  intended `Idle` start (~20 tests, +3 hot rebuilds each: `handoff_shortfall`, `blocked_demand`, `queued_zero_start`,
  `natural_wrap`, `empty_interval`, …). The removed wrapper was the **commit-tick** source: at the commit tick the
  rolling lane still rejects on `hasQueuedPlaybackStart()`, so the first target tick falls to the merged fallback.
  Direction chosen by the owner: seed the frame at the **commit/bind inside the lane** (the `launchBoundTick` adopt,
  allowed through the queued-start guard for that one commit tick), and re-point the shared helper + the rolling tests
  to start from the seeded frame.
  **Third finding (cold hot path).** The native RWO suite's **firmware hot path is compiled cold**: the native env
  carries `PIO_UNIT_TEST_NATIVE` but not `PLAYBACK_WINDOW_ROLLING_ENABLED`; the suite's own `#define` only covers the
  test TU. The shared first-tick invariant is therefore the **cold** `boundLaunchView` source, not the rolling lane.
  A bind seed was measured to adopt (`BINDSEED r=0` at the commit) but the cold hot path never calls
  `resolveRollingPlaybackWindow`, so the +3 first-tick rebuilds remained. The slice split must be explicit:
  - **rolling/shipping path:** remove the resolver's `boundLaunchView` use and seed the lane at the commit — the
    shipping device proof;
  - **cold compatibility (`ROLLING=0`):** keep `boundLaunchView` until 2.5 (the cold image is the rollback/reference,
    and the native suite runs this path), exactly as 2.1 kept the merged fallback. Removing it from the cold build
    changes the cold reference and requires re-pointing the shared helper.
  The owner's directive ("remove boundLaunchView, seed the lane") is satisfied on the rolling path; the cold wrapper is
  the named compatibility, not a second family removal.
  **Fourth finding (order trace, 2026-10-24).** Instrumented the hot path and the resolver. At the commit tick the
  order is: `BIND` -> `COMMITSEED reason=0 (Adopted)` -> hot path (`eligible=1`) -> `QAFTER view=null bridge=null` ->
  merged fallback. The resolver trace is **absent** for that consumer call (the resolver returns at its
  `phase == Idle` gate), while the lane call later in the same tick sees the seed **Serving** (`RRES fphase=3`,
  `cur==bview`, all serving terms pass). A state reset between the commit seed and the hot-path consumer turn clears the
  frame. Candidates to trace next: `syncRevision`/`applyActiveLoopIndex`/`resetPlaybackStateForSlot` in the switch
  commit, or the hot path's pre-resolution stale reset. The removed wrapper served because it does not depend on the
  rolling frame's lifecycle. Next concrete step: trace `rolling.phase` at the hot-path entry and after each early
  branch, pin the reset, and re-seed after it (or make the reset preserve a just-seeded launch frame).
  **Fifth finding (reset narrowing).** The frame is already `phase=0 (Idle)` at the mid-function `HPPRE` inside
  `Track::playCommittedLoopMidi` (hot-path entry for the target, line ~228/255), and `cancelRollingPlaybackWindow` is
  **not** called for slot 2 at the commit tick. The reset is therefore between the commit seed and that point:
  `playMidiEvents`/`playMidiEventsForSlot`, `playCommittedLoopMidi`'s early code (228-255), or a manager call after the
  commit. Next concrete trace: `playCommittedLoopMidi` entry (228), `playMidiEventsForSlot` (621), and the commit end
  (`commitQueuedPlaybackStart` after the seed); one of those three will show the Serving→Idle transition.
  **Sixth finding (static inspection).** The three entry paths contain no reset: `playMidiEvents` (line 608),
  `playMidiEventsForSlot` (621) and `playCommittedLoopMidi` (228) only check the stop/barrier/audibility predicates and
  resolve the source; `playCommittedLoopMidi` reaches `HPPRE` without touching `rolling`. The reset is therefore
  **before** `playCommittedLoopMidi`: the manager's post-commit switch block (edge effects / enabled-set replacement /
  the `commitQueuedPlaybackStart` at 314) or `stepPlaybackViewMaintenance` (the owner lane, `TrackPlaybackWindowBuild.cpp`
  6333; its cold-frame reset at 6573 and the preview reset at 7654 are the only other `rolling = {}` sites besides
  `cancelRollingPlaybackWindow` 4980). Next trace: a `SWITCHEND` marker after the switch block, a `playMidiEvents` entry
  marker, and a marker at `stepPlaybackViewMaintenance` entry/exit; then re-seed after the reset.
- **Case C (overlay).** Initial: a committed note-edit/overdub overlay route that today selects a committed bridge.
  Ordered actions: the overlay/serve turns. Expected after: the live/preview window serves the rows (3.15e/d), or the
  named fallback; the bridge is not selected. Counterexample: an overlay still reading the bridge storage.
- **RED before the change:** the existing bridge/seed fixtures are the route's own oracle; they must fail after the
  removal and be re-pointed (the fixed/bridge tests in `test_playback_prewarm`, the 3.7e bound-launch rows, the
  note-edit/overdub bridge rows). The discriminating mutation is a reinstated bridge/seed selection, restored
  byte-identically.
- Preserved invariants: exactly-once source selection; the window's revision/coverage; the overlay's once-only rows; the
  merged fallback for the full-take base; `ROLLING=0` behavior.

## Controls and coverage

- Existing committed/rolling, launch/switch, note-edit/overdub and prewarm fixtures; no new production seam. The
  `win_*`/source identity and the absence of a bridge selection are the oracle. Mocks excluded from the asserted path.
- Busy/stale/switch-during-capture/transport-stop/overlap cases relevant to the committed routes; the merged fallback
  is the named fallback.
- Numeric limits: existing declared grains/advances; the bridge reservation removal changes external bytes only; the
  RAM1 pins are re-measured (no re-base).
- Bounded drain; last-state failure; fixed seed if a fuzz.

## Verification recipe

- Focused: prewarm/bridge, the launch seed (3.7e), note-edit/overdub overlays, the hot-path source suites; the
  fixed/bridge fixtures removed/re-pointed; expected **0 failures**.
- Broad: full native; both builds at the measured pins; strict OpenSpec; `git diff --check`.
- Device: the clean base pair (as 2.1), the note-edit/overdub overlay route and the launch/switch gesture on shipping +
  cold; the mixed-oracle limitation named; the cold parity and the rollback (prior shipping/cold HEX).
- Evidence path: `openspec/changes/shipping-rolling-activation/evidence/2026-10-24-2.2-fixed-bridge/`.

## Completion record

Six `boundLaunchView` editing attempts (**reverted**; findings above). No production edit retained. Record each command/result when the slice starts; never copy a planned result as a pass.
