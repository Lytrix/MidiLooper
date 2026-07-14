# Memory pressure reclaim — conclusions (2026-07-15)

**Kind:** refinement (session conclusions)  
**Branch:** `feature/memory-pressure-reclaim`  
**Parent plan:** [`memory_pressure_reclaim_refinement.md`](memory_pressure_reclaim_refinement.md)  
**Phase 2 detail:** [`memory_pressure_phase2_pass_metadata_extmem_refinement.md`](memory_pressure_phase2_pass_metadata_extmem_refinement.md)  
**OpenSpec:** [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

---

## Scope

Investigation and implementation of **MemoryPressureLevel** advisory FSM, Low-level derived-cache reclaim, and Phase 2 attempts to free **internal heap** during long multi-track capture. Conclusions below are from native tests, firmware builds, and capture logs **215312 → 235620 → 003306 → 004415**.

---

## Shipped (kept)

| Phase | Deliverable | Status |
|-------|-------------|--------|
| **1A** | `MemoryPressureLevel` (Normal / Low / Critical), hysteresis, `#CAP,DIAG,pressure,...` | Shipped |
| **1B** | `TrackManager::tryReclaimDerivedViewCachesUnderPressure` — playback window, materialized store, published scratch; background-first | Shipped |

Phase 1B targets **PSRAM-tier derived views** (playback windows, materialized store). It does **not** move or reclaim **internal-heap pass metadata** (`ChunkIdList`, bar indices, pass vectors).

---

## Two memory axes (do not conflate)

| Axis | Metric | Phase 1B helps? | Capture append failures? |
|------|--------|-----------------|--------------------------|
| **Internal heap** | `MemoryMonitor::getInternalHeapFreeBytes()` | Indirectly only | **Yes** — when floor exhausted during append |
| **Chunk pool (PSRAM)** | `LoopEventStore::freeChunkCount()` | No (different pool) | Yes — when `≤ CHUNK_RESERVE` |

In **235620**, chunk pool stayed healthy (446+ free) while internal heap hit **~4 KiB** with **0** append failures. In **215312**, both axes were stressed (42 append failures). Pressure telemetry and reclaim must be interpreted on **both** axes.

---

## Capture log evidence

| Log | Firmware | Append failed | Extmem FATAL | Overdub stop OK | Post-boot heap | Critical `persist_queue` |
|-----|----------|---------------|--------------|-----------------|----------------|--------------------------|
| [`215312`](../../captures/session_20260714_215312.log) | Pre–1A/1B baseline | **42** | — | — | — | — |
| [`235620`](../../captures/session_20260714_235620.log) | Phase **1B** | **0** | No | **Yes** (published at **4 KiB** heap) | **~86 KiB** | **0** at pressure transitions |
| [`003306`](../../captures/session_20260715_003306.log) | Blanket **2A** extmem | 1 | **Yes ×2** (`abort()`) | No (reboot) | — | **42** |
| [`004415`](../../captures/session_20260715_004415.log) | Split-tier **2A** (reverted) | 1 | No | No (reboot) | **~57 KiB** | **42** |

Common failure signature when capture breaks (003306 / 004415):

1. `Capture append failed` (often note 12 at overdub stop)
2. User presses Stop Overdub → `Stop finalize pending`
3. `#CAPTURE_RECONNECT` (reboot) — **no** `ODUB,stop,published` / `OVERDUBBING → PLAYING`

**235620** completed the same stop path at **4 KiB** reported heap with **0** prior append failures — so absolute tail-free alone does not predict stop success; **append failure + backlog + lower boot baseline** correlate with failure.

---

## Phase 2A — pass metadata extmem (PARKED)

Both attempts **reverted**. Do **not** retry typedef allocator swaps for pass/edit metadata without a new design session.

### Blanket 2A (`d5c7410` → revert `2e164e4`) — log **003306**

Moved **all** pass metadata to `ExternalMemoryFirstAllocator`: `ChunkIdList`, `BarIndexVec`, `OverdubPassVec`, `EditPassVec`, `EditPassIdList`, `VisualBarVec`.

| Outcome | Evidence |
|---------|----------|
| Hard crash | `[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` → `abort()` → reboot |
| Hot-path violation | `ChunkIdList` / `BarIndexVec` touched on **every capture append** — contradicts [`INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md) |
| Timing / stop | Overdub stop reboot; `COORD,abs` up to ~50 Ki clocks at failure |

### Split-tier 2A (`ed75699` → revert `896c70d`) — log **004415**

Moved **cold only**: `EditPassVec`, `EditPassIdList`, `VisualBarVec`. Kept hot types internal.

| Outcome | Evidence |
|---------|----------|
| No extmem FATAL | Split-tier removed `abort()` on capture hot path |
| Same capture failure | 1× append failed; overdub stop still reboots |
| **NOTE_EDIT sluggish** | User-visible; PSRAM latency on edit pass iteration |
| Not root fix | Internal heap / persist backlog unchanged |

**Conclusion:** Pass-metadata extmem routing is **not** a viable heap-restore strategy for this product. Gains are small for capture-only sessions; costs are capture crashes (hot types), edit UX (cold types), or both.

---

## Phase 2C — Critical orchestration (never shipped)

Planned: Critical pass reclaim, visual defer, persist admission override. **Deferred** by user direction — prefer **heap restore** without further pressure orchestration that might affect edit/responsiveness.

Existing idle hook `reclaimUnreferencedDisabledPasses()` in `main.cpp` remains; no new Critical wiring added.

---

## Internal heap — what consumes and what reclaims

### Fixed (measured, native probe)

| Item | Size |
|------|------|
| `sizeof(Loop)` | 536 B |
| Full `LoopPool` (8 tracks × 8 slots) | **34,304 B (~33.5 KiB)** on internal heap today |

Probe: `test/test_loop_size_probe` (`pio test -e native -f test_loop_size_probe`).

### Dynamic during long capture (stays internal)

- `ChunkIdList` on every pass + active `Capture.store`
- `BarIndexVec` per store
- `OverdubPassVec` growth
- Persistence / SD work (004415: queue depth **42** at Critical)

### Phase 1B reclaim (PSRAM)

- `PlaybackWindow.mergedEvents`
- `passesMaterializedStore_`
- `publishedMidiScratch_`

Reclaim often **no-ops during active overdub** when windows are referenced (stale **and** not-referenced guard).

---

## Reported heap vs usable heap

`getInternalHeapFreeBytes()` = bytes above **`__brkval`** (malloc high-water break).

- Freed cache memory is often **reusable** but **does not raise** the reported tail-free number until reboot or slot clear.
- **Do not** use tail-free alone as success criteria.
- **Do** use: 0× `Capture append failed`, overdub stop publish OK, no reboot on stop, stable repeated sessions.

---

## Undo trim

**Not needed** for record/overdub memory pressure:

- `pushRecordPassAdded` / `pushOverdubPassAdded` store O(1) `passId` only
- Undo = `setCapturePassState(passId, Disabled)`
- MIDI payload in PSRAM chunks
- `GlobalUndoStack` already extmem-backed

Heavy undo path is **ClearSlot** (full pass snapshots) — bounded by pass chunk reclaim, not stack trim at Critical.

---

## Heap-restore direction (next work)

**No** pass/edit typedef tier moves. Priority order:

| # | Lever | Expected benefit | Risk |
|---|--------|------------------|------|
| 1 | **2B — LoopPool extmem** | ~34 KiB fixed internal headroom | **Shipped** (`LoopPool` extmem-first) |
| 2 | **Persist backlog** | Reduce `queue_depth=42` forcing Critical; free CPU/heap during capture | Medium (SD I/O) |
| 3 | **Phase 1B telemetry** | Confirm Low reclaim runs / no-ops during overdub | None |
| 4 | **Fresh workspace boot** | Baseline 86 KiB vs 57 KiB (235620 vs 004415) | Manual hygiene |

**Parked:** Phase 2A extmem typedefs; Phase 2C Critical orchestration.

---

## Git anchors (branch `feature/memory-pressure-reclaim`)

| Commit | Summary |
|--------|---------|
| `f09f547` | Phase 1A — pressure FSM + telemetry |
| `96b9b36` | Phase 1B — Low derived-cache reclaim |
| `d5c7410` | Blanket 2A (later reverted) |
| `2e164e4` | Revert blanket 2A |
| `ed75699` | Split-tier 2A (later reverted) |
| `896c70d` | Revert split-tier 2A |
| `6b74d29` | Plan pivot — heap-restore track |

---

## Open questions

1. **Why reboot on overdub stop** when append has already failed — fault vs watchdog vs OOM during seal/publish (no FATAL in 004415)?
2. **Why persist queue = 42** after crash-recovery boots (004415 / 003306) but **0** in 235620?
3. **Can 2B alone** close the 57 KiB → 86 KiB boot gap, or is SD-loaded state dominating?

---

## References

- [`docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md)
- [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- [`docs/plans/multi_track_playback_pressure_closure_refinement.md`](multi_track_playback_pressure_closure_refinement.md) (M6 predecessor)
