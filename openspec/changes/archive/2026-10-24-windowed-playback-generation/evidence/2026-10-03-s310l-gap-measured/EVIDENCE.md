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

## Runs 3 and 4 (increments 13–14) — two hypotheses tested, one refuted, and what the maximum actually is

**Run 3** (reset-aware row, gate on the *clock*): 15 rows, all "running", **0 resets** — which **refuted my own wrap/reset
hypothesis**: the maxima were neither wraps nor resets. It also showed the gate was wrong: `clockManager.isClockRunning()`
is **the clock**, and a running clock with a **stopped transport** keeps advancing the tick while nothing needs service.

**Run 4** (gate corrected to `clockManager.isTransportRunning()`, `--build-upload`): **`Result: PASS`**.

| quantity | run 4 |
|---|---|
| rows / running / not-running | **25 / 21 / 4** (the gate now discriminates) |
| running `maxDeltaTicks` | min 0, mean **459.0**, **max 4 355** |
| resets reported | **0** |
| **ticks per lane opportunity (median)** | **2.88** |
| wall clock per lane opportunity (median) | **7 475.6 µs** (⇒ ≈ 385 ticks/s at this tempo) |
| the declaration | **768** (one bar) |

**The two readings, and they point in opposite directions — which is the finding:**

1. **Typical:** ~**2.88 ticks** per lane opportunity, i.e. the lane is served every ~7.5 ms and the playhead moves ~3 ticks
   between services. The declaration (768) over-states *this* by ~265×, and it is why the declared envelope refuses work
   the lane could trivially service.
2. **Worst observed while the transport ran: 4 355 ticks** (mean 459), with **0 resets** — so it is a *positive* jump, not
   a wrap and not a reset. The mechanism the code names for exactly that is **clock batching**: `ClockManager` advances
   `currentTick += Config::TICKS_PER_CLOCK` in the batched paths, and the design itself records the hazard — *"batched
   pulses can compress the effective deadline"*. **A batched advance is real playhead travel**: it compresses the time the
   window has, which is precisely what `maximumPhaseAdvancePerTurn` exists to bound.

**So the declaration is not a bound at all — it is far too small at the maximum and far too large in the median.** The
first device evidence therefore says the admission's single constant cannot serve both roles: the *typical* service gap
(~3 ticks) and the *worst* single-step phase advance (~4 355 ticks, batching included) differ by three orders.

**Not claimed:** that 4 355 is the worst case (21 running samples, one leg, one configuration — the clock batch may be
larger elsewhere), nor that ~2.88 is the envelope's typical figure across tempos. The next step is a longer leg that
*deliberately* exercises batching (an external/USB-device clock burst) to bound it, and then the `H × K` contention
envelope can be declared on top of a measured maximum rather than on my arithmetic.

**Instrument note for the record:** `--build-upload` flashes the **working tree**, while `metadata.json`'s `git_commit`
field reports **HEAD** — run 4's identity is its `firmware_hex_sha256` (`44bdec57…`) because the fix was still uncommitted.

## Run 5 (increment 15) — the `accepted_sequence` preset, three legs, and a falsified safety claim

Run 5 drives the **`accepted_sequence`** preset (three legs: record, overdub, mixed) with the stimulus's own targets
(`--track 8 --loop-slot 4 --slot-b 5 --midi-channel 8`; the leg injects note input on the track's own channel, so the
channel must equal the track). Firmware identity, now clean because the tree was committed first:
**`firmware_hex_sha256 44bdec575d24be0b…`**, HEAD `c49e267d08…`.

**84 `win_gap` rows, all 84 taken while the transport ran** (the gate held across three legs):

| quantity | run 5 |
|---|---|
| running `maxDeltaTicks` | min 0, **p50 4**, **p90 8**, **max 2 138** |
| advances > 100 ticks | **1** (a single outlier; > 1000 also 1) |
| resets reported | **3** (so resets *do* occur, and are now excluded and counted) |
| **ticks per lane opportunity (median)** | **2.94** (≈ 7 574 µs apart) |
| the declaration | **768** (one bar) |

**What is now established.** The *typical* service gap is **4 ticks (median), 8 (p90)** — 100–190x below the declared 768.
The *worst observed* is **2 138 ticks in this run and 4 355 in run 4** — single, isolated, positive jumps while the
transport ran, with resets excluded — consistent with the **clock batching** the code and the design both name.

**And that falsifies the argument the constant rests on.** `playbackViewCoverageContract` records: *"The direction is the
safe one: `canCompleteBeforeHandoff` turns a larger advance into a larger required lead, so an unmeasured value can only
refuse work, never admit too much."* That holds **only if the declared value is an upper bound**. Measured, it is not:
observed single-step advances reach **2 138–4 355 ticks, i.e. 2.8x to 5.7x the declared 768**. The admission therefore
computes a required lead that is **too small** for those steps and can **admit work it cannot complete** — the unsafe
direction the comment rules out.

**Consequence, stated plainly:** the derived admission must **not** be enabled on the declared 768, and the constant
cannot be "corrected" by a single larger number either (the median needs ~4 and the worst case needs >=4 355). The
admission needs the two figures — a typical service interval and a **batched** worst step — or the batching must be
excluded from the tick the admission uses (e.g. by bounding how far the tick may advance per owner turn).

**Not claimed:** that 4 355 is the worst case (five runs, one tempo, one configuration, and the deliberate
external-clock/burst leg is still unreachable from the managed harness — no scenario drives a host clock, so batching
can only be *observed*, not *provoked*, until a clock-driving stimulus variant exists).

---

## SUPERSEDED (2026-10-03) — the mechanism named below is wrong; read increment 16 instead

Runs 5's worst-case advance is **not** clock batching. `ClockManager::updateInternalClock()` — the only path while the
clock source is INTERNAL, which run 5 ran — advances the tick by `currentTick++`, one tick per IntervalTimer callback,
so a bulk jump is not constructible there; the `+= TICKS_PER_CLOCK` path is the *external* one, and run 5's
`DIAG,clk_batch` read **`0,0`**. The 2 138-tick step is **lane-service starvation under a running transport**. The
batching mechanism was later provoked deliberately (host-driven clock at 240 BPM) and measured **bounded at 336 ticks =
44 % of the declared 768** — inside the declaration, not the violation. See
[`../2026-10-03-s310m-host-clock-starvation/EVIDENCE.md`](../2026-10-03-s310m-host-clock-starvation/EVIDENCE.md) and
[`../2026-10-03-s310n-declaration-corrected/EVIDENCE.md`](../2026-10-03-s310n-declaration-corrected/EVIDENCE.md).
The *measured figures* in this file stand; only the mechanism attributed to them does not.
