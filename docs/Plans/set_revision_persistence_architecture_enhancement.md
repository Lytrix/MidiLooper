# Set / Revision persistence architecture

**Date:** 2026-06-27 (updated 2026-08-10)  
**Status:** Approved — core shipped on `dev`; overlay 4.1–4.7 shipped; remaining §4.8–4.10 + parked §3.9  
**OpenSpec:** `openspec/changes/set-revision-persistence/`  
**Handoff:** [`set_revision_persistence_handoff.md`](set_revision_persistence_handoff.md)

---

## Design decision

```text
current = mutable runtime workspace   →  MidiLooper/current/
sets    = immutable revision history  →  MidiLooper/sets/
```

**Current SHALL NOT exist inside Sets.**  
**Sets SHALL NOT contain mutable workspace state.**

Recovery checkpoints: `MidiLooper/recovery/` (not inside current or sets).

## Architecture alignment (do not rewrite)

Extends existing:

- Deferred save FSM + CurrentSet chunk writes
- Pass/chunk canonical storage (`LoopPasses`, `StorageLoopIo`)
- Playback-first scheduling

Does **not** refactor: `StorageManager` core, `LoopPasses`, chunk pool, materialize hot paths.

## Runtime priority

1. MIDI capture → 2. Playback → 3. Clock → 4. Display → 5. Persistence (`maxPersistenceMicros`)

## Storage model

| Layer | SD path | Model |
|-------|---------|--------|
| **Current** | `MidiLooper/current/` | Epoch-based; dirty = `currentEpoch != lastCommittedEpoch` |
| **Sets** | `MidiLooper/sets/S####/` | Immutable `v####.bin` snapshots (REVPK02 chunk stream) |
| **Recovery** | `MidiLooper/recovery/checkpoints/` | Boot fallback after Current + derived revision |
| **Commit** | FSM: REQUEST → SNAPSHOT → WRITE → VALIDATE → CATALOG → COMPLETE | Current unchanged after Save |

### REVPK02 revision blob (LMDB-inspired)

```text
[Fixed 128 B header]  identity, sourceEpoch, chunkCount, payloadSize, headerCrc
[Chunk stream]        Transport | LoopSlot (StorageLoopIo v5) | SlotIndex directory
[12 B footer]         completeMagic + payloadCrc32 + fileSize
```

`LoopSlot` chunk bodies carry canonical pass storage (`recordPass`, `overdubPasses[]`, `editPasses[]`).
`SlotIndex` chunk is written last during commit so directory offsets match final payload layout.

## File naming

Runtime fixed-layout records use `.bin` for fast struct reads (`memcpy` / fixed wire layouts):

- `workspace.bin`, `set.bin`, `index.bin`, `v0001.bin`, `transport.bin`, `global.bin`

Human-editable config uses `.json` (e.g. `system/settings.json`) so values can be edited outside the device UI.

Folder and file stem names describe role; extension signals read path.

## Deferred v2

Compaction, deduplication, catalog search, partial restore.

---

See `openspec/changes/set-revision-persistence/design.md`.
