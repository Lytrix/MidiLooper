# Design — revision manifests over fixed event pages (investigation)

## Reassessment record

| Field | Content |
|---|---|
| **Reason triggered** | Persistent state model changes (canonical content becomes pass runs + manifests); undo semantics change (selection of a retained manifest); new owner (`RevisionStore::Store`) if adopted. Owner-requested investigation, 2026-11-01. |
| **Current architecture** | Canonical content: `LoopEventStore` chunks (256 rows, 512-id pool, refcounted, block-backed on demand) referenced by `LoopPass` chunk-id lists (`std::vector`). Effective state: `LoopSnapshot` (shared_ptr membership pages in a slab arena) built by a deferred publication job; until it lands, readers use overlays, frozen Record readers or the merged fallback, each with its own currency rule (DEC-073 coverage, DEC-241 owed/containment, DEC-243 boot views). |
| **Proposed evolution** | Canonical truth = immutable pass runs on fixed pages + an immutable manifest per committed revision, created at commit. Every playback window, view, bridge, `visualCache` and save buffer is a derived cache stamped with the manifest it read. Background compaction reduces run count at equal revision. |
| **Migration impact** | Stage A: two new files, one native suite. Later stages touch `Loop`, `LoopEventStore`, `Track` playback routing, `StorageManager` (runs map naturally to files), undo; each stage separately approved. |
| **Recommendation** | Stage A done. Stage B approved by the owner (2026-11-01) as **qualification only**; the manifest is framed as the revision index with `LoopSnapshot` kept as a derived cache (proposal § Authority framing), so no Snapshots.md amendment is owed unless stage B shows the manifest should take over the snapshot's semantic role. |

## Model

```
EventPage   [kEventsPerPage x PassEvent(16 B)]     free stack of PageId; acquire/release are O(1)
PassRun     pages[], midiCount, hideCount, refs    mutable only while its single writer appends
            sealed at commit: Midi rows by (tick, seq), then HideNote rows by noteId (in-place heapsort)
Manifest    revision, loopLengthTicks, runs[]      immutable; created whole by commitPass()
History     kHistoryDepth manifest ids + cursor     undo/redo move the cursor; commit after undo drops redo
```

## Decisions (prototype-level)

- **D0 — Pages need not be contiguous.** A run is a directory of arbitrary `PageId`s; fragmentation of the free stack
  costs nothing (test `test_pass_spans_non_contiguous_free_pages`).
- **D1 — Commit cannot fail.** `beginPass` reserves the run descriptor, the first page and the manifest slot the
  commit will use. Every capacity shortfall is a named denial at admission or append (`DeniedNoPage`,
  `DeniedRunFull`, `DeniedManifestFull`, …), never a stuck ARMED state or a partial commit. A capacity denial during
  append **latches** (`writerStop()`): every later append returns the same reason, the committed revision is untouched,
  and the writer either abandons (every provisional page returns) or commits the rows captured so far as a complete
  smaller revision. Which of the two the looper does is a policy for stage D, not a store property.
- **D2 — The stamp comes from the read.** `composeWindow` returns `{manifest, identity, revision, count, status}` from the
  manifest it composed. A demanded revision cannot be written onto a window; a full output buffer is
  `OutputFull`, not a short window claiming completeness. This is the property the revision-7 empty window violated.
  `revision` is logical (compaction keeps it); `identity` is physical, monotonic and never reused, unlike the manifest
  slot index — a window built from pre-compaction manifest 41 can never be read as having come from 42.
- **D3 — Composition order** is (tick, equal-tick class NoteOff < NoteOn < other, run order in the manifest, seq),
  matching production's canonical class (`LoopEventStore` private canonical order, `midiEventChronologicalLess`).
  `seq` is a 32-bit append ordinal that survives seal; a compacted run renumbers it in composed order. Hidden notes are dropped. Cost per window is
  O(runs · log rows) to seek plus O(rows in window · runs · log hides) — bounded by the window, not the loop.
