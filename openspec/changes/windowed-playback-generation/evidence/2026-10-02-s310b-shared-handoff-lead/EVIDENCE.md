# Slice 3.10 (increment 2) — the urgency figure, named once and shared

**Change:** `windowed-playback-generation` · **Task:** 3.10 (**stays open** on the same four items as increment 1) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 3.10 (increment 2)
**Predecessor:** [`…-s310-holder-service-fairness/EVIDENCE.md`](../2026-10-02-s310-holder-service-fairness/EVIDENCE.md)

---

## What landed, and the fidelity fix it carries

| File | Change |
|---|---|
| `include/RuntimePlaybackViewBuild.h` | `handoffLeadTicks(currentPhase, handoffPhase, loopLengthTicks)` — the tick distance along the loop to a handoff, **named once**, beside the contract whose `available` sub-term it is. |
| `src/Track/RuntimePlaybackViewBuild.cpp` | `canCompleteBeforeHandoff` now **calls** it instead of computing the same expression inline, so the admission and any scheduling reader cannot diverge. |
| `include/Utils/MidiServiceDrain.h` | The holder's ordering field is the **tick distance** (`handoffLeadTicks`), not a raw phase; the comparison and its documentation say why. |
| `test/test_midi_service_drain/…` | Block **(F)** added to the holder-service fixture: the derivation's own cases; blocks (B)/(C) re-stated in ticks. |

**The correction, stated plainly.** Increment 1 ordered holders by a raw *phase*, which is only meaningful within one
loop: a holder at phase 1,900 on a 2,000-tick loop has 100 ticks left, while one at phase 100 on a 20,000-tick loop has
a whole pass (the fixture now pins exactly that pair: 1,400 vs 18,200 ticks, with the **more urgent holder carrying the
larger phase**). The quantity that is comparable across holders is the tick distance — and it is the same quantity the
admission already judges, so naming it once makes the scheduler's urgency and the admission's margin the same number.

## The oracle

| Case | Observed |
|---|---|
| the derivation | `handoffLeadTicks(800, 1000, 4000) == 200`; **(F)** the wrap case `handoffLeadTicks(800, 200, 4000) == 3400` (a handoff behind the playhead is a full pass away, never negative); `0` when the phases coincide |
| refusals | an undeclared loop (`loopLengthTicks == 0`) and an out-of-range phase each answer `0` — a refusal, not a wrap |
| phase order is not urgency | the pair above, asserted as a strict inequality in the *opposite* direction to the phases |
| the selector's use | the same five bounds as increment 1, now ordered by ticks; the blocked-holder, named-outcome and rotation properties are unchanged |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_midi_service_drain` | **39/39** (fixture extended, not added) |
| `pio test -e native -f test_runtime_working_set_ownership` | **67/67** (the admission side of the shared definition, unchanged by the refactor) |
| `pio test -e native` (full) | **2700 cases: 7 skipped, 2693 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888`, **48 B headroom**: the shared inline helper compiles to the same code the inline expression did |

## Red-proofs

| # | Mutation | Observed failure |
|---|---|---|
| M73 | the wrap branch of the shared arithmetic is dropped | the new fixture's wrap case (`Expected 3400 Was 0`) **and 11 cases in `test_runtime_working_set_ownership`** — the admission side of the same definition, which is the point of naming it once. Recorded precisely: the *named* margin fixture happened to still pass, so that is not claimed |
| M74 | the out-of-range refusal is dropped | `Expected 0 Was 10. an out-of-range phase is a refusal, not a wrap` |
| M75 | "a holder with a deadline beats one without" is inverted | `Expected 3 Was 0. an outstanding deadline outranks holders that have none` |

Each was applied to a named file at an anchor whose occurrence count was asserted, and each was restored
byte-identically (`shasum -a 256 -c`).

## Not claimed

- **Still no production caller.** The holder scan in `Track::stepPlaybackViewMaintenance` and the `src/main.cpp` track
  scan remain ascending first-match; nothing yet *computes* `handoffLeadTicks` for a live holder. This increment gives
  the policy the right input and one definition of it — the routing is the next step.
- **The fixture's own first two attempts were wrong, not the code** (increment 1's outcome block, and here an assertion
  that claimed different loop lengths make the same phase distance a different wait — they do not: the difference is
  the wrap, which is now what is asserted). Both are recorded rather than quietly corrected.
- The other three open items from increment 1 are unchanged: the **starvation half**, the **envelope derivation** from
  grain costs and the worst permitted service gap, and **"one declared grain per turn"** as a uniform invariant.
