# Slice 3.13 (continuation) — fixture 2.12's integration half, and the harness it needed

**Change:** `windowed-playback-generation` · **Task:** 3.13 (**stays open**) · **Date:** 2026-10-05 ·
**Branch:** `feature/windowed-playback-generation`
**Nature:** host tooling + native fixture. **No `src/` or `include/` line changed** (`git diff -- src include` is
empty), so the firmware is untouched and the RAM1 line is unchanged by construction.

The slice landed 2026-10-02 (`2026-10-02-s313-preview-contract/`) implemented the preview **source contract** and
exercised it with a projection **the fixture supplied**. It named two halves as owed. This increment delivers the
first — the harness and fixture 2.12's integration half — and pins the seams the second half must use.

## What landed

| File | Change |
|---|---|
| `test/test_support/track_harness.inc` | The `EditManager` constructor and global are guarded by `HARNESS_LINKS_REAL_EDIT_MANAGER`, so a suite that links the **real** `src/EditManager.cpp` does not collide with the harness's own definitions. Default behaviour is unchanged for every existing suite. |
| `scripts/native_test_link.py` | `test_windowed_preview_integration` joins `_FIRMWARE_BOUNDARY_SUITES` (the real note-edit owner pulls the same Arduino/SD boundaries the continuous-capture harness needs). |
| `test/test_windowed_preview_integration/off_path_deps.h` | The runtime working-set suite's measured boundary set with the note-edit neighbours **removed** (this suite defines them for real). Its own additions are named in the file: the note-edit FSM's five state entries are **observable no-ops** (see the limitation below), the display/surface notifications the real session makes are observable, and `DisplayManager::resolveDetailedWindow` stays fail-closed (its only session-path caller is guarded by `!isNoteEditActive()`). |
| `test/test_windowed_preview_integration/test_windowed_preview_integration.cpp` | The suite: the **real** note-edit owner (session lifecycle, session store and projection, preview revision, `sessionUndo`/`sessionRedo`, the undo owner, `TrackManager::setSelectedTrack`) linked beside the **window lane** (`track_harness.inc`), with five fixtures. |

## The harness combination the 2026-10-02 evidence named as missing

The runtime working-set suite deliberately stubs the edit session ("this suite never opens an edit session"), so
driving a real one there would have been a new harness rather than a fixture. This suite is that harness: the
session's own projection (`EditManager::sessionMidiEvents()`'s const projection — `store.readEvents()`), the
session's own preview revision, the real undo/redo entries and the real focus route are the **inputs**, so the
preview every fixture sees is the session's content and not a hand-built vector.

## Fixtures (fixture 2.12's integration clauses)

Every fixture drives the **production** session and mutation edges: `openNoteEditSession` / `closeNoteEditSession`,
`Track::invalidateCaches(true)` (which arms the deferred refresh while playing),
`EditManager::flushDeferredNoteEditDisplayRefresh` (the flush that advances the playback preview revision),
`pushSessionUndoOnKindChange`, `sessionUndo`, `sessionRedo`, and `TrackManager::setSelectedTrack`.

| Fixture | Clause it pins | Observable |
|---|---|---|
| `test_the_session_owns_the_preview_source_and_its_revision_is_the_lane_currency` | the session owns the source; its revision is the lane's currency | **control:** no window serves the slot before the session exists; opening the session bumps the revision; the published window's `origin == Preview`, `playbackRevision == session revision`, interval == whole loop; the served stream **is** the session's projection |
| `test_a_mutation_between_build_grains_never_publishes_a_mixed_or_stale_preview` | no mixed or stale publication | a rebuild is begun and one grain taken; the mutation lands (content change + flush = revision + 1); the cancelled build's completion publishes **nothing** — the served window is the previous revision and the previous content, field-for-field; the new revision is admitted, adopted, and its content is the session's |
| `test_an_in_session_undo_and_redo_advance_the_preview_revision_and_the_served_content` | in-session undo/redo | the edit's revision is published and served; `sessionUndo` reverts the content (the added note is gone), the revision advances, and the served preview follows; `sessionRedo` advances the revision again and the served preview is the session's projection at that revision |
| `test_a_selected_track_change_cancels_the_departing_tracks_preview_by_its_own_lifecycle` | re-focus | the production focus route names the incoming track and re-opens the session there; the departing track's preview is cancelled as `SelectedTrackChanged` — `NothingToCancel`/`BuildCancelled`, never a committed build — its served window is untouched field-for-field, and **no** committed handoff is owed (control: the incoming track owns no preview yet) |
| `test_the_session_exit_owes_the_committed_handoff_and_the_preview_keeps_serving` | exit + handoff | `closeNoteEditSession` then `SessionExited` → `HandoffOwed`; the preview keeps serving, untouched field-for-field; a repeated exit is `AlreadyHandedOff`; the committed successor is prepared (still preview serving) and, once adopted, serves as `Committed` and the handoff is discharged |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_windowed_preview_integration` | **5 cases: 5 succeeded** |
| `pio test -e native` (full) | **2,815 cases: 7 skipped, 2,808 succeeded, 0 failed** (baseline 2,807 cases before this and the 3.12 fixture slice) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192 / ITCM 393,216 (code 391,576 + padding 1,640) / locals 34,880`, floor 840, unchanged (no production file changed) |
| `git diff -- src include` | empty (test-only + host tooling) |
| `git diff --check` | clean |
| `openspec validate windowed-playback-generation --strict` | valid |

