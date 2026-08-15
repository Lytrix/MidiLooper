# Decision log

Persistent record of **accepted architectural and implementation decisions**. Not a changelog, roadmap, or meeting notes.

**Agents:** run [DECISION_REVIEW.md](Templates/DECISION_REVIEW.md) before implementation; append new decisions at [session closeout](Templates/SESSION_CLOSEOUT.md).

| Rule | Meaning |
|------|---------|
| Append only | Never delete; supersede with a new entry (`Status: Superseded`) |
| Search before design | `rg` this file + active OpenSpec + relevant `docs/Plans/` |
| Challenge via reassessment | Redesign is allowed through [ARCHITECTURE_REASSESSMENT.md](ARCHITECTURE_REASSESSMENT.md) + new log entry |

## Index

| ID | Date | Topic | Status |
|----|------|-------|--------|
| [DEC-037](#dec-037-loop-content-resolution-parallel-prototype) | 2026-08-14 | LoopContentResolution parallel prototype; materialize stays until three gates | Accepted |
| [DEC-036](#dec-036-runtime-effective-event-source-for-overdub) | 2026-08-14 | Runtime effective event source; overdub entry without display reconstruction | Accepted |
| [DEC-035](#dec-035-loop-persists-content-only) | 2026-08-14 | Loop persists content only; undo/redo is derived and not persisted | Accepted |
| [DEC-034](#dec-034-overlap-shorten-seals-at-the-user-triggered-commit) | 2026-08-13 | Overlap shorten seals at the user-triggered commit; closure defers leave-restore only | Accepted |
| [DEC-033](#dec-033-overdub-overlap-ignores-per-note-channel) | 2026-08-12 | Overdub overlap uses loop-scoped notes; no per-note channel filter | Accepted |
| [DEC-032](#dec-032-overdub-editpass-unification-reassessment) | 2026-08-12 | G2: unify resolution; dual storage transitional | Accepted |
| [DEC-031](#dec-031-overdub-overlap-encode-pending-buffer-to-editpass) | 2026-08-12 | Transitional dual-seal encode + undo/restore pins | Accepted (encoding) |
| [DEC-030](#dec-030-sticky-overlap-end-of-participation-on-current-state) | 2026-08-08 | Sticky overlap end-of-participation on NoteEditCurrentState | Accepted |
| [DEC-029](#dec-029-noteeditcurrentstate-owns-note-edit-editable-note-state) | 2026-08-07 | NoteEditCurrentState owns NOTE_EDIT editable note state | Accepted |
| [DEC-028](#dec-028-editsessionaction-geometry-pipeline-phase-1-native) | 2026-08-04 | EditSessionAction geometry pipeline Phase 1 native | Accepted |
| [DEC-026](#dec-026-commit-centered-lazy-slot-load) | 2026-07-18 | Commit-centered lazy slot load (audible boot + on-demand hydrate) | Accepted |
| [DEC-025](#dec-025-split-focus-playing-preview-pending) | 2026-07-09 | Split focus: playing / preview / pending; committed-transition invariant | Accepted |
| [DEC-024](#dec-024-loop-owned-undo-ownership-direction) | 2026-07-08 | Loop-owned undo ownership direction (Phase 1 filter, Phase 2 migrate stack) | Accepted |
| [DEC-022](#dec-022-runtime-bundle-save-tail-integrity) | 2026-07-07 | Runtime bundle save tail integrity (meta temp truncate + append cursor) | Accepted |
| [DEC-021](#dec-021-defer-inactive-loop-slot-restore-at-boot) | 2026-07-07 | Defer inactive loop slot restore + undo bodies at current set restore | Accepted; amended 2026-07-09 |
| [DEC-020](#dec-020-continuous-runtime-persistence-architecture) | 2026-07-07 | Continuous runtime persistence — invariant-driven capture-chunk persistence | Accepted |
| [DEC-019](#dec-019-sd-load-path-extmem-routing-m5-spike) | 2026-07-07 | SD load path extmem routing (M5 spike) | Accepted (spike) |
| [DEC-018](#dec-018-admission-current-heap-derived-rep-consolidation) | 2026-07-07 | Admission uses current heap; derived-rep OpenSpec consolidation | Accepted |
| [DEC-017](#dec-017-skip-long-hitl-gates-implement-runtime-redesign) | 2026-07-07 | Skip long HITL/capture gates; implement runtime redesign | Accepted |
| [DEC-016](#dec-016-runtime-architecture-four-layer-model) | 2026-07-07 | Runtime architecture four-layer model | Accepted |
| [DEC-015](#dec-015-interval-projection-stage-1-stage-2-split) | 2026-07-05 | IntervalProjection module + Stage 1/2 split | Accepted |
| [DEC-014](#dec-014-dual-normalization-boundaries-micro-vs-macro) | 2026-07-03 | Dual normalize micro/macro | Accepted |
| [DEC-013](#dec-013-linear-loop-tick-validate-vs-normalize) | 2026-07-03 | Linear loop tick validate vs normalize | Accepted |
| [DEC-012](#dec-012-storagesession-persistence-state-model) | 2026-06-29 | StorageSession persistence state | Accepted |
| [DEC-011](#dec-011-bias-toward-progress) | 2026-06-29 | Bias toward progress | Accepted |
| [DEC-010](#dec-010-ownership-evolution-protocol) | 2026-06-29 | Ownership evolution protocol | Accepted |
| [DEC-009](#dec-009-runtime-state-vs-roadmap-split) | 2026-06-29 | Runtime state vs roadmap | Accepted |
| [DEC-008](#dec-008-authority-conflict-resolution) | 2026-06-29 | Authority conflict resolution | Accepted |
| [DEC-007](#dec-007-historical-decision-reuse-enforcement) | 2026-06-29 | Historical decision reuse | Accepted |
| [DEC-006](#dec-006-agent-context-harness-vs-chat-history) | 2026-06-29 | Agent context / chat history | Accepted |
| [DEC-005](#dec-005-gpio-base-module-vs-droid-only-actions) | 2026-06-29 | GPIO base vs DROID actions | Accepted |
| [DEC-004](#dec-004-recordoverdub-stop-validation) | 2026-06-29 | Record/overdub stop validation | Accepted |
| [DEC-003](#dec-003-d13-jam-recording-ordering) | 2026-06-29 | D13 jam recording order | Accepted |
| [DEC-002](#dec-002-set-revision-vs-flat-savedset-snapshot) | 2026-06-29 | Set revision vs flat SavedSet | Accepted |
| [DEC-001](#dec-001-loadsave-overlay-confirm-control) | 2026-06-29 | Load/save overlay confirm | Accepted |

---

<!-- Append new entries below (newest first). Next ID: DEC-038 -->

## DEC-037 — LoopContentResolution parallel prototype

**Date:** 2026-08-14  
**Status:** Accepted  
**Owner:** `LoopContentResolution` (new) — effective state/window queries; `LoopPasses` remains content authority; `StorageManager` remains persist owner  
**Plan:** [`loop_event_sourced_resolution_architecture.md`](Plans/loop_event_sourced_resolution_architecture.md)  
**OpenSpec:** `openspec/changes/loop-content-resolution/`  
**Parent:** [DEC-036](#dec-036-runtime-effective-event-source-for-overdub) (overdub entry without display reconstruction still holds; D1 eager flatten stays withdrawn)  
**Does not supersede:** DEC-016 four-layer model; DEC-035 content-only persist; DEC-036 overdub-entry contract; DEC-031/032 overlap semantics

### Problem

Layer D 3b made overdub **entry** cheap when `visualCache` is clean ([`045556`](../captures/session_20260814_045556.log) `begin_capture` 2214 µs). After a new overdub **commit**, `markDisplayCachesStale` still dirties every bar and idle/window gather still walks every active pass list (`CommittedEventRange::inWindow` + `collectActiveCommittedChunkLists`). Short loops still full-materialize on stop (`VCACHE,full`). D1 tried an eager full `passesMaterializedStore_` and `invalidateCaches` discarded it.

A, B, C, and D (range-dirty cache, incremental bake, tick index, checkpoint+tail) each fit **part** of that remaining cost. Shipping them as separate caches repeats D1.

### Decision

1. Introduce **`LoopContentResolution`** as the derivation owner for effective musical state. Primary APIs: `resolveState()`, `resolveWindow()`. `resolveNotes()` is a derived consumer, not the architecture center.
2. **G is the union of A+B+C+D**, not a different physics. Do not ship A then B then C as independent derived owners.
3. Vocabulary: **`RawMidiEvent`**, **`EditAction`**, **`ResolvedEvent`**. Do not collapse them into `Event`. Do not name the owner `Resolver` or `LoopContentResolver` (`NoteGeometryResolver` already owns live NOTE_EDIT overlap Resolution).
4. **Committed pass content is immutable; Active/Disabled is mutable history state.** Resolution uses the active pass set plus edit history.
5. After indexing/checkpointing, cost is proportional to **candidate events and affected state**, not historical pass count. Finding candidates must not walk every pass list.
6. **`resolveState(tick)` is a fundamental query.** Replay distance is bounded by checkpoints at `checkpointIntervalTicks`. Loop switch must not replay from tick 0.
7. For a fixed active pass set and edit history, resolution is **deterministic** and independent of cache state, chunk boundaries, or previous resolution order.
8. **Physical PSRAM chunks are not resolution boundaries.**
9. Build a **parallel native prototype**. Do not delete `materializeToEventVector`. Do not put resolution on `handleMidiInput`. Do not cascade `invalidateCaches` onto the prototype store.
10. Production consumers swap only after **three gates**: correctness vs materialize+reconstruct; complexity (`commit P(N)` does not traverse `P0…P(N-1)` except indexed affected regions); device worst-case latency on the `035414` class (no multi-second MIDI/OLED stall, no `VCACHE,full` on the normal path, no full materialization after commit).

### Rationale

DEC-016 already requires representation × interval. The missing owner is query-time resolution of active history, not another full flatten. D1 proved that an O(history) derived store plus global invalidation cannot survive. A parallel prototype with a hard scaling gate is safer than another cache on `passesMaterializedStore_`.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| A only (range-dirty `visualCache`) | Equal for display bars; idle slices still walk all pass lists |
| B only (true incremental bake) | Equal for derived MIDI current; D1 implementation was full flatten + invalidate, not a disproof of bake — but bake alone does not give loop-switch `resolveState` |
| C only (tick index on `CommittedEventRange`) | Equal for window find-cost; does not stop unrelated invalidation or boot-from-zero |
| D only (DEC-035 Layer C checkpoint) | Equal for load/switch; does not stop per-commit derived rebuild during overdub |
| F (stop at Layer D 3b) | Entry is already PASS; post-commit rebuild remains |
| E (CoW derived note versions) | Overlaps G’s range cache; content undo is already O(1) chunk-ref; do not build both |
| Keep optimizing `materializeToEventVector` | Explicit experimental boundary: prove whether resolution makes materialize unnecessary on the normal path |
| Name owner `Resolver` / `LoopContentResolver` | Collides with `NoteGeometryResolver`; NAMING prefers domain-owner nouns |

### Affected modules

Native prototype first: new `LoopContentResolution` headers/tests. Production later: `LoopMaterialization`, `LoopVisualCache`, `TrackPlaybackWindowBuild`, `establishOverdubSourceView` fallback only. `LoopPasses`, `StorageManager`, `NoteGeometryResolver` unchanged as owners.

### Amendment 2026-08-14 — Checkpoint is a jump point

Device per-bar `soundingAt` on the 68-bar / 1847-note loop is another O(history) derived store ([`225351`](../captures/session_20260814_225351.log) heap Critical). Short-loop sliced `lcr` remains PASS ([`225744`](../captures/session_20260814_225744.log)).

- A checkpoint MUST reduce historical replay work without becoming a proportional copy of the resolved loop.
- Checkpoint density is a performance parameter, not a semantic property of the loop (native 1 bar, device 8/16 bars, later adaptive — identical answers).
- `spans` plus a tick-ordered span-boundary index (start **and** exclusive-end) are sufficient for `resolveState` tail replay. The container need not be a PSRAM `std::multimap` (5.15).
- Split `prepareRebuildSpans` before treating RAM as the only stall. 5.7 probe is a selected loop **>63 bars** ([`115750`](../captures/session_20260815_115750.log) 139 bars accepted). Arm cap stays off. [`143009`](../captures/session_20260815_143009.log) complete `hist=2394` `walk=0`; project PASS; `startsByTick` emplace still fails 5.7. Do not change batch size to chase `emplace`. Do not start 5.1/5.2 or Stage 6 until cold-build latency passes.

### Amendment 2026-08-15 — 5.15b pick flat A

Native A (`append` + `stable_sort` by tick only) matches C `resolveState` and the materialize oracle, including equal-tick end-then-start. `walk=0`. Host: A index 5 µs vs C emplace 29 µs; A resolve not worse. 5.15c swapped the device gate to `spanBoundaries`. Device [`151450`](../captures/session_20260815_151450.log): `app=5490706` `sort=10202` `reb=13851136` `st=13045` `walk=0` (was `reb=104359946`). **5.15 complete.** Do not build B or A2. Do not reopen the resolver query model. 5.16c slices `RebuildPrepare` materialize at `kDeviceGateEventsPerSlice`. Device [`153920`](../captures/session_20260815_153920.log) `prep` PASS — no `loop_rem`; `idle_maint` 22–25 ms.

### Amendment 2026-08-15 — 5.17a–c pick flat A for TickIndex

`TickIndex::byTick` query contract is `tick ∈ [begin, end)` → Active `(passId, eventIndex)`. Native A (`TickEventEntry[]`, C-order append + `stable_sort` by tick only) matches C walk, wrap, disabled-pass skip, and `resolveWindow` vs the materialize oracle. Equal-tick insertion order preserved (NOTE_OFF then NOTE_ON at 192). `walk=0`. Host (94 entries): C emplace 23 µs, A 26 µs, A query 144 vs C 151. Pick **A**. Device [`161355`](../captures/session_20260815_161355.log): `iapp=4819607` `isort=27415` `win=13971` `st=3108` `walk=0` vs C [`155953`](../captures/session_20260815_155953.log) `idx` 50→120.6 ms. **5.17d PASS.** Do not build B or A2. Do not fold `recon` or `pair`. `TickIndex` and `StateCheckpoints` stay separate owners. 5.17e dropped `byTick`: `indexCapturePassEventRange` appends `tickEvents`; `findRawWindow` reads that list only.

### Amendment 2026-08-15 — 5.7a sliced append reserve

[`162630`](../captures/session_20260815_162630.log): `spans` notes/s 565→125 and `DFRAME` 1.003→1.591 s while paint stayed 12 ms. `appendSpanBoundaryEntries` reserved this slice only (`size+16`); `spans` already reserved `notes.size()`. Same growth on `idx` `tickEvents` (`size+8`). 5.7 leftover is that realloc, not `channelForNoteId` (constant per 8-note slice). `appendSpansFromNotes` reserves `2 * notes.size()`; `appendTickEventEntries` reserves remaining events in the pass. Device [`163942`](../captures/session_20260815_163942.log): reserve **PASS** (`app=2349` `iapp=202981`; `idx` 1310→1212 events/s). 5.7 `DFRAME` not closed: `spans` 930→169 notes/s. 5.7b: `appendSpansFromNotes` fills `channelByNoteId` from every NOTE_ON in `resolved` (first wins). Open notes whose OFF is outside the current 8 events still pair (`activeNoteStacks` / `openOnByPitch`) and still get channel. Native `test_stage57_recon_*` / `test_stage57_pair_*` / `test_stage57_span_channel_*` PASS. Do not rewrite `pair` / `recon`. Do not start 5.1.

### Amendment 2026-08-15 — 5.7b device FAIL

Device [`164922`](../captures/session_20260815_164922.log): first `spans` slice `loop_rem,idle_maint,14744010` filling `channelByNoteId` (PSRAM `unordered_map` `emplace`). `DFRAME` 15.702 s with consecutive `frameIndex`. After fill, `spans` ~1000 notes/s flat. Open-note contract held. Do not keep this map on device.

### Amendment 2026-08-15 — 5.7c flat channel lookup

Lookup contract from code: `NoteId` → first NOTE_ON channel in resolved C-order. Channel is the value, not a key or partition. First-wins unique on `NoteId` only. `channelByNoteId` is now `{noteId, channel}[]`: C-order append, `stable_sort` by `noteId`, unique keep-first, `lower_bound`. Device sequences `chan` / `csort` before `spans`. Complete line adds `capp=` / `csort=`. Device [`170024`](../captures/session_20260815_170024.log) **PASS**: `capp=12373` `csort=1779`; `dedup`→`chan` 11.55 ms; `csort`→`spans` 22.63 ms; no `idle_maint` `loop_rem`; `chan`/`spans` `DFRAME` 0.933–0.965 s. Pair `DFRAME` 1.277 s remains (different owner). `walk=0`. Do not rewrite `pair` / `recon`. Do not start 5.1.

### Amendment 2026-08-15 — Derived-index storage invariant

Not a new DEC. Device evidence from three LoopContentResolution derived indexes:

| Structure | Associative PSRAM | Flat A | Device |
|-----------|-------------------|--------|--------|
| `startsByTick` | `multimap` 224–413 ms / 8 inserts | `spanBoundaries` append+sort | 5.15 [`151450`](../captures/session_20260815_151450.log); after reserve [`170024`](../captures/session_20260815_170024.log) `app=1420` `sort=9439` |
| `TickIndex::byTick` | `multimap` 50–121 ms / small batches | `tickEvents` (5.17e **removed** `byTick`) | [`170024`](../captures/session_20260815_170024.log) `iapp=201044` is **bulk append total**, not map insert (was `iapp=4.82 s` before 5.7a reserve) |
| `channelByNoteId` | `unordered_map` 14.7 s [`164922`](../captures/session_20260815_164922.log) | `{noteId, channel}[]` append+sort+unique | [`170024`](../captures/session_20260815_170024.log) `capp=12373` `csort=1779` |
| `TickIndex::byNoteId` | `unordered_map` 5.346 s [`172927`](../captures/session_20260815_172927.log) | `{noteId, loc}[]` append+sort+unique keep-last | [`173842`](../captures/session_20260815_173842.log) `bn=225` `nsort=10003` |

**Invariant:** Derived indexes used by LoopContentResolution must use contiguous/bulk storage on the target device. Per-entry dynamic allocation into PSRAM associative containers (`std::map`, `std::multimap`, `std::unordered_map`) is prohibited on realtime-adjacent index construction paths. Where the query contract permits, indexes are flat PSRAM arrays built by append/bulk construction and ordered or uniqued in a bounded operation. The representation is selected from the query contract; flat storage is not an automatic replacement for every associative structure. Representation B (bucket/offset table) is not justified unless a measured flat query is too expensive.

This is **derived indexes + PSRAM + per-entry construction**. It is not “never use maps anywhere.”

**Still associative (not this invariant’s swap list):** pairing `openOnByPitch` (LIFO, retained after 5.18); `TickIndex::passById` (pass-count, not note-count). Do not fold `recon` into this rule. Do not flatten a LIFO stack.

5.15–5.18 establish this invariant **empirically**, not as a style preference:

| Contract | Representation |
|----------|----------------|
| ordered many-to-many range boundaries | flat + sort (`spanBoundaries`) |
| ordered event window | flat/bulk (`tickEvents`) |
| unique first/last-wins lookup | flat + sort/selection (`channelByNoteId` first-wins; `byNoteId` last-wins) |
| LIFO pairing state | associative/container semantics retained (`openOnByPitch`) |

The last row stops the rule from becoming “replace all maps with arrays.”

### Amendment 2026-08-15 — 5.7c FROZEN; pair is 5.18

Representation part of Stage 5.7 is **closed**. Do not reopen 5.7c.

| Gate | Status |
|------|--------|
| correctness (`walk=0`) | PASS [`170024`](../captures/session_20260815_170024.log) |
| complexity (no B) | PASS |
| derived-index RAM / construction (flat/bulk) | PASS — `spanBoundaries`, `tickEvents`, channel lookup, `byNoteId` |
| device latency on the whole idle gate | **5.1 PASS** [`173842`](../captures/session_20260815_173842.log) |

Successor: [`loop_content_resolution_pair_index_refinement.md`](Plans/loop_content_resolution_pair_index_refinement.md). Flatten from the **query**, not the container type. `byNoteId` is last-wins `NoteId → {passId, on, off}`. `openOnByPitch` is a per-pass LIFO stack keyed by **pitch only**. Measure which produces the stall before picking a representation. No 5.1. No Stage 6. No B. Do not rewrite `recon`.

### Amendment 2026-08-15 — Loop-internal channel is not a resolution key

Not a new DEC. Restates [DEC-033](#dec-033-overdub-overlap-ignores-per-note-channel) for LoopContentResolution.

A loop’s notes are scoped to that loop. MIDI output channel is `Track::midiChannel` (`Track::sendMidiEvent` remaps recorded 1–16). `NoteUtils::DisplayNote` has no channel field. Pairing, overlap, and sounding identity use `NoteId` / pitch + tick, not `event.channel`.

`openOnByPitch` stays pitch-only. Do not add `(pitch, channel)`. The 5.7c `channelByNoteId` index copies a stored MIDI byte onto `SoundingNote.channel`; it is not a musical query. Do not reopen 5.7c to delete that copy in 5.18.

### Amendment 2026-08-15 — 5.18a pair instrument (no flatten)

`pair` reports `tot` / `bn` (`byNoteId`) / `op` (`openOnByPitch`) / `lk` (`find`) / `oth` plus entries, inserts, overwrites, pushes, pops, peak depth, openOn heap bytes. Device complete line `DIAG,lcr,pair`. 1 Hz `phase,pair` lines add `bn=` `op=` `lk=` `pk=`. Native last-wins and peak-depth tests. No header allocator probe (ITCM). Do not flatten until the 139-bar remasure names the expensive owner. No 5.1. No B. No `recon`.

### Amendment 2026-08-15 — 5.18a device: byNoteId owns pair

[`172927`](../captures/session_20260815_172927.log): `tot=5356927` `bn=5345535` `op=2433` `lk=1424` `oth=7535` `pk=1` `ins=2396` `ow=0`. Pair `DFRAME` 390→420 is **1.273 s** (consecutive `frameIndex`). `openOnByPitch` is not the stall. Next representation experiment is **last-wins flat `byNoteId` only**. Retain the LIFO stack. No 5.1. No B. No `recon`.

### Amendment 2026-08-15 — 5.18b last-wins flat byNoteId (native)

`TickIndex::byNoteId` query is `NoteId` → last `{passId, on, off}`. Storage is now `{noteId, loc}[]`: C-order NOTE_ON append, reverse-scan OFF during the unsorted walk, one `stable_sort` by `noteId`, unique **keep-last** (opposite of 5.7c channel keep-first). `appendNoteEvents` uses `lower_bound` after unique. Device sequences one `nsort` slice after all pair ranges, before `isort`. Sliced pair ranges do not unique every 8 events. Native last-wins vs keep-first tests. `openOnByPitch` stays a per-pass LIFO stack. No 5.1. No B. No `recon`. Do not flatten `openOnByPitch`.

### Amendment 2026-08-15 — 5.18b device PASS; 5.18 FROZEN

[`173842`](../captures/session_20260815_173842.log): `tot=7950` `bn=225` `nsort=10003` `op=2256` `pk=1` `ins=2396` `ow=0` `walk=0` `hist=2394`. Pair `DFRAME` consecutive `frameIndex` **0.980–1.026 s** (was 1.273 s [`172927`](../captures/session_20260815_172927.log)). `idle_maint` during pair 25.2 ms, no `loop_rem`. `nsort` 10.0 ms and `isort` 28.1 ms under 50 ms. Do not flatten `openOnByPitch`. Do not reopen 5.18.

### Amendment 2026-08-15 — 5.18 closed

The remaining Stage 5.7 device-latency violation was traced to `byNoteId` PSRAM associative construction. Replacing that construction with the minimum representation matching its last-assignment-wins query contract reduced `bn` from 5.346 s to 225 µs and total pairing from 5.357 s to 7.95 ms. `openOnByPitch` remains a LIFO stack because its mutation contract is not equivalent to a sorted/unique index. No new >50 ms construction slice was observed. Pair is frozen. Do not clean up `recon`, `byNoteId` naming, or `openOnByPitch` before the device realtime gate.

DEC-037 Stage 9 index work:

```text
correctness             PASS
complexity              PASS
derived-index RAM       PASS
flat/bulk construction  PASS
device latency
  spanBoundaries        PASS
  tickEvents            PASS
  channel lookup        PASS
  pair                  PASS
```

Next is the **whole-gate** 5.1 measurement, not another index. Production stays on materialize / 3b copy.

### Amendment 2026-08-15 — 5.1 idle-path device latency PASS

[`173842`](../captures/session_20260815_173842.log) complete LoopContentResolution idle gate on the 139-bar class (`hist=2394`, larger than `035414`). Production MIDI/display unchanged. The gate runs only from `processDeferredIdleMaintenance` when transport is not PLAYING / RECORDING / OVERDUBBING / STOPPED_RECORDING.

| Check | Worst in LCR window (20.67–52.43 s) |
|-------|--------------------------------------|
| OLED | consecutive `DFRAME` **1.034 s** (`frameIndex` +30, paint 9.9 ms) vs healthy after-complete **0.968 s** |
| MIDI | `midi_gap` **39.1 ms**; `idle_maint` **34.9 ms**; no `loop_rem`; `clockrate` 0 (transport idle) |
| `VCACHE,full` during LCR | none |

Not multi-second MIDI or OLED stall. **5.1 PASS.** 5.2 (overdub entry / `VCACHE,full` on the PLAYING path) is not this measurement. No Stage 6.

### Amendment 2026-08-15 — Stage 6 overdub must not cold-build LCR

Not a new DEC. Stage 9 device gates are complete. **5.2 PASS** [`180624`](../captures/session_20260815_180624.log) `begin_capture` **10050 µs** after restoring DEC-036 3b (copy authoritative `visualCache.notes`; no `markDisplayCachesStale` on `startOverdubbing`). Prior FAIL [`175544`](../captures/session_20260815_175544.log) **108979 µs**. Original 3b [`045556`](../captures/session_20260814_045556.log) **2214 µs**.

**Invariant:** Overdub **start and stop** MUST NOT cold-build `LoopContentResolution`. LCR construction belongs to idle/background preparation. Overdub entry consumes already-prepared derived state (3b visual-cache copy when authoritative; already-complete LCR only if that work already finished off the overdub path).

Content authority stays `LoopPasses`. Do not add a `LoopContent` type. Do not treat the 2214 µs 3b number as proof that LCR queries are faster — that number is a cache copy, not an LCR setup/query cost.

**Forbidden on overdub start/stop:**

```
start/stop overdub
  → create/rebuild LCR indexes
  → resolve committed state
  → construct source view
```

That recreates [`175544`](../captures/session_20260815_175544.log) (`markDisplayCachesStale` → discard → materialize → reconstruct) under a new owner.

**Acceptance (Stage 6 overdub path):**

| Kind | Bar |
|------|-----|
| Architecture | no synchronous full-loop work; no materialize; no reconstruct; no `VCACHE,full`; no cache invalidation on entry |
| Performance | `begin_capture` **< 3 ms** target; **< 50 ms** hard gate |

A 4–10 ms landing still proves the invariant if those architecture bars hold. Do not optimize the old 2214 µs number at the expense of the invariant.

Dirty-cache is **not** a license to call `resolveWindow` from `startOverdubbing`. Idle slices (`6.2`) own LCR gather. If LCR is not ready, keep the existing non-LCR 3b fallback (`CommittedEventRange::inWindow` + edit apply) — do not cold-build indexes to help.

Firmware production swap (`6.1`+) does not start until an explicit implement request. This amendment pins the invariant only.

### Amendment 2026-08-15 — Stage 6 consume-only; 6A / 6B / 6C experiment

Not a new DEC. Strengthens the previous Stage 6 amendment. 5.18 is **FROZEN**; derived-index construction on device is tractable (flat/bulk/sliced). The remaining question is **not** whether the LCR representation is viable.

**Question:** Does migrating remaining production consumers to the already-proven LCR owner recover original ~2–3 ms overdub start/stop, or is there another independent stop-path cost? Do **not** answer by wiring LCR into overdub.

**Two dimensions:**

1. **Entry is largely explained.** 3b [`045556`](../captures/session_20260814_045556.log) `begin_capture` **2214 µs** (copy prepared `visualCache.notes`). Restored production [`180624`](../captures/session_20260815_180624.log) **10050 µs** still under the 50 ms gate. The 3b architecture shows **< 3 ms is achievable when the committed visual/source representation is already prepared**. LCR does **not** yet show that it can improve on that copy. Do not claim “switching everything to LCR makes start < 3 ms.”
2. **Stop is the interesting path:** `commitCapturePass` → `notifyCommittedContentChanged` → `markDisplayCachesStale` → dirty bars → idle rebuild. LCR restores responsiveness **indirectly**: idle prepares derived state; overdub start/stop consume it and return.

**Invariant (strengthened):** Overdub start/stop MUST NOT synchronously construct, sort, checkpoint, or resolve LCR state. It may only consume already-prepared derived state. `ensureLcrIndexCurrent()` (or any other ensure/rebuild helper) on that path is still a violation.

**Role:** `LoopContentResolution` is the **producer of prepared derived state**, not a replacement for `overdubSourceView`. `establishOverdubSourceView` stays the consumer. Keep the 3b `visualCache.notes` copy as fallback so LCR can fail to be ready without making overdub entry expensive.

**Preparation is established architecture** (cooperative / bulk / sliced). Device complete-path [`173842`](../captures/session_20260815_173842.log) / channel [`170024`](../captures/session_20260815_170024.log): `spanBoundaries` `app=1420` `sort=9439`; `tickEvents` `iapp=197743` `isort=28116`; channel `capp=12373` `csort=1779`; pair `tot=7950` `nsort=10003`. Do not rediscover this in the migration.

**Stage 6 experiment (no new DEC):**

| Slice | What | Oracle / fallback |
|-------|------|-------------------|
| **6A** | One dirty display range: `resolveWindow` → display projection instead of `CommittedEventRange` → `reconstructDisplayNotes`. Idle only. | Keep old path as oracle. Measure `resolveWindow`, projection, total slice, worst slice, `midi_gap`, `DFRAME`. |
| **6B** | Overdub stop: commit → mark **only affected ranges** → return. No materialize, no whole-loop reconstruct, no `VCACHE,full`. | Separate `stopOverdubbing` entry, `commitCapturePass`, bookkeeping, return-to-MIDI, first idle prep, display repaint, eventual consistency. |
| **6C** | Prepared LCR range → `overdubSourceView`. **Only after 6A/6B.** | Keep 3b visual-cache copy. Score `begin_capture` against 3b **2214 µs**, not against 5.2 **10050 µs**. |

`< 3 ms` is a **regression target**, not an architectural promise. LCR’s job is to eliminate post-commit / full-rebuild machinery, not to make an already-cheap transition intrinsically faster.

Firmware 6A+ waits for an explicit implement request.

### Amendment 2026-08-15 — 6D incremental post-commit LCR maintenance (investigation)

Not a new DEC. Prepared LCR is not late at the overdub button. `Track::processDeferredIdleMaintenance` runs LCR only when STOPPED; visual-cache slices run while PLAYING; `deviceGateComplete` is one-shot; `commitPendingCapturePass` bumps `playbackRevision` so `preparedWindowReady` is false. Evidence: [`loop_content_resolution_stage9_handoff.md`](Plans/loop_content_resolution_stage9_handoff.md) § Why LCR is not ready.

**Pick:** investigate incremental maintenance of the **overdub-query index** after commit (**6D**). First experiment **6D.1**: one `OverdubPass` → `capturePasses` + `tickEvents` + stamp. Not all of LCR. Plan: [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md).

**Rejected:**

- **A** — re-arm the STOPPED cold-build on stamp mismatch. Repeats 30–60 s preparation. Does not meet PLAYING overdub-over-overdub.
- **B** — slice the existing full-history LCR build during PLAYING. That is another continuously maintained O(history) cache on the perform path.

**6C** stays consume-when-ready (`tryResolvePreparedWindow` when the stamp already matches). It does not address always-ready.

**6D is not capability letter C** and is **not** “LCR is now always live.” It is DEC-037 capability **B** for the overdub query. `openOnByPitch` stays a pairing-time LIFO stack. Checkpoints, `byNoteId`, edits, and undo are later 6D slices, not 6D.1.

**Gate before firmware:** native 6D.1 must show overdub-query index maintenance scales with the new pass, not a full-history rebuild. `< 3 ms` is consume of already-prepared state ([`045556`](../captures/session_20260814_045556.log) 2214 µs copy), not a promise that switching to LCR is 2.2 ms.

**6.0 unchanged:** overdub start/stop must not construct, sort, checkpoint, or resolve LCR to open the source view. Bounded index update belongs at the commit site (with 6B) or a later admission slice — not on the button.

### Constraints created

- Overdub start/stop must not synchronously construct, sort, checkpoint, or resolve LCR state; they may only consume already-prepared derived state (`ensure*` rebuild helpers included).
- No second O(history) derived owner that `invalidateCaches` will discard.
- A checkpoint must not be a proportional copy of the resolved loop (per-bar full `soundingAt` fails this).
- Derived indexes must not per-entry-allocate into PSRAM associative containers on realtime-adjacent construction paths (5.15 / 5.17 / 5.7c). Representation follows the query contract; B only if a measured flat query is too expensive.
- `resolveNotes` must not become the playback primitive.
- Failure gate: if the prototype cannot show a materially better scaling model without another O(history) derived owner, stop and implement A+C on existing owners. A weak first tick index does not by itself disprove the architecture. Copying sounding state at every checkpoint does.
- Always-ready for the **next overdub query** is **6D** (6D.1: `tickEvents` + stamp after an `OverdubPass`). Not all of LCR. Not a re-armed STOPPED cold-build (A) and not a sliced full-history build during PLAYING (B). 6C is consume-when-ready only.

### Related OpenSpec

`openspec/changes/loop-content-resolution/` (new). DEC-036 change `loop-effective-event-source` remains for Layer D 3b closeout; D3/D4 stay out of that change.

### Migration notes

No SD format change. Materialize remains the production path until all three gates pass. Persisted checkpoints (DEC-035 Stage 6) reuse the in-RAM checkpoint **shape** after Stage 7; persist owner stays `StorageManager`.

---

## DEC-036 — Runtime effective event source for overdub

**Date:** 2026-08-14  
**Status:** Accepted  
**Owner:** `Loop` — incremental effective committed content; `establishOverdubSourceView` consumes range only  
**Plan:** [`loop_layer_d_overdub_rebuild_architecture.md`](Plans/loop_layer_d_overdub_rebuild_architecture.md)  
**OpenSpec:** `openspec/changes/loop-effective-event-source/` (D1 + D2)  
**Evidence:** [`session_20260814_035414.log`](../captures/session_20260814_035414.log) — `begin_capture` 6.779 s; `set_state` 7 µs  
**Parent:** [DEC-035](#dec-035-loop-persists-content-only) Layer D runtime track

**Context:** Layer A closed the `UndoStacks` bundle walk. Overdub-open latency on a 68-bar / 3385-event loop is dominated by `establishOverdubSourceView` (`gatherCommittedEvents` + `reconstructDisplayNotes`). The overdub FSM transition is cheap; source-view acquisition is not. Display reconstruction must not gate overdub entry.

**Decision:**

1. Maintain an **incrementally updated** runtime effective representation of committed loop content, valid **before** `beginOverdubSession()` — updated on pass commit, undo/redo toggle, edit apply, and load complete; never built on overdub button press.
2. **Overdub entry must not require display reconstruction.** Forbidden at overdub open: full-loop `gatherCommittedEvents()`, full-loop `reconstructDisplayNotes()`, full visual-cache rebuild as prerequisite.
3. Effective store exposes **range query** (`range(window)`) for overdub source establishment (D2). Dependency: layered passes → effective store → tick range → overdub source — not effective store → entire `DisplayNote` vector → overdub.
4. Display is **eventually consistent** during overdub; MIDI capture is **immediately** active. Idle `slice_clean` may lag; blocked MIDI may not.
5. **Out of scope:** persisted checkpoint + tail (D3 / Layer C); range-first load publication (D4 / Layer D); post-stop `PlaybackFullMaterialize` cleanup (separate slice).

**Consequences:** Evolve `passesMaterializedStore_` from lazy full rematerialize to eager incremental maintenance. `establishOverdubSourceView` rewritten for windowed effective range. Device gate: `ODUB,begin_capture` < 50 ms at `035414` scale.

**Validation:** Native equivalence vs `passes.materialize`; overdub-entry counter guards; RC-K3 / overlap fixtures unchanged; device `035414` class.

---

## DEC-035 — Loop persists content only

**Date:** 2026-08-14  
**Status:** Accepted  
**Owner:** `StorageManager` persist; `Loop` content records; `TrackUndo` in-session undo until a later DEC  
**Plan:** [`loop_layer_history_persistence_architecture.md`](Plans/loop_layer_history_persistence_architecture.md)  
**OpenSpec:** `openspec/changes/loop-content-history-persistence/` (Layer A)  
**GitHub:** Task [#33](https://github.com/Lytrix/MidiLooper/issues/33); Bug [#32](https://github.com/Lytrix/MidiLooper/issues/32)  
**Does not supersede:** [DEC-024](#dec-024-loop-owned-undo-ownership-direction) Phase 2 (move `GlobalUndoStack` Track → Loop). Stage 3b will be a new DEC that **replaces** that stack with derived Loop editing state.

**Context:** 64-bar overdub stop walks `DeferredSaveStage::UndoStacks` for ~1300 slices (`LoopUndoHistory` 4–16 s; `SlotMeta` the same walk). The loop file already stores the content those entries describe. A scoped `undo_TT_SS.bin` was withdrawn — it still serializes the duplicate payload.

**Decision:**

1. The Loop persists **content records only**. Undo/redo is runtime/editor behavior derived from ordered content plus grouping rules. Do not persist an undo stack, history cursor, history transition, or undo/redo record.
2. **Layer A (now):** prove content is sufficient (Stage 1); reconstruct load-time editing state while `GlobalUndoStack` remains in-session authority (Stage 2); then delete `UndoStacks` / `admitLoopUndoHistory` (Stage 3). Stage 3 is gated on Stage 2: reboot must reconstruct equivalent undo depth (`U:nn`) from content.
3. A persisted content prefix must be sufficient to define the effective Loop. If Stage 1 finds a gap, add immutable **content metadata** on the content record. Do not restore persisted `stateRaw` as hidden undo persist, and do not assume `active = records <= tip` until the audit survives grouping, companions, geometry, and `dropRedoBranch`.
4. Persisted content and runtime-resident content are different. Chunk-pool occupancy bounds how much history stays in RAM.
5. **Recorded, not authorized now:** publication uses `isRangeAvailable(PlaybackWindow)`, not `COMMITTED` (Layer D; conflicts with `lazy-slot-hydration` — reassessment before that firmware). Clear-as-unlink is Set last-state (`lastUnlinkedSlotLink`) (Layer B). Journal is append-structured with redo-tail reclamation (Layer B). Do not use **Source** as a domain noun.

**Consequences:** First persistence-level lever is deleting the `UndoStacks` stage. New floor is remaining `LoopPersist`, not cheap persist. In-session undo is unchanged until Stage 3b. DEC-024 Phase 1 filter stays until then.

**Validation:** Native Stage 1 audit + Stage 2 load-time editing-state fixtures. Stage 3 device: no `PERS,bundle` at UndoStacks scale; post-stop MIDI gap bounded by remaining `LoopPersist`; reboot `U:nn` matches pre-reboot tip depth.

---

## DEC-034 — Overlap shorten seals at the user-triggered commit

**Date:** 2026-08-13  
**Status:** Accepted  
**Plan:** [`note_edit_overlap_shorten_commit_seal_bugfix.md`](Plans/note_edit_overlap_shorten_commit_seal_bugfix.md)  
**Withdraws:** slice B of [`note_edit_resolver_authority_contracts_refinement.md`](Plans/note_edit_resolver_authority_contracts_refinement.md) § Stage 7.5

**Context:** `buildCommitOverlapRowsFromCurrentState` skipped an overlap participant while `participatingNoteVisibleOverlapTailInProgress` held — the mover's live span still overlapping the participant's `committedSpan`. At a deselect that is true by construction, since the mover is parked on the note it just shortened. `commitEditAction` then reloads the session store from committed passes, so a skipped row is lost, not postponed: the shorten either vanished (display reverted to the pre-shorten length) or landed on the next commit against an unrelated focus ([`204700`](../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2`, then @166.848 `Length 10 960–1247` while the mover was already note 14). Slice B was misattributed: [`225025`](../captures/session_20260807_225025.log) shows note 9 at 534 ticks, mover parked at 2832, commit `2544–2831` @26.291 and reselect `len=287` @36.927 — the correct truncation. "Stub was 47" was the mover's length.

**Decision:** An overlap shorten seals at the commit the user triggers. Active overlap closure defers **leave-restore** only, never persistence. The host note's tail after the mover stays discarded (truncate to `moverStart − 1`); no split, no restore-on-leave.

**Owner:** Unchanged — `buildCommitOverlapRowsFromCurrentState`. The predicate keeps its original home in `determineConstrainedGeometryTargetNoteIds` and `appendOverlapTargetActions`.

**Validation:** Native `test_commit_seals_overlap_length_while_mover_covers_committed_span_225025` and `test_deselect_commit_seals_overlap_shorten_under_parked_mover_204700` (canonical rows equal parity rows). `test_deselect_clears_overlap_participation_without_geometry_restore_232118` unchanged — `Ended` participation still blocks the row.

---

## DEC-033 — Overdub overlap ignores per-note channel

**Date:** 2026-08-12  
**Status:** Accepted  
**Plan:** [`realtime_incremental_work_overdub_note_change_bugfix.md`](Plans/realtime_incremental_work_overdub_note_change_bugfix.md)

**Context:** `Loop::accumulatePendingNoteChangesForIncomingNote` filtered overlap candidates with `noteIdHasChannel`, which scanned every `overdubSourceViewEvents_` row per same-pitch reconstructed note. S0e measured that scan as `notepair` 98 ms on a 4257-event / 2109-note loop ([`204221`](../captures/session_20260812_204221.log)). `NoteUtils::DisplayNote` has no channel field; the lookup existed only to recover channel from events.

**Decision:** A loop's committed notes are already scoped to that loop. Overdub G2 overlap resolution matches NOTE_EDIT: geometry uses pitch + tick window, not a per-note channel check. `noteIdHasChannel` is removed. A stored same-pitch note whose recorded channel differs from the incoming channel participates in overlap resolution.

**Previous owner:** File-local `noteIdHasChannel` in `LoopPendingNoteChange.cpp`.

**New owner:** None — filter deleted. Incoming `channel` still stamps the pending Add / Shorten / Hide row.

**Validation:** Native `test_pending_shorten_ignores_recorded_channel` — source note on channel 1, incoming on channel 2, same pitch and overlapping ticks, produces Shorten.

---

## DEC-032 — Overdub / EditPass unification reassessment

**Date:** 2026-08-12  
**Status:** Accepted — **G2**  
**OpenSpec:** [`archive/2026-08-12-overdub-pass-overlap-resolution`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/)  
**Review:** [`UNIFIED-PASS-ARCHITECTURE-REVIEW.md`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/UNIFIED-PASS-ARCHITECTURE-REVIEW.md)

### Decision

**G2:** Unify the **resolution** model now; keep **dual storage** as transitional encoding.

- MIDI overdub and NOTE_EDIT share canonical Add/Shorten/Hide geometry.
- Session accumulates one pending logical delta; commit encodes via capture chunks (Add) + edit rows (Shorten/Hide).
- Dual seal / `OverdubPassAdded`+`editPassIds` is encoding, not the semantic abstraction.
- U1/U2 persistent unification explicitly out of scope for this change.

### Consequences

- Phase 2 firmware unblocked under G2.
- DEC-031 describes transitional seal/undo encoding under this pin.
- Design §19: resolution unification ≠ storage unification.

---

## DEC-031 — Overdub overlap encode pending buffer to EditPass

**Date:** 2026-08-12  
**Status:** Accepted (encoding under DEC-032 G2)  
**OpenSpec:** [`archive/2026-08-12-overdub-pass-overlap-resolution`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/)  
**PREFLIGHT:** [`PREFLIGHT.md`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/PREFLIGHT.md)

### Decision

Overdub overlap encode is **C → A**:

- Session-local **pending overdub-operation buffer** on `Loop` (not a timeline pass).
- At stop: Adds → existing `OverdubPass` chunks; Shorten/Hide → existing `EditPass` rows.
- Do not extend `OverdubPass` with Shorten/Hide fields; do not write `EditPass` mid-session.
- One logical undo: extend `OverdubPassAdded` to carry companion `editPassIds` (field already on `UndoEntry`); apply/redo/reclaim/GUS honor those ids.
- Grouping identity: `OverdubPass.id` — no new overdub-operation identifier.
- Commit order: publish OverdubPass → save EditPass rows → push undo.
- When `overdubSourceView` is established, `shouldRestoreCommittedOverlapOnOverdubStop` / `removeOpenCaptureNoteOn` is **not** authoritative.

### Alternatives rejected

- **B** — ops on `OverdubPass` + materialize/schema rewrite.
- **Pure A mid-session EditPass writes** — live materialize / cancel / undo boundary races.
- **Two undo entries** — breaks one logical overdub undo.
- **New UndoEntryKind** — unnecessary when `editPassIds` exists on `OverdubPassAdded`.

### Consequences

- Phase 2 slice 1: pending buffer + native geometry bridge.
- Phase 2 slice 2: seal, undo bundling, restore gate, GUS round-trip for companion ids.
- No loop SD schema bump expected; GUS wire extends for `OverdubPassAdded` companions.

---

## DEC-030 — Sticky overlap end-of-participation on current state

**Date:** 2026-08-08  
**Status:** Accepted  
**Plan:** [`note_edit_resolver_authority_contracts_refinement`](../Plans/note_edit_resolver_authority_contracts_refinement.md) §11 step 5.3

**Context:** Sticky deselect cleared Focus `changedOverlapNoteIds` while leaving Visible shortened `currentSpan` (no flash restore). Participation membership could not be derived from geometry alone; the latch was a second authority.

**Decision:** Encode sticky end-of-participation as `NoteEditOverlapParticipationType::{Active,Ended}` on `NoteEditCurrentNoteState` (mirrored on `ParticipatingNoteState`). `clearChangedOverlapParticipationWhenInteractionCleared` sets Ended without rewriting geometry. `currentStateRowIsOverlapParticipant` returns false for Ended. Shorten/Hide/Restore re-enters Active. Focus latch remains dual-write until §11 step 5.5 deletion.

**Previous owner:** End-of-participation expressed only by forgetting `changedOverlapNoteIds`.

**New owner:** `NoteEditCurrentNoteState.overlapParticipation`.

**Validation:** Native fixtures for sticky clear → Ended, Shorten reactivates Active; display/inventory readers use current-state participation.

**Completion (2026-08-08):** §11 step 5.5 removed `changedOverlapNoteIds` and live-store membership reconcile; participation is current-state only (smoke HITL `025807`, `030432`, `032118`).

---

## DEC-029 — NoteEditCurrentState owns NOTE_EDIT editable note state

**Date:** 2026-08-07  
**Status:** Accepted  
**OpenSpec:** [`note-edit-current-state`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/) — normative [`openspec/specs/note-edit-current-state/`](../../openspec/specs/note-edit-current-state/)

**Context:** Same-pitch NOTE_EDIT overlap captures (`session_20260807_021939`, `session_20260807_021022`) showed stable `NoteId` identity was correct, but current editable geometry was reconstructed from `EditSession.store`, `baselineMap`, focus fields, overlap scratch, display order, and live-store scans. Stale committed baseline geometry affected later edits; stopgap guards (`sessionMovedNoteSpans`, overlap skip guards) prevented correct current-span overlap edits.

**Decision:** During NOTE_EDIT, `EditSession` owns editable note state through `NoteEditCurrentState`. `baselineMap` remains committed transaction baseline. `EditSession.store` becomes the canonical event projection of `NoteEditCurrentState` for playback preview, serialization, compatibility, and parity checks; it is not editable-state authority.

**Previous owner:** Current editable NOTE_EDIT geometry was effectively owned by `EditSession.store` plus scattered reconstruction helpers.

**New owner:** `NoteEditCurrentState` inside `EditSession`.

**Migration strategy:** Split read/projection access from mutation APIs, add read-only current-state build/verify, prove projection parity, migrate readers, migrate writers, migrate undo/redo and commit diffs, then remove compatibility state and direct projected-store mutation.

**Compatibility period:** `EditSession.store` remains projected MIDI event storage while readers/writers migrate. Legacy store-diff builders may remain as parity checks only.

**Removal trigger:** Delete `sessionMovedNoteSpans`, session-moved overlap skip guards, live-store geometry authority in resolver/action builder/commit, and direct NOTE_EDIT writes through `track.editAwareMidiEvents()` after native fixtures and HITL edit retest pass.

**Validation:** Native tests for 021939, 021022, repeated A/B move undo/redo commit, selection reorder, hidden/deleted/added rows, projection invariant, accessor gate, undo snapshot invariant, and commit parity; then firmware build and user-approved HITL edit retest.

**Completion (2026-08-08):** Phase 8 closeout — native 969/969; HITL PASS via capture matrix in [`PHASE8_CLOSEOUT.md`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md). **Archived** `2026-08-08-note-edit-current-state`.

---

## DEC-028 — EditSessionAction geometry pipeline (Phase 1 native)

**Date:** 2026-08-04  
**Status:** Accepted  
**OpenSpec:** [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/)

**Decision:** Implement Phase 1 as pure native modules — `EditSessionAction` types, D17 orchestrator helpers, `analyzeEditSessionInteractions`, `groupEditSessionInteractionsByTarget`, `resolveConstrainedGeometry` — without wiring `NoteMovementUtils` yet. Brownfield `overlapNotes` guards remain until Phase 4 retires scratch.

**Rationale:** Derived geometry pipeline needs testable analyze/resolve boundaries before replacing imperative restore-first paths. UIP Edit projection (`IntervalProjection`) is the D20 pre-analyze hook — no `normalizeWrapToLinear` module.

**Tests:** `test_edit_session_interaction`, `test_resolve_constrained_geometry`.

---

## DEC-027 — Deferred job scheduler north star

**Date:** 2026-07-18  
**Status:** Accepted; amended 2026-07-18 (vocabulary: `DeferredJobScheduler`, Job owns state, Workspace reserved for product)  
**Context:** Lazy restore, deferred persistence, and future display/export work share the same interruptible shape. Byte-chunk restore alone does not scale; a second ad-hoc scheduler per domain would duplicate policy (budget, focus, demote, reclaim). Early draft names (`CooperativeScheduler`, job Workspace) collided with product Workspace and did not name scheduler scope.

**Decision:**

1. **North star:** **`DeferredJobScheduler`** owns execution of all non-realtime, interruptible, resumable firmware work. Domain managers submit jobs and own job logic.  
2. **Phase A (now):** implement time-budgeted resumable **`LoadLoopJob`** under `StorageManager::runDeferredFrame()` — no new top-level scheduler type yet. See [`deferred_storage_time_budget_scheduler_enhancement.md`](Plans/deferred_storage_time_budget_scheduler_enhancement.md).  
3. **Phase B:** introduce `DeferredJobScheduler::runFrame()` and migrate execution ownership out of StorageManager (formal ownership trigger; OpenSpec before firmware).  
4. **Focus change:** demote jobs; do not cancel (preserves SD/parser progress). Cancel only for memory pressure discard, SD failure, unload, set close.  
5. **Commit:** atomic only — job execution state → Commit → published loop. No partial loop states. Job owns its unpublished fields until Commit (no separate Workspace/Context/Scratch type for job temp).  
6. **Preemption policy:** High normally preempts; finish current job when estimated remaining work &lt; scheduler slice (not %-complete).  
7. **Vocabulary:** Job; `DeferredJobScheduler`; product **Workspace** and **Slot** reserved. Avoid Workspace/Scratch/Context/Slot for scheduler resources.  
8. **Phase A pins:** load frame before display; deferred save stays separate under StorageManager; `applySnapshot` may exceed one slice in MVP.

**Consequences:** Phase A may ship without a new Manager. Phase B requires an OpenSpec change + architecture gate before `DeferredJobScheduler` lands. Extends DEC-026 Commit model; does not replace it. DEC-020 “cooperative budget-driven” persistence remains shipped history; the unified deferred owner type name is `DeferredJobScheduler`.

**References:** [`deferred_job_scheduler_architecture.md`](Plans/deferred_job_scheduler_architecture.md), DEC-026, DEC-020 (persistence budgets as prior art).

---

## DEC-026 — Commit-centered lazy slot load

**Date:** 2026-07-18  
**Status:** Accepted  
**Context:** Full-set boot drain is SD-bound (~10.5 s / 26 slots). Architecture review froze Commit (verb) over Publish; audible-first boot + on-demand deferred load.

**Decision:**

1. All runtime-visible loop changes occur through **Commit** (semantics, not a single helper).  
2. Hydration lifecycle `UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY` is architectural; storage flexible. COMMITTED is sufficient for play/edit/display; DERIVED_READY optional.  
3. Boot sync-commits audible set only; MVP may keep existing sync load. Runtime priorities: audible + explicitly requested. No speculative prefetch. Load-while-PLAYING interactive = later phase.  
4. OpenSpec change: `unified-commit-lazy-slot-load`. Architecture plans frozen under `docs/Plans/unified_publish_pipeline_*`.

**Consequences:** New branch for firmware; Phase 1 rename Publish→Commit; Phase 2 audible boot before cooperative session requirement.

**References:** [`unified-commit-lazy-slot-load`](../openspec/changes/unified-commit-lazy-slot-load/), [`unified_publish_pipeline_deferred_lazy_loading_architecture.md`](Plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md), DEC-021 (audible-ready gate amends full-set boot drain practice).

---

## DEC-024 — Loop-owned undo ownership direction

**Date:** 2026-07-08  
**Owner:** `TrackUndo` / display sidebar / multi-loop slots  
**Status:** Accepted — Phase 1 shipped; Phase 2 deferred

**Context:** Sidebar `U:nn` showed track-wide `GlobalUndoStack.cursor` and did not update when switching loops. Multi-loop specs describe per-loop undo; Track is becoming a coordinator while Loop owns Capture, Passes, Playback, and Persistence.

**Decision:**

1. **Phase 1 (now):** Keep `GlobalUndoStack` on `Track`; add `TrackUndo::*ForLoop` compatibility layer — filter depth and gate undo/redo when stack tip `slotIndex` matches selected loop. Display resolves `Loop&` once.
2. **Phase 2 (future):** Move `GlobalUndoStack` member from `Track` to `Loop`; target API `loop.undo()` / `loop.undoDepth()`. Requires design session before implementation.

**Consequences:** `U:` reflects selected loop pass-undo depth. Slot/Record gestures remain thin wrappers. Phase 2 touches persistence footer, `PassReclaim`, and redo tests.

**Reference:** [`docs/Plans/loop_undo_ownership_refinement.md`](Plans/loop_undo_ownership_refinement.md)

---

## DEC-022 — Runtime bundle save tail integrity

**Date:** 2026-07-07  
**Owner:** M5 boot load / `continuous-runtime-persistence` gate  
**Status:** Accepted; **shipped** 2026-07-07

**Context:** Fresh record after workspace wipe failed cold boot with `Current workspace load failed` despite `runtime.bundle.bin` tail SAVE token. Deferred save reopened `runtime.bundle.bin.tmp` with `seek(file.size())` after loop-slot writes; when a prior temp file was larger than the new bundle body, footer/undo stacks appended after a stale gap — bundle parse failed silently at epilogue.

**Decision:**

1. **Remove** existing `runtime.bundle.bin.tmp` at `beginDeferredSaveJob` before writing.
2. Track **`metaBundleWritePos`** on meta temp close; **reopen at that cursor**, not `File::size()`.
3. Emit **`BOOT load_stage=…`** / `#CAP,BOOT,load_stage,…` at bundle parse failure sites.
4. Native **`test_runtime_bundle_boot_load`** — snapshot skip round-trip + stale-gap footer regression.

**Consequences:** Cold boot shows `Current workspace loaded successfully` on device. Phase 2 persistence queue (DEC-020) remains paused until full 64+64 HITL passes.

---

## DEC-020 — Continuous runtime persistence architecture

**Date:** 2026-07-07  
**Owner:** OpenSpec `continuous-runtime-persistence`  
**Status:** Accepted

### Problem

64+64 HITL (`20260707_192649`) shows deferred save starved for the entire overdub window because `isCaptureActiveForPersistence()` blocks all slices during capture. Chunk pool and internal heap fill; overdub stop faults before `PERS,result`. Heap routing fixes (`runtime-derived-representation-heap`) improve seal heap but do not fix transport-gated persistence.

### Decision

Replace transport-gated save with **invariant-driven, cooperative budget-driven capture-chunk persistence**:

- Pass ownership ≠ storage ownership; pass may stay open while chunks persist.
- Separate lifecycles: runtime (`Free → Recording → Sealed`) vs persistence (`Not scheduled → Queued → Writing → Persisted`).
- **ChunkManager** (`LoopEventStore`) owns sealing, refs, allocation, reclamation.
- Persistence queue: seal-order admission, exactly-once enqueue, seal-order drain.
- Runtime recording/playback always precede persistence; persistence is cooperative and budget-driven.
- Memory reclaim independent of persistence completion.
- Scope v1: append-only capture-pass storage only; note edit / undo snapshots unchanged.
- Phased rollout: diagnostics (Phase 0) → ownership → queue → scheduler → mid-pass → recovery → HITL.
- Park persistence/stop-path patches on `runtime-derived-representation-heap`.

### Rationale

Architecture checkpoint: ownership and state transitions change — not a stop-path bugfix. SD writer is already chunk-granular; gap is scheduling. Implementation guided by invariants, not ad-hoc patches.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Remove transport gate first (scheduler before queue) | No persistable sealed-chunk work during capture until queue exists |
| More stop-path flush/defer patches | Does not drain SD during overdub; starves pool |
| Mandate CurrentSet v6 layout Option A | Invariants over implementation; format may evolve |

### Affected modules

`LoopEventStore`, `Loop.cpp`, `StorageManager`, `StorageLoopIo`, `PersistenceBudget`, `main.cpp`

### Constraints created

- Phase 0 diagnostics before scheduler behavior change; validate 64+64 HITL assumptions first.
- No note-edit continuous persistence in v1.
- Extend `StorageManager` / `LoopEventStore` — no new top-level manager classes (DEC-008).

### Related OpenSpec

[`openspec/changes/continuous-runtime-persistence/`](../../openspec/changes/continuous-runtime-persistence/)

### Migration notes

Supersedes persistence starvation workarounds on `runtime-derived-representation-heap`. Guide: [`docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md`](Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md).

---

## DEC-021 — Defer inactive loop slot restore at boot

**Date:** 2026-07-07  
**Owner:** runtime-derived-representation-heap OpenSpec change (M5)  
**Status:** Accepted; **shipped** 2026-07-07

**Context:** Cold boot stack overflow (`DACCVIOL`) during synchronous restore of all 8×8 loop slot payloads plus full undo snapshot bodies from SD. `workspace-session-persistence` had listed eager 8×8 load as a non-goal deferral; boot stack depth now requires it.

**Decision:**

1. **Split current set restore from SD** into two steps: (a) load current set bundle (transport + slot metadata + undo metadata), (b) restore loop slot payloads from `slots/loop_TT_SS.bin`.
2. At boot, queue **every** track/slot with a verified SD loop payload (up to 64) into `processDeferredLoopSlotRestore`; **priority orders restore only** — admission does not depend on playback layer or saved index flags. *(Amended 2026-07-09 — [`slot-performance-interaction`](../openspec/changes/slot-performance-interaction/) D21; was: enabled/active/selected only.)*
3. At boot, read undo stack **metadata** only (`readGlobalUndoStackMetadataFromFile`); hydrate snapshot bodies per track in idle (`processDeferredUndoSnapshots`) or before first undo.
4. Emit `#CAP,BOOT,ram1,...` after restore; extend `stabilizeBootMemoryAfterLoad` to trim undo when heap or pool free is below reserve.

**Consequences:**

- Supersedes workspace-session-persistence non-goal “all 8×8 at boot” for stack safety.
- Play entry uses `loop.midiEvents()` in `ensurePlaybackWindowBuilt`; transport start skips `updateAllTracks(0)` when no capture is pending.

**References:** DEC-019, [`m5_sd_load_extmem_routing_handoff.md`](Plans/m5_sd_load_extmem_routing_handoff.md), `loadCurrentSetBundleAndActiveLoopSlots`. **Amendment:** DEC-025, [`slot-performance-interaction`](../openspec/changes/slot-performance-interaction/) D21.

---

## DEC-025 — Split focus: playing / preview / pending

**Date:** 2026-07-09  
**Owner:** [`slot-performance-interaction`](../openspec/changes/slot-performance-interaction/) (D18–D21)  
**Status:** Accepted

**Context:** While transport runs, slot UI mixed preview (piano roll, edit) with playing (audible MIDI, bar/16th LEDs). Boot admission for deferred restore still filtered non-active slots (DEC-021 pre-amendment).

**Decision:**

1. **Preview slot** = `selectedSlotIndex` (no separate `previewSlotIndex` field). Updates immediately on slot peek / launch queue.
2. **Playing slot** = `activeLoopIndex`. While transport running, changes only via **committed playback transitions** (launch commit, capture finalize, quantized record start, layer-hold commit, post-clear restore).
3. **Pending slot** = `pendingSlotIndex` until `SlotQuantization` commit. Performance short-press launch uses **`LoopEnd`** to preserve phrasing.
4. **Display:** preview playhead at destination `loopStartTick`, flashing until playing catches up; **LED phase** stays on playing slot.
5. **`slotHasLoopContent`:** true when RAM has data or SD has verified payload.

**References:** [`slot-performance-interaction/design.md`](../openspec/changes/slot-performance-interaction/design.md) D18–D21, DEC-021 amendment.

---

## DEC-019 — SD load path extmem routing (M5 spike)

**Date:** 2026-07-07  
**Owner:** runtime-derived-representation-heap OpenSpec change  
**Status:** Accepted (spike documented); **Step 1b adopt-on-load shipped** 2026-07-07; hardware boot validation pending

**Context:** M2 routed runtime published flat to `SessionMidiEventVec`. After 64+64 HITL failure, device internal heap reached 0 bytes; clear and deferred save blocked. Reboot restored broken 64+64 state via recovery checkpoints. Code review: `deepCloneChunkRefs` in `Loop.cpp` flattens each pass to internal-heap `MidiEventVec` on `restorePassesSnapshot` and undo restore; load also calls `rebuildVisualCacheFromPasses()` synchronously.

**Decision:**

1. **Document** load-path gap as M5 spike in OpenSpec (`spike_sd_load_extmem_routing.md`) — not part of M1–M4 ship criteria.
2. **Target state:** undo pass clone uses extmem-first flat; **SD load adopts chunk refs** (`adoptPersistedSnapshot`) without deep clone; defer visual rebuild on load per Phase C idle policy; evaluate lazy slot load at boot.
3. **M4 archive** remains gated on 64+64 HITL; quarantine + M5 implementation may be required for reliable re-test after failed long runs.

**Consequences:**

- Spec delta ADDED scenarios under `internal-heap-external-memory-routing` (marked spike).
- Implementation tasks tracked in `tasks.md` § M5.

**References:** DEC-016, DEC-018, [`spike_sd_load_extmem_routing.md`](../../openspec/changes/runtime-derived-representation-heap/spike_sd_load_extmem_routing.md), [`64bar_regression_commit_analysis_enhancement.md`](Plans/64bar_regression_commit_analysis_enhancement.md).

---

## DEC-018 — Admission current heap; derived-rep OpenSpec consolidation

**Date:** 2026-07-07  
**Owner:** runtime-derived-representation-heap OpenSpec change  
**Status:** Accepted

**Context:** July HITL proved 64-bar record stop→PLAY works at 8 KB RAM1 but deferred save never dispatches because `processDeferredSaveState` gates on frozen `admissionHeap` from record stop. Separately, `passesMaterializedStore_` lazy flat still uses internal heap; 64+64 verifier misses `#CAP,ST,OVERDUBBING,PLAYING` when capture ring overflows under MO burst. Cursor plans `64bar_regression_commit_analysis_b1378b37` and `heap_recovery_16bar_ebfaa9cd` were never converted to OpenSpec.

**Decision:**

1. **Consolidate** root-cause analysis, architecture, and tasks into OpenSpec change `runtime-derived-representation-heap` (no heap↔PSRAM FIFO).
2. **Admission:** gate deferred-save **dispatch** on current `getInternalHeapFreeBytes()`; stop-path `admissionHeap` is telemetry only.
3. **HITL floor:** `record_stop_min_free_ram2_bytes` default **0** (telemetry); optional warn at 12 KB — long-run PASS is heartbeat + core transitions + persistence, not stop-entry floor.
4. **Derived reps:** published flat and playback window on `SessionMidiEventVec`; tier-A `#CAP` flush priority under long overdub.
5. **Bisect gates** remain parked (DEC-017); re-open selective 64-bar gates after M1–M4.

**Consequences:**

- [`docs/Plans/64bar_regression_commit_analysis_enhancement.md`](Plans/64bar_regression_commit_analysis_enhancement.md) links to the OpenSpec change.
- User-local Cursor plans marked superseded (not deleted).

**References:** DEC-016, DEC-017, commit `cdd9c2b`, [`openspec/changes/runtime-derived-representation-heap/`](../../openspec/changes/runtime-derived-representation-heap/).

---

## DEC-017 — Skip long HITL/capture gates; implement runtime redesign

**Date:** 2026-07-07  
**Owner:** runtime architecture track (`docs/Authority/Architecture/`, Phase A→C in [64bar_regression_commit_analysis_enhancement.md](Plans/64bar_regression_commit_analysis_enhancement.md))  
**Status:** Accepted

**Context:** 64+64 commit bisect and serial capture gates are slow, unreliable across SD/firmware version mismatches, and duplicate evidence already in archived `captures/` (June PASS vs July FAIL). Partial Phase A shipped in `d635296` (boot + 16-bar record). Remaining blocker is playback materialize on PLAYING entry (H6) — an architecture/scheduling fix, not another bisect loop.

**Decision:**

1. **Skip** as implementation gates: save-bypass HITL, commit bisect at anchor SHAs, `validate-64x64` HITL, UIP Phase 5.5 HITL matrix — until runtime Phase A→C is complete.
2. **Keep** archived serial evidence in `captures/` and investigation plans as **historical** reference only; do not block progress on new long captures.
3. **Implement** runtime invariants per DEC-016: Phase A → Phase B → Phase C (see [64bar_regression_commit_analysis_enhancement.md](Plans/64bar_regression_commit_analysis_enhancement.md)).
4. **Verify** with `pio test -e native` and short manual smoke (boot, 16-bar record); re-open long HITL only after Phase C if needed.

**Consequences:**

- [CURRENT_WORK.md](Runtime/CURRENT_WORK.md) priority is runtime redesign, not bisect.
- Bisect helper `scripts/run_64bar_bisect_anchor.sh` is parked, not maintained.
- UIP Phase 6 / overlap resume stays blocked until runtime phases ship.

**References:** DEC-016, [next_session_handoff_overdub_uip_architecture.md](Plans/next_session_handoff_overdub_uip_architecture.md), commit `d635296`.

---

## DEC-016 — Runtime architecture four-layer model

**Date:** 2026-07-07  
**Owner:** `docs/Authority/Architecture/` (conceptual); brownfield build owners per layer (`Loop`, `Track`, `EditManager`, `DisplayManager`, `IntervalProjection`)  
**Status:** Accepted

**Context:** The 64-bar PLAYING-window regression investigation showed display, playback, and LED paths each deciding when to rebuild timeline data. UIP (DEC-015) unified interval math but not representation ownership, revision chains, or consumer scheduling. Investigation detail must not live in permanent architecture docs.

**Decision:**

1. **Four layers** (orthogonal): **Capture Storage** → **Derived Representations** → **Interval Projection** → **Runtime Request** (representation × `TickInterval` → consumer result).
2. **Derived representations** are not consumer-owned caches. Each documents: owner, dependencies, revision, invalidation, build policy, consumers. Prefer the term *derived representation* in new docs; reserve *cache* for true memoization (e.g. COW materialized flat).
3. **Intervals are consumer-agnostic** — one `TickInterval` (e.g. bars 8–24); playback, display, edit, and LED interpret the same range. Interval projection remains in `IntervalProjection` (DEC-015); it does not own storage or representations.
4. **Revision chain:** storage mutation → event representation → downstream display/playback representations. Consumers validate staleness; they do not call peer rebuild APIs on hot paths (e.g. `ensureVisualCacheBuilt` from `MidiLedManager` during PLAYING).
5. **Scheduling:** document **responsibilities** (owner, policy, defer vs immediate). A dedicated scheduler is optional implementation — not an architectural requirement.
6. **NOTE_EDIT exception:** `NoteEditSession.store` is a live overlay on passes during edit (Tier-2 playback audition via `sessionMidiEvents()` / `sessionPreviewRevision_`) — not folded into a single loop event representation.
7. **Doc split:** permanent model in `docs/Authority/Architecture/`; regression bisect/evidence in `docs/Plans/*_bugfix.md`; concrete patches in `docs/Plans/*_refinement.md`.

**Consequences:**

- Agents load [RuntimeArchitecture.md](Authority/Architecture/RuntimeArchitecture.md) for display/playback/LED read paths before changing rebuild behavior.
- PLAYING/overdub hot-path work defers full display representation rebuild (see [overdub_start_playing_window_hot_path_refinement.md](Plans/overdub_start_playing_window_hot_path_refinement.md)).
- Future features (clip launch, multi-window, LTS) add a representation, an interval source, or a consumer — not parallel cache systems.
- Code rename (`VisualCache` → representation naming) is deferred; docs use architecture terms with brownfield mapping table in [DerivedViews.md](Authority/Architecture/DerivedViews.md).

**References:** DEC-015, [unified-interval-projection/design.md](../openspec/changes/unified-interval-projection/design.md), [overdub_start_64bar_playing_window_regression_bugfix.md](Plans/overdub_start_64bar_playing_window_regression_bugfix.md).

---

## DEC-015 — IntervalProjection Stage 1 / Stage 2 split

**Date:** 2026-07-05  
**Owner:** `IntervalProjection` (`include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp`)  
**Status:** Accepted

**Context:** Display, playback, and edit each duplicated loop-wrap math. `edit-session-action-geometry` would add a third path (`normalizeWrapToLinear`).

**Decision:**

1. **Module home:** `IntervalProjection` in `Utils/` — single engine for all wrap/linearization consumers.
2. **Stage 1 (`generateEquivalentIntervals`):** Pure math — bounded ±k·`loopLength` shifts from canonical `TickInterval` spans; k bounds derived from `ProjectionContext.window` intersection; no `ProjectionType` policy.
3. **Stage 2 (`selectProjectedInterval` / `selectProjectedIntervalsForDisplay`):** Consumer selection only (Playback / Display / Edit / reserved Timeline).
4. **Phase helpers** (`tickPhaseInLoop`, `noteRelativeTick`, `noteStorageTick`, projection-cycle helpers) centralized in `IntervalProjection`; `TickPhase.h` and `SelectNavigation` thin-wrap for brownfield call sites.
5. **`window` is `TickInterval` input frame (D23)** — never `ProjectedNoteInterval`; projection preserves `noteId` across k copies (D8).

**Consequences:** Phases 2–5 migrate consumers to supply `ProjectionContext` only; overlap pipeline blocked until Phase 5 HITL. Native gate: `test_interval_projection`.

**References:** `openspec/changes/unified-interval-projection/`, DEC-013 linear storage.

---

## DEC-014 — Dual normalization boundaries (micro vs macro)

**Date:** 2026-07-03  
**Owner:** `LoopTickNormalize`, NOTE_EDIT commit path  
**Status:** Accepted

### Problem

Single `normalizeWindow` at every boundary blurred live-interaction consistency with persistent canonical commit.

### Decision

- **Micro:** `normalizeWindow` on **edit closure set** at `publishDependentFaderLatch` — local geometric consistency for faders/projection; not sole persistent canonical authority.
- **Macro:** `normalizeAll` at `commitAllPendingNoteEditActions` — full-store canonical invariants, undo snapshots, pass readers. MUST NOT be skipped.
- **Closure set:** seed modified `NoteId`s → paired on/off, overlap participants, wrap interactors; no UI window/selection as scope.
- **Playback during edit:** Tier 2 `sessionMidiEvents()` — verification only, no merge overlay.
- **Set window:** drives F1/F2 range; full-loop window → wrap at fader extremes; partial-window slide deferred.

### Consequences

- Phase 2 wires closure-set computation + dual hooks before HITL 152335.
- OpenSpec: `linear-loop-tick-storage`, `note-edit-modification-session`, `note-edit-fader-feedback`.

---


## DEC-013 — Linear loop tick validate vs normalize

**Date:** 2026-07-03  
**Owner:** `LoopTickNormalize`, `LoopEventValidation` (`include/Utils/`)  
**Status:** Accepted

### Problem

Loop MIDI storage mixed modulo ticks, wrap-pair geometry, and projection — causing edit move cutoff and loop-stretch display inflation.

### Decision

- **Invariants** — pure boolean predicates in `LoopEventValidation`; MUST NOT mutate storage or call normalize.
- **Conversion rules** — pure transforms in `LoopTickNormalize`; ONLY place legacy wrap-pair / synth-off shapes become canonical linear spans.
- **Normalize timing** — boundary-based only (not mid-pipeline, not on SD load). See DEC-014 for micro/macro split.

### Consequences

- `validateAndCleanupMidiEvents` logs canonical failures and performs orphan removal only (no synth loop-end insert on idle).
- OpenSpec: `openspec/changes/linear-loop-tick-storage/`.

---

## DEC-012 — StorageSession persistence state model

**Date:** 2026-06-29  
**Owner:** `StorageManager` (persistence)  
**Status:** Accepted

### Problem

Revision persistence and overlay work (~30 commits) left ~80 anonymous statics in `StorageManager.cpp`. Overlay coordination used imperative `PersistencePhase` assignments (8 sites) alongside bool FSM flags. Sprint naming (**pipeline**, **prompt**, **saveThenLoad**, **LoadRequestGate**) reads as prose and blurs the display/backend split.

### Decision

1. Introduce **`StorageSession`** struct — RAM aggregate of storage **jobs**, owned exclusively by **`StorageManager`** (DEC-008).
2. Jobs: `currentWorkspaceSave`, `revisionCommit`, `revisionLoad`, `setBrowserNavigation`, `bootRecovery`.
3. Revision load lifecycle: **requested → held** (`heldForWorkspaceDirty`) **→ dispatched** (`pending` / `inProgress` / `stage`). Flag **`loadAfterRevisionCommit`** for commit-then-load path. Remove **`revisionLoadPipelineActive`** when derived phase lands (Tier 2).
4. APIs use **request / confirm / cancel / dispatch**; rename away from `*DirtyPrompt*` / `*Pipeline*` / `*Staged*` on backend surfaces.
5. Drop **`LoadRequestGate`**; use `RevisionLoadPolicy::shouldHoldRevisionLoadRequest(bool workspaceDirty)`.
6. **Derived** coordination phase (Tier 2): `AwaitingRevisionCommit`, `RevisionLoadActive`, `RevisionCommitActive` — not stored on session.
7. Display overlay names stay on **`DisplayManager`**; no `StorageOverlaySession` or `*Intent*` struct names.
8. Implementation tiers: 0 snapshot/tests → 2 derived phase → 1 struct migration → 3 TU split before `transport.bin` / `global.bin`.

### Rationale

Matches **EditSession + EditManager** pattern (session struct + coordinator) without a new Manager. Reuses established **request**, **pending**, **inProgress**, **stage**, **job**, **deferred** vocabulary. Keeps front/back naming split.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| `SaveSessionManager` / `StorageHandler` | DEC-008; Handler suffix is event routing, not FSM ownership |
| `SaveSession` / `PersistenceSession` namespace as owner | Ownership ambiguity; collides with rejected Manager name or new top-level noun |
| `StorageOverlaySession` | **Overlay** implies display scope; backend is load/save jobs + navigation |
| `*Intent*` subgroup | New metaphor; not in repo suffix vocabulary |
| `LoadRequestGate` / **Gate** suffix | Only used in 3.7 sprint; queue vocabulary (**held**, **requested**) already fits deferred save |
| Keep imperative `PersistencePhase` only | Desync risk across 8 assignment sites; tested derivation is safer |
| Move drill navigation to `DisplayManager` now | Would be ownership transfer; defer — `StorageManager` already mutates `NavigationState` |

### Affected modules

`StorageManager`, `StorageSession` (new), `RevisionLoadPolicy`, `SetBrowserOverlayPolicy`, `DisplayManager` (getters), `MidiButtonActions`, native tests

### Constraints created

- No parallel persistence owner (DEC-008).
- Tier 0–2 must not reshape deferred/revision FSM **step logic** during active overlay tasks except naming/migration agreed in handoff.
- HITL/serial wire strings may keep legacy names until explicitly migrated.

### Related OpenSpec

`set-revision-persistence`, `load-save-overlay-display-regression`

### Migration notes

Handoff: [`docs/Plans/storage_session_state_refactor_handoff.md`](Plans/storage_session_state_refactor_handoff.md). Builder starts Tier 0.

---

## DEC-011 — Bias toward progress

**Date:** 2026-06-29  
**Owner:** Process (`ARCHITECTURE_REASSESSMENT.md`, agent workflow)  
**Status:** Accepted

### Problem

Governance risked becoming a bottleneck — agents stopped for full preflight/reassessment on normal extension work (new methods, display updates, bug fixes).

### Decision

**Default: continue implementation.** Decision ladder: extend owner → reuse extension point → follow DECISION_LOG → reassess only on [formal triggers](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) → else implement. Max one reassessment per session; if rejected, return to implementation. Lightweight inline preflight for non-trigger work; full PREFLIGHT only when triggered.

### Rationale

Governance exists to detect **structural** change, not to approve every edit.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Full preflight always | Too heavy; slows bug fixes and display work |
| No triggers / trust agents | Loses structural guardrails |
| Stop on “feels unclear” | Indefinite planning loops |

### Affected modules

Agent workflow, PREFLIGHT modes, ARCHITECTURE_REASSESSMENT trigger list

### Constraints created

>3 files alone is not a trigger — must cross ownership. Explicit “not triggers” list documented.

### Related OpenSpec

N/A

### Migration notes

Supersedes implicit “always stop for 3+ files” interpretation.

---

## DEC-010 — Ownership evolution protocol

**Date:** 2026-06-29  
**Owner:** Process (`docs/Authority/ARCHITECTURE_RULES.md`)  
**Status:** Accepted

### Problem

Strict ownership rules risked freezing architecture. Agents responded with permanent adapters, duplicated state, and shadow Managers instead of explicit transfer.

### Decision

**Ownership transfer protocol** in ARCHITECTURE_RULES: reassessment + migration plan + documented compat layer + removal schedule + approval + DECISION_LOG. Temporary delegation/adapters allowed; parallel permanent ownership forbidden.

### Rationale

Architecture must evolve without drift or workaround layers that never leave.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Never allow ownership change | Forces shadow owners and duplicated fields |
| Allow transfer without removal schedule | “Temporary” compat becomes permanent |
| Adapters without reassessment | Same as shadow ownership |

### Affected modules

All modules; template `OWNERSHIP_TRANSFER.md`; reviewer/builder/architect roles

### Constraints created

Compat layers require creation date, removal condition, maximum lifetime. Search owner + extension points before transfer.

### Related OpenSpec

N/A

### Migration notes

Example path: surface-agnostic Actions (DEC-005) should use this protocol when moving input ownership off `MidiButtonActions` only.

---

## DEC-009 — Runtime state vs roadmap split

**Date:** 2026-06-29  
**Owner:** Process (`docs/Runtime/`)  
**Status:** Accepted

### Problem

`PROJECT_STATE.md` mixed active work, future milestones, and next actions. Agents could implement roadmap items (D13, JamRecorder) thinking they were current scope.

### Decision

Split `docs/Runtime/` into **PROJECT_STATE** (execution context), **CURRENT_WORK** (now / not now / completion), **ROADMAP** (future only, never implementation authority). Agents load PROJECT_STATE + CURRENT_WORK before planning; ROADMAP optional.

### Rationale

Operational state must be separable from sequencing information to prevent speculative scaffolding.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Single PROJECT_STATE file with sections | Agents still skimmed roadmap sections as tasks |
| ROADMAP in DELIVERABLE_TRACKING only | Session start does not load that file by default |

### Affected modules

Agent workflow, session closeout, PREFLIGHT loaded docs

### Constraints created

No future milestones in PROJECT_STATE. Implementation must appear in CURRENT_WORK § Now implementing.

### Related OpenSpec

N/A

### Migration notes

Post-M7 sequence moved from PROJECT_STATE to ROADMAP.md.

---

## DEC-008 — Authority conflict resolution

**Date:** 2026-06-29  
**Owner:** Process (`docs/Authority/README.md`)  
**Status:** Accepted

### Problem

Authority ordering defined precedence but not conflict handling. Example: OpenSpec proposes `SaveSessionManager` while ARCHITECTURE_RULES assigns persistence to `StorageManager` and discourages new Managers — agents had no deterministic resolution.

### Decision

Explicit conflict matrix in `Authority/README.md`. OpenSpec owns **behavior**; ARCHITECTURE_RULES owns **structure**. On conflict: **STOP**, architecture reassessment, user approval, update docs, then code. Prefer extending existing owner over new Manager.

### Rationale

Prevents architecture drift, endless replanning, and partial “stub” implementations that bypass ownership.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| OpenSpec always wins | Would redefine architecture through tasks without review |
| Architecture always wins | Would make specs unimplementable without reassessment path |
| Implement and fix later | Partial implementations become production debt |

### Affected modules

Agent workflow, OpenSpec proposals, PREFLIGHT authority conflict check

### Constraints created

No code on authority conflict until reassessment approved. No stub Managers to unblock specs.

### Related OpenSpec

N/A — applies to all changes

### Migration notes

Worked example: `SaveSessionManager` → extend `StorageManager` or approved new owner + DECISION_LOG.

---

## DEC-007 — Historical decision reuse enforcement

**Date:** 2026-06-29  
**Owner:** Process (`docs/DECISION_LOG.md`, agent workflow)  
**Status:** Accepted

### Problem

Agents recreated rejected abstractions, helpers, and ownership patterns because historical decisions were not mandatory reading before implementation.

### Decision

Mandatory **DECISION_REVIEW** + PREFLIGHT § Similar Historical Decisions before firmware edits. Structured **DECISION_LOG** at `docs/DECISION_LOG.md`. Reviewer rejects duplicate abstractions and ignored DEC-### entries.

### Rationale

Planning protected architecture but did not force explicit reuse/challenge of prior conclusions as project history grows.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Rely on agent memory across chats | Unreliable; tabs close |
| Changelog-style decision doc | Conflates shipped code with architectural choices |
| Block all redesign | Legitimate overturn via reassessment + supersede entry required instead |

### Affected modules

Documentation and `.cursor/rules/Agent-Context-Workflow.mdc` only

### Constraints created

New architecture decisions MUST be logged. Supersede — never delete. Challenge via ARCHITECTURE_REASSESSMENT.

### Related OpenSpec

N/A

### Migration notes

`docs/Runtime/DECISION_LOG.md` redirects here. Entries DEC-001–DEC-006 migrated to structured format.

---

## DEC-001 — Load/save overlay confirm control

**Date:** 2026-06-29  
**Owner:** `SetBrowserOverlayPolicy`, `MidiButtonActions`, `GpioButtonManager`  
**Status:** Accepted

### Problem

Load/save overlay needs a single confirm gesture without conflicting with record, scroll, or note-edit bindings.

### Decision

Overlay confirm = **Edit short (note 38)** or **encoder short** only.

### Rationale

Scroll uses REC/PLAY and track buttons; NOTELEN is suppressed or bound to note-edit semantics elsewhere. Edit short and encoder are already the user-facing “commit row” affordance in overlay docs.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| NOTELEN short for confirm | Conflicts with note start/end and delete/create in edit; overlay suppresses NOTELEN for other roles |
| REC/PLAY short for confirm | Used for list scroll down in overlay |

### Affected modules

`SetBrowserOverlayPolicy`, `MidiButtonActions`, `GpioButtonManager`, README overlay table

### Constraints created

No new confirm binding without surface-agnostic Actions refactor for base module.

### Related OpenSpec

`set-revision-persistence` (set-browser-overlay)

### Migration notes

Reopen when base-module-only confirm needed without encoder — extend Actions layer first, do not add a third confirm note without reassessment.

---

## DEC-002 — Set revision vs flat SavedSet snapshot

**Date:** 2026-06-29  
**Owner:** `StorageManager`  
**Status:** Accepted

### Problem

Need durable Sets on SD without disrupting live **Current** workspace during performance.

### Decision

**Current** = mutable auto-saved workspace. **Save** = append immutable **Set revision** (REVPK) via deferred FSM. Current unchanged after Save.

### Rationale

Performers keep playing from Current; revisions are catalog snapshots for recall. Flat replace-on-save breaks flow.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Flat SavedSet as primary model | Superseded by revision catalog (`currentset-savedset-storage-layout` parked) |
| Save clears Current | Violates “music keeps running” intent |

### Affected modules

`StorageManager`, `SetRevisionCatalog`, `SavedSetCatalog`, overlay browser

### Constraints created

No silent revert to flat SavedSet-only layout. Schema changes require OpenSpec + migration notes.

### Related OpenSpec

`set-revision-persistence`, `workspace-session-persistence` (SUPERSEDED.md for old M3/M4)

### Migration notes

Parked: `currentset-savedset-storage-layout`. Reopen only via new OpenSpec change if revision model fails field validation.

---

## DEC-003 — D13 jam recording ordering

**Date:** 2026-06-29  
**Owner:** Timeline / Phase 3 (not yet implemented)  
**Status:** Accepted

### Problem

Phase 3 jam capture was scoped before timeline prerequisites existed, causing architectural drift.

### Decision

Ship order: **JamRecorder (JamAction) → M10 → D13 arrangement capture last**. Pool-budget and M8 edit precede jam capture.

### Rationale

Capture depends on stable passes[], undo, and slot infrastructure already shipped; D13 OpenSpec started too early.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Early `jam-recording` OpenSpec | Parked — prerequisites missing |
| Implement capture from `phase-3-multi-loop.md` alone | Slots yes, capture no; R1–R5 unresolved |

### Affected modules

Future: `Track`, `TrackManager`, jam state fields (read-only until JamRecorder)

### Constraints created

No D13 wiring in production paths until JamRecorder + M10 scoped. See DELIVERY_RULES hard guards.

### Related OpenSpec

Parked: `archive/20260617-parked-jam-recording-d13/`

### Migration notes

Reopen when `jam-recorder` change proposed and M10 design exists.

---

## DEC-004 — Record/overdub stop validation

**Date:** 2026-06-29  
**Owner:** `Loop`, `Track`, `LoopStopFinalize`  
**Status:** Accepted

### Problem

Long loops cannot afford full-loop validate or SD I/O on record/overdub stop.

### Decision

Stop path = `finalizeLoopAtStop` + wrap window only. Full validate via deferred idle maintenance. Persistence via `requestDeferredSaveState`.

### Rationale

Documented in LOOP_MIDI_STORAGE guide and archived pool-budget / memory-headroom specs; HITL and native tests depend on bounded stop path.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Full `validateAndCleanupMidiEvents` on stop | Latency and memory on long loops |
| SD write on stop hot path | Violates deferred save architecture |

### Affected modules

`Loop.cpp`, `Track.cpp`, `StorageManager`, `main.cpp` idle drain

### Constraints created

Hot path: no full flatten/validate on stop. No new stop-side persistence without reassessment.

### Related OpenSpec

`openspec/specs/timeline-epochs/`, `long-record-memory-headroom/`, `storage-loop-io/`

### Migration notes

Normative — challenge only via architecture reassessment and spec amendment.

---

## DEC-005 — GPIO base module vs DROID-only actions

**Date:** 2026-06-29  
**Owner:** Input / `MidiButtonActions` (future: shared Actions)  
**Status:** Accepted

### Problem

DROID development left `ButtonManager` dormant while actions live only in MIDI paths.

### Decision

DROID is extension only. Encoder + 4 GPIO must remain capable in principle. Revive GPIO via **shared Actions layer**, not parallel implementations.

### Rationale

PROJECT_INTENT decisions 1–4; base module is the product hypothesis.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| DROID-only core workflow | Violates intent litmus “base module alone” |
| Wire `ButtonManager` without shared Actions | Duplicates `MidiButtonActions` |

### Affected modules

`ButtonManager` (dormant), `MidiButtonActions`, `GpioButtonManager`

### Constraints created

No new MIDI-only action paths for core record/undo/nav. New input surfaces must map to existing Actions.

### Related OpenSpec

`note-edit-session-undo-gpio` (archived GPIO geometry)

### Migration notes

Reopen when surface-agnostic Actions refactor is explicitly scheduled.

---

## DEC-006 — Agent context harness vs chat history

**Date:** 2026-06-29  
**Owner:** Process (`docs/Authority/`, `docs/Runtime/`)  
**Status:** Accepted

### Problem

Design exclusions and rejected alternatives were lost when chat tabs closed, causing repeated debates.

### Decision

Durable memory in `PROJECT_STATE.md`, this `DECISION_LOG.md`, OpenSpec, and mandatory historical review before implementation.

### Rationale

Chat threads are not searchable and agents do not load them by default.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Rely on open chat tabs | Not portable across sessions |
| Full transcript archive in docs | Noise; extract decisions only |

### Affected modules

Documentation and agent workflow only (no firmware)

### Constraints created

New architecture decisions MUST be recorded here. Session closeout when alternatives discussed.

### Related OpenSpec

N/A

### Migration notes

Supersedes informal “keep the tab open” workflow.

---
