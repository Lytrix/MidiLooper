# Tasks — revision manifests over fixed event pages (investigation)

Commit boundary: `feature/revision-manifest-prototype` (host-tooling/prototype; never `dev`), no PR unless requested.
Stages B–E are **blocked** until the owner reads the stage-A evidence; C–E each need a `DEC-###` amending
`Snapshots.md` before production code.

## A. Prove the data model (host-only)

- [x] A.1 Prototype `RevisionStore::Store` (`include/RevisionStore/RevisionStore.h`): fixed page pool, pass runs,
      manifests, single writer, infallible commit, pinning, undo/redo selection, prefix compaction, stamped compose.
- [x] A.2 Native suite `test_revision_manifest_store` against a vector oracle: design.md evidence items 1–6, with a
      heap-counter self-check so the zero-allocation result is not vacuous.
- [ ] A.3 Owner review of stage A; decide whether stage B proceeds and which open questions (design.md Q1–Q8) gate it.

## B. Windows from a manifest (host-only)

- [ ] B.1 Feed the 25-bar RED fixture (`44baf7be`, four overdubs pending behind the Record publication) through the
      prototype; the window at the newest revision must contain all four overdubs.
- [ ] B.2 Composition parity with today's canonical playback order (Q1), same fixture.
- [ ] B.3 Overlap-resolve cases expressed as hides + replacement rows (Q2).
- [ ] B.4 Target sizing measurement: densest captured take → rows, pages, runs (D7); seal/compose cost on Teensy (Q4).

## C–E. Production stages (not authorized)

- [ ] C Merged fallback derives from the manifest (one reader).
- [ ] D Recording onto the page pool; retire the per-slot 1.79 MB record prewarm.
- [ ] E Retire frozen Record readers, overlay readers and redundant publication/adoption state.
