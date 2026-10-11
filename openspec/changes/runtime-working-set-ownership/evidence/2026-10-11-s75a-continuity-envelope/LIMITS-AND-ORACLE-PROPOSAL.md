# S7.5a — the declared limits, the per-row measurement plan and the verdict oracle (increment 5: a **proposal**)

**Status: HISTORICAL, UNAPPROVED PROPOSAL VERSION — SUPERSEDED for current limits and oracle.**
**Use [`DECLARED-LIMITS.md`](DECLARED-LIMITS.md) §2 (current table), §2b (input-only exception policy),
§3 (current oracle) and §9 (corrected observations).** The signed numerical record stands; this older version
is not retroactively approved. In particular its **12,500 µs** choices are historical proposals, not current caps.
S7.5a — continuity envelope stays unchecked; no qualifying run or firmware/test/device change is claimed here.

**Representation correction:** this version's old table/plan surfaces below are historical proxies, not proof:
combined input requires direct `D_i + G_(i+1)` cycle histogram (**unmeasured**), not marginal quantile sums;
`capstamp_max` is positive in-service drift, not signed physical-arrival error; `late_route(type,ch)` is logical
and `late_on/off` pooled, not physical-route quantiles; eligible advance uses `eligiblePredecessorMax` with
`permitted/overruns/overrunMax`, not raw `maxDelta`; block-total `deniedTurns` is not consecutive holder wait.
Burst **16 aggregate / 8 physical route** is a chosen workload limit, not a theorem from eight tracks.
USB output does not qualify the **1,041 events/s DIN** reference or MI input. Required missing data always
prevents qualification; accepted input p99 violations are never plain PASS. The current oracle, not historical
§4's wording about unproven metrics, decides this. No measurement instrument or runtime verdict enum is implemented.

## 1. How a limit is expressed here: the metric's own unit, justified by musical time

Two units are in play and they answer different questions:

* **µs** is the unit of the *processes* — the MIDI input service, the emission path, serialization, the service
  period. Those are wall-clock and tempo-independent: the same 12 ms of scheduler work costs 12 ms whether the
  transport runs at 20 or 300 BPM.
* **Ticks** is the unit of *musical damage* — how far an event was displaced on the grid. The same µs harms the music
  more at a fast tempo.

So **a limit is declared in the unit its metric is measured in** — µs for the timing families, ticks or counts where
the row itself counts ticks or events — and musical time is used to **justify** the number, never as a run-time
conversion the reader has to perform. The µs values are the musical interval read at the **fastest reachable tempo
(300 BPM, 1,041.7 µs per tick)**, i.e. the tightest case, so one fixed number holds at every tempo and the oracle
compares a row against it directly: `µs = 60000000 / BPM / 192 × ticks` (`60000` gives milliseconds).

**Why the tightest tempo rather than a per-tempo table.** The failure this envelope exists to catch is a *scheduler*
excursion, and the musically equivalent bound at 20 BPM (187.5 ms for 12 ticks) would tolerate a 187 ms stall — the
CPU having left the MIDI path entirely — because the music is slow. A fixed µs cap cannot be satisfied by playing
slowly. The cost is stated plainly: at slow tempos the cap is stricter than the music alone requires, so if that
slack is wanted back it must be declared as an explicit named allowance (§6 question 1), not discovered per run.

**The readings collected in increments 3–4 are evidence that an instrument is fed — they are not, and must not
become, the source of any limit in §2.** `Δmax` (Table A, 64 ticks) is the one anchor a limit is inherited from, by
citation, with its own owed owner confirmation still attached.

## 2. L1 — the proposed limits (all **proposed**, none approved)

