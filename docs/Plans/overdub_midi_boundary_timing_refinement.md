# Overdub MIDI boundary timing refinement

**Status:** Stage 4a telemetry in tree; device recapture open. Stage 3 closed (not authorized). Optimization owner for #5 still unset.  
**Date:** 2026-09-07  
**Kind:** refinement (timing attribution → measured batch bound → owner map → Stage 4a → mechanism)  
**Evidence:** [`170537`](../../captures/session_20260907_170537.log) manual slave-clock record+overdub. Internal-clock occupy [`165045`](../../captures/session_20260907_165045.log) / [`170249`](../../captures/session_20260907_170249.log). Prior [`143701`](../../captures/session_20260907_143701.log) `DIAG,clk` / `usbclk` maxima 6.1–10.5 ms.  
**Does not authorize:** bounded FIFO (Stage 3); `updateAllTracks` optimization; Ring A Slice 4; naming `playCommittedLoopMidi` as optimization owner; interval reservation; a new scheduler; revisiting [`overdub_wrap_sealed_shorten_paint_bugfix.md`](overdub_wrap_sealed_shorten_paint_bugfix.md).

Cursor sources: `~/.cursor/plans/overdub_midi_timing_fc31fe68.plan.md`, Stage 4 gate `~/.cursor/plans/stage_4_design_gate_e4c15a80.plan.md`.

---

## Debugging boundary