- **D4 — Hides are whole-note tombstones by `noteId`**, applying to earlier runs only. With replacement rows in the
  same run they express overlap-resolve (`Hide`, `Shorten`) and NOTE_EDIT (an `Edit` run); B.3 shows parity by output.
  The writer must stamp NoteOff rows with their note's id.
- **D5b — Commit does not sort.** A run's Midi rows are kept as up to `kSegmentsPerRun` sorted segments while appending
  (same-tick class inversions fixed in place over ≤ 32 rows; a tick decrease opens a segment); composition merges
  segments like runs. HideNote rows live in their own region, the only part ordered at commit. Overflow falls back to a
  counted commit-time sort.
- **D0b — Three layers (DEC-245).** Committed source rows are lossless (multiplicity, identity, zero-length pairs);
  effective meaning is an explicit `EffectivePolicy` projection at compose time; windows and snapshots are caches.
  Compaction keeps every row and hide with `origin` + source serial provenance.
- **D4b — Edit precedence and the edit index (B.5d).** A hide targets a `noteId` lineage. A Midi row is hidden iff the
  latest hide of its `noteId` is at a strictly later (run index, origin) position. The `EditIndex` (noteId → latest
  hide position) is a cache keyed by manifest identity, built off-edge in bounded steps; real-time composition asks
  for it with `HideLookup::IndexOnly` and gets `IndexOwed`, never a build.
- **D5c — Compaction takes history-closed ranges only (B.5a default).** A range qualifies when every history entry holds
  all of it at the same positions or none of it; publish rewrites them all, so no second copy outlives the job. With
  undo depth 99 nothing closes during a normal session: many runs, composed by a heap merge on cached keys, are the
  steady state, and compaction only tidies aged-out history.
- **D5 — Compaction is range-based and revision-preserving.** (Was prefix-only; B.5 added `beginCompaction(first, count)`
  with the range's hides carried into the merged run when `first > 0`, and a publish that rewrites every history entry
  holding the range.) Merging runs `[0, k)` applies and drops the hides inside
  the prefix (they can only target rows inside it). It runs in bounded steps (`stepCompaction(maxRows)`) with its
  source pinned; at the end it publishes only if the current manifest still starts with the compacted runs, carrying
  any runs committed meanwhile, else it is `Superseded`. The compacted manifest replaces the history entry at the cursor;
  pinned readers keep the old manifest until they unpin. Overdub admission is denied at `kRunsPerManifest` until
  compaction runs — that is the back-pressure, and it is off the commit path.
- **D6 — Reclamation by reference count**: manifests count history entries and pins; runs count manifests; a run's
  pages return to the free stack when its count reaches zero. Single-core, main-loop only; the ISR never touches
  the store (it consumes published windows — stage C).
- **D7 — Sizing is a budget, not an estimate** (filled by B.5, [evidence](evidence/2026-11-01-b5-capacity-budget.md)).
  One page pool shared by all slots, sized to today's admission ceiling (131,072 events): 2,048 content pages + ~320
  per-run waste + ~256 for one capped compaction range in flight + ~64 for writers and pinned generations ≈ 2,688 pages
  (2.63 MiB), plus ~0.13 MiB metadata once run directories are paged. That is about the same as today's chunk backing
  for the same ceiling (2.67 MB), *provided compaction only takes history-closed ranges*: compacting runs that history
  still names keeps both copies alive (measured 2–3.3× content). Device (B.5b): after one owner-rig session the
  firmware alone holds 7.15 MB of 8.38 MB (1.33 MB free; content chunks 1.0 MB), so migration must replace owners,
  not add the store beside them.
- **D8 — Time slices deferred.** Per-run sorted rows already give bounded window seeks. Slicing roots into bar tiles is
  revisited when edits need partial replacement (stage B Q3), not before.

## Stage A evidence required

1. Record + four overdubs committed back to back: each revision composes exactly its committed passes; pinned older
   manifests still compose theirs.