| # | Metric | Row / field (measurement point) | Limit, in the row's own unit | Musical justification (the derivation) | Equivalent at 300 BPM |
|---|---|---|---|---|---|
| L1.1 | `max_midi_input_service_gap` | `DIAG,midi_gap,<maxUs>` (+ `env_dist` p50/p99) | **≤ 12,500 µs** | An arrival is stamped at *process* time, so a gap displaces it by the gap's worth of ticks; 12 ticks = 1/16 beat is the band's own upper edge, the largest displacement still inside one notated subdivision | 12 ticks (≈0.8 tick at 20 BPM: the cap is deliberate wall-clock) |
| L1.2 | `max_midi_input_service_duration` | `DIAG,midi_input,<maxUs>` (+ `env_dist`) | **≤ 12,500 µs**, and `gap + duration` also ≤ 12,500 µs | Same displacement argument, and it is the *sum* of the two that delays the next arrival — so the oracle checks both lines | 12 ticks |
| L1.3 | `capture_timestamp_error` | `DIAG,envelope_instr,capstamp_max` (+ `capstamp_n`) | **≤ 1 tick** (the row counts ticks; its µs form is tempo-dependent and is *not* the limit) | The storage grid *is* the 192nd note (PPQN 192): an event stored on a neighbouring tick is a grid error. Admissible at every tempo: one DIN byte is 320 µs against a 1,041.7 µs tick at the fastest reachable tempo | 1.04 ms |
| L1.4 | `per_route_output_lateness` — deadline line | `DIAG,late_route,<type>,<ch>,<maxUs>` | **≤ 66,700 µs** | An emission may consume the lane's whole permitted advance (`Δmax` = 64 ticks, cited from Table A) for one opportunity and no more | 64 ticks |
| L1.5 | `per_route_output_lateness` — typical line | `DIAG,env_dist` `late_on`/`late_off` | **p50 ≤ 6,250 µs** and **p99 ≤ 66,700 µs** | The *median* emission should land inside the finest subdivision the band names (6 ticks = 1/32 beat); the p99 line is L1.4 restated so the shape is checked, not only the worst sample | 6 / 64 ticks |
| L1.6 | `per_route_output_jitter` | fitted-per-traversal residual | **≤ 6,250 µs** | Scatter around the route's own clock line should not exceed the finest subdivision either; this is the line that separates "late" from "irregular" | 6 ticks |
| L1.7 | `deferred_owner_service_advance` | `DIAG,win_gap.maxDelta` vs `permitted` | **≤ 64 ticks** — compared against the row's own `permitted` field | Table A anchor; the row already carries the declared term, so the oracle reads the field rather than repeating a constant | 66.7 ms |
| L1.8 | `deferred_owner_denial` | `DIAG,win_gap.deniedTurns` (+ `eligibleTurns`) | **≤ 7 turns** (= `H`) per eligible window | The across-track rotation is `NUM_TRACKS - 1`: an eligible lane is revisited within H turns, so H is the largest denial a rotation can produce without starvation | — |
| L1.9 | `same_tick_burst` | `MO` tick column, per traversal — reported per route **and** aggregated | **≤ 16 events** aggregate, **≤ 8** on any one route | Derived from the *supported* topology (8 tracks × one sounding slot ⇒ on + off per role), not from a run. Bursts above `L` = 2,048 admitted events stay unsupported (§5); the serialization share is reported separately | — |
| L1.10 | `route_throughput` (output) | `MO` rows per route | **≤ 1,041 events/s** on DIN (3-byte messages, no running status) | Transport capacity, accounted **separately** from firmware lateness as the spec requires: 31,250 baud, 10 bits/byte ⇒ 3,125 B/s ⇒ one 3-byte note message every 960 µs. The firmware requirement under it is L1.4, not a throughput figure | — |
| L1.11 | `route_throughput` (input) | `MI` rows per route | **≤ 1,041 events/s** on DIN | Symmetric with L1.10; the firmware requirement under it is L1.1/L1.2 | — |
| L1.12 | `capture_replenishment` | `DIAG,envelope_instr,repl_wait_us` / `repl_pairs` | **no limit declared** (§5): never observed, and a refusal fails the run outright | The metric's role is diagnostic — a refusal is a fail-class event, so a "latency limit" would only describe how the failure looked | — |
| L1.13 | `queue_residence` | `DIAG,droid_queue` | **no limit declared** (§5) | Family compile-gated off in the default capture env; no owner/age probe for any other queue | — |
| L1.14 | `action_to_audible` | parked | **kept out** (owner decision 2026-10-11) | Two questions (which operator action, which emission class) under one position-based rule; see the increment-4 packet's decision section | — |

