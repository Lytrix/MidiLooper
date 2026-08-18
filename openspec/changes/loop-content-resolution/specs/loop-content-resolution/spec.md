## ADDED Requirements

### Requirement: LoopContentResolution owns effective-state queries

The system SHALL provide `LoopContentResolution` as the owner of query-time effective musical state from the **active** pass set plus edit history. `LoopPasses` SHALL remain the content authority. `NoteGeometryResolver` SHALL remain the owner of live NOTE_EDIT overlap Resolution.

Primary APIs SHALL be `resolveState` and `resolveWindow`. `resolveNotes` SHALL be a derived consumer of a resolved window, not the playback primitive.

#### Scenario: Primary queries exist without Notes as authority

- **WHEN** a native fixture requests effective MIDI for a tick range
- **THEN** `resolveWindow` returns `ResolvedEvent` values
- **AND** `resolveState` returns sounding MIDI state at the requested tick
- **AND** `resolveNotes` MAY be derived from that result and MUST NOT be required to produce `resolveWindow`

### Requirement: Resolution uses active history only

Committed pass **content** SHALL be immutable. Active/Disabled SHALL be mutable history state. Resolution MUST use the active pass set plus active edit history, not every stored pass.

#### Scenario: Disabled suffix is not resolved

- **WHEN** a later overdub pass is Disabled
- **THEN** `resolveWindow` for the loop matches materialize of the remaining Active prefix
- **AND** the Disabled pass content is unchanged

### Requirement: Deterministic resolution independent of cache and chunks

For a fixed active pass set and fixed edit history, resolution SHALL be deterministic and independent of cache state, PSRAM chunk boundaries, or previous resolution order.

#### Scenario: Cold and warm cache agree

- **WHEN** `resolveWindow` runs on an empty cache and again after a prior overlapping window populated a cache
- **THEN** the `ResolvedEvent` sequences are identical

### Requirement: Candidate find does not walk every pass

After indexing, finding candidates for a window MUST NOT iterate every historical pass list. Cost SHALL be proportional to candidate events (and index lookup), not pass count.

Walking `for each pass if intersects(window)` SHALL fail this requirement even when the window is small.

#### Scenario: Commit of pass N does not traverse P0 through P(N-1)

- **WHEN** pass N is committed on the canonical stress fixture (45+ passes)
- **THEN** candidate find for the affected region uses indexed references
- **AND** the operation does not scan every prior pass’s chunk list except via those indexed affected regions

### Requirement: resolveState has bounded replay distance

`resolveState(tick)` MUST have a bounded historical replay distance through checkpoints spaced by `checkpointIntervalTicks`. It MUST NOT require replaying the loop from tick 0.

#### Scenario: High-tick loop switch

- **WHEN** `resolveState` is requested at a tick far from 0 on a 64-bar or longer fixture
- **THEN** replay starts from a checkpoint at most `checkpointIntervalTicks` before the target
- **AND** the call does not scan events from tick 0

#### Scenario: Warm destination loop switch does not rebuild

- **WHEN** destination-loop checkpoints are already built
- **AND** `resolveState` is requested at the live playhead after leaving another loop
- **THEN** replay starts from a checkpoint at most `checkpointIntervalTicks` before the target
- **AND** destination checkpoint snapshots are unchanged
- **AND** the call does not scan events from tick 0

### Requirement: Checkpoint is a jump point, not a copy of the resolved loop

A checkpoint MUST reduce historical replay work without becoming a proportional copy of the resolved loop. `checkpointIntervalTicks` SHALL be a performance parameter, not a semantic property of the loop. Changing density (native 1 bar, device 8 bars, device 16 bars, later adaptive) MUST NOT change `resolveState` answers for a fixed active history.

`spans` plus a tick-ordered span-boundary index (start **and** exclusive-end) are currently sufficient as the base index for `resolveState`. The index container MUST NOT be required to be a PSRAM associative map. Equal-tick boundaries MUST apply in the same order as C-order insertion (start then end per span, spans in `rebuildNotes` order) after a stable tick sort. Span channel lookup is `NoteId` → first NOTE_ON channel in resolved C-order and MUST use a contiguous `{noteId, channel}` list (append, `stable_sort` by `noteId`, first-wins unique), not a PSRAM associative map. The device probe SHALL measure whether additional indexing is required. The probe MUST NOT assume a per-bar full `soundingAt` snapshot is the checkpoint.

Building a sounding-state snapshot at every bar of an `035414`-class loop (notes × bars membership copies) SHALL fail this requirement.

#### Scenario: Flat span-boundary index matches the map

- **WHEN** `resolveState` runs from sparse checkpoints using a flat tick-ordered span-boundary list built by appending start then exclusive-end per span and `stable_sort` by tick
- **THEN** the sounding-state results match the materialize oracle
- **AND** `passChunkListsWalked` is 0
- **AND** a span that ends at tick T and another that starts at T produce the same sounding set as the map