2. An uncommitted overdub never changes the current manifest; abandoning it returns its pages.
3. Every two-bar window of a 25-bar take at revision 7 contains Record plus all six overdubs inside it.
4. Undo/redo are manifest selection; a commit after undo reclaims the redo branch's pages.
5. Compaction preserves revision and content for pinned and new readers; page conservation holds; superseded runs are
   reclaimed once no manifest references them.
6. Zero `operator new` calls after construction across record, overdub, commit, compose, undo, redo and compaction.

## Stage B findings so far

- **Equal-tick order in production is a class, not an ordinal.** Production orders equal ticks NoteOff < NoteOn <
  other (`LoopEventStore` canonical cursor; `NoteUtils::midiEventChronologicalLess`). Within a class it is *stable*
  in the `LoopEventStore` cursor but **unspecified** where `sortMidiEventsChronologically` uses `std::sort`. Parity
  for the differential test is therefore defined on (tick, class) plus whatever within-class order the chosen
  production oracle actually guarantees; task B.2a records that.
- Reader lifetime across a discarded redo branch, physical identity vs logical revision, latched capacity stop and
  non-contiguous pages are covered by native tests (17/17).

## Open questions (stage B and later)

- **Q1** Resolved by B.2a: parity on (tick, class) groups; production defines no within-class order (owner question: make
  append order the defined order?). [B.2a](evidence/2026-11-01-b2a-production-oracle.md)
- **Q1b** Resolved by DEC-245 B.3b: kept in source, suppressed by the explicit `productionCompat()` projection.
- **Q2** Overlap-resolve and ledger semantics expressed as hides + replacement rows; compare against
  `OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md` cases.
- **Q3** NOTE_EDIT / quantize as edit runs; whether that needs bar-sliced manifests (D8).
- **Q4** Resolved (B.4b): commit is sort-free (append-time sorted segments; 15 µs for 16,640 rows) and compaction is
  step-bounded (2,048-row step ≤ 5.4 ms on target, superseded when the prefix goes stale).
- **Q5** Resolved: `seq` is 32-bit; the row stays 16 B (the two reserved bytes went).
- **Q6** SysEx is explicitly unsupported in stage B; the row format is not bent around it.
- **Q7** Persistence comes last in stage B, after semantic and timing qualification: immutable pass records/pages, a
  manifest record, an atomic current-manifest commit marker. Crash consistency, garbage collection and persisted undo
  history get their own design.
- **Q8** Loop-length change and a new Record on a non-empty slot (prototype: the Record manifest replaces content and
  advances the revision; history keeps the old take for undo).

## C5 — one manifest-backed rolling-window reader (DEC-246 amendment 19)

Authorized by the owner on 2026-11-01. Status: **specified**; no firmware has been written.

### Seam: what changes, and where

- Production's committed rolling windows (active slot, `PlaybackWindowOrigin::Committed`) are filled today from the
  **published snapshot**. `RuntimePlaybackViewFill::begin(destination, LoopSnapshot::EventWindowCursor&, …)` runs
  with scope `SnapshotWindow`.
- Serving is gated on `loop.publishedSnapshotIsCurrent()` and `snapshotIdentity == publishedSnapshotIdentity()`
  (`serviceRollingPlaybackWindow`, `TrackPlaybackWindowBuild.cpp`). That gate is why the C4 runs kept showing
  `pending_publish=1` / snapshot not current.
- C5 replaces only the **content source and its currency test** for those windows:
  - The fill gains a source kind `ManifestWindow`. It drives the RevisionStore window job
    (`beginWindow(M, expectedIdentity, expectedRevision, from, to, staging, cap)` / `stepWindow(maxRows)`) in bounded
    quanta, converting each `PassEvent` into the view row.
  - Per row it writes:
    - the `MidiEvent`;
    - `phases[i] = tick`, so the order builder's default mode applies: phase, then NoteOff < NoteOn < other, then
      tick;
    - a rank equal to the window index, which keeps DEC-244's run-order and seq tie;
    - `passOrigins[i]` set to the row's source serial and ordinal. The edit bit is kept so an edit row is never
      treated as a live overlay (`kLiveOverlayPassOrigin` is never produced).
  - The view's source identity carries the **actual manifest identity and revision** the window read, and the frozen
    `ImportSourceKey` it was imported from. `snapshotIdentity` is `nullptr`.
  - The serving test becomes: the manifest identity equals the reader's current published identity, and the
    imported source key equals `importSourceKeyOf(loop)`. Both are metadata only, with no snapshot term.
