# Task 6.5 — the native/hardware verification split: native half

Execution packet, written **before** the fixtures. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), **DEC-108 §13 verification split**
and `design.md` §"Verification split". Test-only slice: no production line changes.

## Identity and scope

- **Slice:** task 6.5's **native** half — the ten interleavings *"native tests force on the real
  request→worker→publication→consumption path with controlled ticks and explicit expected MIDI output, no sleeps and no
  copied state machines"*. This packet inventories what the suite already forces, implements the **gaps**, and records the
  **hardware** half as NOT RUN with its prerequisites (task 6.4's registered profile, an authorized upload, and the
  contention/clock legs) — never as a claim.
- **Primary files:** `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (fixtures +
  registrations) and this directory.
- **Non-goals:** no production change, no gate flip, no constant move; no hardware/device claim; no re-litigating the
  ordering proofs that already exist.

### Coverage inventory (before this slice)

| # | Native force | Already forced by |
|---|---|---|
| 1 | **A→B→A slot reuse with the first A completion arriving last** | **gap** — the S4.2 fixtures force it for the *lease*, and `test_rolling_generation_change_at_completion_publishes_nothing` forces a stale completion, but no fixture forces **view-slot reuse** with the first completion refused and the reused slot then serving |
| 2 | the interval A/B **revision trace** | partial — `test_invalidated_successor_preserves_serving_content_until_boundary_revalidation`, `test_a_late_arrival_is_named_even_when_the_served_window_carries_the_wanted_revision`, `test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary` (content parity at one revision) |
| 3 | **cancellation between every preparation grain** | partial — `test_immutable_launch_cancellation_drains_every_preparation_phase` (the launch path, a few turn counts), `test_rolling_cancellation_retires_only_its_owned_request` (one point); **no** fixture cancels at *every* grain boundary on the rolling path and then recovers |
| 4 | repeated edits without a deadline reset | `test_a_new_promise_for_the_same_interval_does_not_reset_the_intervals_miss_history`, `test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss` (M4) |
| 5 | stop/start | `test_rolling_stop_or_slot_exit_releases_the_episode_off_clock`, `test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees` |
| 6 | undo/redo | `test_undo_edit_batch_settles_its_own_soundings_and_never_blankets_the_slot`, `test_checkpoint_parity_names_the_lagging_case_and_ignores_the_windows_lag`, `test_s4b_4_the_undo_route_addresses_the_holder_the_switch_moved_to` |
| 7 | clear/load | `test_shared_view_transitions_state_the_ownership_rules_and_a_late_completion_publishes_nothing` (identity invalidated on clear/load), `test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set` |
| 8 | clock revalidation | `test_an_epoch_change_invalidates_and_names_the_promise_without_minting_an_allowance`, `test_window_deadline_clock_rejects_reset_and_unobserved_laps_at_admission`, `test_an_admitted_deadline_is_revalidated_across_an_epoch_change_and_a_new_promise_counts_from_zero`, and this change's activation audit (AA-1/AA-2) |
| 9 | resource refusal followed by recovery | `test_window_admission_refuses_by_name_once_the_interference_budget_is_enforced`, `test_committed_retry_capacity_refusal_counts_bounded_source_without_partial_publication`, and the rolling-path boundary fixture (R3, increment 32) — plus this slice's **R6**, which refuses *and recovers* at every grain boundary |
| 10 | closures crossing version boundaries | `test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing`, `test_undo_edit_batch_settles_its_own_soundings_and_never_blankets_the_slot` |

**Implemented by this slice:** forces **1** and **3** (and their recovery halves), plus the **A/B revision trace** if the
budget allows — each on the playing chain the production lane composes (`rollingTestTick` cadence), with explicit expected
MIDI output.

## Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation |
|---|---|---|---|
| **S1** A→B→A view-slot reuse, first completion last | the playing demoted 24-bar target, launch bound; the branch tip's state is otherwise untouched | A: request the first interval, drive it while the completion reader **bumps the playback generation** (so A's completion arrives *after* ownership moved); cancel A's episode; B: request again and drive it clean; then let the same interval recur on the next lap | A: its completion **publishes nothing** (no ready candidate, nothing served), the episode ends cancelled with its demand and roles released. B: takes the **same view slot** A built in (`lifecycle.build`), carries a **different operation generation**, completes and is **served**, with the emitted trace exactly the loop's own notes. The reused slot's served coverage and source generation are B's, never A's; `one_pass_late == 0` |
| **S2** cancellation at every preparation grain, then recovery | as S1 | for `k = 0..K` grains: request the interval, drive exactly `k` grains, cancel the episode, assert the state; then one clean episode to the end | after every `k`: the frame is `Idle`, no coverage source is owned, no pending demand survives, no ready candidate is published and nothing was served; the same request then completes and is served cleanly in the last round |
| **S3** the interval A/B revision trace (if reached) | as S1 | serve interval A; edit the loop's committed content (a revision change) before interval B's boundary; serve B | A's trace is the pre-edit content; B's is the post-edit content; the boundary emits the closures the edit requires **before** B's note-ons, exactly once, and no stale A note survives into B |

- **Counterexamples that must fail:** reusing the view slot without a new operation identity (B's completion would be
  refused as A's); cancelling without releasing the coverage source or the demand; publishing A's refused completion over
  B's window.
- **Preserved invariants:** the enforcement flag is left as found; capacity/span/constants untouched; every expectation
  is read from a production surface.

## Controls and coverage

- **Reused seams:** `assert_s4b_7_a_demoted_target_through_the_turn`, `rollingTestTick`, `MidiSendCapture`,
  `PlaybackPathRows`, `Track::bumpPlaybackGeneration`, `Track::cancelRollingPlaybackWindow`,
  `stepPlaybackViewMaintenance(..., completionClockReader)`, `TrackCaptureStopTestAccess::runtimeIfAllocated`, and the
  runtime's own `viewBuildOperationGeneration` / `lifecycle.{build,ready,current}` / `ownsCoverageSource()` /
  `pendingViewRequest()` readers.
- **Bounded drain:** the drive bounds are the fixtures' own tick counts; no unbounded loop, no sleeps.
- **Untestable-before-link:** `src/main.cpp` itself is not native-linkable (the suite's established limit).

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership`; broad: `pio test -e native`.
- Build: `pio run -e teensy41-capture-serial` (RAM1 must be unchanged); `git diff --check`;
  `openspec validate windowed-playback-generation --strict`.
