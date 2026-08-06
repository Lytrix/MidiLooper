//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditFocusInternal.h"

#include <algorithm>
#include <vector>

#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findLinearOffForNoteOnLifo(
    std::vector<MidiEvent, Alloc>& events, MidiEvent* noteOnEvent, uint8_t pitch) {
  if (noteOnEvent == nullptr) {
    return nullptr;
  }
  std::vector<MidiEvent*> activeNoteOnStack;
  for (auto& evt : events) {
    if (evt.channel != noteOnEvent->channel) {
      continue;
    }
    const bool isNoteOn =
        evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.data.noteData.note == pitch;
    const bool isNoteOff = evt.isNoteOff() && evt.data.noteData.note == pitch;
    if (isNoteOn) {
      activeNoteOnStack.push_back(&evt);
    } else if (isNoteOff) {
      for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
           --stackIndex) {
        MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
        if (evt.tick <= candidateOn->tick) {
          continue;
        }
        MidiEvent* pairedOn = candidateOn;
        activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
        if (pairedOn == noteOnEvent) {
          return &evt;
        }
        break;
      }
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findPlausibleOffForNoteOn(
    std::vector<MidiEvent, Alloc>& events, const MidiEvent& noteOn, uint32_t loopLength) {
  const uint8_t pitch = noteOn.data.noteData.note;
  const uint32_t startTick = noteOn.tick;
  MidiEvent* nearestInLoop = nullptr;
  MidiEvent* nearestBeyondLoop = nullptr;
  for (auto& evt : events) {
    if (!evt.isNoteOff() || evt.channel != noteOn.channel || evt.data.noteData.note != pitch ||
        evt.tick <= startTick) {
      continue;
    }
    if (!isPlausibleStorageSpan(startTick, evt.tick, loopLength)) {
      continue;
    }
    if (evt.tick <= loopLength) {
      if (nearestInLoop == nullptr || evt.tick < nearestInLoop->tick) {
        nearestInLoop = &evt;
      }
    } else if (nearestBeyondLoop == nullptr || evt.tick < nearestBeyondLoop->tick) {
      nearestBeyondLoop = &evt;
    }
  }
  if (nearestInLoop != nullptr && nearestBeyondLoop != nullptr && startTick > loopLength / 2) {
    return nearestBeyondLoop;
  }
  if (nearestInLoop != nullptr) {
    return nearestInLoop;
  }
  return nearestBeyondLoop;
}

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findLinearOffForNoteId(std::vector<MidiEvent, Alloc>& events,
                                                const MidiEvent& noteOn, NoteId noteId,
                                                uint32_t loopLength) {
  if (noteId == kInvalidNoteId) {
    return nullptr;
  }
  const uint8_t pitch = noteOn.data.noteData.note;
  MidiEvent* nearestTaggedOff = nullptr;
  for (auto& evt : events) {
    if (!evt.isNoteOff() || evt.channel != noteOn.channel || evt.noteId != noteId ||
        evt.tick <= noteOn.tick) {
      continue;
    }
    if (evt.data.noteData.note != pitch) {
      continue;
    }
    if (loopLength > 0 && !isPlausibleStorageSpan(noteOn.tick, evt.tick, loopLength)) {
      continue;
    }
    if (nearestTaggedOff == nullptr || evt.tick < nearestTaggedOff->tick) {
      nearestTaggedOff = &evt;
    }
  }
  if (nearestTaggedOff != nullptr) {
    return nearestTaggedOff;
  }
  MidiEvent* mutableOn = nullptr;
  for (auto& evt : events) {
    if (evt.noteId == noteId && evt.channel == noteOn.channel && evt.isNoteOn() &&
        evt.data.noteData.velocity > 0 && evt.tick == noteOn.tick &&
        evt.data.noteData.note == noteOn.data.noteData.note) {
      mutableOn = &evt;
      break;
    }
  }
  if (mutableOn == nullptr) {
    return nullptr;
  }
  return findLinearOffForNoteOnLifo(events, mutableOn, noteOn.data.noteData.note);
}

template MidiEvent* findLinearOffForNoteId<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, const MidiEvent&, NoteId, uint32_t);
template MidiEvent* findLinearOffForNoteId<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, const MidiEvent&, NoteId, uint32_t);

