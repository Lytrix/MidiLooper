//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ApplyEditSessionActions.h"

#include <algorithm>

#include "EditSessionLiveStoreSpan.h"

namespace {

bool eraseNotePairByNoteId(MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                           uint32_t loopLength) {
  NoteBaseline span{};
  if (!findLinearNoteSpanForNoteId(liveStore, noteId, channel, span, UINT32_MAX, loopLength)) {
    return false;
  }
  const uint8_t pitch = span.pitch;
  const uint32_t startTick = span.startTick;
  const uint32_t endTick = span.endTick;

  liveStore.erase(
      std::remove_if(liveStore.begin(), liveStore.end(),
                     [&](const MidiEvent& evt) {
                       if (evt.channel != channel || evt.data.noteData.note != pitch) {
                         return false;
                       }
                       if (evt.isNoteOn() && evt.data.noteData.velocity > 0 &&
                           evt.tick == startTick) {
                         return evt.noteId == noteId || noteId == kInvalidNoteId;
                       }
                       if (evt.isNoteOff() && evt.tick == endTick) {
                         return evt.noteId == noteId || noteId == kInvalidNoteId;
                       }
                       return false;
                     }),
      liveStore.end());
  return true;
}

MidiEvent* findNoteOnForNoteId(MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                               uint8_t pitch, uint32_t startTick) {
  for (MidiEvent& evt : liveStore) {
    if (evt.noteId != noteId || evt.channel != channel || !evt.isNoteOn() ||
        evt.data.noteData.velocity == 0 || evt.data.noteData.note != pitch ||
        evt.tick != startTick) {
      continue;
    }
    return &evt;
  }
  return nullptr;
}

bool resolveNotePairForAction(MidiEventVec& liveStore, const NoteEditFocus& focus, NoteId noteId,
                              uint8_t channel, uint8_t pitch, uint32_t /*startTick*/,
                              uint32_t loopLength, MidiEvent*& noteOn, MidiEvent*& noteOff) {
  noteOn = nullptr;
  noteOff = nullptr;

  NoteBaseline span{};
  if (findLinearNoteSpanForNoteId(liveStore, noteId, channel, span, UINT32_MAX, loopLength)) {
    noteOn = findNoteOnForNoteId(liveStore, noteId, channel, span.pitch, span.startTick);
    if (noteOn != nullptr) {
      noteOff = findLinearOffForNoteId(liveStore, *noteOn, noteId, loopLength);
      if (noteOff != nullptr) {
        return true;
      }
    }
  }

  if (focus.active && focus.movingNoteId == noteId) {
    noteOn = findNoteOnForMovingNoteEdit(liveStore, focus, channel, pitch,
                                         focus.commitBaseline.startTick, loopLength);
  } else {
    for (MidiEvent& evt : liveStore) {
      if (evt.noteId == noteId && evt.channel == channel && evt.isNoteOn() &&
          evt.data.noteData.velocity > 0) {
        noteOn = &evt;
        break;
      }
    }
  }

  if (noteOn == nullptr) {
    return false;
  }

  noteOff = findLinearOffForNoteId(liveStore, *noteOn, noteId, loopLength);
  if (noteOff != nullptr) {
    return true;
  }

  const uint8_t notePitch = noteOn->data.noteData.note;
  for (MidiEvent& evt : liveStore) {
    if (evt.channel != channel || !evt.isNoteOff() || evt.data.noteData.note != notePitch) {
      continue;
    }
    if (evt.noteId == noteId || evt.noteId == kInvalidNoteId) {
      noteOff = &evt;
      return true;
    }
  }

  return false;
}

void applyRestoreNote(const EditSessionAction& action, MidiEventVec& liveStore, uint8_t channel) {
  if (liveStoreHasNotePair(liveStore, action.targetNoteId, channel)) {
    return;
  }
  MidiEvent noteOn = MidiEvent::NoteOn(action.startTick, channel, action.pitch, action.velocity);
  noteOn.noteId = action.targetNoteId;
  liveStore.push_back(noteOn);
  MidiEvent noteOff = MidiEvent::NoteOff(action.endTick, channel, action.pitch, 0);
  noteOff.noteId = action.targetNoteId;
  liveStore.push_back(noteOff);
}

void applyShortenNote(const EditSessionAction& action, MidiEventVec& liveStore,
                      const NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  MidiEvent* noteOff = nullptr;
  if (!resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                action.startTick, loopLength, noteOn, noteOff)) {
    return;
  }
  noteOff->tick = action.endTick;
}

