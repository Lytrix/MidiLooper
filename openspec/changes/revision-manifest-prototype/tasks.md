# Tasks — revision manifests over fixed event pages (investigation)

Commit boundary: `feature/revision-manifest-prototype` (host-tooling/prototype; never `dev`), no PR unless requested.
Stage B is qualification only (owner, 2026-11-01). Stage C is authorized by DEC-246 (one read path; writes stay with
today's firmware). Stages D–E are not authorized; each needs its own approval and a `DEC-###`.

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

## C. One authoritative read path (DEC-246; writes stay with today's firmware)

- [x] C0 DEC-246, `CURRENT_WORK` scope, these tasks.
- [x] C1 Import committed `Loop::passes` into a disposable manifest (`RevisionStore::importLoop`): capture runs in
      commit order with production-paired NoteOff ids, one Edit run per import (hide + final geometry via production's
      `applyNoteEditPass`), named refusals with no partial mirror. Read the real-time way (edit index, `IndexOnly`,
      `productionCompat()`), it equals production's effective view on **659 / 659** states (B.2a, B.2b seeded, B.3,
      B.3 seeded). Found: production's cross-pass collapse is between capture passes only — projection fixed.
      [Evidence](evidence/2026-11-01-c1-import.md)
- [x] C1b C1 follow-ups: collapsed Edit run documented as shadow-only; production collapse provenance matrix measured
      (capture+capture collapse, capture+edit both kept, edit+edit both kept) and reproduced; the edit-source bit now
      survives compaction (per-origin kind). [Evidence](evidence/2026-11-01-c2a-index-lifetime.md)
- [x] C2a Index lifetime and capacity: reader pins per manifest; slots `Empty`/`Building(identity)`/`Ready(identity)`;
      reclaim only unpinned `Ready`; `requestIndex` → `IndexCapacityDenied` instead of eviction; a build publishes the
      identity it started on, never superseded except by explicit cancel. RED (pinned index evicted) → GREEN.
      [Evidence](evidence/2026-11-01-c2a-index-lifetime.md)
- [x] C2b Bounded worker on device: registry child `RevisionEditIndexGrain`, one call site; **device-qualified**
      under real hides (occupy-paint): max whole call 49–76 µs at 256 rows (declared 2 ms), indexes published for the
      exact manifest identity, `IndexOnly` reader Ready. [Evidence](evidence/2026-11-01-c3b-transactional-import.md)
- [x] C3a Stopped-only shadow import of the selected slot on frozen source identity. Device, owner chain: shadow
      `revision()` read = production's passes view **1,042 / 1,042**. Findings: import is a 136–162 ms unit (C3b
      stepwise required); restored slots have no passes (`NoRecordPass`); production's collapse is a snapshot
      full-rebuild artefact (passes view and incremental snapshot keep all rows); production snapshot stayed
      un-current after the chain. [Evidence](evidence/2026-11-01-c2b-c3a-device-shadow.md)
- [x] C3b Bounded transactional import: frozen `ImportSourceKey`, private staged candidate, atomic publish,
      `SourceMoved` cancel, up-front refusal (pages, runs, manifests), own child `RevisionShadowImportGrain`; no
      whole-pass copy/pairing/scan; restored slots `NoCommittedSource`. Native 662/662 parity through the job; device
      import step max 683 µs at 64 rows; shadow `revision()` = production passes view **54/54** under real hides.
      Found: production's snapshot (62) and passes view (54) disagree after overlap Shortens + undo/redo.
      [Evidence](evidence/2026-11-01-c3b-transactional-import.md)
- [x] C3c Restored slots: **no new source needed**. Boot restore installs the persisted capture and edit passes into
      `loop.passes`, so the importer's live source is the persisted-pass source once the restore applies. Before
      that, the result is `NoCommittedSource`; a snapshot is never imported. Host round trip (save → restore →
      import, with Hide and Shorten edits) equals the pre-save import and the restored passes view. Device reboot:
      restored t8/s4 imported 1,210 / 1,210, 10/10 windows, `c4_clean=1`. DEC-246 amendment 18 (finding, for the owner
      to confirm). [Evidence](evidence/2026-11-01-c3c-restored-slots.md)
- [x] C3d Shadow window producer: store `beginWindow`/`stepWindow` (`Ready` with actual identity, `IndexOwed`,
      `ImportOwed`, `CapacityDenied`, `SourceMoved`; no build, no scan; honest finish); firmware child
      `RevisionShadowWindowGrain` (2 ms / 0.5 ms), each window produced once per manifest identity, digests in PSRAM.
      [Evidence](evidence/2026-11-01-c3d-c4-window-producer.md)
