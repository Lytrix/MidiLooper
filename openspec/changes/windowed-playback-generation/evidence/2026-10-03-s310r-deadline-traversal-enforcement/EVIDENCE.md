# Slice 3.10 (increment 21) — the deadline rollover fixed, and the enforced admission path tested

**Status:** 2026-10-03. Answering the three review points on increment 20.

## 1. A retained phase is **not** a retained deadline — fixed (the review's sharpest point)

`handoffLeadTicks` measures **forward** distance, so the moment the playhead goes past the deadline it wraps and reports
a nearly full pass of available lead: the predicate would have **silently re-granted a passed deadline another whole
pass**. The repair retains the **intended traversal** — `admittedPhase` and `admittedLeadTicks` — and closes it
arithmetically: the forward distance from the admission phase is inside the admitted lead exactly while the traversal is
**open**, and wraps past it once the deadline is behind. A closed traversal is refused *before* the contract arithmetic
can flatter it, so the requirement can only ever refuse. The boundary is **exact**: one tick past the deadline closes it
(fixture pin (D)).

## 2. A refusal retains no obligation — claim corrected, not papered over

The admission comment no longer implies that "the caller re-issues". `TrackInterferenceBudgetExceeded` is a **named
refusal**; it becomes a promise only when a caller actually keeps the pending request and its original deadline. No such
caller or pending-request store exists, so that half is **not implemented and not claimed** — the review's point 2 is
answered by withdrawing the over-claim, not by asserting the mechanism.

## 3. The enforced path is tested **now**, with a controlled timing contract

The enforcement switch is no longer a compile-time gate: it is a **runtime** flag
(`sInterferenceBudgetEnforced`, `DMAMEM` on the target and plain on the host, like the service-gap sampler's state, so a
scalar never moves the approved RAM1 line). `test_window_admission_refuses_by_name_once_the_interference_budget_is_enforced`
turns it on, drives the **real** admission, and gets the **named refusal** (`Expected 11` = `TrackInterferenceBudgetExceeded`
in the mutation proof); with the switch off the *same request* is admitted, which is what makes the refusal attributable
to the budget and not to another guard. Production activation stays gated until the timing bound is supported — the
switch exists so the enforced path is exercised, not so it can be quietly believed.

**Charging full admitted grains** is kept as the conservative substitute for remaining work; the sums saturate rather
than wrap (`playbackViewOutstandingGrains` saturates at `UINT16_MAX`), and the candidate is neither omitted nor
double-counted: for each existing window the charge is *its own* grains (from its retained record) plus **all** other
outstanding work, the candidate included. The fixture asserts exactly that arithmetic, including the exclusion.

## Fixtures

- step 2's fixture gains **(D)**: a passed deadline is not re-granted another pass — the same charge that fits inside the
  traversal is refused one tick past the deadline.
- `test_window_admission_refuses_by_name_once_the_interference_budget_is_enforced` — the enforced path with a controlled
  contract, and the switch's own attribution.

## Red-proofs

Guarded replace (unique anchor asserted), restored byte-identical (`TrackPlaybackWindowBuild.cpp` `ddd83a0b…` after
every run):

| mutation | change | the assertion that spoke |
|---|---|---|
| **M116** | traversal closure removed | *"once the playhead is past the deadline the traversal is closed, so no extra pass is granted"* |
| **M118** | traversal lead not retained | *"on the loop both windows were admitted on, the same charge still fits"* |
| **M119** | budget check removed from the admission | *"Expected 11 Was 0. with the budget enforced the real admission refuses by name, charging the sibling"* (+2 other fixtures) |

## Verification

Focused **82/82** (two fixtures grew by one case and one new case was added). Full native **2720 cases / 2713 succeeded
/ 7 skipped / 0 failed**. `pio run -e teensy41-capture-serial` **SUCCESS**; RAM1 line **byte-identical** (`variables
96,192 / code 390,088 + padding 3,128`) ⇒ **no RAM1 re-base** — the flag is `DMAMEM`, which is why it costs no RAM1
`variables`. One measured size re-base recorded with the figure below.

## Still owed

Enforcement activation (awaiting `Delta-max`), a caller that retains a pending obligation, step 3's instrumentation and
device leg, the `H x K` envelope, the systematic sweep.
