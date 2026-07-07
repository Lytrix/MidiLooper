# 64-bar overdub-start PLAYING-window overload — bugfix (investigation)

**Kind:** bugfix  
**Date:** 2026-07-07  
**Architecture (permanent):** [RuntimeArchitecture.md](../00-authority/Architecture/RuntimeArchitecture.md)

Investigation-only. Implementation changes live in [overdub_start_playing_window_hot_path_refinement.md](overdub_start_playing_window_hot_path_refinement.md). Architecture: [DEC-016](../DECISION_LOG.md#dec-016-runtime-architecture-four-layer-model).

---

## Symptom

After 64-bar record stop, device fails to reach `PLAYING → OVERDUBBING`; serial stalls; persistence may not complete. 2-bar / June baseline passes.

---

## Evidence

| Artifact | Role |
|----------|------|
| `captures/host_midi_automation_serial_20260623_112324.log` | **Canonical PASS** — `PLAYING→OVERDUBBING` ×2, `PERS,result…ok` |
| `captures/host_midi_automation_serial_20260707_032321.log` | **Canonical FAIL (gate)** — trk 6 / slot 8, 0 overdub transitions |
| `captures/host_midi_automation_serial_20260707_010856.log` | July **PASS** — trk 5 / slot 0 (config sensitivity, O7) |
| `captures/host_midi_automation_serial_20260623_004032.log` | Early PASS — before `58d6c08` |
| `captures/host_midi_automation_baseline_20260707_024009.json` | Pre-fix FAIL JSON |
| `captures/host_midi_automation_baseline_20260623_112324.json` | June PASS JSON |

Full timeline: [64bar_regression_commit_analysis_enhancement.md](64bar_regression_commit_analysis_enhancement.md) § Capture evidence index.

| Signal | FAIL (`032321`, trk 6 / slot 8) | PASS (`112324`, trk 5) |
|--------|--------------------------------|------------------------|
| 64-bar record | completes | completes |
| `STOPPED_RECORDING → PLAYING` | 1 | 1 |
| `PLAYING → OVERDUBBING` | **0** | **2** |
| `PERS,result` ok | yes (save may complete) | yes |
| Post-PLAYING | MIDI out starts; overdub never arms | minutes of healthy PLAYING |

Post-fix partial (`20260707_025841`): record PASS; device died ~99 ms after `STOPPED_RECORDING→PLAYING` (46 `#CAP`, 0 inbound MIDI, 0 `ODUB` stages). Overdub press may not have reached firmware.

---

## Hypothesis (confirmed in code review)

PLAYING entry triggers **synchronous full-loop work** on paths that should defer:

1. `DisplayManager` → `ensureVisualCacheBuilt()` on PLAYING display refresh
2. `MidiLedManager::hasNoteOnInRange` → `ensureVisualCacheBuilt()` every bar-LED update during PLAYING
3. Redundant materialize on overdub entry / visual cache rebuild

64-bar loop magnifies cost → watchdog / USB stall before overdub arm.

**Architectural framing:** consumers triggered **representation rebuild** during **playback interval** activation — violates [DerivedViews.md](../00-authority/Architecture/DerivedViews.md) build policy.

---

## Validation gates (HITL)

Re-run after flash `teensy41-capture-serial`:

```bash
# Canonical 64+64 (or project baseline with 64-bar record)
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 64 --overdub-bars 64 \
  --verify-serial-log captures/session_<timestamp>.log
```

Pass criteria:

- `PLAYING → OVERDUBBING` ≥ 1
- `PERS,result,...,ok`
- `#CAP,ODUB,stage` lines present
- No serial heartbeat abort during post-stop window

---

## Status

- Root cause: documented
- Fixes: landed in firmware (uncommitted at investigation write-up) — see refinement plan
- **HITL re-gate:** pending successful Teensy upload

**Historical:** This plan may be archived after HITL PASS; architecture remains in `docs/00-authority/Architecture/`.
