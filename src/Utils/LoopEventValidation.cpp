//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/LoopEventValidation.h"

#include <climits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LoopEventStore.h"
#include "Utils/LoopValidationMem.h"
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

namespace {

/// Judges one note-on against the note-off it belongs to. @p wrapWindowTicks is only meaningful
/// for the head/tail wrap check; the linear-off check ignores it.
using NotePairViolationFn = bool (*)(const MidiEvent& onEvt, const MidiEvent& offEvt,
                                     uint32_t loopLength, uint32_t wrapWindowTicks);

/// Walks canonical note-on/note-off pairs and reports the first pair @p violates rejects.
///
/// Canonical pairing only — a note-off is judged against the note-on it actually belongs to,
/// never against every same channel/pitch off in the vector. Without pairing, two sequential
/// notes on one pitch (A 100→200, B 500→600) match the pair (on B, off A) and report a
/// violation that does not exist. Shared by every pair-shape invariant so one pairing scheme
/// serves them all (and one instantiation stays in flash).
LOOP_VALIDATION_MEM bool validateCanonicalNotePairs(const MidiEventVec& events, uint32_t loopLength,
                                                    uint32_t wrapWindowTicks,
                                                    NotePairViolationFn violatesPair) {
  const size_t count = events.size();
  std::vector<bool> pairedOn(count, false);
  std::vector<bool> pairedOff(count, false);

  const auto violates = [&](const MidiEvent& onEvt, const MidiEvent& offEvt) {
    return violatesPair(onEvt, offEvt, loopLength, wrapWindowTicks);
  };

  const auto sameNote = [](const MidiEvent& a, const MidiEvent& b) {
    return a.channel == b.channel && a.data.noteData.note == b.data.noteData.note;
  };

  const auto isSoundingNoteOn = [](const MidiEvent& evt) {
    return evt.isNoteOn() && evt.data.noteData.velocity > 0;
  };

  // Pass 1 — explicit noteId on both sides is the strongest pairing signal and is order
  // independent (stampNoteIdsOntoPairedNoteOffs guarantees it for the session store).
  for (size_t i = 0; i < count; ++i) {
    const MidiEvent& onEvt = events[i];
    if (!isSoundingNoteOn(onEvt) || onEvt.noteId == kInvalidNoteId) {
      continue;
    }
    for (size_t j = 0; j < count; ++j) {
      const MidiEvent& offEvt = events[j];
      if (pairedOff[j] || !offEvt.isNoteOff() || offEvt.noteId != onEvt.noteId ||
          !sameNote(onEvt, offEvt)) {
        continue;
      }
      pairedOn[i] = true;
      pairedOff[j] = true;
      if (violates(onEvt, offEvt)) {
        return false;
      }
      break;
    }
  }

  // Pass 2 — untagged pairs: each note-off claims the nearest preceding unpaired note-on on its
  // own lane, which is the LIFO order NoteUtils::orderSamePitchNoteOffsForLifo establishes.
  for (size_t i = 0; i < count; ++i) {
    const MidiEvent& offEvt = events[i];
    if (!offEvt.isNoteOff() || pairedOff[i]) {
      continue;
    }
    for (size_t k = i; k-- > 0;) {
      const MidiEvent& onEvt = events[k];
      if (pairedOn[k] || !isSoundingNoteOn(onEvt) || !sameNote(onEvt, offEvt)) {
        continue;
      }
      pairedOn[k] = true;
      pairedOff[i] = true;
      if (violates(onEvt, offEvt)) {
        return false;
      }
      break;
    }
  }

  // Pass 3 — a still-open note-on plus an unmatched off of the same note is a stored wrap pair,
  // which canonical linear storage forbids. An off with no note-on at all is left for
  // checkOrphanNoteOff.
  for (size_t i = 0; i < count; ++i) {
    const MidiEvent& onEvt = events[i];
    if (pairedOn[i] || !isSoundingNoteOn(onEvt)) {
      continue;
    }
    for (size_t j = 0; j < count; ++j) {
      const MidiEvent& offEvt = events[j];
      if (pairedOff[j] || !offEvt.isNoteOff() || !sameNote(onEvt, offEvt)) {
        continue;
      }
      pairedOff[j] = true;
      if (violates(onEvt, offEvt)) {
        return false;
      }
      break;
    }
  }
  return true;
}

}  // namespace

