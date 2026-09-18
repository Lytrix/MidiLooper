//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Implementation owner for `CaptureAdmissionStaging`/`CaptureNoteReservation`
// (declared in `RuntimePlaybackView.h`). The declarations stay beside the
// per-slot `RuntimePlaybackViewSet` that owns two stagings by value: the struct
// holds a `FixedPlaybackEventStorage`, which that same header declares, so a
// declaration move would need a third header for the fixed storage.
//
// Placement is per function and unchanged by this split: `prewarm` stays cold
// (`RUNTIME_CAPTURE_STAGING_COLD_MEM`); the admission path below stays in the
// ITCM default because it runs on the MIDI note-on/note-off turn.

#include "RuntimePlaybackView.h"

#include "Utils/NoteEditMem.h"

#include <algorithm>

// Placement alias: the subsystem's cold code already shares `NOTE_EDIT_MEM` (see the sibling
// companion TUs), so this name documents the local rationale without redefining placement.
#define RUNTIME_CAPTURE_STAGING_COLD_MEM NOTE_EDIT_MEM

RUNTIME_CAPTURE_STAGING_COLD_MEM bool CaptureAdmissionStaging::prewarm() {
  return storage.prewarm(RuntimePlaybackViewConfig::kCaptureStagingEvents);
}

void CaptureAdmissionStaging::reset() {
  storage.clear();
  captureGeneration = 0;
  frozenGeneration = 0;
  frozen = false;
  reservedStagingCredits = 0;
  reservedBridgeCredits = 0;
  reservedStopFinalizationCredits = 0;
  reservations.fill(CaptureNoteReservation{});
}

bool CaptureAdmissionStaging::freeze(uint32_t generation) {
  if (!storage.isPrewarmed() || frozen || generation != captureGeneration) {
    return false;
  }
  frozenGeneration = generation;
  frozen = true;
  return true;
}

size_t CaptureAdmissionStaging::openReservationCount() const {
  size_t count = 0;
  for (const CaptureNoteReservation& reservation : reservations) {
    if (reservation.open) {
      ++count;
    }
  }
  return count;
}

bool CaptureAdmissionStaging::reserveIndependentEvent() {
  if (frozen || !hasCredits(1, 1, 0)) {
    return false;
  }
  ++reservedBridgeCredits;
  return true;
}

void CaptureAdmissionStaging::releaseIndependentEvent() {
  reservedBridgeCredits -= std::min(reservedBridgeCredits, static_cast<size_t>(1));
}

CaptureNoteReservation* CaptureAdmissionStaging::firstOpenReservation() {
  for (CaptureNoteReservation& reservation : reservations) {
    if (reservation.open) {
      return &reservation;
    }
  }
  return nullptr;
}

bool CaptureAdmissionStaging::hasCredits(uint16_t stagingCredits, uint16_t bridgeCredits,
                                         uint16_t stopFinalizationCredits) const {
  const size_t stagingCapacity = storage.capacity >= storage.eventCount
                                     ? storage.capacity - storage.eventCount
                                     : 0;
  const size_t bridgeCapacity = RuntimePlaybackViewConfig::kBridgeCount *
                                RuntimePlaybackViewConfig::kRecordsPerBridge;
  const size_t stopCapacity = RuntimePlaybackViewConfig::kMaxCaptureNoteReservations;
  return stagingCredits <= stagingCapacity - std::min(stagingCapacity, reservedStagingCredits) &&
         bridgeCredits <= bridgeCapacity - std::min(bridgeCapacity, reservedBridgeCredits) &&
         stopFinalizationCredits <=
             stopCapacity - std::min(stopCapacity, reservedStopFinalizationCredits);
}

CaptureNoteReservation* CaptureAdmissionStaging::reserve(
    NoteId noteId, uint8_t channel, uint8_t note, uint16_t stagingCredits, uint16_t bridgeCredits,
    uint16_t stopFinalizationCredits) {
  if (frozen || noteId == kInvalidNoteId ||
      matchingOpenReservation(noteId, channel, note) != nullptr ||
      !hasCredits(stagingCredits, bridgeCredits, stopFinalizationCredits)) {
    return nullptr;
  }
  for (CaptureNoteReservation& reservation : reservations) {
    if (!reservation.open && reservation.noteId == kInvalidNoteId) {
      reservation = CaptureNoteReservation{noteId, channel, note, stagingCredits, bridgeCredits,
                                            stopFinalizationCredits, SIZE_MAX, true};
      reservedStagingCredits += stagingCredits;
      reservedBridgeCredits += bridgeCredits;
      reservedStopFinalizationCredits += stopFinalizationCredits;
      return &reservation;
    }
  }
  return nullptr;
}

CaptureNoteReservation* CaptureAdmissionStaging::matchingOpenReservation(NoteId noteId,
                                                                           uint8_t channel,
                                                                           uint8_t note) {
  for (CaptureNoteReservation& reservation : reservations) {
    if (reservation.open && reservation.noteId == noteId && reservation.channel == channel &&
        reservation.note == note) {
      return &reservation;
    }
  }
  return nullptr;
}

bool CaptureAdmissionStaging::appendContinuationNoteOn(const MidiEvent& event) {
  if (frozen || !event.isNoteOn() || event.noteId == kInvalidNoteId) {
    return false;
  }
  CaptureNoteReservation* reservation =
      reserve(event.noteId, event.channel, event.data.noteData.note, 2, 2, 1);
  if (reservation == nullptr) {
    return false;
  }
  if (storage.append(event)) {
    reservation->noteOnStagingIndex = storage.eventCount - 1;
    return true;
  }
  close(*reservation);
  return false;
}

bool CaptureAdmissionStaging::appendReservedNoteOff(const MidiEvent& event, bool retainPair) {
  if (!event.isNoteOff()) {
    return false;
  }
  CaptureNoteReservation* reservation =
      matchingOpenReservation(event.noteId, event.channel, event.data.noteData.note);
  if (reservation == nullptr || !storage.append(event)) {
    return false;
  }
  if (!retainPair) {
    (void)storage.discardEvent(reservation->noteOnStagingIndex);
    (void)storage.discardEvent(storage.eventCount - 1);
  }
  close(*reservation);
  return true;
}

void CaptureAdmissionStaging::close(CaptureNoteReservation& reservation) {
  reservedStagingCredits -= std::min(reservedStagingCredits,
                                     static_cast<size_t>(reservation.stagingCredits));
  reservedBridgeCredits -= std::min(reservedBridgeCredits,
                                    static_cast<size_t>(reservation.bridgeCredits));
  reservedStopFinalizationCredits -=
      std::min(reservedStopFinalizationCredits,
               static_cast<size_t>(reservation.stopFinalizationCredits));
  reservation = CaptureNoteReservation{};
}
