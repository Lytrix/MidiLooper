// Minimal stub surface for the load harness.
//
// This suite deliberately does NOT reuse test_continuous_capture/off_path_deps.h: it links
// src/TrackUndo.cpp, whose `restorePlaybackAfterSlotClear` collides with the anonymous-namespace one in
// MidiButtonActions.cpp, and the seam that would omit that TU also removes the edit-state headers
// off_path_deps.h depends on. Instead this file supplies only what the load suite's linked production set
// actually needs, which was measured rather than guessed: `resolveTrackIndexForPersistence` plus the five
// note-edit state vtables (their virtual methods are what emit them).
//
// Nothing here is domain logic under test: the note-edit routes are unreachable from a load fixture, and
// resolving a track index is a pointer comparison. The load path itself is real production code.
#pragma once

#include <unity.h>

// The edit-state class declarations, which the note-edit button TU would otherwise bring in transitively.
#include "EditLengthNoteState.h"
#include "EditNoteHomeState.h"
#include "EditPitchNoteState.h"
#include "EditSelectNoteState.h"
#include "EditStartNoteState.h"

/// Fail closed if a fixture accidentally enters a note-edit route.
#define LOAD_HARNESS_OFF_PATH() TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__)

#define LOAD_HARNESS_OFF_PATH_EDIT(Type)                                    \
  void Type::onEnter(EditManager&, Track&, uint32_t) { LOAD_HARNESS_OFF_PATH(); } \
  void Type::onExit(EditManager&, Track&) { LOAD_HARNESS_OFF_PATH(); }       \
  void Type::onEncoderTurn(EditManager&, Track&, int) { LOAD_HARNESS_OFF_PATH(); } \
  void Type::onButtonPress(EditManager&, Track&) { LOAD_HARNESS_OFF_PATH(); }

LOAD_HARNESS_OFF_PATH_EDIT(EditNoteHomeState)
LOAD_HARNESS_OFF_PATH_EDIT(EditSelectNoteState)
LOAD_HARNESS_OFF_PATH_EDIT(EditStartNoteState)
LOAD_HARNESS_OFF_PATH_EDIT(EditLengthNoteState)
LOAD_HARNESS_OFF_PATH_EDIT(EditPitchNoteState)
#undef LOAD_HARNESS_OFF_PATH_EDIT

/// Note-edit is not reachable from a load fixture, so these fail closed like the state handlers above.
void EditManager::commitAllPendingNoteEditActions(Track&) { LOAD_HARNESS_OFF_PATH(); }
void EditManager::foldLiveCaptureIntoNoteEditSession(Track&) { LOAD_HARNESS_OFF_PATH(); }

/// A physical output boundary, not an unrelated domain route: both stop destinations silence the track, so
/// this is observable rather than fail-closed (the same decision as the other suite's boundary). The counter
/// itself is one of the shared harness globals in track_harness.inc.
void Track::silenceTrackMidiOutput() { ++g_nativeSilenceTrackCalls; }

#ifndef HARNESS_LINKS_REAL_STOP_TELEMETRY
// Load ownership fixtures retire a real capture job before retrying a restore.
// They observe this outbound request only; persistence admission itself is tested
// with the real storage owner in test_shared_capture_finalization.
namespace LoadHarnessPersistence {
unsigned requests = 0;
Track* lastTrack = nullptr;
uint8_t lastSlot = Config::INVALID_LOOP_SLOT;
}
void requestLoopSlotPersistAndSaveState(Track& track, uint8_t slot, uint32_t) {
  ++LoadHarnessPersistence::requests;
  LoadHarnessPersistence::lastTrack = &track;
  LoadHarnessPersistence::lastSlot = slot;
}
/// Supplied here because the load suite's linked set never resolves the stop-telemetry TU. A suite that
/// **does** link it (the S4.1 persistence-identity fixture drives the production slot-keyed persistence
/// admission, which lives there) defines `HARNESS_LINKS_REAL_STOP_TELEMETRY` and gets the real identity
/// resolution instead of this pointer comparison.
inline uint8_t resolveTrackIndexForPersistence(const Track& track) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (&trackManager.getTrack(i) == &track) return i;
  }
  return 0;
}
#endif
