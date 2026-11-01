## Why

The overdub-audibility investigation (2026-11-01) ends in a revision that is committed but not servable: at
committed revision 7 the t7/s3 merged fallback built **0 events** and was stamped and reused as revision 7, while the
committed passes held 98–101. Each repair so far has added a lifecycle rule to the prepare → retain → freeze →
publish → adopt → retire chain, and each rule has to hold across a Record publication job, overlay readers, frozen
Record readers and window adoption. Separately, record admission prewarms ~1.79 MB per slot (10 capture stagings,
three playback views, bridges) before the first NoteOn, and the profiler attributes 45–90 ms edges to on-edge
construction and allocation.

The owner asked (2026-11-01) to investigate a different shape before adding more publication/fallback handling:
**fixed-capacity storage + immutable committed passes + tiny immutable revision manifests + derived caches**, with no
general-purpose allocation after boot. The property sought: *there is always a stable state* — every committed
revision is complete and immutable at the instant it commits, and no derived cache can define a revision's identity.

## What Changes

This is an **investigation with a host-only prototype**. Nothing is wired to firmware; no production owner changes.

- Stage A (this change's first slice): a dependency-free prototype `RevisionStore::Store` in
  `include/RevisionStore/RevisionStore.h` with a fixed event-page pool, pass runs (Record, overdub, compacted),
  revision manifests, single-writer append, infallible commit, manifest pinning, undo/redo as manifest selection,
  prefix compaction at equal revision, and window composition stamped from the manifest it read.
  Native suite `test_revision_manifest_store` proves the stage-A properties against a vector oracle, including zero
  heap allocations after construction.
- Stage B (gated on the owner reading stage A): composition parity against today's canonical playback order for the
  25-bar RED fixture (`44baf7be`), and the mapping of overlap-resolve onto runs.
- Stages C–E (each needs its own approval and a `DEC-###`): one playback reader from the manifest, recording onto the
  page pool, retirement of the old ownership machinery.

## Authority conflicts (named, not resolved here)

- [`Snapshots.md`](../../../docs/Authority/Architecture/Snapshots.md) and NAMING define `LoopSnapshot` as a complete
  effective-state object that is **not a parent-delta chain**, and require consumers never to reconstruct content from
  a parent. A manifest of pass runs *is* a delta chain whose composition is the effective state. Adopting it is a
  persistent-state-model change and an undo-semantics change — formal reassessment triggers. Stage A does not touch
  `LoopSnapshot`; adoption needs a `DEC-###` that amends Snapshots.md.
- It overlaps [`immutable-loop-snapshot-passes`](../immutable-loop-snapshot-passes/proposal.md) (complete snapshots,
  candidate construction, deferred publication). The prototype's claim is that publication is a manifest written at
  commit, so no construction job sits between commit and a servable revision.
- [DEC-076 — Playhead-scheduled MVCC](../../../docs/DECISION_LOG.md) is compatible in spirit (readers pin one
  generation); the difference is where materialisation happens (never on the correctness path).

## Non-goals

- No firmware build or device change; no change to `LoopEventStore`, `LoopSnapshot`, `Track` or the windowed lane.
- No storage schema change; no SD format.
- No decision on retiring `playback-tick-consume-only` or the overdub-audibility repair; they stay active.

## Impact

- New: `include/RevisionStore/RevisionStore.h`, `test/test_revision_manifest_store/`, this change folder.
- Native suite count rises by the new suite only.
