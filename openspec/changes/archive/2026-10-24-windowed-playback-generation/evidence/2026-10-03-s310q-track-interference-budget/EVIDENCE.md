# Slice 3.10 (increment 20) — review step 2: the per-track outstanding-work budget

**Status:** 2026-10-03. Second half of the reviewer's recommended slice. The bound is **implemented, proved and
retained**, and its **enforcement is deliberately gated off** — with `Delta-max` still unchosen, enforcing it today
would reduce capacity, which the reviewer's ordering forbids at this step. The gate is not a claim: it is measured, and
the measurement is a red-proof (M115 below).

## What landed

- **A per-track outstanding-work budget** replaces the vague `K` allowance:
  `Track::playbackViewOutstandingGrains(excludingSlot)` sums every active build's **full** admitted grains on the
  track. Full, not remaining, is deliberate — it is an **upper bound** on what is still owed (a served grain is charged
  again), which is the safe direction for a budget. A tighter figure needs the build's remaining-grain count, which is
  not observable today (owed).
- **The promise-keeping predicate**: `Track::playbackViewAdmissionKeepsAdmittedDeadlines(contract, currentPhase,
  loopLengthTicks, candidateGrains)` requires that **every already-admitted window on the track** can still reach its
  own deadline once the candidate *and* all sibling work is charged against it. The rule is the reviewer's deliberate
  EDF-free one — *charge all sibling work* — and each window is evaluated through a per-slot copy of the contract so
  the arithmetic is the contract's own `canCompleteBeforeHandoff` and the budget cannot disagree with the admission
  about what a grain costs in turns.
- **The deadline is retained**, not re-derived: `PlaybackViewAdmittedWork::handoffPhase`, written where the build
  begins, so a *later* request can be checked against it rather than only against its own margin.
- **A named refusal**, `PlaybackViewRequestResult::TrackInterferenceBudgetExceeded`: the request is refused by name so
  it can be re-issued when the interference clears — never silently dropped, and the existing window's deadline is never
  traded away.

## Why enforcement is gated, measured rather than argued

Charging a sibling against a new window's lead only becomes usable once the per-turn advance inside that charge is the
**measured** one. With the declared one bar per turn, a single sibling's charge already exceeds a 40-bar loop by more
than an order of magnitude (fixture pin (C)). **M115 therefore flips the gate on and three existing fixtures fail**
with `TrackInterferenceBudgetExceeded` (`Expected 0 Was 11`) at their "the fixture's loop must be long enough for the
declared lead" assertion — direct evidence that the gate is load-bearing and that enforcing it now is the capacity
reduction the reviewer said the envelope work must not start with. Step 4's formula
(`Delta-max x H x bounded per-track work`) is what turns it on.

## Fixture

`test_window_track_budget_protects_an_older_windows_admitted_deadline_from_a_sibling`: two windows on one track, both
requested **just past** their interval's start so their deadline is nearly a full pass away (the available lead is the
distance to the handoff, and a near deadline binds long before any charge does — my first two attempts failed on
exactly that, recorded here). It asserts the budget's exclusion/whole-track sums, that both deadlines are retained, that
the same candidate charge **fits the loop the windows were admitted on and does not fit a 40-bar loop** (so the bound
refuses capacity, not siblings), and the measured inequality that explains the gate.
**Bound of this fixture, stated:** it exercises the bound through its public predicate with explicit phases; it is not
a claim that the admission currently refuses those admissions.

## Red-proofs

Guarded replace (unique anchor asserted), restored from a saved copy and verified byte-identical
(`TrackPlaybackWindowBuild.cpp` `826bc01a45d1e794` after every run):

| mutation | change | the assertion that spoke |
|---|---|---|
| **M112** | budget charges no siblings | *"Expected 40 Was 0. the budget excluding slot 0 is slot 1's outstanding work"* |
| **M113** | deadline not retained | *"Expected 0 to be greater than 0. the admitted window retains its deadline"* |
| **M114** | budget does not exclude the named slot | *"Expected 40 Was 45. …"* (the sum re-counts the excluded slot) |
| **M115** | gate forced **on** | three existing fixtures fail with `TrackInterferenceBudgetExceeded` — the gate is load-bearing |

## Verification

- Focused `test_runtime_working_set_ownership` **80/80** (+1 fixture).
- Full native **2719 cases / 2712 succeeded / 7 skipped / 0 failed**.
- `pio run -e teensy41-capture-serial` **SUCCESS**; RAM1 line **byte-identical** (`variables 96,192 / code 390,088 +
  padding 3,128`; 2 288 B headroom) ⇒ **no RAM1 re-base**.
- **Size re-base, measured:** `sizeof(LoopPlaybackRuntime)` **6 640 → 6 648** on the target (the retained deadline).
  The **host** pin is **still 6 544**: the host layout has absorbed all 20 bytes of the two review records, and its
  untouched `static_assert` is the evidence. **Noted honestly:** the prose above that pin still reads as if one field
  were added, so its explanation is now stale even though its claim (host unchanged) holds.

## Still not done

Enforcement (awaiting `Delta-max`), the reviewer's *coalescing* rule (there is no pending-request store: the budget
refuses by name and the caller may re-issue, which keeps the obligation without erasing a deadline, but that is not the
same mechanism), step 3's instrumentation and device leg, the `H x K` envelope, and the systematic sweep.
