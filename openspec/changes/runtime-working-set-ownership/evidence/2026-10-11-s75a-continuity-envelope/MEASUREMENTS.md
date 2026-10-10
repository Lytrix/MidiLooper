# S7.5a increment 2 — the measurement layer for the missing continuity timings (with readings)

S7.5a — continuity envelope and timing oracle requires every metric in the envelope to have a **unit, a
measurement point and a clock correlation before any qualifying run**, and forbids inventing the numbers
(`DEC-108` §13b: *"the envelope is derived from grain costs and the worst permitted service gap — no invented
microsecond budget"*). Increment 1 ([`DECLARED_ENVELOPE.md`](DECLARED_ENVELOPE.md), §5 Table B) listed the
metrics the spec names that had **no number**; this increment gives **each one its own measurement** and its
own test, and reports what the two captures this repo holds actually say.

**No limit is declared here** and none of these readings is a limit: they are measurements of **specific
runs**, most of them runs known to be outside any sane envelope (the 373 ms input gap below is the
Note-size/Record-return leg, not a steady state). S7.5a stays unchecked.

## 1. What was built

| Artifact | Role |
|---|---|
| `scripts/hitl/measure/envelope_timings.py` | One measurement function per metric, a `Measurement` record (`metric`, `unit`, `availability`, `value`, `source_rows`, `reason`, `detail`) and `measure_all()`. Runnable: `PYTHONPATH=scripts .venv/bin/python -m hitl.measure.envelope_timings <serial-log> [--accept-row TEXT] [--json]`. |
| `scripts/test_envelope_timings.py` | **One test class per metric** (34 cases): synthetic rows for determinism plus structural read-outs of the real captures. Run: `.venv/bin/python -m unittest discover -s scripts -p test_envelope_timings.py` → **34/34 OK**. |

Three availability states, which are the whole point of the layer:

* `measured` — the rows carry the quantity the metric names; `value` is that quantity.
* `unproven` — rows exist but cannot carry the metric (a maximum-only family, an all-zero instrument that
  cannot distinguish "no lateness" from "never fed", or a proxy whose number would be misread). **`value` is
  `None`**; the proxy reading stays in `detail` and is never reported as the metric.
* `missing` — the capturing image cannot carry the metric (family env-gated off, or no instrument exists).
  `value` is `None`, `reason` names what is owed.

Every row format is taken from the emitters (`RuntimeTimingTelemetry.cpp`, `DebugSessionCapture.cpp`) and the
`win_gap` field order from `scripts/hitl/verify/windowed_lane.py` — during this increment the tests caught and
fixed **one real field-index bug** in that parser (`permitted`/`overruns`/`overrunMax` read one field late).

## 2. Readings from the two captures held in this repo

Capture A = `captures/note-edit-enter-24bar-t8s4-01/session_20261010_194956.log` (the 2026-10-10
NOTE_EDIT-enter device run). Capture B = `captures/s39-hw-receipts-run1/session_20261009_153953.log`
(the 3.9 hardware-receipts 24-bar record leg).

| Metric | Unit | A | B | Instrument status |
|---|---|---|---|---|
| `max_midi_input_service_gap` | µs | **372,702** (34 rows) | **336,627** (33 rows) | measured — `DIAG,midi_gap`; maximum only, distribution (p50/p99) **owed** |
| `max_midi_input_service_duration` | µs | **99,663** (34 rows) | **38,832** (33 rows) | measured — `DIAG,midi_input`; one MIDI service of ~100 ms is itself a named envelope blocker |
| `capture_timestamp_error` | ticks | 0 (8 rows) | 0 (384 rows) | measured **as a schedule-relative proxy**; the firmware-internal stamping error has **no instrument at all** |
| `per_route_output_lateness` | µs | **7,922** (68 rows) | **6,620** (66 rows) | measured — `DIAG,late_on`/`late_off`; per-route split and jitter still owed |
| `per_route_output_jitter` | µs | `unproven` (2,514 rows) | `unproven` (2,690 rows) | **not measurable today**: no scheduled tick per emitted event; onset spread includes musical rests (tens of seconds) |
| `queue_residence` | µs | `missing` | `missing` | instrument exists (`DIAG,droid_queue,<depth>,<oldestAgeUs>,<overflow>`) but the family is **env-gated off** in the default capture env (`SESSION_CAPTURE_DROID_QUEUE=0`); only the investigation-diag image can carry it, and only for the DROID feedback queue |
| `route_throughput` | events/s | **20** peak (266 rows) | **90** peak (514 rows) | measured — emissions per route from `MO` rows; sustained rate and per-route split travel in `detail` |
| `same_tick_burst` | events | `unproven` (2,514 rows) | `unproven` (2,690 rows) | **not measurable as a musical-tick quantity**: the `MO` row has no tick column |
| `deferred_owner_service_advance` | ticks | **112** vs declared `permitted=64` (78 rows) | **50** (625 rows) | measured — `DIAG,win_gap.maxDelta`; capture A shows the recorded overrun class, so it is **not** an in-envelope run |
| `deferred_owner_denial` | turns | 0 (78 rows) | 1 (625 rows) | measured — `DIAG,win_gap.deniedTurns`, with eligible/progress turns in `detail` |
| `capture_replenishment` | µs | `unproven` (46 rows) | `unproven` (17 rows) | **not measurable as a latency**: no request→completion row; inter-reclaim cadence is dominated by idle stretches |
| `action_to_audible` | µs | `unproven` (1,889 candidates) | `missing` (no marker given) | **parked outside the envelope's limit table (2026-10-11)**: it is measured (635 µs and 3,244,952 µs on two device legs — and those are *different event classes*), but the endpoint is positional and the class unnamed, so no limit may be attached |

What these readings already establish, as measurements rather than claims:

* the **input-service gap** and the **single service duration** in these legs are 1–2 orders of magnitude above
  every musical deadline band in the envelope basis (1/32–1/16 beat = 6.25–187.5 ms) — these runs cannot be
  used as in-envelope evidence, and the metric that must bound them now has a number and a source row;
* the **lateness instrument is fed** (7.9 ms / 6.6 ms), so it is an instrument and not a zero-only placeholder;
* **four of the twelve metrics cannot carry a number from today's rows** (jitter, queue residence,
  same-tick burst, replenishment), and **two of those need a new column or row**, not a new analysis.

