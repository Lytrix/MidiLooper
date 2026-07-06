# Internal heap and external memory pool

Agent-oriented rules for **where** allocations live on Teensy 4.1 and **how** admission gates use each tier. Read this before adding NOTE_EDIT temps, undo payloads, display buffers, or UIP projection vectors.

**Shipped:** 2026-07-06 (`b1260ce`). Refinement log: [`internal_heap_psram_routing_refinement.md`](../plans/internal_heap_psram_routing_refinement.md). OpenSpec: [`openspec/specs/internal-heap-external-memory-routing/spec.md`](../../openspec/specs/internal-heap-external-memory-routing/spec.md).

---

## Two memory tiers

| Tier | API | Hardware | Role |
|------|-----|----------|------|
| **Internal heap** | `malloc` / `free`, `MemoryMonitor::getInternalHeapFreeBytes()` | ~512 KiB RAM1 (fast) | Hot path, small fixed state, admission reserves |
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
4. If PSRAM available: require `getExternalMemoryPoolFreeBytes() >= externalBytes` (external bytes are **not** added to the internal threshold).
5. If PSRAM **unavailable**: add external estimate to internal requirement (native tests / no chip).

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
| `DIAGCHK` | Boot after hard fault | Same layout; last record from `DiagLastRecordSlot` |

```bash
.venv/bin/python scripts/parse_diag_trace.py captures/session_*.log
```

**NOTE_EDIT open path:** `DIAG_EVENT` only on inner steps — no `DIAG_MEMORY` or `logStatus` inside `openNoteEditSession`. See [`memory_diagnostics_optimization_enhancement.md`](../plans/memory_diagnostics_optimization_enhancement.md).

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
- [`record_overdub_memory_display_timeline_enhancement.md`](../plans/record_overdub_memory_display_timeline_enhancement.md) — end-to-end timeline  
- [`FADER_STATE_SYSTEM.md`](FADER_STATE_SYSTEM.md) — `kNoteEditFaderFeedbackEnabled` on/off  
- [`openspec/specs/long-record-memory-headroom/spec.md`](../../openspec/specs/long-record-memory-headroom/spec.md)  
- [`openspec/specs/note-edit-session-undo/spec.md`](../../openspec/specs/note-edit-session-undo/spec.md)
- [`memory_diagnostics_optimization_enhancement.md`](../plans/memory_diagnostics_optimization_enhancement.md) — diagnostics platform + Phase 1–5 order
