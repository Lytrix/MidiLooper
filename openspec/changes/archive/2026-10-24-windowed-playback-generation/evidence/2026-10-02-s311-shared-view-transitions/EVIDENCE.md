# Slice 3.11 — shared-view ownership and the transition table (DEC-108 §13d)

**Change:** `windowed-playback-generation` · **Task:** 3.11 · **Date:** 2026-10-02
**Branch:** `feature/windowed-playback-generation` · **Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.11
**Contract:** DEC-108 §13d; spec *Published views are read-only and ownership survives transitions*; this is **3.7's
first declared dependency**.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `PlaybackViewTransition` (the spec's seven transitions), `PlaybackViewTransitionEffect` (one field per clause the spec names) and `resolvePlaybackViewTransition` — the **table as one pure resolution**. |
| `include/TrackPlaybackRuntime.h` | `viewBuildOperationGeneration`: the operation identity the in-flight build was started under. |
| `include/Track.h`, the owner TU | `applyPlaybackViewTransition` (applies the row), `playbackViewRoleStateForSlot` (the role state a fixture — and a later slice — asserts with), and the **publication guard**: a completion whose operation generation is no longer current is refused, releasing only its own candidate. |
| the reference fixture | `test_shared_view_transitions_state_the_ownership_rules_and_a_late_completion_publishes_nothing`. |

**Why the operation generation is recorded where the build begins**, rather than read from the source identity: the
identity a caller stamps into `PlaybackViewSourceIdentity` is free-form (the earlier slices' fixtures pass arbitrary
generations), so "is this completion late?" must be answered from the operation's own record, not from a field a
caller controls.

## The oracle, case by case

| # | Case | Observed |
|---|---|---|
| W-29 | the table, row by row | every field per transition as tabulated in the packet, plus the invariant asserted for **all seven** rows: no lifecycle transition makes the served window unreadable |
| W-30 | selected-track change, new edit, slot switch, stop-with-successor on a **live** worker | the build stays **owed**, its operation generation and the loop generation are unchanged, and the build then completes and publishes normally — "an existing worker is never retargeted" |
| W-31 | transport stop, then clear/load | transport stop retires the prepared lookahead (the retired successor **cannot be adopted** afterwards) with the served window byte-for-byte unchanged; clear/load additionally **invalidates the generation** |
| W-32 | a completion after the identity moved | it **publishes nothing** (no window is readable afterwards) and nothing is owed — it released its own candidate only |
| W-33 | storage reuse | after clear/load the released roles take a **new** operation under the new identity, while the served window is still readable and not recycled into the candidate |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **55/55** (was 54; +1 fixture) |
| `pio test -e native` (full) | **2686 cases: 7 skipped, 2679 succeeded, 0 failed** (baseline 2685 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** — `variables 96,192` (**unchanged**, no re-base) / `code 391,928 + padding 1,288` / locals 34,880, floor 840, **448 B headroom** |
| the S3.0 pin | **unchanged** (target 6,512): the 4-byte operation-generation field fitted existing padding on both ABIs, so this slice re-based nothing |

## Red-proofs

`shasum -a 256 -c` against the pre-mutation hashes, both files `OK` after every restore:

- **M10 — the publication guard dropped**: *"a late completion publishes nothing"* fails.
- **M11 — the identity not invalidated** on clear/load: *"clear/load invalidates the old operation's identity"* fails
  (`Expected 1 Was 0`).
- **M12 — the table's continuing rows made to cancel the candidate** (a wrong row): the W-30 assertion fails
  (`Expected FALSE Was TRUE`) — i.e. the row's "no" is load-bearing.

**And one mutation that did *not* discriminate, recorded rather than quietly dropped.** The first attempt at M12
emptied the *"retire the prepared successor"* branch (the `resetMetadata` on the `ready` role) — and the suite stayed
green. The reason is measurable in the code: `RuntimePlaybackViewBuild::discard()` clears **its own destination's**
`complete` flag, and after `publishCompletedBuildAsReadySuccessor` that destination *is* the view the ready role now
holds. So for this lane's own sequencing the cancel already retires the lookahead, and the explicit retirement branch
covers a different case: a `ready` view the *current* builder is not pointing at (a successor prepared by an earlier
operation). **That case is not constructed by this fixture** — it is named as a gap, not claimed as covered, and the
packet's W-31 mutation cell was corrected to say so. The gap is deferred to the slice that gives the successor lane a
production caller (3.7), where that interleaving becomes reachable.

## Not claimed

- The **worker-side** rules are exercised through the lane's own seams, not through a production caller (the requester
  is 3.7's): "only the owning builder writes its candidate" is asserted by the roles' behaviour, not by a concurrent
  writer test.
- The **prepared-successor retirement** case above: named gap, with the discard interaction it does cover measured.
- No timing/deadline claim (F-2 stays open), no envelope claim beyond the per-slot figure, no device evidence, no
  flash, no stimulus.
