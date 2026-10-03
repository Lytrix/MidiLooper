# Slice 3.10 (increment 16) — the host clock is reachable from the managed harness, the 2× control, and the corrected attribution of the worst-case advance

**Status:** 2026-10-03. One device run, **PASS**, identity clean. This increment **corrects increment 15's attribution**, **corrects its "blocked" note**, and produces a **new, larger maximum** — while keeping the derived admission **disabled** and claiming **no bound**.

## 1. Correction: the increment 15 attribution was wrong — the internal clock cannot batch

Increment 15 recorded the worst-case advance as **clock batching**, on the strength of the code path
`currentTick += Config::TICKS_PER_CLOCK` and the design's line that *"batched pulses can compress the effective
deadline"*. Read against the source, that attribution does not hold for the regime run 5 actually ran in:

- `ClockManager::updateInternalClock()` — the **only** path while `clockSource == CLOCK_INTERNAL` — advances the tick by
  `currentTick++`, **one tick per IntervalTimer callback** (`ClockManager.cpp`, the timer armed at `microsPerTick`). A
  bulk jump is not constructible there.
- The external path (`onMidiClockPulse` / `handleMidiClock`) is where `+= TICKS_PER_CLOCK` lives, and run 5
  (`--clock-source device`, the default) never entered it: its `DIAG,clk_batch` telemetry read **`0,0`**.

So the 2 138-tick step of run 5 is **lane-service starvation under a running transport**, not a compressed tick. The
number was right; the mechanism named for it was not, and it is now recorded correctly.

## 2. Correction: the host clock **is** reachable from the managed entry point

Increment 15 recorded the deliberate batching leg as **blocked** — *"no managed scenario drives a host clock … would
need a clock-driving stimulus variant"*. That conclusion was drawn from the scenario list alone and is **incomplete**:

- `scripts/hitl/runner.py` parses with `parser.parse_known_args(argv)`; unknown flags fall into `legacy` and are
  forwarded to the legacy baseline.
- `scripts/hitl/scenarios/base.py` merges with `merge_legacy_cli_args(defaults, legacy)`, whose own comment is
  *"user flags override via last-wins"*.
- `scripts/hitl/legacy_record_baseline.py` already declares `--clock-source {device,host}` and paces the host master off
  `--tempo-bpm`.

So `--clock-source host --tempo-bpm <n>` reach the record baseline **through the authoritative entry point**, with **no
temporary stimulus variant and no canonical stimulus edit**. The earlier "blocked" note is retracted.

## 3. Run 6 — host-driven clock at 240 BPM (the 2× control)

```
host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 \
                      --clock-source host --tempo-bpm 240 --build-upload
```

**`Result: PASS`** (exit 0). `metadata.json`: `git_commit 887ba773fe2c…`, `firmware_hex_sha256
44bdec575d24be0b…` — **byte-identical firmware to run 5**, so the two runs differ **only** in clock regime and flow.

**The device really adopted the host clock, measured not assumed:** `DIAG,service_period` max = **10 416 µs**, which is
exactly `microsPerTick × Config::TICKS_PER_CLOCK` at 240 BPM (60 000 000 / (240 × 192) × 8 = 10 416 ✓) = **1.0 s per bar
= 96 pulses/s**. The external path budgets a whole clock pulse per service, which is why the row changes from the
internal regime's **2 604 µs** (one internal tick at 120 BPM).

**Batching exists — and is bounded well under the declaration.** `DIAG,clk_batch` (max bearing pulses, batch count):
**`0,0`** under run 5's internal clock, **max 42 pulses / 480 batches** under the host clock. A 42-pulse batch is
42 × 8 = **336 ticks**, i.e. **44 % of the declared 768** — so batching, on its own, **never** violated the declaration.
This is the separation increment 15 could not make.

## 4. The 2× control: the rotation is tempo-invariant; the constant is not

| | run 5 | run 6 |
|---|---|---|
| clock | internal, device master | **host master (external)** |
| tempo | 120 BPM | **240 BPM** |
| tick rate | 384 ticks/s | **768 ticks/s** |
| ticks per lane opportunity | **2.9** | **5.5** (= ×2) |
| wall-clock per lane opportunity | **7 475 µs** | **7 309 µs** |
| declared `maximumPhaseAdvancePerTurn` = 768 ticks | = **2.0 s** | = **1.0 s** |

**The two rows in bold are the result:** the per-opportunity advance in ticks scales **exactly with the tick rate**,
while the **rotation period does not move**. Therefore the declaration's wall-clock meaning **halves** when the tempo
doubles — the constant is expressed in a **tempo-dependent** unit. That is a property of the unit, not of the
observation, and no measurement of "the rotation takes ~7.4 ms" changes it.

## 5. The new maximum, and what it is made of

`DIAG,win_gap` rows for run 6 (all 17 rows taken while the transport ran; 1 reset reported):

