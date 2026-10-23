# Task 6.5 — the native half: the two forces the suite did not force

Evidence for `EXECUTION.md` in this directory. Task 6.5 asks for the ten interleavings *"native tests force on the real
request→worker→publication→consumption path with controlled ticks and explicit expected MIDI output"*, and it is the
task that also owes the **hardware** half. This slice implements the native half's **gaps** on the playing chain and
records the hardware half as **NOT RUN**. No production line changed; the mutations below are the evidence.

## What was already forced, and what was not

The inventory in `EXECUTION.md` names ten forces. Two were **gaps** and are implemented here:

| Force | Before | Now |
|---|---|---|
| **1 — A→B→A view-slot reuse with the first A completion arriving last** | gap: the S4.2 fixtures force it for the *lease*, and `test_rolling_generation_change_at_completion_publishes_nothing` forces a stale completion, but nothing forced **view-slot reuse** with the refused completion's slot then serving | **`test_rolling_path_reuses_the_view_slot_after_a_refused_completion` (S1)** |
| **3 — cancellation between every preparation grain** | partial: `test_immutable_launch_cancellation_drains_every_preparation_phase` (launch path, a few turn counts) and `test_rolling_cancellation_retires_only_its_owned_request` (one point) | **`test_rolling_path_cancellation_between_every_preparation_grain_recovers` (S2)** |

Force **2 — the interval A/B revision trace** stays **partial** and is *not* claimed: on the shared-view path it is
covered by `test_invalidated_successor_preserves_serving_content_until_boundary_revalidation`,
`test_a_late_arrival_is_named_even_when_the_served_window_carries_the_wanted_revision` and
`test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary` (content parity at one revision); the
rolling chain's own revision trace is a residual gap, named here rather than counted as done.

## S1 — the refused completion's slot is reused, under a new identity

Fixture: `test_rolling_path_reuses_the_view_slot_after_a_refused_completion`. Setup: the playing demoted 24-bar target
through the production edge (`assert_s4b_7_a_demoted_target_through_the_turn(true)`), so slot 2 is the playing slot and
its launch is bound. The fixture moves the generation once itself (`bumpPlaybackGeneration`) so that A's operation
identity is non-degenerate: "stamped" and "unstamped" must be distinguishable for the identity claim to mean anything.

Measured, in order (probe of this slice, then asserted):

1. **A's demand opens** and its build begins 3 service turns later; the admitted work record owes exactly the promise the
   frame made (`viewBuildWork.deadlineTick == rolling.deadlineTick`).
2. **A's completion arrives after ownership moved**: the completion reader bumps the playback generation *inside* the
   final grain. The completion then publishes **nothing** — no ready candidate, the served view untouched, **0 events
   emitted**, `one_pass_late == 0` — and the episode is **Idle** afterwards: the frame's ownership-moved branch ends it.