## 3. What each remaining metric needs before it can carry a value

| Metric | What is owed |
|---|---|
| `max_midi_input_service_gap` | a distribution (p50/p99) sample row, or an accepted maximum-only basis |
| `capture_timestamp_error` | **an instrument**: event-arrival-vs-tick-boundary error has no probe in `src/`/`include/` |
| `per_route_output_lateness` / jitter | per-route split, and a scheduled tick per emitted event (a tick column on the emission row) |
| `queue_residence` | the family enabled on the qualifying image, plus residence instruments for the other queues |
| `same_tick_burst` | a tick column on the emission row (µs stamps are a lower bound only) |
| `deferred_owner_service_advance` / denial | an in-envelope run (the held captures are outside it) |
| `capture_replenishment` | a request→completion latency row for the replenishment owner |
| `action_to_audible` | **nothing is owed to this envelope — it is parked.** Reinstating it needs an identity-paired, class-split pair: the device's own accept row (`GS`) as the anchor, with the answered feedback row and the answered musical note scored separately |

## 4. Reproduce

```sh
.venv/bin/python -m unittest discover -s scripts -p test_envelope_timings.py
PYTHONPATH=scripts .venv/bin/python -m hitl.measure.envelope_timings \
    captures/note-edit-enter-24bar-t8s4-01/session_20261010_194956.log \
    --accept-row "Button press: Edit Mode (short)"
PYTHONPATH=scripts .venv/bin/python -m hitl.measure.envelope_timings \
    captures/s39-hw-receipts-run1/session_20261009_153953.log
```

**Not claimed:** no limit, no envelope, no qualification run, no in-envelope verdict; no firmware, test-suite,
bound-class or task-checkbox change beyond this measurement layer and its tests; S7.5a, S7.5b, S7.5 and
S7.5b.8c/d remain unchecked.
