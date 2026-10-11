# S7.5a — declared continuity limits and corrected qualification oracle

**Status: DECLARED numerical record — owner-signed 2026-10-11; representation corrected on owner request.**
The signed numbers and representative active-set choice stand. This correction is not a new universal `PASS`,
threshold change, measurement, instrument implementation or device authorization. **S7.5a — continuity envelope
remains unchecked.** Historical `envdist-t8s4-01` is **NOT QUALIFIED / INCONCLUSIVE** for this envelope (§9), not
a full current-image failure inferred from invalid proxies. DEC-117 — declared continuity envelope records the
original decision; its documentary reconciliation is separate from this three-file correction.
DEC-100 — reported envelope still governs: no admission enforcement is implied.

**Current-image distinction:** the counting-only telemetry bar is now **6,250 µs and FLASHED**, with bounded
boot verification recorded in the newest `PROJECT_STATE.md` entry. The historical captures below used **5,000 µs**.
Neither boot smoke nor changing a component counting bar measures the combined cycle or qualifies the envelope.

## 1. Units, decision provenance and coverage

Use each metric's own unit. The correct conversion is `µs = 60000000 / BPM / 192 × ticks`
(`60000 / BPM / 192 × ticks` gives **milliseconds**). At **300 BPM**, one tick is **1,041.67 µs**.
The fixed caps are an **owner policy**, not a theorem that one 300 BPM run proves all slower tempos,
multi-track workloads or batched external clocks. Slow contention and actual batched-clock advancement need coverage.
No cap or target may be loosened to fit observed spikes. The normal **2,000 µs** target was informed by
**gap-only** musical-window p99 evidence (1.024–1.535 ms), not by measured combined-cycle p99.

§7 retains the discussion excerpts historically. User choices and assistant/advisory read-backs are distinguished;
quoting advisory prose is not user sign-off. The existing signed numerical record stands, and the current request
authorizes honest representation, not fresh acceptance of missing evidence.

## 2. Current declared limits — corrected table

| # | Declared metric / scope | Authoritative measurement needed; existing surface | Unchanged numerical choice | Current measurement status |
|---|---|---|---|---|
| **L1.1/L1.2** | Combined input-service **cycle**, previous entry → next entry; polling quality, **not physical arrival-to-capture latency** | Direct cycle histogram: `C_i = entry_(i+1) − entry_i = D_i + G_(i+1)`. This instrument is **owed**, not implemented here. Separate `env_dist` gap/input histograms are diagnostic only | **p99 ≤ 6,250 µs per nominal five-second window**; normal target **p99 ≤ 2,000 µs**; minimum **100 cycle samples** | **UNMEASURED**; never `p99(gap) + p99(input)` |
| Components | Gap outside service; duration inside service | `DIAG,midi_gap`, `DIAG,midi_input`, component `env_dist` | No independent ceiling; retain diagnostic **100 samples per channel** floor | Diagnostic distributions/maxima/frequencies only |
| **L1.3** | Signed stored-tick error relative to **physical arrival** | Signed, epoch-qualified arrival reference needed. `capstamp_max` records only positive stored tick minus input-service-entry tick; negative differences ignored, arrival not observed | **±1 tick** | **UNMEASURED** arrival error; observed 1 tick is positive in-service drift only |
| **L1.4** | Output lateness deadline, **each physical route** | Physical-route identity and emission endpoint needed. `late_route(type,ch)` is a **logical group**, not DIN/USB routing | **≤ 66,700 µs** (64-tick scheduling anchor) | Physical-route qualification **unmeasured/partial** |
| **L1.5** | Lateness shape, **each physical route** | Route-specific quantiles needed. `env_dist late_on/late_off` are pooled, not per physical route | **p50 ≤ 6,250 µs; p99 ≤ 66,700 µs** | Physical-route qualification **unmeasured/partial** |
| **L1.6** | Jitter, **each physical route** | Per-traversal residual for every physical route needed; current helper selects logical group with most samples, not worst every group | **≤ 6,250 µs** | Physical-route qualification **unmeasured/partial** |
| **L1.7** | Eligible-predecessor deferred-owner service advance | `DIAG,win_gap.eligiblePredecessorMax`, corroborated by `permitted`, `overruns`, `overrunMax`; **not raw `maxDelta`** | **≤ 64 ticks**, compare row's `permitted` | Observed eligible maximum **6**, overruns **0**, overrunMax **0**; in-limit observation with coverage caveat (§9) |
| **L1.8** | Maximum **consecutive unserved opportunities**, per progress-capable holder | Per-holder consecutive wait needed. `deniedTurns` sums a reporting block, not consecutive fairness | **≤ 7 other turns** (`H = NUM_TRACKS − 1`) | **UNMEASURED** intended wait; block-total 0 is diagnostic |
| **L1.9** | Same-tick burst, aggregate and **each physical route** | Complete tick/traversal and physical-route split, including tick zero | **≤ 16 events aggregate / ≤ 8 per physical route** | Partial count **6**; tick zero excluded; logical groups do not supply physical split |
| **L1.10** | Physical-route output demand, DIN reference | Actual wire demand and physical route needed; current output-rate helper chooses most-sampled logical group | **1,041 events/s DIN**, three-byte messages, no running status | DIN **unmeasured/not exercised**; USB 60 events/s is descriptive, not a DIN pass |
| **L1.11** | Physical-route input demand, DIN reference | `MI` physical input/wire-demand measurement needed | **1,041 events/s DIN**, same assumptions | **Unmeasured/not exercised**; USB output does not qualify input |

