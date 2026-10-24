# G2 execution packet — 2.1 prepared pair (specified; no code)

Authority: `docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice`;
change spec `openspec/changes/shipping-rolling-activation/specs/windowed-playback-generation/spec.md`; proposal family 1.

## Identity and scope

- Task: **2.1 prepared pair**. Status: **specified / fixture owed** (no production edit yet).
- Remove: `Track::resolvePreparedPairConsume` (`src/Track/TrackPlaybackWindowBuild.cpp`, decl
  `include/TrackInternal.h`), the `PreparedPlaybackPair` publication/clearing path
  (`LoopPlaybackRuntime::requestPreparedPlaybackPair` / `publishPreparedPlaybackPair` /
  `clearPreparedPlaybackPair` / `prewarmPreparedPlaybackPair`, `Track::publishPreparedPlaybackPairFromWorkingWindow`),
  the owner service (`Track::servicePendingPreparedPlaybackPair`(+Slot), `TrackManager::servicePendingPreparedPlaybackPair`,
  the `main.cpp`/`StorageManager.cpp` callers), the pending-request predicates, and the pair's consume in
  `TrackPlaybackHotPath.cpp` (`resolvePreparedPairConsume`, the `livePair` source).
- Prove: the **launch/switch routes serve from the unified reader** (`RuntimePlaybackView` window / the source identity
  with `Committed`/`Preview`/`Overlay` origin; the `win_*`/source rows) and **not** from the prepared pair.
- Primary files: the pair runtime/owner (`src/Track/TrackPlaybackRuntime.cpp`, `TrackPlaybackWindowBuild.cpp`,
  `TrackPlaybackHotPath.cpp`, `TrackManager.cpp`, `main.cpp`, `StorageManager.cpp`, `TrackInternal.h`, `Track.h`,
  `TrackManager.h`, `RuntimePlaybackView.h`); tests around the pair/launch/switch and the hot-path route.
- Preconditions present: 1.3 activation, 1.4 base/dense parity, the stable-demand demand; the unified window reader and
  the compatibility bridge are compiled. **External named limitation:** the accepted-sequence mixed switch is
  setup-limited (seed_b finalization release); its ordered cold/shipping parity is **not** claimed.
- Mutation/postconditions: the pair no longer exists as a source; **the launch/switch full-loop target's selected
  source origin is the unified whole-loop window** (the scope correction); the remaining full-loop routes that used
  the pair's merged source fall to the **raw merged fallback** (owned by 2.5, still compiled and reachable) — no
  second family removed.
- Non-goals: 2.2–2.5, the merged fallback/echo removal, `ExecutionBounds.md` row 2, any switch full-parity claim, any
  bound or RAM1 re-base.
- Commit boundary: the pair removal + the re-pointed oracle/tests; independently buildable; `ROLLING=0` cold image and
  the compatibility branches untouched.

## Oracle fixed before behavior edits

- **Case A (launch/switch source identity).** Initial: a committed **full-loop** slot with a selected switch/launch
  target; ordered actions: the qualified launch/switch gesture; expected after: the served source identity is a
  **unified whole-loop window** (origin `Committed`/`Preview`/`Overlay`) with its own interval/revision; the prepared
  pair is not published or consumed. This is the scope-correction RED: today the target publishes/consumes the pair. Counterexample that must fail: a route that selects the pair (the old `livePair` branch) after the removal
  (the test observes the pair/absence).
- **Case B (pair publication/request absence).** A launch/switch that previously queued a prepared pair must now leave
  no pending pair request/published pair and still serve the whole-loop window; a **non-launch/switch** route with no
  window (the named `SourceUnavailable`/fallback) may fall to the **raw merged fallback** until 2.5, never to a silent
  pair.
- **Case C (RED before the change).** The existing prepared-pair fixture (the publication/consume route) is RED by
  construction after the removal; it is retired/re-pointed to the window oracle. The **discriminating** control is a
  reinstated pair path (mutation) that makes the launch/switch choose the pair and fails the window-identity
  assertion; restore byte-identically.
- Preserved invariants: exactly-once source selection; no pending pair/published pair after the route; the window's
  revision/coverage; the other families' fallbacks; `ROLLING=0` behavior.

## Controls and coverage

- Existing launch/switch, source-identity, prewarm/owner and hot-path fixtures; no new production seam. The
  source-identity/`win_` rows and the pair's absence are the oracle. Mocks excluded from the asserted path.
- Busy/stale/switch-during-capture/transport-stop/overlap cases relevant to the launch/switch; the merged fallback's
  raw route is exercised as the named fallback.
- Numeric limits: existing declared grains/advances only; no re-base.
- Bounded drain; last-state failure; fixed seed if a fuzz.

## Verification recipe

- Focused: the launch/switch/source-identity/hot-path suites (exact IDs after the fixture re-point), the prepared-pair
  suite removed/retired; expected **0 failures**.
- Broad: full native; both builds at `96,192 / 392,184 + 1,032 / 34,880`; strict OpenSpec; `git diff --check`.
- Device: the qualified launch/switch gesture on the shipping image (and the cold control for the legacy route), the
  source/identity/content/ledger oracle; the mixed-oracle finalization limitation named; the cold parity comparison and
  the rollback (`09a5ae69…` ↔ `e7e4b4fc…`).
- Evidence path: `openspec/changes/shipping-rolling-activation/evidence/2026-10-24-2.1-prepared-pair/`.

## Completion record

NOT RUN (specified). Record each command/result when the slice starts; never copy a planned result as a pass.
