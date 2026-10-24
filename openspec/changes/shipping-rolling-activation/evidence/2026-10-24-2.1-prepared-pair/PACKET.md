# G2 execution packet — 2.1 prepared pair (specified; no code)

Authority: `docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice`;
change spec `openspec/changes/shipping-rolling-activation/specs/windowed-playback-generation/spec.md`; proposal family 1.

## Identity and scope

- Task: **2.1 prepared pair**. Status: **increment 1 landed** (the pair is no longer published or consumed on any
  production route). Increment 2 (delete the now-dead type/API/storage/owner wiring and re-point the remaining
  pair-own tests) is still open; 2.1 stays unchecked.
- Remove: `Track::resolvePreparedPairConsume` (`src/Track/TrackPlaybackWindowBuild.cpp`, decl
  `include/TrackInternal.h`), the `PreparedPlaybackPair` publication/clearing path
  (`LoopPlaybackRuntime::requestPreparedPlaybackPair` / `publishPreparedPlaybackPair` /
  `clearPreparedPlaybackPair` / `prewarmPreparedPlaybackPair`, `Track::publishPreparedPlaybackPairFromWorkingWindow`),
  the owner service (`Track::servicePendingPreparedPlaybackPair`(+Slot), `TrackManager::servicePendingPreparedPlaybackPair`,
  the `main.cpp`/`StorageManager.cpp` callers), the pending-request predicates, and the pair's consume in
  `TrackPlaybackHotPath.cpp` (`resolvePreparedPairConsume`, the `livePair` source).