The **16/8 burst limits are chosen workload limits**, not derived from eight tracks: each track may play chords
and coincident closures. DIN's physical reference is valid: **31,250 baud / 10 bits = 3,125 bytes/s**, or one
three-byte message per **960 µs** (≈1,041 events/s). It is not a USB ceiling or an arbitrary mixed-message ceiling.
Sixteen messages on one DIN route take **15.4 ms**, longer than one tick at 300 BPM; physical serialization share
must be reported separately, without waiving the declared firmware/content/deadline gates.
The one-byte **320 µs** floor does not prove physical arrival accuracy or signed ±1-tick capture correctness.

### 2b. Combined cycle, windows and input-only exceptions

The input criterion is statistical service quality, **not maximum ≤ 6,250 µs** and not a hard latency guarantee.
The round-2 **12,500 µs** provisional ceiling is withdrawn; **6,250 µs** is the pass line and **2,000 µs** is a
normal target, not a separate fail gate. Gap-only evidence cannot establish either combined-cycle result.

* Marginal p99s cannot be added to obtain or bound combined p99: disjoint component tails can make their sum
  cover only 98% of cycles. A direct previous-entry → next-entry histogram is owed; no pair logging or new
  instrument is authorized by this document correction.
* Nominal windows are **five seconds**; actual emission/sample span is variable and must be reported.
  The historical minimum delta was 5.000 s, median 5.001 s; a 15.002 s gap between `late_*` rows alone does
  **not** prove lost windows (there may be no samples). Missing required data remains missing, never presumed good.
* **100 samples minimum** applies to the new cycle population, while the recorded **100 per component channel**
  diagnostic floor remains. Below-floor windows are reported **not scored**. This floor is not a confidence proof;
  an unscored required window cannot be waived into qualification.
* Report input maxima, frequency, workload/window and owning component. An input p99-over-line window stays
  **over-limit** even if explicitly accepted. Isolated input maxima above the diagnostic bar with in-limit p99
  require adjudication but do not turn the p99 criterion into a hard maximum guarantee.
* Acceptance is **input-only**, by a documented authorized owner decision scoped to **image, workload, window,
  owning component and reason/mitigation**. A/B may support attribution; it is not automatic causality, an
  exception acceptance, or a replacement for missing paired-cycle data. No acceptance is invented here.
* `overCount` uses the **6,250 µs counting-only global bar**, now **FLASHED**. Component counts do **not** count
  combined-cycle crossings: **4 ms + 4 ms** exceeds 6.25 ms while neither component does. Other `recordSample`
  families share the bar, not their own declared limits; `late_*` instead counts every positive lateness.
  Historical 5,000 µs counts cannot be recalculated at 6,250 µs without raw samples.

## 3. Current qualification oracle — analytical outcomes, not runtime CLI enums

Inputs must pin image SHA-256, correct environment metadata, registered stimulus/recipe, `invocation.json`,
actual tempo/clock and workload/window coverage. Fixed µs limits are not scaled at runtime.
Per metric report **in-limit / over-limit / indeterminate / unmeasured**, with partial scope and sample floor.
For quantiles, `hi ≤ limit` is in-limit; `lo > limit` is over-limit; a straddling bucket is indeterminate.
Never substitute a component, logical group, aggregate or unexercised route for the declared metric.

* **Plain `PASS`:** all required declared metrics fully measured and in-limit over required coverage, no fail-class
  event, no unadjudicated input outlier. Above-normal-target but in-limit input p99 is not itself a failure.
