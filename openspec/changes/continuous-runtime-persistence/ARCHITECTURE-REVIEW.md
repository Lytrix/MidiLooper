# Architecture review — continuous runtime persistence

**Change:** `continuous-runtime-persistence` (DEC-020)  
**Date:** 2026-07-08 (review artifact restored)  
**Status:** Active — Phases 0–4 shipped (native); Phase 4 HITL gate pending; **Phase 5 next**

**Related:** [proposal.md](proposal.md), [design.md](design.md), [tasks.md](tasks.md), [RUNTIME_STORAGE_AND_PERSISTENCE.md](../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md)

Restores the per-change **architecture + implementation review** pattern. Global templates: [`PREFLIGHT.md`](../../../docs/templates/PREFLIGHT.md), [`reviewer.md`](../../../docs/agents/reviewer.md).

**Orthogonal:** [DEC-023 `unified-capture-commit-owner`](../unified-capture-commit-owner/ARCHITECTURE-REVIEW.md) — capture **commit** at stop; this change owns **persistence scheduling** and **recovery load**. Commit owner must not block mid-pass slices.

---

## Governing invariants (DEC-020)

1. Runtime ownership independent of persistence state  
2. Sealed chunk immutable  
3. Persistence cooperative and budget-driven  
4. Persistence preserves seal order  
5. Runtime recording/playback precede persistence  
6. Memory reclaim independent of persistence completion  

---

## Finding → phase map

| Severity | Finding | Phase | Capability |
|----------|---------|-------|------------|
| Critical | `isCaptureActiveForPersistence()` starves writer entire overdub | 3 | `persistence-scheduler` |
| Critical | Pass-close-gated first byte to SD — pool fills during capture | 2–4 | `persistence-lifecycle`, mid-pass writer |
| High | No seal-order queue — duplicate or out-of-order persist | 2 | `persistence-lifecycle` |
| High | Chunk runtime vs persist state conflated | 1 | `chunk-manager` |
| High | Load assumes full pass on disk — partial mid-pass crash loses slot | **5** | recovery prefix |
| Medium | No `oldestDirtyChunkAge` / pressure telemetry before behavior change | 0 | `persistence-diagnostics` |
| Medium | Backpressure undefined at `CHUNK_RESERVE` | 4 | `persistence-failure-policy` |
| Medium | 64+64 HITL not re-run after Phase 4 | **6** | `long-record-memory-headroom` |

---

## Evidence anchors (code)

| Concern | Primary location |
|---------|------------------|
| Transport gate (removed Ph 3) | `StorageManager::processDeferredSaveState`, `isCaptureActiveForPersistence` |
| Persistence queue | `LoopEventStore`, `StorageManager` mid-pass admission |
| Mid-pass writer | `stepMidPassChunkPersist`, seal journal `.sealj` |
| Chunk lifecycle | `test_chunk_lifecycle`, `LoopEventStore` seal |
| Cooperative budget | `PersistenceBudget`, `main.cpp` idle slice |
| Deferred save FSM | `StorageLoopIo::stepDeferredLoopPersist` |
| Load / recovery | `StorageManager::loadState`, `readLoopPersisted`, quarantine helpers |
| Diagnostics | `#CAP,PERS,diag`, `#CAP,PERS,pressure` |

---

## Per-phase gates

Phases 0–3: **retrospective** — shipped; review documents what was verified. Phases 4–6: **active** gates before closeout.

### Phase 0 — Diagnostics (shipped)

| Architecture gate | Ownership change: **NO** | Transition change: **NO** |
| Implementation review | [x] `PERS,diag` on 64+64 capture | [x] `pio test -e native` |
| **Approval** | APPROVE (retrospective) |

---

### Phase 1 — Chunk lifecycle (shipped)

| Architecture gate | Extend `LoopEventStore` — **NO** new Manager (DEC-008) |
| Implementation review | [x] `test_chunk_lifecycle` |
| **Approval** | APPROVE (retrospective) |

---

### Phase 2 — Persistence queue (shipped)

| Architecture gate | Seal-order enqueue; exactly-once admission |
| Implementation review | [x] `test_persistence_queue` |
| **Approval** | APPROVE (retrospective) |

---

### Phase 3 — Cooperative scheduler (shipped)

| Architecture gate | **YES** — scheduling ownership; runtime precedence invariant |
| Implementation review | [x] 16-bar HITL overdub stop save (`f0ee520`) | [x] native |
| **Approval** | APPROVE (retrospective) |

---

### Phase 4 — Mid-pass persistence (shipped native; HITL gate open)

**Scope:** `stepMidPassChunkPersist`, seal journal, `PersistenceFailurePolicy`.

#### Architecture gate

| Question | Answer |
|----------|--------|
| Ownership change? | **YES** — pass open while chunks persist (documented DEC-020) |
| Conflicts DEC-023 commit owner? | **NO** — commit must not block slices |
| Edit-pass / undo continuous persist? | **NO** — out of scope v1 |

#### Implementation review