- **Hardware: NOT RUN** — it needs task 6.4's registered profile and an authorized upload; recorded as owed, with the four
  hardware claims named (grain duration and maximum service gaps; sustained multi-track playback with capture and
  persistence contention; external-clock behaviour; memory high-water marks with boundary content parity), and with the
  design's own note that reproducing the A→B→A race on hardware is **not** required once native establishes the ordering.
- Evidence path: `evidence/2026-10-05-s65-native-verification-split/`.

## Completion record

- [x] Fixtures implemented: **S1** and **S2** (forces 1 and 3). **Deviations, recorded rather than smoothed:**
  - S1's planned step *"cancel A's episode"* is a **no-op by measurement**: A's late completion ends the episode itself
    (the frame's ownership-moved branch), so the fixture asserts the cancellation's idempotence instead of a second
    release (F-5). The planned *"then let the same interval recur on the next lap"* is realised as B's own episode on the
    lane cadence, which is what the drive actually serves.
  - S1's *"different operation generation"* is asserted, but mutation **MR-A** showed the admitted begin's stamp is **not**
    load-bearing on its own — the identity also arrives via the freeze path (F-6). The fixture's identity assertion is
    therefore not a claim about that single line; the ordering claim itself is red-proved by MR-B.
  - **S3 (force 2, the A/B revision trace) was not implemented**: it stays *partial* on the shared-view path, and the
    rolling chain's own revision trace remains a named residual gap. Budget was the only reason; no claim is made for it.
- [x] Mutations: **MR-B** (late completion not recognised) red-proves S1; **MR-C1** (demand not released) and **MR-C2**
  (owned build not discarded) red-prove S2, with S1 unaffected by MR-C1 (discrimination); **MR-A** recorded as a
  non-red-proof. Production restored byte-identically after each (`75c23a3f1849d15a…`, `src include` diff empty).
- [x] Native verification: focused **142/142**, full **2796 cases / 2789 succeeded / 7 skipped / 0 failed**.
- [x] Build/RAM1: capture build **SUCCESS**, RAM1 unchanged (`variables 96,192` / `padding 1,640`, floor 840).
  `openspec validate … --strict` valid; `git diff --check` clean.
- [x] Hardware half: **NOT RUN** (prerequisites named above, claims listed, no device claim made).
- [x] Docs: `EVIDENCE.md` + `native-force-verification.json` in this directory; change task records updated.
- [x] Task checkoff: **6.5 stays open** — its hardware half is unrun by construction here.
