# Task 3.7d — landed: the blocked-demand row names the term that stopped it

**Status: LANDED 2026-10-20** (firmware + native fixtures + host decode; the device payoff is a rerun, not a new
firmware line). Supersedes the "prepared and reverted" state in [`PREPARED.md`](PREPARED.md); the blocker recorded there
is answered at the bottom.

## What landed

`win_blocked_by_request` now carries the **`PlaybackViewPendingReason` ordinal** in `rq`:

```cpp
const PlaybackViewPendingReason blockedBy =
    (request->reason == PlaybackViewPendingReason::None ||
     request->reason == PlaybackViewPendingReason::AwaitingSourceQualification)
        ? pendingViewRequestInvalidation(activeLoopIndex, clock)   // the owner's existing recomputation
        : request->reason;                                        // the term the record holds
```

Two sources, one rule: the record's own terminal term when it has one; otherwise the owner's **existing**
`pendingViewRequestInvalidation` (a bare `positionInvalidated` flag carries no term of its own). No new field, no schema
change, no new rule about invalidation.

**The token split that makes `rq` unambiguous.** DEC-147's traversal terminal had been using the same
`win_blocked_by_request` token with `rq` = the spent span ordinal, which would have given one field two meanings. That
terminal now emits its own token, **`win_blocked_by_budget`**, and keeps its ordinal. One token, one meaning of `rq`:

| exit | `rq` |
|---|---|
| `win_blocked_by_request` | the `PlaybackViewPendingReason` ordinal that stopped the retained demand |
| `win_blocked_by_budget` | the spent span ordinal of a whole measured traversal |

`win_blocked_by_budget` is registered in the lane verifier's `DEMAND_EXITS` (it is a demand answer like the rest), so
old and new captures are read the same way.

## The fixture blocker, answered

`PREPARED.md` recorded three failed attempts, all ending `Expected 8 (Rejected) Was 4 (Waiting)`, and named the next
diagnostic: the record the resolver reads was not active-and-latched. Two facts about the code answered it:

1. `runtime->pendingViewRequest()` returns **nullptr unless `active`**, so an attempt that invalidated a *discharged*
   record made the exit unreadable. The fixture must invalidate the record the resolver reads **while it is active** --
   the same shape the stale-demand control beside the DEC-147 fixtures already used.
2. The reply only reaches the exit for four conditions (`positionInvalidated`, `CannotCompleteBeforeHandoff`,
   `WindowExceedsRetainedCapacity`, `PinnedEditRowsUnresolved`). Driving the **deadline** past expiry (attempt 3) records
   `DeadlineExpired`, which is **not** one of them -- so that attempt could never reach the row, by design rather than by
   a state bug.

## Verification

* **Native, recorded term:** `test_3_7d_the_blocked_demand_row_names_the_term_that_invalidated_it` -- the demand is
  opened through the lane, the record is invalidated while active with a terminal reason the recomputation cannot
  produce, and the exit's `rq` is that reason's ordinal.
* **Native, recomputation fallback:** `test_3_7d_the_blocked_demand_row_falls_back_to_the_owners_recomputation` -- the
  record names no term; its clock epoch no longer matches the live clock, so the owner's own
  `pendingViewRequestInvalidation` answers epoch. (A geometry mismatch cannot reach the exit: the lane's identity refresh
  repairs `loopLengthTicks` at the top of every turn -- recorded in the fixture.)
* **Red-proof:** replacing the computed value with a constant `0` fails **exactly** those two fixtures (`Expected 9 Was 0`,
  `Expected 5 Was 0`) and nothing else; the file was restored byte-identically (sha256
  `a9d5c1b57a45d80d857b5878f647409722d15b74a1be77fabab23a040fd1ad3a` before and after).
* **Focused:** `test_runtime_working_set_ownership` **220/220**.
* **Full native:** **3337 cases -- 7 skipped, 3330 succeeded, 0 failed** (was 3335/3328; the two new fixtures account for
  the difference).
* **Host:** `scripts.test_windowed_content_parity_verify` **57/57**, with two new tests -- the term rendering (three
  ordinals, including an unknown one named `term_99`) and `win_blocked_by_budget` as its own demand answer.
* **Builds:** `teensy41-capture-serial` SUCCESS at `code 392,216 + padding 1,000` (unchanged); lane image
  `teensy41-capture-serial-window-qualification` SUCCESS at `code 392,376 + padding 840` (**the approved floor,
  unchanged** -- the change cost **0 B** of ITCM).

## Owed

The **device payoff**: one run of the registered windowed leg, which should now print *which* term blocked a demand
instead of leaving the record's five candidates. No firmware line is needed for it.

## Commit

The commit that carries this file on `feature/unified-rolling-playback-path` (firmware, native fixtures, host decode,
docs). Not pushed. (No hash is pinned here on purpose: the evidence is amended into its own commit, so a hash would
name the commit before the amendment.)
