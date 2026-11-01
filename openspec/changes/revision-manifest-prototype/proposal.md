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
- Stage B (owner-approved 2026-11-01, qualification only): differential parity against the production model,
  Teensy cost measurements with the runtime profiler, and an exact capacity budget; see `tasks.md` § B.
- Stages C–E (each needs its own approval and a `DEC-###`): one playback reader from the manifest, recording onto the
  page pool, retirement of the old ownership machinery.

## Authority framing (owner review, 2026-11-01)

The manifest does **not** have to become a `LoopSnapshot`, so DEC-076 and
[`Snapshots.md`](../../../docs/Authority/Architecture/Snapshots.md) are not overturned by this change:

```
immutable Record / overdub / edit passes   authoritative history
          ↓
RevisionManifest                           authoritative revision index (complete pass list of revision N)
          ↓
materialised LoopSnapshot / root           optional derived acceleration cache
          ↓
rolling playback windows                   real-time serving format
```

A manifest is a complete immutable descriptor of revision N; no reader chases a parent to establish identity. That
differs from the parent-delta chain Snapshots.md rules out. Snapshots.md is rewritten only if stage B shows the
manifest should replace the *semantic* role of `LoopSnapshot`.

It still overlaps [`immutable-loop-snapshot-passes`](../immutable-loop-snapshot-passes/proposal.md) (deferred
snapshot publication): under this framing a snapshot that is late is a cache miss, not a missing revision.

## Stage B question

> Can `RevisionManifest` + immutable passes reproduce the current authoritative semantics and fit the Teensy timing and
> memory envelope without introducing real-time construction?

Stage B is qualification, not migration. It is **not** "how do we port the firmware to `RevisionStore`".

## Non-goals

- No firmware build or device change; no change to `LoopEventStore`, `LoopSnapshot`, `Track` or the windowed lane.
- No storage schema change; no SD format.
- No decision on retiring `playback-tick-consume-only` or the overdub-audibility repair; they stay active.

## Impact

- New: `include/RevisionStore/RevisionStore.h`, `test/test_revision_manifest_store/`, this change folder.
- Native suite count rises by the new suite only.
