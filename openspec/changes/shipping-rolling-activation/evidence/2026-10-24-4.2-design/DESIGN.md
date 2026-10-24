# 4.2 — save checkpoint parity: content oracle and repair design (2026-10-24)

**Task:** F-6 (checkpoint parity, save), transferred from `windowed-playback-generation` task 4.2.
**Status:** design only; no firmware line changed. This document fixes the oracle and the repair choice.

## 1. The claim under test

A persisted body is written by `writeLoopPersisted` from a settled loop and installed by
`applyLoadedLoopSlotBodyCommit`. Today the restore edge installs the persisted root with
`storePublishedSnapshot(…, kSnapshotCoverageCurrent)`: currency is claimed on the **writer's** admission, and the body
does not record the root's coverage. A body whose root and passes cannot come from one committed state is therefore
accepted and presented as a **named-current** checkpoint (`publishedSnapshotIdentity()` non-null).

The cheap cross-check (`nextNoteId`) is **refuted by reading the code**: `EditSelectNoteState` reaches
`Loop::allocateNoteId()`, so the counter can advance while a settled root's committed MIDI content is unchanged. The
oracle must therefore compare **content**, not the counter.

## 2. Divergence definition

Let `R` be the root stored in the body and `P` the passes stored in the body. The body is coherent iff, for `R`'s
coverage, the root's event set equals the effective committed content derived from `P` (same ticks, note identities,
On/Off, and same closure membership), and `R`'s recorded coverage revision equals `P`'s committed-pass revision. A
**content-divergent** body is one where the event sets differ (e.g. a note's start/end/velocity/pitch changed, a pair
removed or added) while the metadata remains plausible.

This is the shape the task's falsification clause names: the body is self-consistent enough to be read, but not one
committed state.

## 3. Oracle (registered native fixture)

Drive the **production writer** and the **production restore edge**; do not construct the root by hand.

1. **Settled writer leg.** Commit a pass with a known pair inventory; publish the root; `writeLoopPersisted` to the
   storage harness; re-read the body and assert the writer's own round trip is `Current` (this is the 2026-10-02
   verification half, retained).
2. **Divergent body.** In the serialized body (not in a live loop), change **committed MIDI content** of the root — one
   note's On/Off tick and velocity (or pitch) — while leaving the passes and metadata otherwise plausible and the
   `nextNoteId` figure untouched. This is deliberately not the counter probe.
3. **Restore.** `applyLoadedLoopSlotBodyCommit` the mutated body. The RED is the current behavior: the persisted root
   is installed and `publishedSnapshotIdentity()` is non-null — claims current although the root cannot be derived
   from the loaded passes.
4. **Post-repair assertion.** The restore's outcome is `CheckpointParity::Current` **only** when the content comparison
   holds; otherwise it is named `OwesReconciliation` (or the rebuild completes and the installed root is the
   passes-derived root, `Current`), and the diverged root is never served as current.
5. **Control.** A coherent body (the writer's own round trip) still restores `Current`; a body with a root whose
   coverage is unset still names `CoverageUnrecorded` (DEC-073), and a missing root still names `NoCheckpoint`.
6. **Mutation teeth.** Falsify the comparison (return equal unconditionally) → the divergent case fails; falsify the
   rebuild (install the persisted root anyway) → the "never served as current" clause fails. Restore byte-identically.

The oracle uses the existing content pipeline vocabulary: `Track::playbackWindowEventAtForSlot` /
`copyEffectiveCommittedEventsInRange` for the root's rows, and the loaded passes' effective content for the other
side. It does not add a second content reader.

## 4. Repair options

### A. Persisted provenance (format change)

Add the root's coverage (revision + a content identity or hash) to the LSV1 body. On load, compare the field to the
loaded passes' revision/content; mismatch → `OwesReconciliation`. **Cost:** LSV1 v1 is pinned; a field/version change
needs SD compatibility, an upgrade path and rollback. **Limitation:** provenance proves what the writer *claimed*, not
that the root matches the passes; a still-wrong body with a matching stamp would pass. It is a fast-path check, not
the oracle.

### B. Bounded load reconciliation (no format change)

On load, compare the persisted root's content to the effective content of the loaded passes for the root's coverage:
- equal → install `Current` (the writer's own admission is confirmed by content);
- unequal → do **not** install the persisted root as current. Name `OwesReconciliation` and rebuild the root from the
  loaded passes (the existing snapshot construction), installing the rebuilt root with the loaded passes' coverage;
  if the rebuild cannot complete within the load job's budget, keep no current root and name stale; never serve the
  diverged root.
- **Cost:** the comparison is one pass enumeration over the loaded content, charged to the existing load job (the
  loop is already being loaded; the root's rows are bounded by the body). If the body is large, use the existing
  page/cursor continuation and a bounded step per load turn; the job already owns progress. No format change, no new
  persistence, rollback is the prior HEX.

### C. A + B (provenance fast path, reconciliation fallback)

Only justified if the content comparison measures too expensive on device; A alone is not a correctness proof.

## 5. Recommendation

**B — bounded load reconciliation.** It is the only option that proves the property the task states (the persisted body
is equivalent to the root it was serialized from), needs no LSV1 format/version change, and keeps the existing
load-job bound. The comparison is over content the load path already has; the failure outcome is named, never a false
current. A provenance field can be added later as a fast path **on top of** B (option C), not as its replacement.

## 6. Interaction and failure rules

- **Undo:** the parity answer is the same vocabulary; the undo path restores the entry's root exactly (4.1) and never
  consults the window's lag. The load reconciliation is at the save/restore edge only.
- **Persistence:** no new format field; the body remains LSV1 v1. The load job's content comparison and optional
  rebuild are charged to its existing per-turn budget; no synchronous work on a timing edge.
- **Failure:** a divergent body does not crash, truncate or silently repair to the diverged root; it names the outcome
  and either rebuilds from the passes or leaves no current until the rebuild completes. A persistent inability to
  rebuild is reported once, retaining the prior valid root where one exists.
- **Rollback:** the change is behavioral only; the prior image handles the same LSV1 bodies (the body format is
  unchanged).
- **RAM1/PSRAM:** no new persistent state; the comparison/rebuild uses the load path's existing scratch. The rebuild's
  root is the same representation the writer would produce; no whole-take reservation per slot.

## 7. Acceptance and next slice

- Native: the oracle in §3 (divergent body RED → named/rebuild GREEN, coherent control, unset-coverage and no-root
  controls, two mutations restored).
- Device: load a diverged body from SD on the qualification image; assert the named outcome and that the served
  content is the passes-derived content (or no current), never the diverged root; the persisted body remains readable.
- Then: reclassify `checkpointParity`'s consumer (the load path is the first production consumer; the undo path keeps
  its exact-root restore) and record the outcome in the task ledger.

## 8. Not in this design

- A new persistence format, a second content authority, or a second reader.
- Device/perf numbers (the load job's budget is the existing bound; a device measurement is the implementation slice).
- The `nextNoteId` counter as a parity signal.
