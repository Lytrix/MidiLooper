# Tasks — runtime-derived-representation-heap

## Done (Phase A→C + `cdd9c2b`)

- [x] Phase A: playback defer, REVT gate, display stale-while-revalidate (PLAYING), chunk-ref merge paths (`d635296`, follow-ups)
- [x] Phase B: idle seed `passesMaterializedStore`, display from flat when fresh
- [x] Phase C: bar-slice visual rebuild, provisional long-loop window
- [x] Capture-serial ring Tier A/B foundation, min-ever watermark, playback window extmem materialize, MidiHandler OUT debug gated (`cdd9c2b`)
- [x] HITL: 16-bar record-only; 16-bar + 2× overdub; 16-bar + 2×16-bar overdub PASS (`20260707_161350`, `161554`, `161911`)

## M1 — Admission + telemetry

- [x] Fix `StorageManager.cpp` dispatch gate to use **current** `getInternalHeapFreeBytes()`
- [x] Update `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` § admission
- [x] HITL script: `record_stop_min_free_ram2_bytes` default 0; `--record-stop-min-free-ram2-warn-bytes 12288`
- [x] Native: extend `test_noncritical_work_admits_when_heap_recovers_after_stop_snapshot`

## M2 — Published flat extmem

- [x] `passesMaterializedStore_` → `PublishedLoopEventStore` (`SessionMidiEventVec`)
- [x] `Loop::midiEvents()` / `Track::getMidiEvents()` → `SessionMidiEventVec&`
- [x] `Track::legacyMidiEventsFromPublished()` revision-keyed boundary for `editAwareMidiEvents()`
- [x] Playback/commit paths materialize to `SessionMidiEventVec` where fresh

## M3 — Capture ring long runs

- [x] Tier C: sample MO when ring pressure high (1/8)
- [x] Tier-A text protected from discard; flush budget 256 when overflow pending
- [x] `SC_REC_FLUSH_ALL_PENDING_REVTS()` before overdub stop → PLAYING

## Parked — superseded by `continuous-runtime-persistence` (DEC-020)

Persistence starvation, transport-gate workarounds, and stop-path flush/defer patches on this change are **parked**. Root cause: `isCaptureActiveForPersistence()` blocks all save slices during capture. Fix: OpenSpec [`continuous-runtime-persistence`](../../continuous-runtime-persistence/) — Phase 0 diagnostics first.

- [ ] ~~Further `SC_REC_FLUSH` / post-`set_state` crash patches~~ → architecture change
- [ ] ~~Remove transport gate before sealed-chunk queue~~ → Phase 3 after Phase 2 on `continuous-runtime-persistence`
- [ ] ~~64+64 `PERS,result` via stop-path-only fixes~~ → Phase 6 gate on `continuous-runtime-persistence`

## M4 — Verification gates (revised 2026-07-14)

- [x] 64-bar record-only PASS (`20260707_184646`, seal heap=57344, PERS ok)
- [ ] 64+64 track 2/slot 1 — partial (`20260707_192649`); HITL **blocked on M6** manual gates
- [x] `pio test -e native` (baseline at last M5 merge)
- [x] Update `docs/runtime/CURRENT_WORK.md`, `PROJECT_STATE.md` (prior milestones)
- [ ] Archive change when M6 manual + HITL gates pass (`/opsx:archive`)

### M4 manual gates (before HITL 64+64)

| Gate | Evidence target | Pass criteria |
|------|-----------------|---------------|
| Light multi-track overdub | [`session_20260714_174731.log`](../../../captures/session_20260714_174731.log) baseline | After M6 Phase 1: same scenario, 0 sustained `RING,overflow` |
| Heavy multi-track overdub | [`session_20260714_175327.log`](../../../captures/session_20260714_175327.log) repro | After M6 Phase 2–3: no reboot / no 16s overflow-only tail; `ODUB,stop` or clean user stop |
| HITL 2+2 (optional) | Standard baseline preset | Existing pass criteria + no regression |
| HITL 64+64 | `163446` class failures | Record completes; overdub completes; tier-A transitions present |

## M5 — Spike: SD load path extmem routing

**Documented:** [`spike_sd_load_extmem_routing.md`](spike_sd_load_extmem_routing.md) · DEC-019

- [x] Spike doc + spec delta + implement adopt-on-load, defer boot restore, extmem clone
- [x] Play entry: defer `updateAllTracks(0)` on transport start when no capture pending
- [ ] Native: load/restore heap budget test (64-bar two-pass fixture)
- [ ] HITL: cold boot ×3 → play → clear → re-run M4 64+64 (after M6)

**Note (2026-07-14):** M5 task “`ensurePlaybackWindowBuilt` uses `loop.midiEvents()`” describes shipped interim behavior; **M6 Phase 1 replaces it** with chunk-ref merge per DEC-016 H6.

## M6 — DEC-016 completion: legacy callsite migration (2026-07-14) — **NEXT**

