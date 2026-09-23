//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"
#include "TrackManagerInternal.h"

#include "EditManager.h"
#include "LoopEventStore.h"
#include "PassReclaim.h"
#include "Utils/MemoryPressureLevel.h"
#include "Utils/MemoryPressurePolicy.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPressurePolicy.h"

#if defined(SESSION_CAPTURE)
#include "Utils/DebugSessionCapture.h"
#endif

#if defined(__IMXRT1062__)
#define PRESSURE_RECLAIM_MEM FLASHMEM
#else
#define PRESSURE_RECLAIM_MEM
#endif

PRESSURE_RECLAIM_MEM void TrackManager::tryReclaimDerivedViewCachesUnderPressure(
    MemoryPressureLevel level) {
  if (level < MemoryPressureLevel::Low) {
    return;
  }

  uint8_t trackOrder[Config::NUM_TRACKS];
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    trackOrder[i] = i;
  }
  for (uint8_t i = 0; i + 1 < Config::NUM_TRACKS; ++i) {
    for (uint8_t j = i + 1; j < Config::NUM_TRACKS; ++j) {
      const Track& a = tracks[trackOrder[i]];
      const Track& b = tracks[trackOrder[j]];
      const uint8_t priA = reclaimTrackPriority(trackOrder[i], *this, a);
      const uint8_t priB = reclaimTrackPriority(trackOrder[j], *this, b);
      if (priB < priA) {
        const uint8_t tmp = trackOrder[i];
        trackOrder[i] = trackOrder[j];
        trackOrder[j] = tmp;
      }
    }
  }

  for (uint8_t orderIdx = 0; orderIdx < Config::NUM_TRACKS; ++orderIdx) {
    const uint8_t trackIndex = trackOrder[orderIdx];
    Track& track = tracks[trackIndex];
    const bool selected = isSelectedTrack(track);
    const bool noteEditBlocksSelected =
        editManager.isNoteEditActive() && selected;

    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      if (noteEditBlocksSelected && slot == track.getActiveLoopIndex()) {
        continue;
      }
      Loop& loop = track.loopForSlot(slot);
      if (!isSlotEnabled(trackIndex, slot) && !loop.hasCommittedPasses()) {
        continue;
      }
    }

    (void)track.tryClearCommittedMidiScratch();
    (void)track.tryReleasePlaybackMergedMidiEventsMemory();
  }
}

void TrackManager::reclaimUnreferencedDisabledPasses(PassReclaimStats* statsOut,
                                                     bool diagnosticVisibility) {
  const uint32_t startUs = micros();
  const uint16_t usedBefore = LoopEventStore::usedChunkCount();
  const uint16_t freeBefore = LoopEventStore::freeChunkCount();
  uint16_t passesReclaimed = 0;
#if defined(SESSION_CAPTURE)
  // S1.4's second clause needs the *pinned* side of the ledger, which nothing emitted: the reclaim
  // line reports only what was reclaimed, and only when something was. Accumulated from the `refs`
  // already built below -- no extra walk and, in particular, no `InternalHeapFirstAllocator` vector
  // construction on the transport thread, which a periodic census would have added.
  uint16_t pinnedCapturePasses = 0;
  uint16_t pinnedEditPasses = 0;
  uint16_t slotsWithPins = 0;
#endif

  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    Track& track = tracks[trackIndex];
    PassReferenceSet refs{};
    collectReferencedPasses(track.getGlobalUndoStack(), refs);
    for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
      Loop& loop = track.getLoop(slotIndex);
#if defined(SESSION_CAPTURE)
      const SlotPassReferences& slotRefs = refs.slots[slotIndex];
      pinnedCapturePasses += static_cast<uint16_t>(slotRefs.capturePassIds.size());
      pinnedEditPasses += static_cast<uint16_t>(slotRefs.editPassIds.size());
      if (!slotRefs.capturePassIds.empty() || !slotRefs.editPassIds.empty()) {
        ++slotsWithPins;
      }
#endif
      passesReclaimed += loop.reclaimUnreferencedDisabledCapturePasses(refs.slots[slotIndex]);
      passesReclaimed += loop.reclaimUnreferencedDisabledEditPasses(refs.slots[slotIndex]);
    }
  }

  const uint16_t usedAfter = LoopEventStore::usedChunkCount();
  const uint16_t freeAfter = LoopEventStore::freeChunkCount();
  const uint16_t chunksReleased =
      usedBefore > usedAfter ? static_cast<uint16_t>(usedBefore - usedAfter) : 0;
  const uint32_t durationUs = micros() - startUs;

  if (statsOut != nullptr) {
    statsOut->passesReclaimed = passesReclaimed;
    statsOut->chunksReleased = chunksReleased;
    statsOut->chunksFreeBefore = freeBefore;
    statsOut->chunksFreeAfter = freeAfter;
    statsOut->durationUs = durationUs;
  }

#if defined(SESSION_CAPTURE)
  // The pinned triple is change-detected rather than merely appended: this function runs far more
  // often than its nine call sites suggest, so a plain "pins exist" gate wrote ~2,000 lines into one
  // session log and stole serial bandwidth during playback.
  static uint16_t lastCapturePins = 0xFFFFu;
  static uint16_t lastEditPins = 0xFFFFu;
  static uint16_t lastSlotsWithPins = 0xFFFFu;
  const bool pinsChanged = pinnedCapturePasses != lastCapturePins ||
                           pinnedEditPasses != lastEditPins || slotsWithPins != lastSlotsWithPins;
  if (passesReclaimed > 0 || chunksReleased > 0 || diagnosticVisibility || pinsChanged) {
    lastCapturePins = pinnedCapturePasses;
    lastEditPins = pinnedEditPasses;
    lastSlotsWithPins = slotsWithPins;
    const MemoryPressureLevel pressure = MemoryMonitor::getAdvisoryPressureLevel();
    bool transportActive = false;
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
      const Track& track = tracks[i];
      if (track.isRecording() || track.isAudiblePlaybackActive()) {
        transportActive = true;
        break;
      }
    }
    SC_PASS_RECLAIM(freeBefore, freeAfter, passesReclaimed, chunksReleased, durationUs,
                    memoryPressureLevelName(pressure), transportActive ? 1 : 0,
                    pinnedCapturePasses, pinnedEditPasses, slotsWithPins);
  }
#endif
}