```
maxDelta:  0  8  8  8  8 16 56 56 1144  8 7784 296 600  8 8520 2400  8
            min 0 / p50 8 / p75 296 / p90 2400 / max 8520
            rows above the declared 768:  4 / 17   (24 %)
```

Compare run 5 (**1 / 84 rows**, 1.2 %) and run 4 (**2 / 25**). The typical value under the host clock is **8 ticks —
exactly one clock pulse per opportunity**, the floor the external quantisation imposes — and the tail is an order of
magnitude worse than anything seen under the internal clock.

**A single episode, not a sum read wrong.** The `7784` row reports Δmicros **10 262 508 µs** against `maxDelta 7784`
(7 784 × 1 302 µs = **10.1 s**) — the whole 16-opportunity interval was consumed by **one** advance. The `8520` row
(Δphase +8 616 against `maxDelta 8520`) is the same shape. So the tail is **one starvation episode ~10–11 s long**,
against a nominal rotation of **7.3 ms** — a factor of ~**1 400**.

**Wall-clock starvation, all runs, in the constant's own allowance:**

| run | regime | worst step | wall-clock | vs declared allowance |
|---|---|---|---|---|
| 4 | internal 120 BPM | 4 355 ticks | **11.3 s** | 5.7× (of 2.0 s) |
| 5 | internal 120 BPM | 2 138 ticks | **5.6 s** | 2.8× |
| 6 | **host 240 BPM** | 8 520 ticks | **11.1 s** | **11.1×** (of 1.0 s) |

## 6. It is **not** one blocking call — so no bound follows from "the longest span"

The largest single blocking spans in run 6's own telemetry: **`load_frame` 501 188 µs (501 ms)**, `idle_maint`
125 529 µs, `persist_save` 44 559 µs (120 693 µs in run 5's leg). The **largest** of these is ~20× **smaller** than the
measured gap. The gap is therefore **cumulative starvation** — many consecutive lost lane turns — and *not* an artefact
of one long call. A bound cannot be read off the longest blocking operation.

## 7. Instrument limits, stated (they bound what this run can show)

- **The instrumented build inflates the measurement.** `teensy41-capture-serial` with session capture emits `[Memory]`
  heap-walk dumps **once per second** plus `#CAP`/`DIAG` rows per tick. The measured figure is therefore an **upper
  bound under instrumentation**, not a production figure, and the production worst case is **unknown** (probably
  smaller). It is nonetheless the only measurement there is — and it **exceeds** the declaration, which is all that is
  needed to keep increment 15's falsification standing.
- **A stop/restart inside an interval is invisible.** The sampler accumulates a delta only between two samples that both
  read *running*, and `sGapLastPhase` is updated unconditionally, so a mid-interval stop both suppresses the delta and
  hides itself: run 5's `…1328` row shows a net phase change of **+338** while reporting a single step of **2 138**,
  which is only consistent with a non-running sample inside the interval. `resets = 0` therefore does **not** prove that
  no reset occurred; it proves that none occurred between two *running* samples.
- Five runs, two flows, one board, two tempos. This bounds neither the worst case nor the `H × K` envelope.

## 8. What follows for the design decision (stated, not applied)

The split increment 15 proposed was **"a typical service interval + a batched worst step"**. Run 6 shows the second half
is the **wrong term**: batching is bounded at ≤ 336 ticks (under the declaration), while the unbounded term is
**starvation**, which is 5.6–11.3 s of wall-clock. The honest split is therefore:

- **the per-turn scheduling advance** — what `maximumPhaseAdvancePerTurn` names, measured near **8 ticks** at the
  floor and ~2.9–5.5 ticks per opportunity in the steady state — which *is* boundable; and
- **blocking/starvation episodes** — seconds-scale, caused by cumulative lost turns, which belong to the **lag /
  one-pass-late** machinery (3.6b) and **not** to a per-turn constant.

Enabling `applyWindowCostToContract` on the declared value remains **wrong** in the unsafe direction increment 15
identified, and a larger constant does not fix it (the typical value and the starvation tail differ by ~1 000×). The
derived admission **stays disabled**; no `H × K` envelope is declared here, because the measured maximum is a
starvation figure under instrumentation, not a scheduling bound.

## 9. Verification

- Device run **PASS**, exit 0; `overall_ok = True`; serial log and report kept at
  `$PI_SCRATCH_DIR/hitl-gap-6/` (`session_20261003_181525.log`, `host_midi_automation_baseline_20261003_181643.json`).
- Identity: `git_commit 887ba773fe2c3cac59e9bf5648628b2191f80f41`, `firmware_hex_sha256
  44bdec575d24be0b6d870b28f6c02bd7dc3e128e0e15345e2947d0dac9ee716a` — no code changed in this increment, so no native
  suite, no RAM1 movement and no re-base.
- This increment is **documentation and measurement only**; it changes no production line.
