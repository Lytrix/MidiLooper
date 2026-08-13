//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/LoopTickNormalize.h"

#include <algorithm>
#include <climits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "NoteEditGeometryApplyWrap.h"
#include "Utils/NoteUtils.h"

namespace LoopTickNormalize {

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

void expandRange(uint32_t loopLength, uint32_t wrapWindowTicks, uint32_t rangeStart,
                 uint32_t rangeEnd, uint32_t& outStart, uint32_t& outEnd) {
  const uint32_t margin = std::min(wrapWindowTicks, loopLength);
  outStart = rangeStart > margin ? rangeStart - margin : 0;
  outEnd = std::min(rangeEnd + margin, loopLength + margin);
}

bool isAlreadyLinearPair(uint32_t onTick, uint32_t offTick, uint32_t loopLength) {
  if (offTick >= onTick) {
    return true;
  }
  if (loopLength == 0) {
    return false;
  }
  return offTick >= loopLength;
}

uint32_t tickRangeWinStart(const NormalizeScope& scope, uint32_t loopLength,
                           uint32_t wrapWindowTicks) {
  uint32_t winStart = 0;
  uint32_t winEnd = 0;
  expandRange(loopLength, wrapWindowTicks, scope.rangeStart, scope.rangeEnd, winStart, winEnd);
  return winStart;
}

bool noteOnParticipates(const MidiEvent& onEvt, const NormalizeScope& scope, uint32_t loopLength,
                        uint32_t wrapWindowTicks) {
  if (!onEvt.isNoteOn() || onEvt.data.noteData.velocity == 0) {
    return false;
  }
  if (onEvt.tick >= loopLength) {
    return false;
  }
  switch (scope.kind) {
    case NormalizeScope::Kind::EntireStore:
      return true;
    case NormalizeScope::Kind::TickRange:
      return onEvt.tick >= tickRangeWinStart(scope, loopLength, wrapWindowTicks);
    case NormalizeScope::Kind::NoteIds:
      return onEvt.noteId != kInvalidNoteId && scope.closureNoteIds.count(onEvt.noteId) > 0;
  }
  return false;
}

NormalizeResult normalizeStore(MidiEventVec& events, uint32_t loopLength, const NormalizeScope& scope,
                               const NormalizeOptions& options) {
  NormalizeResult result;
  if (events.empty() || loopLength == 0) {
    return result;
  }

  const uint32_t wrapWindowTicks = options.wrapWindowTicks;

  std::sort(events.begin(), events.end(), eventLess);
  std::vector<bool> remove(events.size(), false);

  // Pass 1: merge legacy head/tail wrap pairs into one linear off.
  // Skip when both events are tagged and the noteIds disagree (193838 mover off).
  for (size_t onIdx = 0; onIdx < events.size(); ++onIdx) {
    const MidiEvent& onEvt = events[onIdx];
    if (remove[onIdx] || !noteOnParticipates(onEvt, scope, loopLength, wrapWindowTicks)) {
      continue;
    }

    for (size_t offIdx = 0; offIdx < events.size(); ++offIdx) {
      if (remove[offIdx] || offIdx == onIdx || !events[offIdx].isNoteOff()) {
        continue;
      }
      const MidiEvent& offEvt = events[offIdx];
      if (offEvt.channel != onEvt.channel || offEvt.data.noteData.note != onEvt.data.noteData.note) {
        continue;
      }
      if (onEvt.noteId != kInvalidNoteId && offEvt.noteId != kInvalidNoteId &&
          onEvt.noteId != offEvt.noteId) {
        continue;
      }
      if (!NoteUtils::isHeadTailWrappedPair(onEvt.tick, offEvt.tick, loopLength, wrapWindowTicks)) {
        continue;
      }
      if (!NoteUtils::wrapPairIsUnblocked(events, offEvt.tick, onEvt.tick,
                                          onEvt.data.noteData.note, onEvt.channel)) {
        continue;
      }

      const uint32_t linearOff =
          onEvt.tick + NoteEditGeometryApply::calculateNoteLength(onEvt.tick, offEvt.tick, loopLength);
      events[offIdx].tick = linearOff;
      ++result.wrapPairsMerged;

      for (size_t j = 0; j < events.size(); ++j) {
        if (j == offIdx || remove[j] || !events[j].isNoteOff()) {
          continue;
        }
        if (events[j].channel == onEvt.channel && events[j].data.noteData.note == onEvt.data.noteData.note &&
            events[j].tick == loopLength - 1) {
          remove[j] = true;
          ++result.synthOffsPromoted;
        }
      }
      break;
    }
  }

  // Pass 2: promote lone synth loop-end off to linear off.
  for (size_t onIdx = 0; onIdx < events.size(); ++onIdx) {
    if (remove[onIdx] || !noteOnParticipates(events[onIdx], scope, loopLength, wrapWindowTicks)) {
      continue;
    }
    const MidiEvent& onEvt = events[onIdx];

    size_t synthIdx = events.size();
    bool hasLinearOff = false;
    for (size_t i = 0; i < events.size(); ++i) {
      if (remove[i] || !events[i].isNoteOff()) {
        continue;
      }
      if (events[i].channel != onEvt.channel || events[i].data.noteData.note != onEvt.data.noteData.note) {
        continue;
      }
      if (events[i].tick == loopLength - 1) {
        synthIdx = i;
      } else if (isAlreadyLinearPair(onEvt.tick, events[i].tick, loopLength)) {
        hasLinearOff = true;
      }
    }

    if (synthIdx == events.size() || hasLinearOff) {
      continue;
    }

    // Long in-loop notes (e.g. moved wrap note collapsed to off@loopLength-1) are already
    // linear; only promote legacy short open-tail synth offs near the loop boundary.
    const uint32_t spanToLoopEnd = loopLength - onEvt.tick;
    if (spanToLoopEnd > kSynthOpenTailMaxSpanTicks) {
      continue;
    }

    const uint32_t linearOff = onEvt.tick + spanToLoopEnd;
    events[synthIdx].tick = linearOff;
    ++result.synthOffsPromoted;
  }

  // Pass 3: close open notes in scope (macro / capture only).
  if (options.closeOpenTails) {
    std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, PairHash> openOn;
    for (size_t i = 0; i < events.size(); ++i) {
      if (remove[i]) {
        continue;
      }
      if (events[i].isNoteOn() && events[i].data.noteData.velocity > 0) {
        openOn[{events[i].data.noteData.note, events[i].channel}] = i;
      } else if (events[i].isNoteOff()) {
        openOn.erase({events[i].data.noteData.note, events[i].channel});
      }
    }

    std::vector<MidiEvent> appended;
    for (const auto& entry : openOn) {
      const MidiEvent& onEvt = events[entry.second];
      if (!noteOnParticipates(onEvt, scope, loopLength, wrapWindowTicks)) {
        continue;
      }
      uint32_t closeTick = loopLength - 1;
      if (options.openTailCloseTick != UINT32_MAX) {
        closeTick = std::min(options.openTailCloseTick, loopLength - 1);
        if (closeTick < onEvt.tick) {
          closeTick = loopLength - 1;
        }
      }
      const uint32_t linearOff =
          onEvt.tick + NoteEditGeometryApply::calculateNoteLength(onEvt.tick, closeTick, loopLength);
      appended.push_back(
          MidiEvent::NoteOff(linearOff, onEvt.channel, onEvt.data.noteData.note, 0));
      ++result.openTailsClosed;
    }

    MidiEventVec kept;
    kept.reserve(events.size() - remove.size() + appended.size());
    for (size_t i = 0; i < events.size(); ++i) {
      if (!remove[i]) {
        kept.push_back(events[i]);
      }
    }
    kept.insert(kept.end(), appended.begin(), appended.end());
    events.swap(kept);
    std::sort(events.begin(), events.end(), eventLess);
  } else {
    MidiEventVec kept;
    kept.reserve(events.size() - remove.size());
    for (size_t i = 0; i < events.size(); ++i) {
      if (!remove[i]) {
        kept.push_back(events[i]);
      }
    }
    events.swap(kept);
    std::sort(events.begin(), events.end(), eventLess);
  }

  return result;
}

}  // namespace

