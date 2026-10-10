# S7.5a — the declared limits, the per-row measurement plan and the verdict oracle (increment 5: a **proposal**)

**Status: proposal for owner sign-off. Nothing in this document is declared, approved or exercised.** It answers
items 3–5 of [`DECLARED_ENVELOPE.md`](DECLARED_ENVELOPE.md) §8 (a measurement plan per row, then the oracle row) by
turning the §2 tempo basis and Table A anchors into **candidate numbers**, and it names what cannot carry one.
**No value here is owner-approved**, no qualifying run was made, no firmware/test/bound-class/checkbox changed, and
**S7.5a stays unchecked**. `DEC-100` still governs: the envelope is reported, never enforced at admission.

## 1. How a limit is expressed here, and the rule that produced it

Every limit below is a **musical interval in ticks**, converted to µs only at the tempo the qualifying run actually
advanced (never a nominal tempo, per §2's external-clock caveat):

`µs = 60000 / BPM / 192 × ticks` — 1 tick = 1,041.7 µs @300 BPM, 2,604 µs @120, 15,625 µs @20.

This shape is deliberate: `DEC-108` §13(b) forbids an invented microsecond budget, and the spec forbids fitting a
limit to an observed run. A tick-denominated limit is *derived from the musical grid* and is tempo-correct by
construction; the run supplies the tempo, not the number. **The readings already collected in increments 3–4 are
evidence that an instrument is fed — they are not, and must not become, the source of any limit in §3.**

`Δmax` = the per-lane-turn permitted phase advance (Table A, 64 ticks) is the one anchor a limit is inherited from,
and it is inherited **by citation**, with its own owed owner confirmation still attached to it.

## 2. L1 — the proposed limits (all **proposed**, none approved)

| # | Metric | Row / field (measurement point) | Unit | Proposed limit | Musical justification | @300 | @120 | @20 |
|---|---|---|---|---|---|---|---|---|
| L1.1 | `max_midi_input_service_gap` | `DIAG,midi_gap,<maxUs>` (+ `env_dist` p50/p99) | µs | **≤ 12 ticks** = 1/16 beat | An event is stamped from `getCurrentTick()` *at process time*, so a gap displaces it by the gap's worth of ticks; the band's own upper edge is the largest displacement still inside one notated subdivision | 12.5 ms | 31.25 ms | 187.5 ms |
| L1.2 | `max_midi_input_service_duration` | `DIAG,midi_input,<maxUs>` (+ `env_dist`) | µs | **≤ 12 ticks**, and `gap + duration` also ≤ 12 ticks | Same displacement argument: it is the *sum* of the two that delays the next arrival, so the oracle checks both lines | 12.5 ms | 31.25 ms | 187.5 ms |
| L1.3 | `capture_timestamp_error` | `DIAG,envelope_instr,capstamp_max` (+ `capstamp_n`) | ticks | **≤ 1 tick** | The storage grid *is* the 192nd note (PPQN 192); an event stored on a neighbouring tick is a grid error. Physically admissible: one DIN byte is 320 µs against a 1,041.7 µs tick at the fastest reachable tempo, so the transport floor does not force a looser bound | 1.04 ms | 2.60 ms | 15.6 ms |
| L1.4 | `per_route_output_lateness` — deadline line | `DIAG,late_route,<type>,<ch>,<maxUs>` | µs | **≤ 64 ticks** (= `Δmax`, cited from Table A) | An emission may consume the lane's whole permitted advance for one opportunity, and no more: beyond it the lane is outside the envelope the sibling already declared | 66.7 ms | 166.7 ms | 1000 ms |
| L1.5 | `per_route_output_lateness` — typical line | `DIAG,env_dist,late_on|late_off` | µs | **p50 ≤ 6 ticks** (1/32 beat) and **p99 ≤ 64 ticks** | The *median* emission should land inside the finest subdivision the band names, while a rare emission may consume the whole permitted advance; the p99 line is L1.4 restated so the shape is checked, not only the worst sample | 6.25 / 66.7 ms | 15.6 / 166.7 ms | 93.75 / 1000 ms |
| L1.6 | `per_route_output_jitter` | fitted-per-traversal residual | µs | **≤ 6 ticks** | Scatter around the route's own clock line should not exceed the finest subdivision either; this is the line that separates "late" from "irregular" | 6.25 ms | 15.6 ms | 93.75 ms |
| L1.7 | `deferred_owner_service_advance` | `DIAG,win_gap.maxDelta` vs `permitted` | ticks | **≤ 64 ticks** (the `permitted` field's own declared value, cited) | Table A anchor; the row already carries the declared term, so the oracle compares against the field, not against a repeated constant | 66.7 ms | 166.7 ms | 1000 ms |
| L1.8 | `deferred_owner_denial` | `DIAG,win_gap.deniedTurns` (+ `eligibleTurns`) | turns | **≤ 7** (= `H`) per eligible window | The across-track rotation is `NUM_TRACKS - 1`: an eligible lane is revisited within H turns, so H is the largest denial a rotation can produce without starvation | — | — | — |
| L1.9 | `same_tick_burst` | `MO` tick column, per traversal — reported per route **and** aggregated | events | **≤ 2 × roles at one tick**: ≤ 16 aggregate across the 8 supported roles (on + off each), and ≤ 8 on any single route (a route carries at most one note event per sounding role, so which figure binds depends on the channel mapping) | Derived from the *supported* topology (8 tracks × one sounding slot, Table A role envelope), not from a run. Bursts above `L` = 2,048 admitted events stay unsupported (§5); the serialization share is reported separately — a DIN route cannot carry 16 × 960 µs inside a 1.04 ms tick | — | — | — |
| L1.10 | `route_throughput` (physical route ceiling) | `MO` rows per route | events/s | **≤ 1,041** on DIN (3-byte messages, no running status) | Transport capacity, accounted **separately** from firmware lateness as the spec requires: 31,250 baud, 10 bits/byte ⇒ 3,125 B/s ⇒ a 3-byte note message every 960 µs. The firmware requirement under it is L1.4, not a throughput figure | — | — | — |
| L1.11 | `route_throughput` (input, same ceiling) | `MI` rows per route | events/s | **≤ 1,041** on DIN | Symmetric with L1.10; the firmware requirement under it is L1.1/L1.2 | — | — | — |
| L1.12 | `capture_replenishment` | `DIAG,envelope_instr,repl_wait_us` / `repl_pairs` | µs | **no limit declared** — see §5: never observed, and a refusal fails the run outright | The metric's own role is diagnostic: a refusal is a fail-class event, so a "latency limit" would only describe how the failure looked | — | — | — |
| L1.13 | `queue_residence` | `DIAG,droid_queue` | µs | **no limit declared** — see §5 (env-gated family; no owner/age probe for any other queue) | — | — | — |
| L1.14 | `action_to_audible` | parked | µs | **kept out** (owner decision 2026-10-11) | Two questions (which operator action, which emission class) under one position-based rule; see the increment-4 packet's decision section | — | — | — |

Two consequences worth stating plainly, because they are properties of the proposed numbers rather than of any run:

* **L1.3 is tight and admissible.** 1 tick at 300 BPM is 1.04 ms, and the DIN byte floor is 0.32 ms, so "store an
  event on the tick it arrived on" is not physically contradicted at any reachable tempo.
* **A 16-event same-tick burst cannot be serialized inside one tick on a DIN route** (16 × 960 µs = 15.4 ms against a
  1.04 ms tick at 300 BPM). Under the spec's separation rule that is *not* a firmware lateness failure; it is the
  route's physical share, and the honest report is "serialized over N ticks", named as such.

## 3. L2 — the measurement plan, one row per metric (covers `DECLARED_ENVELOPE.md` §8 item 3)

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
is not accepted), the registered stimulus ID, `invocation.json`, and the run's **actual clock advancement** (the
device's own tempo/clock rows) for the tick→µs conversion of §3.

**Per metric**, read its row, convert the limit at the run's tempo, and return one of:

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

1. L1.1/L1.2 band: **12 ticks (1/16 beat, proposed)** or 6 ticks (1/32 beat) or 48 ticks (a full 16th *note*)?
2. L1.3: is **±1 tick** on the storage grid the criterion, or ±1/32 beat (6 ticks)?
3. L1.4: confirm **64 ticks** (`Δmax`) as the output deadline line — noting the 64-tick anchor is itself still
   recorded as *not* owner-confirmed.
4. L1.5: accept the **p50 = 6 ticks, p99 = 64 ticks** shape, or collapse to the single max line?
5. L1.6: what does jitter's limit mean before its interpretation is settled (or hold it unproven)?
6. L1.8: is **H = 7 turns** the denial bound, and are seconds-scale starvation runs a declared exception?
7. L1.10/L1.11: accept the **DIN serialization ceiling** as the *route* limit (firmware judged only by L1.4), and what
   is the USB route's derivation (unproven today)?
8. L1.9: **16 events per route per tick** as the declared burst, with the serialization share reported separately?
9. §4 oracle: confirm the fail-class list and that `indeterminate` is never rounded into a pass.
10. §5: `queue_residence` in or out; `capture_replenishment` fault-injection scope; confirm the `action_to_audible`
    parking.
11. After 1–10: the `DEC-###` entry that records the envelope, and the owner's sign-off line itself.

## 7. What this increment does not claim

No limit is declared, approved or enforced; no qualifying run; no firmware, test, bound-class, task-checkbox or
delivery change; **S7.5a, S7.5b and S7.5 stay unchecked**, and the sibling's statement — *"S7.5a is open: it has no
numeric limits recorded"* — remains true until the owner signs §2. A `DEC-###` entry is deliberately **not**
appended: the numbers it would carry are proposals. Three of the twelve metrics have no limit at all in this
proposal (L1.12–L1.14), and that is a statement about the instruments, not a reduction of the required envelope.
