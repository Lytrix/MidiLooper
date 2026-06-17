//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <cstddef>
#include <climits>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "MidiEvent.h"
#include "LoopEventStore.h"

namespace LoopStopFinalize {

struct NoteKeyHash {
  size_t operator()(const std::pair<uint8_t, uint8_t>& p) const noexcept {
    return (static_cast<size_t>(p.first) << 8) | p.second;
  }
};

using NoteKey = std::pair<uint8_t, uint8_t>;

inline bool noteEventLess(const MidiEvent& a, const MidiEvent& b) {
  if (a.tick != b.tick) {
    return a.tick < b.tick;
  }
  const int aOrder = a.isNoteOff() ? 0 : (a.isNoteOn() ? 1 : 2);
  const int bOrder = b.isNoteOff() ? 0 : (b.isNoteOn() ? 1 : 2);
  return aOrder < bOrder;
}

struct Result {
  size_t syntheticOffsInserted = 0;
};

inline bool tailNoteHasLaterOffInEvents(const MidiEventVec& events, const MidiEvent& noteOn) {
  for (const MidiEvent& evt : events) {
    if (!evt.isNoteOff() || evt.channel != noteOn.channel ||
        evt.data.noteData.note != noteOn.data.noteData.note) {
      continue;
    }
    if (evt.tick > noteOn.tick) {
      return true;
    }
  }
  return false;
}

inline bool tailNoteHasLaterOffInStore(const LoopEventStore& store, const MidiEvent& noteOn) {
  for (size_t i = 0; i < store.size(); ++i) {
    const MidiEvent& evt = store.at(i);
    if (!evt.isNoteOff() || evt.channel != noteOn.channel ||
        evt.data.noteData.note != noteOn.data.noteData.note) {
      continue;
    }
    if (evt.tick > noteOn.tick) {
      return true;
    }
  }
  return false;
}

inline void eraseTailOnsWithLaterOff(
    std::unordered_map<NoteKey, size_t, NoteKeyHash>& activeTailOnIndex,
    const MidiEventVec& events) {
  for (auto it = activeTailOnIndex.begin(); it != activeTailOnIndex.end();) {
    if (tailNoteHasLaterOffInEvents(events, events[it->second])) {
      it = activeTailOnIndex.erase(it);
    } else {
      ++it;
    }
  }
}

inline void eraseTailOnsWithLaterOff(
    std::unordered_map<NoteKey, size_t, NoteKeyHash>& activeTailOnIndex,
    const LoopEventStore& store) {
  for (auto it = activeTailOnIndex.begin(); it != activeTailOnIndex.end();) {
    if (tailNoteHasLaterOffInStore(store, store.at(it->second))) {
      it = activeTailOnIndex.erase(it);
    } else {
      ++it;
    }
  }
}

/// Lightweight stop-path cleanup: wrap-window note pairing + synthetic open-tail offs.
/// Does not scan or sort the full loop.
inline Result finalizeWrapWindow(MidiEventVec& events,
                                 uint32_t loopLengthTicks,
                                 uint32_t openTailCloseTick = UINT32_MAX,
                                 uint32_t wrapWindow = 768) {
  Result result;
  if (events.empty() || loopLengthTicks == 0) {
    return result;
  }

  const uint32_t window = std::min(wrapWindow, loopLengthTicks);
  const uint32_t tailStart = loopLengthTicks > window ? loopLengthTicks - window : 0;
  const uint32_t headEnd = window;

  const auto inHead = [&](uint32_t tick) { return tick < headEnd; };
  const auto inTail = [&](uint32_t tick) {
    return tick >= tailStart && tick < loopLengthTicks;
  };
  const auto inWindow = [&](uint32_t tick) { return inHead(tick) || inTail(tick); };

  std::unordered_map<NoteKey, size_t, NoteKeyHash> activeTailOnIndex;

  for (size_t i = 0; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (!inWindow(evt.tick)) {
      continue;
    }

    if (evt.isNoteOn()) {
      const NoteKey key = {evt.data.noteData.note, evt.channel};
      if (inTail(evt.tick)) {
        activeTailOnIndex[key] = i;
      }
    } else if (evt.isNoteOff()) {
      const NoteKey key = {evt.data.noteData.note, evt.channel};
      auto it = activeTailOnIndex.find(key);
      if (it != activeTailOnIndex.end()) {
        activeTailOnIndex.erase(it);
      }
    }
  }

  // Wrapped pairs: tail note-on with head note-off (off tick < on tick).
  for (auto it = activeTailOnIndex.begin(); it != activeTailOnIndex.end();) {
    const MidiEvent& noteOn = events[it->second];
    bool wrapped = false;
    for (const MidiEvent& evt : events) {
      if (!evt.isNoteOff() || evt.channel != noteOn.channel ||
          evt.data.noteData.note != noteOn.data.noteData.note) {
        continue;
      }
      if (!inHead(evt.tick)) {
        continue;
      }
      if (noteOn.tick > evt.tick &&
          (noteOn.tick - evt.tick) > (loopLengthTicks / 2)) {
        wrapped = true;
        break;
      }
    }
    if (wrapped) {
      it = activeTailOnIndex.erase(it);
    } else {
      ++it;
    }
  }

  eraseTailOnsWithLaterOff(activeTailOnIndex, events);

  uint32_t closeTick = loopLengthTicks - 1;
  if (openTailCloseTick != UINT32_MAX) {
    closeTick = std::min(openTailCloseTick, loopLengthTicks - 1);
  }

  std::vector<MidiEvent> syntheticOffs;
  syntheticOffs.reserve(activeTailOnIndex.size());

  for (const auto& entry : activeTailOnIndex) {
    const MidiEvent& noteOn = events[entry.second];
    uint32_t offTick = closeTick;
    if (openTailCloseTick != UINT32_MAX && offTick < noteOn.tick) {
      offTick = loopLengthTicks - 1;
    }
    syntheticOffs.push_back(
        MidiEvent::NoteOff(offTick, noteOn.channel, noteOn.data.noteData.note, 0));
  }

  if (syntheticOffs.empty()) {
    return result;
  }

  events.insert(events.end(), syntheticOffs.begin(), syntheticOffs.end());
  std::sort(events.begin(), events.end(), noteEventLess);
  result.syntheticOffsInserted = syntheticOffs.size();
  return result;
}

/// Chunk-store stop path: append synthetic offs without flattening the loop.
inline Result finalizeWrapWindowOnStore(LoopEventStore& store,
                                        uint32_t loopLengthTicks,
                                        uint32_t openTailCloseTick = UINT32_MAX,
                                        uint32_t wrapWindow = 768) {
  Result result;
  if (store.empty() || loopLengthTicks == 0) {
    return result;
  }

  const uint32_t window = std::min(wrapWindow, loopLengthTicks);
  const uint32_t tailStart = loopLengthTicks > window ? loopLengthTicks - window : 0;
  const uint32_t headEnd = window;

  const auto inHead = [&](uint32_t tick) { return tick < headEnd; };
  const auto inTail = [&](uint32_t tick) {
    return tick >= tailStart && tick < loopLengthTicks;
  };
  const auto inWindow = [&](uint32_t tick) { return inHead(tick) || inTail(tick); };

  std::unordered_map<NoteKey, size_t, NoteKeyHash> activeTailOnIndex;
  const size_t eventCount = store.size();

  for (size_t i = 0; i < eventCount; ++i) {
    const MidiEvent& evt = store.at(i);
    if (!inWindow(evt.tick)) {
      continue;
    }

    if (evt.isNoteOn()) {
      const NoteKey key = {evt.data.noteData.note, evt.channel};
      if (inTail(evt.tick)) {
        activeTailOnIndex[key] = i;
      }
    } else if (evt.isNoteOff()) {
      const NoteKey key = {evt.data.noteData.note, evt.channel};
      auto it = activeTailOnIndex.find(key);
      if (it != activeTailOnIndex.end()) {
        activeTailOnIndex.erase(it);
      }
    }
  }

  // Wrapped pairs: tail note-on with head note-off (off tick < on tick).
  for (auto it = activeTailOnIndex.begin(); it != activeTailOnIndex.end();) {
    const MidiEvent& noteOn = store.at(it->second);
    bool wrapped = false;
    for (size_t j = 0; j < eventCount; ++j) {
      const MidiEvent& evt = store.at(j);
      if (!evt.isNoteOff() || evt.channel != noteOn.channel ||
          evt.data.noteData.note != noteOn.data.noteData.note) {
        continue;
      }
      if (!inHead(evt.tick)) {
        continue;
      }
      if (noteOn.tick > evt.tick &&
          (noteOn.tick - evt.tick) > (loopLengthTicks / 2)) {
        wrapped = true;
        break;
      }
    }
    if (wrapped) {
      it = activeTailOnIndex.erase(it);
    } else {
      ++it;
    }
  }

  eraseTailOnsWithLaterOff(activeTailOnIndex, store);

  uint32_t closeTick = loopLengthTicks - 1;
  if (openTailCloseTick != UINT32_MAX) {
    closeTick = std::min(openTailCloseTick, loopLengthTicks - 1);
  }

  size_t inserted = 0;
  for (const auto& entry : activeTailOnIndex) {
    const MidiEvent& noteOn = store.at(entry.second);
    uint32_t offTick = closeTick;
    if (openTailCloseTick != UINT32_MAX && offTick < noteOn.tick) {
      offTick = loopLengthTicks - 1;
    }
    if (store.append(MidiEvent::NoteOff(offTick, noteOn.channel, noteOn.data.noteData.note, 0))) {
      ++inserted;
    }
  }

  result.syntheticOffsInserted = inserted;
  return result;
}

}  // namespace LoopStopFinalize