**Plan:** [`docs/plans/multi_track_playback_pressure_closure_refinement.md`](../../../docs/plans/multi_track_playback_pressure_closure_refinement.md)  
**Framing:** Complete approved DEC-016 architecture — not a new playback model  
**Evidence:** manual `174731` PASS vs `175327` FAIL; HITL `163446` RECORDING stall  
**Cancelled:** `#CAP,DIAG,heap` firmware (RAM1 ~−50 KB on `teensy41-capture-serial`)

### Investigation (done)

- [x] HITL `PERS` heap vs `PERS,diag` misparse — [`scripts/hitl/persistence_rows.py`](../../../scripts/hitl/persistence_rows.py)
- [x] Classify failure modes (main-loop stall vs chunk pool)
- [x] Confirm implementation gap: `ensurePlaybackWindowBuilt` still calls `loop.midiEvents()` (H6)
- [x] Cancel DIAG heap firmware; shift to callsite audit + existing `RECS`/`ODUB`/`RING`/`Memory` lines

### Phase 0 — Implementation audit (gate before firmware) — **DONE 2026-07-14**

- [x] Complete materialization callsite matrix (plan doc § Phase 0): 18 production callsites documented
- [x] Sign off **Expected behaviour** per callsite — all map to existing DEC-016 policy; no new architecture
- [x] Verdict: 5 callsites need migration; Phases 1–3 sufficient + Phase 2 expanded (Display capture-active ×2, MidiLedManager P2)
- [x] **User gate:** audit reviewed — proceed to Phase 1

### Phase A — Architecture metrics (first firmware edit)

- [x] Extend [`Diagnostics::Counter`](../../../include/Utils/Diagnostics.h): `LegacyMidiEvents`, `PlaybackFullMaterialize`, `PlaybackDeferredReuse`, `PlaybackWindowRebuild`, `DisplayFullRebuild`, `DisplayIncrementalUpdate` (map existing `PlaybackMergeRebuild` / `VisualCacheRebuild`)
- [x] Add `PlaybackBuildTime` / `DisplayBuildTime` µs accumulators in Diagnostics
- [x] Hook counters at audit callsites; optional `#CAP,DIAG,counter` serial snapshot on capture builds
- [x] Native: extend `test/test_diagnostics`
- [x] **Baseline snapshot** on pre-fix firmware (verify-only HITL or manual capture) before Phase 1

### Phase 1 — Playback window (complete H6 migration)

- [x] `ensurePlaybackWindowBuilt`: `gatherPublishedFlatForDerivedView` policy for `!captureActive()`; chunk merge + capture layer for `captureActive()` (both branches)
- [x] Expose/share `gatherPublishedFlatForDerivedView` on `Loop` (policy extraction — not new architecture)
- [x] `pio test -e native`
- [x] **Manual gate (user):** 174731-style; counter check — hot-path `PlaybackFullMaterialize` / `LegacyMidiEvents` → 0

### Phase 2 — Display defer (complete OVERDUBBING migration + audit additions)

- [x] `DisplayManager::resolveDisplayNotes`: stale-while-revalidate overdub committed layer; stop per-frame full rebuild / sort / flatten when cache warm
- [x] Incremental capture overlay via revision gating / `capturePreview`
- [x] Replace `mergeMaterializedPassesWithCapture` in capture-active PLAYING display paths (~773, ~807)
- [x] `MidiLedManager::prepareLedNoteLookup`: chunk merge when `visualCacheDirty` (P2 — bar-rate)
- [x] **Manual gate (user):** 175327-style; `DisplayFullRebuild` rate drops; `DisplayIncrementalUpdate` ↑ (`session_20260714_212306`: +3840 incremental vs +392 full, ~10:1)

### Phase 3 — Idle gating during capture

- [x] `processDeferredIdleMaintenance`: skip full materialize/visual on non-selected tracks while any track RECORDING/OVERDUBBING
- [x] `TrackManager::anyTrackRecordingOrOverdubbing` + `isSelectedTrack` helpers (reserved; idle defer reverted pending memory fix)
- [ ] **Phase 3 idle defer:** re-introduce after record+multi-play manual gate passes (reverted 2026-07-14 — heap exhaustion on first NoteOn)
- [x] **Record-start headroom:** `releaseBackgroundPlaybackWindowMemory` + live-record display without per-frame flatten
- [ ] **Manual gate (user):** full 5-track stress (175327 config)

### Phase 4 — Close docs + scripts + exit criteria

- [ ] Ship persistence row parser + baseline wiring (if not merged)
- [ ] Update [`long_record_capture_heap_investigation_refinement.md`](../../../docs/plans/long_record_capture_heap_investigation_refinement.md) → point to M6 plan
- [ ] Verify M6 exit criteria: audit, hot-path `rg`, **architecture counter snapshot**, manual gates, native tests
- [ ] Re-run `pio test -e native`; optional HITL 2+2; then M4 64+64 HITL regression
- [ ] `/opsx:archive` when exit criteria met

## Docs (scaffold PR)

- [x] OpenSpec change folder + proposal, design, tasks, spec deltas
- [x] Frontmatter on `64bar_regression_commit_analysis_enhancement.md`
- [x] Append DEC-018 to `DECISION_LOG.md`
- [ ] M6 design § + spec scenarios (DEC-016 completion framing + exit criteria)
