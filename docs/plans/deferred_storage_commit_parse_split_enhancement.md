# Deferred storage — Commit parse split (Phase A.6)

**Kind:** enhancement  
**Role:** Completes Phase A § A.6 of [`deferred_storage_time_budget_scheduler_enhancement.md`](deferred_storage_time_budget_scheduler_enhancement.md)  
**Branch:** `feature/deferred-lazy-load`  
**Evidence baseline:** [`session_20260718_220005.log`](../../captures/session_20260718_220005.log)

## Shipped (2026-07-18)

LoadLoopJob Commit is split:

| Phase | Work | Budgeted? |
|-------|------|-----------|
| Reading | Timed SD fill into `job.buffer` | Yes (`ReadChunkBytes` + deadline) |
| Parsing | `stepPersistedLoopSnapshotParse` → job `PersistedLoopSnapshot` | Yes (pass/edit grains under deadline) |
| Committing | Token + `applySnapshotToLoop` + mark persisted | One-shot (atomic; move-dominated) |

`Loop` is untouched until Committing. Phase transitions yield the frame (one expensive class of work per turn). Paint before every background-only load frame.

Telemetry: `#CAP,LLBG,parse_us` (only when > `BackgroundRestoreUs`), `#CAP,LLBG,apply_us`, `#CAP,LLBG,done`.

**Follow-up (2026-07-18, [`223130`](../../captures/session_20260718_223130.log)):** a single capture-pass grain still ran ~300ms (deadline checked only between passes). Fix: mid-pass event batches (`CHUNK_CAPACITY`) with one batch per main-loop parse turn so MIDI/buttons run between batches.