* **`QUALIFIED_WITH_ACCEPTED_INPUT_EXCEPTIONS`:** all required data measured and decidable, all non-input metrics
  in-limit, no fail-class event, and every input p99-over-line window/outlier explicitly adjudicated as above.
  Accepted p99 violations retain **over-limit** labels; this is **never plain PASS or falsely in-limit**.
* **`INCONCLUSIVE`:** any required metric/data/coverage unmeasured, indeterminate or below floor. Missing evidence
  cannot be accepted away or converted to the qualified outcome. Report independently valid failures alongside it.
* **Failure:** a measured non-waivable breach, unaccepted input violation, or fail-class event prevents qualification.
  An unadjudicated input outlier prevents plain PASS and accepted-exception qualification.

No input exception waives MIDI-content refusal, hard output/deadline limits, deadline fault/latch, a second
consecutive missed traversal, unintended silence, involuntary STOPPED/recovery, capture/ring loss or content-parity
mismatch. Recovery cannot qualify normal operation. One-pass-late is reported separately, never in-limit emission.
Content parity requires committed record/effective vs emitted inventory comparison, not merely no crash.
Only the explicitly excluded §5 watch list is outside scoring; **required-but-unmeasured is not excluded**.
These are documentary analytical outcomes; no CLI enum, host verdict implementation or firmware behavior is inferred.

## 4. Measurement plan and campaign prerequisites

The historical proposal's §3 remains a planning reference, **not authoritative proof that its proxy surfaces measure
the corrected §2 metrics**. The current table specifies the owed endpoints/scope; record bucket resolution,
sample count, actual window span, clock/epoch/correlation and physical-route coverage beside each result.
Qualify maximum-stress **300 BPM**, short/long loops, boundary-dense/chord/closure material, every enabled route,
multi-track capture/finalization/switch/interaction overlap, slow wall-clock contention and batched external clocks.
One tempo or a single-track baseline does not cover this matrix.

Known capability: canonical managed `host_midi_hitl.py run --preset base --clock-source host --tempo-bpm 300 …`
forwards clock/tempo to the baseline. The earlier claim of no tempo/clock plumbing is stale. A current registered
recipe, invocation and actual clock/tempo provenance must still match; capability is not qualification or permission
to run/overwrite/upload. A/B (previous image, same preset) remains an **attribution campaign**, not paired-instrument
replacement or automatic causal proof.

## 5. Explicitly excluded watch list

| Item | Why outside scoring | Retained obligation |
|---|---|---|
| `queue_residence` | No declared number; default capture env gates the known queue family off | Revisit a specific issue or authorized measurement decision |
| `capture_replenishment` | No latency number; no observed completed pair in the cited captures | A real refusal is fail-class; missing observation is not replenishment qualification |
| `action_to_audible` | Parked positional acceptance-to-emission proxy, not identity-paired awaited note | Preserve acceptance/execution/audible handoff/publication distinction |

## 6. Recorded active-set approval, not a fresh capacity guarantee

The owner chose **8 sounding roles + 1 live capture + 1 deferred finalizer + 1 in-flight switch**.
The historical source arithmetic is **5,471 KiB / 8,192 KiB**, margin **2,721 KiB**, **provisional/incomplete**:
interactive `I`, undo protected horizon, canonical/roots/visual/standby/reserve terms and simultaneous device
validation remain owed. The historical settled census **6,505 KB** is not a fresh whole-envelope guarantee.
The companion budget preserves **Cmax = Dmax = Omax = 8** and the conservative **12,626 KiB**, over by **4,434 KiB**;
the **8,177 KiB** C/D/O subtotal leaves no complete budget even before fixed/playback terms. No count was shrunk.
`N = 8` is topology, not a derived capacity bound; historical `N ≈ 13` arithmetic is provisional until phase F;
16/32/48/64 remain targets. K1/K2 between-step budgets cannot bound indivisible SD operations; empirical class
citations and guards do not prove a hard **6,250 µs** cycle bound. K5/K6 remain unbounded, not reservable guarantees.

## 7. Historical discussion excerpts — user decisions versus advisory read-back