- **Unchanged:** frame admission, epoch/generation/projection checks, cursor anchoring, the ledger, closure, the
  order builder, Capture/Preview origins, NOTE_EDIT preview windows, the Record-return window, and every write path.

### Owned states (no real-time fallback)

| State | Meaning | Reader action |
|---|---|---|
| `ImportOwed` | The source key moved, or no manifest is published for the selected slot | keep the previously qualified window if the playback contract allows; otherwise a named miss |
| `NoCommittedSource` | slot restore not applied yet (amendment 18) | named miss; the importer begins when the restore moves the key |
| `IndexOwed` | edit index for M not `Ready` | same as `ImportOwed`; never `Scan` |
| `WindowOwed` | the window for the interval is still preparing | existing lane behaviour for a preparing successor |
| `CapacityDenied` | window rows exceed the view, or `IndexCapacityDenied` (both slots pinned, no eviction) | named miss |
| `SourceMoved` | M changed during preparation | the candidate is discarded, never published |

Each state is reported through the existing `route_reject` / `win_*` telemetry with a new reason value. No path
reconstructs from the snapshot, and no path falls back to a content-sized legacy read on the real-time path.
The legacy snapshot fill remains callable off-edge **only** as the diagnostic oracle (`!REVSHADOW_WINDOWS`-style
compare).

### Ownership

`RevisionShadow` becomes the reader owner on the C5 image:
- it imports, indexes and serves `beginWindow` for the lane;
- the three registered idle children keep their 2 ms bounds;
- nothing runs from the PIT/ISR;
- window preparation stays on the lane's existing deferred producer turn.

### Slices

1. **C5.1 Consumer contract.** A native fixture fills one view from the snapshot cursor and one from the manifest
   window, for the same committed content with a current snapshot. It asserts equal emitted sequences (rows, order,
   closure) through the real order builder and cursor. It also pins which per-row fields the consumers read
   (`phases`, ranks, `passOrigins`, `ledgerBoundary`).
2. **C5.2 Fill source.** `RuntimePlaybackViewFill` `ManifestWindow` scope. Named refusals: capacity, `SourceMoved`,
   `IndexOwed`. The identity stamp is the actual one read. Native: an identity move mid-fill → no publish; both
   index slots pinned → `IndexCapacityDenied` with no eviction; an undo/redo or generation move cannot publish a
   stale candidate.
3. **C5.3 Lane switch.**
   - Committed windows of the active slot request manifest windows under a compile flag
     (`REVISION_MANIFEST_READER`), set only in the probe env.
   - The serving and currency gate becomes metadata-only.
   - Owed states are named in telemetry, and there is no legacy fallback on the real-time path.
   - Native: current-boot content, restored content (`NoCommittedSource` → `ImportOwed` → served), index owed →
     ready, zero `Scan`, zero snapshot-source reads on the serving path.
4. **C5.4 Device.**
   - Armed owner chain (current boot), plus a reboot with no stimulus (restored).
   - Oracle compare per window.
   - Every served window carries the exact identity it read.
   - MIDI timing: `tick_playback_miss` and route counts against the same chain on the probe image with the flag off.

### Open questions for the owner before C5.2

1. **Image scope.** C5 is behind `REVISION_MANIFEST_READER` in the probe env only; the default image keeps the
   snapshot fill until C6. Proposed: yes.
2. **Coexistence with `playback-tick-consume-only`.** That change is also active on this lane and file (phases 5–6
   change routing). Proposed: C5 touches only the fill source and the Committed currency term, and lands on this
   branch; the other change keeps the routing. Confirm the sequencing, or name the change that goes first.
