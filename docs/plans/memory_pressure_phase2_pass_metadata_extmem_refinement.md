# Memory pressure Phase 2 — pass metadata extmem routing (revised)

**Kind:** refinement  
**Date:** 2026-07-15  
**Parent:** [`memory_pressure_reclaim_refinement.md`](memory_pressure_reclaim_refinement.md)  
**Branch:** `feature/memory-pressure-reclaim`  
**OpenSpec:** [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

**Evidence (Phase 1B):** [`session_20260714_235620.log`](../../captures/session_20260714_235620.log) — 0× `Capture append failed`; internal heap floor ~4 KiB at publish; chunk pool healthy (446+ free); Phase 1B reclaim targets PSRAM-tier derived views, not internal-heap pass metadata.

---

## Revision summary

| Original Phase 2 | Revised Phase 2 |
|------------------|-----------------|
| `trimGlobalUndoStackForMemory` at Critical | **Skip** — record/overdub undo is already pass-centric (O(1) `passId` entries) |
| Pass reclaim + persist override + visual defer | **Keep** reclaim + persist override + visual defer |
| (implicit) more derived-cache reclaim | **Add** route pass **metadata** vectors off internal heap → external memory pool |

Phase 2 attacks the **internal heap axis** that Phase 1B does not touch. Phase 1B frees PSRAM playback/materialized caches; pass metadata (`ChunkIdList`, pass vectors, bar indices) still grows on `InternalHeapFirstAllocator` during long multi-track capture.

---

## Why skip undo trim

Record and overdub undo entries store **only** `passId` + slot geometry — not full pass snapshots:

- `TrackUndo::pushRecordPassAdded` / `pushOverdubPassAdded` → `UndoEntryKind::RecordPassAdded` / `OverdubPassAdded`
- Undo apply → `Loop::setCapturePassState(passId, Disabled)` on live `LoopPasses`
- MIDI payload already lives in **PSRAM chunks** (immutable after seal)
- `GlobalUndoStack::entries` already uses `ExternalMemoryFirstAllocator<UndoEntry>`

The heavy undo path is **`ClearSlot`** (`beforeSnapshot` / `afterSnapshot` via `sharePassesSnapshot()`). That is bounded by **`reclaimUnreferencedDisabledPasses`** (chunk + pass row reclaim when undo no longer pins), not by trimming pass undo depth.

**Keep unchanged:** existing opportunistic trim inside `TrackUndo::pushUndoEntry` when `overUndoMemoryPressure` (absolute rail + ClearSlot pressure). **Do not** wire `trimGlobalUndoStackForMemory` into Critical pressure orchestration in `main.cpp`.

---

## Measured struct sizes (native probe)

Probe: `test/test_loop_size_probe` (`pio test -e native -f test_loop_size_probe`).

| Symbol | sizeof | Notes |
|--------|--------|-------|
| `Loop` | **536** | Per-slot shell (embedded structs + lazy `unique_ptr` caches) |
| `LoopPasses` | 88 | Shell only; vectors allocate separately |
| `RecordPass` | 40 | Contains `ChunkIdList` (empty = 0 heap) |
| `OverdubPass` | 40 | Same |
| `EditPass` | 56 | Contains `EditPassIdList` refs |
| `Capture` | 72 | Embeds `LoopEventStore` (64) |
| `LoopEventStore` | 64 | Shell; `chunkIds_` / `barFirstIndices_` allocate separately |
| `VisualCache` | 56 | `DisplayNoteVec` extmem; **`dirtyBars` default allocator** |
| `PublishedLoopEventStore` | 40 | Inside `Loop` |
| **Loop pool total** | **34,304** | `8 tracks × 8 slots × 536` |

The ~34 KiB pool is **fixed at first slot touch** per track (`LoopPool::ensureInitialized`). Dynamic pressure during 215312/235620-style sessions comes from **pass metadata vector growth**, not from re-allocating the pool shell.

---

## Internal heap consumers — pass metadata (today)

| Type | Allocator today | Owner | Scales with |
|------|-----------------|-------|-------------|
| `ChunkIdList` | `InternalHeapFirstAllocator<uint16_t>` | Each record/overdub/pending pass; each `LoopEventStore` | Chunks per pass (loop length × density) |
| `BarIndexVec` | `InternalHeapFirstAllocator<size_t>` | Each `LoopEventStore` | Bars with events |
| `OverdubPassVec` | `InternalHeapFirstAllocator<OverdubPass>` | `LoopPasses` | Overdub pass count |
| `EditPassVec` | `InternalHeapFirstAllocator<EditPass>` | `LoopPasses` | Edit pass count |
| `EditPassIdList` | `InternalHeapFirstAllocator<EditPassId>` | Undo entries, edit batching | Edit batch size |
| `VisualBarVec` (`dirtyBars`) | **default** (`malloc`) | `VisualCache`, `CapturePreview`, `VisualCacheDelta` | Loop bars |

Chunk **payload** is already PSRAM (`EventChunk` pool). Phase 2 moves **refs and pass rows** off internal heap.

[`INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md) deferred `ChunkIdList` / `BarIndexVec` as “small; audit if bisect implicates”. Phase 1B manual gate (**235620**) implicates internal heap floor independent of chunk pool — audit satisfied.

---

## Loop[] in external memory — what it gains (and does not)

### Fixed gain

Move `LoopPool::loops_` from `new Loop[n]` (internal heap) to `extmem_malloc(n * sizeof(Loop))` + placement-new:

| Metric | Value |
|--------|-------|
| Bytes moved off internal heap (all tracks) | **34,304** (~33.5 KiB) |
| When paid | First `ensureInitialized()` per track (lazy) |
| PSRAM cost | Same 34 KiB (negligible vs 8 MiB pool) |

This improves **post-setup / idle** internal heap and reduces **high-water break** baseline before capture. It does **not** shrink per-pass vector growth during overdub.

### What Loop[] extmem does **not** move

Each `Loop` embeds handles to separately allocated buffers:

- `OverdubPassVec` / `EditPassVec` reallocation
- Every `ChunkIdList` on record/overdub/edit passes and on `Capture.store`
- `LoopEventStore::chunkIds_` and `barFirstIndices_` on active capture
- Lazy caches: `playbackOrder_`, `noteCache_`, `cachedEventIndex_` (`unique_ptr` control blocks stay internal unless types change)
- `passesMaterializedStore_` flat cache (already extmem-first when materialized)

**Rule:** Loop[] extmem is **complementary**, not a substitute for pass metadata routing.

### Access pattern / hot-path note

- `Loop` scalar fields (`playbackRevision`, tick cursors, flags) are read on playback and capture paths.
- PSRAM latency applies to the **shell**; vectors already indirect through pointers (tier unchanged when only shell moves).
- Precedent: `Track::playbackRuntime`, `GlobalUndoStack`, `SessionMidiEventVec` already extmem-first.
- **Gate:** HITL baseline + 215312 manual profile — no MIDI timing regression on selected-track playback.

### Suggested sub-phase split

| Sub-phase | Scope | Expected internal heap impact |
|-----------|--------|--------------------------------|
| **2A — Pass metadata routing** | Typedef allocator swaps (below) | **Dynamic** — scales with capture session length and pass count |
| **2B — LoopPool extmem** | `LoopPool` allocation only | **Fixed ~34 KiB** per fully initialized fleet |

Implement **2A first** — matches 235620 failure mode (heap floor during publish/overdub, not at boot).

---

## Phase 2A — Pass metadata extmem routing

### Typedef / alias changes

In `LoopEventStore.h`:

```cpp
using ChunkIdList = std::vector<uint16_t, ExternalMemoryFirstAllocator<uint16_t>>;
using BarIndexVec = std::vector<size_t, ExternalMemoryFirstAllocator<size_t>>;
```

In `LoopPasses.h`:

```cpp
using OverdubPassVec = std::vector<OverdubPass, ExternalMemoryFirstAllocator<OverdubPass>>;
```

In `EditPass.h`:

```cpp
using EditPassIdList = std::vector<EditPassId, ExternalMemoryFirstAllocator<EditPassId>>;
using EditPassVec = std::vector<EditPass, ExternalMemoryFirstAllocator<EditPass>>;
```

In `VisualCache.h`:

```cpp
using VisualBarVec = std::vector<uint8_t, ExternalMemoryFirstAllocator<uint8_t>>;
```

### Files to touch

| File | Change |
|------|--------|
| `include/LoopEventStore.h` | `ChunkIdList`, `BarIndexVec` typedefs |
| `include/LoopPasses.h` | `OverdubPassVec` |
| `include/EditPass.h` | `EditPassVec`, `EditPassIdList` |
| `include/VisualCache.h` | `VisualBarVec` |
| `include/GlobalUndoStack.h` | `EditPassIdList` on `UndoEntry` (via EditPass.h) |
| `src/Loop.cpp` | `deepCloneChunkRefs` — no logic change; verify extmem copy |
| Native tests | `test_loop_event_store`, `test_pool_budget`, `test_edit_apply`, `test_sd_load_adopt` |

### Non-goals (Phase 2A)

- Do not move `LoopEventStore` chunk **pool** metadata (already PSRAM).
- Do not change undo semantics or pass immutability contract.
- Do not lower `HEAP_RESERVE_BYTES` until post-routing manual gate passes.

### Native verification

- `pio test -e native` (full suite)
- Existing `test_extmem_allocator` / `test_pool_budget` must pass on Teensy path
- Optional: extend `test_loop_size_probe` with `sizeof(ChunkIdList)` capacity probe under synthetic append

---

## Phase 2B — LoopPool external memory

### Implementation sketch

```cpp
// LoopPool.cpp — ensureInitialized()
const size_t n = LoopPoolConfig::MAX_LOOPS_PER_TRACK;
void* mem = extmem_malloc(n * sizeof(Loop));
if (!mem) { /* fallback: internal new Loop[n] for no-PSRAM / native */ }
loops_ = static_cast<Loop*>(mem);
for (size_t i = 0; i < n; ++i) {
  new (&loops_[i]) Loop();
}
```

Destructor: explicit `~Loop()` per element + `extmem_free` (or `InternalHeapFirst`-style `isInExternalMemoryPool` routing).

Native (`PIO_UNIT_TESTING`): keep internal `new Loop[n]` — no PSRAM.

### Files

| File | Change |
|------|--------|
| `src/LoopPool.cpp` | extmem allocation + placement new |
| `include/LoopPool.h` | Document tier; optional `bool loopsInExternalMemory_` |
| `test/test_loop_pool` | Native unchanged; Teensy device test optional |

---

## Phase 2C — Critical reclaim (unchanged intent, minus undo trim)

Wire under `MemoryPressureLevel::Critical` in `main.cpp` (after Low reclaim in same turn):

| Action | Owner | Notes |
|--------|-------|-------|
| `TrackManager::reclaimUnreferencedDisabledPasses()` | Existing | Frees chunks for disabled passes not pinned by undo |
| Visual defer | `DisplayManager` | Skip REVT; defer LED merge; non-selected `visualCache` idle |
| Persist admission override | `StorageManager` | `tryOverrideNonCriticalPersistAdmission` after reclaim — **not** before |

**Removed from Critical policy:** `trimGlobalUndoStackForMemory` as orchestrated reclaim.

---

## Expected outcomes (observable)

| Criterion | Phase 1B (235620) | Phase 2 target |
|-----------|-------------------|----------------|
| `Capture append failed` | 0 | 0 |
| Internal heap floor during long overdub | ~4 KiB | **≥ 16–32 KiB** interim (tune after 215312 re-run) |
| Chunk pool headroom | Healthy | Unchanged |
| Undo depth after record/overdub stack | Full preferred depth | Unchanged (no trim policy) |
| HITL baseline | Pass | Pass |

Reported `getInternalHeapFreeBytes()` may still lag actual reuse (high-water `__brkval`). Success = **no append failures** + **stable repeated overdub sessions**, not tail-free alone.

---

## Pre-implementation review

### Ready

- Phase 1A + 1B shipped on `feature/memory-pressure-reclaim`
- sizeof probe committed (`test/test_loop_size_probe`)
- Pass-centric undo model documented in [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- Extmem allocator patterns proven (`SessionMidiEventVec`, `GlobalUndoStack`)

### Resolved

| Topic | Decision |
|-------|----------|
| Undo trim at Critical | **Skip** — pass undo is O(1); trim harms UX without fixing metadata axis |
| Primary Phase 2 lever | Pass metadata extmem routing (2A) |
| Loop[] extmem | **2B** — fixed ~34 KiB; complementary |
| Pass chunk reclaim | **Keep** `reclaimUnreferencedDisabledPasses` at Critical |

### Open before coding

1. **2A vs 2B order** — default 2A first; 2B same session only if RAM1/ITCM budget allows (FLASHMEM cold paths).
2. **BarIndexVec routing** — include in 2A (same typedef file as `ChunkIdList`); playback uses bar index on materialize paths, not per-clock tick.
3. **Fallback when PSRAM exhausted** — `ExternalMemoryFirstAllocator` falls back to internal heap (existing contract); telemetry should log allocator failure if both tiers fail.

### Proceed?

**YES** — implement **Phase 2A** first after user approves this plan revision. Gate with native tests + 215312 manual profile before 2B.

---

## Architecture gate (Phase 2 — for implementer)

| Question | Answer |
|----------|--------|
| **Owner module** | Typedef owners: `LoopEventStore`, `LoopPasses`, `EditPass`, `VisualCache`; pool owner: `LoopPool` |
| **Primary invariant** | Pass MIDI stays in PSRAM chunks; undo remains pass-id toggle; no full flatten on stop |
| **Ownership change?** | NO — allocator tier only |
| **State transition change?** | NO |
| **Behavior-preserving?** | YES for 2A/2B (memory tier only) |
| **Reuse decision** | YES — extend existing `ExternalMemoryFirstAllocator` typedef pattern |
| **Phase scope** | 2A typedefs + tests; 2C Critical wiring; 2B optional same PR or follow-on |

---

## Related

- [`memory_pressure_reclaim_refinement.md`](memory_pressure_reclaim_refinement.md) — parent phases
- [`internal_heap_psram_routing_refinement.md`](internal_heap_psram_routing_refinement.md) — July routing baseline
- [`docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md)
- [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