template <typename Alloc>
NOTE_EDIT_MEM bool findLinearNoteSpanForNoteId(std::vector<MidiEvent, Alloc>& events, NoteId noteId,
                                               uint8_t channel, NoteBaseline& outBaseline,
                                               uint32_t preferredStartTick, uint32_t loopLength) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  auto tryNoteOn = [&](MidiEvent& evt, bool requireChannel) -> bool {
    if (evt.noteId != noteId) {
      return false;
    }
    if (requireChannel && evt.channel != channel) {
      return false;
    }
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      return false;
    }
    if (preferredStartTick != UINT32_MAX && evt.tick != preferredStartTick) {
      return false;
    }
    MidiEvent* noteOffEvent = nullptr;
    if (evt.noteId != kInvalidNoteId) {
      noteOffEvent = findLinearOffForNoteId(events, evt, evt.noteId, loopLength);
    }
    if (noteOffEvent == nullptr) {
      noteOffEvent = findLinearOffForNoteOnLifo(events, &evt, evt.data.noteData.note);
    }
    if (noteOffEvent == nullptr) {
      return false;
    }
    outBaseline.pitch = evt.data.noteData.note;
    outBaseline.velocity = evt.data.noteData.velocity;
    outBaseline.startTick = evt.tick;
    outBaseline.endTick = noteOffEvent->tick;
    if (loopLength > 0 &&
        !isPlausibleStorageSpan(outBaseline.startTick, outBaseline.endTick, loopLength)) {
      return false;
    }
    return true;
  };

  for (MidiEvent& evt : events) {
    if (tryNoteOn(evt, true)) {
      return true;
    }
  }
  for (MidiEvent& evt : events) {
    if (tryNoteOn(evt, false)) {
      return true;
    }
  }
  return false;
}

template bool findLinearNoteSpanForNoteId<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, NoteId, uint8_t, NoteBaseline&, uint32_t, uint32_t);
template bool findLinearNoteSpanForNoteId<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, NoteId, uint8_t, NoteBaseline&, uint32_t, uint32_t);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnAtChannelPitchTick(
    std::vector<MidiEvent, Alloc>& events, uint8_t channel, uint8_t pitch, uint32_t tick) {
  for (auto& evt : events) {
    if (evt.channel == channel && evt.isNoteOn() && evt.data.noteData.velocity > 0 &&
        evt.data.noteData.note == pitch && evt.tick == tick) {
      return &evt;
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnForNoteIdAtTick(
    std::vector<MidiEvent, Alloc>& events, NoteId noteId, uint8_t channel, uint8_t pitch,
    uint32_t tick) {
  for (auto& evt : events) {
    if (evt.noteId != noteId || evt.channel != channel || !evt.isNoteOn() ||
        evt.data.noteData.velocity == 0 || evt.data.noteData.note != pitch ||
        evt.tick != tick) {
      continue;
    }
    return &evt;
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnForNoteIdAnyChannel(
    std::vector<MidiEvent, Alloc>& events, NoteId noteId, uint8_t pitch) {
  for (auto& evt : events) {
    if (evt.noteId != noteId || !evt.isNoteOn() || evt.data.noteData.velocity == 0 ||
        evt.data.noteData.note != pitch) {
      continue;
    }
    return &evt;
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findNoteOnForMovingNoteEdit(std::vector<MidiEvent, Alloc>& events,
                                                     const NoteEditFocus& focus, uint8_t channel,
                                                     uint8_t pitch, uint32_t startTick,
                                                     uint32_t loopLength) {
  if (focus.active && focus.movingNoteId != kInvalidNoteId) {
    NoteBaseline linearSpan;
    const uint32_t preferredStarts[] = {startTick, focus.commitBaseline.startTick, UINT32_MAX};
    for (uint32_t preferredStart : preferredStarts) {
      if (!findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                       preferredStart, loopLength)) {
        continue;
      }
      if (MidiEvent* on =
              findNoteOnForNoteIdAtTick(events, focus.movingNoteId, channel, linearSpan.pitch,
                                        linearSpan.startTick)) {
        return on;
      }
    }
    if (MidiEvent* on =
            findNoteOnForNoteIdAnyChannel(events, focus.movingNoteId, focus.last.pitch)) {
      return on;
    }
  }

  const uint8_t pitchCandidates[] = {pitch, focus.active ? focus.last.pitch : pitch,
                                     focus.active ? focus.commitBaseline.pitch : pitch};
  for (uint8_t pitchCandidate : pitchCandidates) {
    const uint32_t tickCandidates[] = {startTick,
                                       focus.active ? focus.commitBaseline.startTick : startTick,
                                       focus.active ? focus.movingNoteRange.start : startTick};
    for (uint32_t tick : tickCandidates) {
      if (MidiEvent* on = findNoteOnForNoteIdAtTick(events, focus.movingNoteId, channel,
                                                    pitchCandidate, tick)) {
        return on;
      }
    }
  }

  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    const uint32_t tickCandidates[] = {startTick,
                                       focus.active ? focus.commitBaseline.startTick : startTick,
                                       focus.active ? focus.movingNoteRange.start : startTick};
    for (uint32_t tick : tickCandidates) {
      if (MidiEvent* on = findNoteOnAtChannelPitchTick(events, channel, pitch, tick)) {
        return on;
      }
    }
  }
  return nullptr;
}

template MidiEvent* findNoteOnForMovingNoteEdit<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, const NoteEditFocus&, uint8_t, uint8_t, uint32_t, uint32_t);
template MidiEvent* findNoteOnForMovingNoteEdit<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, const NoteEditFocus&, uint8_t, uint8_t, uint32_t, uint32_t);

template <typename Alloc>
NOTE_EDIT_MEM bool syncNoteEditFocusLinearFromSessionStore(NoteEditFocus& focus,
                                                           std::vector<MidiEvent, Alloc>& events,
                                                           uint8_t channel, uint32_t loopLength) {
  if (!focus.active) {
    return false;
  }
  NoteBaseline linearSpan;
  if (focus.movingNoteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                  focus.last.startTick, loopLength)) {
    focus.last.startTick = linearSpan.startTick;
    focus.last.endTick = linearSpan.endTick;
    focus.last.pitch = linearSpan.pitch;
    focus.last.velocity = linearSpan.velocity;
    focus.movingNoteRange.start = linearSpan.startTick;
    focus.movingNoteRange.end = linearSpan.endTick;
    return true;
  }
  if (focus.movingNoteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                  focus.commitBaseline.startTick, loopLength)) {
    focus.last.startTick = linearSpan.startTick;
    focus.last.endTick = linearSpan.endTick;
    focus.movingNoteRange.start = linearSpan.startTick;
    focus.movingNoteRange.end = linearSpan.endTick;
    return true;
  }
  if (focus.movingNoteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan, UINT32_MAX,
                                  loopLength)) {
    focus.last.startTick = linearSpan.startTick;
    focus.last.endTick = linearSpan.endTick;
    focus.movingNoteRange.start = linearSpan.startTick;
    focus.movingNoteRange.end = linearSpan.endTick;
    return true;
  }
  const uint32_t startCandidates[] = {focus.last.startTick, focus.commitBaseline.startTick};
  for (uint32_t startTick : startCandidates) {
    MidiEvent* noteOnEvent =
        findNoteOnAtChannelPitchTick(events, channel, focus.last.pitch, startTick);
    if (noteOnEvent == nullptr) {
      continue;
    }
    MidiEvent* noteOffEvent = nullptr;
    if (focus.movingNoteId != kInvalidNoteId) {
      noteOffEvent = findLinearOffForNoteId(events, *noteOnEvent, focus.movingNoteId, loopLength);
    }
    if (noteOffEvent == nullptr) {
      noteOffEvent = findLinearOffForNoteOnLifo(events, noteOnEvent, focus.last.pitch);
    }
    if (noteOffEvent == nullptr) {
      continue;
    }
    focus.last.startTick = startTick;
    focus.last.endTick = noteOffEvent->tick;
    focus.movingNoteRange.start = startTick;
    if (focus.movingNoteRange.end <= focus.last.startTick ||
        focus.last.endTick > focus.movingNoteRange.end) {
      focus.movingNoteRange.end = focus.last.endTick;
    }
    return true;
  }
  return false;
}

