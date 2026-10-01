# S4c.0 — publication/consumer inventory and the shared work contract (input to sibling F)

**Status:** records the *existing* machinery. This change keeps S4c as **contract only**: it changes no publication
unit and ships no windowed publication (`IMPLEMENTATION_CONTRACT.md` §"… retains S4c only as contract/acceptance
definition"; `design.md` §"the sibling is planned but not created").
**Consumes:** `design.md` rows 12, 16, 28 (the decided rules), and the code cited below. **Feeds:** S4c.1, S4c.2,
S4c.3, S4c.4, S4c.5 and the S4c gate.

## 1. The publication unit and its owners

| element | where | what it is |
|---|---|---|
| the published root | `Loop::publishedSnapshot()` (`include/Loop.h`), `ImmutableLoopSnapshotRef` | an immutable snapshot the loop publishes; readers get a ref, not a mutation |
| the revision it is stamped with | `Loop::playbackRevision` | bumped by the inventory change; the currency unit every consumer compares against |
| "is the root current?" | `Loop::publishedSnapshotIsCurrent()` | the currency predicate |
| "is a publish owed?" | `Loop::hasPendingSnapshotPublish()` | the deferred-publish predicate (S4b.6/S4b.7 rely on it) |
| the candidate before publication | `Loop::unpublishedSnapshot()` | the in-flight candidate a publish would install |
| the publication itself | `storePublishedSnapshot(…)` at `src/Loop/LoopSnapshotPublish.cpp` (three call sites) | with three **coverage** outcomes in the call: `wrapTargetRevision`, `box.coveredPassRevision`, `kSnapshotCoverageCurrent` |
| a fresh publish | `Loop::storePublishedSnapshotForTest` | the existing test seam the S4b.7 fixtures use for a hand-published root |

**Consumers of the root** (all read-only; none may mutate it):

| consumer | entry point | what it reads the root for |
|---|---|---|
| playback window build | `Track/TrackPlaybackWindowBuild.cpp` (`ensurePlaybackMergedEventsForSlot`, `isPlaybackMergedMidiEventsReadyForSlot`) via `Loop::tryGatherPublishedSnapshotEventsInWindow` | the merged MIDI events the commit/readiness path needs (S4b.6/S4b.7) |
| display | `DisplayManager/DisplayNoteResolve*.cpp`, `DisplayColdHelpers.cpp`, `DisplayNoteWindowGather.cpp` (`tryGatherPublishedSnapshotEventsInWindow[WithCapture]`) | the window the display resolves/visual-caches |
| persistence | `StorageLoopIo.cpp`, `StorageManager/WorkspaceSave.cpp`, `LibraryObservationSession.cpp` | the body it serializes and the library metadata it reports |
| edit session | `EditManager/NoteEditSessionLifecycle.cpp`, `NoteEditDisplayProjection.cpp` | the committed baseline an edit session starts from |
| overdub lifecycle | `Track/TrackOverdubLifecycle.cpp` | the committed baseline a take is built on |
| undo/save checkpoints | `TrackUndo.cpp`, `Persistence*` | the root a checkpoint's parity is judged against (S4c.4) |

## 2. The resumable publication machinery the sibling must reuse (S4c.2)

The windowed/publication work must reuse what already resumes rather than inventing a second scheduler:

* **Body format versioning:** `StorageLoopIoBodyFormatV1.cpp` (`LoopSnapshotBodyFormatV1SaveScratch`,
  `beginLoopSnapshotBodyFormatV1SaveGather`, `stepLoopSnapshotBodyFormatV1SaveTail`) — the persisted body is written
  and parsed in bounded, resumable steps.
* **Resume arming:** `armLoopPersistTailWriteResume(persistKeyForLoop(loop.loopId))` in `WorkspaceSave.cpp`, i.e. a
  tail write that does not finish in one slice is *armed* and continued, with its progress stage named
  (`DeferredLoopWriteStage::LoopSnapshotTail`) — the same pattern the display window and the publication path are
  expected to reuse.
* **Sliced persistence with a budget:** `PersistenceSyncDrainBudget.cpp` (`buildSyncDrainBudget`,
  `syncDrainRestoreWaitName`, `classifySyncDrainVerdict`) charges restore *and* persistence work against one drain
  budget, so a large publish cannot starve an in-flight save.
* **Telemetry cadence:** `Utils/TelemetryCadence.h` (added 2026-10-01) — a diagnostic may not evict the evidence it
  explains; the sibling inherits this as a *rule*, not just a helper (see
  [`evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md`](evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md)).
* **Delivery discipline:** a row an acceptance surface fences on must not depend on bulk-retention policy for its
  delivery (`evidence/2026-10-01-ring-resync-and-receipt-delivery/FIX.md`) — the same trap awaits a windowed
  publication's readiness receipts.

## 3. The decided rules this contract must preserve (S4c.3 and design rows)

* **One pass late (decided, `design.md` row 12):** a missed deadline keeps the **previous version** authoritative for
  that pass — never torn, never held, never timing-deferred. This is a *requirement on the sibling*, not something
  this change implements.
* **No allocation and no blocking on a timing-critical edge (row 16):** S4c.0 records it here because a windowed
  publication is exactly the feature that would be tempted to break it.
* **Reuse before invention (row 28):** duplication is a recorded failure mode; §2 is the list to reuse.

## 4. The open questions this inventory hands to the sibling

1. **Interval/window readiness and immutable currency (S4c.1):** today currency is `publishedSnapshotIsCurrent()`
   against `playbackRevision`, and readiness for playback is the *merged window* (`isPlaybackMergedMidiEventsReadyForSlot`
   needs a non-empty windowed gather). Under a windowed `N`, what makes a window "ready" and "immutable" for a
   consumer — and for how long may the previous window stay authoritative? (The one-pass-late rule fixes the second
   half; the first is unspecified.)
2. **Length-independent root residency (S4c.5):** the window size must be *swept*, not hard-coded to bars
   (`design.md`), and `S2.3`'s `N` is provisional. What is the acceptance that a window is served without the whole
   root being resident?
3. **The stale-root/serialized-body divergence (S4c.4's neighbour, recorded elsewhere in this change):** the
   persisted body is serialized from `publishedSnapshot()` with no reconcile against the committed passes, and a
   restore installs that body as the root — so "checkpoint parity" must state *which* root a save/undo checkpoint
   represents and what happens when it lags the committed passes. This is a **known** open item, not a new finding:
   see the persisted-vs-playback note in `docs/Runtime/CURRENT_WORK.md` (the `content-verify-59` entry) and
   `design.md`'s root-pinning section.
4. **Fixture inventory (S4c.5):** content parity for a demoted/re-activated slot is already owed by the **S5 gates**
   line — "content parity for a demoted/re-activated slot (existing content-verification pipeline)" — and the
   immutable *window* fixtures are new. The sibling should extend that pipeline, not duplicate it.

## 5. What this change must not touch

* The publication **unit** (`publishedSnapshot()` and its `ImmutableLoopSnapshotRef`), the coverage vocabulary, and
  the revision/epoch identity: unchanged here (`S4c.1`'s "do not change the publication unit in this change").
* No playback-root dependency may be removed here (S4c.4), even where the sibling's windowing will later relax it.
