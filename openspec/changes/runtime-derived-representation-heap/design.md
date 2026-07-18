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

## M6 — DEC-016 completion: legacy callsite migration (2026-07-14)

**Plan:** [`docs/plans/multi_track_playback_pressure_closure_refinement.md`](../../../docs/plans/multi_track_playback_pressure_closure_refinement.md)

### Framing

M6 is **not** a new playback architecture. M1–M5 and Phase A→C established deferred/windowed derived representations (DEC-016). Investigation shows remaining failures come from **production callsites that still eager-materialize** instead of using the shipped policy (`gatherPublishedFlatForDerivedView`, PLAYING stale-while-revalidate, bar-slice idle rebuild).

**M6 question:** Has DEC-016 been fully implemented across all runtime callsites? If no → complete migration gaps; validate; only then consider further streaming designs.

### Sequence

| Milestone | Role |
|-----------|------|
| M1–M5 | Architecture + extmem routing + idle seed + load spike |
| **M6** | Phase 0 audit + complete legacy bypass paths |
| Post-M6 (optional) | Iterator/streaming playback only if audit + gates show need |

### Evidence

Manual captures show solo / light multi-track succeeds; heavy multi-track overdub reboots:

| Log | Outcome | Signal |
|-----|---------|--------|
| `session_20260714_174731` | PASS — 64-bar + full overdub | 0 `RING,overflow`; heap 16 KB min |
| `session_20260714_175327` | FAIL — overdub ~79-bar loop | 7121 MO; 412 `RING,overflow`; 16s overflow-only tail → reboot |
| HITL `163446` | FAIL — record ~bar 9 | 28 KB at arm; serial silence (different phase) |

Chunk pool healthy (`PERS,diag`); failure is **main-loop starvation** under derived-data work + MO volume, not reserve exhaustion.

### Cancelled: DIAG heap firmware (Phase 0 investigation)

Phase 0 heap telemetry (`#CAP,DIAG,heap` / `HeapInvestigationTelemetry`) was investigated but **abandoned** because instrumentation exceeded RAM1 budget (~50 KB increase), preventing `teensy41-capture-serial` builds. M6 relies on **callsite audit + existing telemetry** (`RECS,stage`, `ODUB,stage`, `[Memory]`, `RING,overflow`, `PERS,result`) — not new diagnostic infrastructure.

### Implementation gap (confirmed)

Phase A handoff marks chunk-ref playback done; **`ensurePlaybackWindowBuilt` still assigns from `loop.midiEvents()`** → full materialize on every non-capture PLAYING track per `playbackRevision` bump. Display **OVERDUBBING** excludes `deferVisualRebuild` and sync-rebuilds every frame. These are **migration gaps**, not missing architecture.

### Phase 0 — callsite audit (**complete 2026-07-14**)

Complete materialization callsite matrix in plan doc. During implementation review, **additional legacy materialization paths** were identified (DisplayManager capture-active merge sites, MidiLedManager bar hook) — now in M6 audit scope. Sign-off complete; proceed to Phase 1.

### Architectural invariant

> **Runtime consumers do not choose their own data representation. Representation selection remains owned by the derived-view layer.**

### Incremental architecture metrics (Phases A–D)

- **Phase A (during M6):** Extend existing [`Diagnostics::Counter`](../../../include/Utils/Diagnostics.h) — not the cancelled ~50 KB `#CAP,DIAG,heap` approach. Permanent regression counters: `LegacyMidiEvents`, `PlaybackFullMaterialize`, `PlaybackDeferredReuse`, `PlaybackWindowRebuild`, `DisplayFullRebuild`, `DisplayIncrementalUpdate`; timing accumulators `PlaybackBuildTime`, `DisplayBuildTime`.
- **Phase B:** M6 firmware Phases 1–3; counters prove legacy path removal.
- **Phase C:** HITL + manual gates; hot-path counters → 0 during stress.
- **Phase D (post-M6 only if needed):** Full Architecture Performance Metrics Framework.

### M6 exit criteria

