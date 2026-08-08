# Note Edit Session Current State Refinement

## Status

Draft architecture verification. No firmware implementation has started from this plan.

This supersedes the narrower "Note Edit Geometry Index" proposal as an ownership design. The prior name described a lookup/cache; this plan defines a mutable note edit session state owner.

## Problem

The same-pitch overlap captures show that stable `noteId` identity is correct, but the current session has no single owner for a note's current editable state.

Evidence from `captures/session_20260807_021939.log`:

- The first same-pitch note is moved as `EditSessionAction type=3 noteId=17 ... start=1392`.
- The second same-pitch note is moved as `EditSessionAction type=3 noteId=25 ...`.
- `DNTE` display ids change as display ordering changes, while action targets keep stable `noteId`s.
- Current code now prevents stale-baseline damage by excluding session-moved targets before analyze and skipping overlap actions for them, but that also prevents the second mover from modifying the first moved note at its current span.

## Architecture Verification

### Source Documents Checked

- `docs/Authority/ARCHITECTURE_RULES.md`
- `docs/Authority/DELIVERY_RULES.md`
- `docs/Authority/NAMING.md`
- `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- `docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`
- `openspec/specs/edit-session-action-geometry/spec.md`
- `openspec/specs/note-edit-modification-session/spec.md`
- `openspec/specs/note-edit-stable-note-id/spec.md`

### Verification Result

The original "geometry index beside `EditSession::store`" plan is not safe as written.

The archived specs currently say:

- `EditSession.store` / `sessionMidiEvents()` is the live store during NOTE_EDIT.
- `applyEditSessionActions` is the only live-store mutator for live geometry.
- Commit rows are derived from transaction baseline compared to final live store.
- `EditorSelection` owns stable `NoteId` selection and never list index or geometry.
- `baselineMap` is transaction baseline, not current editable geometry.

Adding an independently mutable geometry index beside `EditSession.store` would create duplicated ownership unless the proposal explicitly transfers ownership. That transfer is allowed only through the architecture ownership-transfer protocol: document current owner, target owner, migration, compatibility, removal trigger, validation, user approval, and a `DECISION_LOG.md` entry.

## Revised Ownership Model

### Current Owner

Today, current NOTE_EDIT geometry is effectively owned by `EditSession.store` as a linear MIDI event buffer, with `baselineMap`, `focus.last`, `overlapNotes`, `changedOverlapNoteIds`, and live-store scans reconstructing meaning around it.

This is the source of the bug class: current geometry, presence, display ordering, and overlap history are reconstructed by several readers instead of read from one note-state owner.

### Target Owner

`EditSession` remains the session owner. Inside `EditSession`, add a current editable note-state structure.

Preferred concept name: **Note edit current state**.

Do not use `NoteEditGeometryIndex` as the owner name. `Index` reads as a cache/lookup, and `Geometry` is narrower than the future edit scope. `docs/Authority/NAMING.md` defines `State` as mutable ownership and `Geometry` as timing, length, pitch, and overlap relationships. Current editable note state can later include velocity or other note metadata without renaming the owner.

Suggested code shape after naming review:

```cpp
enum class NoteEditPresenceType : uint8_t {
  Visible,
  Hidden,
  Deleted,
  Added,
};

struct NoteEditCurrentNoteState {
  NoteId noteId = kInvalidNoteId;
  NoteBaseline committedSpan{};
  NoteBaseline currentSpan{};
  NoteEditPresenceType presence = NoteEditPresenceType::Visible;
};
```

The collection should be keyed by `NoteId` and live on `EditSession`, not on `NoteEditFocus`.

### Ownership Boundaries

- `baselineMap` owns transaction baseline for the active edit driver and commit comparisons. It does not own current geometry.
- Note edit current state owns current editable note state per `noteId`: current span and presence.
- `EditSession.store` becomes the canonical event projection of current state for playback preview, existing event APIs, serialization, and compatibility during migration. It must not be a second geometry authority.
- `EditorSelection` owns only `NoteId` selection and selected bracket. It never owns geometry or display order.
- Display order, `selectedIdx`, `DNTE`, and filtered selectable inventories remain derived views.
- `overlapNotes`, `changedOverlapNoteIds`, and `sessionMovedNoteSpans` are not long-term authority. They are compatibility state to remove after current state owns overlap presence.

### Projection Direction

During NOTE_EDIT, projection is one-way after session construction:

```text
NoteEditCurrentState
        |
        v
