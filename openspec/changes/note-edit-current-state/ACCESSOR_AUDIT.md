# NOTE_EDIT projected-store accessor audit

**OpenSpec:** `note-edit-current-state`  
**Task:** 3.1  
**Removal trigger:** `openspec/changes/note-edit-current-state/tasks.md` §5–7 after native parity + HITL edit retest

## API split (task 3.2)

| Access | Method | Role |
|--------|--------|------|
| Read projection | `EditManager::noteEditSessionProjectionEvents() const` | Canonical read-only `EditSession.store` flat view |
| Projection owner | `EditManager::refreshNoteEditSessionProjection(channel)` | Mutate store from `noteEditCurrentState` only |
| Current state read | `EditManager::noteEditCurrentState() const` | Authoritative editable rows |
| Current state mutate | `EditManager::noteEditCurrentStateMut()` | Owner mutations before projection refresh |
| Compat projection mut | `EditManager::mutNoteEditSessionProjectionEventsCompat()` | Legacy direct store writes during migration |
| Compat track mut | `EditManager::mutEditProjectionEventsCompat(Track&)` | Routes to compat session store during NOTE_EDIT |

Legacy names `sessionMidiEvents()` / `editMidiEvents()` remain as compat aliases until writer migration completes.

## Direct projected-store writers (compat)

| File | Symbol / context | Operation |
|------|------------------|-----------|
| `ApplyEditSessionActions.cpp` | `applyEditSessionActions` | Geometry overlap restore/hide/shorten/move via live store ref |
| `EditSelectNoteState.cpp` | `createDefaultNote` | Add note pairs |
| `NoteEditGeometryOps.cpp` | `deleteSelectedNote` | Erase events |
| `NoteMovementUtils.cpp` | `applyPitchChange`, `moveNoteWithOverlapHandling`, `changeLengthWithOverlapHandling` | Move/length/pitch |
| `NoteGeometryResolver.cpp` | `resolve` | Passes mutable live store into pipeline |
| `NoteEditFocusRebuild.cpp` | `rebuildNoteEditFocusAtSelect`, `syncNoteEditFocusLastFromSessionStore`, `deleteSelectedNote` | Focus sync / delete |
| `NoteEditSessionCommit.cpp` | `commitAllPendingNoteEditActions` | Normalize / validate session flat |
| `NoteEditSessionLifecycle.cpp` | `foldLiveCaptureIntoNoteEditSession` | Merge capture into session flat |
| `FaderDependentSnapshot.cpp` | `publishDependentFaderLatch` | Closure normalize in place |
| `NoteEditSessionUndoStack.cpp` | undo/redo apply | Replay rows into `store.mutEvents()` |
| `NoteEditSessionLifecycle.cpp` | open path | `assignMissingNoteIds`, `stampNoteIds` on `mutEvents()` |

## Read-only projection consumers (no change required for 3.x)

| File | Context |
|------|---------|
| `NoteEditDisplayProjection.cpp` | Display projection input |
| `NoteEditFocusRebuild.cpp` | Pending diff checks, reconstruct (read paths) |
| `TrackPlaybackWindowBuild.cpp` | Playback preview |
| `EditStartNoteState.cpp`, `EditLengthNoteState.cpp`, `EditPitchNoteState.cpp` | Hash baseline (read) |
| `EditSelectNoteState.cpp` | `lastMidiEventCount`, read path in selection |

## Projection owner path

1. Mutate `editSession.noteEditCurrentState` through owner APIs (`upsertRow`, future typed mutations).
2. Call `refreshNoteEditSessionProjection(channel)`.
3. Verify with `noteEditCurrentState.verifyProjection(noteEditSessionProjectionEvents(), channel)` in debug builds.

Direct `mutNoteEditSessionProjectionEventsCompat()` / `track.editAwareMidiEvents()` writes bypass current state until §5 writer migration.
