## Context

DEC-016 defines four runtime layers: capture storage (PSRAM chunks + passes), derived representations (revision-keyed extmem builds), interval projection (UIP), and runtime request (consumers read derived × interval). June–July regressions added **sync full materialize** on PLAYING/display paths and **internal-heap lazy flat** on `Loop::midiEvents()`.

July HITL (`cdd9c2b`) fixed the 16-bar hang by materializing playback window to `SessionMidiEventVec` and ring-first `#CAP`. 64-bar still leaves **8 KB RAM1** at stop because `passesMaterializedStore_` flat uses `InternalHeapFirstAllocator`, and persistence never dispatches because admission reads a stale snapshot.

## Goals / Non-Goals

**Goals**

- Keep internal heap above operational floor for MIDI clock, note-out, and playback — never gate those paths.
- Route length-scaling **published** flat and playback-window builds to external memory pool.
- Fix deferred-save admission to re-check heap when dispatching, not only at record stop.
- Preserve tier-A serial evidence (ST, PERS, RECS) under long overdub MO volume.

**Non-Goals (M1–M4)**

- Heap↔PSRAM event FIFO between layers.
- Full-loop `materializeToEventVector(MidiEventVec)` on stop, PLAYING entry, or per-frame display.
- Lowering `INTERNAL_HEAP_SAFETY_FLOOR_BYTES` without bounded SD batch proof.
- NOTE_EDIT geometry / UIP Phase 5.5 matrix (still deferred per DEC-017).

**Follow-up (M5 spike — documented, not in M1–M4 scope):** SD load / pass-clone / boot recovery paths — see [`spike_sd_load_extmem_routing.md`](spike_sd_load_extmem_routing.md).

## Decisions

### 1. Admission uses current heap at dispatch

| Signal | Use |
|--------|-----|
| `admissionHeap` at `requestDeferredSaveState` | Telemetry only |
| `getInternalHeapFreeBytes()` at **dispatch** | Gate non-critical persistence |
| `getInternalHeapMinEverFreeBytes()` | HITL telemetry / regression detection |

When below floor at dispatch: emit `PERS,defer,...,heap_floor` once, retry next idle tick. Once `inProgress`, slices run without re-gating (unchanged).

### 2. Published flat on extmem

`Loop::passesMaterializedStore_` SHALL use `LoopEventFlatCache<SessionMidiEventVec>`. `Loop::midiEvents()` returns `SessionMidiEventVec&`. `mergeActiveCapturePassesInto` temporaries inherit the output vector allocator.

`editAwareMidiEvents()` returns `SessionMidiEventVec&` — session store (`CowLoopEventStore`) also uses `SessionMidiEventVec` so the API is uniform. Functions that require internal heap copy at boundary do so explicitly (rare).

### 3. Derived-representation build policy (DEC-016)

- **Playback:** `ensurePlaybackWindowBuilt` uses chunk-ref merge or `materializeToEventVector(SessionMidiEventVec)` — never lazy internal flat on PLAYING entry.
- **Display:** bar-slice idle rebuild + provisional window; stale-while-revalidate during PLAYING.
- **Persistence:** slice FSM unchanged; admission per §1.

At most **one** full materialize per `playbackRevision` off hot path (idle maintenance).

### 4. Capture ring tiers (SESSION_CAPTURE)

| Tier | Lines | Policy |
|------|-------|--------|
| A | ST, PERS, RECS, HDR | Never dropped; preferential flush |
| B | REVT, DIAG, SAVE | Ring-first; may defer Serial |
| C | MO, MI (sustained OVERDUBBING) | Sample or drop when ring >75% or overflow pending |

When `overflowPending`, main loop raises `flushCaptureBuffer` budget until tier-A backlog clears.

## Risks / Trade-offs

- **SessionMidiEventVec everywhere for published flat** — slightly more PSRAM use; acceptable vs RAM1 exhaustion.
- **MO sampling** — long-run HITL may see fewer MO lines; tier-A transitions remain mandatory pass criteria.
- **64-bar stop-path nadir** — 8 KB at stop may persist; gate is playback + persistence completion, not stop-entry floor.

## Migration Plan

1. M1 admission + docs + native test (heap recovers → dispatch proceeds).
2. M2 extmem published flat + API boundary updates.
3. M3 capture ring tiers.
4. M4 HITL 64-bar gates; archive change to `openspec/specs/`.

## M5 spike — SD load path (2026-07-07)

After M2, runtime `passesMaterializedStore_` uses extmem. **Load and undo restore** still call `deepCloneChunkRefs`, which materializes each pass into **`MidiEventVec`** (internal heap) before re-chunking. `restorePassesSnapshot` also calls `rebuildVisualCacheFromPasses()` synchronously. Boot recovery reloads long 64+64 loops from checkpoints; combined load cost can leave **0 bytes** internal heap, blocking deferred save and HITL clear preconditions.

Proposed fixes (spike — implement in M5 after approval): extmem clone path, defer visual rebuild on load, optional lazy slot load. Full analysis: [`spike_sd_load_extmem_routing.md`](spike_sd_load_extmem_routing.md). DEC-019.

## Open Questions

- **M5:** Can pass snapshots share immutable chunk refs without per-pass internal flatten (`deepCloneChunkRefs`)?
- **M5:** Minimum boot slot set for transport-ready vs full 8×8 eager load?
- Bisect gates remain parked (DEC-017).
