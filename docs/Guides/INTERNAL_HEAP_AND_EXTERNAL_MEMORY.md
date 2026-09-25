# Internal heap and external memory pool

Agent-oriented rules for **where** allocations live on Teensy 4.1 and **how** admission gates use each tier. Read this before adding NOTE_EDIT temps, undo payloads, display buffers, or UIP projection vectors.

> **Different axis:** this guide covers the malloc heap (internal RAM2) vs PSRAM. For **RAM1 linker overflow** (`free for local variables: -N`) — a DTCM/ITCM 32 KB bank-quantization problem solved by moving globals to `DMAMEM` — see [`.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc).

**Shipped:** 2026-07-06 (`b1260ce`). Refinement log: [`internal_heap_psram_routing_refinement.md`](../Plans/internal_heap_psram_routing_refinement.md). OpenSpec: [`openspec/specs/internal-heap-external-memory-routing/spec.md`](../../openspec/specs/internal-heap-external-memory-routing/spec.md).

---

## Two memory tiers

| Tier | API | Hardware | Role |
|------|-----|----------|------|
| **Internal heap** | `malloc` / `free`, `MemoryMonitor::getInternalHeapFreeBytes()` | 512 KiB RAM2 shared with static DMA buffers | Hot path, small fixed state, admission reserves |
| **External memory pool** | `extmem_malloc` / `extmem_free`, `MemoryMonitor::getExternalMemoryPoolFreeBytes()` | 8 MiB PSRAM (when fitted) | Length-scaling and cold buffers |

**Naming (code):** use **internal heap** and **external memory pool** — not “RAM2” or “PSRAM” in new identifiers. Platform symbols (`extmem_malloc`, `external_psram_size`) stay as-is behind allocators.

**Typedefs:**

- `InternalHeapFirstAllocator<T>` — malloc-first, extmem fallback.
- `ExternalMemoryFirstAllocator<T>` — extmem-first, malloc fallback.
- `SessionMidiEventVec` — `std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>` for note-edit session and display temps.

---

## Routing policy

### Hot path — keep on internal heap

Do **not** move these to the external memory pool solely for headroom:

- MIDI clock, note-out, playback servicing (`playMidiEvents`, `rebuildPlaybackOrder` sort keys)
- `LoopEventStore` chunk pool metadata and small per-store ID lists (`ChunkIdList`, `BarIndexVec`)
- `Track::playbackRuntime` hot scan state (already extmem-backed where length-scaling)
- Any per-tick or per-CC allocation in fader inbound handlers

**UIP playback guardrail (D24):** `playbackEventPhase` is scalar — never call `generateEquivalentIntervals` inside a sort comparator.

### Cold / length-scaling — external memory pool first

Route new buffers here when they scale with loop length, edit closure, or undo depth and are **not** on the MIDI-clock path:

| Owner | Vector / map | Notes |
|-------|----------------|-------|
| `NoteEditFocus` | `baselineMap`, `overlapNotes` | `ExternalMemoryFirstAllocator` unordered maps |
| `NoteEditSessionUndoStack` | `entries_` | Cold stack; depth ≤ `PREFERRED_SESSION_UNDO_DEPTH` |
| `CowLoopEventStore` session flat | `flatCache_` | `SessionMidiEventVec` during NOTE_EDIT |
| `DisplayManager` | `liveDisplayEventBuffer` | Display-only; copy to `MidiEventVec` at API boundary if needed |
| `MemoryPool::globalMidiEventPool` | pool + `used[]` | `init()` in `setup()` after PSRAM ready |
| UIP | `buildCanonicalSpansFromMidi` temps, `IntervalProjection` batch vectors | Per reconstruct / per project |
| `NoteUtils` | span rebuild temps in `buildCanonicalSpansFromMidi` | Same allocator as UIP |
| `Loop` snapshot nodes | 32 KB bump slabs (`kSnapshotSlabBytes`) | `allocateSnapshotShared` only. One `extmem_malloc` per slab; live-object count frees the slab. Construction `failed_` if a new slab is denied. Not a general `ExternalMemoryFirstAllocator` arena. |

### Batch loops — one buffer per batch, never one per item

`extmem_malloc` is **not** O(1). Teensy routes it to smalloc, and `sm_malloc_pool` restarts a linear walk of the pool header chain from `spool->pool` on **every** call, verifying each block's hashed tag as it goes. There is no free list. Cost per allocation is the number of allocated blocks before the first adequate gap, so it grows with how much of the pool the session already holds — event chunks, playback runtimes, persistence queues. A full traversal of that same chain measured **593 ms** (see the walk rule below).

Therefore a loop that allocates one external-memory temp per item costs `O(items × pool blocks)` and gets slower as a session accumulates passes, independent of item count.

**Rule:** hoist the temp out of the loop and `clear()` it per item — `clear()` keeps capacity, so a reused buffer allocates once per batch. Prefer the out-param overload when one exists.

| Owner | Reused buffer |
|-------|----------------|
| `IntervalProjection::projectDisplayNotes` | one `candidates` across all spans |
| `IntervalProjection::projectNoteIntervals` | one `candidates` + one `selected` across all spans |

`generateEquivalentIntervals` and `selectProjectedIntervalsForDisplay` each have a by-value form (single-span callers, tests) and an out-param form. Batch callers use the out-param form.

### Baseline map scope (NOTE_EDIT)

`rebuildNoteEditFocusFromStore` SHALL NOT populate `baselineMap` for every note in the loop. Populate **edit closure** only (`populateBaselineMapForEditClosure`): moving note + overlap notes. Undo snapshots trim further via `snapshotFocusForSessionUndo`.

Call `populateBaselineMapForEditClosure` after focus rebuild on F1 reselect (`EditManager::rebuildNoteEditFocusAtSelect`, `rebuildNoteEditFocusForDisplayNote`).

---

## Admission gates

Gates consult **internal heap free** plus tier-specific estimates. They do **not** treat external pool bytes as internal headroom.

| Gate | Constant | Checks |
|------|----------|--------|
| Edit pass commit | `HEAP_RESERVE_BYTES` (32 KiB) | `saveNoteEditPass` — internal heap only |
| Session undo push | `HEAP_RESERVE_BYTES` + entry estimate | `canHeapAdmitSessionUndoEntry` — **split** internal vs external |
| Global undo trim | `HEAP_RESERVE_BYTES`, chunk reserve | `undo-memory-trim` spec |
| Deferred save | `INTERNAL_HEAP_SAFETY_FLOOR_BYTES` | Non-critical persistence |
| Chunk seal | `CHUNK_RESERVE` | `canAllocChunkWithReserve` |

### Session undo admission (split tier)

`canHeapAdmitSessionUndoEntry`:

1. Estimate **internal** bytes: `SessionUndoEntry` struct, `editRows`, `addedEvents`, redo payload.
2. Estimate **external** bytes: `focus.baselineMap`, `focus.overlapNotes` (and redo focus maps).
3. Require `getInternalHeapFreeBytes() >= HEAP_RESERVE_BYTES + internalBytes`.
4. If PSRAM **unavailable**: add external estimate to internal requirement (native tests / no chip).
5. If PSRAM **available**: **do not** call `getExternalMemoryPoolFreeBytes()` or `getExternalMemoryPoolUsedBytes()` anywhere in `loop()` — `sm_malloc_stats_pool` walks the full pool (measured **593 ms** on 8 MiB in [`141815`](../../captures/session_20260812_141815.log)). Same rule as `LoopEventStore::hasHeadroomForCommittedChunkIdList`: optimistic extmem admit; `push_back` is the real alloc gate.

**Automatic allocator walks belong to `setup()` only** — pass `logStatus(true)` there. Runtime callers report `getExternalMemoryPoolTotalBytes()` (`pool_size`, O(1)) instead.

An earlier version of this rule allowed the walk in "idle / stopped diagnostics (`logStatus`, 60 s main-loop interval)". That exemption was wrong. The `main.cpp` gate is `!timingCriticalTrackActive`, derived from track state (`isRecording() || isOverdubbing() || isPlaying()`), which says nothing about whether an **external clock is still streaming**. In `141815` the gate opened 152 ms after `PLAYING → STOPPED`, the walk blocked the loop 593 ms, and `ClockManager` reported `MIDI clock lost, switching to internal at 118.4 BPM`. There is no track-state predicate that makes this call safe; do not reintroduce one.

**Do not** post-push trim session undo based only on internal heap when entries live in the external pool — that falsely evicted depth at ~32 steps before the split.

On failed push after reclaim: `editSession.store.discardFlatCache()` then one retry (`EditManager::pushSessionUndoOnKindChange`, `foldLiveCaptureIntoNoteEditSession`).

---

## Boot and measurement

Post-setup internal free dropped from ~344 KiB (June) to ~60 KiB (July) before routing fixes. Targets after routing:

| Gate | Interim | Stretch |
|------|---------|---------|
| Post-setup internal free | ≥ 120 KiB | ≥ 200 KiB |
| NOTE_EDIT geometry | No `heap below reserve` on F2/F3 after F1 reselect | — |

**Controlled capture protocol** (comparable across sessions):

1. Fresh power-on → first `[Memory]` line  
2. After `setup()` (~5 s)  
3. After canonical HITL record+overdub (no NOTE_EDIT)  
4. After NOTE_EDIT open + F2 move + F1 reselect  

```bash
.venv/bin/python scripts/parse_memory_capture.py captures/session_*.log
```

### Diagnostics trace (capture-serial)

Binary diagnostic records complement `[Memory]` prose lines. They use the same `#CAP` prefix and deferred PSRAM ring as other capture tags.

| Tag | When | Fields |
|-----|------|--------|
| `DIAG` | Main-loop flush | `formatVer`, `eventId`, context snapshot, optional heap snapshot, `payload` |
| `DIAG,snapshot_alloc` | `LoopSnapshotConstruction::freeze()` | `nodes`, `bytes`, `slabs` for `allocateSnapshotShared` (capture-serial) |
| `DIAGCHK` | Boot after hard fault | Same layout; last record from `DiagLastRecordSlot` |

```bash
.venv/bin/python scripts/parse_diag_trace.py captures/session_*.log
```

**NOTE_EDIT open path:** `DIAG_EVENT` only on inner steps — no `DIAG_MEMORY` or `logStatus` inside `openNoteEditSession`. See [`memory_diagnostics_optimization_enhancement.md`](../Plans/memory_diagnostics_optimization_enhancement.md).

Allocator audit (Phase 4):

```bash
python3 scripts/audit_vector_allocators.py
```

PSRAM pool totals are **not** comparable across arbitrary sessions (SD load, chunk count). Always pair heap lines with the protocol above.

---

## When adding code

1. **Classify** the buffer: hot vs cold, scales with loop length?  
2. **Pick allocator** — default `std::vector` on NOTE_EDIT reconstruct is a regression.  
3. **Admission** — if push can fail, use existing gate; do not lower `HEAP_RESERVE_BYTES` without capture baseline.  
4. **Native test** — `pio test -e native`; mock heap via `MemoryMonitor::setNativeTestFreeHeap` where applicable.  
5. **No full-loop copy on fader CC** — use `focus.last`, session store reconstruct, or scoped closure; see fader hot-path commit `432da4d`.

---

## Related

- [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](LOOP_MIDI_STORAGE_AND_VALIDATION.md) — chunk pool, stop path, undo  
- [`record_overdub_memory_display_timeline_enhancement.md`](../Plans/record_overdub_memory_display_timeline_enhancement.md) — end-to-end timeline  
- [`FADER_STATE_SYSTEM.md`](FADER_STATE_SYSTEM.md) — `kNoteEditFaderFeedbackEnabled` on/off  
- [`openspec/specs/long-record-memory-headroom/spec.md`](../../openspec/specs/long-record-memory-headroom/spec.md)  
- [`openspec/specs/note-edit-session-undo/spec.md`](../../openspec/specs/note-edit-session-undo/spec.md)
- [`memory_diagnostics_optimization_enhancement.md`](../Plans/memory_diagnostics_optimization_enhancement.md) — diagnostics platform + Phase 1–5 order

## Accounting semantics (S1.6 heap-correctness slice)

`getInternalHeapFreeBytes()` and serial `heap_free` measure unused arena tail
(`_heap_end - __brkval`), excluding reusable malloc free-list blocks. They remain a
conservative admission input; zero alone does not prove every internal block is live.
The core's `extmem_malloc` already falls back to internal malloc. Wrapped live/class/tag
counters now include only physical external pointers; internal fallback is excluded because
owners may free it directly. They measure wrapped payload bytes, not allocator overhead,
fragmentation or physical free space. Historical wrapper totals included internal fallback
and stale tag attribution, so do not compare old and corrected live totals as reclaimed memory.
The fixed 8192-entry shadow table itself consumes 96 KiB of RAM2 (12 bytes per device entry).
It is not a free diagnostic: it reduces the internal malloc arena. Its size is unchanged.
Capture-build refusal lines add `accounting=2,caller=0x...`; resolve the return address against
the exact flashed ELF (Thumb return address, inspect the preceding instruction). A template
allocator symbol may require further bounded owner attribution. Null means both the external
pool and the core's internal fallback failed, not the earliest external-only refusal.

### Explicit stopped heap inspection

`!HEAP_STATS` is a session-capture-only, user-requested exception: MemoryMonitor inspects allocator metadata only after transport, external clock, active tracks/capture/stop work and deferred storage/prewarm are quiescent. It never runs periodically or from allocation/admission paths. Keep external MIDI clock off throughout the request; this synchronous diagnostic intentionally blocks main-loop service while it scans. Refusal returns a reason without reading either allocator.

Version 1 reports bytes. Internal `free_chunk_bytes`, `largest_chunk`, `top_chunk` include allocator chunk overhead; `heap_tail` is unused sbrk address space, separate from free chunks. `used_chunk_bytes` includes allocated chunk overhead. External `free`, `largest_gap`, `largest_request`, `occupied` and `payload` describe the actual smalloc pool, including allocations outside the wrapper. `largest_request` subtracts the 24-byte header/trailer cost. Do not combine internal largest-chunk and heap-tail values as a promised allocation size.

Only use readings with both completeness flags set. Incomplete fields are partial, and `used_chunk_bytes=0` on incomplete internal inspection is unavailable, not zero usage. The internal walk caps at 4096 nodes; the external scan follows the pinned 12-byte smalloc layout. Toolchain/core allocator changes require revalidation. No test allocation or allocator metadata mutation occurs.

For retention/leak investigation compare repeated snapshots at the same settled workload after all transient owners retire. Growth establishes retained allocation, not automatically an unreachable malloc leak. Fragmentation is evidenced by sufficient aggregate free bytes but insufficient largest eligible extent for a specified request; distinguish pool capacity and internal uncommitted tail. This command adds no pointer/callsite allocation ledger.

The diagnostic clock guard also requires one second without a received MIDI clock pulse, tracked before ClockManager’s stopped-transport early return. This capture-only timestamp bookkeeping performs no allocation or heap inspection. Diagnostic timestamps are initialized during ClockManager setup.

### Known owner lifetime repairs

Whole-take view release now retires the origin/rank side arrays together with event storage through `RuntimePlaybackView::release`. The selected-bridge refusal still precedes mutation; no earlier mid-record demotion is introduced. For two 2048-event bases, this corrects 24,576 bytes of retained side-array payload omitted by the released reservation report.

The draw-source resolver borrows the existing pending-paint vector as synchronous compose scratch instead of allocating a third vector every frame. Before painting transforms, it returns old compose storage to the paint owner. Compose and paint remain distinct, and handoff frames retain authority. Slide inclusion filtering is in-place and preserves ordering and note geometry. Neither repair changes allocation routing, detail horizon or admission thresholds; reducing churn is not proof that the N=56 capacity failure is eliminated.

Pending-paint growth explicitly retires obsolete backing before copy assignment when the composed source exceeds capacity. This avoids target libstdc++'s allocate-copy-free overlap; compose remains intact and capacity-fitting frames retain reuse. No record/display rows are dropped to fit memory.

### Snapshot construction allocation (DEC-085)

One unpublished construction retains its private index-edit workspaces across changes. Notes/events still have distinct immutable objects, but repeated edits of the same pitch/bar reuse private directories and membership pages instead of leaving retired intermediate copies in bump slabs. Published roots remain immutable. Slab `usedBytes` is bump high-water, including retired nodes; it is not the byte total of currently live objects. A slab with many live nodes and high bump usage can still contain substantial retired allocation. Only whole-slab release returns that backing to the allocator.
