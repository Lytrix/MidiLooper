# I5 — transport-preserving dispatch and bounded owner checkpoints

**Status: concrete timing-contract draft, developed with user approval; NOT production implementation approval or measured service qualification.** User approved developing the direction in [the timing reassessment](OWNER-SERVICE-REASSESSMENT.md). This document fixes candidate ownership, continuation layout, ordering, fairness and conditional sufficiency. Physical timing, source-progress implementation and linked production fixtures remain explicit gates. No firmware, clock delivery, batch-cap, SD, size-pin or parent checkbox changes.

## 1. Current facts and chosen boundaries

The existing USB-device/DIN batch protocol promotes Start/Stop/Continue, preserving their order, then dispatches Clock/channel messages in FIFO. USB-host callbacks currently dispatch FIFO immediately; do not silently add transport promotion there. Existing cap is128 messages, or32 in post-overdub drain. The original helper-based batch counterexample is Off736/fourClocks→768 without a main opportunity. The established late-input oracle independently requires Off740→adoption768, lead28.

The proposed checkpoint work stays in main and existing deferred owners, never in Capture append or Clock callback. No new scheduling Manager or independent content publisher. Across-track playback selection remains actual round-robin; within-track selection stays earliest owing slot. Current, ready, source currency, closure and stop-handoff rules remain unchanged.

## 2. MidiHandler-owned continuation

**One fixed-capacity continuation buffer**, shared serially across the existing route cycle USB-device→DIN→USB-host. No three permanent route queues or unbounded global event backlog. Freeze the acquired route/batch cap until that batch drains; changing post-overdub mode cannot shrink an already accepted batch. Another poll entry resumes this same batch rather than acquiring another one over it.

Rows are copied by value: type/channel/data1/data2 (four8bit fields), plus32bit per-row **read timestamp**. The timestamp measures residence since read, not actual wire arrival, and must not be used to fabricate signed physical arrival error. Route and source eligibility are retained in header state; payload rows do not borrow library buffers. Owned lifetime lasts through exact-once dispatch and terminal cleanup. Prewarm the buffer before active input; never allocate/grow it on input/clock/stop edges. Missing prewarm must not silently lose messages or activate the new protocol.

### Route ordering

- USB-device and DIN use two bounded scans of the frozen original array: transport-only pass then nontransport pass. Each scan segment inspects at most16 row types and dispatches at most one complete message. This proposed scalar quantum is a candidate design value, not a firmware constant change or throughput approval. It replaces a persistent planned-index array; the full batch's transport grouping is unchanged.
- USB-host retains a single FIFO scan. Callbacks become capture-only while the owning read is acquiring the bounded batch; normal routing is resumed by MidiHandler after acquisition. Verify the library's callback timing, Task()/read behavior and source lifetime before implementing this adapter. It must not route messages twice or silently swallow callbacks outside the designated acquisition step.
- Completing a route batch advances to the next route, not back to USB on every main poll. Per-cycle route progress and each route's backlog/residence need a bound; a continuously busy first route must not indefinitely suppress later routes.
- An empty transport-scan segment may emit no message but still consumes charged work. Inspecting128 types can take eight such segments; account for them. No whole-buffer rescanning on every resume and no hidden dispatch loop behind an ostensibly one-message API.

Acquisition still needs its own cost bound. Retaining the existing at-most128 read cap is not proof that USBHost Task(), parser/library reads, telemetry or routing callbacks are constant-time. Structural caps or empirical guards/failure policy must cover those indivisible children before timing admission.

### Epoch/transport/source changes

Consumed indexes only advance after the actual handler executes exactly once. Already-read messages remain retained across MIDI Start/Stop/Continue and clock epoch changes: invalidating a preparation promise is not permission to drop pending input. Later messages observe the correct post-transport state in the same route order. The source/operation generation protects stale cleanup and recycled storage; reset cannot let old cleanup clear a newer batch.

Read data that outlives device disconnect must be handled under the established routing/source-eligibility policy, not arbitrarily relabelled. Source retirement, shutdown and release need explicit exact-once outcomes; no unreported payload discard. Main activities formerly after a full poll must not interleave user actions with an unfinished batch unless their semantic equivalence is proved. Preparation checkpoints are the permitted interleaving, with source/request generation checks.

## 3. Main-owned checkpoint protocol

A checkpoint dispatches **one named deferred grain**, then returns to the loop/input continuation boundary. No builder loop, whole input drain, IRQ hold or synchronous wait. Source preparation and Track window preparation remain distinct owners and costs.

Retain the existing ordinary deferred-owner cursor. If there is a generation-qualified, progress-capable urgent window obligation, the next ordinary owner is not Window, and no normal-owner debt is outstanding, insert one Window opportunity **without consuming the saved ordinary turn**. The saved ordinary owner becomes mandatory next; no second inserted Window turn may overtake it. Serving/denying that ordinary turn advances its existing rotation exactly once. Existing selected-turn refusal policies remain explicit; a resource refusal is not successful progress.

