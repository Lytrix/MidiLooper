# Task 3.10 (increment 28) — blocker 3: request deduplication vs fault history, epoch revalidation, and the promise retained through completion

Baseline: `5cbd3bb8` (increment 27 landed). Branch `feature/windowed-playback-generation`. Authority: DEC-108 §13(a)
(*"a later edit never resets an earlier interval's missed-deadline allowance"*, *"the request is retained … the miss is
reported"*), §13(c) (a changed clock invalidates the **promise**, not the content), §13(e) (second miss → named breach +
latch), §13(f) (containment).

## The three gaps, each verified in the code before this packet was written

A review of increment 27 (pasted into the session, **independently verified here** at `5cbd3bb8`) named three
correctness gaps, and all three are present:

1. **A new admission erases the interval's miss history.** `serviceAdmittedDeadlineMisses` calls
   `lagState.resetEpisode(...)` whenever `admittedTick` changes, so a repeated request for the **same interval** mints a
   fresh first-miss allowance — exactly what DEC-108 §13(a) forbids ("a later edit never resets an earlier interval's
   missed-deadline allowance"). Request **deduplication** must not be the same act as **fault history**.
2. **An epoch mismatch is skipped, not revalidated.** The service `continue`s, the worker carries on, and publication has
   **no clock-validity guard** — so a promise qualified in one epoch is neither honoured nor named invalidated in
   another. §13(c) says the promise is invalidated, the request retained, the valid source continuing, and the miss
   **reported**.
3. **A deadline crossed at/around the final grain escapes.** The check uses the turn's clock, read **before** the worker
   grain, and `activeViewBuildWork()` returns null once the build completes — so a promise that completes a tick past its
   deadline is never judged. The promise must be **retained through completion** and judged with a fresh clock at the next
   opportunity.

The review's closing point is also taken as a requirement: **accounting plus a latch does not yet prove late-publication
containment**, so containment must be asserted from the policy state (`containmentForSlot`), not only counted.

## Transition oracle (fixed before edits)

| # | Transition | Required observable | Mutation that must fail it |
|---|---|---|---|
| C1 | A **new promise for the same interval** does not reset the interval's history | after one missed traversal of interval I, a second promise for I that also misses names the **breach** (not another allowance) | reset the episode on every new promise |
| C2 | An **epoch change** revalidates and **names** the invalidation | `promise_epoch_invalidated` once per promise, no traversal miss counted, the obligation retained, the interval's history untouched | skip silently |
| C3 | ...and it **cannot mint an allowance** | a promise in a new epoch that misses names the **breach** when the interval already had a miss | reset the episode on the epoch change |
| C4 | A promise completing **past its deadline** is still judged | the retained promise is judged at the next opportunity with a fresh clock | drop the retained-promise judgement |
| C5 | The content is **never blocked** by any of this | the late/invalidated completion still publishes; nothing is rebuilt inline and nothing is adopted by the policy | (asserted) |
| C6 | The policy feeds **containment** | after the breach, `containmentForSlot` reads `ServeLastCoherentRevision` while coverage remains | never latch on the second miss |
| C7 | The live path is otherwise unchanged | full native suite green; the admission still has no production caller, so production judges nothing | (build-level) |

## What landed

* **Request deduplication is not fault history** (`src/Track/TrackPlaybackWindowBuild.cpp`,
  `serviceAdmittedDeadlineMisses`). A new promise still starts its own traversal count
  (`countedMissPromiseTick`/`countedMissedTraversals`), but the **episode** (`lagState`: the allowance and the breach) is
  reset only when the *interval* changes -- `coversInterval(handoffPhase, admittedIntervalLength)` guards the
  `resetEpisode` call. A repeated request for the same interval therefore cannot re-open its allowance.
* **An epoch mismatch is revalidated and named, never skipped** (§13(c)). The `continue` became: name
  `promise_epoch_invalidated` **once per promise** (per-slot `invalidatedPromiseTick`), claim **no** traversal miss, leave
  the interval's history untouched, and retain the obligation intact -- `CLOCK_EPOCH` mismatch neither re-admits nor
  re-positions the promise, so when the live clock returns to that epoch the **same** promise is judged again. The
  **content** is never invalidated: the valid audible source continues and the window still publishes (no inline rebuild,
  no block).
* **The promise is retained through completion** (§13a/§13(c), review gap 3). `stepPlaybackViewMaintenance` records
  `completedPromisePendingJudgement` where the build completes, and the service consumes it at the **next opportunity**,
  so a deadline crossed by the turn's final grain -- where the turn's clock was read *before* the grain and the record
  becomes unreadable the moment the build completes -- is still judged with a fresh clock. A **new admission clears the
  flag**, so a successor promise can never be judged as its predecessor. Both new fields are documented at their
  declaration in `include/TrackPlaybackRuntime.h`.
* Size pins re-based **on the compiler's own figures**: target `sizeof(LoopPlaybackRuntime)` 6,816 -> **6,832** B, host
  8,768 -> **8,784** B (the 8-byte promise key, the 1-byte retention flag and the 2-byte count, 8-byte aligned). Per-slot
  external storage: **no RAM1 re-base** -- `variables 96,192` unchanged, no DTCM.

## Fixtures (three, ownership suite; `RUN_TEST` at the end of the existing 3.10 block)

1. `test_a_new_promise_for_the_same_interval_does_not_reset_the_intervals_miss_history` — C1 **and** C6: one missed
   traversal of interval I (the allowance), the build completed and **published**, then a successor promise for the
   *same* interval whose miss is the interval's **second** -- named `lag_breach`, latch set, `one_pass_late` still 1 --
   and `containmentForSlot(0, intervalStart + 1)` reads `ServeLastCoherentRevision` while coverage remains. So the policy
   is **read by containment**, not merely counted.
2. `test_an_epoch_change_invalidates_and_names_the_promise_without_minting_an_allowance` — C2/C3/C5: an allowance in
   epoch 1; a **real** epoch change names `promise_epoch_invalidated` **once** and moves no traversal count; the
   obligation survives the mismatch with the same admission tick and deadline; the return leg re-judges *that* promise
   (`countedMissPromiseTick` still its own -- the mismatch suspended judgement, it did not erase the promise); the build
   **completes in the new epoch** and the window publishes; a promise admitted in the new epoch that misses names the
   **breach**, proving the epoch change minted no fresh allowance.
3. `test_a_promise_that_completes_at_its_deadline_is_still_judged` — C4: the build is finished with a clock **inside**
   the promise (nothing late), the record is then unreadable (`activeViewBuildWork() == nullptr`), and the **next**
   opportunity with a fresh clock past the deadline still names `one_pass_late` -- once, not repeatedly.

## Mutations (each red-proved, each restored byte-identically)

| # | Mutation | Red observable |
|---|---|---|
| MC1 | a new promise always resets the episode | fixture 1: `one_pass_late` Expected 1 Was 2, and fixture 2's breach Expected 1 Was 0 |
| MC2 | the epoch mismatch is skipped silently (no row) | fixture 2: `promise_epoch_invalidated` Expected 1 Was 0 |
| MC3 | the mismatch resets the episode | fixture 2: history Expected 1 Was 0 |
| MC4 | the promise is not retained through completion (old `activeViewBuildWork() == nullptr` gate) | fixture 3: Expected 1 Was 0 |
| MC5 | the completion does not record the retention | fixture 3: Expected 1 Was 0 |
| MC6 | the second miss never latches | fixture 1 (`containmentForSlot`/latch) and the two older deadline fixtures fail |

Driver: `$PI_SCRATCH_DIR/mutations28.py` (patch -> `pio test -e native -f test_runtime_working_set_ownership` -> restore,
hash-checked). Owner TU `src/Track/TrackPlaybackWindowBuild.cpp` hash
`377b5041b44a8f9d` before and after every case, final `6f4961ef38d9186c` (doc comments added after the mutation run);
`include/TrackPlaybackRuntime.h` `c9b5654c266b5138`.

## Actual verification

- Focused: `pio test -e native -f test_runtime_working_set_ownership` — **97 cases, 97 succeeded** (was 94/94 at
  increment 27: +3 fixtures). The run before the fixture-expectation correction was 1 failed / 96 succeeded; the cause is
  recorded below, not smoothed.
- Full: `pio test -e native` — **2,740 cases: 2,733 succeeded, 7 skipped, 0 failed** (2,737/2,730/7/0 at increment 27).
- `pio run -e teensy41-capture-serial` — **SUCCESS**; RAM1 `variables 96,192` / `code 390,472` / `padding 2,744`,
  identical to the recorded baseline and unchanged by this increment. **No upload**: this increment was not evaluated
  with a HITL preset, and nothing here is device-observable on its own (the admission still has no production caller).
- `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.

### Corrections (recorded, not smoothed)

1. The first version of fixture 2 asserted `countOf("one_pass_late") == 0` after the epoch-2 turn, which is wrong against
   its own earlier epoch-1 turn: that turn legitimately counted the interval's allowance, so the correct assertion is that
   the count **does not move** across the mismatch. Fixed to compare against the count captured before the change.
2. `completedPromisePendingJudgement` is a bool, not a promise-tick key: a tick of 0 is a legal instant, so a
   `tick != 0` sentinel would silently drop the retention for a promise admitted exactly at tick 0. The flag is instead
   **cleared at admission** (`runtime->viewBuildWork = admittedWork`), which gives the same "this promise, not its
   successor" guarantee with no sentinel and no extra byte.

## Addendum — "publication has no clock-validity guard" (review 2, gap 2), answered with evidence

The revalidation is not meant to *block* publication, and adding a publication-time gate would contradict §13(c): *"a
changed clock invalidates the **promise** to finish in time, not the content -- the request is retained, the valid audible
source continues, the miss is reported, and an inline rebuild is never used to compensate."* What the code owes is that
the **promise** is revalidated before the turn's own publication and that nothing crosses a deadline unobserved:

* `serviceAdmittedDeadlineMisses(clock)` is the **first** statement of `stepPlaybackViewMaintenance`, i.e. it runs with
  the live `windowClock` **before** that turn's grain **and before** `publishCompletedPlaybackWindow` -- so the
  publication in a turn follows the revalidation of the promise that turn can see.
* For the promise completed *by* that grain, the fresh-clock judgement is the retained one, consumed at the **next**
  opportunity (fixture 3, red-proved by MC4/MC5) -- which is before the next publication or handoff for that slot.
* The **content** publishes either way (fixture 2 asserts the window identity after completion in the changed epoch;
  fixture 1 asserts publication before the successor miss), so no state is left un-served and no rebuild is ever used to
  compensate. C5 is therefore asserted, not assumed.

## Remaining gates / non-claims

Task 3.10 -- bounded deadline-ordered service -- remains **OPEN**. `sInterferenceBudgetEnforced` stays **off**: with
enforcement on, admission fails closed on an unqualified clock stamp and refuses twelve existing fixtures, so activation
awaits the calling owners' qualified clock stamps (blocker-1's production requester). Also still owed and unchanged by
this increment: the elapsed-time **O8 re-attempt**, the `H x K` sweep of the declared envelope, device legs for the
envelope/overrun (grain duration and maximum service gaps), and the production requester itself. This increment adds no
device evidence, no upload, no timing acceptance claim and no new owner.