LOOP_VALIDATION_MEM bool checkLinearNoteOff(const MidiEventVec& events, uint32_t loopLength) {
  if (loopLength == 0) {
    return true;
  }
  return validateCanonicalNotePairs(
      events, loopLength, 0,
      [](const MidiEvent& onEvt, const MidiEvent& offEvt, uint32_t loopLen, uint32_t) {
        if (NoteUtils::isWrappedLoopNotePair(onEvt.tick, offEvt.tick, loopLen)) {
          return true;
        }
        return offEvt.tick < onEvt.tick && offEvt.tick < loopLen;
      });
}

LOOP_VALIDATION_MEM bool checkNoWrappedPairStorage(const MidiEventVec& events, uint32_t loopLength,
                                                   uint32_t wrapWindowTicks) {
  if (loopLength == 0) {
    return true;
  }
  return validateCanonicalNotePairs(
      events, loopLength, wrapWindowTicks,
      [](const MidiEvent& onEvt, const MidiEvent& offEvt, uint32_t loopLen, uint32_t wrapWindow) {
        return NoteUtils::isHeadTailWrappedPair(onEvt.tick, offEvt.tick, loopLen, wrapWindow);
      });
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

OrphanRepairResult repairOrphanNoteEvents(MidiEventVec& events, uint32_t loopLengthTicks,
                                          uint32_t wrapWindowTicks) {
  OrphanRepairResult result;
  if (events.empty()) {
    return result;
  }

  std::sort(events.begin(), events.end(), eventLess);

  std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, PairHash> activeNotes;
  std::vector<bool> eventsToKeep(events.size(), true);

  for (size_t i = 0; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];

    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      const auto key = std::make_pair(evt.data.noteData.note, evt.channel);
      if (activeNotes.find(key) != activeNotes.end()) {
        eventsToKeep[activeNotes[key]] = false;
        ++result.orphanedRemoved;
      }
      activeNotes[key] = i;
    } else if (evt.isNoteOff()) {
      const auto key = std::make_pair(evt.data.noteData.note, evt.channel);
      auto it = activeNotes.find(key);
      if (it != activeNotes.end()) {
        activeNotes.erase(it);
        continue;
      }
      bool wrappedTailAhead = false;
      if (loopLengthTicks > 0) {
        const uint32_t noteOffTick = evt.tick;
        for (size_t j = i + 1; j < events.size(); ++j) {
          if (!eventsToKeep[j]) {
            continue;
          }
          const MidiEvent& later = events[j];
          if (!later.isNoteOn() || later.channel != evt.channel ||
              later.data.noteData.note != evt.data.noteData.note) {
            continue;
          }
          if (!NoteUtils::isHeadTailWrappedPair(later.tick, noteOffTick, loopLengthTicks,
                                                wrapWindowTicks)) {
            continue;
          }
          if (!NoteUtils::wrapPairIsUnblocked(events, noteOffTick, later.tick,
                                              evt.data.noteData.note, evt.channel)) {
            continue;
          }
          wrappedTailAhead = true;
          break;
        }
      }
      if (!wrappedTailAhead) {
        eventsToKeep[i] = false;
        ++result.orphanedRemoved;
      }
    }
  }

  if (result.orphanedRemoved == 0) {
    return result;
  }

  MidiEventVec cleaned;
  cleaned.reserve(events.size() - result.orphanedRemoved);
  for (size_t i = 0; i < events.size(); ++i) {
    if (eventsToKeep[i]) {
      cleaned.push_back(events[i]);
    }
  }
  std::sort(cleaned.begin(), cleaned.end(), eventLess);
  events = std::move(cleaned);
  return result;
}

MidiEventVec extractEventsForNoteIds(const MidiEventVec& events,
                                     const std::unordered_set<NoteId>& noteIds) {
  if (noteIds.empty()) {
    return {};
  }
  std::unordered_map<std::pair<uint8_t, uint8_t>, bool, PairHash> closurePitch;
  for (const MidiEvent& evt : events) {
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0 || evt.noteId == kInvalidNoteId) {
      continue;
    }
    if (noteIds.find(evt.noteId) != noteIds.end()) {
      closurePitch[{evt.data.noteData.note, evt.channel}] = true;
    }
  }

  MidiEventVec subset;
  subset.reserve(events.size());
  for (const MidiEvent& evt : events) {
    if (evt.noteId != kInvalidNoteId && noteIds.find(evt.noteId) != noteIds.end()) {
      subset.push_back(evt);
      continue;
    }
    if (!evt.isNoteOn() && !evt.isNoteOff()) {
      continue;
    }
    const auto key = std::make_pair(evt.data.noteData.note, evt.channel);
    if (closurePitch.find(key) != closurePitch.end()) {
      subset.push_back(evt);
    }
  }
  return subset;
}

}  // namespace LoopEventValidation
