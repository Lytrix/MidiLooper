# Internal heap PSRAM routing — refinement

**Kind:** refinement  
**Date:** 2026-07-06  
**Build env:** `teensy41-capture-serial`

## Problem

Internal heap free at boot dropped from **~344 KB** (mid-June captures) to **~60 KB** (July captures) on the same ~486 KB RAM1 heap. NOTE_EDIT geometry then fails `canHeapAdmitSessionUndoEntry` when free internal heap falls below `HEAP_RESERVE_BYTES` (32 KiB) plus undo entry estimate.

PSRAM pool lines are **not** comparable across arbitrary sessions (SD load, loop count, chunk pool pre-init). Use the controlled protocol below.

## Controlled capture protocol

1. Fresh power-on → first `[Memory]` line in log  
2. After `setup()` (~5 s) — second `MemoryMonitor::logStatus` in `main.cpp`  
3. After SD `loadState` (implicit in step 2)  
4. After canonical HITL record+overdub (no NOTE_EDIT)  
5. After NOTE_EDIT open + F2 move + F1 reselect  

Parse with:

```bash
.venv/bin/python scripts/parse_memory_capture.py captures/session_*.log
```

## Bisect window (firmware)

| Anchor | Capture | Post-setup heap used (approx.) |
|--------|---------|--------------------------------|
| Good | `session_20260615_023726` | 142 KB used / 344 KB free |
| Bad | `session_20260704_000159` | 406 KB used / 76 KB free steady |
| Bad | `session_20260706_022221` | 426 KB used / 60 KB free after setup |

Commits between anchors (grep `git log --since=2026-06-14 --until=2026-07-05`): pool-budget, linear note-edit spans (`df6c51b`), capture-serial FLASHMEM/PSRAM ring (`c9164c2`), UIP Phases 1–4 (`ecb3b8a`), StorageSession migration (`9ef4bd0`–`ccd416f`).

## Owner table

| Owner | Tier | Allocator today | Target | Notes |
|-------|------|-----------------|--------|-------|
| `LoopEventStore` chunk pool | Boot | PSRAM `poolAlloc` | unchanged | ~4 MB capacity; not internal heap |
| `MemoryPool::globalMidiEventPool` | Boot | `MidiEventVec` internal-first, 1024 reserve | **ExternalMemoryFirstAllocator** | ~24 KB moved off malloc-first |
| `NoteEditFocus::baselineMap` | NOTE_EDIT session | default `unordered_map` | **ExternalMemoryFirstAllocator** + closure-only population | Was full-loop per rebuild |
| `NoteEditFocus::overlapNotes` | NOTE_EDIT session | default `unordered_map` | **ExternalMemoryFirstAllocator** | |
| `NoteEditSessionUndoStack::entries_` | NOTE_EDIT session | `InternalHeapFirstAllocator` | **ExternalMemoryFirstAllocator** | Cold; depth ≤ 32 |
| `CowLoopEventStore::flatCache_` | NOTE_EDIT / materialize | `MidiEventVec` internal-first | **SessionMidiEventVec** extmem-first | Edit session flat only |
| `DisplayManager::liveDisplayEventBuffer` | Display | `MidiEventVec` | **SessionMidiEventVec** | Not MIDI-clock hot |
| UIP `buildCanonicalSpansFromMidi` temps | Per reconstruct | default `vector`/`map` | **ExternalMemoryFirstAllocator** | UIP Phase 1 |
| `IntervalProjection` batch vectors | Per project | default `vector` | **ExternalMemoryFirstAllocator** | UIP Phase 1 |
| `Track::playbackRuntime` / `mergedEvents` | Playback | already extmem | unchanged | See `test_pool_budget` |
| `GlobalUndoStack` | Global undo | already extmem | unchanged | |
| `DebugSessionCapture` ring | Boot | PSRAM 96 KiB | unchanged | RAM2 telemetry only |
| `ChunkIdList` / `BarIndexVec` per store | Per loop store | internal-first | defer | Small; audit if bisect implicates |

## Admission policy (unchanged)

`canHeapAdmitSessionUndoEntry` and `HEAP_RESERVE_BYTES` gate on **`MemoryMonitor::getInternalHeapFreeBytes()`** only. Moving cold data to PSRAM reduces pressure; do not lower reserve until post-setup free ≥ 150 KB on capture-serial baseline.

## Verification targets

| Gate | Target |
|------|--------|
| Post-setup internal free | ≥ 120 KB interim; ≥ 200 KB stretch |
| NOTE_EDIT F2 after reselect | No `heap below reserve` during geometry |
| Native | `pio test -e native` |
| HITL | Canonical record/overdub + NOTE_EDIT fader pass |

## Related

- [`docs/plans/record_overdub_memory_display_timeline_enhancement.md`](record_overdub_memory_display_timeline_enhancement.md)  
- [`docs/plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md`](capture_serial_ram1_recovery_extmem_debug_enhancement.md)  
- [`scripts/parse_memory_capture.py`](../../scripts/parse_memory_capture.py)
