# Task 3.10 (increment 26) — blocker 3: the permitted service-gap limit and its overrun policy, **wired on the live path**

Baseline: `c0057493` (increments 25/25b/25c landed). Branch `feature/windowed-playback-generation`.
Work order: the timing-envelope qualification's **blocker 3** — *"No permitted service-gap limit and operational overrun
policy is wired into the live admission/publication path. Empirical maxima cannot be promoted to guarantees;
containment after a miss is not passing normal-operation continuity."* Authority: DEC-108 §13(a)/(b)/(e).

## Why this is still open after step 4

Step 4 **declared** the terms (the permitted per-turn advance = 64 ticks, measured; H; the derived cost) and **named**
the overrun policy. Two things were still not true on the live path, and both were visible in the device leg:

1. **The declared limit is never compared to anything live.** `PlaybackLaneMeasurement` records `maxDeltaTicks` and
   `eligiblePredecessorMax` but flags nothing: its `saturatedGaps` counter is the *instrument's* own uint32 overflow,
   not "this gap exceeded the permitted bound". The device leg's blocked 70-tick gap (inside a 4 561-node snapshot
   publish) was recorded as a maximum and **reported to nobody**.
2. **The admitted window's own promise is never checked.** `PlaybackViewAdmittedWork::deadlineTick` /
   `clockEpoch` are *written* by the admission and *read* by the budget and the request bookkeeping — **no live
   serving/publication path reads them**. The overrun policy that exists (`one_pass_late` / `lag_breach`,
   `deadlineContractFaultLatched`, `resolvePlaybackLag`, `PlaybackLagState`) is driven by **revision currency** only:
   a window that becomes ready *after the traversal it was admitted for* is never named as late.

## Scope

