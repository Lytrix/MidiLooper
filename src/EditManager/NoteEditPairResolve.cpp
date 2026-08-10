//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Logger.h"
#include "NoteEditGeometryApply.h"
#include "NoteEditGeometryApplyInternal.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/NoteUtils.h"

namespace NoteEditGeometryApply {

NOTE_EDIT_MEM bool linearStorageSpansOverlap(uint32_t start1, uint32_t end1, uint32_t start2,
                                             uint32_t end2) {
    return start1 < end2 && start2 < end1;
}

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findCorrespondingNoteOff(std::vector<MidiEvent, Alloc>& midiEvents,
                                                  MidiEvent* noteOnEvent, uint8_t pitch,
                                                  std::uint32_t startTick, std::uint32_t endTick) {
    (void)startTick;
    (void)endTick;
    if (noteOnEvent == nullptr) {
        return nullptr;
    }
    std::vector<MidiEvent*> activeNoteOnStack;

    for (auto& evt : midiEvents) {
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                         evt.data.noteData.note == pitch);
        bool isNoteOff = ((evt.type == midi::NoteOff ||
                           (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
                          evt.data.noteData.note == pitch);

        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
                 --stackIndex) {
                MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
                if (evt.tick <= candidateOn->tick) {
                    continue;
                }
                MidiEvent* correspondingNoteOn = candidateOn;
                activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
                if (correspondingNoteOn == noteOnEvent) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                               "Found corresponding note-off: pitch=%d, noteOn@%lu -> noteOff@%lu",
                               pitch, correspondingNoteOn->tick, evt.tick);
                    return &evt;
                }
                break;
            }
        }
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "No corresponding note-off found for pitch=%d, start=%lu",
               pitch, startTick);
    return nullptr;
}

template MidiEvent* findCorrespondingNoteOff<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, MidiEvent*, uint8_t, std::uint32_t, std::uint32_t);
template MidiEvent* findCorrespondingNoteOff<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, MidiEvent*, uint8_t, std::uint32_t, std::uint32_t);

NOTE_EDIT_MEM MidiEvent* findNoteOffPairedAt(MidiEventVec& midiEvents, uint8_t pitch,
                                             uint32_t startTick, uint32_t endTick) {
    std::vector<MidiEvent*> activeNoteOnStack;
    for (auto& evt : midiEvents) {
        const bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                               evt.data.noteData.note == pitch);
        const bool isNoteOff =
            ((evt.type == midi::NoteOff ||
              (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
             evt.data.noteData.note == pitch);

        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            if (activeNoteOnStack.empty()) {
                continue;
            }
            MidiEvent* correspondingNoteOn = activeNoteOnStack.back();
            activeNoteOnStack.pop_back();
            if (correspondingNoteOn->tick == startTick && evt.tick == endTick) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Found paired note-off: pitch=%d, start=%lu, end=%lu",
                           pitch, startTick, endTick);
                return &evt;
            }
        }
    }

    logger.log(CAT_MIDI, LOG_DEBUG,
               "No paired note-off for pitch=%d, start=%lu, end=%lu",
               pitch, startTick, endTick);
    return nullptr;
}

NOTE_EDIT_MEM MidiEvent* findNoteOffForNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel,
                                                     uint8_t pitch, uint32_t startTick,
                                                     NoteId noteId, uint32_t loopLength) {
    if (noteId != kInvalidNoteId) {
        for (auto& evt : midiEvents) {
            if (evt.noteId != noteId || evt.channel != channel || !evt.isNoteOn() ||
                evt.data.noteData.velocity == 0 || evt.data.noteData.note != pitch) {
                continue;
            }
            if (evt.tick != startTick) {
                continue;
            }
            if (MidiEvent* linearOff =
                    findLinearOffForNoteId(midiEvents, evt, noteId, loopLength)) {
                return linearOff;
            }
            return findCorrespondingNoteOff(midiEvents, &evt, pitch, evt.tick, 0);
        }
    }
    for (auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            return findCorrespondingNoteOff(midiEvents, &evt, pitch, startTick, 0);
        }
    }
    return nullptr;
}

NOTE_EDIT_MEM MidiEvent* resolveNoteOffForEditSpan(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                                                   uint8_t channel, uint8_t pitch,
                                                   uint32_t startTick, uint32_t displayEndTick,
                                                   uint32_t loopLength) {
    if (!noteOnEvent) {
        return nullptr;
    }

    if (MidiEvent* noteOffEvent = findCorrespondingNoteOff(
            midiEvents, noteOnEvent, pitch, startTick, displayEndTick)) {
        return noteOffEvent;
    }
    if (MidiEvent* noteOffEvent =
            findNoteOffPairedAt(midiEvents, pitch, startTick, displayEndTick)) {
        return noteOffEvent;
    }

    for (auto& evt : midiEvents) {
        if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        const uint32_t headOffTick =
            noteEditGeometryApplyStorageTickToDisplayPhase(evt.tick, loopLength);
        if (NoteUtils::isPreferredWrapTailForHeadOff(startTick, headOffTick, midiEvents, pitch,
                                                     channel, loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Found wrap-head note-off: pitch=%d, tailOn@%lu -> headOff@%lu",
                       pitch, startTick, headOffTick);
            return &evt;
        }
    }
    return nullptr;
}

NOTE_EDIT_MEM bool isOpenTailNoteAtLoopEnd(const MidiEventVec& midiEvents, uint8_t channel,
                                           uint8_t pitch, uint32_t startTick,
                                           uint32_t displayEndTick, uint32_t loopLength) {
    if (loopLength == 0 || displayEndTick != loopLength - 1) {
        return false;
    }

    const MidiEvent* noteOnEvent = nullptr;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            noteOnEvent = &evt;
            break;
        }
    }
    if (!noteOnEvent) {
        return false;
    }

    MidiEventVec& mutableEvents = const_cast<MidiEventVec&>(midiEvents);
    return resolveNoteOffForEditSpan(mutableEvents, const_cast<MidiEvent*>(noteOnEvent), channel,
                                     pitch, startTick, displayEndTick,
                                     loopLength) == nullptr;
}

}  // namespace NoteEditGeometryApply
