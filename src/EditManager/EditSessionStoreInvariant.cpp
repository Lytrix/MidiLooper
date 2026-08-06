//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionStoreInvariant.h"

#include <algorithm>

#include "EditSessionLiveStoreSpan.h"
#include "Utils/NoteEditMem.h"

EditSessionStoreInvariantResult verifyEditSessionStoreInvariant(const MidiEventVec& events,
                                                                uint32_t loopLength,
                                                                uint8_t channel) {
  (void)channel;
  EditSessionStoreInvariantResult result;
  if (loopLength == 0) {
    return result;
  }
  const LoopEventValidation::LoopEventValidationResult validation =
      LoopEventValidation::validateLoopEvents(events, loopLength,
                                            LoopEventValidation::kIdleCleanupMask);
  result.passed = validation.passed;
  result.firstFailure = validation.firstFailure;
  return result;
}

NOTE_EDIT_MEM void enforceEditSessionStoreInvariant(MidiEventVec& liveStore,
                                                    const NoteEditFocus& focus, uint8_t channel,
                                                    uint32_t loopLength) {
  if (loopLength == 0) {
    return;
  }

  for (size_t i = 0; i < liveStore.size(); ++i) {
    MidiEvent& evt = liveStore[i];
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0 || evt.channel != channel) {
      continue;
    }
    if (evt.noteId == kInvalidNoteId) {
      continue;
    }
    MidiEvent* noteOff = findLinearOffForNoteId(liveStore, evt, evt.noteId, loopLength);
    if (noteOff != nullptr) {
      continue;
    }

    const auto baselineIt = focus.baselineMap.find(evt.noteId);
    if (baselineIt != focus.baselineMap.end() &&
        baselineIt->second.endTick > evt.tick &&
        baselineIt->second.endTick <= loopLength) {
      MidiEvent offEvt =
          MidiEvent::NoteOff(baselineIt->second.endTick, channel, evt.data.noteData.note, 0);
      offEvt.noteId = evt.noteId;
      liveStore.push_back(offEvt);
      continue;
    }

    evt.data.noteData.velocity = 0;
  }

  liveStore.erase(std::remove_if(liveStore.begin(), liveStore.end(),
                                 [](const MidiEvent& evt) {
                                   return evt.isNoteOn() && evt.data.noteData.velocity == 0;
                                 }),
                  liveStore.end());
}
