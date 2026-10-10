# S7.5a increment 4 — distribution rows, the per-route split, and the action-to-audible clock correlation

**Status: instruments landed and observed on device. No limit, no sign-off and no checkoff is claimed** — S7.5a,
S7.5b and S7.5 remain open, and `DEC-100` still governs: the envelope is *declared and reported*, never enforced.

## What landed

| Surface | Row | Unit | Where |
|---|---|---|---|
| Window p50/p99 per channel | `#CAP,<us>,DIAG,env_dist,<channel>,<p50LoUs>,<p50HiUs>,<p99LoUs>,<p99HiUs>,<samples>` | µs (bucket bounds) | `src/Utils/EnvelopeDistRows.cpp` (own TU, DTCM-carved) |
| Per-route lateness split | `#CAP,<us>,DIAG,late_route,<type>,<ch>,<maxUs>,<overCount>,<samples>,<spill>` | µs | same |
| Classifier entries (Tier A) | `DIAG,env_dist,` (14 B), `DIAG,late_route,` (16 B) | — | `isTierATextLine`, prefixes returned by the carved TU |

Channels: `0` `midi_gap`, `1` `midi_input`, `2` `late_on`, `3` `late_off` — one histogram each, companded: 256 buckets
of 256 µs cover 0..65,535 µs and 16 power-of-two buckets cover 65,536 µs..2^32-1. A quantile is therefore reported
as **the interval of the bucket it fell in**, never as an exact microsecond (`DEC-108` §13b). State is window-scoped
and lives with the rest of `State` (PSRAM on target); one row per channel that recorded a sample, one row per route
the window held, and every route row carries the split's spill count so a truncation is visible.

## RAM1: why the rows needed their own translation unit

The first landing put the formats, the four channel names and the two Tier-A prefixes in
`DebugSessionCapture.cpp`, and the build failed the guard exactly as the rule predicts:

```
[RAM1 budget] variables 100,288 / ITCM 393,216 (code 392,440 + padding 776) / locals 30,784
  - RAM1 variables changed: 100,288 B vs approved 96,192 B (+4,096 B)   <- the 4 KB DTCM align snap
  - local allowance changed: 30,784 B vs approved 34,880 B (-4,096 B)
  - padding below the approved floor: 776 B < 840 B
```

Two costs, two distinct fixes, **no re-base**:

- **DTCM (+4,096 B)**: the new literals crossed the align edge of the classifier's own unit. Fixed by moving every
  new literal — both formats, the four channel names *and* both Tier-A prefixes — into
  `src/Utils/EnvelopeDistRows.cpp`, carved out of DTCM in `linker/imxrt1062_t41_lcr.ld`
  (`*EnvelopeDistRows.cpp.o(.rodata.str* .rodata.*.str*)`). The prefixes are returned by that unit
  (`envelopeDistTagPrefix()` / `envelopeRouteTagPrefix()`) rather than written inline at the classifier, so no new
  literal lands in a DTCM unit at all.
- **ITCM (+80 B)**: the three new sample feeds sit in functions that stay in ITCM. Fixed by moving the two MIDI-input
  handlers to FLASHMEM (they are main-loop input handlers; `noteInputServiceTick` / `noteCaptureStampTick` are
  already flash) and by feeding the note-emission path through **one** flash entry point
  (`recordLatenessSample`) instead of two calls.

Measured after both fixes, on the linked image:

```
[RAM1 budget] teensy41-capture-serial: variables 96,192 / ITCM 393,216 (code 392,184 + padding 1,032) / locals 34,880
[RAM1 budget] 192 B of headroom above the floor
```

