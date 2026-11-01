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
- [ ] B.2a Name the production oracle for "the effective ordered MIDI stream of revision N" (candidates: the published
      `LoopSnapshot` event index; the `LoopEventStore` private canonical order over a pass's chunks; `Track`
      `mergedEvents` / playback order) and record which within-class order it guarantees.
- [ ] B.2b Differential harness: one generated operation sequence (Record, overdub, overlap, undo, redo, commit after
      undo, wrap-crossing notes, NOTE_EDIT where reachable) applied to the production model and to `RevisionStore`;
      compare every revision's ordered effective stream, a set of playback windows, undo/redo results and
      branch-after-undo. Randomized, seeded, with shrinking to the first divergent revision.
- [ ] B.2c Feed the 25-bar RED fixture (`44baf7be`) through the harness; the store's window at the newest revision
      must contain all four overdubs.
- [ ] B.3 Overlap-resolve and NOTE_EDIT parity **by output**: whichever representation (hides + replacement rows,
      immutable edit pass, compaction) reproduces the production stream; decided by B.2b divergences, not by fitting
      today's implementation.
- [ ] B.4 Teensy cost experiment in `teensy41-capture-serial-profile`, store not wired to playback: begin pass, append,
      seal, manifest commit, window compose, undo/redo selection, compaction grain, on the densest captured take.
      Any content-sized step (seal sort, compaction) must be shown boundable off Record/Stop and off the PIT path.
- [ ] B.5 Capacity budget (design.md D7) with exact figures, including the compaction peak and pinned discarded
      branches.
- [ ] B.6 Persistence design (after B.2–B.5 are green): pass records, manifest record, atomic current-manifest
      marker, crash consistency, garbage collection, persisted undo history.

## C–E. Production stages (not authorized)

- [ ] C Merged fallback derives from the manifest (one reader).
- [ ] D Recording onto the page pool; retire the per-slot 1.79 MB record prewarm.
- [ ] E Retire frozen Record readers, overlay readers and redundant publication/adoption state.
