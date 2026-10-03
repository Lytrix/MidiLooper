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
| **(B) the cadence bound** | the reported rows cannot exceed one per declared interval plus the always-reported first — an ungated reporter cannot satisfy it (M82 below) |

**An open question, recorded rather than smoothed over.** The first version of (B) asserted the exact rate ("the first
outcome plus the interval-th" = 2 rows for 17 completions) and **measured 1** (`Expected 2 Was 1`). The loop asserts
`Admitted` and `!pending` on every round, so the *fewer* reported outcomes are not a missing completion at the loop's
level; **why only one outcome reached the reporter is not established by this slice.** (B) was therefore restated as
the **bound** the cadence actually guarantees, and the question is named here for the next increment rather than
explained away.

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
| M82 | the **cadence gate** is dropped (`++count` instead of `shouldLogOccurrence`) | the new fixture: `reported at most once per declared interval, plus the first` — so the bound is load-bearing and the fixture is not vacuous |

Applied to the named file at an anchor whose occurrence count was asserted, and restored **byte-identically**
(`shasum -a 256 -c`). Increment 1's two mutations (the ring fallback dropped, the direct path dropped) remain the
delivery-rules red-proofs.

## Not claimed

- **The refusal paths are still silent by row.** `ContractUndeclared`, `CannotCompleteBeforeHandoff`,
  `AdmissionRefused`, `BuildInFlight` and `SuccessorAlreadyPrepared` return named results and emit nothing; a receipt
  about *why a request was refused* is a different row from the readiness receipt and is **not** in this increment.
- **"One pass late but served" has no row** (`PlaybackLagDecision::PreviousOnePass`); only the breach is named.
- **The exact outcome rate is an open question** (above), and the receipt's cadence interval (16) is a **declared
  choice**, not a measured one.
- **The receipt is not observed on hardware.** It is proved through the capture build's own native seam (a substituted
  `Serial` for the delivery rules; the `#ifndef` recorder for the call site), not on a device.
- **Fixture isolation was repaired, not designed:** the new fixture inherits per-track build state from earlier
  fixtures in the suite, so the cross-track fixture now resets the manager the way the suite's own `setUp` does — a
  repair this slice records because it briefly presented as a production regression.
