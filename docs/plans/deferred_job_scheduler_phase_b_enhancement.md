# Deferred job scheduler — Phase B enhancement

**Kind:** enhancement  
**OpenSpec:** `deferred-job-scheduler`  
**North star:** [`deferred_job_scheduler_architecture.md`](deferred_job_scheduler_architecture.md)  
**Predecessor:** Phase A archived as `openspec/changes/archive/2026-07-18-unified-commit-lazy-slot-load/`  
**Evidence baseline:** [`session_20260718_230145.log`](../../captures/session_20260718_230145.log)

## Goal

Move deferred **load** execution ownership to `DeferredJobScheduler::runFrame` without changing Phase A realtime/Commit behavior.

## Phases

| Step | Scope | Behavior-preserving? |
|------|-------|----------------------|
| B.1 | Thin `runFrame` → domain step; `main` switches | YES — gate [`231510`](../../captures/session_20260718_231510.log) |
| B.2 | Explicit `StorageManager::stepSubmittedLoadJobs` called only from scheduler | YES |
| B.3 | Scheduler-owned active/parked selection (`selectSubmittedLoadJobs` then step) | YES |
| B.4 | Native `LoadLoopSelectionPolicy` tests; device gate vs `230145` | YES |
| Later | SaveLoopJob shared frame | Separate change |

## Non-goals

Save/display/export jobs; changing Commit; PSRAM walk regressions.

## Success

Native green; device gate matches `230145` (buttons 1:1; no ~295ms parse clusters).
