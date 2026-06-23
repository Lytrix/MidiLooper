## 1. Config and seal outcome

- [x] 1.1 Add `PREFERRED_UNDO_DEPTH`, `MIN_UNDO_DEPTH`, `ABSOLUTE_MAX_UNDO_ENTRIES`, `HEAP_RESERVE_BYTES` in `Globals.h`
- [x] 1.2 Add `PassConfig::CHUNK_RESERVE` in `LoopPasses.h`; remove `MAX_CAPTURE_PASSES_PER_LOOP` as admission gate
- [x] 1.3 Rename `SealOutcome::AtPassCap` → `PoolExhausted`; update callers and logs
- [x] 1.4 Remove `Config::MAX_UNDO_HISTORY` fixed trim constant (or alias to `PREFERRED_UNDO_DEPTH` only in tests during migration)

## 2. LoopEventStore admission

- [x] 2.1 Implement `usedChunkCount`, `freeChunkCount`, `canAllocChunkWithReserve` in `LoopEventStore`
- [x] 2.2 Remove `capturePassCount >= MAX_CAPTURE_PASSES_PER_LOOP` check in `Loop::sealCapture`
- [x] 2.3 Native test: seal succeeds when `capturePassCount > 25` and pool has headroom
- [x] 2.4 Native test: seal returns `PoolExhausted` when pool full (simulate `POOL_CHUNK_COUNT`)

## 3. Loop reclaim

- [x] 3.1 Add `reclaimDisabledCapturePass` and `reclaimUnreferencedDisabledCapturePasses` on `Loop`
- [x] 3.2 Add `reclaimUnreferencedDisabledEditPasses` on `Loop`
- [x] 3.3 Add `reclaimUnreferencedDisabledPasses(PassReferenceSet)` combining 3.1–3.2
- [x] 3.4 Native test: disabled overdub reclaimed when passId not in reference set
- [x] 3.5 Native test: disabled pass retained when undo entry still references passId

## 4. TrackUndo reference set and trim

- [x] 4.1 Implement `collectReferencedPasses(TrackManager&)` in `TrackUndo.cpp`
- [x] 4.2 Replace `trimGlobalUndoHistory` with `trimUndoStackForMemory` (pressure + preferred depth)
- [x] 4.3 Call reclaim after trim, `dropRedoBranch`, `eraseUndoEntriesForSlot`
- [x] 4.4 Native test: trim drops oldest entry and reclaims unreferenced disabled pass
- [x] 4.5 Native test: 90 undo entries retained when pool and heap headroom mock satisfied

## 5. TrackManager and idle orchestration

- [x] 5.1 Add `TrackManager::reclaimUnreferencedDisabledPasses()`
- [x] 5.2 Call from `main.cpp` idle block when `!timingCriticalTrackActive`
- [x] 5.3 `Track::finalizeCommitSideEffects`: on `SealFailed`, reclaim + one seal retry

## 6. Edit heap admission

- [x] 6.1 Add heap admission check in `Loop::saveNoteEditPass` (no row-count cap)
- [x] 6.2 `EditManager::commitEditAction`: reclaim + one retry on `kInvalidEditPassId`
- [x] 6.3 Native test: save rejected when heap below reserve; succeeds after reclaim mock

## 7. Docs and verification (groups 1–6)

- [x] 7.1 Update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` — admission, reclaim, global undo trim
- [x] 7.2 Run `pio test -e native` — full suite green after groups 1–6
- [x] 7.3 HITL canonical baseline if user confirms firmware upload (groups 1–6)
- [x] 7.4 `/opsx:archive` — groups 1–9 complete; main spec **`note-edit-session-undo`** merged 2026-06-23 (§9 vocabulary post-**scoped-edit-pass-payload**)

## 8. Follow-up (parked in design D8)

- [ ] 8.1 (Future) Dynamic `POOL_CHUNK_COUNT` growth when PSRAM headroom allows

## 9. Small session undo entries (after `note-edit-session-undo-gpio`)

Replace **`NoteEditSessionUndoStack`** **`cloneShared`** payloads with **editRows** + **focus** entries
(design D10). Depends on kind-boundary **`pushSessionUndoOnKindChange`** from **gpio** change.
**Closeout:** main spec [`openspec/specs/note-edit-session-undo/spec.md`](../../../specs/note-edit-session-undo/spec.md) — 2026-06-23.

- [x] 9.1 Add `SessionUndoEntry` (`editRows`, `NoteEditFocus`, selection/bracket fields) in
      `NoteEditSessionUndo.h`; remove `shared_ptr<const LoopEventStore>` entry type
- [x] 9.2 Implement `buildSessionUndoEntry` using `resolveOverlapNotesForPreCommit` +
      `buildPreCommitEditPasses` at kind boundary
- [x] 9.3 `pushSessionUndoOnKindChange` → append `SessionUndoEntry`; remove `pushSessionUndoBeforeMutation`
      **`cloneShared`** path
- [x] 9.4 `sessionUndo` / `sessionRedo`: materialize + **applyNoteEditPassSequence** + restore focus +
      `applyUndoRedoLanding` / `syncNoteEditSessionStateToUi`
- [x] 9.5 Add `PREFERRED_SESSION_UNDO_DEPTH` + heap admission before push; pressure trim of oldest entries
- [x] 9.6 Native parity: clone-based vs **editRows**-based undo — overlap round-trip, move → pitch → move back
- [x] 9.7 Native: 128-bar fixture — four kind-boundary **E:** steps without N× full-loop clone memory
- [x] 9.8 Document in `LOOP_MIDI_STORAGE_AND_VALIDATION.md`: committed **editPass** = **EditPass** row fields;
      live store = **materialize**; **E:** = **editRows** + **focus** entries (not **cloneShared** stack)
