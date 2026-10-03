# Slice 3.10 (increments 12–13) — the service-gap unit measured on the device, twice, and why it is still not a bound

**Status:** 2026-10-03. Two device runs, both **PASS**. The unit is measured and the instrument was extended once (the
transport state). A **second** ambiguity remains, named below; the derived admission therefore **stays disabled**.

## Run 1 (increment 12) — the unit, and the instrument's first limit

`host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 1 --build-upload` (the canonical baseline
stimulus with **its own targets**; the earlier failed leg used track 5/slot 1, which is why its content never armed).
**`Result: PASS`**; `metadata.json`: `git_commit a545def42d26…`, `firmware_hex_sha256 531259700436…`.

34 rows over **528 lane opportunities**: the **phase advance per lane opportunity** — the unit the admission's
`maximumPhaseAdvancePerTurn` is declared in — measured **median 3.25 ticks, p90 3.5** (≈ 8 458 µs apart), against a
declared **768** (one bar). The per-row max ranged 0…**9 367** (mean 279.5).

**Limit found:** the row carried **no transport state**, so "the lane was not served for a long time" could not be
separated from "the transport was stopped" (a stopped transport advances no ticks), nor a wrap from a real gap.

## Run 2 (increment 13) — the state added, and the remaining ambiguity

The row now carries the flag, and the sampler only accumulates a delta **between two samples taken while the transport
ran** (`stepPlaybackViewMaintenance(currentPhase, cursor, transportRunning)`, the caller supplying
`clockManager.isClockRunning()`). Row: `#CAP,<micros>,DIAG,win_gap,<maxDeltaTicks>,<opportunities>,<phase>,<running>`.
Native fixture extended: a **stop/restart jump is not a service gap**, and an all-stopped interval accumulates none at
all (**76/76**).

**Measured, `Result: PASS`, `metadata.json`: `git_commit 6913a7205332…`, `firmware_hex_sha256 1d7277fe868b…`:**

| | value |
|---|---|
| rows | 22 — **18 running, 4 marked not-running** (the discrimination works) |
| **running** rows, `maxDeltaTicks` | min 0, mean **515.4**, **max 4 337** |
| micros per lane opportunity (median) | **7 416 µs** |
| **ticks per lane opportunity (median)** | **2.88** |
| the declaration | **768** (one bar) |

**So the steady state is confirmed: ~2.9 ticks per lane opportunity, roughly 265× below the declared one bar.** But the
**max is still 4 337 ticks**, and I cannot yet say what it is:

- **either** a genuine long stall (a save/load or a dense leg that left the lane unserved for ~seconds), **or**
- **a tick *reset* misread as a loop wrap**: the sampler's wrap branch treats any *decrease* in `currentPhase` as a wrap
  around the loop (`currentPhase + loopLength - sGapLastPhase`), which is right for a loop-relative phase but produces a
  huge apparent gap when the tick counter itself resets (a transport restart). The row carries **no discriminator**
  between the two.

**No bound is claimed, and that is the point of this record.** Declaring an envelope from 4 337 would either overstate the
requirement by three orders (if it is an artefact) or understate a real stall's consequence (if it is real).

## The next instrument fix (small, and specific)

Carry, in the same row, what distinguishes the two: the **loop-relative phase with an explicit epoch/reset marker** (or
the clock-source state), so a reset is *reported* rather than inferred — then the running-max field means "ticks of
playhead travel between two services while the transport ran", which is exactly what the admission needs. With that, the
supported `H × K` contention envelope can be declared (the review's step 2) and the derived admission re-based and
enabled.

## Claimed / not claimed

- **Claimed:** the unit exists and is measurable; under continuous transport it is **~2.9–3.25 ticks per lane
  opportunity** on this firmware, stimulus and tempo — versus a declaration of 768; and the row now distinguishes the
  transport state (18/22 vs 4/22 in run 2).
- **Not claimed:** any **bound** (the residual ambiguity above); any generalisation across tempos, loop lengths, track
  counts or clock sources (two runs, one configuration each); and therefore **no envelope**, so the derived admission
  **stays disabled**.