Two consequences worth stating plainly, because they are properties of the proposed numbers rather than of any run:

* **L1.3 is tight and admissible.** 1 tick at 300 BPM is 1.04 ms, and the DIN byte floor is 0.32 ms, so "store an
  event on the tick it arrived on" is not physically contradicted at any reachable tempo.
* **A 16-event same-tick burst cannot be serialized inside one tick on a DIN route** (16 × 960 µs = 15.4 ms against a
  1.04 ms tick at 300 BPM). Under the spec's separation rule that is *not* a firmware lateness failure; it is the
  route's physical share, and the honest report is "serialized over N ticks", named as such.

## 2b. The tempo axis: what a 300 BPM leg covers, and what it does not

**Decision recorded (owner, 2026-10-11): the qualifying envelope is exercised at the fastest reachable tempo.**
`ClockManager` enforces **20–300 BPM** (§2), so 300 BPM is the bound, and it is simultaneously the worst case on both
axes that matter: the **tightest musical deadline** (1 tick = 1,041.7 µs, so the 14-tick caps of §2 are at their
smallest µs values) and the **highest sustained event rate** (960 ticks/s against 64 ticks/s at 20 BPM, so the same
musical density loads the firmware ~15× harder). A leg at **180 BPM** is a useful *musical reference* — it is the
tempo a demanding player actually reaches — but it is **coverage, not the bound**: 300 BPM is the number that
decides.

**Correction to the historical tempo inference.** Fixed caps are an owner policy, not proof that a pass at
300 BPM implies every slower tempo or multi-track workload passes. Lower nominal musical rate can relax
musical deadlines without bounding independent contention, operation batching or clock advancement.
One 300 BPM run does not qualify the whole matrix; the named slow and batched-clock coverage below remains owed.

**What it does not buy — two named classes still need the slow end of the range:**

1. **Batched external clock.** External pulses advance playback directly (`TICKS_PER_CLOCK` per pulse), and batched
   pulses **compress the effective advance** (recorded in §2 and `DEC-108` item 7). A nominal 300 BPM leg does not
   exercise that, so the proof must run against **actual clock advancement**, and the batched case stays a named
   exception until a leg measures it.
2. **Wall-clock contention.** Persistence, deferred save, display and load work keep their **own** arrival cadence
   while the transport merely plays slower: a two-bar loop is **1.6 s at 300 BPM but 24 s at 20 BPM**, so a slow leg
   accumulates many more chances for a stall to land *inside* a musical interval, and SD write latency is recorded as
   unbounded until measured. A 300 BPM leg therefore does not bound the contention axis; the matrix keeps a slow leg
   for it (the §3 rows name which is which).

**Correction to the historical plumbing claim:** the canonical managed entry point supports
`host_midi_hitl.py run --preset base --clock-source host --tempo-bpm 300 …`, forwarded to the baseline.
The current registered recipe, invocation and actual tempo/clock provenance must match. This capability does
not supply a qualifying run or authorization to execute it, alter a canonical stimulus, overwrite or upload.
Content must also be tempo-following for musical-density rate comparisons to mean anything.

## 3. L2 — the measurement plan, one row per metric (covers `DECLARED_ENVELOPE.md` §8 item 3)

