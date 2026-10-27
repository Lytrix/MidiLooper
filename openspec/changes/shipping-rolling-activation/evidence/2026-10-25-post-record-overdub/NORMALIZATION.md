# Record normalization latency investigation — 2026-10-25

## Actual device observation

Bounded streaming parse of `captures/session_20261025_112515.log` (56,793lines), not a content rewrite or new device run:

- Record stop723.150s, accepted stop entry1,033rows, final32bars/24,576ticks; Record publication731.378s, about8.228s later.
- Source staging first grain723.165469s, terminal end723.449541s,33calls.
- Normalization outer phase2 arrival723.451145s, terminal end731.356592s: **7.905447s and1,411calls**, about96% of stop-to-publication delay.
- Transfer phase13:2calls, max57us. Publication phase14:1call,11.086ms.
-49 sampled normalization grains:61/117/121.3/273us min/median/mean/max. Aggregate phase maximum273us. Thus sum of all1,411 timed grain bodies is at most385.203ms, conditional on the inspected telemetry semantics; this is an upper bound, not measured CPU total.
- Span per offered grain averages5.60ms. Multiple calls occur per~20.8ms service period. Exact unsampled service gaps are unavailable.

**Telemetry correction:** `stop_phase ...1411,1357,0` means calls/deadline-slack-crossings/unknown-slack-calls, NOT progress. `RuntimeTimingTelemetry::recordStopGrain` increments crossed when duration exceeds slack. Aggregate start/end identify the maximum-duration grain, not the whole phase. Sampling suppresses intermediate rows after call768 (stride16→4096), so a3.088s interval between logged rows is NOT proof of a3s service gap. This phase completes; it is not permanently stuck.

## Controllable production-cursor measurement

Scratch harness includes actual production store/cursor through existing `test_record_preparation` include fixture, quantum32, fixed reserve and per-step unit counters. No algorithm copy or firmware change.

Deterministic recipe:516pairs+oneCC=1,033rows,32*768ticks; pair i has On47*i, Off24ticks later, channel1, pitch60+i%12, NoteId i+1; CC at24575. Open-tail close200, wrapWindow768, minLength12 when enabled. Ordered/reversed/rotated-by517 variants. Every variant completes with1,033rows, zero synthetic Offs, zero removed pairs.

- Ordered/filter off:1,218calls/38,966units.
- Ordered/filter on:1,380calls/44,128units.
- Reverse/filter on:2,232calls/71,392units.
- Rotated/filter on:1,513calls/48,400units.

Ordered/filter-on entry-phase attribution: Order481calls15,392units; AssignIds64/2,048; RepairReset64/2,048; RepairPreflight33/1,056; RepairRead32/1,024; RepairOutput64/2,048; Reorder480/15,360; FilterReset64/2,048; FilterPreflight33/1,056; FilterRead32/1,024; FilterCompact32/1,024; completion-return1/0. Eleven calls cross phase boundaries: attribution is at call entry, not exact isolated subphase cost. Totals are exact for this recipe.

Standalone production canonical order over the already-sorted1,033-row output costs481calls/15,362units. Two order passes dominate~70% of this recipe's units. The1,380call recipe is close to device1,411calls, but is NOT a reconstruction of captured rows/policy. Host runtime in hundreds of microseconds is NOT Teensy/PSRAM timing.

## Cause and recommendation

The delay combines many bounded normalization units with intervals between service opportunities, not eight seconds of continuous sort CPU. Quantum32 bounds one child step, not32rows end-to-end. Besides ordering, it performs private preflight scans, identity completion,16*128 lane resets/scans, tail repair and optional minimum-length filtering/compaction.

Concrete candidate for later optimization: skip the unconditional second canonical order when tail repair appends zero synthetic rows (`RecordPreparationCursor::step`, RepairOutput→Reorder). This could eliminate~15,362units/~35% in the ordered/filter-on recipe. It needs independent zero/synthetic/equal-tick/pressure/stale/differential regressions before implementation; no proportional wall-time guarantee follows. Broader shared-sort/scheduler/budget changes are NOT included in this task. Record quanta, work credits, fairness and source lifetimes are unchanged.

The implemented handoff accepts one overdub press and retains it through publication/admission wait. It does **not** claim immediate MIDI ingress or eliminate this measured delay. Timing must be remeasured on device after any later work reduction.

## Verification / reproducibility

`pio test -e native -f test_record_preparation -f test_record_tail_repair -f test_record_minimum_length`:32/32PASS,3suites,3.081s.

Measurement command (session scratch):

```sh
c++ -std=gnu++17 -O2 -DPIO_UNIT_TEST_NATIVE=1 \
  -I include -I .pio/libdeps/native/Unity/src \
  "$PI_SCRATCH_DIR/record_measure.cpp" \
  test/test_support/ProjectionGateWeakStub.cpp \
  .pio/libdeps/native/Unity/src/unity.c \
  -o "$PI_SCRATCH_DIR/record_measure"
"$PI_SCRATCH_DIR/record_measure"
```

Artifacts: session scratch `parse_record_latency.py`, `record-capture-analysis.json`, `record_measure.cpp`, `record-measure-results.txt`, `record-native-tests.log`. An initial harness link without the standard weak projection-gate fixture failed; the successful command above includes it. No source, hardware, SD or firmware upload by this measurement.
