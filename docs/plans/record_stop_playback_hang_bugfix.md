# Record-stop playback hang bugfix

**Status:** Implemented  
**Branch:** `continuous-saving` (e10701b+)  
**Evidence:** [`session_20260713_153703.log`](../captures/session_20260713_153703.log), [`session_20260713_153834.log`](../captures/session_20260713_153834.log)

## Problem

After first record stop → PLAYING, Teensy hung ~1–14s: ~900 MO/s retrigger storm (same note-ons repeated), then sluggish recovery at ~28 KB internal heap.

## Root causes

1. `atLoopStart` used `tickInLoop <= prevTickInLoop` — same transport phase re-fired all events every `loop()` iteration.
2. `ensurePlaybackWindowBuilt` called `loop.midiEvents()` (full materialize) on first PLAYING hot path after stop.
3. `reanchorPlaybackProjection` clobbered record-stop `lastTickInLoop` with `UINT32_MAX`.

## Fixes

| Area | Change |
|------|--------|
| `playMidiEvents` / `playMidiEventsForSlot` | `atLoopStart` uses `<` not `<=` |
| `ensurePlaybackWindowBuilt` | `mergeActiveCapturePasses` or `materializeToEventVector` when store fresh — no `midiEvents()` |
| `reanchorPlaybackProjection` | Preserve `lastTickInLoop` from record-stop rewind when `preserveLoopPhaseOrigin` |

## Phase 2 (session_160233 follow-up)

| Area | Change |
|------|--------|
| `reanchorPlaybackProjection` | Single `anchorPhase` for `projectionCycleStartTick` + `lastTickInLoop`; skip `nextEventIndex=0` when phase known |
| `IntervalProjection::isPlaybackAtLoopStart` | Fresh-origin UINT32_MAX catch-up vs preserve-path wrap-only |
| `playMidiEvents` stale reset | Do not zero index when `lastTickInLoop != UINT32_MAX` |
| `DisplayManager` | Warm cache via `liveDisplayCacheCommittedEnd`; `mergeActiveCapturePasses` on PLAYING path; `refreshViewportAfterRecordStop` |

## Phase 3 (session_162210 — continuous save + projection)

**Evidence:** [`session_20260713_162210.log`](../captures/session_20260713_162210.log) — 661 `PERS` slices in 3s, 1 `DISP` in 10s, ~525 MO/s ch4 after record stop.

| Area | Change |
|------|--------|
| `StorageManager::deferWorkspaceSaveDispatchDuringPlayback` | 2s grace (`Config::playbackSaveDispatchGraceMs`) before full `PERS,dispatch` while transport active; mid-pass persist unchanged |
| `Track::stopRecording` | `loop.ensureVisualCacheBuilt()` before PLAYING; `reanchorPlaybackAfterRecordStop` after `invalidatePlaybackCaches` |
| Verify script | [`scripts/verify_record_stop_post_play_metrics.py`](../../scripts/verify_record_stop_post_play_metrics.py) |

## Phase 4 — coordinate frame fix (session_163933 regression)

**Evidence:** [`session_20260713_163933.log`](../captures/session_20260713_163933.log) — `COORD` split `storage=248` vs `proj/display=1784`; playhead ~bar 3; MO ~285/s; `PERS,dispatch` at +6.7ms.

| Area | Change |
|------|--------|
| `Track::reanchorPlaybackProjection` | Stop sticky `playbackPreservePhaseOrigin_`; clear flag in `reanchorPlaybackAfterRecordStop` |
| `DisplayManager::resolvePlayheadInLoop` | Transport-active playhead uses `tickPhaseInLoop` (storage frame) |
| `DisplayManager::refreshViewportAfterRecordStop` | Center bounded window on explicit storage-phase tick |
| `Track::startOverdubbing` | Re-anchor `projectionCycleStartTick` + `lastTickInLoop` from current transport |
| `StorageManager` | `isTransportActiveForPersistence()` gates defer grace; defer before `requestDeferredSaveState` |
| Verify script | `coord_aligned` check on `#CAP,COORD` lines in first 2s after PLAYING |

## Phase 4c — MIDI flood fix (session_165557)

**Evidence:** [`session_20260713_165557.log`](../captures/session_20260713_165557.log) — 571 note-ons / 5 pitches in 704 ticks; BPM 120→46; `RING,overflow`.

| Area | Change |
|------|--------|
| `Track::reanchorPlaybackProjection` | Restored `901c4d9` baseline: unconditional `nextEventIndex=0` + `lastTickInLoop=UINT32_MAX`; preserve branch only recomputes `projectionCycleStartTick` |
| `Track::playMidiEvents` / `playMidiEventsForSlot` | Restored inline `atLoopStart = prev==MAX \|\| tick<=prev`; unconditional stale index reset |
| `ensurePlaybackWindowBuilt` | Restored `loop.midiEvents()` materialize path |
| `Track::stopRecording` | Restored `loop.nextEventIndex=0`; removed `reanchorPlaybackAfterRecordStop`; explicit storage-phase tick to `refreshViewportAfterRecordStop` |
| Display | Kept storage-frame `resolvePlayheadInLoop` + viewport refresh |
| Persistence | Kept `deferWorkspaceSaveDispatchDuringPlayback` + `isTransportActiveForPersistence` gate |

## Verification

- `pio test -e native`
- `pio test -e teensy41-capture-serial` build
- HITL: MO/s ≤ ~50 in first 2s after record stop (no 900/s burst)
- Post-fix log: `python3 scripts/verify_record_stop_post_play_metrics.py captures/session_*.log`
- COORD after PLAYING: `storage` ≈ `display` ≈ `proj` (within 1 tick); OLED playhead in bar 1 at stopped beat
