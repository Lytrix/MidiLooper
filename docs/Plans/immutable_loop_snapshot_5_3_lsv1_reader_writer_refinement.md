# OpenSpec 5.3 — LoopSnapshot body format LSV1 reader/writer + migration

**Status:** **5.3.2 SHIPPED** — 5.3.3 load authority next  
**OpenSpec:** `immutable-loop-snapshot-passes` task **5.3**  
**Authority:** [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write), [5.2 plan](immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md)  
**Architect:** APPROVED WITH TIGHTENING (2026-09-11)

## Vocabulary (DEC-057 aligned)

| Term | Meaning |
|---|---|
| **slot file** | Complete `loop_TT_SS.bin` persistence artifact (epoch header + payload + SAVE) |
| **LoopSnapshot body format** | LSV1 payload layout inside that artifact — canonical 5.3 domain name |
| **LSV1** | Format identifier for **LoopSnapshot body format version 1** |
| **StorageLoopIo** | Implementation owner (`read` / `write` / `measure`) — not part of the domain name |

**Avoid in new 5.3 prose:** wire, codec, slot body format.

**Reader/writer phrase:** **LoopSnapshot body format reader/writer** (not “LSV1 codec”, not “slot body format”).

## Architecture gate (Phase 4 / task 5.3)

| Question | Answer |
|---|---|
| **Owner** | Extend `StorageLoopIo` + `StorageManager` deferred save/load. `Loop` performs atomic publication handoff only (Stage 3). |
| **Primary invariant** | LSV1 dual-write; `LoopSnapshot` section is load authority; legacy = no `LSV1`; wrong `formatVersion` = reject; undo depth 0; no `rebuildSlotFromLoopContent` on LSV1 load |
| **Ownership change?** | NO |
| **State transition change?** | YES — LSV1 load selects published `LoopSnapshot` via `storePublishedSnapshot`; must not call `TrackUndo::rebuildSlotFromLoopContent` |
| **Execution bound change?** | Extend existing rows 14/15 (`processDeferredSaveState`, `LoadLoopJob`) with named substeps and caps — **no new execution-bound class** |
| **Reuse** | YES — extend `writeLoopPersisted`, `writePersistedLoopSnapshot`, `stepPersistedLoopSnapshotParse`, `stepDeferredLoopSnapshotPersist` |

## Sequencing (locked)

**LoopSnapshot body format reader/writer first → native round-trip → save hook → load authority switch → save-time migration → bounds/docs.**

Not one mega-commit. Commit boundaries:

| Commit | Scope |
|---|---|
| **5.3.1** | LoopSnapshot body format reader/writer + native tests only (no SD behavior change) — **SHIPPED** |
| **5.3.2** | Save integration (sizing + writer + legacy fallback + deferred verify) — **SHIPPED** |
| **5.3.3** | Load authority (probe + stepped parse + candidate + atomic publish + depth 0) |
| **5.3.4** | Save-time migration + ExecutionBounds + runtime docs + RAM1 + native + HITL smoke |

**5.4–5.6** remain separate verification tasks.

---

## LoopSnapshot body-format primitives (frozen before 5.3.1)

**Rule:** LSV1 is **little-endian, fixed-width** for all multi-byte scalars. Every field has an explicit width below. Native and Teensy targets share this layout (host tests use the same struct sizes as firmware).

### Body header (immediately after optional epoch header)

| Field | Width | Type | Notes |
|---|---|---|---|
| Format identifier | 4 | ASCII | `LSV1` — existing fixed-token convention (`EPT3` / `GEO1` / `OSI1`) |
| `formatVersion` | 2 | `uint16_t` LE | Must be **1**; else reject (not legacy) |
| `flags` | 2 | `uint16_t` LE | Bit0 = `hasPersistedLoopSnapshot` (must be 1 during dual-write). **Unknown flag bits → reject** |

### Section framing (repeated until end of body)