The rule is obligation-based, not RECORDING/OVERDUBBING-wide priority. If there is no valid request, no extra turn is inserted. All playback turns still use the same round-robin track cursor; a blocked track does not monopolise the opportunity. The same-track grain/coverage accounting remains required.

This bounds urgency interference to at most one extra checkpoint per selected ordinary turn. Existing Snapshot backlog/session-exit extra turns remain in the ordinary selector; do not replace their semantics. At most14 selected ordinary turns cover one7-owner base cycle under its extra-Snapshot cases; the insertion rule makes at most28 checkpoint opportunities per such cycle. This is a selection-count bound only—individual owner refusal, child cost, route work and per-owner track rotation must still be priced.

One checkpoint per dispatched message is **not enough** for the existing late trace: eight tracks may need multiple round-robin cycles. Main may execute additional separately bounded checkpoint turns between input segments, but only up to the earliest independent input/clock dispatch deadline and an explicitly derived grant cap. This is not permission to delay Clocks until ready. Grant caps must come from remaining work and physical timing proof; no production cap is adopted here.

## 4. Physical-time safety and fail behavior

Readiness cannot be scored solely against the tick counter whose next Clock is retained in the continuation. Maintain an independent physical-time reference for the eligible adoption boundary and the next due Clock/audible edge. A queued batch may already be late when read; its historical physical deadline cannot move forward just because dispatch was segmented. Host/device correlation uncertainty must be included, not subtracted away.

Before an extra checkpoint, determine whether its enforced indivisible cost bound plus validation/return overhead fits the remaining physical input/clock/adoption budget. If it does not, resume input promptly, report the owner/source/adoption miss and use the existing containment policy. Do not wait indefinitely, suppress a Clock, reorder it ahead/behind channel messages, grant a new traversal allowance or label old-version output a readiness pass.

The declared6,250µs input-service **p99** allowance is not a hard maximum checkpoint or Clock-hold budget. Output jitter/lateness have their own physical endpoints and metrics. All required input, output, clock and timestamp criteria remain in force; no new timing limit or exception is invented in this draft. Wire-arrival and Clock-reference instrumentation/uncertainty are not established by per-row read timestamps.

Internal timer playback still advances asynchronously. Do not stop the timer, mask IRQs to freeze the deadline, or transfer preparation into the tick ISR. Include interrupt time and revalidate source/epoch/geometry at publication. External dispatch segmentation alone does not prove internal-clock readiness.

## 5. Concrete candidate resource price

Compiler-measured standalone candidate record (both ARM Cortex-M7 hard-float target and native ABI):

```cpp
struct MidiDispatchContinuationPrice {
  uint8_t rows[128][4];
  uint32_t readAtUs[128];
  uint64_t operationGeneration;
  uint64_t checkpointDeadlineTick;
  uint32_t clockEpoch;
  uint32_t acquiredAtUs;
  uint16_t count;
  uint16_t transportScan;
  uint16_t channelScan;
  uint8_t route;
  uint8_t phase;
  uint8_t frozenCap;
  uint8_t nextRoute;
  bool callbacksCapturing;
  bool active;
};
struct MainOwnerCheckpointPrice {
  uint64_t operationGeneration;
  uint32_t ownerDebtSequence;
  uint8_t owedNormalOwner;
  bool normalOwnerMandatory;
  bool urgentTurnUsed;
};
```

Continuation **1064B**, checkpoint **16B**, alignment8. Total **1080B payload** for one global input role, not per recorded slot or per route. Preferred placement is preallocated external-role storage owned by existing MidiHandler/main, without adding1064B to a DTCM-resident singleton. Pointer/allocator/global/constructor/link cost and physical reference state are not included or approved; actual owner embedding must be measured. A complete physical-time receipt may require additional fields, so1080 is the exact listed candidate, not the final implementation footprint.

Combined with prior candidate source progress (64Btarget runtime extension +8B Loop counter),64 Loop shells and8 runtimes give1024B source-state payload +1080B continuation/checkpoint payload = **2104B**, before other state/allocation/link effects. Existing size pins, three-view arrays, default rollingOFF and SD/undo representations remain unchanged.

Scratch `i5-dispatch-price.cpp` emits sizeof symbols; ARM nm/native printer both confirm1064/16/8. Logs `i5-dispatch-price-arm-sizes.txt`, `i5-dispatch-price-native-sizes.txt`. No firmware object/layout/pin was modified.

## 6. Conditional composed sufficiency

Let H be eligible playback track count, G remaining window grains plus same-track interference, B_source the independently bounded prerequisite source work, C_checkpoint the complete enforced physical-time cost of one checkpoint including its input-segment/validation/interrupt overhead, B_other any work outside those checkpoints, and M the justified boundary margin.

During continuously valid urgent work, the mandatory-normal rule yields a conservative selection bound of **2*H*G checkpoint turns** for G Window grants to a chosen track. Thus a necessary conservative serial sufficiency condition is:

`B_source + 2*H*G*C_checkpoint + B_other + M <= min(adoption_physical_remaining, independent_input_clock_remaining)`

