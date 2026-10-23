# 4.5 — the latency latch is gated to the image that can clear it

**Date:** 2026-10-19 · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.5, containment prerequisite
(*DEC-108 §13e–g*; *DEC-145*) · **Type:** firmware (one `#if`), native evidence only.

This packet follows the [G0 investigation](../2026-10-19-s45-containment-g0/INVESTIGATION.md), which established that
the *"production clear site"* the owner approved could not be wired: the recorded rule is boundary-only, and the default
image has no adoption boundary. The owner then chose the bounded alternative — **latch only where a clear site exists**.

## Scope pin

One statement: **`deadlineContractFaultLatched` is set only when a clear site is reachable in the same image.**

- Set: `Track::noteMissedTraversalForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp:2248`), now inside
  `#if PLAYBACK_WINDOW_ROLLING_ENABLED`.
- Clear sites: `:1157` (inside the same flag) and `:2210` (called only from `:4907`, inside the same flag).
- Unchanged in **both** images: the decision (`PreviousOnePass` / `BreachSecondMiss`), `missedTraversals`,
  `breachReported`, `breachReports`, and the `lag_breach` row. The gate removes only the state bit.

## Transition oracle (controllable production fixture)

`test/test_continuous_capture/test_continuous_capture.cpp::test_lag_breach_is_reported_but_not_latched_where_no_boundary_can_clear_it`,
on the suite's `preparePlayingTrack()` (real committed slot, real prewarm, flag off):

| Step | Expected | Basis |
|---|---|---|
| first `advancePlaybackLagTraversal` | `PreviousOnePass`, no latch | the allowance is per interval and per pass |
| second, same interval | `BreachSecondMiss`, `breachReports == 1`, **no latch** | the breach is named; the latch has no clear site here |
| control, flag on (sibling suite) | `BreachSecondMiss`, **latch set** | `test_second_miss_latches_a_fault_and_the_last_resort_is_targeted_not_global` |

The `kUnservedRevision = 0xFFFFFFFF` term makes `servedIsCurrent` unambiguously false, so the fixture cannot accidentally
pass through a published revision.

**RED → GREEN (measured):**
- RED, before the change: `no latch is set where this image has no boundary to clear it` **FAILED**; the
  `breachReports == 1` assertion passed — i.e. the report was already correct and only the bit was wrong.
- GREEN, after: `test_continuous_capture` + `test_runtime_working_set_ownership` **322/322**.

## Gates

| Gate | Result |
|---|---|
| focused (the two suites) | **322 / 322** |
| full native `pio test -e native` | **3331 cases: 3324 succeeded, 7 skipped, 0 failed** |
| capture build `teensy41-capture-serial` | **SUCCESS** |
| RAM1 | `variables 96,192` · `code 392,088 + padding 1,128` · `locals 34,880` — **byte-identical to HEAD**, no re-base |
| object-level delta | the TU object moves **+8 B** (alignment); the linked ITCM section absorbs it |
| `openspec validate --strict`, `git diff --check` | valid, clean |

No upload, no flash, no device leg: containment is not observable on any flashable image (the qualification image is over
its RAM1 budget and the default image keeps the lane off).

## What this does and does not settle

**Settles:** the default image can no longer latch a fault it has no way to clear — the hazard recorded in the G0 packet
as hidden dependency 1. Because neither reader (`containmentForSlot`, `deadlineContractFaultLatchedForSlot`) has a
production caller, nothing observable changes.

**Does not settle (4.5 stays open):**
- **Production containment / the approved last resort** — still blocked on an owner-level *"no serving route at all"*
  predicate (G0 verdict), and now with a stated precondition: any image that wires containment must also reach a clear,
  so the latch gate is lifted only together with that owner.
- **Displayed provenance** (RWO **S4e**) — owed, untouched.
- **3.7b** — still the removal vehicle for the legacy emission branches, and still downstream of 4.5.