- Prove: the **supported (demoted) launch/switch routes serve from the unified reader** (`RuntimePlaybackView` window /
  the source identity with `Committed`/`Preview`/`Overlay` origin; the `win_*`/source rows) and **not** from the
  prepared pair. **Finding that pins the scope:** `requestCommittedPlaybackWindowForSlot` deliberately refuses
  undemoted/full-take slots (the 1.4 scope split), the demoted launch prep already publishes the unified launch window,
  and `publishPreparedPlaybackPairFromWorkingWindow` requires a full-loop window -- so the pair's **only** production
  route is the **full-take legacy switch**. Removing it therefore leaves that legacy route on the **raw merged
  fallback** (2.5's family, still compiled and reachable), not on a window. Extending the window lane to full-take would
  cross the 1.4 scope split and is **not** part of 2.1.
- Primary files: the pair runtime/owner (`src/Track/TrackPlaybackRuntime.cpp`, `TrackPlaybackWindowBuild.cpp`,
  `TrackPlaybackHotPath.cpp`, `TrackManager.cpp`, `main.cpp`, `StorageManager.cpp`, `TrackInternal.h`, `Track.h`,
  `TrackManager.h`, `RuntimePlaybackView.h`); tests around the pair/launch/switch and the hot-path route.
- Preconditions present: 1.3 activation, 1.4 base/dense parity, the stable-demand demand; the unified window reader and
  the compatibility bridge are compiled. **External named limitation:** the accepted-sequence mixed switch is
  setup-limited (seed_b finalization release); its ordered cold/shipping parity is **not** claimed.
- Mutation/postconditions: the pair is no longer published or consumed on any production route; the supported
  demoted launch/switch route is unchanged (the unified window); the full-take legacy switch/active route falls to
  the **raw merged fallback** (2.5's family, still compiled and reachable) — no second family removed.
- Non-goals: 2.2–2.5, the merged fallback/echo removal, `ExecutionBounds.md` row 2, any switch full-parity claim, any
  bound or RAM1 re-base.
- Commit boundary: the pair removal + the re-pointed oracle/tests; independently buildable; `ROLLING=0` cold image and
  the compatibility branches untouched.

## Oracle fixed before behavior edits

- **Case A (full-take switch owner route).** Initial: a committed full-take (768-tick) target, active slot 0, transport
  playing, pending switch to the target. Ordered actions: `servicePendingSlotProvision`, then `updateAllTracks` across
  the switch boundary with `TrackManager::servicePendingPreparedPlaybackPair` each turn. Expected after 2.1: the target
  is active and **no pair is published** (`active=1 pair=0`); the route runs on the merged fallback. RED evidence before
  the edit: `active=1 pair=1 merged=2 order=2 len=768 req=0`, failing the `pair=0` assertion (test
  `test_2_1_the_full_take_switch_does_not_publish_a_prepared_pair`).
- **Case B (pair publication/request absence).** The full-take switch leaves no pending request and no published pair
  and runs the **raw merged fallback** until 2.5; the demoted launch/switch continues to serve the unified window and
  never touches the pair. No supported route may fall to a silent pair.
- **Case C (retired pair-own tests).** The eight pair-own tests (seven in `test_continuous_capture`, one read-witness)
  were the pair's own oracle; with the production publication/consume removed they fail and are retired. Remaining
  pair-API tests (the three direct-seed content cases and the prewarm/storage assertions) are increment 2's work: they
  exercise the dead API and must be re-pointed to the merged-fallback oracle or deleted with the type.
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
- Broad: full native (**3437 cases, 7 skipped, 3430 passed**); both builds at **96,192 / 392,040 + 1,176 / 34,880**
  (ITCM total 393,216; approved floor padding >= 840; 336 B headroom); strict OpenSpec; `git diff --check`.
- Device: **pending**. The qualified launch/switch gesture on the shipping image `15103188953f919d` (and the cold
  control `e7e4b4fc…` for the legacy route), the source/identity/content/ledger oracle; the mixed-oracle finalization
  limitation named; the cold parity comparison and the rollback (`09a5ae69…` ↔ `e7e4b4fc…`).
- Evidence path: `openspec/changes/shipping-rolling-activation/evidence/2026-10-24-2.1-prepared-pair/`.

## Completion record

**Increment 1 (production path) — DONE; 2.1 not checked.**

- Edit: removed the publication call in `Track::ensurePlaybackMergedEventsForSlot` and the `livePair` consume block in
  `TrackPlaybackHotPath.cpp`; the owner/type/API remain compiled but unreachable in production (increment 2).
- RED: `test_2_1_the_full_take_switch_does_not_publish_a_prepared_pair` failed with `active=1 pair=1 merged=2 order=2
  len=768 req=0`.
- GREEN: same test passes (`pair=0`), focused **607/607**, full native **3437 / 7 skipped / 3430 passed**, both builds
  **96,192 / 392,040 + 1,176 / 34,880** (floor 840), shipping HEX `15103188953f919d`, qualification
  `8e4216c797e95225`.
- Retired: `test_prepared_pair_publishes_off_clock_and_the_consume_decision_never_builds`,
  `test_source_change_requests_a_replacement_and_holds_the_last_coherent_pair`,
  `test_published_pair_emits_the_synchronous_oracle_trace_after_a_source_change`,
  `test_seal_admission_signal_requests_the_pair_replacement`,
  `test_gathered_pair_matches_the_synchronous_path_across_a_wrap_and_a_loop_edge_note`,
  `test_gathered_pair_holds_then_replaces_atomically_when_the_root_changes_mid_window`,
  `test_gathered_pair_waits_for_the_record_stop_root_then_publishes`,
  `test_selected_prepared_pair_has_a_real_reader_witness`.
- Increment 2 (open): delete `PreparedPlaybackPair` + the runtime pair fields/storage/API, `resolvePreparedPairConsume`,
  `publishPreparedPlaybackPairFromWorkingWindow`, the service owner (`TrackManager`/`main`/`StorageManager`), the dead
  hot-path variables/branches, the post-reload clear and the metrics row; re-point/delete the three direct-seed pair
  tests and the prewarm/whole-take storage assertions; remove the increment-1 test's pair reference; re-baseline the
  docs pin text. Device requalification + cold parity + rollback record remain as the slice gate.
- Rollback: prior shipping `09a5ae69…` (stable-demand), cold control `e7e4b4fc…`.