Also require the absolute qualified epoch/tick adoption deadline, exact content completeness and all other admitted source/owner obligations. For source-owner prerequisites, charge their actual owner-cycle/track rotation and grant counts separately; at-most28checkpoint selection-cycle bound is not their WCET. Do not multiply the outer cycle again into C_checkpoint if already included there. Shared/simultaneous obligations must each pass with mutual interference, not just the most urgent track.

For the existing28tick lead at300BPM, nominal physical span is29,166.666667µs. For H8 and G1/2/3/4/5, checkpoint counts are16/32/48/64/80; optimistic C_checkpoint ceilings before all other costs are1822.916667/911.458333/607.638889/**455.729167**/364.583333µs. These are **derived feasibility conditions**, not approved budgets, measured WCET or guarantees. Independent clock/input limits may be tighter. G is not assumed4 for every workload; accepted density/repeated deltas and source preparation must supply its actual bound.

Current classified Snapshot/persistence/display/load children may not fit such small checkpoints. A nominal time budget checked after an indivisible child is insufficient. Split/reclassify the owning child or refuse the readiness claim under the unchanged envelope; do not silently exclude an admitted competitor to pass this arithmetic.

No finite universal zero-miss guarantee follows for arbitrarily late accepted input. Report actual insufficient lead as the prescribed miss; a supported readiness claim needs its workload/arrival envelope and independent endpoint evidence. Capture acceptance remains separate from build success, and containment is not normal readiness qualification.

## 7. Host design checks and limits

Scratch `i5-dispatch-contract-model.cpp` is an explicit design model, **not production MidiHandler continuation**. It invokes real MidiDispatchOrder and owner/track selectors; proposed segmented scans/debt sequencing are model code only. Verified:

- 19,531 input type traces of length0..6 agree with the current production USB/DIN helper and preserve each original ordinal exactly once;
- explicit Note-before-Start/Continue mixed-clock expected order `[2,4,0,1,3,5]`, versus unchanged USB-host FIFO `[0,1,2,3,4,5]`;
- full-capacity17/32/128 row traces preserve ordering/FIFO;
- backlog and session-exit combinations each serve every ordinary owner while at most doubling selected-turn opportunities;
- 32 real round-robin selections rotate eight progress-capable tracks;
- four-grain/eight-track selection calculation gives64 checkpoints;
- already-late physical example remains rejected, not converted to readiness by virtual tick delay. The example uses illustrative physical numbers only, not accepted limits.

Model exits0; log `i5-dispatch-contract-model-final.log`. Fresh focused command `pio test -e native -f test_midi_dispatch_order -f test_midi_service_drain -f test_runtime_working_set_ownership -f test_playback_prewarm -f test_loop_take_survival`: **599/599 succeeded**, five PASSED suites, exit0. Log `i5-timing-contract-native.log`. No fresh full-native/build/link/device result is claimed.

## 8. Execution and device boundary

The structural contract and listed candidate price are now explicit. **Not yet implementation-ready as a complete timing qualification:** real continuation acquisition/resume/callback/lifetime fixtures, source-prefix/delta implementation and mutation hooks, physical receipt/arrival endpoints and enforced indivisible child costs are missing. User approval was to develop this contract, not to adopt its candidate quanta/new state or bypass those dependencies.

Next bounded engineering slice should be fixture-first, qualification-gated production continuation semantics and source/window interleavings, explicitly scoped as unqualified timing until bounds are measured. Freeze source/route/generation/consumption outcomes before code; capture exact target/host embedding and RAM1 headroom. Do not make full deadline PASS a prerequisite for a measurement-only fixture, or call a semantic slice hardware-qualified.

HITL becomes required to determine checkpoint/child duration, real ingress/Clock residence and physical correlation, timer interference, output jitter/lateness, resource behavior and contention after a production-path measurement slice exists. Neither the host model nor current helper probes can supply those observations. No HITL or upload is performed here; default rollingOFF/legacy fallback stay until the actual gates pass.

No new DEC is adopted by this draft; timing-model/continuation implementation needs the completed reassessment decision and separately pinned execution scope. No architecture conflict is papered over, no budget is tightened to fit measured spikes, and all parent checkboxes remain open.

## Documentary verification

Strict OpenSpec validation valid; local links resolve; both quoted state records match the compiled scratch layouts exactly; diff hygiene clean; checkboxes unchanged29/60. Design/task/runtime/handoff references updated. No new physical budget, implementation gate pass, DEC adoption, device result or source/firmware action is implied.

**Fixture-first production-path follow-up:** [I5 — real input/clock execution evidence](REAL-INPUT-EXECUTION.md) now links actual polling/ClockManager/TrackManager/Capture/reader with native hardware boundaries. Ordinary10/10/fullnative3110succeeded7skip/115suites; three separately gated Off-consumption return cases fail736vs768 onUSB/DIN/host after exact accepted-content/current/ledger invariants. Onefocusedreview2fixturedefects repaired/retested and controls discriminate. This closes the input/clock linking prerequisite only, not continuation semantics, source-progress, physicalreference/WCET or timing qualification. Firmware/policies/default gate unchanged.