## Red-proofs (each restored byte-identically, `shasum -a 256` compared)

| # | Mutation | Observed failure |
|---|---|---|
| MA1 | `flushDeferredNoteEditDisplayRefresh` does not `bumpSessionPlaybackPreviewRevision()` | F2 and F3: *"a mutation advances the preview revision once"* (`Expected 2 Was 1`) |
| MA2 | `openNoteEditSession` does not `bumpSessionPlaybackPreviewRevision()` | F1: *"opening the session bumps the preview revision it freezes"* (`Expected 0 to be greater than 0`) |
| MA3 | `cancelPreviewWindowForSlot` skips `applyPlaybackViewTransition(ClearOrLoad)` | F2: the un-released candidate blocks the new revision — *"the new revision is admitted"* (`Expected 0 Was 3`) |

`src/EditManager/NoteEditDisplayProjection.cpp`, `src/EditManager/NoteEditSessionLifecycle.cpp` and
`src/Track/TrackPlaybackWindowBuild.cpp` are byte-identical to their pre-mutation hashes after each restore.

## Limits recorded, not smoothed over

- **The FSM's five state TUs cannot be linked natively.** `src/EditStates/EditStartNoteState.cpp:69` (and its
  siblings) assign `focus.last.startTick` (a `uint32_t`) to a `uint8_t` member in a braced initializer, which the
  host toolchain rejects (`-Wc++11-narrowing`); a plain `g++ -std=gnu++17 -fsyntax-only` on the file reproduces it
  outside this harness. The firmware toolchain evidently permits it, which is not something this slice changes. The
  suite therefore stubs the five states' four entry points as **observable no-ops** rather than fail-closed, and the
  session's content store, revision, undo/redo entries and lifecycle stay real.
- **The redo half is asserted at the preview boundary, not as a byte-exact redo of the fixture's own edit.** The
  fixture's mutation writes its row straight into the session store, while the session's *current-state model* —
  which the redo re-projects from — is built from the rematerialized loop; the redo therefore restores the loop's
  own content. A production geometry edit runs through the FSM, which cannot be linked (previous bullet), so that
  half belongs with the production-callers stage. The revision advance and "the served window is the session's
  projection, never the undone one" are asserted.
- **The loop storage survives between fixtures** in this harness (there is no pool-reset seam for loops), so a
  previous fixture's window can still be serving when the next one publishes. The helper adopts the successor when
  one is prepared, which is the same production boundary a traversal would take; `setUp` additionally closes any
  session left open and resets the session state, so every fixture starts from "no session, revision 0".
- **The preview window's *consumption* is not exercised** — nothing in production serves a published window yet
  (the merged fallback still serves NOTE_EDIT preview; the rolling lane is behind `PLAYBACK_WINDOW_ROLLING_ENABLED`,
  default 0). This suite asserts what the lane **publishes**.

## Still owed by task 3.13

- **The production callers**: the preview requester (the session's revision bump is the natural seam —
  `Track::invalidateCaches(true)` while playing arms the deferred refresh, and the flush is where the revision
  advances) and the `EditManager` lifecycle hooks for mutation, re-focus and exit. They belong with the stage that
  admits the committed requester, because a published window is consumed by the windowed serving path (3.10/3.7);
  this suite pins the seams they will use so the wiring lands against a pinned integration.
- **Fixture 2.12's `then save` clause** is not covered here: the storage closure is the storage suites' harness
  (`capture_storage_harness.inc` and friends), and this harness keeps the save/storage requests observable rather
  than linking that closure. Named as owed rather than asserted weakly.
