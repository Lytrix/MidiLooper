## Context

M1/M2 shipped **CurrentSet** and flat SavedSet copies. Incremental slot-skip (§1.1–1.3) is verified.
Product model: **Current** always live (epoch persistence); **Save** appends immutable packed
**Revisions**; **Load** restores with explicit prompt when epochs diverge.

**Format bump:** prior SD layouts (`Sets/_current/`, flat SavedSet folders at card root) are
invalidated. No migration.

**Architecture constraint:** This change extends — does **not** rewrite — existing runtime:

- chunk-based loop storage, pass model (`recordPass`, `overdubPasses`, `editPasses`)
- deferred persistence FSM (`requestDeferredSaveState` / `processDeferredSaveState`)
- CurrentSet chunk-bounded SD writes
- playback-first scheduling

Do **not** refactor as part of this change: `StorageManager`, `StorageLoopIo`, `LoopPasses`,
deferred save FSM core, chunk pool, pass materialization hot paths.

## Design decision: Current and Sets are separate

Adopt explicit separation:

```text
current = mutable runtime workspace
sets    = immutable revision history
```

**Current SHALL NOT exist inside Sets.**  
**Sets SHALL NOT contain mutable workspace state.**

Reason: different lifecycle rules; mixing them creates implementation branching and persistence
ambiguity. **CurrentSet** remains the in-code ownership name for the mutable workspace; on SD it
maps to `MidiLooper/current/` only.

Folder names on SD are **lowercase**. UI labels may remain capitalized.

## Requirement: Runtime timing has absolute priority

Persistence, UI, loading, revision creation, catalog updates, and recovery SHALL never measurably
alter MIDI input timing, playback timing, clock accuracy, or recording continuity.

Persistence correctness SHALL be achieved through **resumable epochs**, **deferred FSM slices**, and
**budget-limited writes** — not synchronous completion.

Runtime priority (highest first):

1. MIDI capture
2. Playback scheduling
3. Clock generation
4. Display
5. Persistence

Persistence SHALL yield to higher-priority runtime work via `maxPersistenceMicros` per slice.

## Terminology

| Term | Meaning |
|------|---------|
| **epoch** | Immutable Current snapshot boundary; dirty when `currentEpoch != lastCommittedEpoch` |
| **commitRevision** | Deferred FSM: REQUEST → SNAPSHOT → WRITE → VALIDATE → CATALOG_UPDATE → COMPLETE |
| **loadRevisionIntoCurrent** | Deferred restore from packed blob → new Current epoch |

## Locked decisions

### Persistence

| Topic | Decision |
|-------|----------|
| Current | Epoch-based `MidiLooper/current/`; highest valid epoch at boot |
| Sets | Immutable `MidiLooper/sets/S####/revisions/v####.bin` |
| Revision payload | Canonical pass/chunk storage — no flatten on save path |
| Commit | Reuse deferred FSM; budget-limited WRITE slices; Current unchanged after Save |
| Snapshot | Save commits last **completed** epoch; post-snapshot changes → next epoch |
| Revision ids | Visible only after VALIDATE; failed commit reuses id |
| Schema | `schemaVersion` only (major reject / minor ignore unknown) |
| File names | Runtime fixed-layout records use `.bin` (`workspace.bin`, `set.bin`, `index.bin`, `v####.bin`); human-editable config uses `.json` (`settings.json`) |

### Boot / recovery

| Step | Source |
|------|--------|
| 1 | `MidiLooper/current/` highest valid epoch |
| 2 | Exact derived revision |
| 3 | Latest validated revision on Set |
| 4 | `MidiLooper/recovery/checkpoints/` |
| 5 | Empty |

### Save flow

```text
current → snapshot → revision → validate → update catalog
```

Save SHALL NOT move, clear, or reload Current.

## Root storage structure

```text
/MidiLooper
├── current/
│   ├── workspace.bin
│   ├── transport.bin
│   ├── global.bin
│   ├── slots/
│   │   └── slot_##.bin          # loop payloads (track×slot wire: loop_TT_SS until split)
│   ├── undo/
│   └── temp/
├── sets/
│   ├── index.bin
│   ├── S0001/
│   │   ├── set.bin
│   │   └── revisions/
│   │       ├── v0001.bin
│   │       └── ...
│   └── archive/                 # brownfield flat SavedSet folders (until 3.6)
├── recovery/
│   ├── checkpoints/
│   └── boot.log
├── imports/
├── exports/
└── system/
    ├── schema.bin
    ├── migration.log
    └── settings.json
```

**Interim firmware note:** deferred FSM may still write a single runtime bundle file under
`current/temp/` until transport.bin + global.bin split lands (task 2.x). OpenSpec target names are
`transport.bin` / `global.bin`; interim bundle uses `runtime.bundle.bin` under `current/temp/`.

## File naming

| Kind | Extension | Examples |
|------|-----------|----------|
| Runtime fixed-layout records | `.bin` | `workspace.bin`, `set.bin`, `index.bin`, `v0001.bin`, `transport.bin` |
| Human-editable config | `.json` | `settings.json` (editable in a text editor; no serializer on boot hot path) |

Folder and file stem names describe role (`workspace`, `set`, `v0001`); extension signals read path (memcpy struct vs JSON parse).

## Deferred persistence

One **deferred save FSM** serves Current epoch writes and revision commit stages. No separate queue
workers. Overlay **Save** enters commit **REQUEST** and exits immediately; FSM advances on idle ticks.

## Non-goals (v1)

- Set compaction, revision deduplication, catalog search, partial revision restore, delta encoding
- SD migration from legacy card-root `Sets/`
- Refactor of StorageManager core, chunk pools, pass storage, runtime loop model

## Deferred v2

Load all slots from track, MIDI encoder overlay mapping, revision compaction.

## Overlay wireframe

(See prior — workspace + loop overlay wireframes unchanged)

**Save Current?** — Yes / No / Cancel; Set-revision load only.