Items 1–9 and 11 below preserve the recorded user choices; item 10 is **assistant/advisory discussion**, not
independent user sign-off for every sentence. The read-back paragraphs are interpretation, not new approval.
> 1. Declare the timing limits (round 1): *"the question malforms the table can you show the table before the question?"*
> 2. The over-limit consequence: *"Declare, and make the A/B (previous image, same preset) the next packet"*
> 3. The 1/16-vs-1/32 band (round 1): *"the timing needs to be rebased on 300 BPM as max stressed value and then on 1/32 band"* — **reverted in round 2, item 9 below**
> 4. Capture timestamp error: *"±1 tick (as proposed)"*
> 5. Slow-tempo slack: *"Keep the fixed cap, no allowance (as proposed)"*
> 6. Metrics with no number: *"if we have no number exclude them from the metric but keep the in mind if there is a specific issue on these parameters"*
> 7. The budget's declared claim: *"Declare the representative transient state: 8 sounding + 1 capture + 1 finalizer + 1 switch (5,471 KiB, margin 2,721)"*
> 8. Recording the decision: *"Append the DEC entry recording the declared envelope (recommended)"*
> 9. **Round 2 — the residual question answered, and the band decision reverted:** *"Keep them as is because I want to revert the 1/32 rebase decision. L1.1 and L1.2 on 12.5 ms as the provisional combined ceiling, not two separate 12.5 ms allowances. Aim for 6.25 ms, then qualify it after bounding the long clock/control and persistence/load operations. Don't loosen either ceiling merely to accommodate today's spikes."*

**Read back, so a misreading stays visible — round 3 supersedes round 2 on the levels.** Round 2's combined 12.5 ms
provisional ceiling and its 6.25 ms aim are **replaced** by round 3's three levels (§7 item 10): pass line **p99 ≤ 6,250 µs
per five-second window**, normal-path target **p99 ≤ 2,000 µs**, exceptions retained with maximum, frequency and cause.
Rounds 1 and 2 still stand on everything else they decided: the criterion is **one combined displacement** and never two
separate allowances; **no ceiling may be moved to accommodate an observed spike**; every µs cap is fixed, so a slow tempo
is not excused a larger stall; and the three no-number metrics stay out of scoring (§5). The anchor-derived rows (L1.3,
L1.4, L1.7, L1.8, L1.9, L1.10/L1.11) and L1.5's p50 / L1.6 keep the values their own justifications give them.
> 10. **Round 3 — assistant/advisory discussion of levels (historical excerpts, not verbatim user approval):** *"I'll look at the distribution across the recent
>     service windows, rather than let the single maximum define the budget. I'll also count how often the slower gaps
>     occur, so we can distinguish a normal operating range from an explicit exception policy."* … *"a 2 ms p99
>     service-gap target per five-second window is supported by the recent captures, while maxima remain diagnostic."* …
>     *"One correction: there were 38 gaps above 5 ms in 49,223 polls — 0.077%, but about 2.5 occurrences/second. They are
>     rare by poll count, not just one or two isolated events."* … *"For the normal-operation window you mean, 6.25 ms is
>     defensible and already has substantial headroom over the observed p99. I would not move to 12.5 ms just because of
>     those tails."* … *"Then I'd choose 6.25 ms as the normal service window — not 12.5 ms."* … *"Normal-path target:
>     p99 gap ≤ 2 ms. Generous operating window: 6.25 ms. Exceptions: retain maximum, frequency and cause separately;
>     investigate or explicitly accept them."* … *"If exceptions are allowed, define the criterion as p99 per five-second
>     window, rather than 'maximum ≤ 6.25 ms' and then ignore violations. This is a service-quality target, not a
>     worst-case latency guarantee."* … *"No firmware or authoritative limit will change."*
> 11. **The sample floor and the adjudication subject (verbatim):** *"100 samples per channel"*, and *"Attribute and
>     adjudicate them from the A/B result"*.
>
> **Read-back note, so a misreading stays visible:** in *"named by owner"*, "owner" is read as the **architectural owning
> component** (the owner that executes the work), not a person, and the mitigate-or-accept decision is the envelope's
> adjudication of that exception. If that is a misreading, this is the rule to correct — it is the one place where a
> decision was interpreted rather than quoted.

## 8. What remains owed before S7.5a — continuity envelope is earned

* Existing numerical sign-off and DEC-117 — declared continuity envelope record stand; no new signature is invented.
* Direct combined-cycle histogram and sufficient required windows; signed physical-arrival error; physical-route
  max/quantiles/jitter; per-holder consecutive progress-capable wait; complete burst including tick zero;
  physical DIN output and MI input demand remain unmeasured/partial.
* Pinned registered qualifying recipe/image/clock/tempo and complete workload matrix, simultaneous active-set
  validation and resource terms, content/deadline/latch evidence remain owed. No checkbox is closed here.
* Input outlier attribution/adjudication remains owed, scoped as §2b/§3. The withdrawn proxy sums below are not
  measured p99 exceptions. A/B needs separate session/device authorization and cannot replace missing metrics.
* Frequency reporting must name its **cohort, metric, actual span and bar**; the historical cohorts in §9 are
  already distinct, not an unresolved common denominator. New combined-cycle frequency is unmeasured.

## 9. Historical observations — corrected classifications, no full qualification

