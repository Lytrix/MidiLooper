# Slice 3.10 (increment 19) — review step 3, first pass: the lane dispatch boundary investigated (read-only, no code changed)

**Status:** 2026-10-03. The reviewer's step 3 asks for the lane dispatch boundary to be instrumented *independently of
whether it serves a build*, the starvation episodes reproduced, and **what prevents dispatch** identified and bounded
or repaired **before** choosing `Delta-max`. This increment is the investigation only: read-only, no production line
touched, no fixture, no device run. The full report is
[`LANE-DISPATCH-INVESTIGATION.md`](LANE-DISPATCH-INVESTIGATION.md).

## The findings that change how step 3 must be done

1. **One dispatch site, and it is silent when it serves nothing.** The lane is dispatched from exactly one place
   (`src/main.cpp`, the `PlaybackViewMaintenance` block, reachable in every track state because it returns before the
   all-stopped guard), but the body runs **only when the holder selection is `Service`**. A dispatch that owes nothing
   therefore produces **no call and no sample** — so the existing `DIAG,win_gap` series counts *served* turns, and a
   quiet interval is invisible and **inflates the next sample's delta** ✓. This is a limitation of my own instrument
   (increments 11–15), now named: the counter must be incremented at the dispatch boundary, before the selection.
2. **The build slot is shared with the record lanes.** Eligibility is `viewBuild.active()`, and the same single
   `viewBuild` is also begun and stepped by `serviceFirstRecordingBar` and `serviceFrozenRecordWindow`. The offer does
   **not** check the fill scope, so **a record-lane build makes the window lane eligible, and the window lane steps its
   grains**. That is the plausible source of the hundreds-to-thousands of measured "lane opportunities" in runs 1–6
   **despite there being no production window requester** ✓ — and it may itself be a defect. Intent: **UNKNOWN**;
   recorded as a question for step 2/3 rather than assumed either way.
3. **Starvation is possible by construction.** One owner per iteration, and the rotation cursor advances **one per
   iteration regardless of how long the iteration took**: there is no deadline, no catch-up and no bound on the
   iteration duration. With six other iterations between two lane turns, a ~0.5 s iteration accumulates to **seconds** —
   exactly the shape increment 16 measured (the largest single blocking span is ~20× smaller than the gap). No other
   owner can steal the lane's turn, and the per-track contention is `H` lane turns per grain (`H ≤ 8`) under the
   7-owner rotation — the undeclared `H × K`.
4. **What is not observable today**, and therefore what step 3's instrumentation must add: dispatches that serve
   nothing; the selection outcome and the chosen track; the **loop-iteration count** between dispatches; which owners
   took the intervening iterations; the microsecond spacing between dispatches; transport-state *history* (a
   non-running sample silently erases a delta, because the last phase is updated unconditionally); resets seen only
   between running samples; the in-flight build's fill scope; and the turn result with the grains it served.
5. **Ranked candidates for the multi-second episodes** (each with a falsifier in the report): cumulative slow
   iterations; **blocking USB serial writes in the capture flush** (the only mechanism the report finds unbounded by
   construction); SD grains exceeding their budgets; external-clock catch-up (already falsified for run 5 by its
   `clk_batch 0,0`); the **instrument artifact** of finding 1; and heap/pressure walks.

## What this does *not* do, and what step 3 still owes

No instrumentation was added, no reproduction run was made, and **no `Delta-max` is proposed** — the largest observed
gap remains evidence, not a guaranteed maximum, and the reviewer's ordering is explicit that the preventing paths are
bounded or repaired **first**. The next slice is therefore: instrument the dispatch boundary (counter at the dispatch
point, selection outcome, iteration count, intervening owners, microsecond spacing), re-run the device leg, and only
then bound or repair what the series shows.
