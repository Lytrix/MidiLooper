//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/LoopEventValidation.h"

#include <climits>
#include <unordered_map>
#include <utility>

#include "LoopEventStore.h"
#include "Utils/NoteUtils.h"

namespace LoopEventValidation {

namespace {

struct PairHash {
  size_t operator()(const std::pair<uint8_t, uint8_t>& p) const noexcept {
    return (static_cast<size_t>(p.first) << 8) | p.second;
  }
};

bool eventLess(const MidiEvent& a, const MidiEvent& b) {
  if (a.tick != b.tick) {
    return a.tick < b.tick;
  }
  const int aOrder = a.isNoteOff() ? 0 : (a.isNoteOn() ? 1 : 2);
  const int bOrder = b.isNoteOff() ? 0 : (b.isNoteOn() ? 1 : 2);
  return aOrder < bOrder;
}

}  // namespace

bool checkNoteOnInLoopRange(const MidiEventVec& events, uint32_t loopLength) {
  if (loopLength == 0) {
    return true;
  }
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.tick >= loopLength) {
      return false;
    }
  }
  return true;
}

bool checkLinearNoteOff(const MidiEventVec& events, uint32_t loopLength) {
  if (loopLength == 0) {
    return true;
  }
  for (const MidiEvent& onEvt : events) {
    if (!onEvt.isNoteOn() || onEvt.data.noteData.velocity == 0) {
      continue;
    }
    for (const MidiEvent& offEvt : events) {
      if (!offEvt.isNoteOff() || offEvt.channel != onEvt.channel ||
          offEvt.data.noteData.note != onEvt.data.noteData.note) {
        continue;
      }
      if (NoteUtils::isWrappedLoopNotePair(onEvt.tick, offEvt.tick, loopLength)) {
        return false;
      }
      if (offEvt.tick < onEvt.tick && offEvt.tick < loopLength) {
        return false;
      }
    }
  }
  return true;
}

bool checkNoWrappedPairStorage(const MidiEventVec& events, uint32_t loopLength,
                               uint32_t wrapWindowTicks) {
  if (loopLength == 0) {
    return true;
  }
  for (const MidiEvent& onEvt : events) {
    if (!onEvt.isNoteOn() || onEvt.data.noteData.velocity == 0) {
      continue;
    }
    for (const MidiEvent& offEvt : events) {
      if (!offEvt.isNoteOff() || offEvt.channel != onEvt.channel ||
          offEvt.data.noteData.note != onEvt.data.noteData.note) {
        continue;
      }
      if (NoteUtils::isHeadTailWrappedPair(onEvt.tick, offEvt.tick, loopLength, wrapWindowTicks)) {
        return false;
      }
    }
  }
  return true;
}

bool checkDerivedLength(const MidiEventVec& events, uint32_t loopLength) {
  (void)loopLength;
  std::unordered_map<std::pair<uint8_t, uint8_t>, uint32_t, PairHash> activeOnTick;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      activeOnTick[{evt.data.noteData.note, evt.channel}] = evt.tick;
    } else if (evt.isNoteOff()) {
      const auto key = std::make_pair(evt.data.noteData.note, evt.channel);
      const auto it = activeOnTick.find(key);
      if (it == activeOnTick.end()) {
        continue;
      }
      if (evt.tick < it->second) {
        return false;
      }
      activeOnTick.erase(it);
    }
  }
  return true;
}

bool checkNoteIdPairing(const MidiEventVec& events) {
  std::unordered_map<NoteId, size_t> noteIdCounts;
  for (const MidiEvent& evt : events) {
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0 || evt.noteId == kInvalidNoteId) {
      continue;
    }
    ++noteIdCounts[evt.noteId];
    if (noteIdCounts[evt.noteId] > 1) {
      return false;
    }
  }

  std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, PairHash> openOnCount;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      ++openOnCount[{evt.data.noteData.note, evt.channel}];
    } else if (evt.isNoteOff()) {
      const auto key = std::make_pair(evt.data.noteData.note, evt.channel);
      if (openOnCount[key] > 0) {
        --openOnCount[key];
      }
    }
  }
  for (const auto& entry : openOnCount) {
    if (entry.second > 0) {
      return false;
    }
  }
  return true;
}

bool checkPersistedTickCap(const MidiEventVec& events, uint32_t loopLength) {
  if (loopLength == 0) {
    return true;
  }
  const uint32_t maxTick = loopLength + LoopEventStoreConfig::BAR_TICKS;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOff() && evt.tick > maxTick) {
      return false;
    }
  }
  return true;
}

