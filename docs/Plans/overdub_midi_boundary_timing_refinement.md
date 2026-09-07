# Overdub MIDI boundary timing refinement

**Status:** Stage 1 host shipped (observability; device recapture open). Stages 2–4 not started.  
**Date:** 2026-09-07  
**Kind:** refinement (timing attribution → measured batch bound → bounded FIFO)  
**Evidence:** [`143701`](../../captures/session_20260907_143701.log) `DIAG,clk` / `usbclk` maxima 6.1–10.5 ms in overdub windows; `late_event` reports note-on/off lateness. Those lines do not name clock arrival, batch clock cardinality, or the note-deadline service period.  
**Does not authorize:** bounded FIFO firmware (Stage 3); `updateAllTracks` optimization (Stage 4); interval reservation; a new scheduler; revisiting [`overdub_wrap_sealed_shorten_paint_bugfix.md`](overdub_wrap_sealed_shorten_paint_bugfix.md).

Cursor source: `~/.cursor/plans/overdub_midi_timing_fc31fe68.plan.md`.

---

## Debugging boundary

```text
display / wrap-sealed paint                         ← trust (143701)
RuntimeTimingTelemetry S0 + late_on/off/clk         ← trust as duration / send lateness
external-clock arrival / batch / dispatch / period  ← Stage 1
USB batch composition                               ← Stage 2
bounded FIFO dispatch                               ← Stage 3 (design gate)
TrackManager::updateAllTracks cost                  ← Stage 4 after measurement
```

## Authority

- [`RuntimeArchitecture.md`](../Authority/Architecture/RuntimeArchitecture.md) — representations must not do content-sized work on the timing-critical path.
- [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) — `TrackManager::updateAllTracks` is inventory row 1 (unbounded); `MidiHandler::processPendingPresses` is row 20.
- [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md) — MIDI Input Gap is diagnostic; MIDI deadline lateness is the musical gate; do not infer C15 from `midi_gap` alone.
- Owner: [`RuntimeTimingTelemetry`](../../include/Utils/RuntimeTimingTelemetry.h). Call sites: `MidiHandler::handleMidiInput`, `MidiHandler::dispatchMidiBatch`, `ClockManager::onMidiClockPulse`, `TrackManager::updateAllTracks`, `Track::sendMidiEvent`.

## Working hypothesis

A large synchronous USB batch admits too much clock-driven work in one service turn. The first optimization target is the amount of clock advancement admitted per turn, not USB reading alone. Stage 1 makes that claim testable.

## Stage 1 — Make clock timing attribution explicit

**Invariant:** a later capture can distinguish clock arrival delay, USB dispatch delay, track-update delay, and output-send delay without claiming C15 from `midi_gap` alone.

| Window tag | Meaning | Not this |
|------------|---------|----------|
| `extclk_arrival` | USB-device batch arrival vs expected external-clock instant (`overCount` = late batches) | `late_clk` (outgoing master clock send) |
| `extclk_dispatch` | µs from that batch's read-complete to each `onMidiClockPulse` entry | `usbdisp` (whole-batch dispatch duration); `usbclk` (time inside clock handler) |
| `clk_batch` | max MIDI Clock messages in one USB-device input batch; second field is clock-bearing batch count | `clockrate` (pulses/sec) |
| `service_period` | playback service period (µs) used for `late_on` / `late_off` | `late_clk` on-time window |
| `tracks` / `clk` / `usbclk` | existing work durations | arrival vs send |
| `late_on` / `late_off` | output-send vs service cadence | arrival |
| `late_clk` | outgoing clock-send lateness | external-clock arrival |

Hot path: fixed-size integer stores. Emit from `maybeEmit` only.

Native: `test_runtime_timing_telemetry` — cadence reset, external-clock batch attribution, note lateness classification (`late_on` / `late_off` stay independent of `extclk_arrival` and `late_clk`).

### Pre-implementation review

| Topic | Decision |
|-------|----------|
| Owner | Extend `RuntimeTimingTelemetry` |
| Ownership / transitions / interval bound | No / No / No |
| USB-host / DIN | Not attributed in Stage 1 (HITL uses USB-device) |
| Arrival cadence | Expected instant, no extra-period slack; consume `clockCount` periods per batch |
| Dispatch delay | Per pulse vs last USB-device batch arrival; ignored when no batch remains |

## Stage 2 — Measure USB batch composition

Not started. Decision gate: proceed to Stage 3 only when captured composition shows one service turn can admit enough clock-driven work to exceed the declared timing/execution bound.

## Stage 3 — Bounded FIFO dispatch during overdub

Not started. Architecture checkpoint will be YES / YES / YES. Do not implement without the approved design gate.

## Stage 4 — Optimize `updateAllTracks` from measurements

Not started. Optimize only the measured owner path after the Stage 1–3 before/after experiment.