**Which leg each row needs (§2b):** every row below is exercised **first at the fastest reachable tempo (300 BPM)**,
which is the bound for all *deadline-shaped* rows; the rows whose failure mode is *wall-clock contention* rather than
deadline tightness — replenishment, and any overlap with persistence/display/load work — additionally need the **slow
leg**, together with the batched-external-clock case, which no nominal-tempo leg covers.

| Metric | Capture point (authoritative) | Clock / correlation | Uncertainty to record | Matrix axes to cover | Supplied by |
|---|---|---|---|---|---|
| `max_midi_input_service_gap` / `_duration` | device `DIAG,midi_gap` / `midi_input` rows + `env_dist` intervals | device µs native (no correlation) | none beyond the row's own µs resolution; the distribution is **bucket-bounded** (256 µs below 65,536 µs, wider above) so a quantile is an interval | short/long loop, lowest/highest reachable tempo, internal/external clock, host-driven and DIN input | existing HITL legs; a dense-input leg still owed |
| `capture_timestamp_error` | `DIAG,envelope_instr.capstamp_max` with `capstamp_n` (samples behind the max) | device ticks native | none; report `capstamp_n` beside the max so the sample count is visible | record + overdub at both loop lengths; boundary-dense material | existing legs |
| `per_route_output_lateness` (max, split) | `DIAG,late_route` per (type, channel) | device µs native | none; `spill_samples` must be 0, else the split is truncated and the reading is partial | every enabled route; both clocks; short/long | existing legs (one route so far) |
| `per_route_output_lateness` (distribution) | `DIAG,env_dist` for `late_on`/`late_off` | device µs native | **bucket interval** — a limit inside a bucket is *indeterminate*, not a pass | as above | existing legs |
| `per_route_output_jitter` | fitted residual per loop traversal (the tick column wraps) | device µs + tick column | the fit's own rate and the traversal count; ≥ 8 samples per traversal or the metric refuses | long loops (enough samples per traversal) | existing legs — interpretation still owed (§5) |
| `deferred_owner_service_advance` / `denial` | `DIAG,win_gap` (`maxDelta`, `permitted`, `deniedTurns`, `eligibleTurns`) | device ticks native | none | lane enabled, under transport, both clocks | `window-qualification` image only (the lane is off in the default env) |
| `same_tick_burst` | `MO` tick column per traversal | device — tick column | identical-tick rows are the burst; tick 0 means *unstamped* and is excluded (stated under-count) | boundary-dense content; burst ≥ 16 | existing legs |
| `route_throughput` | `MO` / `MI` row rates per route | device µs native | rate is computed over the window's own span; report the span | DIN and USB routes separately | existing legs (USB so far) |
| `capture_replenishment` | `DIAG,envelope_instr.repl_wait_us` / `repl_refused` / `repl_pairs` | device µs native | none; `pairs = 0` means never observed | requires a leg that actually refuses or resolves a top-up | **not yet supplied by any run** |
| `queue_residence` | `DIAG,droid_queue` | device µs native | family is compile-gated (`SESSION_CAPTURE_DROID_QUEUE=0` in the default capture env) | requires the gated family enabled on the qualifying image | **not supplied today** |
| Any host-stamped interval (e.g. a named acceptance marker) | host log line + device rows | **host stamp read as device time**, bounded by the smallest host-stamp-to-preceding-device-row gap (102–548 µs measured) | that bound **plus** the host stamp's 1 ms resolution must be reported with the reading | — | host tooling |

## 4. L3 — the verdict oracle (mechanical, one run at a time)

**Inputs.** A pinned run: image SHA-256, the env metadata pin (a capture whose metadata names the wrong environment
is not accepted), the registered stimulus ID, `invocation.json`, and the run's own tempo/clock rows **as context for
reading the music** — not as a scaling factor. Every µs limit of §2 is fixed, so nothing is converted at run time;
the tempo is reported beside the verdict so a reader can see which musical interval the run was playing.

**Per metric**, read its row and compare it against its own fixed limit, returning one of:

| Verdict | Rule |
|---|---|
| `in-limit` | max-typed: `maxUs ≤ limit`. Quantile-typed (bucket-bounded): the whole bucket interval is at or below the limit (`hi ≤ limit`). Count-typed: `count ≤ limit` |
| `over-limit` | `maxUs > limit`, or the bucket's lower bound is already above it (`lo > limit`), or `count > limit` |
| `indeterminate` | the bucket interval **straddles** the limit — the instrument's resolution cannot decide it. Reported as such; never rounded into a pass |
| `unmeasured` | the row family is absent from the capture (env-gated, not fed, or the leg did not exercise it) |

**Run verdict.** `PASS` requires (a) every *declared* metric `in-limit` **and** (b) no fail-class event anywhere in
the run. **Fail-class events, which void the run regardless of every number above:** MIDI-content refusal,
deadline-contract fault/latch, a second consecutive missed traversal, unintended slot silence, involuntary
STOPPED/recovery, capture loss or ring loss, and a content-parity mismatch. Recovery from a fault does not convert a
run into a pass (spec § *Fault recovery cannot qualify normal operation*); the one-pass-late allowance is reported
**separately** and never counted as an in-limit emission for the affected interval (spec § *Immediate acceptance is
not immediate audible execution*).

**Content parity** is decided by inventory comparison (committed record/effective vs emitted), not by "no crash" and
not by an overflow-free log. **Named-unproven metrics never block a verdict, and must appear in it as not carried** —
so a pass is never read as covering something it did not measure.

## 5. Named unproven and unsupported (extends `DECLARED_ENVELOPE.md` §7; nothing is shrunk here)

- `queue_residence` — **no limit, no number**: the only instrumented queue is compile-gated off in the default capture
  env, and no other queue has an owner/age probe. Owner choice: enable the family on the qualifying image, or carry
  the metric as unproven with that reason.
- `capture_replenishment` — **never observed** in any capture to date (0 refusals, no completed pair). The instrument
  exists and is exercised natively with injected instants; fault injection may prove the *instrument*, but the spec
  forbids it from qualifying normal operation, and a real refusal fails the run.
- `per_route_output_jitter` — the instrument and its per-traversal fit exist, but what its large readings mean is
  **not established** (they sit in the same order as lateness, which a pure clock-line deviation should not). One
  limit is proposed (L1.6) so the row exists; its interpretation is owed before any verdict rests on it.
- `action_to_audible` — parked by owner decision (2026-10-11); the acceptance must report acceptance / execution /
  audible handoff / publication freshness separately *without* it unless the class-split pair is built first.
- Everything `DECLARED_ENVELOPE.md` §7 already names stays named: roles above 8, whole-take residency, bursts above
  `L` = 2,048, batched external pulses, seconds-scale lost-turn runs, unbounded SD write latency, and the 12,626 KiB
  conservative sum that exceeds the pool by 4,434 KiB.
- **No holder/reserve/competitor budget is proposed here** — §6 of the frame stays owed, and the spec's ban on
  silent concurrency reduction means it must be resolved by declaring, not by shrinking.

## 6. The sign-off questions (what the owner has to decide, each with its arithmetic)

1. **Slow-tempo slack (new, because the cap is fixed):** accept the fixed µs cap as it stands — stricter than the
   music alone requires at slow tempos — or declare an explicit **named allowance** for a stated tempo band? The
   arithmetic: 12 ticks is 12,500 µs at 300 BPM but 187,500 µs at 20 BPM, so the fixed cap is ~15× stricter
   musically at the slow end; the alternative must be *declared*, never discovered per run.
2. L1.1/L1.2 — which musical interval justifies the cap: **12 ticks (1/16 beat) → 12,500 µs (proposed)**, 6 ticks
   (1/32 beat) → 6,250 µs, or 48 ticks (a full 16th note) → 50,000 µs?
