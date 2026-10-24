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
  The 3.7e seed test stays the proof; a reinstated fixed-view seed (mutation) must fail the chosen assertion. Do not
  delete the view before this decision.
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

First `boundLaunchView` edit attempted and **reverted** (packet finding above). NOT RUN otherwise. Record each command/result when the slice starts; never copy a planned result as a pass.
