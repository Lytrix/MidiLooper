## ADDED Requirements

### Requirement: Capture and published chunk-id types are distinct

Mutable capture-builder chunk-id storage and immutable published-pass chunk-id storage SHALL use
**different typedefs** so the compiler rejects cross-domain use without an explicit cold-path
transfer.

At minimum:

- **`CaptureChunkIdList`** — `InternalHeapFirstAllocator<uint16_t>`; used only by
  `LoopEventStore` (CaptureBuilder / `Capture.store`).
- **`PublishedChunkIdList`** — `ExternalMemoryFirstAllocator<uint16_t>`; used by sealed
  `RecordPass`, `OverdubPass`, `PendingCapturePass`, and SD snapshot rows for capture passes.

The monolithic shared `ChunkIdList` typedef SHALL be removed or deprecated once migration completes.

#### Scenario: Capture append uses capture types only

- **WHEN** `Loop::appendCaptureEvent` runs during RECORDING or OVERDUBBING
- **THEN** the firmware SHALL mutate only `Capture.store` backed by `CaptureChunkIdList`
- **AND** SHALL perform zero allocator-domain transitions per appended event

#### Scenario: Compiler rejects published list on capture store

- **WHEN** firmware code attempts to pass `PublishedChunkIdList` to `LoopEventStore::append` or
  `detachChunksTo(CaptureChunkIdList&)` without an approved transfer helper
- **THEN** the build SHALL fail at compile time

---

### Requirement: Published pass creation performs one domain transition at seal

Creating a published capture pass from a sealed capture buffer SHALL perform **at most one**
internal-heap → external-memory-pool transition for chunk-id metadata, on the **stop path at
seal**.

Publish into `LoopPasses` SHALL move `PublishedChunkIdList` within the published domain (no second
allocator-domain transition).

#### Scenario: Seal transfers chunk ids to published pending row

- **WHEN** `sealCapture` succeeds after record or overdub stop
- **THEN** chunk ids SHALL move from `Capture.store` into `PendingCapturePass.publishedChunkIds`
  as `PublishedChunkIdList` via a single approved transfer helper
- **AND** `Capture.store` SHALL be empty of chunk ids after transfer
- **AND** sealed chunks in the PSRAM pool SHALL retain correct reference counts

#### Scenario: Publish moves within published domain

- **WHEN** `commitPendingCapturePass` commits a pending row to `RecordPass` or `OverdubPass`
- **THEN** `publishedChunkIds` SHALL move into the pass row without re-allocating or copying
  chunk ids across allocator domains
- **AND** `hasPendingCapturePass()` SHALL clear and `Capture.store` SHALL reset for the next session

#### Scenario: Transfer helper bounded work

- **WHEN** the approved transfer converts `CaptureChunkIdList` to `PublishedChunkIdList`
- **THEN** the operation SHALL perform at most one allocation, one linear copy of chunk ids, and
  no intermediate temporary vectors

#### Scenario: Transfer failure on seal is graceful

- **WHEN** external memory pool allocation fails during seal transfer
- **THEN** seal SHALL fail without calling `abort()`
- **AND** capture stop semantics SHALL surface failure consistent with existing `SealOutcome` /
  `CommitResult` paths

---

### Requirement: PendingCapturePass is recoverable commit staging not a capture builder

`PendingCapturePass` SHALL hold **published-representation** chunk ids after seal. It SHALL NOT
accept MIDI append and SHALL NOT use `CaptureChunkIdList`.

#### Scenario: Append blocked while pending

- **WHEN** `hasPendingCapturePass()` is true
- **THEN** `appendCaptureEvent` SHALL NOT append to pending chunk ids
- **AND** existing guard behavior SHALL remain

#### Scenario: Discard releases published refs without publishing

- **WHEN** `discardPendingCapturePass` runs (edit cancel, load adopt, publish failure cleanup)
- **THEN** published chunk refs on the pending row SHALL be released
- **AND** no row SHALL be added to `LoopPasses`

---

### Requirement: Published chunk ids do not repatriate into capture types

Published chunk-id lists SHALL NOT convert back into `CaptureChunkIdList` except when
**intentionally starting a new empty capture session** (empty builder).

Undo disable, pass reclaim, SD load adopt, and pass snapshot clone paths SHALL stay in the
published domain or release refs without repatriation.

#### Scenario: Undo disable stays published

- **WHEN** undo applies `setCapturePassState(passId, Disabled)` on a capture pass
- **THEN** pass rows SHALL retain `PublishedChunkIdList` until reclaim
- **AND** firmware SHALL NOT move those ids into `Capture.store`

#### Scenario: SD load constructs published types directly

- **WHEN** `readPersistedLoopSnapshot` or equivalent loads capture pass chunk refs from SD
- **THEN** loaded refs SHALL populate `PublishedChunkIdList` on pass rows directly
- **AND** SHALL NOT round-trip through `CaptureChunkIdList` unless starting a new capture session

---

### Requirement: Published overdub pass vector uses external memory pool

`LoopPasses::overdubPasses` SHALL use an external-memory-first vector typedef
(`CommittedOverdubPassVec`) so overdub pass row growth does not consume internal heap.

`EditPassVec` and edit-metadata vectors are **out of scope** for this requirement.

#### Scenario: Overdub publish allocates pass row off internal heap

- **WHEN** a new overdub pass is published after stop
- **THEN** the `OverdubPass` row and its `PublishedChunkIdList` SHALL prefer external memory pool
  allocation
- **AND** capture append path SHALL remain unchanged

---

### Requirement: Primary success criterion is ownership not RAM recovery

This migration SHALL be evaluated primarily on **ownership separation**, not peak internal-heap
recovery alone.

#### Scenario: Ownership gate passes with modest RAM delta

- **WHEN** Phase 1–4 verification completes on the 215312-profile workload
- **THEN** capture append SHALL use capture types only (tests + HITL)
- **AND** published metadata SHALL no longer contribute to long-lived internal-heap ownership
- **AND** a modest internal-heap improvement (for example a few KiB) SHALL be sufficient secondary
  success if ownership invariants hold

#### Scenario: Exit criterion limits further published-metadata work

- **WHEN** measurement shows less than approximately 5 KiB internal-heap recovery under the
  215312 workload and no measurable long-session pressure improvement
- **THEN** no further published-metadata migration work SHALL be pursued on this track
- **AND** the Phase 1 ownership split MAY still ship; later phases MAY be scoped down
