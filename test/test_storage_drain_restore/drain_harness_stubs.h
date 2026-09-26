// Boundary stubs for the real blocking-drain suite.
//
// The drain itself is production code under test. What is supplied here are cold boundaries it reaches
// transitively but that cannot be linked natively, each recorded rather than silently satisfied. None is
// domain logic for a drain fixture:
//
//   * `RtcTime` reads the Teensy RTC through `core_pins.h`/imxrt registers, so its TU does not compile
//     for `native` at all. The drain only needs a monotonic non-zero wall clock for `lastActiveUnix`.
//   * `LooperStateManager::exitLoadSaveMode` lives in `src/LooperState.cpp`, which also defines the
//     `looperState` global the shared harness owns; including the TU is a duplicate definition. The
//     method only closes the set-browser overlay.
//   * `DisplayManager::invalidateForSlotChange` (cold display cache invalidation),
//     `EditManager::beforeSelectedTrackChange` / `onTrackChanged` (note-edit session lifecycle) and
//     `Track::silenceSlotMidiOutput` (physical note-off emission) live in TUs that pull the OLED library
//     headers and, for the track one, redefine the shared harness's `silenceTrackMidiOutput` boundary.
//     The workspace-save path calls them defensively; a drain fixture can never reach a sounding track.
//
// Every stub below is a no-op or a counter, never a success that conceals a decision: no domain branch
// under test reads them.
#pragma once

#include <cstdint>

#include "EditManager.h"
#include "DisplayManager.h"
#include "LooperState.h"
#include "Track.h"
#include "TrackManager.h"

namespace DrainHarness {

/// Test-controlled wall clock. A fixture may pin it.
inline uint32_t& wallClockSeconds() {
  static uint32_t seconds = 1700000000u;
  return seconds;
}

/// Counters so a fixture can assert a boundary was (or was not) reached, rather than merely trusting a
/// no-op.
inline uint32_t& slotChangeInvalidations() {
  static uint32_t count = 0;
  return count;
}
inline uint32_t& trackChangeNotifications() {
  static uint32_t count = 0;
  return count;
}
/// S4.1 persistence-identity fixture: a slot move reaches the same note-edit session lifecycle as the
/// track-change pair above (`TrackManager::setSelectedSlotIndex` departs/re-enters the session around the
/// move). Counted, not fail-closed, for the same reason: the fixture moves focus through the production
/// route and asserts the **persisted footer**, and the edit session is a neighbour of that move.
inline uint32_t& slotChangeNotifications() {
  static uint32_t count = 0;
  return count;
}
inline uint32_t& slotSilenceCalls() {
  static uint32_t count = 0;
  return count;
}

}  // namespace DrainHarness

namespace RtcTime {
uint32_t getUnixTime() { return DrainHarness::wallClockSeconds(); }
void raiseWallClockToAtLeast(uint32_t value) {
  if (value > DrainHarness::wallClockSeconds()) {
    DrainHarness::wallClockSeconds() = value;
  }
}
}  // namespace RtcTime

void LooperStateManager::exitLoadSaveMode() {
  // Qualified: `LooperStateManager` has a member named `looperState` (the enum), which shadows the
  // global `LooperStateManager looperState` object the harness owns.
  ::looperState.setLoadSaveModeForTest(false);
}

void DisplayManager::invalidateForSlotChange(uint8_t, uint8_t, uint8_t) {
  ++DrainHarness::slotChangeInvalidations();
}

void EditManager::beforeSelectedTrackChange(Track&) { ++DrainHarness::trackChangeNotifications(); }
void EditManager::onTrackChanged(Track&) { ++DrainHarness::trackChangeNotifications(); }
void EditManager::beforeSelectedSlotChange(Track&) { ++DrainHarness::slotChangeNotifications(); }
void EditManager::onSelectedSlotChanged(Track&, uint8_t) {
  ++DrainHarness::slotChangeNotifications();
}

// ---------------------------------------------------------------------------
// S4.1 persistence-identity fixture — the capture-finalization neighbours.
//
// `TrackManager::setSelectedSlotIndex` (the gesture route the fixture moves focus with) reaches
// `TrackManager::setActiveLoopIndex`, whose body calls `finalizeCaptureAndSelectSlot` **only while a take
// is live**. That capture-finalization closure is the stop-commit plus display/edit-refresh stack this
// suite deliberately does not link (A1.7's recorded scope decision), so each reached symbol fails closed
// rather than pretending to act. The fixture moves focus on a **stopped** track, so none of these is on
// its asserted path: if one is ever entered, the fixture says so instead of passing on a no-op.
// ---------------------------------------------------------------------------

#define CAPTURE_FINALIZATION_OFF_LIMITS() TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__)

void EditManager::bumpSessionPreviewRevision() { CAPTURE_FINALIZATION_OFF_LIMITS(); }
void EditManager::bumpSessionPlaybackPreviewRevision() { CAPTURE_FINALIZATION_OFF_LIMITS(); }
void EditManager::scheduleDeferredNoteEditDisplayRefresh() { CAPTURE_FINALIZATION_OFF_LIMITS(); }

void DisplayManager::emitDisplayCaptureSnapshot(const Track&, uint8_t, uint32_t) {
  CAPTURE_FINALIZATION_OFF_LIMITS();
}
void DisplayManager::refreshViewportAfterOverdubStop(Track&, uint8_t, uint32_t) {
  CAPTURE_FINALIZATION_OFF_LIMITS();
}

void Track::stopPlaying() { CAPTURE_FINALIZATION_OFF_LIMITS(); }
void Track::startPlaying(uint32_t, bool) { CAPTURE_FINALIZATION_OFF_LIMITS(); }
void Track::stopRecordingToStopped(uint32_t) { CAPTURE_FINALIZATION_OFF_LIMITS(); }
CommitResult Track::commitCaptureForStop(CommitReason, uint32_t, uint32_t, uint8_t) {
  CAPTURE_FINALIZATION_OFF_LIMITS();
  return CommitResult::SealFailed;
}

#undef CAPTURE_FINALIZATION_OFF_LIMITS

void Track::silenceSlotMidiOutput(uint8_t) { ++DrainHarness::slotSilenceCalls(); }