```text
display / wrap-sealed paint                         ← trust (143701)
RuntimeTimingTelemetry S0 + late_on/off/clk         ← trust as duration / send lateness
external-clock arrival / batch / dispatch / period  ← Stage 1
USB batch composition                               ← Stage 2
bounded FIFO dispatch                               ← Stage 3 CLOSED (not authorized)
TrackManager::updateAllTracks cost                  ← Stage 4 owners-first design (then mechanism)
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

**Invariant:** a 5 s window reports USB-device batch-size and clock-count distributions plus per-bucket dispatch maxima without changing `MidiDispatchOrder::planDispatchOrder`.

| Window tag | Meaning |
|------------|---------|
| `usb_batch` | max size, saturated-batch count, max clocks, max consecutive clocks |
| `usb_bkt_size` | counts in 1–8 / 9–16 / 17–32 / 33–64 / 65–128 |
| `usb_bkt_clk` | clock-count buckets (batches with `clockCount > 0` only) |
| `usb_bkt_disp` | max `dispatchMidiBatch` µs in that clock-count bucket |

Owner: `MidiBatchComposition::summarize` + `RuntimeTimingTelemetry::noteUsbDeviceBatchComposition`. Serial emit stays on `maybeEmit`. Empty polls are ignored.

Host: `test_midi_dispatch_order` (`Clock, Note, Clock` stays two clock runs) and `test_runtime_timing_telemetry` bucket/saturation accounting.

### [`165045`](../../captures/session_20260907_165045.log) correlation

Internal-clock occupy HITL (`service_period=2666`). Stage 1 clock-batch tags are present and stay 0.

| Metric | Overdub max | Whole-run max |
|--------|-------------|---------------|
| `usbclk` / `clk` / `clk_batch` / `extclk_arrival` / `extclk_dispatch` | 0 | 0 |
| `usbread` | 129 µs | 129 µs |
| `usbdisp` | 1738 µs | 18709 µs |
| `tracks` | 9234 µs | 24688 µs |
| `late_event` | outgoing `clk` 4699–12697 µs | `on` 5428 µs; later `off` 13412 µs |
| `late_on` / `late_off` / `late_clk` overCount | 255 / 247 / 235 | 46 / 35 / 43 |

**Decision gate:** this log does **not** show USB-device clock-batch admission. Clock-driven work here is internal-tick `tracks` and outgoing `late_clk`. Stage 3 bounded FIFO is **not** authorized from `165045`.

### [`170537`](../../captures/session_20260907_170537.log) slave-clock composition

Manual `capture_session.py` (not occupy HITL). `clockrate` 47–48. `service_period` ≈ 20824 µs. `late_clk` = 0 on every window.

ST: `PLAYING → OVERDUBBING` at 204.97 s and 218.08 s. OD1 returns `OVERDUBBING → PLAYING`. OD2 ends `OVERDUBBING → STOPPED`.

| Tag | Record max | OD1 max | OD2 max |
|-----|------------|---------|---------|
| `usb_batch` (size, sat, clocks, consec) | 4, 0, 3, 3 | 3, 0, 1, 1 | 3, 0, 1, 1 |
| `usb_bkt_size` / `usb_bkt_clk` | all in 1–8 (234) | all in 1–8 (241) | all in 1–8 (240) |
| `usb_bkt_disp` (1–8) | 42821 µs | 25030 µs | 26281 µs |
| `clk_batch` (max clocks, batch count) | 3, 234 | 1, 241 | 1, 240 |
| `extclk_arrival` | 65498 µs / 234 | 50941 µs / 241 | 66590 µs / 240 |
| `extclk_dispatch` | 12184 µs / 2 | 674 µs / 0 | 1472 µs / 0 |
| `usbclk` / `clk` / `tracks` | 19560 / 19463 / 19530 | 24568 / 24568 / 24567 | 26279 / 26279 / 26278 |
| `usbdisp` / `usbread` | 42821 / 5 | 25030 / 2 | 26281 / 4 |

Whole-run `usbdisp` later reaches 187445 µs after stop (not inside OD windows). Saturated-batch count stays 0. No window leaves the 1–8 size or clock bucket.

**Decision gate:** USB-device clock-batch admission is present, and every clock-bearing batch is 1–3 clocks (OD1/OD2: 1). Stage 3 bounded FIFO is **not** authorized from `170537` — this capture does not show a large synchronous clock batch.

## Stage 3 — Bounded FIFO dispatch during overdub

**Closed — not authorized** from Stage 2 decision gates ([`165045`](../../captures/session_20260907_165045.log), [`170537`](../../captures/session_20260907_170537.log)). Clock-bearing USB batches stay 1–3 messages; saturated count 0. Primary measured owner is `TrackManager::updateAllTracks`, not FIFO admission. Architecture checkpoint for FIFO remains YES / YES / YES if ever reopened under a new gate.

## Stage 4 — Owners first, then mechanism

**Design session.** User pins (2026-09-07): (1) name owners before Ring A; (2) Stage 4a attribution before naming an optimization owner for the `170537` multi-track sum.

### Ownership vocabulary (locked)

Do **not** conflate measurement boundaries with architectural ownership:

| Role | Symbol | Meaning |
|------|--------|---------|
| Clock-path aggregate owner | `TrackManager::updateAllTracks` | Owns the interval-path aggregate (`tracks` / row 1) |
| Per-track measurement shell | `track_update` | Timing shell around one track iteration — **not** an architectural owner |
| Clock-path consumer / call path | `playCommittedLoopMidi` | Path entered from `playMidiEvents` / `playMidiEventsForSlot` — **not** an optimization owner by itself |
| Known rebuild component | `ensurePlaybackMergedMidiEventsBuilt` | Owns content-sized window rebuild (row 2) when it runs |
| Optimization owner | *(unset for #5)* | The bounded work that **causes** the measured cost — assigned only after Stage 4a |

Naming `playCommittedLoopMidi` as the owner of #5 would turn a measurement/path boundary into an architectural ownership claim. That is forbidden until internal cost is decomposed.

### Sequence (locked)

1. Name aggregate / shell / path / known component owners (below).
2. Stage 4a — decompose remaining time inside the consumer path.
3. Assign an optimization owner from the dominant attributed span.
4. Only then choose a mechanism. Ring A is **only** for rebuild-admission multiplicity (#4), not a generic cure for a large `tracks` sum.

### Owner map

| # | Cost | Owner / role | Bounds | Status |
|---|------|--------------|--------|--------|
| 1 | Aggregate clock body (`tracks` ≈ `clk`) | `TrackManager::updateAllTracks` — clock-path aggregate | 1 | **Confirmed** |
| 2 | Per-track sample | `track_update` — measurement shell only | 1 shell | **Confirmed shell** |
| 3 | Content-sized window rebuild | `ensurePlaybackMergedMidiEventsBuilt` | 2 | **Confirmed** |
| 4 | Rebuild multiplicity per dispatch | Ring A **admission policy** at the row-1 call site (caps how many #3 enter one pulse) | 1 note; class stays Unbounded | **Designed; firmware not authorized** |
| 5 | `170537` multi-track sum | `playCommittedLoopMidi` path; **internal work unresolved** | under 1 (+ nested 2) | **Stage 4a tags in tree; device attribution open** |
| 6 | Compatibility unit cost | row-2 compatibility gather | 2 / RC4 | **Separate fix; not Ring A / not Stage 4 mechanism** |

### Stage 4a — attribution inside the consumer path

**In tree.** Architecture checkpoint (tags only): Ownership **NO** / Transitions **NO** / Unbounded interval owner **NO**.

**Invariant:** a 5 s window attributes the per-track clock body so that `track_rebuild + track_order + track_send + track_slot + track_resid ≈ track_update` (within fixed accounting), without changing admission or playback behavior.

| Window tag | Span | Call site |
|------------|------|-----------|
| `track_rebuild` | `ensurePlaybackMergedMidiEventsBuilt` + ledger reconcile | `playCommittedLoopMidi` |
| `track_order` | order rebuild + wrap/ledger catch-up + cursor advance **minus** nested send | `playCommittedLoopMidi` |
| `track_send` | `sendMidiEvent` duration | `Track::sendMidiEvent` while the track-path nest is active |
| `track_slot` | `updateAllTracks` per-track body before play **minus** nested send | `TrackManager::updateAllTracks` |
| `track_resid` | `trackDuration − (rebuild+order+send+slot)` | `commitTrackPathNested` |

Owner: `RuntimeTimingTelemetry` (`beginTrackPathNested` / `commitTrackPathNested`). Emit from `maybeEmit` only. Inactive nest is a no-op. Native: `test_runtime_timing_telemetry` residual and exclusive-send accounting; `test_capture_line_tier` Tier-A tags.

**Does not authorize:** Ring A; emit admission; skipping tracks; changing rebuild reuse policy; naming `playCommittedLoopMidi` as optimization owner.

### [`235930`](../../captures/session_20260907_235930.log) Stage 4a device attribution

Slave-clock record + two overdubs. `service_period` ≈ 20824–20832 µs. `tracks` ≈ `clk` ≈ `usbclk` on every window. Residual max **24 µs**; slot max **26 µs**. Attribution covers the per-track body.

ST: `PLAYING → OVERDUBBING` at 23.85 s and 54.80 s; both return `OVERDUBBING → PLAYING`. Stop at 64.82 s.

| Window | `tracks` | Dominant `track_update` | `track_rebuild` | `track_order` | `track_send` | `track_slot` / `track_resid` |
|--------|----------|-------------------------|-----------------|---------------|--------------|------------------------------|
| 23.73 s (OD entry) | 10870 | T4 7604; T6 3575 | 3160 | 544 | **6010** | 23 / 18 |
| 33.73 s (OD1 peak) | **13149** | T4 **9007**; T6 3932 | 3526 | **7974** | 1026 | 1 / 23 |
| 38.73 s | 5976 | T6 4790; T4 3053 | **4366** | 414 | 3022 | 1 / 24 |
| 43.73 s | 12398 | T4 8316; T6 4775 | 4348 | **7269** | 3018 | 1 / 24 |
| 58.73 s (OD2) | 6902 | T6 4729; T4 3053 | 4284 | 435 | 3020 | 1 / 19 |

`playback_build,compatibility` on T6 is 3.2–4.3 ms about every 2 s (matches `track_rebuild` / T6). Window-tag maxima are not one iteration: do not sum them to `tracks`. `tracks` remains a **multi-track sum**. OD1 peak 13.1 ms stays under `service_period`.

**Decision gate:** this is **not** Ring A. Rebuild multiplicity is not the OD1 peak. Slot/commit is not the cost. Residual does not hide a missing owner. The largest attributed span on the peak windows is **order / cursor catch-up** (`track_order` 7.3–8.0 ms with T4 8.3–9.0 ms). Send is the largest span at OD entry (6.0 ms). Rebuild stays a separate ~4 ms T6 compatibility unit (RC4 / #6), not this peak.

Optimization owner for #5 is still unset until a mechanism is chosen from this evidence.

### After Stage 4a device evidence

Re-open mechanism choice from the **dominant attributed span**:

- Rebuild **multiplicity** dominates → Ring A under its own Slice-4 authorize + OpenSpec (#4 only).
- Rebuild **unit cost** dominates → RC4 / row 2 (#6), not this Stage 4 mechanism.
- Order / catch-up / send / slot-commit dominates → that function becomes the Stage 4 optimization owner (+ DEC if bound policy changes).
- Residual dominates or spans stay flat multi-track emit → document capacity / defer; Ring A would be the wrong problem.

Ring A is a mechanism for rebuild-admission multiplicity, **not** a generic solution for a large `tracks` sum. If attribution shows ordinary per-track playback work, do not authorize Ring A for #5.
