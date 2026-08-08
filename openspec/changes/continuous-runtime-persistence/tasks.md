# Tasks — continuous-runtime-persistence

Guide: [`docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md`](../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md).

**Parked on `runtime-derived-representation-heap`:** persistence starvation patches, `SC_REC_FLUSH` defer workarounds, transport-gate tweaks — superseded by this change.

## Phase 0 — Diagnostics only (no behavior change)

- [x] Add `#CAP,PERS,diag,...` for: `freeChunkCount`, `usedChunkCount`, queue depth (deferred-save proxy), chunks in `Writing`, transport/heap/budget block counts, slice done, peak writer latency, `oldestDirtyChunkAge` (dirty-save age proxy), max backlog, pending/inProgress/capture flags
- [x] Add `#CAP,PERS,pressure,...` when `freeChunkCount` approaches `CHUNK_RESERVE`
- [x] Emit transport-block count when `isCaptureActiveForPersistence()` prevents slice dispatch
- [x] **Gate:** 64+64 HITL track 2 / slot 1 — `20260707_202345`: 53× `PERS,diag`; `transportBlk` 0→97k during overdub; `dirtyAgeMs` →127s; `PERS,result,...,ok` (overall HITL FAIL unrelated: overdub_stored_count_short, capture_dedup)
- [x] `pio test -e native` — no regressions (473/473)

## Phase 1 — Chunk ownership and lifecycle

- [x] Introduce ChunkManager responsibilities on `LoopEventStore` (or documented extension): `Free` / `Recording` / `Sealed` runtime state per chunk
- [x] Enforce single mutable `Recording` tail per active capture store
- [x] Enforce sealed immutability — no post-seal mutation paths
- [x] Reference tracking for reclaim when all runtime owners release
- [x] **Gate:** native unit tests for seal immutability, single tail, runtime state independent of persistence state (`test_chunk_lifecycle`)

## Phase 2 — Persistence queue

- [x] Admit sealed capture chunks to persistence queue on seal (capacity trigger + pass-close tail)
- [x] Seal-order enqueue; exactly-once admission
- [x] Queue drain order = seal order
- [x] **Gate:** native unit tests for enqueue order, no duplicate admission (`test_persistence_queue`)

## Phase 3 — Cooperative budget-driven scheduler

- [x] Remove unconditional `isCaptureActiveForPersistence()` early return (replace with cooperative budget + runtime precedence)
- [x] Use `PersistenceBudget` active budget (~300 µs) during capture
- [x] Preserve one finite-state-machine sub-step per `processDeferredSaveState()` call
- [x] **Gate:** short HITL (16-bar record+overdub) — overdub stop save completes without USB disconnect (`f0ee520`)
- [x] **Prerequisite:** Phase 0 validated on 64+64; Phase 2 queue shipped

## Phase 4 — Continuous mid-pass persistence

- [x] Writer drains persistence queue during open capture pass (`stepMidPassChunkPersist`, seal journal `.sealj`)
- [x] Incremental slot append for sealed chunks while pass open (seal journal sidecar; full slot write on deferred save)
- [x] Implement failure-policy backpressure per `persistence-failure-policy` spec (`PersistenceFailurePolicy`, queue alarm, reserve prioritize)
- [x] **Gate (HITL):** 64+64 record/overdub on track 5 — [`captures/session_20260709_171043.log`](../../../captures/session_20260709_171043.log) (user, 2026-07-09): 282× `#CAP,PERS,mid_pass,...ok`; 9× `#CAP,PERS,result,...,ok`; `#CAP,PERS,diag` freeChunk 105→136 (reserve 16); `ST,Track,OVERDUBBING,PLAYING` @ line 49283
- [x] **Gate (HITL, boot restore):** cold boot reload of persisted 64+64 loop — [`captures/session_20260709_171951.log`](../../../captures/session_20260709_171951.log): `BOOT,load,ok`; `Current workspace loaded successfully`; deferred restore slot `4/0`; `DISP,0,...,41472,...` (589/176 notes); `ST,Track,STOPPED,PLAYING` after transport start
- [ ] **Gate (native):** `test_storage_loop_io` sign-off (agent)

## Phase 5 — Recovery

- [ ] Load reconstructs longest valid prefix of persisted sealed chunks
- [ ] Quarantine invalid tail per SAVE-token policy
- [ ] **Gate:** native load fixture with partial persisted pass

## Phase 6 — Full 64+64 HITL

- [x] 64+64 track 5 — writer advances during overdub — [`session_20260709_171043.log`](../../../captures/session_20260709_171043.log): `PERS,mid_pass` through overdub
- [x] `PERS,result,...,ok` after overdub stop — same log (e.g. lines 28069, 31248, 51035)
- [x] `SEVT`, `DISP` verification lines present post-stop — same log ~49271–49296
- [x] Cold-boot restore of persisted loop — [`session_20260709_171951.log`](../../../captures/session_20260709_171951.log)
- [ ] `oldestDirtyChunkAge` bounded under baseline conditions (not extracted from this capture)
- [x] Update `docs/Runtime/CURRENT_WORK.md`, `PROJECT_STATE.md`
- [ ] Append DEC-020 to `DECISION_LOG.md`
- [ ] Archive change when green (`/opsx:archive`)

## Docs (scaffold)

- [x] OpenSpec change folder: proposal, design, tasks, spec deltas
- [x] Agent guide: [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md)
- [x] Update [`DEFERRED_RUNTIME_PERSISTENCE.md`](../../../docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md) scheduler § after Phase 3 ships
- [x] Register in `PROJECT_STATE.md`, `CURRENT_WORK.md`
- [x] Park persistence patches on `runtime-derived-representation-heap/tasks.md`