#### Scenario: Sparse checkpoints agree with dense checkpoints

- **WHEN** `resolveState` runs at the same high tick with `checkpointIntervalTicks` equal to one bar and again equal to eight or sixteen bars
- **THEN** the sounding-state results are identical
- **AND** the sparse run stores fewer `soundingAt` snapshots than the one-bar run

#### Scenario: Per-bar sounding copies are not the device checkpoint

- **WHEN** the idle device gate runs on a 64-bar or longer loop with thousands of notes
- **THEN** it MUST NOT allocate a full sounding vector at every bar
- **AND** it MUST NOT consult `MemoryMonitor` / advisory pressure to arm, slice, or abort
- **AND** derived indexes (`spanBoundaries`, `tickEvents`, `channelByNoteId`) MUST use `ExternalMemoryFirstAllocator` bulk arrays, not per-entry PSRAM associative insert
- **AND** `prepareRebuildSpans` materialize-plus-reconstruct MUST NOT run as a single idle slice
- **AND** IndexCommit, `pairCapturePassNotes`, reconstruct span build, display project, channel-index append, and RebuildSpans MUST process at most `kDeviceGateEventsPerSlice` events or spans per idle slice
- **AND** those slices MUST NOT commit a whole long-loop pass, pair every event of a long pass, emplace every span in one idle call, or insert every channel-lookup entry into a PSRAM associative container
- **AND** the idle gate MUST emit `#CAP,DIAG,lcr,phase,...` on step change and at most once per `kDeviceGatePhaseLogIntervalUs` while Continue

### Requirement: Derived-index storage is bulk, not per-entry PSRAM associative insert

Derived indexes used by `LoopContentResolution` on the target device MUST use contiguous/bulk storage. Per-entry dynamic allocation into PSRAM associative containers (`std::map`, `std::multimap`, `std::unordered_map`) is prohibited on realtime-adjacent index construction paths.

Where the query contract permits, indexes SHALL be represented as flat PSRAM arrays built by append or bulk construction and ordered or uniqued in a bounded operation. The representation MUST be selected from the query contract. Flat storage is not an automatic replacement for every associative structure. A bucket or offset table MUST NOT be added unless a measured flat query is too expensive.

This requirement covers **derived indexes + PSRAM + per-entry construction**. It does not forbid maps on unrelated paths. `pair` / `TickIndex::byNoteId` / `openOnByPitch` are 5.18 — representation follows the query contract.

#### Scenario: Span-boundary, tick-event, and channel indexes are flat arrays

- **WHEN** the idle device gate builds `spanBoundaries`, `tickEvents`, and `channelByNoteId` on a loop with thousands of notes
- **THEN** those indexes are contiguous PSRAM arrays filled by sliced append and a bounded sort or unique
- **AND** construction does not `emplace` per note or per event into a PSRAM `std::map`, `std::multimap`, or `std::unordered_map`

#### Scenario: Channel lookup first-wins on NoteId

- **WHEN** `appendSpansFromNotes` assigns `span.note.channel`
- **THEN** the channel is the first NOTE_ON in resolved C-order for that `NoteId`
- **AND** a later NOTE_ON with the same `NoteId` is ignored even if its channel differs

### Requirement: Physical chunks are not resolution boundaries

`LoopEventStore` chunks SHALL remain a storage packing detail. Resolution MUST operate on ticks, identities, and events. A note, edit, or checkpoint MAY span chunk boundaries.

#### Scenario: Note spanning two chunks

- **WHEN** a NOTE_ON and matching NOTE_OFF reside in different chunks
- **THEN** `resolveWindow` that covers both ticks still yields the same effective note as a single-chunk fixture with the same ticks

### Requirement: Canonical fixture and three gates before production

A canonical native fixture SHALL exist before Stage 1 is treated as proven: 64 or 128 bars, 45+ passes, multiple channels, same-pitch overlaps, shorten, extend, delete, move, wrap-around. Every stage SHALL report events in history, passes in history, events in query window, candidate events, resolution operations, and elapsed µs.

Production MIDI, display, overdub entry, and NOTE_EDIT MUST stay on `materializeToEventVector` / Layer D 3b until all three gates pass: correctness vs materialize+reconstruct; complexity (this spec’s candidate-find requirement); device worst-case latency on the `035414` class (no multi-second MIDI or OLED stall, no `VCACHE,full` on the normal path, no full materialization after commit).

#### Scenario: Prototype does not replace production materialize yet

- **WHEN** native stages 0–8 run
- **THEN** firmware record/overdub/playback/display call sites still use the existing materialize and 3b visual-cache overdub copy
- **AND** `handleMidiInput` does not call `LoopContentResolution`

#### Scenario: Correctness vs materialize

- **WHEN** the same fixture is resolved and materialized+reconstructed for a window
- **THEN** effective MIDI (or documented semantic equivalence) matches
