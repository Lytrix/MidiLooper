# CurrentSet persist work-item queue enhancement

**Kind:** Architecture + implementation plan  
**Date:** 2026-07-14  
**Branch:** `feature/persistence-work-queue` from `dev` @ `ec7b4a5`  
**Status:** B1 shipped; B2 shipped; B3 shipped — B4 retire monolith next  
**Parent plan:** [SD Write Reduction](.cursor/plans/sd_write_reduction_910f40b3.plan.md)  
**Evidence:** [`captures/session_20260714_031454.log`](../../captures/session_20260714_031454.log) — ~8,400 `PERS,slice` lines per HITL baseline; `w1_s63` payload stats prove monolithic meta/undo sweep.

---

## Architecture overview (A7)

This change defines a **layered persistence architecture** — not only SD write reduction.

```text
Runtime
        │
        ▼
Domain transitions
        │
        ▼
Persistence admission          ← domain admits stale work only
        │
        ▼
Persistence scheduling         ← StorageManager + budget
        │
        ▼
Serialization                  ← WorkspaceSave / StorageLoopIo
        │
        ▼
Storage backend                ← SD files
```

Loop-centric view:

```text
Loop
        │
        │  "Persistence became stale"
        ▼
PersistenceWorkQueue
        │
        │  "When scheduler budget allows"
        ▼
StorageManager
        │
        │  "Serialize"
        ▼
WorkspaceSave
        │
        ▼
SD Card
```

Each layer owns **exactly one responsibility** (see § Layer invariants).

Compatible with DEC-024, Slot/Loop decoupling, loop libraries, and future serializer layouts without redesigning admission.

---

## Persistence subsystem ownership

The persistence queues and scheduler are **owned exclusively by `StorageManager`**.

Neither `Track`, `Loop`, `TrackUndo`, nor any other domain object owns queue state, scheduling, budgeting, or retry policy.

`PersistenceWorkQueue` is a **private implementation detail** of the persistence subsystem — not a public API surface for domain code.

```text
Application
    │
    ▼
StorageManager
    │
    ├── owns PersistenceQueue (capture chunks)
    ├── owns PersistenceWorkQueue (semantic stale work)
    ├── owns persistence scheduler (processDeferredSaveState)
    ├── owns retry / backpressure policy
    ├── owns persistence budget
    │
    ├── coordinates WorkspaceSave (serialize)
    └── coordinates StorageLoopIo (SD I/O)
```

### Responsibility split

| Module | Responsibility |
|--------|----------------|
| **Track / Loop / TrackUndo** | Detect transitions; call **`StorageManager::admit*`** only |
| **StorageManager** | Admission forwarding, scheduling, draining, retry, flush, budget |
| **`PersistenceWorkQueue`** | Stores pending `PersistWorkItem`s only — **no scheduling** |
| **`WorkspaceSave`** | Serializes one work item into workspace artifacts |
| **`StorageLoopIo`** | SD card I/O for loop payloads |

### Queue visibility

`PersistenceWorkQueue` must **not** be visible outside `StorageManager` translation units.

- **Public:** [`StorageManager.h`](../../include/StorageManager.h) — semantic `admit*` methods only
- **Private:** `PersistenceWorkQueue.h` included from `StorageManager.cpp` / `StorageManagerInternal` only (or `namespace StorageManagerInternal`)

Domain code must not call `PersistenceWorkQueue::admitWork` directly.

```text
Track / Loop / TrackUndo
        │
        ▼
StorageManager::admitLoopPersist(loopId)
        │
        ▼
PersistenceWorkQueue   (internal)
```

This keeps the queue **replaceable** without modifying domain call sites.

### Public admission API (only surface for domain)

```cpp
// StorageManager.h
static void admitLoopPersist(LoopId loopId);
static void admitLoopUndoHistory(LoopId loopId);
static void admitSlotMeta(uint8_t trackIndex, uint8_t slotIndex);
static void admitTrackMeta(uint8_t trackIndex);
static void admitWorkspaceFooter();
static void admitGlobalMeta();
// FinalizeWorkspace: StorageManager scheduler internal only — not public
```