**Half 1 — the permitted limit, wired and named.**
`PlaybackLaneMeasurement::observe(...)` takes the permitted advance and counts **overruns**: gaps across a turn whose
predecessor was **eligible** and that exceed the permitted bound (exactly the quantity the declaration is about, and
exactly what the sampler's eligibility split exists for). The reporting block row gains `permittedAdvanceTicks`,
`overruns`, `overrunMaxTicks` (**schema 3**), and a **named** row is emitted once per block that saw an overrun:
`#CAP,<us>,DIAG,win_gap_overrun,<permitted>,<overruns>,<overrunMaxTicks>,<running>,<epoch>`. The bound the lane compares
against is the declared one (`Track::kPlaybackViewPermittedAdvancePerTurn`), read where the sampler already lives — not
a second constant.

**Half 2 — the admitted promise, checked where it is kept.**
The lane turn takes the deadline clock (`main.cpp` already has it for sampling; the parameter is defaulted so every
existing caller — and every clockless path — is unchanged). When a build reaches **completion**, if its admitted record
holds a **qualified** deadline and the clock is past it, the promise was **missed**: the miss feeds the *existing*
policy through one factored helper, so "what a second miss is" keeps one definition —
first missed traversal of the episode names `one_pass_late`, a **second consecutive** one names `lag_breach` and latches
`deadlineContractFaultLatched` (DEC-108 §13e) — and the deadline is **never extended** (the increment-21 rule, now on
the service side as well). An unqualified stamp (epoch 0) or a clockless caller claims **nothing**: an unpositioned
promise cannot be late.

## Transition oracle (fixed before edits)

| # | Transition | Required observable | Mutation that must fail it |
|---|---|---|---|
| B1 | An **eligible-predecessor** gap above the permitted bound is an overrun | counted in `overruns`, its size in `overrunMaxTicks`, and the **named** row names the permitted value it exceeded | remove the comparison |
| B2 | A gap at or below the bound is not an overrun | zero overruns, no named row | count at bound |
| B3 | An **idle** (ineligible-predecessor) gap above the bound is **not** an overrun | the same "contains the >bound gap" case, with the predecessor ineligible, counts **0** | drop the eligibility condition |
| B4 | An epoch change is not an overrun | a gap spanning an epoch change counts 0 | count it |
| B5 | The block row carries the limit it compared against | schema 3 fields present with the declared permitted value | drop the permitted field |
| B6 | A window completing **past its own admitted deadline** is named late | `one_pass_late` for the first missed traversal of the episode | remove the deadline check |
| B7 | A **second consecutive** miss names the breach and latches the fault | `lag_breach` + `deadlineContractFaultLatched` | never latch |
| B8 | The deadline is **never silently re-granted** | the admitted record's `deadlineTick` is unchanged after a miss | extend the deadline instead of naming the miss |
| B9 | An **unqualified** stamp claims nothing | no miss, no row, with `clock.epoch == 0` or no clock passed | check it anyway |
| B10 | The live path is otherwise unchanged | full native suite green; no production caller requests a window, so the admission is still never called; RAM1 line held | (build-level) |

## Verification recipe

- Focused `pio test -e native -f test_runtime_working_set_ownership` (+`test_playback_prewarm`), then full
  `pio test -e native`; `pio run -e teensy41-capture-serial` with the approved RAM1 line held; `git diff --check`;
  `openspec validate --strict`.
- Each mutation restored byte-identically with its hash recorded.
- **Device: NOT RUN** in this increment (no live behaviour changes — the admission has no production caller and the lane's
  production builds carry no admitted deadline), and no timing-envelope acceptance claim.

---

## Completion record (2026-10-04) — **half 1 landed; half 2 is NOT done, and blocker 3 is therefore only half closed**

| Field | Value |
|---|---|
| Scope pinned | DONE (above) — both halves named |
| **Half 1 — the permitted gap, wired and named** | **DONE**: `PlaybackLaneMeasurement` compares every eligible-predecessor gap against the permitted advance, counts the overruns, sizes the worst, carries the permitted value in the block row (**schema 3**) and emits the **named** `DIAG,win_gap_overrun` row once per block that saw one |
| **Half 2 — the admitted deadline read by the live path** | **NOT DONE** (design point recorded below) |
| Oracle B1-B5 | proved (fixture + four mutations) |
| Focused suite | **91/91** (+1 fixture) |
| Full native suite | `pio test -e native`: **2,734 cases / 2,727 succeeded / 7 skipped / 0 failed** |
| Capture build / RAM1 line | **SUCCESS**; `variables 96,192` unchanged, code 390,472 + padding 2,744 — **no RAM1 re-base, no size-pin movement** (the new state is four `uint32_t`s in the lane sampler's `DMAMEM` static and one flash-resident row emitter) |
| Mutations red-proved and restored | **M130-M133**, all restored byte-identically |
| Device / HITL | **NOT RUN** (no live behaviour changed: no production caller requests a window, and a clockless path compares against nothing) |

### What landed
`PlaybackLaneMeasurement` gained `permittedAdvanceTicks`, `overruns` and `overrunMaxTicks`; `observe()` takes the
permitted advance (default `0` = *no comparison*, so the clock-plumbing fixtures that observe the epoch rule on its own
are untouched) and counts an overrun **only** for a gap whose predecessor was **eligible** — the eligibility split the
sampler already records is exactly what separates a scheduling delay from idle time. The block row is now
**schema 3** (`permittedAdvanceTicks, overruns, overrunMaxTicks` appended), and a **named** row
`#CAP,<us>,DIAG,win_gap_overrun,<permitted>,<overruns>,<overrunMaxTicks>,<running>,<epoch>` is emitted once per block
that saw one — through a `#ifndef`-guarded seam (`SC_WINDOW_GAP_OVERRUN`), the pattern every other lane row uses.
The lane reads the limit **from the declaration** (`kPlaybackViewPermittedAdvancePerTurn`), so the bound and the
observation cannot drift apart, and the release of the bound itself is still governed by the device legs.

**Why this closes the gap the device legs exposed:** the 70-tick blocked gap in the increment-25c leg was recorded as a
maximum and *reported to nobody*; a gap above the permitted advance across an eligible turn is now an **event**, named,
with the value it was compared against.

### Fixture and mutations
`test_the_declared_permitted_gap_is_wired_and_its_overrun_is_named` drives the real sampler seam and pins: a gap **at**
the limit is not an overrun (and the block row carries the permitted value, schema 3); one tick past it **is** (counted,
sized at `permitted + 1`, and named with the permitted value in the row); an **idle** gap far above the limit is **not**
an overrun (the maximum is still recorded, the overrun count stays 0); a gap spanning an **epoch change** is a
discontinuity, not an overrun, and accumulates no delta. One property the fixture had to be *repaired* for, recorded:
its first version let the **inter-case** gap form its own block, so a case inherited the previous case's overrun — each
case now advances the tick by one and closes its own block.

**M130** the permitted comparison removed -> *"one tick past the limit is named"* (`Expected 1 Was 0`). **M131** every
gap above the limit counted, eligible or not -> *"an idle gap above the limit is NOT an overrun"*. **M132** the named
row not emitted -> *"one tick past the limit is named"*. **M133** the block row no longer carries the limit ->
*"while carrying the permitted advance it compared against"* (`Expected 64 Was 0`). Restores byte-identical:
`include/Utils/PlaybackLaneMeasurement.h` **3245e37db9c710b7** (pre-mutation), `src/Track/TrackPlaybackWindowBuild.cpp`
**c94103e134533e2a** (pre-mutation).

### Half 2 — NOT done, and the design point is pinned rather than improvised
The admitted window's own promise is still unread by the live path: `PlaybackViewAdmittedWork::deadlineTick` /
`clockEpoch` are written by the admission and read by the budget and the request bookkeeping, and the **overrun policy
that exists** (`advancePlaybackLagTraversal` -> `resolvePlaybackLag` -> `one_pass_late` / `lag_breach` +
`deadlineContractFaultLatched`) is driven by **revision currency** only — and it has **no production caller** either
(only its fixtures call it). Wiring the *deadline* into it needs three decisions this packet does not make mid-flight:

1. **The episode key.** `PlaybackLagState` keys an episode on `(intervalStart, intervalLength)`; the admitted record
   retains `handoffPhase` (the interval start, since the admission passes `coverage.startPhase` as its handoff) but
   **not** the interval length, and `RuntimePlaybackViewBuild`/`Fill` take the coverage without exposing it. So either
   the admitted record gains `admittedIntervalLength` (**+8 B** measured, one recorded per-slot size re-base) or the
   coverage is exposed from the fill state. Retaining what was promised is the consistent choice; it is a state
   decision, not a detail.
2. **The clock reaches the turn.** `Track::stepPlaybackViewMaintenance` takes a phase, not a clock; the lane dispatch in
   `src/main.cpp` already holds one for sampling, so the parameter is a small, defaulted extension.
3. **What a deadline miss means at completion.** The completion path must ask the *existing* resolver with
   `replacementReady = false` (the traversal it was admitted for is already underway), name the miss through the one
   implementation, and **never** extend `deadlineTick` (the increment-21 rule, now on the service side).

None of these is large; all three are *state/interface* decisions, and this increment's own discipline is to pin scope
before editing rather than invent it mid-file. **Blocker 3 is therefore half closed**: the permitted service-gap limit
**is** wired and its exceedance **is** named on the live lane, while the admitted deadline is still not read where the
promise is kept.