| Check | Pass |
|-------|------|
| `test_storage_loop_io` (mid-pass / journal paths) | [ ] |
| 64-bar record HITL — `#CAP,PERS,mid_pass` during capture | [ ] |
| `freeChunkCount` above `CHUNK_RESERVE` through capture | [ ] |
| `pio test -e native` | [x] |
| No regression: transport gate stays removed | [ ] |

**Approval:** APPROVE / REQUEST CHANGES

---

### Phase 5 — Recovery (next)

**Scope:** Load reconstructs **longest valid prefix** of persisted sealed chunks; quarantine invalid tail per SAVE-token policy.

#### Architecture gate (mandatory before implementation)

| Question | Required answer |
|----------|-----------------|
| Ownership change? | **YES** — load path owns prefix reconstruction |
| State transition change? | **YES** — partial pass visible after crash |
| Formal trigger? | **YES** — full PREFLIGHT or PR equivalent |
| Worst-case loss documented? | ≤ one unsealed **recording** chunk |
| DEC-021 boot defer? | **Amended** — [`slot-performance-interaction`](../slot-performance-interaction/) D21: queue **all** SD loop payloads cooperatively at boot (priority order only); still one slot per `processDeferredLoopSlotRestore` idle slice |
| DEC-022 bundle tail? | Verify meta temp / append cursor with partial prefix |

#### Design constraints

- Reuse `readLoopPersisted` / chunk stream where practical — **behavior** normative, layout flexible (design.md).
- Quarantine follows existing SAVE-token / `quarantineStorageFile` patterns.
- Native fixture: partial persisted pass (mid-pass journal prefix) before HITL depends on it.

#### Implementation review

| Check | Pass |
|-------|------|
| Native load fixture — partial persisted pass → longest prefix | [ ] |
| Invalid tail quarantined; boot does not fault | [ ] |
| Loaded slot matches runtime chunk refs for prefix only | [ ] |
| `pio test -e native` | [ ] |
| No full validate on load hot path beyond existing policy | [ ] |
| Guide updated: `RUNTIME_STORAGE_AND_PERSISTENCE.md` § Recovery | [ ] |

| HITL (smoke after native green) | |
|--------------------------------|--|
| Boot after simulated partial write (fixture SD or scripted) | [ ] optional before Phase 6 |

**Approval:** APPROVE / REQUEST CHANGES

---

### Phase 6 — Full 64+64 HITL (closeout)

**Scope:** End-to-end gate; archive change.

#### Architecture gate

| Question | Answer |
|----------|--------|
| All DEC-020 invariants satisfied on hardware? | Required **YES** |
| DEC-023 Phase 1–2 shipped? | Recommended before Phase 6 — commit/stop stability |

#### Implementation review

| Check | Pass |
|-------|------|
| 64+64 track 2 / slot 1 — writer advances during overdub | [ ] |
| `PERS,result,...,ok` after overdub stop | [ ] |
| No USB reboot / unexpected `HDR,v1` | [ ] |
| `SEVT`, `DISP` verification post-stop | [ ] |
| `oldestDirtyChunkAge` bounded under baseline | [ ] |
| `CURRENT_WORK`, `PROJECT_STATE`, `DELIVERABLE_TRACKING` updated | [ ] |
| DEC-020 entry complete (already in DECISION_LOG) | [x] |
| Archive (`/opsx:archive`) | [ ] |

**Approval:** APPROVE / REQUEST CHANGES

---

## Verification matrix (summary)

| Phase | Native | HITL | Review section |
|-------|--------|------|----------------|
| 0 | PASS | 64+64 diag capture | § Phase 0 |
| 1 | `test_chunk_lifecycle` | — | § Phase 1 |
| 2 | `test_persistence_queue` | — | § Phase 2 |
| 3 | PASS | 16-bar (`f0ee520`) | § Phase 3 |
| 4 | `test_storage_loop_io` | 64-bar mid-pass | § Phase 4 |
| 5 | partial load fixture | optional smoke | § Phase 5 |
| 6 | full suite | canonical 64+64 | § Phase 6 |

---

## Agent workflow (each remaining phase)

1. Load [CURRENT_WORK](../../../docs/runtime/CURRENT_WORK.md), this file, `tasks.md`, [RUNTIME_STORAGE_AND_PERSISTENCE.md](../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md).
2. Complete **Architecture gate** for target phase.
3. Implement; do not expand into DEC-023 commit-owner scope.
4. Run verification matrix row.
5. [reviewer.md](../../../docs/agents/reviewer.md) checklist → APPROVE / REQUEST CHANGES.
6. Check off `tasks.md`.

---

## Cross-change coordination

| Partner change | Coordination |
|----------------|--------------|
| **DEC-023** capture commit owner | Mid-pass writer runs during open pass; `finalizeCommitSideEffects` resets mid-pass state on **publish** only |
| **runtime-derived-representation-heap** | Heap routing parallel; persistence patches there **parked** |
| **DEC-021** boot slot defer | **Amended** by [`slot-performance-interaction`](../slot-performance-interaction/) D21 — exhaustive cooperative queue at boot; incremental restore unchanged |