### Scheduler ownership

[`StorageManager::processDeferredSaveState`](../../src/StorageManager.cpp) remains the **single scheduler** for all persistence work.

The queue **never schedules itself**.

Order per iteration:

1. Revision load / commit work
2. Mid-pass chunk persistence (`PersistenceQueue`)
3. One `PersistenceWorkQueue` item slice
4. Idle (legacy monolith removed after B4)

All lifecycle decisions — budgeting, retries, flushing, shutdown, revision commits, admitting `FinalizeWorkspace` — stay **centralized in `StorageManager`**.

### Design rationale

Persistence scheduling is **infrastructure**, not musical behaviour.

- Tracks must not know whether persistence is queued, immediate, journaled, or disabled.
- Loops must not own persistence state.
- Workspace data does not schedule execution.
- The queue does not own its own lifecycle.

### Architectural invariant

> **StorageManager owns the persistence subsystem.**
>
> **`PersistenceWorkQueue` is a private scheduling structure owned by StorageManager.**
>
> **Track, Loop, and TrackUndo only publish stale persistence work through `StorageManager` admission APIs.**

Preserves separation between musical state ownership, persistence scheduling, serialization, and SD I/O.

---

## Motivation

Move from **poll-the-whole-workspace** (`requestDeferredSaveState` → full `DeferredSaveStage`) to **event-driven admission** aligned with shipped [`PersistenceQueue`](../../include/PersistenceQueue.h) (capture chunks).

Capture chunks already use mid_pass (`PERS,mid_pass`). Semantic workspace work does not.

---

## Final architecture refinements (A1–A9)

### A1 — `PersistKey` abstraction

Work items must not expose heterogeneous key styles directly to queue internals.

```cpp
struct PersistWorkItem {
    PersistWorkType type;
    PersistKey key;
};
```

`PersistKey` is a **tagged key** (union / variant) — queue does not care which domain object owns the work:

| Key kind | Carries (today) |
|----------|-----------------|
| `LoopId` | `loopId` |
| `Track` | `trackIndex` |
| `Slot` | `trackIndex` + `slotIndex` *(interim UI assignment)* |
| `Singleton` | no sub-key (`GlobalMeta`, `WorkspaceFooter`, `FinalizeWorkspace`) |

Admission API stays ergonomic (`admitLoopPersist(loopId)`) — wrappers build `PersistKey`.

Future domains (library, revision export) add key variants without queue redesign.

### A2 — Admission never serializes

**Invariant:**

```text
Admission never serializes.
Serialization never decides what became stale.
```

| Layer | Responsibility |
|-------|----------------|
| Loop / Track / TrackUndo | Signal stale persistence (`admit*`) |
| `StorageManager` | Schedule when / how much per slice |
| `WorkspaceSave` / `StorageLoopIo` | Serialize to bytes + paths |

```text
Loop → admitLoopPersist()
StorageManager → stepPersistenceWorkItem()
WorkspaceSave → serialize*
```

### A3 — Eventual consistency

**Invariant:**

```text
Immediately after a domain transition, runtime state is authoritative.
Persistence converges asynchronously within scheduler budget.
```

Budgeted slices are **correct by design** — not a limitation to paper over with synchronous SD writes.

### A4 — Batching / coalescing semantics

The queue tracks **outstanding stale persistence work**, not individual mutations.

```text
admit LoopPersist(loop42)   ×3 before drain completes
        ↓
one persisted write for loop42
```

**Invariant:** Repeated admission of the same `(type, PersistKey)` while `Queued` or `Writing` is a no-op — semantic batching (mirror `admitSealedChunk`).

### A5 — Stale persistence wording

Prefer:

```text
Loop persistence became stale.
```

over “loop changed” or per-mutation event names.

The queue records that **on-disk representation may be behind RAM** — not every edit as a separate queue entry.

### A6 — Serializer independence

**Invariant:**