bool checkOrphanNoteOff(const MidiEventVec& events, uint32_t loopLength, uint32_t wrapWindowTicks) {
  MidiEventVec sorted = events;
  std::sort(sorted.begin(), sorted.end(), eventLess);
  std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, PairHash> activeOn;
  for (size_t i = 0; i < sorted.size(); ++i) {
    const MidiEvent& evt = sorted[i];
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      activeOn[{evt.data.noteData.note, evt.channel}] = i;
    } else if (evt.isNoteOff()) {
      const auto key = std::make_pair(evt.data.noteData.note, evt.channel);
      if (activeOn.find(key) != activeOn.end()) {
        activeOn.erase(key);
        continue;
      }
      bool wrappedTailAhead = false;
      if (loopLength > 0) {
        for (size_t j = i + 1; j < sorted.size(); ++j) {
          const MidiEvent& later = sorted[j];
          if (!later.isNoteOn() || later.channel != evt.channel ||
              later.data.noteData.note != evt.data.noteData.note) {
            continue;
          }
          if (!NoteUtils::isHeadTailWrappedPair(later.tick, evt.tick, loopLength, wrapWindowTicks)) {
            continue;
          }
          if (!NoteUtils::wrapPairIsUnblocked(sorted, evt.tick, later.tick, evt.data.noteData.note,
                                              evt.channel)) {
            continue;
          }
          wrappedTailAhead = true;
          break;
        }
      }
      if (!wrappedTailAhead) {
        return false;
      }
    }
  }
  return true;
}

bool checkOrphanNoteOn(const MidiEventVec& events, uint32_t loopLength) {
  (void)loopLength;
  std::unordered_map<std::pair<uint8_t, uint8_t>, int, PairHash> balance;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      ++balance[{evt.data.noteData.note, evt.channel}];
    } else if (evt.isNoteOff()) {
      --balance[{evt.data.noteData.note, evt.channel}];
    }
  }
  for (const auto& entry : balance) {
    if (entry.second > 0) {
      return false;
    }
  }
  return true;
}

LoopEventValidationResult validateLoopEvents(const MidiEventVec& events, uint32_t loopLength,
                                             uint32_t checkMask, uint32_t wrapWindowTicks) {
  LoopEventValidationResult result;
  const auto run = [&](LoopEventCheck check, bool ok) {
    if (!ok && result.passed) {
      result.passed = false;
      result.firstFailure = check;
    }
  };

  if (checkMask & static_cast<uint32_t>(LoopEventCheck::NoteOnInLoopRange)) {
    run(LoopEventCheck::NoteOnInLoopRange, checkNoteOnInLoopRange(events, loopLength));
  }
  if (checkMask & static_cast<uint32_t>(LoopEventCheck::LinearNoteOff)) {
    run(LoopEventCheck::LinearNoteOff, checkLinearNoteOff(events, loopLength));
  }
  if (checkMask & static_cast<uint32_t>(LoopEventCheck::NoWrappedPairStorage)) {
    run(LoopEventCheck::NoWrappedPairStorage,
        checkNoWrappedPairStorage(events, loopLength, wrapWindowTicks));
  }
  if (checkMask & static_cast<uint32_t>(LoopEventCheck::DerivedLength)) {
    run(LoopEventCheck::DerivedLength, checkDerivedLength(events, loopLength));
  }
  if (checkMask & static_cast<uint32_t>(LoopEventCheck::NoteIdPairing)) {
    run(LoopEventCheck::NoteIdPairing, checkNoteIdPairing(events));
  }
  if (checkMask & static_cast<uint32_t>(LoopEventCheck::PersistedTickCap)) {
    run(LoopEventCheck::PersistedTickCap, checkPersistedTickCap(events, loopLength));
  }
  if (checkMask & static_cast<uint32_t>(LoopEventCheck::OrphanNoteOff)) {
    run(LoopEventCheck::OrphanNoteOff, checkOrphanNoteOff(events, loopLength, wrapWindowTicks));
  }
  if (checkMask & static_cast<uint32_t>(LoopEventCheck::OrphanNoteOn)) {
    run(LoopEventCheck::OrphanNoteOn, checkOrphanNoteOn(events, loopLength));
  }
  return result;
}

}  // namespace LoopEventValidation
