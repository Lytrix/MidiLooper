# Tasks — runtime-derived-representation-heap

## Done (Phase A→C + `cdd9c2b`)

- [x] Phase A: playback defer, REVT gate, display stale-while-revalidate, chunk-ref merge paths (`d635296`, follow-ups)
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

## M4 — Verification gates

- [x] 64-bar record-only PASS (`20260707_184646`, seal heap=57344, PERS ok)
- [ ] 64+64 track 2/slot 1 — partial (`20260707_192649`): `OVERDUBBING→PLAYING` + overdub MIDI 513/513 PASS; seal heap 53,248; serial ends after `ODUB,stop,set_state` (flush/finalize/SEVT/PERS missing; USB disconnect)
- [x] `pio test -e native` (473/473)
- [x] Update `docs/runtime/CURRENT_WORK.md`, `PROJECT_STATE.md`
- [ ] Archive change when 64+64 gate passes (`/opsx:archive`)

## M5 — Spike: SD load path extmem routing (follow-up)

**Documented:** [`spike_sd_load_extmem_routing.md`](spike_sd_load_extmem_routing.md) · DEC-019

M2 fixed runtime published flat; **SD load / undo restore** still flattens each pass via internal-heap `MidiEventVec` in `deepCloneChunkRefs` and eagerly rebuilds visual cache on `restorePassesSnapshot`. Boot recovery reloads long loops into 0-byte heap, blocking clear and M4 re-runs.

- [x] Spike doc: root cause, evidence, proposed direction, open questions
- [x] Spec delta: load-path scenarios in `internal-heap-external-memory-routing`
- [x] Cross-link [`64bar_regression_commit_analysis_enhancement.md`](../../../docs/plans/64bar_regression_commit_analysis_enhancement.md)
- [x] **Implement** `deepCloneChunkRefs` → `SessionMidiEventVec` (undo / share only)
- [x] **Implement** defer `rebuildVisualCacheFromPasses` on load — idle bar-slice only
- [x] **Implement** adopt-on-load — `adoptPersistedSnapshot` / `applySnapshotToLoop` (no deep clone on SD restore)
- [x] Native: `test_sd_load_adopt` pool budget (single pool copy after load)
- [x] **Implement** defer inactive loop slot restore at boot — `loadCurrentSetBundleAndActiveLoopSlots`, `processDeferredLoopSlotRestore`, `requestLoopSlotRestoreFromSd`
- [x] **Implement** defer undo snapshot bodies at boot — `readGlobalUndoStackMetadataFromFile`, idle `processDeferredUndoSnapshots`
- [x] Play entry: `ensurePlaybackWindowBuilt` uses `loop.midiEvents()`; defer `updateAllTracks(0)` on transport start when no capture pending
- [ ] Native: load/restore heap budget test (64-bar two-pass fixture)
- [ ] HITL: restore quarantined workspace → cold boot ×3 → play → clear → re-run M4 64+64 gate

## Docs (scaffold PR)

- [x] OpenSpec change folder + proposal, design, tasks, spec deltas
- [x] Frontmatter on `64bar_regression_commit_analysis_enhancement.md`
- [x] Append DEC-018 to `DECISION_LOG.md`
- [x] Mark superseded Cursor plans (user-local)