M6 is complete when: (1) audit complete and intentional materialize documented; (2) no unintended `Loop::midiEvents()` on PLAYING/RECORDING/OVERDUBBING hot paths; (3) DEC-016 policy consistently followed — *complete implementation of approved derived-representation policy across runtime playback paths*; (4) manual gates + `pio test -e native` PASS; (5) 64-bar multi-track HITL regression without architectural regression. Post-M6 streaming/iterator work **only if required** after production measurement.

### M6 decisions (completion work only)

1. **Playback window:** Align with `gatherPublishedFlatForDerivedView` — `mergeActiveCapturePasses` when no active edit passes; `midiEvents()` only when edit passes require it; capture-active path uses chunk merge + capture layer.
2. **Display capture:** Stale-while-revalidate overdub committed notes; incremental capture overlay; no per-frame `ensureVisualCacheBuilt` / full flatten on hot path.
3. **Idle:** When any track is RECORDING/OVERDUBBING, defer full materialize/visual on non-selected tracks; background playback stays enabled (cheap after §1).
4. **Persistence:** M6 preserves cooperative persistence (mid_pass + work items during capture, ~300 µs slices); indirect relief only — no scheduler/admission changes unless Option B approved after manual gates. See refinement doc § Persistence integration.
5. **Verification:** One stacked PR; **user manual gate** after each phase; HITL 64+64 only after M6 manual PASS.

### M6 non-goals

- DIAG heap firmware, extmem-fallback counter firmware
- **New streaming / iterator playback architecture** (evaluate only after M6 PASS)
- Capture/stop FSM or persistence admission/budget changes (DEC-020 is follow-on if 64+64 still starves after M6)
- HITL script timing changes before firmware fix

## Open Questions

- **M5:** Can pass snapshots share immutable chunk refs without per-pass internal flatten (`deepCloneChunkRefs`)?
- **M5:** Minimum boot slot set for transport-ready vs full 8×8 eager load?
- Bisect gates remain parked (DEC-017).

## M7 — Published capture / builder split (2026-07-15)

**Plan:** [`docs/plans/published_pass_capture_builder_split_refinement.md`](../../../docs/plans/published_pass_capture_builder_split_refinement.md)  
**Spec:** [`specs/published-capture-pass-split/spec.md`](specs/published-capture-pass-split/spec.md)  
**Review:** [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § M7

### Framing

M7 is primarily an **ownership refactor**, not a memory optimization. It replaces the reverted Phase 2A approach (single `ChunkIdList` typedef → extmem) with **compiler-enforced separation**:

- **CaptureBuilder** (`Capture.store`) — mutable, latency-sensitive, `CaptureChunkIdList` on internal heap.
- **Published passes** — immutable, storage-oriented, `PublishedChunkIdList` in external memory pool.

Chunk **payload** remains in the PSRAM pool (unchanged). M7 moves **pass metadata** (chunk-id lists, overdub pass vector) off internal heap where safe.

### Decisions

1. **One domain transition at seal** — `transferCaptureChunkIdsToPublished`; publish is a move within published types.
2. **PendingCapturePass** — recoverable commit staging with `PublishedChunkIdList` at seal; not a second builder.
3. **No published → capture repatriation** — except empty new capture session.
4. **Transfer bounded** — at most one alloc + one linear copy; no temp vectors on seal.
5. **Exit criterion** — if < ~5 KiB recovered under 215312 and no pressure improvement, stop further published-metadata migration (ownership split may still ship).

### Non-goals (M7)

- `EditPassVec` / `EditPassIdList` extmem (004415 sluggishness)
- Merge sorted-index cache (profiling follow-on only)
- Changing capture/stop FSM or persistence scheduler

### Sequence

| Phase | Scope |
|-------|--------|
| 0 | OpenSpec + plan + ARCHITECTURE-REVIEW (doc) |
| 1 | Typedefs + seal transfer + pending/publish |
| 2 | `PublishedOverdubPassVec` + call sites |
| 3 | SD / undo / clone alignment |
| 4 | HITL + exit measurement |

**Related shipped fix (same branch):** urgent deferred save below heap floor (`917cb87`) — persistence; orthogonal to M7 representation split.