```text
Persistence work items remain valid when serialization format changes.
```

Footer layout, loop files, library storage, or future backends change **serializer only** — admission APIs unchanged.

### A7 — Overview diagram

See § Architecture overview (top of this doc).

### A8 — Queue naming: `PersistenceWorkQueue`

| Avoid | Use |
|-------|-----|
| `PersistWorkQueue` | **`PersistenceWorkQueue`** |

Aligns with existing `PersistenceQueue`, `PersistenceFailurePolicy`, and StorageManager persistence APIs.

Implementation files: `src/PersistenceWorkQueue.cpp`, internal header (StorageManager-only include), `test/test_persistence_work_queue/`. **No** public `include/PersistenceWorkQueue.h` for domain modules.

### A9 — Delay queue unification

**Initial implementation keeps two queues:**

| Queue | Role |
|-------|------|
| [`PersistenceQueue`](../../include/PersistenceQueue.h) | Sealed capture chunks → mid_pass |
| **`PersistenceWorkQueue`** | Semantic stale work → meta / loop / undo / footer |

**Rationale:** Lower implementation risk, easier regression bisection, independent validation.

Convergence behind one scheduler (R8 future) — **no merge in B1–B5**.

---

## Architecture review (R1–R9, 2026-07-14)

### R1 — Name queue after responsibility

**`PersistenceWorkQueue`** — scheduled persistence **work**, not a storage domain.

### R2 — Semantic items, not files

| Good | Bad |
|------|-----|
| `LoopPersist` + `PersistKey(LoopId)` | `loop_TT_SS.bin` |
| `LoopUndoHistory` + `PersistKey(LoopId)` | `undo_track_2` |

Scheduler + serializer choose files.

### R3 — Loop-centric musical state

`LoopPersist` admit key = **`loopId`**. Drain resolves `loopId → assignment → path` (1:1 era today).

`SlotMeta` + `PersistKey(Slot)` interim for UI assignment only.

### R4 — `LoopUndoHistory` not `UndoTrack`

Owner key = **`loopId`** from B1. Item type expresses loop-owned undo history (DEC-024). Drain wire updates in B6.

### R5 — Scheduler ownership

Domain **admits only**. `StorageManager` schedules. No `requestDeferredSaveState` from domain after migration.

### R6 — `FinalizeWorkspace`

Scheduler-only. Epoch + `workspace.bin` + clear workspace dirty — domain finalization, not “queue empty hook”.

### R7 — Stale loop persistence (see A5)

One `LoopPersist` admission for any cause: record, overdub, undo, redo, clear, edit, import, future transforms.

### R8 — Long-term convergence (deferred per A9)

Future single work model may absorb chunk items; not B1–B5 scope.

### R9 — No recursive admission

Drain / serialize paths **never** admit work. **Exception:** scheduler admits `FinalizeWorkspace` when appropriate.

---

## Final architecture refinements (F1–F3, 2026-07-14)

### F1 — `LoopUndoHistory` not `UndoHistory`

The work item name must express that persistence ownership is the **musical Loop**, not the Track.

| Avoid | Use |
|-------|-----|
| `UndoHistory` | **`LoopUndoHistory`** |

Public API: `StorageManager::admitLoopUndoHistory(loopId)`.

Work item catalog:

```text
LoopPersist
LoopUndoHistory
TrackMeta
SlotMeta
GlobalMeta
WorkspaceFooter
FinalizeWorkspace
```

Aligns with DEC-024 loop-owned undo. B6 updates **serializer wire only** — type and key unchanged from B1.

### F2 — FIFO ordering within `PersistenceWorkQueue`

Scheduler priority between **systems** is unchanged (revision → chunks → work queue). **Within** `PersistenceWorkQueue`:

> **Invariant:** FIFO ordering for **distinct** work items. Re-admission of an already `Queued` or `Writing` `(PersistWorkType, PersistKey)` refreshes stale state but **does not** change queue position.

Example:

```text
Queue:  LoopPersist(loopA) → TrackMeta(track2) → LoopPersist(loopB)

Re-admit LoopPersist(loopA) before write:
        LoopPersist(loopA) → TrackMeta(track2) → LoopPersist(loopB)   (loopA stays at head)
```

Prevents starvation when one loop is edited repeatedly; keeps deterministic drain order.

**B1 test:** admit A, B, C; re-admit A; drain order remains A, B, C.

### F3 — Serializer idempotence

> **Invariant:** Serializing the same `PersistWorkItem` multiple times **without intervening domain changes** must produce the same persistent state.

Enables:

- safe retries after interrupted writes
- duplicate scheduler slices without corruption
- deterministic replay after restart (future)

Scheduling and serialization stay decoupled — idempotence is a **serializer contract** (`WorkspaceSave` / `StorageLoopIo`), validated in native tests where feasible.

---

## Summary of invariants

1. Admission never serializes.
2. Serialization never determines stale state.
3. Runtime is authoritative; persistence is eventually consistent.
4. Queue tracks **stale persistence work**, not each mutation.
5. Duplicate admission = semantic batching (same item stays one queue entry).
6. Admission is independent of serializer layout.
7. Each architectural layer has exactly one responsibility.
8. **StorageManager** exclusively owns the persistence subsystem; **`PersistenceWorkQueue`** is private to it.
9. **`PersistenceWorkQueue`** preserves **FIFO** order for distinct items; re-admit does not move position (F2).
10. Serializing the same work item without intervening domain change is **idempotent** (F3).
11. **`LoopPersist`** and **`LoopUndoHistory`** are loop-domain stale signals — independent of current slot assignment (F1).

---

## Work item catalog

| `PersistWorkType` | `PersistKey` | Stale meaning | Serializer (today) |
|-------------------|--------------|---------------|---------------------|
| `GlobalMeta` | Singleton | Transport / BPM / master length / looper state | `CurrentSetMeta` |
| `TrackMeta` | Track | Track state / mute | track header |
| `SlotMeta` | Slot *(interim)* | Slot enabled / muted / assignment row | slot meta triplet |
| `LoopPersist` | LoopId | Loop on-disk representation stale | `loop_TT_SS.bin` + journal finalize |
| `LoopUndoHistory` | LoopId | Loop undo history stale | undo section *(interim wire)* → per-loop footer (DEC-024) |
| `WorkspaceFooter` | Singleton | Selection / active indices stale | footer slice |
| `FinalizeWorkspace` | Singleton | Workspace commit pending | epoch + `workspace.bin` |

**Lifecycle per item:** `NotScheduled → Queued → Writing → Persisted`.

**FIFO (F2):** Distinct items drain in admit order; re-admit same `(type, key)` does not reorder.

---

## Scheduler

Owner: [`StorageManager::processDeferredSaveState`](../../src/StorageManager.cpp).

Per iteration:

1. Revision load / commit (unchanged)
2. **`PersistenceQueue`** → `stepMidPassChunkPersist` *(chunks first — A9)*
3. **`PersistenceWorkQueue`** → `stepPersistenceWorkItem` (one budget slice)
4. Legacy monolithic save — removed after B4

Empty work queue + no in-flight work → **no SD I/O**.

`FinalizeWorkspace`: scheduler admits when drain completes or `saveState()` flushes.

---

## Epoch rules

- Domain admits work — does **not** bump `currentWorkspaceEpoch`
- **`FinalizeWorkspace`** serialization bumps epoch + writes `workspace.bin`
- `lastCommittedEpoch` on revision commit COMPLETE only ([`current-workspace/spec.md`](../../openspec/changes/set-revision-persistence/specs/current-workspace/spec.md))

---

## Transition matrix (B2)

