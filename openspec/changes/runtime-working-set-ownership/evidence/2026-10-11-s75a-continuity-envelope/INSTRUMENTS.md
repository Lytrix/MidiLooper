# S7.5a increment 3 — the missing-timing instruments, landed inside the RAM1 envelope

Increment 2 measured what the device's existing rows could carry and named four metrics that could not be
measured at all, plus two that needed a tick column ([`MEASUREMENTS.md`](MEASUREMENTS.md) §3). This increment
**builds those instruments** — the first, second and fourth items of that list:

| # | Instrument | Row / field | Where |
|---|---|---|---|
| 1 | **scheduled tick on the emission row** | `#CAP,<us>,MO,<type>,<ch>,<d1>,<d2>,<tick>` (trailing field, so existing parsers keep working) | `DebugSessionCapture::midiOut` / `midiOutEvent` — the scheduled tick `MidiEvent::tick` beside the emission µs, which is what real jitter and the musical-tick burst need |
| 2 | **capture timestamp error** | `capstamp_max=<ticks>,capstamp_n=<samples>` in `DIAG,envelope_instr` | `noteInputServiceTick(clock)` sampled in `MidiHandler::handleMidiInput` and `noteCaptureStampTick(newEvt.tick)` at the capture append in `Track/TrackCaptureInput.cpp`; the delta is the musical time an event slipped between entering the input service and being stamped |
| 4 | **capture replenishment** | `repl_wait_us`, `repl_work_us`, `repl_refused`, `repl_pairs` in the same row | `LoopPlaybackRuntime::replenishCanonicalCaptureChunk` reports one call's work, a refusal (below target but the reserve could not be topped up — a request that could not complete) and the pair that resolves it; the wait is computed by the owner of the refusal timestamp |

Both new instruments ride **one aggregated Tier-A row per telemetry window** (`DIAG,envelope_instr`), never one row
per event, emitted direct-when-USB-has-room with the ring as fallback (the `readinessReceiptImmediate` rules) —
these are raised while a transport runs, which is the regime whose shed path drops non-Tier-A head records.

## The RAM1 result, and why the shape changed

The first cut (`4 telemetry tags + 4 tier prefixes + 4 new functions`) **failed the guard**:

```
variables 100,288  (approved 96,192 → +4,096 B: the DTCM align-edge snap from the new literals)
ITCM padding 536   (floor 840; code 392,680 = +320 B)
```

The landed form is inside the envelope, **byte-identical to the approved baseline**:

```
[RAM1 budget] teensy41-capture-serial: variables 96,192 / ITCM 393,216 (code 392,360 + padding 856) / locals 34,880
```

Three changes bought that, all of them the trap document's own advice and none of them a budget re-base:

1. **One row instead of four tag literals**: the consolidated `envelope_instr` sink carries every field, and its
   format is flash-resident (`SC_FMT`/`PSTR`). Exactly **one** plain literal is added —
   `"DIAG,envelope_instr,"` for the Tier-A prefix (20/31 bytes, `check_marker_contract.py`: `OK`; `--audit`: 90
   prefixes, 0 over the limit).
2. **All new functions in flash** (`RT_FLASHMEM_FN`; the sink is `SC_MEM_ATTR` like its neighbours), so ITCM code
   is unchanged. The only additions to ITCM are the three call sites, and they fit in the existing 16 B.
3. **The replenishment owner's clock is local and guarded**: that TU cannot see `micros()` in a native build, so
   `replenishNowUs()` returns the device clock on device and 0 under `PIO_UNIT_TEST_NATIVE` — which the module
   reads as "no instant" (a refusal at 0 never completes). Its logic is exercised natively with injected
   timestamps; its values are device measurements.

A **native weak default** (`test/test_support/EnvelopeInstrWeakStub.cpp`, registered in
`scripts/native_test_link.py`) keeps suites that compile a reporting owner without linking the telemetry module
linkable, exactly like the projection-gate stub beside it; the real definitions win wherever the module is linked.

## Verification

| Check | Result |
|---|---|
| `pio test -e native` | **3047 cases / 7 skipped / 3040 succeeded / 0 failed** (3041 baseline + the 6 new instrument cases) |
| `test_runtime_timing_telemetry` (new cases) | green: the stamp error is the tick delta from the input-service entry; an append with no active service records nothing; the worst is kept across services; a refusal at 1000 with the top-up at 3500 reads **exactly 2500 µs**; a completion with no refusal records nothing; the work maximum is kept |
| `test_capture_ring` | green, with the new Tier-A boundary pinned positively **and** by a longer-subtag negative |
| `pio run -e teensy41-capture-serial` | **SUCCESS**; RAM1 `variables 96,192 / code 392,360 + padding 856` — identical to the approved baseline |
| `check_marker_contract.py 'DIAG,envelope_instr,'` | `OK emitted / tier-a-prefix (20/31 bytes)`; `--audit` 90 prefixes, 0 over |

## What this changes for the envelope

* `capture_timestamp_error` can now be read in **ticks** from the device's own row (its earlier reading was a
  schedule-relative proxy only).
* `same_tick_burst` and real `per_route_output_jitter` get the **scheduled tick** they were missing — the µs-stamp
  burst becomes a lower bound beside a tick-exact one.
* `capture_replenishment` gets a **request-to-completion** wait instead of a reclaim cadence.

Still owed and unchanged: `queue_residence` (the `droid_queue` family is env-gated off in the default capture env
and covers only the DROID feedback queue), the distribution (p50/p99) rows, a per-route lateness split, and
host↔device clock correlation for `action_to_audible` — the last named in `MEASUREMENTS.md` §3.

**Not claimed:** no numeric limit, no envelope declaration, no owner sign-off, no qualification run, no
`S7.5a`/`S7.5b`/`S7.5` checkoff; the frames above are device *measurements* to be read by the qualification legs.
The immediate follow-up is wiring the new rows into `scripts/hitl/measure/envelope_timings.py` (`envelope_instr`
→ `capture_timestamp_error` and `capture_replenishment`; the `MO` tick column → `same_tick_burst` and jitter).
