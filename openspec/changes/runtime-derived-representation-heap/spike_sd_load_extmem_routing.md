# Spike — SD load path internal-heap routing (M5 follow-up)

**Status:** Documented spike — not implemented  
**OpenSpec change:** `runtime-derived-representation-heap`  
**Related:** DEC-016 (derived views), DEC-018 (M1–M4), [`docs/Plans/64bar_regression_commit_analysis_enhancement.md`](../../../docs/Plans/64bar_regression_commit_analysis_enhancement.md)

## Problem

M2 routed **runtime** published flat (`passesMaterializedStore_`) and playback-window builds to `SessionMidiEventVec`. After a failed 64+64 HITL run, the device reported **0 bytes** internal heap and blocked clear (`Clear aborted: could not complete deferred save first`, `PERS,defer,0,0,heap_floor`). Reboot restored the same broken loop from SD recovery checkpoints even when `MidiLooper/current/` was cleared manually.

Investigation shows the **SD load / pass-restore path** still materializes full-loop vectors on **internal heap**, independent of M2.

## Evidence (2026-07-07)

| Observation | Source |
|-------------|--------|
| 64-bar record-only PASS, heap_before=81920 | `captures/host_midi_automation_baseline_20260707_171500.json` |
| 64+64 FAIL — missing `#CAP,ST,OVERDUBBING,PLAYING` (ring pressure); device overdub stopped | `captures/host_midi_automation_baseline_20260707_171942.json`, session log `Overdub stopped:` |
| Post-failure clear blocked at heap_floor | Later HITL runs (`175415`, etc.) |
| Broken loop reloads after SD removal / clear | Boot recovery chain restores `MidiLooper/recovery/checkpoints/_*` |

## Root cause (code)

### 1. `deepCloneChunkRefs` — internal-heap full flatten per pass

`Loop.cpp` clones every pass chunk list by flattening to `MidiEventVec` (internal heap), then re-chunking:

```134:144:src/Loop.cpp
ChunkIdList deepCloneChunkRefs(const ChunkIdList& refs) {
  if (refs.empty()) {
    return {};
  }
  MidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(refs, flat);
  LoopEventStore store;
  store.loadFromEvents(flat);
  ChunkIdList cloned;
  store.detachChunksTo(cloned);
  return cloned;
}
```

Called from `deepCloneRecordPass`, `deepCloneOverdubPass`, and `deepClonePasses` — used by:

- `Loop::restorePassesSnapshot` (SD load, undo restore)
- `Loop::shareForSnapshot` (undo snapshot push)

For a 64+64 loop with record + overdub passes, load/restore pays **one full internal-heap materialize per pass** before M2 extmem routing applies.

### 2. Eager visual rebuild on load

`restorePassesSnapshot` calls `rebuildVisualCacheFromPasses()` synchronously at end of load:

```534:553:src/Loop.cpp
void Loop::restorePassesSnapshot(const PersistedLoopSnapshot& snapshot) {
  // ...
  passes = deepClonePasses(snapshot.passes);
  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
  rebuildVisualCacheFromPasses();
}
```

`rebuildVisualCacheFromPasses` already uses `SessionMidiEventVec` for its flat gather — acceptable for derived view — but it runs **during boot load** for every restored slot, not deferred to idle maintenance (Phase C policy).

### 3. Boot load scope

`StorageManager` boot path:

- Tries `MidiLooper/current`, then **recovery checkpoints**, then SavedSet fallback.
- Loads **all 8×8 slots** (or every slot in current set) via `loadLoopSlotFromCurrentSetSd` → `StorageLoopIo` → `restorePassesSnapshot`.
- May queue derived revision reload after recovery.

Undo runtime bundle and per-slot snapshots can add further internal-heap copies on top of pass clone.

## Relationship to M1–M4

| Layer | M2 target | Load path today |
|-------|-----------|-----------------|
| Published flat at runtime | `SessionMidiEventVec` | Discarded on restore; rebuilt later via `midiEvents()` |
| Pass chunk ownership | PSRAM chunks + refs | Clone via **internal** `MidiEventVec` flatten |
| Display derived view | Idle bar-slice / stale-while-revalidate | **Sync** `rebuildVisualCacheFromPasses` on every restore |
| Persistence admission | Current heap at dispatch (M1) | Blocked when load leaves 0 bytes free |

M4 64+64 gate failure (missing ST line) is **capture ring** (M3). Post-failure **clear / re-test** failure is this **load + recovery** spike.

## Proposed direction (spike — do not implement without approval)

1. **`deepCloneChunkRefs` → extmem flat** — use `SessionMidiEventVec` (or chunk-ref shallow copy if snapshot semantics allow sharing immutable chunks without re-flatten).
2. **Defer visual rebuild on load** — set `visualCacheDirty = true` only; let `processDeferredIdleMaintenance` / bar-slice rebuild run when heap allows (align with Phase C).
3. **Lazy slot load** — load active track/slot first; defer non-active slots until selected or idle (reduces boot peak for multi-slot sets).
4. **Recovery quarantine** — `!QUARANTINE_WORKSPACE` boot command (implemented separately) to break reload loop during HITL; not a substitute for load-path extmem.
5. **Native test** — restore 64-bar two-pass fixture; assert internal heap delta bounded vs full-loop × pass count.

## Open questions

- Can undo snapshots **share** chunk IDs without `deepCloneChunkRefs` re-flatten (ref-count / immutability contract)?
- Does `shareForSnapshot` need deep clone at push time, or only `restoreFromSnapshot` clone?
- Boot load: minimum slot set to restore for transport-ready state?

## Verification (when implemented)

- `pio test -e native` — load/restore heap budget test
- HITL: quarantine → clean boot → 64+64 track 2/slot 1 (M4 gate)
- Serial: `Boot recovery chain exhausted` OR successful load with `DIAG,VisualCacheRebuild` deferred to idle

## References

- [`design.md`](design.md) § M5 spike summary
- [`tasks.md`](tasks.md) § M5
- [`specs/internal-heap-external-memory-routing/spec.md`](specs/internal-heap-external-memory-routing/spec.md) — ADDED load-path scenarios
- DEC-019 (load-path extmem routing spike)
- **Handoff:** [`docs/Plans/m5_sd_load_extmem_routing_handoff.md`](../../../docs/Plans/m5_sd_load_extmem_routing_handoff.md)