NormalizeScope NormalizeScope::entireStore() {
  NormalizeScope scope;
  scope.kind = Kind::EntireStore;
  return scope;
}

NormalizeScope NormalizeScope::tickRange(uint32_t rangeStart, uint32_t rangeEnd,
                                         uint32_t /*wrapWindowTicks*/) {
  NormalizeScope scope;
  scope.kind = Kind::TickRange;
  scope.rangeStart = rangeStart;
  scope.rangeEnd = rangeEnd;
  return scope;
}

NormalizeScope NormalizeScope::noteIds(std::unordered_set<NoteId> ids) {
  NormalizeScope scope;
  scope.kind = Kind::NoteIds;
  scope.closureNoteIds = std::move(ids);
  return scope;
}

NormalizeResult normalize(MidiEventVec& events, uint32_t loopLength, NormalizeScope scope,
                          NormalizeOptions options) {
  if (loopLength == 0 || events.empty()) {
    return {};
  }
  return normalizeStore(events, loopLength, scope, options);
}

NormalizeResult normalizeWindow(MidiEventVec& events, uint32_t loopLength, uint32_t rangeStart,
                                uint32_t rangeEnd, uint32_t wrapWindowTicks,
                                uint32_t openTailCloseTick) {
  NormalizeOptions options;
  options.wrapWindowTicks = wrapWindowTicks;
  options.closeOpenTails = true;
  options.openTailCloseTick = openTailCloseTick;
  return normalize(events, loopLength, NormalizeScope::tickRange(rangeStart, rangeEnd, wrapWindowTicks),
                   options);
}

NormalizeResult normalizeAll(MidiEventVec& events, uint32_t loopLength, uint32_t wrapWindowTicks,
                             uint32_t openTailCloseTick) {
  NormalizeOptions options;
  options.wrapWindowTicks = wrapWindowTicks;
  options.closeOpenTails = true;
  options.openTailCloseTick = openTailCloseTick;
  return normalize(events, loopLength, NormalizeScope::entireStore(), options);
}

}  // namespace LoopTickNormalize