- [x] C4 Device shadow compare against the **passes-derived `revision()` surface** (DEC-246 amendments 9, 14). Two
      coverage claims, recorded separately:
  - [x] **C4 content** (owner-chain scale and windowing): armed owner chain, Record and overdubs both `16ths.mid`,
        take at tick 0, the whole file sent. Whole loop **1,210 = 1,210**, **10/10** windows, `c4_clean=1`, import max
        209 µs, window max ≤ 241 µs, 0 duplicate publications. This run had **no companion overlays**: production
        skipped all 12. [Evidence](evidence/2026-11-01-c4-closeout.md)
  - [x] **C4 hide/index** (Hide + `EditIndex` read `IndexOnly`): C3b real-hide run, **54/54**, `IndexOnly` used a
        published index. Qualification: that preset's own verdict was FAIL (amendment 10); the owner counts its parity
        (amendment 14). [Evidence](evidence/2026-11-01-c3b-transactional-import.md)
  - History: the first hide run was not clean (2 NoteOffs misattributed, a 3,212 µs step)
    ([evidence](evidence/2026-11-01-c4-hides-not-clean.md)); window producer
    ([evidence](evidence/2026-11-01-c3d-c4-window-producer.md)).
- [x] C4a NoteOff attribution equal to production's; bounded seal; whole-loop comparator.
  - [x] `LanePairer` = production's tick-ordered lane walk + unique-earlier wrap NoteOff, shared by importer
        (`PairWalk`/`PairWrap`) and host oracle; wrap-overlap repro RED → GREEN; zero-length suppression no longer
        needs a stamped id; 664 parity checks.
  - [x] Bounded seal (`stepSealStagedRun`, resumable heapsort); no per-edit allocation in `EditApply`;
        per-phase import timing (`import_phases`).
  - [x] Whole-loop comparison in `!REVSHADOW_WINDOWS` (`windows_whole_loop`, `c4_clean`).
        [Evidence](evidence/2026-11-01-c4a-pairing-and-whole-loop.md)
- [x] C4b Rig and importer fixes.
  - [x] The midifile chain recorded the take `startLoopTick mod 48` off the grid. It now arms Record with the clock
        stopped, waits for `armed, waiting for clock`, then sends `Start`; the take starts at tick 0 and the file is
        streamed from event zero. The run fails by name otherwise.
  - [x] Importer crash (null read in `stepCaptures`: pointers into production pass vectors) fixed with ids resolved
        per step; regression fixture "same source identity, backing container relocates".
        [Evidence](evidence/2026-11-01-c4b-hitl-alignment-and-import-crash.md)
- [ ] Residuals, not blocking C3c:
  - The 3,212 µs import step seen once with 33 edits is unattributed (`import_phases` will name it). Owed on an
    edit-carrying source before C5.
  - Smaller shadow footprint for long uptimes.
  - HITL validity on the default image.
- Out of scope here (DEC-246 amendment 16): production overdub companion loss ("companion path skipped (canonical
  derivation unavailable); raw pass only"), to go to its own change.
- [ ] C5 One manifest-backed rolling-window reader (DEC-246 amendment 19; authorized 2026-11-01). Seam and G2 packet:
      [design.md § C5](design.md). The committed lane's **snapshot-window fill** becomes a manifest-window fill with a
      metadata-only currency test; owed states are named and there is no real-time fallback.
  - **Unblocked (DEC-246 amendment 21):** prerequisite is only the landed internal-clock consume-only invariant
    (DEC-241, on this branch). The tick never imports, indexes or builds a window. Probe env only, A/B flag 0 / 1;
    with flag 0 the integration allocates nothing. After C5.3, the overdub-audibility RED is the A/B discriminator.
  - [ ] C5.1 consumer contract fixture: the full tuple (`MidiEvent`, phase, rank, `passOrigin`, ledger/boundary)
        through the real order builder equals the snapshot fill; a manifest `passOrigin` never collides with the
        live-overlay origin
  - [ ] C5.2 `ManifestWindow` fill source with named refusals and the exact identity stamp
  - [ ] C5.3 lane switch behind `REVISION_MANIFEST_READER` (probe env)
  - [ ] C5.4 device A/B (probe flag 0 / 1, same stimulus): current-boot owner chain + reboot-restored; timing
- [ ] C6 Remove the corresponding old reader after device parity.

## D–E. Production stages (not authorized)

- [ ] D Recording onto the page pool; retire the per-slot 1.79 MB record prewarm.
- [ ] E Retire frozen Record readers, overlay readers and redundant publication/adoption state.