template bool syncNoteEditFocusLinearFromSessionStore<InternalHeapFirstAllocator<MidiEvent>>(
    NoteEditFocus&, MidiEventVec&, uint8_t, uint32_t);
template bool syncNoteEditFocusLinearFromSessionStore<ExternalMemoryFirstAllocator<MidiEvent>>(
    NoteEditFocus&, SessionMidiEventVec&, uint8_t, uint32_t);

template <typename Alloc>
NOTE_EDIT_MEM void stampNoteIdsOntoPairedNoteOffs(std::vector<MidiEvent, Alloc>& events) {
  std::vector<MidiEvent*> activeNoteOnStack;
  for (MidiEvent& evt : events) {
    const uint8_t pitch = evt.data.noteData.note;
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      activeNoteOnStack.push_back(&evt);
      continue;
    }
    if (!evt.isNoteOff()) {
      continue;
    }
    for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
         --stackIndex) {
      MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
      if (candidateOn->data.noteData.note != pitch || candidateOn->channel != evt.channel ||
          evt.tick <= candidateOn->tick) {
        continue;
      }
      activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
      if (evt.noteId == kInvalidNoteId && candidateOn->noteId != kInvalidNoteId) {
        evt.noteId = candidateOn->noteId;
      }
      break;
    }
  }
}

template void stampNoteIdsOntoPairedNoteOffs<InternalHeapFirstAllocator<MidiEvent>>(MidiEventVec&);
template void stampNoteIdsOntoPairedNoteOffs<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&);