| Transition | Admit (stale) |
|------------|----------------|
| Record / overdub publish | `LoopPersist`; `LoopUndoHistory` if stack touched |
| Undo / redo | `LoopPersist`, `LoopUndoHistory` |
| Clear slot | `LoopPersist`, `SlotMeta`, `LoopUndoHistory` if needed |
| Slot selection | `WorkspaceFooter` |
| Transport stop (playback only) | `GlobalMeta` if looper meta stale; else none |
| Slot enable / mute | `SlotMeta` |
| Track mute / state | `TrackMeta` |
| Edit autosave | `LoopPersist` per stale loop |
| Migration / repair | scheduler maintenance flush |

**Remove:** `handleTransportStop` mass `markCurrentSetLoopSlotDirty`.

---

## Implementation phases

| Phase | Deliverable |
|-------|-------------|
| **B0** | This doc ✅ |
| **B1** | `PersistenceWorkQueue` (internal) + `PersistKey` + `test_persistence_work_queue` | **Done** |
| **B2** | Public `StorageManager::admit*` only; deprecate `mark*` / `requestDeferredSaveState` | **Done** |
| **B3** | `stepPersistenceWorkItem()` — schedule only; R9; no serialize in queue | **Done** |
| **B4** | Scheduler wiring; retire monolith | **Next** |
| **B5** | Native + HITL |
| **B6** | DEC-024 `LoopUndoHistory` serializer wire only (type/key unchanged) |

### B1 sketch

```cpp
// PersistenceWorkQueue.h — StorageManager internal only (not included by Track/Loop)
enum class PersistWorkType : uint8_t { … };
struct PersistKey { … };
struct PersistWorkItem { PersistWorkType type; PersistKey key; };

namespace PersistenceWorkQueue {
  bool admitWork(PersistWorkType type, PersistKey key);
  bool beginWriteQueuedItem(PersistWorkItem& out);
  void markItemPersisted(const PersistWorkItem& item);
  uint16_t queueDepth();
}

// StorageManager.cpp — forwards admit* → PersistenceWorkQueue::admitWork
// StorageManager.h — public admit* only (see § Persistence subsystem ownership)
```

**B1 files:**

- `src/PersistenceWorkQueue.cpp` + internal header under `StorageManager/` or `include/StorageManagerInternal.h` forward declarations
- **Not** a top-level public include for domain modules
- `test/test_persistence_work_queue/` (native unit tests may include internal header)

---

## Architecture gate (before B1 firmware)

| Question | Answer |
|----------|--------|
| **Persistence subsystem owner** | **`StorageManager` only** — queues, scheduler, budget, retry, flush |
| **Queue visibility** | `PersistenceWorkQueue` **internal** — domain uses `StorageManager::admit*` only |
| Primary invariant | Stale semantic work → eventual SD convergence (A3) |
| Layer split | Admission (public admit*) / schedule (SM) / serialize (WorkspaceSave) / I/O (StorageLoopIo) |
| Musical ownership change? | **No** |
| Persistence layering change? | **Yes** (approved) |
| Two queues | **Yes** intentionally (A9) |
| R9 / A4 / F2 | Dedup batching without reorder; FIFO for distinct items; drain never admits; scheduler alone admits `FinalizeWorkspace` |
| F3 | Serializer idempotence — contract on WorkspaceSave; test where feasible |

---

## Verification

| Gate | Criteria |
|------|----------|
| `pio test -e native` | `test_persistence_work_queue` — dedup, **FIFO re-admit (F2)**, key equality, lifecycle |
| HITL | Fewer meta/undo slices; record/undo/redo/boot OK |
| Log | vs `session_20260714_031454.log` |

Telemetry: `#CAP,PERS,work,<type>,<key>,phase,ok|failed`

---

## Out of scope

- Merging `PersistenceQueue` into `PersistenceWorkQueue` (A9 / R8 future)
- Meta patch-in-place
- SavedSet revision commit policy
- SlotAssignment persist table

## Deprecated naming

- `PersistWorkQueue`, `WorkspacePersistQueue`
- `UndoHistory`, `UndoTrack`, `LoopSlotPayload`, `WorkspaceCompletion`
- Option A dirty-bitmap extension
- Wording “loop changed” → use “persistence became stale” (A5)