| Field | Width | Type | Notes |
|---|---|---|---|
| `nameLength` | 1 | `uint8_t` | 0–64 inclusive; **0 or >64 → reject** |
| `name` | `nameLength` | UTF-8 bytes | No NUL terminator |
| `payloadByteLength` | 4 | `uint32_t` LE | **Must be ≤ remaining body bytes**; overflow check before allocation |
| `payload` | `payloadByteLength` | bytes | Parsed per section name |

Allowed section names (exact UTF-8): `LoopMetadata`, `PersistedLoopSnapshot`, `LoopSnapshot`.

**Maximum section payload:** `min(remainingBodyBytes, kLsv1MaxSectionPayloadBytes)` where `kLsv1MaxSectionPayloadBytes = 64 * 1024 * 1024` (policy cap for corrupt files; normal slot files are far smaller).

### `LoopMetadata` payload

Matches [`PersistedLoopSnapshot`](../include/StorageLoopIo.h) header scalars (same widths as today’s legacy body start):

| Field | Width | Type |
|---|---|---|
| `loopId` | 4 | `LoopId` (`uint32_t` LE) |
| `startLoopTick` | 4 | `uint32_t` LE |
| `loopLengthTicks` | 4 | `uint32_t` LE |
| `loopStartTick` | 4 | `uint32_t` LE |
| `nextPassId` | 4 | `PassId` (`uint32_t` LE) |
| `nextNoteId` | 4 | `NoteId` (`uint32_t` LE) |
| `nextMergeSequence` | 4 | `uint32_t` LE |
| `lastCommittedPassId` | 4 | `PassId` (`uint32_t` LE) |

Fixed payload size: **32 bytes**. Wrong length → reject.

### `PersistedLoopSnapshot` payload

**Invariant:** byte-for-byte output of existing `writePersistedLoopSnapshot()` — **no semantic or layout change in 5.3**. LSV1 dual-write does not alter the compatibility representation.

### `LoopSnapshot` payload (logical content only)

| Region | Layout |
|---|---|
| **Root header** | `loopId` u32, `loopLengthTicks` u32, `loopStartTick` u32, `nextNoteId` u32, `nextEventId` u32 — **20 bytes** |
| **Notes** | `noteCount` u32 LE, then `noteCount` × **note record** (20 bytes each) |
| **Note record** | `noteId` u32, `pitch` u8, `velocity` u8, `padding` u16 (0), `startTick` u32, `endTick` u32 |
| **Events** | `eventCount` u32 LE, then `eventCount` × **event record** |
| **Event record** | `eventId` u32, `canonicalOrder` u32, `midiEventBytes` **`kLsv1LoopSnapshotMidiEventBytes`** |
| **Note membership** | `pitchMembershipSectionCount` u32 LE, then per pitch with refs: `pitch` u8, `barEntryCount` u32, per bar: `barIndex` u32, `refCount` u32, `refCount` × `noteId` u32 |
| **Event membership** | `barEntryCount` u32 LE, per bar: `barIndex` u32, `refCount` u32, `refCount` × `eventId` u32 |

**`kLsv1LoopSnapshotMidiEventBytes`:** `sizeof(MidiEvent)` on the firmware/native-test target, matching existing pass/event batch writes in [`StorageLoopIo.cpp`](../src/StorageLoopIo.cpp). **SysEx with non-null `data` pointer → reject at write.** On read: `data` pointer fields must be null/zero; non-zero pointer field → reject.

No page packing, no `shared_ptr`, no chunk IDs on disk.

### Single authoritative layout module

`measureLoopSnapshotBodyFormatV1Bytes()` and `writeLoopSnapshotBodyFormatV1()` **must share the same field definitions** (one struct table or shared `measure` calling the same size walk as write). **No independently duplicated byte-count formula.**

---

## Section rules (fail-closed)

