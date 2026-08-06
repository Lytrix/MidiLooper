---
name: Track TU split
overview: Shrink `src/Track.cpp` (~2436 LOC) into a thin per-track coordinator by moving cohesive capture/playback/transport domains into `src/Track/*.cpp`, one phase per PR, behavior-preserving, with state remaining on `Track`.
todos:
  - id: phase-0-scaffold
    content: "Phase 0: TrackInternal.h + TrackStopTelemetryColdHelpers.cpp (stop stats, #CAP stage logging)"
    status: completed
  - id: phase-1-routing
    content: "Phase 1: Extract TrackMidiEventRouting.cpp (legacy scratch, editAwareMidiEvents, invalidateCaches)"
    status: completed
  - id: phase-2-slots
    content: "Phase 2: Extract TrackSlotPool.cpp (loop pool adapters, hasDataInSlot, reconcileTransportState)"
    status: completed
  - id: phase-3-state
    content: "Phase 3: Extract TrackStateFacade.cpp (setState wrapper, predicates, mute, slot focus)"
    status: completed
  - id: phase-4-capture-in
    content: "Phase 4: Extract TrackCaptureInput.cpp (startRecording, recordMidiEvents, noteOn/Off, finalizePendingNotes)"
    status: completed
  - id: phase-5-capture-stop
    content: "Phase 5: Extract TrackCaptureStopCommit.cpp (prepareRecordStop, stopRecording, commitCaptureForStop) — protected"
    status: completed
  - id: phase-6-overdub
    content: "Phase 6: Extract TrackOverdubLifecycle.cpp (start/stop overdub, handleNoteEditFold) — protected"
    status: completed
  - id: phase-7-window
    content: "Phase 7: Extract TrackPlaybackWindowBuild.cpp (merged MIDI build, runtime reclaim, playback reset)"
    status: completed
  - id: phase-8-playback
    content: "Phase 8: Extract TrackPlaybackHotPath.cpp (playMidiEvents, sendMidiEvent, jam filter)"
    status: completed
  - id: phase-9-transport
    content: "Phase 9: Extract TrackTransportControl.cpp (start/stop playing, queued grid start)"
    status: completed
  - id: phase-10-defer-geom
    content: "Phase 10: Extract TrackDeferredMaintenance.cpp + TrackLoopJamGeometry.cpp (REVT, idle validate, loop/jam, clear)"
    status: completed
isProject: false
---

# Track translation-unit extraction

**Authoritative plan:** [docs/plans/track_translation_unit_extraction_refinement.md](../../docs/plans/track_translation_unit_extraction_refinement.md)

**Branch:** `refactor/track` (from `dev`)

**Baseline:** `Track.cpp` **~2436 LOC** → target **~200–350 LOC** after Phases 0–10.

**Shipped siblings (do not re-move):** `TrackStateMachine.cpp`, `TrackPlaybackRuntime.cpp`, `TrackUndo.cpp`, `TrackDisplayState.cpp`, `TrackManager.cpp`.

**PR stack:** 0 → 1 → 2; 3 parallel after 2; 4 before 5; 5 → 6 sequential (protected); 7 before 8; 9 after 3+8; 10 last.
