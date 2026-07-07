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
| `captures/host_midi_automation_baseline_20260707_024009.json` | Pre-fix FAIL |
| `captures/host_midi_automation_serial_20260707_024009.log` | Pre-fix serial |
| `captures/host_midi_automation_baseline_20260623_112324.json` | June PASS reference |

| Signal | Pre-fix FAIL | June PASS |
|--------|--------------|-----------|
| 64-bar record | PASS (49152 ticks) | PASS |
| `STOPPED_RECORDING → PLAYING` | 1 | 1 |
| `PLAYING → OVERDUBBING` | 0 | 2 |
| `PERS,result` | 0 | 8 (`ok`) |
| Post-stop serial tail | ~198 ms stall; no ODUB | Overdub ST ~158 s after record stop |
| Seal heap (RAM2) | 28672 B | 16384 B |

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