| Condition | Result |
|---|---|
| Unknown section name | **Skip** (forward compatible) |
| Missing `LoopSnapshot` section | **Reject** |
| Duplicate `LoopSnapshot` | **Reject** |
| Duplicate `LoopMetadata` | **Reject** |
| Duplicate `PersistedLoopSnapshot` | **Reject** |
| Truncated section header | **Reject** |
| `payloadByteLength` > remaining body | **Reject** |
| Malformed section payload | **Reject** |
| Unknown **header** flag bits | **Reject** |
| `formatVersion != 1` | **Reject** (not legacy) |
| Absence of `LSV1` | **Legacy** path |

---

## Read / allocation bounds (format validation, not runtime owners)

Before allocating from untrusted SD bytes, validate:

| Check | Rule |
|---|---|
| `noteCount` / `eventCount` | `count × recordSize ≤ remainingBytes`; `count ≤ kLsv1MaxSnapshotObjects` (**1_048_576**) |
| Membership `refCount` | `refCount × 4 ≤ remainingBytes`; `refCount ≤ kLsv1MaxMembershipRefsPerBar` (**4096**) |
| `pitch` | 0–127 |
| `barIndex` | `< maxBarIndex(loopLengthTicks)` where `maxBarIndex = loopLengthTicks > 0 ? (loopLengthTicks - 1) / ticksPerBar : 0` |
| Object ID uniqueness | All `noteId` / `eventId` in object tables unique and non-zero |
| Membership refs | Every `noteId` / `eventId` in membership exists in object table; no duplicate ref per (pitch, bar, id) or (bar, eventId) |
| Tick sanity | `startTick` / `endTick` valid for wrap semantics; `endTick` may be `< startTick` only when wrap note |
| Allocator high-water | `nextNoteId` / `nextEventId` > max loaded id (or equal if empty) |
| Arithmetic overflow | Reject any `count * elementSize` overflow |

These are **LoopSnapshot body-format read guards** inside the LoopSnapshot body format reader — not new interval-path execution owners.

---

## Publication boundary (Stage 1 vs Stage 3)

### Stage 1 / LoopSnapshot body format read path (no runtime mutation)

```text
probe → parse + validate → construct unpublished snapshot → validate complete → freeze → return candidate
```

**Contract:**

- `readLoopSnapshotBodyFormatV1()` / stepped parse helpers **never** mutate the currently published `LoopSnapshot`.
- **Never** publish a candidate.
- Returns `ImmutableLoopSnapshotRef` candidate or failure.

### Deserialization construction API (ownership pin)

> The cold rebuild / deserialization construction primitive builds a `LoopSnapshot` from validated LoopSnapshot body-format bytes only. It has **no publication authority**, **no runtime ownership**, **no deferred scheduling**, and is **not** used by capture/edit/runtime paths. Exact access mechanism (`friend` vs internal TU) is implementation detail.

### Stage 3 only (atomic handoff)

```text
validated candidate → storePublishedSnapshot() → Loop selects published root
```

- **Do not** call `TrackUndo::rebuildSlotFromLoopContent()` on LSV1 load.
- GUS depth **0**, redo empty.
- Native test must assert LSV1 load path never invokes `rebuildSlotFromLoopContent` (observable stub or call counter).

---

## LSV1 load: `PersistedLoopSnapshot` is non-authoritative

On LSV1 load, `PersistedLoopSnapshot` is retained only as **compatibility metadata / oracle data**.

**Must never** construct, repair, supplement, or replace loaded `LoopSnapshot` content from passes.

| Situation | Behavior |
|---|---|
| `LoopSnapshot` validates | **Authority** = `LoopSnapshot` section |
| Oracle mismatch (passes vs snapshot) | **Diagnostic only** (warn once per load); **never merge** |
| Missing / invalid `LoopSnapshot` | **Reject slot file load**; no fallback to `PersistedLoopSnapshot` |

---

## Save path (5.3.2)

