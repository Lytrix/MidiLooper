# OpenSpec 5.6 — Large-loop persist bounds verification

**Status:** **5.6 SHIPPED** (2026-09-11)  
**OpenSpec:** `immutable-loop-snapshot-passes` task **5.6**  
**Depends on:** [5.5 plan](immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md) (SHIPPED), [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)  
**Kind:** verification (no firmware owner change)

## North star (task 5.6)

Verify **large-loop save and migration progress**, **overrun/failure preservation**, **active-snapshot availability during save**, and **absence of content-sized synchronous work** at record/overdub **stop**, **`handleMidiInput()`**, **clock dispatch**, and **track-tick** paths.

5.5 proved owner + sliced runtime execute. 5.6 proves the **bounds contract** on rows **14** / **15** and timing-critical call sites.

## Architecture gate (Phase 4 / task 5.6)

| Question | Answer |
|---|---|
| **Owner module** | `StorageManager::processDeferredSaveState` / `LoadLoopJob` (rows 14/15); stop admits via `requestDeferredSaveState` only |
| **Primary invariant** | Save/load/migration advance in declared deferred units; failure keeps prior snapshot + valid persisted state; consumers keep reading published snapshot while save is pending; stop/input/clock/tick paths do not run content-sized persist |
| **Ownership change?** | NO |
| **State transition change?** | NO |
| **Execution bound change?** | NO — verify existing rows 14/15 |
| **Behavior-preserving?** | YES |
| **Reuse** | YES — extend HITL `base` verify + host gates + native matrix from 5.3/5.4/5.5 |
| **Phase scope** | Task 5.6 verification only |

### Architecture checkpoint

1. **Ownership?** NO  
2. **State transitions?** NO  
3. **Unbounded interval owner?** NO

## Verification matrix

| Requirement | Gate | Status |
|---|---|---|
| Save progresses in LoopPersist phases | HITL: `start` → `payload_slice*` → `payload_done` → `finalize_slice*` → `done` | **PASS** [`203803`](../../captures/session_20260911_203803.log) 5/5 windows |
| Per-turn persist slice bounded (not full-loop sync) | HITL: max `DIAG,persist_save` ≤ **100 ms**; max `PERS,work` ≤ **100 ms** per line | **PASS** max_pers=44624 µs, max_save=44931 µs |
| Migration progress (save-time LSV1) | Native `test_legacy_slot_migrates_to_lsv1_after_loaded_hydration` | **PASS** (`test_storage_loop_io`) |
| Failure / reject preserves state | Native `test_lsv1_rejects_*`, `test_persistence_failure_policy` | **PASS** (47 cases) |
| Active snapshot during save | HITL: `playback_build,snapshot` or `MO` within ±500 ms of each LoopPersist window while PLAYING/OVERDUBBING | **PASS** |
| Stop path admits only (no execute) | Source gate: `TrackCaptureStopCommit.cpp` has `requestDeferredSaveState` / `admitLoopSlotPersist`, no execute | **PASS** |
| No execute on input/clock/tick | Source gate: `MidiHandler`, `ClockManager`, `TrackManagerTransportTick`, stop path | **PASS** |
| Record stop defers save | HITL: `RECS,stage,save_request` outcome `requested`; slowest stop stage ≤ **100 ms** | **PASS** slowest publish=1683 µs |
| Row 14/15 substeps documented | `ExecutionBounds.md` unchanged | **PASS** |

**Threshold note:** 100 ms caps catch **content-sized** stop-stack or interval-path persist (historical 73–83 ms+ finalize classes). Empirically bounded SD steps such as `VerifyAndRename` (~44–48 ms in [`203803`](../../captures/session_20260911_203803.log) / [`200657`](../../captures/session_20260911_200657.log)) remain allowed on the deferred remainder path.

**Active snapshot note:** Idle or STOPPED persist windows (slot hydration, post-stop saves) do not require MO/`playback_build,snapshot`. Playback-active windows use ±500 ms padding so short persist gaps between MO ticks still pass when PLAYING continues.

## Stages

| Stage | Scope | Status |
|---|---|---|
| **5.6.1** | Host gates + HITL verifier (`deferred_persist_large_loop_bounds`) | **SHIPPED** |
| **5.6.2** | Native matrix sign-off (`pio test -e native` filters) | **SHIPPED** — **1823/1** |
| **5.6.3** | HITL `base` PASS | **SHIPPED** [`203803`](../../captures/session_20260911_203803.log) / report [`203843`](../../captures/host_midi_automation_baseline_20260911_203843.json) |
| **5.6.4** | Close `tasks.md` 5.6 + runtime docs | **SHIPPED** |

### Device / host evidence

| Step | Evidence | Result |
|---|---|---|
| Host call-site + unit tests | `.venv/bin/python scripts/test_deferred_persist_nonblocking.py` (12 tests) | **PASS** |
| Native persistence matrix | `test_storage_loop_io`, `test_persistence_sync_drain_budget`, `test_persistence_failure_policy` | **PASS** (47 cases) |
| Full native | `pio test -e native` | **PASS** **1823/1** |
| HITL `base` (canonical sign-off) | [`203803`](../../captures/session_20260911_203803.log) / report [`203843`](../../captures/host_midi_automation_baseline_20260911_203843.json) | **PASS** — full `base` (2+2+second OD + undo/redo); `VERIFY deferred persist large-loop bounds` `windows=5 progress=True max_pers=44624us max_save=44931us active=True stop=True ok=True`; `persistence_handoff result=ok` |
| Prior verify-only replay | [`200657`](../../captures/session_20260911_200657.log) / report [`202341`](../../captures/host_midi_hitl_verify_20260911_202341.json) | **PASS** (13 windows; pre-live-run regression anchor) |

## Explicit non-goals (5.6)

- Removing `handleClearTrack` / SavedSet `saveState()` drains (document only; bounded sync drain is maintenance)
- Boot sync load LSV1 (`CurrentSetBootLoad`)
- Long-run bar threshold HITL (`--record-bars 48+`) — optional follow-up; 2-bar `base` + native matrix suffice for 5.6 closeout
- Dropping `PersistedLoopSnapshot` dual-write (6.x)
- New persist owner or scheduler

## Pre-implementation review

### Proceed?

**YES** — verification only; extend 5.5 host/HITL patterns.