project current rows to MIDI events
        |
        v
EditSession.store
```

Reverse construction from `EditSession.store` is allowed only at defined boundaries:

- Session open/reopen after committed materialize and `noteId` stamping.
- Workspace reload/rematerialize.
- Undo/redo migration phase while legacy `SessionUndoEntry` still stores event rows.
- Debug/parity verification that rebuilds a probe current state from projected events and compares it to authoritative current state.

After the current-state owner is active, normal geometry readers must not reconstruct editable state from projected events.

### Projection Semantics

During NOTE_EDIT, `EditSession.store` is the canonical, deterministic event projection of note edit current state. It must never become an independent source of editable note state.

The projection is intentionally lossy:

- `Visible` rows project to note-on/note-off event pairs.
- `Hidden` rows do not project to event pairs, because the visible MIDI event stream must omit hidden notes.
- `Deleted` rows do not project to event pairs, because the visible MIDI event stream must omit deleted notes.
- `Added` rows project to event pairs while present in current state.
- `committedSpan`, `currentSpan`, and `presence` for hidden/deleted rows remain only in note edit current state.

Do not encode hidden or deleted notes into projected events. If a future feature needs debug visibility, add a debug-only current-state dump rather than extending the MIDI event projection.

## Authoritative State

Store only the minimal state that cannot be derived:

- `committedSpan`
- `currentSpan`
- `presence`

Do not store independent flags for moved, resized, pitch changed, or restored. Derive those from:

- `currentSpan.startTick != committedSpan.startTick`
- `currentSpan.endTick != committedSpan.endTick`
- `currentSpan.pitch != committedSpan.pitch`
- `presence`

Velocity is already carried by `NoteBaseline`. Future note metadata can extend the row without changing ownership.

## State Invariants

These invariants must be documented with the implementation:

- Every `noteId` that can participate in NOTE_EDIT has exactly one current-state row.
- Current editable geometry is read from current state, not reconstructed from `baselineMap` plus live-store heuristics.
- `baselineMap` is immutable transaction baseline for the active edit driver.
- Selection owns only `noteId` and bracket; display order never determines identity.
- Every mutation path updates current state through one owner API before later readers observe the result.
- Hidden and deleted notes retain their current-state rows even when no pair exists in `EditSession.store`.
- Commit/publication derives rows from current state compared to committed baseline, then publishes `editPass` rows through the existing `Loop` owner.
- Current state and projected store are restored together; undo/redo must never restore one without the other.

## Projection Invariants

These invariants keep derived event storage separate from editable state:

- `EditSession.store` is the canonical event projection of current state during NOTE_EDIT.
- Projection is one-way after session construction.
- Visible and added rows project to MIDI event pairs.
- Hidden and deleted rows do not project to MIDI event pairs.
- Projected events never become authoritative editable state during normal NOTE_EDIT operation.
- Display projection and selectable inventories are derived from current state or from the canonical projection during compatibility migration.
- Reverse construction from projected events is allowed only at the boundaries listed in Projection Direction.

## `EditSession.store` Ownership Audit

This audit verifies whether `EditSession.store` can become projection/serialization rather than current editable geometry authority.

### API Boundary

- `EditManager::sessionMidiEvents()` returns `editSession.store.mutEvents()` and `EditManager::editMidiEvents()` routes NOTE_EDIT writes through it.
- `Track::editAwareMidiEvents()` exposes that mutable event vector to edit code.

Audit result: incompatible with the target model as-is.

Required migration:

- Split NOTE_EDIT APIs into read-projection access and owner mutation APIs.
- Keep `sessionMidiEvents()` or a renamed replacement as read/projection access for preview and compatibility.
- Remove mutable NOTE_EDIT geometry writes through `track.editAwareMidiEvents()` except inside the projection owner.

### Lifecycle Construction And Clear

Current paths:

- `EditManager::openNoteEditSession` materializes committed edit view into `editSession.store`, assigns/stamps `noteId`s, and enters default state.
- `reopenNoteEditSession`, `closeNoteEditSession`, `revertNoteEditSessionForLoopClear`, and `rematerializeNoteEditSessionAfterWorkspaceReload` clear or rebuild the store.

Audit result: compatible after adding current-state construction/clear beside these lifecycle transitions.

Required migration:

- Build current state immediately after materialize and `noteId` stamping.
- Clear current state anywhere `editSession.store` is cleared.
- Rebuild projected store from current state after construction so existing preview paths observe the same event rows.

### Playback Preview And Display

Current paths:

- `NoteEditDisplayProjection::selectableDisplayNotesAtEditSelect` calls `projectNoteEditDisplayNotes(committedBase, track.editAwareMidiEvents(), focus, ...)`.
- `liveEditDisplayNoteAtSelect`, focus rebuild, and selected index sync read projected events or focus values.
- `Track::invalidateCaches` treats NOTE_EDIT as a session-store overlay.

Audit result: compatible if these paths become read-only consumers of projected store or current-state-derived display rows.

Required migration:

- Display projection should prefer current state as the note source.
- Existing event projection can remain as a compatibility input until display parity is proven.
- `selectedIdx` remains derived from `EditorSelection.primaryNote` and current-state span.

### Focus Rebuild And Selection

Current paths:

- `rebuildNoteEditFocusAtSelect` uses `sessionMidiEvents()` to detect pending diffs, populate closure baselines, reconcile overlap notes, and read live selected spans.
- `rebuildNoteEditFocusForDisplayNote` reads session events to set `focus.last`.
- `syncNoteEditFocusLastFromSessionStore` reads linear pairs from session events.

Audit result: incompatible as final architecture because focus rebuild still treats projected events as current geometry authority.

Required migration:

- Focus rebuild must read the selected note's current span from note edit current state.
- Pending diff checks must compare current state against committed state.
- Closure population must use current-state rows and committed baselines, not event scans as authority.

### Geometry Resolution And Actions

Current paths:

- `NoteGeometryResolver::resolve` reads `track.editAwareMidiEvents()` as live store.
- `EditSessionInteraction` collects scope from `baselineMap`, `changedOverlapNoteIds`, and live event note-ons.
- `EditSessionActionBuilder` emits actions by comparing constrained geometry against live store.
- `applyEditSessionActions` mutates live store directly.

Audit result: incompatible as final architecture.

Required migration:

- Resolver and interaction analysis must read target current spans and presence from note edit current state.
- Action builder must compare constrained geometry against current state.
- Apply must update current state through one owner API, then project current state to `EditSession.store`.

### Add, Delete, Move, Length, And Pitch Edit Operations

Current paths:

- `EditSelectNoteState::createDefaultNote` pushes new note-on/off events directly into `track.editAwareMidiEvents()`.
- `EditManager::deleteSelectedNote` erases events from `track.editAwareMidiEvents()`.
- `NoteMovementUtils::applyPitchChange`, `moveNoteWithOverlapHandling`, and `changeLengthWithOverlapHandling` read and sometimes mutate `track.editAwareMidiEvents()` around the geometry pipeline.

Audit result: incompatible as final architecture.

Required migration:

- Add must allocate `noteId`, create an `Added` current-state row, then project.
- Delete must mark the row `Deleted`, then project.
- Move/length/pitch must mutate current state through the geometry owner and stop using direct event-vector mutation for current geometry.

### Control-Surface Dependent Fader Latch

Current path:

- `ControlSurfaceManager::publishDependentFaderLatch` obtains `editManager.sessionMidiEvents()`, normalizes a closure in place, and validates extracted store events.

Audit result: incompatible as final architecture.

Required migration:

- Closure normalization must move into the current-state projection owner.
- Dependent fader latch can request projection/validation, but it must not mutate projected store directly.

### Undo And Redo

Current paths:

- Session undo entries are built from `editSession.store.readEvents()`, `NoteEditFocus`, selection, and edit pass ids.
- Undo/redo apply entry rows to `editSession.store`, then restore focus/selection.

Audit result: compatible only after treating current state and projected store as one logical snapshot.

Required migration:

- `SessionUndoEntry` must carry current-state rows or a lossless current-state delta.
- `restoreSessionUndoEntry`, `sessionUndo`, and `sessionRedo` must restore current state and projected store together.
- Redo payload generation must derive from current state, not only event rows.

### Commit, Bake, And Publication

Current paths:

- `commitAllPendingNoteEditActions` detects pending work from focus and live-store diffs, normalizes session events, validates events, and builds rows with `buildPreCommitEditPasses`.
- `bakeNoteEditSessionStoreToPasses` builds replacement rows with `buildSessionStoreEditPasses(baselineStoreEvents, editSession.store.readEvents(), ...)`.
- `commitEditAction` saves rows through `Loop::saveNoteEditPass`, then rematerializes/replays into `editSession.store`.
- `foldLiveCaptureIntoNoteEditSession` merges capture events directly into session events and builds undo rows from store diff.

Audit result: publication remains compatible, but diff ownership must change.

Required migration:

- Commit and bake must build rows from current state versus committed baseline.
- `Loop::saveNoteEditPass` and `LoopPasses` remain the committed publication owners.
- `foldLiveCaptureIntoNoteEditSession` must add capture notes into current state, then project them to store.
- Existing store-diff row builders can remain temporarily as parity checks, not authority.

### Copy-On-Write Store

Current path:

- `LoopEventVectorCache` provides copy-on-write store snapshots and lazy event-vector caches.

Audit result: compatible as projection storage.

Required migration:

- Keep COW snapshots for projected event storage.
- Do not depend on COW store snapshots alone for hidden/deleted current-state rows, because those rows can be absent from projected events.

### Audit Conclusion

`EditSession.store` can become a projection, but not without an API split and migration of direct writers. Implementation must start with read-only build/verify helpers and an audit gate before changing ownership.

The required compatibility gate is:

- No NOTE_EDIT production path mutates projected store directly except the current-state projection owner.
- All remaining `mutEvents()` / `track.editAwareMidiEvents()` write sites are either removed, converted to owner calls, or guarded as temporary compatibility with a removal trigger.

## Editable Geometry Reader Audit

This audit complements the mutation audit. Every geometry reader listed here currently reads editable geometry from projected MIDI events or focus values derived from projected MIDI events. Each must migrate to note edit current state before the projection model is complete.

### Display And Selectable Inventory Readers

Current readers:

- `NoteEditDisplayProjection::selectableDisplayNotesAtEditSelect`
- `projectNoteEditDisplayNotes`
- `selectableDisplayNotesForEditUi`
- `syncSelectedNoteIdxToFilteredInventory`
- `liveEditDisplayNoteAtSelect`

Current behavior:

- Reconstructs or projects display notes from `track.editAwareMidiEvents()` and `NoteEditFocus`.

Required migration:

- Build selectable/display notes from current-state visible/added rows.
- Keep display ordering derived.
- Treat projected events as parity input only until display tests pass.

### Focus And Driver Validation Readers

Current readers:

- `isLiveEditDriverValid`
- `rebuildNoteEditFocusAtSelect`
- `rebuildNoteEditFocusForDisplayNote`
- `syncNoteEditFocusLastFromSessionStore`
- `isLiveEditDriverValidForTrack`
- `isMacroCommitAlignedWithSelectTargetForTrack`

Current behavior:

- Reads `sessionMidiEvents()` and `findLinearNoteSpanForNoteId` to recover the selected note's current span.

Required migration:

- Validate selected driver against current-state row for `EditorSelection.primaryNote`.
- Refresh `focus.last` from current state.
- Compare pending commit and pending overlap state from current-state diffs.

### Geometry Resolution Readers

Current readers:

- `NoteGeometryResolver::resolve`
- `EditSessionInteraction::collectEvaluationScopeNoteIds`
- `ensureBaselineMapEntriesForEvaluationScope`
- `overlayAnalysisBaselineForSessionMovedOverlaps`
- `ResolveConstrainedGeometry`

Current behavior:

- Reads live event note-ons, `baselineMap`, changed overlap ids, and helper scans such as `readLiveLinearSpan`.

Required migration:

- Evaluation scope comes from current-state rows plus selection and pitch lane.
- Target spans come from `currentSpan`.
- Target presence comes from `presence`.
- `baselineMap` remains committed baseline only.

### Action Builder Readers

Current readers:

- `buildEditSessionActions`
- `liveStoreHasNotePair`
- `readLiveLinearSpan`
- `findLiveNoteIdForPitchStart`

Current behavior:

- Determines whether to restore, shorten, or hide by checking projected store pair presence.

Required migration:

- Determine actions from current-state `presence` and `currentSpan`.
- Use projected store only for parity assertions until the builder no longer accepts live store as authority.

### Commit And Bake Readers

Current readers:

- `commitAllPendingNoteEditActions`
- `buildPreCommitEditPasses`
- `bakeNoteEditSessionStoreToPasses`
- `buildSessionStoreEditPasses`
- `commitEditAction` replay/parity path

Current behavior:

- Builds committed rows from baseline plus final projected store.

Required migration:

- Build committed rows from current state compared to committed baseline.
- Keep projected-store row builders temporarily as parity checks.
- Remove parity path after fixtures cover hidden, deleted, added, moved, pitch, and length cases.

### Undo And Redo Readers

Current readers:

- `buildSessionUndoEntry`
- `processKindBoundaryUndoWarm`
- `sessionUndo`
- `sessionRedo`
- `applyUndoRedoLanding`

Current behavior:

- Reads projected events to build undo payloads and uses display reconstruction to land selection.

Required migration:

- Snapshot current-state rows with the projected event store.
- Build redo payloads from current state.
- Land selection from current-state row for the restored `primaryNote`.

### Edit Operation Readers

Current readers:

- `NoteMovementUtils::applyPitchChange`
- `moveNoteWithOverlapHandling`
- `changeLengthWithOverlapHandling`
- `EditManager::applyCreatedNoteOverlapGeometry`
- `EditManager::deleteSelectedNote`
- `EditSelectNoteState::createDefaultNote`

Current behavior:

- Reads current geometry from display notes, focus, and projected event pairs before mutating events or calling the resolver.

Required migration:

- Resolve the operation target by `EditorSelection.primaryNote` and current-state row.
- Use display note payloads only as UI inputs, not geometry authority.
- Convert add/delete/move/length/pitch to current-state mutations.

### Control-Surface Reader

Current reader:

- `ControlSurfaceManager::publishDependentFaderLatch`

Current behavior:

- Reads and normalizes projected events for closure validation before publishing dependent fader latches.

Required migration:

- Read current-state closure rows.
- Request projection-owner validation if projected event checks are still needed.
- Do not normalize projected events from this path.

### Reader Audit Gate

Ownership transfer is not complete until:

- No NOTE_EDIT geometry reader uses projected MIDI events as editable-state authority.
- Any remaining projected-event read is display/playback/serialization, debug, or parity-only.
- Every parity-only projected-event reader has a named removal trigger.

## State Transitions

The current-state owner must cover every NOTE_EDIT mutation path:

- Open session: build current state from committed materialize and session store after `noteId` stamping.
- Reopen/rematerialize session: rebuild current state from the authoritative session snapshot.
- Select/reselect: update `EditorSelection` and `NoteEditFocus`, but do not rebuild current geometry from display order.
- Move: update `currentSpan.startTick` and `currentSpan.endTick` for the selected `noteId`.
- Length: update `currentSpan.endTick`.
- Pitch: update `currentSpan.pitch`.
- Add: create an `Added` row with a new `noteId`.
- Delete: mark the row `Deleted`.
- Hide: mark the target `Hidden` while preserving `currentSpan`.
- Shorten: keep `Visible` and update `currentSpan.endTick`.
- Restore: mark `Visible` and restore `currentSpan` to the resolved span.
- Undo/redo: restore current state together with the session event snapshot.
- Commit: publish diffs from current state, then advance committed spans or clear rows according to the committed batch.
- Close session: clear current state with `EditSession`.

## Commit And Publication Model

The ownership transfer must update archived behavior that currently says commit rows are derived from final live store.

Revised model:

- Commit rows are derived from current state compared to committed baseline.
- `Loop::saveNoteEditPass` remains the publication API for committed `editPass` rows.
- `LoopPasses` remains the canonical committed timeline.
- `EditSession.store` remains the MIDI event projection used for preview and compatibility, but commit must not reconstruct hidden/deleted/current spans from event absence and baseline heuristics.

This requires an OpenSpec delta or explicit architecture decision because it changes normative archived specs:

- `openspec/specs/edit-session-action-geometry/spec.md`
- `openspec/specs/note-edit-modification-session/spec.md`

## Copy-On-Write And Memory

The current-state row collection must use the same memory philosophy as other NOTE_EDIT cold structures:

- Use `ExternalMemoryFirstAllocator` for row maps/vectors.
- Do not add hot-path external pool walks.
- Keep `EditSession.store` copy-on-write snapshots for event projection.
- Session undo must restore current state and store projection consistently.
- If full row snapshots are too expensive, snapshot only current-state rows that differ from committed baseline plus rows needed for hidden/deleted/add state. The implementation must prove this with native tests before relying on it.

## Module Boundaries

Keep the owner inside existing `EditSession` / `EditManager` boundaries. Do not add a new top-level Manager.

Suggested modules:

- `include/NoteEditCurrentState.h`
- `src/EditManager/NoteEditCurrentState.cpp`

Allowed responsibilities:

- Build current state at NOTE_EDIT open/reopen.
- Read current state by `noteId`.
- Apply a typed note-edit current-state mutation.
- Project current state to `EditSession.store`.
- Verify current state against store projection in debug builds.

Not allowed:

- Direct mutation of current state from display code.
- Direct mutation of current state from control-surface input code.
- Parallel live-store mutation outside the owner API.
- Persistent compatibility layers without a removal trigger.

## Migration Plan

1. Create an ownership-transfer section in this plan and append a `DECISION_LOG.md` entry before firmware edits.

2. Split NOTE_EDIT accessors conceptually before behavior changes: identify read/projection access, owner mutation access, and temporary compatibility writes.

3. Add current-state row types and read-only build/verify helpers. Initially build from `EditSession.store` and compare against current behavior without changing behavior.

4. Add projection verification: current state projects to the same visible session-store pairs as the legacy store for visible rows, while hidden/deleted rows remain represented only in current state.

5. Route display, selection, and focus rebuild reads through current state while keeping projected store parity assertions.

6. Route `NoteGeometryResolver` reads through current state for target current spans and presence. Keep `baselineMap` only for committed baseline comparisons.

7. Route `buildEditSessionActions` decisions through current state. Stop using `liveStoreHasNotePair` as geometry authority.

8. Route `applyEditSessionActions` through a single current-state mutation function, then project the result to `EditSession.store`.

9. Convert direct add/delete/move/length/pitch event-vector writes to current-state owner calls.

10. Convert control-surface closure normalization to a current-state projection-owner operation.

11. Update undo/redo snapshots to restore current state and store projection as one unit.

12. Route commit row generation through current state diffs. Keep a temporary parity check against current live-store diff output.

13. Remove compatibility state after parity tests pass:
   - `sessionMovedNoteSpans`
   - session-moved target exclusion in `NoteGeometryResolver`
   - overlap action skip guard in `ApplyEditSessionActions`
   - any remaining commit authority from `overlapNotes` or `changedOverlapNoteIds`

14. Update OpenSpec specs and architecture docs after the implementation path is accepted.

## Debug Assertions

Add debug-only verification around the new owner:

- Selected `EditorSelection.primaryNote` exists in current state when a note is selected.
- Current-state rows have unique `noteId`s.
- Every visible current-state row has a matching projected session-store pair.
- Hidden and deleted rows do not require a session-store pair.
- Overlap analysis consumes target `currentSpan`, not `baselineMap` as current geometry.
- Commit reads current state for changed rows and does not reconstruct current geometry from baseline plus event absence.
- Display-selected index resolves back to the same `noteId` after projection/order changes.

## Tests

Native tests:

- `session_20260807_021939`: move note A, move note B, verify B can modify A using A's current span.
- `session_20260807_021022`: no stale-baseline `RestoreNote` / `ShortenNote` / `HideNote` for note 17 at committed start after note 17 moved to 1392.
- Repeated sequence: move A, move B, move A again, move B again, undo, redo, commit.
- Selection reorder: display index changes do not change selected `noteId` or current-state lookup.
- Hidden row: no session-store pair, current state still has geometry for restore.
- Deleted row: no session-store pair, commit emits delete from current state.
- Added row: new `noteId` exists in current state and projects to session store.
- Projection invariant: current state projects visible rows to `EditSession.store`; hidden/deleted rows remain in current state without projected pairs.
- Accessor gate: NOTE_EDIT add/delete/move/length/pitch paths cannot mutate projected store except through the current-state projection owner.
- Undo snapshot invariant: undo/redo restores current state and projected store together.
- Commit model: commit rows come from current state diffs; legacy store-diff builder is parity-only during migration.
- Commit parity during migration: current-state diff output matches legacy live-store diff for scenarios that legacy handles correctly.

Verification gates:

- `pio test -e native`
- Firmware build with `pio run -e teensy41-capture-serial`
- HITL edit retest after user-approved upload

## Decision Log Entry Draft

Append this to `docs/DECISION_LOG.md` after user approval and before firmware implementation.

```markdown
## DEC-XXX — NoteEditCurrentState owns NOTE_EDIT editable note state