3. **The stale identity is retained** (`viewBuildOperationGeneration` still holds A's generation), which is *why* a
   publication from it can never land: the refusal is the guard, not a silent clean-up.
4. **Cancelling afterwards is a no-op** — the episode the completion already ended is not cancelled twice. This is a
   deviation from the packet's planned action list ("cancel A's episode"), measured rather than assumed: the fixture
   asserts the idempotence instead of a second release (F-5).
5. Slot 2's **coverage source retires** normally, and the discharge from A's admission is still the only record: nothing
   was retained back.
6. **B** (the same interval, requested later) is **Requested**, is offered **the very view slot A was building in**, and
   its frame carries the moved epoch. The slot still carried A's identity immediately before B built into it — the
   premise that makes "reused" a statement about *that* slot.
7. B's build runs in flight under the **current** operation's identity, re-populating the reused slot with **the interval
   the frame itself projected when that build ran** — which is *forward* of the request's interval and different from the
   interval A left behind (F-8).
8. **B is served** from the reused slot's window: 1536 serving ticks, and the emitted trace is exactly the interval's own
   content — 22 events = 11 note-ons + 11 closures of note 48, **0 foreign notes**, `one_pass_late == 0`.

## S2 — cancellation at every preparation grain recovers

Fixture: `test_rolling_path_cancellation_between_every_preparation_grain_recovers`. Seven cycles (`k = 0..5`): each opens
its own demand, drives exactly `k` preparation turns, then cancels the episode. The sweep has to have actually swept, and
it asserts that it did: at least one cancellation happens **before** the build began and at least one **inside** it.

After **every** `k`, the same recovered state: frame **Idle**, no pending demand and no frozen demand surviving, no
builder in flight, no published candidate, the served view untouched, **0 events** emitted, `one_pass_late == 0`, and the
cancelled preparation's coverage source retiring — the slot is reusable, not merely free.

The last act is the one a cancelled slot is really judged by: after every one of those grains, **one clean episode still
runs and is served on the playing chain** (`servingTicks > 0`), with the interval's own note-ons and closures and no
foreign note.

## Mutations (the evidence for a test-only slice)

Production restored **byte-identically** after every one: `src/Track/TrackPlaybackWindowBuild.cpp` back to
`75c23a3f1849d15a…`, with `git diff --stat -- src include` empty at the end.

| ID | Change | Result |
|---|---|---|
| **MR-A** | the admitted build begin does not re-stamp `viewBuildOperationGeneration` | **no test failed — a non-red-proof**, recorded as F-6: the identity is stamped on more than one path (the admitted begin *and* the freeze path), so this fixture's identity assertion is not a claim about that single line |
| **MR-B** | the rolling completion branch does not recognise the moved generation as late (A's completion publishes as a ready successor) | **S1 fails** at `and no prepared successor was created from it`; the pre-existing `test_rolling_generation_change_at_completion_publishes_nothing` fails too |
| **MR-C1** | cancellation does not clear the retained demand | **S2 fails** at `whoever held the demand … released it`; S1 unaffected (discrimination) |
| **MR-C2** | cancellation does not discard a coverage build it owns | **S2 fails** at `and no builder still in flight`; S1 fails at B's request (the surviving builder occupies the slot); `test_rolling_cancellation_retires_only_its_owned_request` fails |

MR-A is reported as it happened. A mutation that does not red-prove does not become "passing evidence" by being omitted,
and it does not invalidate the fixture either: the ordering rule S1 tests is red-proved by MR-B, and the reuse claim by
MR-C2's effect on B's request.

## Verification

- Focused: `pio test -e native -f test_runtime_working_set_ownership` — **142 cases, 142 passed, 0 failed**.
- Full native: `pio test -e native` — **2796 cases, 2789 succeeded, 7 skipped, 0 failed** (baseline before this slice:
  2794 cases; +2 is S1 and S2).
- Capture build: `pio run -e teensy41-capture-serial` — **SUCCESS**, RAM1 unchanged
  (`variables 96,192` / `code 391,576` / `padding 1,640`, approved floor 840, not re-based).
- `openspec validate windowed-playback-generation --strict` — **valid** (OpenSpec CLI on `node v24.21.0`; this machine
  has no installed `v20` under `nvm`, and the CLI runs on the default).
- `git diff --check` — clean. `git diff --stat -- src include` — **empty**.
- **Device: NOT RUN.**

## Not claimed

- The **hardware half** of task 6.5: grain duration and maximum service gaps; sustained multi-track playback with capture
  and persistence contention; external-clock behaviour; memory high-water marks with boundary content parity. It needs
  task **6.4**'s registered device profile and an **authorized** upload. The design's own note stands: reproducing the
  A→B→A race on hardware is not required once native establishes the ordering.
- The interval A/B revision trace **on the rolling chain** (force 2, still partial; the covering fixtures are named above).
- Activation in production, and any checkoff of task 6.5 while its hardware half is unrun.

## Next

1. Task **6.4** — the registered device profile (host-only; no upload).
2. Task **6.5**'s hardware half under a dedicated HITL preset, after authorization.
