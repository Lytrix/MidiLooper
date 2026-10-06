# Task 2.7 — the demoted-shape coverage gap is closed: removing the window build on a demoted slot is RED

**Date:** 2026-10-07 · **Task:** 2.7 (fixtures) · **Base:** `8af97b01` · **Type:** red-proof of an existing
fixture set; **no production change** (the mutation was temporary and is restored byte-identically).

## What the task asked

Task 2.7's own wording: *close the measured coverage gap for the demoted shape — verify a fixture **fails** when the
window build is removed on a demoted slot (RWO recorded that dropping it left every fixture green and pinned that as a
harness tripwire)*. The gap is a **harness** property, not a missing production feature: a demoted slot's windowed
build could be dropped with every fixture still green, which would make the whole windowed lane unfixtured on the shape
it exists for.

## The mutation

Inserted immediately after the chosen holder's runtime check in the owner turn
`Track::stepPlaybackViewMaintenance` (`src/Track/TrackPlaybackWindowBuild.cpp`, after
`if (runtime == nullptr) return PlaybackViewTurnResult::Idle;`):

```cpp
// MUTATION 2.7 (temporary, not to be committed): the window build is removed on a demoted slot -- the admitted
// coverage demand never receives a grain, so no window is ever published for it.
if (loopForSlot(slot).shouldAvoidFullVisualRebuild(loopForSlot(slot).loopLengthTicks)) {
  return PlaybackViewTurnResult::Idle;
}
```

This is exactly the failure the tripwire describes: for a demoted slot the admitted coverage demand still exists, is
still selectable, and **never receives a grain**, so no window is ever built or published — and nothing is *refused by
name*, so only a fixture that observes the demoted slot's own outcome can catch it.

## Result — RED, and the demoted shape is what fails

`pio test -e native -f test_runtime_working_set_ownership`: the runner printed **`177 test cases: 89 failed, 87
succeeded`** and reported the suite `ERRORED` (`Program errored with 89 code`); 89 of the 176 registered cases failed.

The failures that own the demoted shape (verbatim assertion messages):

| Fixture | Assertion that failed |
|---|---|
| `test_window_owner_resolves_the_interval_through_the_published_snapshot` (`:4320`) | `Expected FALSE Was TRUE` — the published window for the demoted two-bar interval |
| `test_retained_source_window_serves_a_rootless_interval_and_the_index_bounds_the_lookup` (`:5073`) | *the production turn publishes the rootless window* |
| `test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary` (`:7680`) | `Expected FALSE Was TRUE` (F-7's served-window stream) |
| `test_demoted_slot_residency_is_length_independent_swept_over_lengths` (`:7484`) | `Expected FALSE Was TRUE` (F-8's residency sweep) |
| `test_window_readiness_names_its_holder_and_an_empty_interval_is_ready` (`:4529`) | *an empty interval is still a window, and it is published* |
| `test_window_owner_publishes_the_interval_window_atomically` (`:4447`) | `Expected 2 Was 0. the last grain completes the build` |

The remaining failures are the lane's own suites (3.7a I2/I3/I3b/I3c/I4/I5/I7/I8, the retry and deadline-promise
families, 2.11, 3.12/5.5, 3.14's between-grains matrix, the activation audit), i.e. the mutation is not caught by one
incidental assertion — it is caught wherever a window is owed. The full failed-case list is the 89 names in the run.

## Restore and post-restore state

- Restored with `git checkout -- src/Track/TrackPlaybackWindowBuild.cpp` at base `8af97b01`.
- `sha256` before the mutation and after the restore are **identical**:
  `269331f615b66c6a4d4376007007bed77985e5e9eb1bddbf79aaa790504b6495`.
- `git status --porcelain` empty after the restore.
- Post-restore focused suite: **`176 test cases: 176 succeeded`** (`test_runtime_working_set_ownership` PASSED).

## What this does and does not establish

- **Establishes:** the tripwire is discharged — the windowed lane's demoted-slot production is fixture-backed, and no
  mutation that removes the demoted slot's window build can pass the suite.
- **Recorded, not smoothed:** the runner's own summary line reads `177 test cases` while 89 + 87 = 176 registered
  cases, because the errored suite is counted once more in that total. The per-case results are what the 89 figure is
  taken from.
- **Not claimed:** no production line changed; no device or timing result; 2.8 (per-fixture red-proof *inside the slice
  that implements it*) stays open, as does RWO's own S4b.7 tripwire about the `ensurePlaybackMergedEventsForSlot` call,
  which belongs to that change's tasks.
