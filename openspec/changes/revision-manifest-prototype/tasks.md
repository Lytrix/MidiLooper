# Tasks — revision manifests over fixed event pages (investigation)

Commit boundary: `feature/revision-manifest-prototype` (host-tooling/prototype; never `dev`), no PR unless requested.
Stage B is qualification only (owner, 2026-11-01). Stages C–E are not authorized; each needs its own approval and a
`DEC-###` (amending `Snapshots.md` only if the manifest takes over the snapshot's semantic role).

## A. Prove the data model (host-only)

- [x] A.1 Prototype `RevisionStore::Store` (`include/RevisionStore/RevisionStore.h`): fixed page pool, pass runs,
      manifests, single writer, infallible commit, pinning, undo/redo selection, prefix compaction, stamped compose.
- [x] A.2 Native suite `test_revision_manifest_store` against a vector oracle: design.md evidence items 1–6, with a
      heap-counter self-check so the zero-allocation result is not vacuous.
- [x] A.3 Owner review of stage A (2026-11-01): proceed to stage B as qualification only; manifest framed as revision
      index with `LoopSnapshot` as derived cache (no Snapshots.md amendment owed yet).

## B. Qualification (host + Teensy measurement; no live wiring)

Question: can `RevisionManifest` + immutable passes reproduce the current authoritative semantics and fit the Teensy
timing and memory envelope without introducing real-time construction?

- [x] B.1 Prototype gates from the owner review: 32-bit `seq` (row stays 16 B); production equal-tick class key;
      physical manifest `identity` separate from logical `revision`; latched capacity stop on append; tests for a
      reader holding a discarded redo branch, identity across compaction and slot reuse, equal-tick class across
      runs, and non-contiguous pages. Native suite 17/17.
- [x] B.2a Production oracle named: the published `LoopSnapshot` edit view (`tryCopyCurrentSnapshotEditViewEvents`);
      the store is fed each committed pass's chunk rows; parity is on (tick, class) groups compared as multisets.
      Findings: four production surfaces use four equal-tick keys; same-tick rows identical up to data2 (sustain
      127/0) have no defined order in the oracle; span-less (zero-length) notes are dropped by the effective view
      (named divergence, pinned). Suite `test_revision_store_differential` 3 pass + 1 ignored.
      [Evidence](evidence/2026-11-01-b2a-production-oracle.md)
- [x] B.2b Seeded differential sequences under DEC-244 (owner, 2026-11-01: append order is the defined order): 40 seeds
      × 12 steps, 520 revisions incl. 75 undos, 22 redos and branch-after-undo. Store identical to DEC-244 on every
      revision; production identical in content once its cross-pass collapse is modelled. Findings: production reverses
      a same-tick sustain release (`CC64 127, 0` → `0, 127`, pedal left down) in its effective view; production
      collapses identical events across passes (`midiEventMatchesSnapshot`). Live overdub sessions with pending note
      changes and NOTE_EDIT move to B.3. [Evidence](evidence/2026-11-01-b2b-seeded-differential.md)
- [x] B.2c The 25-bar RED fixture (`44baf7be`) through the store: at revision 5 every two-bar window holds every
      committed row (60) and all four overdubs; production's replay sounds 1 of 4 while its own content authority holds
      all 60 rows and 4 overdubs after the replay — a serving defect, not content (caveat: authority read after the
      replay). [Evidence](evidence/2026-11-01-b2c-red-fixture.md)
- [x] B.3 Overlap-resolve and NOTE_EDIT parity by output: `Hide` / `Shorten` companions (incl. wrap, second-overdub
      target, channel 3) and NOTE_EDIT pitch / length / velocity / delete, each with undo, are represented as HideNote +
      replacement rows in a later run (new `PassKind::Edit`); seeded mix 132/132 revisions in parity (61 overlap changes,
      43 edits, 10 undos). Writer requirement: NoteOff rows carry their note's id. Open: span-less notes (Q1b), the
      cross-pass collapse decision, live `Track` overlap-resolve, Tick moves, CC edits, redo of edits.
      [Evidence](evidence/2026-11-01-b3-overlap-and-edits.md)
- [x] B.4 Teensy cost probe (env `teensy41-capture-serial-revstore-probe`, RAM1-neutral, flash-resident code, PSRAM
      arena; HEX `e5db1dfc…`, boot verified). Manifest commit < 1 µs, undo/redo ≤ 2 µs, begin ≤ 95 µs, append < 1 µs avg,
      2-bar compose 2.8 ms max at 10 runs (1.4 ms compacted). **Not fitting:** seal is a content-sized sort (134 ms at
      16,640 rows) and compaction is synchronous (67 ms for 25k rows). Next design step: sort-free seal (two sorted
      segments around the capture wrap) and step-bounded compaction (~2,900 rows per 8 ms grain).
      [Evidence](evidence/2026-11-01-b4-teensy-timing.md)
- [x] B.4b Seal and compaction fixed and re-measured (HEX `5ad8e493…`, boot verified): sort-free commit (append-time
      sorted segments; 16,640-row Record commits in 15 µs, `slow_seals=0`), step-bounded compaction (2,048-row step
      5.4 ms max, superseded when undo or a different history makes the prefix stale), construction 0.25 ms,
      `beginPass` ≤ 22 µs. Compose at 10 runs rises to 3.3 ms max (two segments per run). Native store suites 39 + 1
      ignored. [Evidence](evidence/2026-11-01-b4-teensy-timing.md#b4b--after-the-fixes-same-day-sort-free-commit-step-bounded-compaction)
- [x] B.5 Capacity budget. Device (HEX `9be5e197…`, boot verified): 8 MB PSRAM, pool used 4.93 MB / free 3.44 MB with
      slots loaded (event chunks only 0.67 MB of it; ~1 MiB capture ring, ~1.9 MB boot-reserved views). Native
      worst-case slot (dense 64-bar take, 30 overdubs, undo branches, pinned reader): peak 703 pages without compaction
      (1.08× content) vs 1.8–2.2k pages when compaction includes the Record and ~1.3k when it takes overdubs only —
      compaction duplicates whatever history still references. Range compaction and history-wide rewrite added
      (content hash-identical under every policy). Budget ≈ 2.76 MiB store-wide at today's 131,072-event ceiling,
      about the same as today's 2.67 MB of chunk backing. [Evidence](evidence/2026-11-01-b5-capacity-budget.md)
- [x] B.5a Store changes: history-closed compaction by default (`NotHistoryClosed`; publish rewrites every holder),
      heap-merge compose with cached keys and hide-run skipping (store-owned scratch, not stack), paged run
      directories (descriptor 76 B), shared `PagePool` across slot stores. Capacity: history-closed peak 1.12–1.23×
      content (vs 2–3.4× with copies); device (HEX `8f70c93a…`, boot verified): 2-bar compose at 31 runs 5.45 ms max,
      compaction step 4.16 ms, commit 10 µs. [Evidence](evidence/2026-11-01-b5a-store-changes.md)
- [x] B.5c DEC-245 recorded (lossless committed source, explicit projections, compaction changes representation
      only). Compaction made lossless (origin + serial provenance, origin-aware hiding); `EffectivePolicy`
      (revision / source / productionCompat); `planCompaction` in the store; zero-length divergence now parity under
      `productionCompat`, and that projection equals production on all 520 seeded revisions. Device (HEX `2db2116a…`):
      31-run compat 5.26 ms, dense burst 7.85 ms, **hide-heavy 31 runs 30.7 ms (over budget)**.
      [Evidence](evidence/2026-11-01-b5c-lossless-projections-planner.md)
- [x] B.5d Hides as scalable source operations + manifest-qualified edit index (owner scope 2026-11-01): page-listed
      hides (no 256 cap), target = stable `noteId` lineage, explicit precedence (latest hide position strictly later
      than the row hides it), `EditIndex` keyed by manifest identity with two LRU slots, bounded `stepEditIndex`
      worker, `HideLookup::IndexOnly` returns `IndexOwed` (never builds), `Scan` kept as oracle. Device
      (HEX `365b6d11…`): hide-heavy 31-run window **30.8 → 5.56 ms**, index build steps ≤ 1.12 ms.
      [Evidence](evidence/2026-11-01-b5d-edit-index.md)
- [x] B.5b Session high-water on device (probe HEX `8f70c93a…`, fresh boot verified, owner rig: 20-bar Record + 12
      overdubs + internal-clock hold; `session_20261101_170808.log`): firmware alone peaks at **7.15 MB of 8.38 MB**,
      1.33 MB free after stop; event chunks are 1.0 MB of it. The store cannot sit beside today's firmware at its
      2.76 MiB ceiling budget: stage C must size the pool to present content, stage D must retire chunk backing as it
      lands, stage E returns most memory. [Evidence](evidence/2026-11-01-b5b-session-high-water.md)
- [ ] B.6 Persistence design (after B.2–B.5 are green): pass records, manifest record, atomic current-manifest
      marker, crash consistency, garbage collection, persisted undo history.

## C–E. Production stages (not authorized)

- [ ] C Merged fallback derives from the manifest (one reader).
- [ ] D Recording onto the page pool; retire the per-slot 1.79 MB record prewarm.
- [ ] E Retire frozen Record readers, overlay readers and redundant publication/adoption state.