void applyHideNote(const EditSessionAction& action, MidiEventVec& liveStore, uint8_t channel,
                   uint32_t loopLength) {
  eraseNotePairByNoteId(liveStore, action.targetNoteId, channel, loopLength);
}

void applyMoveNote(const EditSessionAction& action, MidiEventVec& liveStore, NoteEditFocus& focus,
                   uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  MidiEvent* noteOff = nullptr;
  if (!resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                action.startTick, loopLength, noteOn, noteOff)) {
    return;
  }
  noteOn->tick = action.startTick;
  noteOff->tick = action.endTick;
  if (focus.active && focus.movingNoteId == action.targetNoteId) {
    noteEditFocusApplyMoveEnd(focus, action.startTick, action.endTick);
  }
}

void applyChangeLength(const EditSessionAction& action, MidiEventVec& liveStore,
                       NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  MidiEvent* noteOff = nullptr;
  if (!resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                action.startTick, loopLength, noteOn, noteOff)) {
    return;
  }
  noteOff->tick = action.endTick;
  if (focus.active && focus.movingNoteId == action.targetNoteId) {
    noteEditFocusApplyLengthEnd(focus, action.endTick);
  }
}

void applyChangePitch(const EditSessionAction& action, MidiEventVec& liveStore,
                      NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  MidiEvent* noteOff = nullptr;
  if (!resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                action.startTick, loopLength, noteOn, noteOff)) {
    return;
  }
  noteOn->data.noteData.note = action.pitch;
  noteOff->data.noteData.note = action.pitch;
  if (focus.active && focus.movingNoteId == action.targetNoteId) {
    noteEditFocusApplyPitch(focus, action.pitch, action.startTick, action.endTick, loopLength);
  }
}

void syncFocusAfterApply(NoteEditFocus& focus, MidiEventVec& liveStore, uint8_t channel,
                         uint32_t loopLength) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return;
  }
  syncNoteEditFocusLinearFromSessionStore(focus, liveStore, channel, loopLength);
}

}  // namespace

void applyBoundarySplitForEditSession(MidiEventVec& liveStore, uint8_t channel) {
  for (MidiEvent& offEvt : liveStore) {
    if (!offEvt.isNoteOff() || offEvt.channel != channel) {
      continue;
    }
    const uint32_t offTick = offEvt.tick;
    if (offTick == 0) {
      continue;
    }

    bool anotherOnAtTick = false;
    for (const MidiEvent& onEvt : liveStore) {
      if (!onEvt.isNoteOn() || onEvt.channel != channel || onEvt.data.noteData.velocity == 0) {
        continue;
      }
      if (onEvt.tick != offTick) {
        continue;
      }
      anotherOnAtTick = true;
      break;
    }

    if (anotherOnAtTick) {
      offEvt.tick = offTick - 1;
    }
  }
}

void applyEditSessionActions(const EditSessionActions& actions, MidiEventVec& liveStore,
                             NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  for (const EditSessionAction& action : actions) {
    switch (action.type) {
      case EditSessionActionType::RestoreNote:
        applyRestoreNote(action, liveStore, channel);
        break;
      case EditSessionActionType::ShortenNote:
        applyShortenNote(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::HideNote:
        applyHideNote(action, liveStore, channel, loopLength);
        break;
      case EditSessionActionType::MoveNote:
        applyMoveNote(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::ChangeLength:
        applyChangeLength(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::ChangePitch:
        applyChangePitch(action, liveStore, focus, channel, loopLength);
        break;
    }
  }

  applyBoundarySplitForEditSession(liveStore, channel);
  syncFocusAfterApply(focus, liveStore, channel, loopLength);
}
