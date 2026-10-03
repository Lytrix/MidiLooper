# Slice 3.9 (increment 2) — the receipt's emission site, its cadence, and the RAM1 edge it crossed

**Change:** `windowed-playback-generation` · **Task:** 3.9 (**landed; one open question recorded below**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 3.9 (increment 2)
**Predecessor:** [increment 1](../2026-10-02-s39-readiness-receipt-delivery/EVIDENCE.md) (the delivery rules)

---

## What landed

| File | Change |
|---|---|
| `include/Track.h` | `Track::playbackWindowReadinessLabel(...)` (the outcome's **name**, if/else form for the `.data` reason) and `reportPlaybackWindowReadinessReceipt(slot, currentPhase)`. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | `#include "Utils/TelemetryCadence.h"`; the label mapping; a `DMAMEM` cadence counter; the receipt reporter (**cadence first**, then readiness + holder + revision + the lead from the served window); and the call site on the turn's two terminal outcomes — `GrainDenied` and `BuildComplete` — never per grain. |
| `linker/imxrt1062_t41_lcr.ld` | The DTCM→FLASH routing for this TU **widened to `.rodata.CSWTCH*`** (see below). |
| `test/test_runtime_working_set_ownership/…` | The `SC_WINDOW_READINESS` substitution (the `#ifndef` pattern) plus the fixture for the emission site and the cadence bound. |

## The RAM1 guard caught a second +4096, and the precedent's fix covered it

The wiring first **FAILED** the approved budget: `variables 100,288` against 96,192 — **+4096**, the same edge
slice 3.5 recorded. The cause was not my ~100 B of label literals (those are already routed) but the compiler's own
**jump tables**: `objdump` showed `.rodata.CSWTCH.377/476/494` **unmatched** by the script's patterns, i.e. sitting in
DTCM. The if/else form does **not** prevent the optimizer from tabulating a dense enum chain. The fix is the
precedent's, one class wider — route this TU's read-only tables to FLASH too — and it restores the line **exactly**:
`variables 96,192` / `code 392,328 + padding 888`, byte-identical to the eight preceding slices. The pin was not
re-based.

## The fixture

| Part | Assertion |
|---|---|
| **(A) the content** | driving a real build to completion reports a receipt whose **slot**, **readiness** (`ready`), **holder** (`ready`) and **revision** are what the outcome was — through the suite's own `SC_WINDOW_READINESS` recorder, i.e. the production call site, not a direct emitter call |
| **(B) the cadence rate** | two independent blocks of exactly `kReceiptInterval` rounds each report **exactly one** row, from a primed counter — so the declared interval bounds the rows and one completed build reports once (M83–M85 in the addendum) |

**The first version of (B) measured something other than what it asserted, and that is recorded here.** It asserted the
exact rate ("the first outcome plus the interval-th" = 2 rows for 17 completions) and **measured 1**
(`Expected 2 Was 1`). The loop asserts `Admitted` and `!pending` on every round, so the difference was not a missing
completion at the loop's level; the explanation is in **the addendum below**, and it is the cadence counter being
process-wide rather than the reporter being silent.

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **70/70** (was 69; +1 fixture) |
| `pio test -e native -f test_capture_ring` | **16/16** (increment 1's delivery rules) |
| `pio test -e native -f test_midi_service_drain` | **39/39** |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** |
| `pio test -e native` (full) | **2704 cases: 7 skipped, 2697 succeeded, 0 failed** (baseline 2703 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, **48 B headroom** |

## Red-proofs

| # | Mutation | Observed failure |
|---|---|---|
| M82 | the **cadence gate** is dropped (`++count` instead of `shouldLogOccurrence`) | the new fixture: `reported at most once per declared interval, plus the first` — so the bound is load-bearing and the fixture is not vacuous. **(Superseded by M83–M85 in the addendum below, which assert the rate itself rather than a bound.)** |
Applied to the named file at an anchor whose occurrence count was asserted, and restored **byte-identically**
(`shasum -a 256 -c`). Increment 1's two mutations (the ring fallback dropped, the direct path dropped) remain the
delivery-rules red-proofs.

## Not claimed

- **The refusal paths are still silent by row.** `ContractUndeclared`, `CannotCompleteBeforeHandoff`,
  `AdmissionRefused`, `BuildInFlight` and `SuccessorAlreadyPrepared` return named results and emit nothing; a receipt
  about *why a request was refused* is a different row from the readiness receipt and is **not** in this increment.
- **"One pass late but served" has no row** (`PlaybackLagDecision::PreviousOnePass`); only the breach is named.
- **The cadence interval (16) is a declared choice**, not a measured one. What *is* measured (M83) is that the reporter
  honours it exactly, from any starting phase.
- **The receipt is not observed on hardware.** It is proved through the capture build's own native seam (a substituted
  `Serial` for the delivery rules; the `#ifndef` recorder for the call site), not on a device.
- **Fixture isolation was repaired, not designed:** the new fixture inherits per-track build state from earlier
  fixtures in the suite, so the cross-track fixture now resets the manager the way the suite's own `setUp` does — a
  repair this slice records because it briefly presented as a production regression.

## Addendum — the cadence loose end, closed by measurement (same day)

**(B) is no longer a bound; it is the declared rate, measured twice.** The open question above had one answer, and it
belonged to the **fixture**, not the production path. `sReadinessReceiptCount` is a **process-wide TU static**, not
per-fixture state: by the time this fixture runs, earlier fixtures in the same binary have already advanced it.
`TelemetryCadence::shouldLogOccurrence` emits when `count == 1 || count % interval == 0`, so **any 16 consecutive
counter values contain exactly one** multiple of 16 — so the first version's 17 rounds produced exactly one row, which
is the cadence behaving correctly from an unknown phase. No emission was missing: the loop's per-round `Admitted` and
`!pending` assertions were already true.

The fixture now **primes** the counter with `2 × kReceiptInterval` rounds whose count is **deliberately not asserted**,
then measures **two independent blocks of exactly `kReceiptInterval` rounds**, each asserted to report **exactly one**
row. Both blocks start above the `count == 1` rule, so the assertion is phase-independent: it pins a rate, not this
fixture's position in the binary. Each round still asserts its own `Admitted` and `!pending`, so the 64 completions
driven cannot be vacuous.

**Why the priming count is not asserted (a self-inflicted trap, recorded).** The first pass of this addendum *did*
assert a priming bound, "no more than the declared rate plus the first". All three mutations below then failed **at
that line** — a strictly weaker assertion that **masked** the rate assertion, so the sharp assertion never spoke and
the run was weaker evidence than it looked. The bound was removed; the mutations now fail on the rate itself.

**A second property fell out of the same assertion.** `exactly one` over 16 rounds also pins **one reporter call per
completed build**: two calls per round would be 32 calls and therefore exactly 2 rows (measured under M85).

| # | Mutation | Observed failure (measured value) |
|---|---|---|
| M83 | the declared interval `16` → `8` | `Expected 1 Was 2` — the rate assertion is what pins the declared interval |
| M84 | the **cadence gate dropped** (`shouldLogOccurrence` called, its result ignored) | `Expected 1 Was 16` — one row per outcome, i.e. exactly the eviction the cadence exists to prevent |
| M85 | the receipt reports on **every service turn** instead of the two terminal outcomes (`if (true)`) | `Expected 1 Was 2` — **the value is the finding**: 2 rows = **2 calls per round** for this fixture's build, so the assertion separates "one row per completed build" from "one row per grain". I predicted 7 for this mutation before measuring; the measured figure is 2. |

M82 (the gate dropped by replacing the call with `++count`) remains the increment's original red-proof; M83–M85 are the
sharpened set that replaced the masked run. Every mutation was applied to `src/Track/TrackPlaybackWindowBuild.cpp` at an
anchor whose **occurrence count was asserted to be 1** (the M44/M50 lesson: a wrong-file or non-unique anchor yields a
green run that is not evidence), and each was restored **byte-identically** — the TU's sha256 was `b5c5220e…344b66`
before and after every case.

Re-verification after the sharpening: `test_runtime_working_set_ownership` **70/70**; full `pio test -e native`
**2704 cases: 7 skipped, 2697 succeeded, 0 failed**; `pio run -e teensy41-capture-serial` **SUCCESS** with the RAM1 line
**byte-identical** (`variables 96,192 / code 392,328 + padding 888`). That build took **2.77 s**, which is a **relink,
not a rebuild** — this addendum changes only the test file, so no firmware object changed and the RAM1 line cannot have
moved; it is reported as a no-op rather than as a re-measurement.