NOTE_EDIT_FOCUS_INTERNAL_MEM bool eventMatchesNoteEndpoint(const MidiEvent& e, uint8_t channel,
                                                           uint8_t pitch, uint32_t tick,
                                                           bool wantOn) {
  if (e.channel != channel || e.data.noteData.note != pitch || e.tick != tick) {
    return false;
  }
  if (wantOn) {
    return e.type == midi::NoteOn && e.data.noteData.velocity > 0;
  }
  return e.type == midi::NoteOff || (e.type == midi::NoteOn && e.data.noteData.velocity == 0);
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void eraseNoteEndpoint(std::vector<MidiEvent, Alloc>& flat,
                                                    uint8_t channel, uint8_t pitch, uint32_t tick,
                                                    bool wantOn) {
  flat.erase(std::remove_if(flat.begin(), flat.end(),
                            [&](const MidiEvent& e) {
                              return eventMatchesNoteEndpoint(e, channel, pitch, tick, wantOn);
                            }),
             flat.end());
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void eraseNotePairAtBaseline(std::vector<MidiEvent, Alloc>& flat,
                                                          uint8_t channel,
                                                          const NoteBaseline& bl) {
  eraseNoteEndpoint(flat, channel, bl.pitch, bl.startTick, true);
  eraseNoteEndpoint(flat, channel, bl.pitch, bl.endTick, false);
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnAt(std::vector<MidiEvent, Alloc>& flat,
                                                     uint8_t channel, uint8_t pitch,
                                                     uint32_t startTick) {
  for (MidiEvent& e : flat) {
    if (eventMatchesNoteEndpoint(e, channel, pitch, startTick, true)) {
      return &e;
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOffForOn(std::vector<MidiEvent, Alloc>& flat,
                                                         uint8_t channel, uint8_t pitch,
                                                         uint32_t startTick, uint32_t endTick) {
  (void)startTick;
  for (MidiEvent& e : flat) {
    if (eventMatchesNoteEndpoint(e, channel, pitch, endTick, false)) {
      return &e;
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void insertNotePair(std::vector<MidiEvent, Alloc>& flat,
                                                 uint8_t channel, const NoteBaseline& bl,
                                                 uint32_t endTick, NoteId noteId) {
  MidiEvent noteOn = MidiEvent::NoteOn(bl.startTick, channel, bl.pitch, bl.velocity);
  noteOn.noteId = noteId;
  flat.push_back(noteOn);
  flat.push_back(MidiEvent::NoteOff(endTick, channel, bl.pitch, 0));
}

template void eraseNoteEndpoint<InternalHeapFirstAllocator<MidiEvent>>(MidiEventVec&, uint8_t,
                                                                       uint8_t, uint32_t, bool);
template void eraseNoteEndpoint<ExternalMemoryFirstAllocator<MidiEvent>>(SessionMidiEventVec&,
                                                                         uint8_t, uint8_t,
                                                                         uint32_t, bool);
template void eraseNotePairAtBaseline<InternalHeapFirstAllocator<MidiEvent>>(MidiEventVec&, uint8_t,
                                                                             const NoteBaseline&);
template void eraseNotePairAtBaseline<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, uint8_t, const NoteBaseline&);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void materializeShortenedOverlap(
    std::vector<MidiEvent, Alloc>& flat, uint8_t channel, const OverlapNote& entry,
    uint32_t loopLength) {
  const NoteBaseline& bl = entry.baseline;
  const uint32_t targetOff = entry.shortenedEndTick;

  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(flat, loopLength, false);
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId == entry.noteId ||
        (dn.note == bl.pitch && dn.startTick == bl.startTick && dn.endTick == targetOff)) {
      return;
    }
  }

  eraseNoteEndpoint(flat, channel, bl.pitch, bl.endTick, false);

  MidiEvent* noteOn = findNoteOnAt(flat, channel, bl.pitch, bl.startTick);
  if (noteOn != nullptr) {
    MidiEvent* noteOff = findNoteOffForOn(flat, channel, bl.pitch, bl.startTick, targetOff);
    if (noteOff == nullptr) {
      noteOff = findNoteOffForOn(flat, channel, bl.pitch, bl.startTick, bl.endTick);
    }
    if (noteOff != nullptr) {
      noteOff->tick = targetOff;
      return;
    }
    flat.push_back(MidiEvent::NoteOff(targetOff, channel, bl.pitch, 0));
    return;
  }

  insertNotePair(flat, channel, bl, targetOff, entry.noteId);
}

template void materializeShortenedOverlap<InternalHeapFirstAllocator<MidiEvent>>(MidiEventVec&,
                                                                                uint8_t,
                                                                                const OverlapNote&,
                                                                                uint32_t);
template void materializeShortenedOverlap<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, uint8_t, const OverlapNote&, uint32_t);
