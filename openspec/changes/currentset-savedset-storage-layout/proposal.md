## Why

CurrentSet currently uses per-slot loop files, which is good for incremental writes, but transport-stop paths can still trigger broad rewrites and SavedSet packaging is not yet finalized for low file-count snapshots. We need a storage layout that keeps live capture/edit persistence fast while improving SD wear behavior and slot-list metadata lookup latency.

## What Changes

- Keep **CurrentSet** (`Sets/_current`) as a mutable, incremental-write layout:
  - `workspace.bin` as the live index and transport/footer state
  - per-slot `loop_TT_SS.bin` files for loop payloads
- Add explicit slot-summary metadata in CurrentSet `workspace.bin` for fast slot-list queries without opening loop files.
- Define **SavedSet** as immutable packed snapshots with low file count:
  - `set.bin` with SavedSet metadata and packed-loop index table
  - `loops.bin` containing packed loop payload blobs for non-empty slots
- Separate write policy by scope:
  - live runtime mutations stay per-slot incremental
  - snapshot/export paths use packed `loops.bin`
- Clarify and tighten stop/save policy so transport stop does not force full-slot rewrites by default.
- Define migration/read compatibility rules so existing CurrentSet v6 per-slot trees remain loadable.

**BREAKING**

- SavedSet on-disk payload layout changes from per-slot loop files to packed `loops.bin` + index table in `set.bin` (CurrentSet layout is retained).

## Capabilities

### New Capabilities

- `current-set-live-storage`: Mutable CurrentSet layout and write policy optimized for dirty-slot incremental persistence.
- `saved-set-packed-storage`: Immutable SavedSet two-file packaging (`set.bin` + `loops.bin`) with blob index table.
- `slot-metadata-index`: Slot summary/index fields for fast slot-list browsing/search without loop-file reads.

### Modified Capabilities

- *(none in `openspec/specs/`; this change introduces new capability specs)*

## Impact

| Area | Primary files/systems |
|------|------------------------|
| CurrentSet writer FSM | `src/StorageManager.cpp`, `include/StorageManager.h` |
| Path/format helpers | `src/CurrentSetStorage.cpp`, `include/CurrentSetStorage.h` |
| SavedSet copy/load pipeline | new SavedSet storage helpers under `src/` and `include/` |
| Slot-list browser read path | `src/DisplayManager.cpp`, set browser flow once M2 lands |
| Tests | new native format/index tests + migration tests; HITL baseline unchanged |

Open decisions (TBD):

- Whether transport stop forces full-slot dirty marking in any non-recovery path.
- Whether SavedSet keeps optional per-slot sidecar files for diagnostics.
- Exact slot-summary field set in CurrentSet/SavedSet meta for browser rendering.

Brownfield references:

- `docs/DELIVERABLE_TRACKING.md`
- `docs/plans/workspace_session_persistence_handoff.md`
- `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`
