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
- **D4 — Hides are whole-note tombstones by `noteId`**, applying to earlier runs only. This is a hook, not the
  overlap-resolve model: trims and shortened notes need replacement rows (an edit run) — stage B question Q2.
- **D5 — Compaction is prefix-only and revision-preserving.** Merging runs `[0, k)` applies and drops the hides inside
  the prefix (they can only target rows inside it). The compacted manifest replaces the history entry at the cursor;
  pinned readers keep the old manifest until they unpin. Overdub admission is denied at `kRunsPerManifest` until
  compaction runs — that is the back-pressure, and it is off the commit path.
- **D6 — Reclamation by reference count**: manifests count history entries and pins; runs count manifests; a run's
  pages return to the free stack when its count reaches zero. Single-core, main-loop only; the ISR never touches
  the store (it consumes published windows — stage C).
- **D7 — Sizing is a budget, not an estimate.** Rows are 16 B. The 4 MiB figure (1 KB pages × 4096) is a placeholder
  until task B.5 fills:

  ```
  PSRAM page pool
  − worst simultaneous immutable history (all slots × history depth, shared runs counted once)
  − active writers and their reservations
  − manifests held by readers (incl. discarded redo branches still pinned)
  − compaction peak: old and new representations coexisting
  − playback / display candidates built from manifests
  − existing non-store PSRAM obligations
  = guaranteed reserve  (must be ≥ 0 at every admission)
  ```

  A design that normally uses 4 MiB but needs 7 MiB at compaction peak has a different admission problem.
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

- **Q1** Resolved for the class key (above); open for within-class order until the production oracle is named (B.2a).
- **Q2** Overlap-resolve and ledger semantics expressed as hides + replacement rows; compare against
  `OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md` cases.
- **Q3** NOTE_EDIT / quantize as edit runs; whether that needs bar-sliced manifests (D8).
- **Q4** Step-bounded compaction and seal (heapsort of a 3,000-row run is a main-loop cost to measure on target).
- **Q5** Resolved: `seq` is 32-bit; the row stays 16 B (the two reserved bytes went).
- **Q6** SysEx is explicitly unsupported in stage B; the row format is not bent around it.
- **Q7** Persistence comes last in stage B, after semantic and timing qualification: immutable pass records/pages, a
  manifest record, an atomic current-manifest commit marker. Crash consistency, garbage collection and persisted undo
  history get their own design.
- **Q8** Loop-length change and a new Record on a non-empty slot (prototype: the Record manifest replaces content and
  advances the revision; history keeps the old take for undo).