Source: `captures/envdist-t8s4-01/base_20261010_213036_serial.log`, historical `888d2188` image line,
**not the latest flashed image**. The previously reported envelope score is withdrawn where it substituted proxies.

| Declared row | Retained observation | Correct classification |
|---|---|---|
| L1.1/L1.2 combined cycle | Separate gap/input histograms, 15 reporting windows; first has 2/3 component samples | **UNMEASURED** combined p99; no 7/4/3 combined classification |
| Components only | Gap p99 0.26–26.4 ms; input-duration p99 upper interval ≤ 0.767 ms in the cited summaries | Diagnostic only, neither independently ceilinged nor proof of combined target |
| Input maxima | Gap maxima **375,509 / 340,843 / 310,198 / 198,102 / 179,176 / 139,188 / 101,159 µs**; input peak **377,951 µs** | Retained outlier diagnostics with attribution owed, not hard-max gate or measured combined p99 |
| L1.3 arrival accuracy | Positive in-service `capstamp_max` **1 tick** | Arrival error **UNMEASURED**; no signed/physical in-limit claim |
| L1.4/L1.5 physical-route lateness | Logical lateness **241,547 µs**; pooled p50/p99 interval **[131,072, 262,143] µs** | Diagnostics above **66,700 / 6,250 µs** reference lines; physical-route qualification **unmeasured/partial** |
| L1.6 physical-route jitter | Selected logical-group residual **249,145 µs** | Diagnostic above **6,250 µs**, not full worst-every-physical-route qualification |
| L1.7 eligible advance | Eligible-predecessor max **6 ticks**, permitted **64**, overruns **0**, overrunMax **0** | **Observed in-limit eligible metric**, coverage caveat; raw 158 is not an overrun |
| L1.8 consecutive wait | Reporting-block `deniedTurns` **0** | Diagnostic; consecutive fairness **UNMEASURED** |
| L1.9 burst | Partial aggregate **6 events**, tick zero excluded, logical grouping | No full **16 aggregate / 8 physical-route** in-limit claim |
| L1.10/L1.11 demand | USB output selected group **60 events/s** | DIN/input **unmeasured/not exercised**; not in-limit against DIN reference |

**L1.7 — eligible-advance evidence:** 32 `win_gap` rows; the raw maximum **158 ticks** at **73.835327 s** has
`transport=0, eligible=0, progress=0, eligiblePredecessorMax=0, permitted=64, overruns=0`.
Across the capture eligible maximum is **6**, with **0 overruns**. The **24 transport-active rows** total
**157 eligible turns / 23 progress turns / 0 denials**. This is useful observed in-limit scheduling evidence,
not proof of full transport/demand/readiness/content or multi-track matrix qualification.

**Withdrawn proxy history:** the old **7 normal / 4 operating / 3 exception** classification and sums
**6,654 / 14,078 / 25,086 µs** at approximately **21.0 / 76.1 / 81.1 s** were sums of marginal p99s.
They are **not valid combined-cycle classifications or measured exception windows**; no attribution or acceptance
can repair that missing measurement.

### Frequency provenance — different cohorts, not unreconciled denominators

| Capture / image / selection | Metric and historical bar | Recorded frequency |
|---|---|---|
| `live-display-verification-t8s4-03`, image **6f176b90**, **3 musical windows** | Gap above **5,000 µs** | **38 / 49,223 polls ≈ 0.0772%, ≈ 2.53/s** |
| `envdist-t8s4-01`, older image, full **15 windows**, nominal ≈75 s coverage | Gap `overCount`, **5,000 µs** | **557 / 168,116 ≈ 0.3313%, ≈ 7.43/s** (nominal-span approximation) |
| Same older cohort | Input duration `overCount`, **5,000 µs** | **5 / 168,117 ≈ 0.00297%** |
| Same older cohort, **two metric populations combined** | Gap counts + duration counts | **562 / 336,233 ≈ 0.1671%**; **not poll/cycle frequency** |

These counts describe different images/window selections/metric populations. Both historical images used **5,000 µs**;
the latest **6,250 µs bar is FLASHED**. The counts cannot be re-derived at the new bar from recorded aggregates,
and even new component `overCount` cannot count combined-cycle crossings.

**Outcome:** this historical run is **NOT QUALIFIED / INCONCLUSIVE** under the current declared oracle because
required metrics are unmeasured or partial. Large valid diagnostics stay visible; the invalid input sums and
ineligible raw gap do not establish a full **current-image FAIL**. A/B remains an attribution campaign only.
No new test, measurement, build, upload, device, SD or Git evidence is claimed by this correction.
