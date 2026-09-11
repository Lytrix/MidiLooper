# Immutable LoopSnapshot 5.2 — storage layout approval

**Status:** APPROVED (docs/decision only) — 2026-09-11  
**OpenSpec:** `immutable-loop-snapshot-passes` task **5.2**  
**Decision:** [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)  
**Firmware:** deferred to task **5.3** (not part of this approval)

**Change:** `immutable-loop-snapshot-passes` task **5.2**  
**Kind:** Docs / decision approval only (no firmware — that is **5.3**)  
**Architect:** REVISE applied — `LSV1` throughout; `LoopSnap01` rejected; `formatVersion != 1` unsupported (not legacy).

**Pinned:**
- Q1=1 breaking body bump with an explicit format identifier
- Q2=1 dual-write (`PersistedLoopSnapshot` + latest `LoopSnapshot`) during migration
- Leading format identifier = **`LSV1`** (fixed 4-byte ASCII tag)
- Body format version field = **`uint16_t formatVersion = 1`** immediately after `LSV1`
- **`LoopSnap01`:** rejected / not part of the wire format

**Rationale (user):** `LoopSnapshot` is the new canonical representation; dual-write keeps the compatibility `PersistedLoopSnapshot` wire for migration without requiring old readers to understand the `LoopSnapshot` section.

## Format identity (keep distinction explicit)

| Role | Name |
|---|---|
| Human / DEC name | **LoopSnapshot body format version 1** |
| Wire discriminator | **`LSV1`** — 4-byte ASCII, encoded using the existing fixed-token wire convention (`EPT3` / `GEO1` / `OSI1`) |
| Body format version field | **`uint16_t formatVersion = 1`** immediately after `LSV1` |
| Metadata section | **`LoopMetadata`** |
| Compatibility section | **`PersistedLoopSnapshot`** |
| Authority section | **`LoopSnapshot`** |

Vocabulary in DEC/docs: **format identifier** / **wire discriminator** — not “magic.”

DEC-057 defines: **`LSV1` = LoopSnapshot body format version 1** (wire discriminator for that named format).  
`formatVersion` after `LSV1` is an extensibility field for v1 readers: **only `1` is supported**. Values other than `1` are **unsupported/rejected** — they do **not** fall through to legacy handling, and 5.2 does **not** pre-approve a version-2 body interpretation.

Rejected draft tags (not on wire): `LSP1`, `LoopSnap01`, `META`, `PASS`, `SNAP`.

## Current format identifier (today)

**There is none on the loop slot body.**

[`writePersistedLoopSnapshot`](src/StorageLoopIo.cpp) starts the body at `loopId` / geometry / allocator scalars. Nested fixed 4-byte markers appear later: `EPT3`, `GEO1`, `OSI1`. Outer CurrentSet uses `SAVE` / `REVPK02` elsewhere — those are not the loop-body format identifier.

**Legacy rule (explicit):** absence of leading **`LSV1`** means **legacy** `PersistedLoopSnapshot` body, routed through the deferred migration path in 5.3. Not merely “old format” wording — **legacy**.

## Architecture gate (Phase 4 / task 5.2)

| Question | Answer |
|---|---|
| **Owner** | Approval artifacts: `DECISION_LOG` + change `design.md` + layout plan; runtime owners unchanged (`StorageManager` / `StorageLoopIo` / `Loop`) |
| **Primary invariant** | Approved on-disk contract for content-only persist of latest complete `LoopSnapshot` + migration dual-write; no undo lineage |
| **Ownership change?** | NO |
| **State transition change?** | NO for 5.2 (approval only); load undo-depth-0 is contracted for 5.3 |
| **Execution bound change?** | NO in 5.2; 5.3 classifies deferred units |
| **Reuse** | YES — extend existing CurrentSet slot path and deferred stages; do not invent a second persistence owner |

## Approved layout (concrete)

### Identity

- **Format name:** LoopSnapshot body format version 1
- **Leading format identifier (wire discriminator):** 4-byte ASCII `LSV1`, encoded using the existing fixed-token wire convention
- **Body format version:** `uint16_t formatVersion = 1` immediately after `LSV1`
- **Flags:** `uint16_t flags` after `formatVersion` (bit0 = `hasPersistedLoopSnapshot`, must be 1 during migration dual-write)
- **Outer container:** CurrentSet / epoch / `SAVE` / `REVPK02` unchanged. No `CONTAINER_VERSION` bump for 5.2.
- **Legacy:** first four body bytes ≠ `LSV1` ⇒ **legacy** `PersistedLoopSnapshot`; deferred migration in 5.3. No silent treat-as-LSV1.
- **Unsupported:** `LSV1` present but `formatVersion != 1` ⇒ **reject** that slot load (unsupported), **not** legacy fallthrough.

### File shape (CurrentSet slot)

```text
[optional EpochFileHeader 10 B]     # unchanged
LSV1 + uint16 formatVersion(=1) + uint16 flags
section "LoopMetadata"
section "PersistedLoopSnapshot"     # dual-write migration
section "LoopSnapshot"              # latest complete immutable snapshot
[SAVE 4 B]                           # unchanged
```