**Date:** 2026-08-07

**Status:** Accepted

**Context:** Same-pitch NOTE_EDIT overlap captures showed that stable `NoteId` identity was correct, but current editable geometry was reconstructed from `EditSession.store`, `baselineMap`, focus fields, overlap scratch, display order, and live-store scans. This allowed stale committed baseline geometry to affect later edits and then required guard patches that prevented correct current-span overlap edits.

**Decision:** During NOTE_EDIT, `EditSession` owns editable note state through `NoteEditCurrentState`. `baselineMap` remains committed transaction baseline. `EditSession.store` becomes the canonical event projection of `NoteEditCurrentState` for playback preview, serialization, compatibility, and parity checks; it is not editable-state authority.

**Previous owner:** Current editable NOTE_EDIT geometry was effectively owned by `EditSession.store` plus scattered reconstruction helpers.

**New owner:** `NoteEditCurrentState` inside `EditSession`.

**Migration strategy:** Split read/projection access from mutation APIs, add read-only current-state build/verify, prove projection parity, migrate readers, migrate writers, migrate undo/redo and commit diffs, then remove compatibility state and direct projected-store mutation.

**Compatibility period:** `EditSession.store` remains projected MIDI event storage while readers/writers migrate. Legacy store-diff builders may remain as parity checks only.

**Removal trigger:** Delete `sessionMovedNoteSpans`, session-moved overlap skip guards, live-store geometry authority in resolver/action builder/commit, and direct NOTE_EDIT writes through `track.editAwareMidiEvents()` after native fixtures and HITL edit retest pass.

**Validation:** Native tests for 021939, 021022, repeated A/B move undo/redo commit, selection reorder, hidden/deleted/added rows, projection invariant, accessor gate, undo snapshot invariant, and commit parity; then firmware build and user-approved HITL edit retest.
```

## Decision

This plan recommends the ownership-transfer model, not a derived cache model.

A derived cache would align more easily with the archived specs, but it would keep `EditSession.store` as the geometry owner and continue forcing hidden/deleted/current note meaning through event-pair reconstruction. That does not solve the ownership issue exposed by the same-pitch moved-note captures.

The implementation should not begin until the user accepts this ownership transfer and the first implementation step records the decision.
