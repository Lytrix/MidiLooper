# Slice 3.14c — G0 investigation: the publication-owner request exists, but a republish cannot represent a note-edit undo

**Change:** `windowed-playback-generation` · **Task:** 3.14 (the publication-owner request) ·
**Date:** 2026-10-20 · **Branch:** `feature/unified-rolling-playback-path` · **Baseline:** `1372953c`
**Outcome:** **G0 BLOCKED on a design fork.** No firmware change landed; the tree is unchanged. The naively correct
change was implemented and measured, and the owner refused to publish it for a reason the task text did not name.

## Scope traced

| # | Trace | Result |
|---|---|---|
| 1 | Entry points | `TrackUndo::undoForLoop` / `redoForLoop` (`src/TrackUndo.cpp:991`, `:…`), reached from the play/stop and edit gestures |
| 2 | Call paths | case body (disable pass, `setEditPassState`, `refreshPlaybackAfter*PassStateChange`, `refreshVisualCacheAfterPassStateChange`, `refreshNoteEditSessionAfterPassStateChange`) → `applyPublishedSnapshotSelect` (`:153`) → `requestCommittedPassesRepublish` (only two kinds today) |
| 3 | State transitions | `playbackRevision` advances on every enumerable inventory change; `committedPassRevision()` **is** `playbackRevision` (`src/Loop/LoopSnapshotLifetime.cpp:61`) |
| 4 | Owners | Loop owns pass content + the published root; `Loop::stepPendingSnapshotPublish` is the only publisher; `Track` owns the lane and the ledger |
| 5 | Lifetimes | The previous root stays selected and audible until a replacement publishes; the request box is per-loop |
| 6 | Deferred continuations | `Track::processSnapshotOwnerTurn` → `loopWithPendingSnapshotPublish()` ranks the active slot first and services any slot of the track |
| 7 | Publication/handoff | `requestCommittedPassesRepublish` (`src/Loop/LoopSnapshotPublish.cpp:1893`) clears the pending box, re-queues **all** committed passes, auto-adds the **active** edit passes as companions (`:1858`), and stamps `coveredPassRevision = committedPassRevision()` |
| 8 | Persistence | LSV1 admission requires a settled, current root (DEC-073 RC2); a permanently stale root also blocks the LSV1 body |
| 9 | Undo/redo interaction | This is the edge under investigation |
| 10 | Consumers | The window lane reads `publishedSnapshotIsCurrent()` / `committedPassRevision()`; `requestPlaybackWindow` refuses `SourceNotCurrent` |
| 11 | Hot path | The request is `TRACK_COLD_MEM` (flash); the work is idle-only, bounded at `kSnapshotIdleConstructionUnitsPerTurn` = 2 units |
| 12 | RAM1/PSRAM | No new state in the attempts (a helper + call sites); the RAM1 line did not move |

## What the measurement showed

The change under test was: a `TRACK_COLD_MEM requestPlaybackSourceCurrency(Loop&)` helper, called at the **end** of
`applyPublishedSnapshotSelect` (after every case mutation, including `refreshNoteEditSessionAfterPassStateChange`), with
`hasPendingSnapshotPublish()` / `publishedSnapshotIsCurrent()` guards. The 3.14 fixture was extended to serve a real root,
then run a real `TrackUndo::undoForLoop` on a `NoteEditPassClosed` batch.

| Observation | Value |
|---|---|
| the edge requests | `hasPendingSnapshotPublish() == true`, `pendingSnapshotFromLoadedPasses() == false`, `pendingSnapshotQueuedPassCount() == 1` |
| the demand before service | `SourceNotCurrent` (refused by name, never served stale — DEC-073 intact) |
| after 20 000 owner turns | `hasPendingSnapshotPublish() == false`, `publishedSnapshotCoveredRevision() == 1` while `playbackRevision == 4`, **no new root** |
| the refusal path | `stepPendingSnapshotPublish`'s finish branch with `unpublishedCandidate == nullptr` → `clearPendingSnapshotPublish()` |

**A first attempt is worth recording too:** the same request placed *inside* the two refresh helpers stamped a coverage
that the caller then invalidated (`refreshNoteEditSessionAfterPassStateChange` can advance the revision), so it could
never publish current. Placement at the end of the applied entry is the correct one; it is the publisher, not the
placement, that refuses.

## Why it refuses (VERIFIED, from the code)

1. `queueAllCommittedPassesInto` queues the **committed** passes and appends the **active** edit passes as companions
   (`LoopSnapshotPublish.cpp:1855-1862`).
2. `startNextPendingConstruction` diffs each queued pass against the **parent** root
   (`appendNextPassChangeFragment`). A record pass the parent already represents yields an empty change set, so the start
   returns false and the pass id is dropped.
3. The companions branch then runs with `box.companionIds` — but the batch that the undo just **disabled** is no longer
   active, so `appendCompanionChanges` skips it (`findActiveEditPass` returns null) and the change set stays empty.
4. An empty candidates path with no `unpublishedCandidate` clears the pending box: the loop asked for a republish, the
   owner correctly found **nothing to publish**, and no root was stamped current.

And the deeper term: `appendEditRowChange` can only **add/update** a companion row. A companion that becomes `Disabled`
has no removal representation in a snapshot change set, so a root that was published *with* the overlay cannot have that
overlay retracted by this path at all. Re-stamping a no-op diff as current would therefore be **silent-wrong** (the
undone note would keep playing) and was rejected.

## Consequence for the task text

3.14c's sentence — "request currency through the existing publication owner, without inline enumeration/publication" —
is **necessary but not sufficient**: the request exists and can be made from this edge, but the owner cannot build a
replacement for a note-edit-only change. 3.14c therefore needs a **shape** decision for the request, not a call to the
existing one.

## Options (not yet chosen)

| # | Option | Content correctness | Cost / risk |
|---|---|---|---|
| **A** | A **rebuild-from-empty** flavour of the request: the candidate is constructed from an empty parent + every committed pass + the **active** overlays. The existing incremental path stays as it is for record/overdub undo. | Correct for both cases this slice found: an overlay the undo disabled is simply absent from the rebuild, and an unchanged record pass is still enumerated. | Full enumeration per request (the same work a session-exit publication already pays); touches the publisher's parent selection behind a new flag; needs its own DEC. **Recommended.** |
| B | Companion **removals**: teach the snapshot change set to express "this edit row is gone" and apply it at membership. | Correct, and keeps candidates small. | Changes the snapshot model and every membership consumer; larger blast radius than the task's contract suggests. |
| C | **Coverage repair**: treat companion-only changes as an overlay revision that does not stale the content coverage; the read path applies the live overlay. | Correct by construction, and removes the whole class of spurious staleness. | Overturns DEC-073's "coverage is the committed-pass revision" contract; needs a reassessment, not a slice. |

## Recommendation

Take **A**, as its own slice with a DEC: *"a republish request rebuilds from the committed passes, not from the parent's
diff"*. It answers 3.14c in the owner's own vocabulary, keeps DEC-073 untouched, leaves the record/overdub path
byte-identical, and is native-verifiable with the fixture scaffolding already proven in this investigation (a real root
at setup, three `NoteEditPassClosed` batches so consecutive real `undoForLoop` edges are available — the redo route
drags `LoopEditManager` into this suite and does not link).

**Task state:** 3.14 stays open; 3.14c is unchanged and still owed. `tasks.md`, `PROJECT_STATE.md`, `CURRENT_WORK.md` and
`DELIVERABLE_TRACKING.md` were **not** edited for this investigation beyond this note, because nothing shipped.
