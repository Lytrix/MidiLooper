## Why

July 2026 HITL on `derived-note-overlap-logic` proved the 16-bar hang and 64-bar failures are **derived-representation scheduling and admission bugs**, not a missing heap↔PSRAM FIFO. Commit `cdd9c2b` fixed stop→PLAYING freeze (lazy internal-heap flatten + blocking Serial on MO). Remaining gaps:

- **64-bar record** completes stop→PLAY but **8 KB** RAM1 at stop; `PERS,defer heap_floor` blocks save because dispatch gates on a **frozen stop-path `admissionHeap` snapshot** instead of current internal heap.
- **64+64** overdub completes on device but HITL verifier misses `#CAP,ST,OVERDUBBING,PLAYING` when the capture ring drops tier-A lines under MO burst (`RING,overflow`).
- **`passesMaterializedStore_`** published flat still lazy-flattens to **internal heap** on `Loop::midiEvents()` — the main RAM1 drain for long loops after Phase B idle seed.

This change consolidates the never-shipped Cursor plan `64bar_regression_commit_analysis_b1378b37`, July heap-recovery work, and [`docs/plans/64bar_regression_commit_analysis_enhancement.md`](../../../docs/plans/64bar_regression_commit_analysis_enhancement.md) into one OpenSpec track aligned with **DEC-016** four-layer derived views.

**Not in scope (M1–M4):** long-loop display window (`long-loop-piano-roll-window`), NOTE_EDIT geometry, bisect script revival (DEC-017 parked).

**M5 spike (documented follow-up):** SD load / pass-clone internal-heap path — [`spike_sd_load_extmem_routing.md`](spike_sd_load_extmem_routing.md), DEC-019.

## What Changes

- **Admission fix (M1):** deferred save dispatch gates on **`getInternalHeapFreeBytes()` at dispatch time**; stop-path `admissionHeap` is telemetry only (`PERS,request`, `RECS` stages).
- **Published flat extmem (M2):** `passesMaterializedStore_` and remaining PLAYING/display hot paths use `SessionMidiEventVec`; `editAwareMidiEvents()` copy boundary at Track/EditManager when session store differs.
- **Capture ring hardening (M3):** tier-A `#CAP` lines (ST, PERS, RECS) flush before MO sampling during long overdub; raised flush budget when `RING,overflow` pending.
- **HITL gates (M4):** re-open selective 64-bar record-only and 64+64 gates; record-stop heap floor **telemetry-only** (default 0, optional warn at 12 KB).
- **No FIFO module** — authoritative storage stays chunk refs + passes; derived reps remain revision-keyed, window-bounded extmem builds (DEC-016).

Phase A→C + `cdd9c2b` capture-serial ring work is **done**; tasks mark complete with commit cites.

**M6 (2026-07-14):** Complete DEC-016 derived-representation policy across runtime playback paths (legacy callsite migration — **not** streaming playback). Phase 0 audit complete; additional legacy paths documented in plan. Exit criteria: no unintended hot-path materialize, manual + HITL gates PASS. `#CAP,DIAG,heap` cancelled (~50 KB RAM1). Plan: [`docs/plans/multi_track_playback_pressure_closure_refinement.md`](../../../docs/plans/multi_track_playback_pressure_closure_refinement.md).

## Capabilities

### New Capabilities

- `derived-representation-scheduling`: playback window and published flat builds SHALL NOT call lazy internal-heap `Loop::midiEvents()` on PLAYING entry; at most one full materialize per `playbackRevision` off hot path; tier-A `#CAP` SHALL NOT block USB.

### Modified Capabilities

- `long-record-memory-headroom`: amend admission to current-heap dispatch; distinguish stop-path nadir vs post-seal telemetry; relax 64-bar stop→PLAY gate to heartbeat + core transitions.
- `internal-heap-external-memory-routing`: published-loop flat cache and merge temporaries follow output vector allocator (extmem when `SessionMidiEventVec`).
- `hitl-automation`: `record_stop_min_free_ram2_bytes` default 0 (telemetry); optional warn threshold 12 KB.

## Impact

- Firmware: `StorageManager.cpp`, `Loop.cpp` / `Loop.h`, `Track.cpp` / `Track.h`, `EditManager.cpp`, `DebugSessionCapture.cpp`, `MidiHandler.cpp` (already gated).
- Guides: `DEFERRED_RUNTIME_PERSISTENCE.md`, `LOOP_MIDI_STORAGE_AND_VALIDATION.md` (admission paragraph).
- Verification: `pio test -e native`; HITL 64-bar record-only and 64+64 with floor=0.
- Supersedes: Cursor plans `64bar_regression_commit_analysis_b1378b37`, `heap_recovery_16bar_ebfaa9cd` (user-local `.cursor/plans/`).
- Brownfield: [`docs/00-authority/Architecture/DerivedViews.md`](../../../docs/00-authority/Architecture/DerivedViews.md), DEC-016/017/018.