3. L1.3 — **±1 tick** on the storage grid, or ±6 ticks (1/32 beat)? (This one stays in ticks: the row counts ticks.)
4. L1.4 — confirm `Δmax` = **64 ticks** as the output deadline line, declared as **66,700 µs**? Note the 64-tick
   anchor is itself still recorded as *not* owner-confirmed.
5. L1.5 — accept the **p50 6,250 µs / p99 66,700 µs** shape, or collapse to the single max line?
6. L1.6 — what does jitter's 6,250 µs limit mean before its interpretation is settled (or hold it unproven)?
7. L1.8 — is **H = 7 turns** the denial bound, and are seconds-scale starvation runs a declared exception?
8. L1.10/L1.11 — accept the **DIN serialization ceiling** (1,041 events/s) as the *route* limit, with the firmware
   judged only by L1.4, and what is the USB route's derivation (unproven today)?
9. L1.9 — **16 events aggregate / ≤ 8 on any one route** at one tick, with the serialization share reported
   separately?
10. §4 oracle — confirm the fail-class list and that `indeterminate` is never rounded into a pass.
11. §5 — `queue_residence` in or out; `capture_replenishment` fault-injection scope; confirm the `action_to_audible`
    parking.
12. **Slow-tempo coverage (recorded decision, needs confirming):** the 300 BPM leg bounds every *deadline-shaped*
    metric and, as an implication, every slower tempo; the **slow leg** is still required for the *contention* class
    (replenishment, persistence/display/load overlap) and for **batched external clock**, which no nominal tempo
    covers (§2b). Confirm that split, and confirm the prerequisite it exposes: the managed runner has no tempo or
    clock-source plumbing today, so a pinned way to set both has to exist before any of this can be run.
13. After 1–12: the `DEC-###` entry that records the envelope, and the owner's sign-off line itself.

## 7. What this increment does not claim

This historical proposal version declares/enforces no current limit and claims no qualifying run or firmware,
test, bound-class, checkbox or delivery change. **S7.5a — continuity envelope, S7.5b — continuity implementation,
and S7.5 — combined acceptance stay unchecked.** The former claim that no numeric limits are recorded is
superseded by the signed **DECLARED-LIMITS.md** record and DEC-117 — declared continuity envelope, not by
retroactively signing this table. L1.12–L1.14 are the explicitly excluded watch list in the current document;
other required unmeasured metrics cannot be excluded merely because their instruments are insufficient.

## Basis update, 2026-10-11: the reachable clock range is now 1–300 BPM

`DEC-108` item 7 was revised on owner decision, together with the loop-edit tempo fader moving from the ch16 CC 102
form (128 steps, one BPM per step, 60–187) to a **14-bit pitchbend on channel 15** spanning **1–300 BPM in 0.1 BPM
steps** (`TempoMapping::bpmFromPitchBendLogical`; 16,384 wire values onto 2,991 positions, 5.5 wire values per step).
`DECLARED_ENVELOPE.md` §2 carries the revised basis.

What that changes here, and what it does not:

* **The fixed µs caps of §2 are unaffected** — 300 BPM is still the fastest reachable tempo, so every cap is still the
  justifying interval read at its tightest point, and §4's oracle still converts nothing at run time.
* **The slow-tempo caveat in §2b gets stronger**: the range now reaches 1 BPM (1 tick = 312.5 ms), so a
  musically-equivalent bound at the slow end would be 3,750 ms for 12 ticks — an even clearer reason why the cap is
  wall-clock and why any slow-tempo slack must be a declared allowance (sign-off question 1).
* **Current coverage caveat:** fastest reachable tempo remains the primary stress leg; slow contention and
  batched-clock coverage remain owed. A looser musical tick at 1 BPM does not prove slower-tempo execution,
  multi-track or wall-clock contention bounds. See the current declared limits §1/§4 rather than the withdrawn
  universal implication or historical sign-off question 12.