`variables` 96,192 and `locals_free` 34,880 are byte-identical to the approved baseline, `padding` 1,032 is *above*
the approved floor of 840 (and above the baseline's own 856), and ITCM code is 176 B *smaller* than the baseline.

## Host side

- `measure_per_route_output_lateness` reads the split when the rows are present (value = the worst route's window
  maximum, `detail.routes` names every route, `detail.worst_route_(type/ch)`, `detail.spill_samples`) and falls back
  to the `late_on`/`late_off` aggregate for a capture that predates it, saying so in `detail.split_owed`.
- The four channels' p50/p99 **intervals** are attached to the metric they belong to
  (`midi_gap` → the gap metric, `midi_input` → the duration metric, `late_on`/`late_off` → lateness). A capture
  without the rows gains nothing and names what is owed.
- `action_to_audible` now uses the **host↔device clock correlation** it owed. The log interleaves host-stamped lines
  with device `#CAP,<us>` rows and no line carries both clocks, so the offset is *bounded*: over every host-stamped
  line, the gap to the nearest preceding device row, minimised. On the S7.5a capture that bound is **102 µs over 328
  pairs**, which is far finer than the nearest-row base it replaces (median ~0.3 s in the same capture).

## Evidence

- **Build**: `pio run -e teensy41-capture-serial` **SUCCESS**, guard line above; marker contract preflight OK for both
  new prefixes (`DIAG,env_dist,` 14/31 B, `DIAG,late_route,` 16/31 B).
- **Native**: full sweep **3,051 cases / 7 skipped / 3,044 succeeded / 0 failed**, including four new cases for the
  bucket scheme, the quantile intervals, the route split with spill, and the window-boundary clear, plus the two new
  Tier-A boundaries pinned in `test_capture_ring`.
- **Host**: `test_envelope_timings` **53/53**, including the split, the interval attachment, the legacy fallbacks and
  the clock correlation.
- **Device** (`captures/envdist-t8s4-01`, base preset, track 8 / slot 4, managed run **PASS**): both rows are live —
  56 `env_dist` rows and 22 `late_route` rows, e.g. `env_dist,midi_gap,0,255,1024,1279,12051` (12,051 samples in that
  window: p50 ≤ 255 µs, p99 ∈ [1,024, 1,279] µs) and `late_route,144,8,4735,9,12,0`.

## Recorded readings from that run (not a qualification)

| Metric | Reading | Note |
|---|---|---|
| `per_route_output_lateness` | **241,547 µs** | worst route `144/8`; split: `128/8` 241,498, `144/8` 241,547, spill 0 |
| — its distribution | p50 **and** p99 ∈ [131,072, 262,143] µs | 505 `late_on` samples / 11 windows: the shape is not an outlier tail |
| `max_midi_input_service_gap` | 375,509 µs | 17 rows |
| `max_midi_input_service_duration` | 377,951 µs | 17 rows |
| `capture_timestamp_error` | 1 tick | 168 sampled appends |
| `same_tick_burst` | 6 events | per loop traversal |
| `route_throughput` | 80 events/s | 509 MO rows |
| `deferred_owner_service_advance` | 158 ticks | declared permitted 64 |
| `capture_replenishment` | unproven | 0 refusals, no completed pair |
| `action_to_audible` | **3,244,952 µs** | host-stamp base; positional base reading 3,298,580 µs; correlation 548 µs over 467 pairs |

**Open observation, not attributed.** This run reads an order of magnitude worse than the S7.5a capture from the
previous image (lateness max 44 ms, input-service duration 103 ms) and the whole distribution moved, not one tail:
the median lateness is ≥131 ms in both families. The instrument's own cost on those paths is a flash call and a
16-bit histogram increment (PSRAM, on the window emitter's side), which is µs-scale and cannot produce 131 ms; but
this run *also* differs in content and outcome (it passed the base leg where the earlier one aborted), so nothing is
attributed. Settling it needs the previous image re-run under this same preset, device-leg-to-device-leg.

## Also true

- `queue_residence` stays `missing` on this image (`SESSION_CAPTURE_DROID_QUEUE=0` in the default capture env).
- `capture_replenishment` stays `unproven`: no refusal and no completed pair occurred, so the request→completion
  interval was never observed — that is a fact about the run, not about the instrument.