| Condition | Body written |
|---|---|
| Published complete `LoopSnapshot` available | **LSV1** dual-write (`flags.hasPersistedLoopSnapshot = 1`) |
| No published snapshot (pre-hydration) | **Legacy** bare `PersistedLoopSnapshot` only |

**Save-time migration:** legacy load → existing deferred hydration → published snapshot → **next successful deferred save** writes LSV1. No eager migration owner.

`measureLoopSlotFileBytes()` uses the same LoopSnapshot body-format layout as `writeLoopSnapshotBodyFormatV1()` (see layout module above).

Failed/partial write: retain prior valid slot file (existing deferred finalize / verify-rename).

---

## Load path (5.3.3)

| Probe | Path |
|---|---|
| No `LSV1` | Legacy: existing parse + `applySnapshotToLoop` + `rebuildSlotFromLoopContent` / `queueLoadedSnapshotPublish` |
| `LSV1` + `formatVersion != 1` | Reject |
| `LSV1` + `formatVersion == 1` | Stepped LSV1 parse under `LoadLoopJob` budgets; adopt passes for oracle only; atomic `storePublishedSnapshot`; **no** `rebuildSlotFromLoopContent` |

Extend `LoadLoopJob` substeps (row 15): probe → parse section → construct candidate → validate/finalize — each step capped; resumable state like existing `stepPersistedLoopSnapshotParse`.

---

## Native test matrix (5.3.1 minimum)

### Positive

- LSV1 dual-write round-trip → equivalent snapshot
- Empty/minimal snapshot
- Multiple notes same pitch
- Bar-crossing note
- Multiple membership refs
- Event membership
- Non-contiguous IDs
- Unknown section skipped

### Negative

- Wrong format identifier → legacy path
- Truncated LSV1 header
- `formatVersion != 1`
- Unsupported header flags
- Missing `LoopSnapshot`
- Duplicate `LoopSnapshot` / `LoopMetadata` / `PersistedLoopSnapshot`
- Truncated section header / payload
- Section length beyond body
- Malformed object counts
- Duplicate object IDs
- Dangling membership ID
- Invalid pitch / bar
- Oversized counts (allocation-bound violation)

### Authority assertion

- **LSV1 load never calls `rebuildSlotFromLoopContent()`** (observable in native test)

---

## ExecutionBounds (5.3.4)

Extend row **14** (`processDeferredSaveState`) and row **15** (`LoadLoopJob`) with named LSV1 substeps and existing µs caps. **Do not** create a new bound class. LoopSnapshot body-format read work is **bounded per step** even when total slot file exceeds one slice.

---

## Explicit non-goals (5.3)

- GUS persistence / undo lineage
- `CONTAINER_VERSION` / `REVPK02` change
- Eager migration subsystem
- 5.4 verification matrix (smoke only in 5.3.4)
- Dropping `PersistedLoopSnapshot` dual-write (§6)
- New persistence owner or scheduler
- Runtime snapshot architecture beyond load publication handoff

---

## Pre-implementation review

### Ready

- Stage order, authority rule, legacy path, save-time migration, logical serialization, no second persistence owner.

### Resolved (Architect 2026-09-11)

| Topic | Decision |
|---|---|
| Vocabulary | **LoopSnapshot body format** (domain); **slot file** (artifact); **LSV1** (format identifier); **StorageLoopIo** (owner) |
| LoopSnapshot body-format primitives | LE fixed-width table above |
| Section rules | Fail-closed table above |
| Read bounds | Format-validation caps above |
| Publication | LoopSnapshot body format read path non-publishing; Stage 3 atomic handoff only |
| Deserialization helper | Deserialization-only; not a new authority |
| Dual-write | `PersistedLoopSnapshot` bytes unchanged |
| Sizing | Shared layout module with write |
| Oracle | Diagnostic only; never merge/repair |
| Commits | 5.3.1 → 5.3.4 |

### Proceed?

**YES — Stage 5.3.1 coding may start.**