### Section framing (actual type/role names)

```text
uint8_t  nameLength
char     name[nameLength]     # UTF-8, no NUL required
uint32_t payloadByteLength
uint8_t  payload[payloadByteLength]
```

| Section name | Payload |
|---|---|
| `LoopMetadata` | Loop id / geometry / allocator counters |
| `PersistedLoopSnapshot` | Exact current `writePersistedLoopSnapshot` stream (`EPT3` / `GEO1` / `OSI1` nested as today) |
| `LoopSnapshot` | Latest complete immutable `LoopSnapshot` (logical objects + membership) |

Unknown section names: skip by `payloadByteLength` (forward compatible within supported `LSV1` + `formatVersion == 1`). Nested `EPT3`/`GEO1`/`OSI1` inside the `PersistedLoopSnapshot` payload are unchanged.

### `LoopMetadata` payload

Same counters/geometry already at the head of [`PersistedLoopSnapshot`](include/StorageLoopIo.h):

- `loopId`, `startLoopTick`, `loopLengthTicks`, `loopStartTick`
- `nextPassId`, `nextNoteId`, `nextMergeSequence`, `lastCommittedPassId`

No C++ pointers. No published revision (runtime-only).

### `PersistedLoopSnapshot` payload (migration dual-write)

Exact current `writePersistedLoopSnapshot` capture+edit+`EPT3`+`GEO1`+`OSI1` stream. Dual-write supports rebuild of compatibility `LoopPasses` and migrate/oracle during transition; §6 may later drop this section.

### `LoopSnapshot` payload (latest complete snapshot)

Logical content only — sole persisted complete snapshot/root:

1. **Root header:** `loopId`, `loopLengthTicks`, allocator high-water / next logical ids
2. **Immutable objects:** `LoopSnapshotNote` / `LoopSnapshotEvent` with stable logical ids — not RAM chunk IDs
3. **Membership:** logical `NoteRef` / `EventRef` lists (span + event index), canonical order
4. **Selected root:** one `LoopSnapshot` section per slot file; no snapshot lineage

**Exclusions** (DEC-035 / DEC-044 / 5.1): GUS / undo lineage / `beforePublishedSnapshot` / `afterPublishedSnapshot` / VisualCache / ledgers / cursors / provisional `E:`/`O:` stacks. Logical refs on disk; RAM page packing is not the file format.

### Load / handle rebuild contract (binds 5.3)

```text
LSV1 absent:
  LEGACY PersistedLoopSnapshot body
  existing readPersistedLoopSnapshot path
  deferred migration MAY rebuild a complete LoopSnapshot
  next successful save writes LSV1 dual-write

LSV1 present AND formatVersion != 1:
  UNSUPPORTED — reject slot load
  do NOT treat as legacy
  do NOT parse as PersistedLoopSnapshot authority

LSV1 present AND formatVersion == 1:
  parse LoopMetadata + PersistedLoopSnapshot + LoopSnapshot
  Loop rebuilds local handles from LoopSnapshot logical refs
  Loop selects the LoopSnapshot section as published root
  runtime undo depth = 0, redo empty
  do NOT invent U: depth from passes
```

Footer GUS remains empty headers only (DEC-035 Stage 3).

### Rollback / read-failure invariant

- Failed/partial `LSV1` write: retain previously valid card file (existing deferred finalize / verify-rename policy).
- **New-format file (`LSV1` + `formatVersion == 1`) with missing or unparseable required `LoopSnapshot` section: load failure for that slot. Must not fall back to the compatibility `PersistedLoopSnapshot` section as authority.**

## 5.2 deliverables (shipped with this approval)

1. [x] Plan → [`docs/Plans/immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md`](immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md) (+ optional dated copy).
2. [x] **DEC-057:** LoopSnapshot body format version 1; wire discriminator `LSV1`; `formatVersion == 1` only; dual-write; load undo depth 0; legacy = absence of `LSV1`; unsupported = wrong `formatVersion`; no `LoopSnap01`; exclusions.
3. [x] Close open question in change [`design.md`](openspec/changes/immutable-loop-snapshot-passes/design.md); point to DEC-057.
4. [x] Note in [`Storage.md`](docs/Authority/Architecture/Storage.md).
5. [x] Check off **5.2** in `tasks.md`; update CURRENT_WORK / PROJECT_STATE; ARCHITECTURE-REVIEW Phase 4 note (5.3 still open).

## Explicit non-goals

- 5.3 reader/writer firmware, migration units, `ExecutionBounds.md` rows
- Dropping the `PersistedLoopSnapshot` section / retiring `LoopPasses` (§6)
- Changing CurrentSet `CONTAINER_VERSION` or `REVPK02`
- Persisting undo lineage or published-identity GUS fields
- Specifying RAM page packing as on-disk layout
- Pre-approving `formatVersion >= 2` body interpretation
