# Architecture review — published capture / builder split (M7)

**Change:** `runtime-derived-representation-heap` — **M7**  
**Date:** 2026-07-15  
**Status:** Active — Phase 0 doc gate **complete**; Phase 1 implementation **next**  
**Plan:** [`docs/plans/published_pass_capture_builder_split_refinement.md`](../../../docs/plans/published_pass_capture_builder_split_refinement.md)

**Related:** [design.md](design.md) § M7, [tasks.md](tasks.md) § M7, [specs/published-capture-pass-split/spec.md](specs/published-capture-pass-split/spec.md)

**Orthogonal:** Capture **commit** owner ([DEC-023](../unified-capture-commit-owner/ARCHITECTURE-REVIEW.md)); persistence scheduling ([DEC-020](../continuous-runtime-persistence/ARCHITECTURE-REVIEW.md)). M7 does **not** change seal/publish FSM transitions — only **representation** at existing boundaries.

**Supersedes:** Phase 2A blanket/split-tier typedef swaps (reverted — see [`memory_pressure_reclaim_conclusions_refinement.md`](../../../docs/plans/memory_pressure_reclaim_conclusions_refinement.md)).

---

## Governing invariants (M7)

1. **Capture is mutable and latency-sensitive** — internal heap; zero domain transitions on append.  
2. **Published passes are immutable and storage-oriented** — external memory pool metadata.  
3. **Type system enforces the split** — `CaptureChunkIdList` ≠ `PublishedChunkIdList`.  
4. **One domain transition at seal** — publish moves within published domain only.  
5. **No published → capture repatriation** — except empty new capture session.  
6. **Primary success = ownership** — RAM gain is secondary; exit criterion ~5 KiB under 215312.

---

## Evidence anchors (code)

| Concern | Primary location |
|---------|------------------|
| Capture append (hot) | `Loop::appendCaptureEvent`, `LoopEventStore::append` |
| Seal / publish | `Loop::sealCapture`, `commitPendingCapturePass`, `commitCapturePass` |
| Pending staging | `PendingCapturePass`, `hasPendingCapturePass`, `discardPendingCapturePass` |
| Published passes | `LoopPasses::recordPass`, `overdubPasses`, `LoopPasses::materialize` |
| Transfer (sole domain cross) | `transferCaptureChunkIdsToPublished` (Phase 1) |
| SD I/O | `StorageLoopIo`, `readPersistedLoopSnapshot` |
| Clone / undo | `deepClonePasses`, `sharePassesSnapshot`, `setCapturePassState` |

---

## Per-phase gates

### Phase 0 — OpenSpec + plan (doc-only) — **COMPLETE 2026-07-15**

| Architecture gate | |
|-------------------|---|
| **Owner module** | `Loop` (seal/publish), `LoopEventStore` (transfer helper), `LoopPasses` (storage) |
| **Primary invariant** | CaptureBuilder internal; published chunk ids external; one transition at seal |
| **Ownership change?** | **NO** — representation split at existing boundaries |
| **State transition change?** | **NO** — same seal → pending → publish → clear builder |
| **Behavior-preserving?** | **YES** |
| **Reuse decision** | **YES** — extend `sealCapture` / `commitPendingCapturePass` |

| Implementation review | |
|-----------------------|---|
| Spec delta | [x] `specs/published-capture-pass-split/spec.md` |
| Plan doc | [x] `published_pass_capture_builder_split_refinement.md` |
| Firmware edits | [ ] none (Phase 0) |

**Approval:** APPROVE — proceed to Phase 1 firmware

---

### Phase 1 — Type split + seal transfer — **NEXT**

**Scope:** `CaptureChunkIdList`, `PublishedChunkIdList`, transfer helper, pending at seal, publish move; **not** `CommittedOverdubPassVec` yet.

#### Architecture gate

| Question | Answer |
|----------|--------|
| Owner module | `Loop`, `LoopEventStore` |
| Primary invariant | One alloc + one linear copy on seal; zero transitions on append |
| Ownership change? | **NO** |
| Transition change? | **NO** |
| Behavior-preserving? | **YES** |
| Reuse | **YES** — extend `sealCapture`, `commitPendingCapturePass` |
| Phase scope | `LoopEventStore.h/cpp`, `LoopPasses.h`, `Loop.cpp` + native tests |

#### Implementation review (before checkoff)

| Check | Pass |
|-------|------|
| `pio test -e native` (`test_loop_event_store`, `test_loop_take_survival`) | [ ] |
| Append path never references `PublishedChunkIdList` | [ ] |
| Transfer helper: ≤1 alloc, ≤1 copy, no temp vectors | [ ] |
| No `abort()` on seal extmem failure | [ ] |

---

### Phase 2 — `CommittedOverdubPassVec` + call sites

#### Architecture gate

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| Transition change? | **NO** |
| Behavior-preserving? | **YES** |
| Phase scope | `LoopPasses.h`, `LoopPasses.cpp`, persistence/reclaim call sites |

#### Implementation review

| Check | Pass |
|-------|------|
| `pio test -e native` full suite | [ ] |
| `teensy41-capture-serial` build | [ ] |

---

### Phase 3 — SD load / snapshot / undo alignment

#### Architecture gate

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| Forbidden pattern | Published → capture repatriation on load/clone | 

#### Implementation review

| Check | Pass |
|-------|------|
| `test_storage_loop_io`, `test_sd_load_adopt`, undo/snapshot tests | [ ] |

---

### Phase 4 — HITL + exit measurement

#### Architecture gate

| Question | Answer |
|----------|--------|
| Primary pass | Ownership invariants + 0 append failures + stop completes |
| Secondary pass | Heap at overdub publish vs 215312 baseline |
| Exit criterion | < ~5 KiB recovery + no pressure improvement → scope down further migration |

#### Implementation review

| Check | Pass |
|-------|------|
| HITL baseline preset | [ ] |
| 215312-profile manual capture | [ ] |
| `LoopPersist,done,ok` + persistence restore | [ ] |
| NOTE_EDIT smoke (no sluggishness) | [ ] |
| Exit measurement documented in session notes | [ ] |

---

## Relationship to reverted Phase 2A

| Phase 2A (reverted) | M7 (this review) |
|-------------------|------------------|
| Single `ChunkIdList` → extmem | Dual typedefs; capture stays internal |
| Hot path touched | Hot path isolated |
| `abort()` on dual exhaustion | Graceful seal failure |
| Primary goal: RAM | Primary goal: **ownership** |
