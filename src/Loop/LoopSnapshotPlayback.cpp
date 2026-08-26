//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "LoopSnapshotBoxes.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#endif

#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define LOOP_SNAPSHOT_DIAG_FMT(literal) PSTR(literal)
#else
#define LOOP_SNAPSHOT_DIAG_FMT(literal) literal
#endif

namespace {

struct OrderedSnapshotEvent {
  uint32_t canonicalOrder = 0;
  EventId eventId = kInvalidEventId;
  MidiEvent value{};
};

void appendUniquePassId(std::vector<PassId>& ids, PassId id) {
  if (id == kInvalidPassId) {
    return;
  }
  for (const PassId existing : ids) {
    if (existing == id) {
      return;
    }
  }
  ids.push_back(id);
}

void appendUniqueEditPassId(EditPassIdList& ids, EditPassId id) {
  if (id == kInvalidEditPassId) {
    return;
  }
  for (const EditPassId existing : ids) {
    if (existing == id) {
      return;
    }
  }
  ids.push_back(id);
}

bool isRecordPassId(const LoopPasses& passes, PassId id) {
  return passes.hasRecordPass() && passes.recordPass.id == id;
}

void appendChunkListForPass(const LoopPasses& passes, PassId passId,
                            std::vector<const CommittedChunkIdList*>& lists) {
  if (passId == kInvalidPassId) {
    return;
  }
  if (passes.hasRecordPass() && passes.recordPass.id == passId &&
      passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    lists.push_back(&passes.recordPass.committedChunkIds);
    return;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id == passId && pass.state == CapturePassState::Active &&
        !pass.committedChunkIds.empty()) {
      lists.push_back(&pass.committedChunkIds);
      return;
    }
  }
}

void gatherPublishedSnapshotMembership(const LoopSnapshot& snapshot, SessionMidiEventVec& out,
                                       uint32_t windowStart, uint32_t windowLength) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  const uint32_t gatherStartUs = micros();
#endif
  const uint32_t loopLength = snapshot.loopLengthTicks();
  if (loopLength == 0 || windowLength == 0) {
    out.clear();
    return;
  }
  if (windowLength >= loopLength) {
    windowStart = 0;
    windowLength = loopLength;
  }

  const uint32_t windowEnd = windowStart + windowLength;
  const uint32_t lastTick = windowEnd > 0 ? windowEnd - 1 : 0;
  const uint32_t firstBar = windowStart / Config::TICKS_PER_BAR;
  const uint32_t lastBar = lastTick / Config::TICKS_PER_BAR;

  std::vector<OrderedSnapshotEvent> collected;
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    const auto membership = snapshot.eventMembership(bar);
    if (!membership) {
      continue;
    }
    for (const auto& reference : membership->references) {
      if (!reference) {
        continue;
      }
      const uint32_t tick = reference->value.tick;
      if (tick < windowStart || tick >= windowEnd) {
        continue;
      }
      collected.push_back(
          {reference->canonicalOrder, reference->eventId, reference->value});
    }
  }

  std::sort(collected.begin(), collected.end(),
            [](const OrderedSnapshotEvent& left, const OrderedSnapshotEvent& right) {
              if (left.canonicalOrder != right.canonicalOrder) {
                return left.canonicalOrder < right.canonicalOrder;
              }
              return left.eventId < right.eventId;
            });

  out.clear();
  out.reserve(collected.size());
  for (const OrderedSnapshotEvent& event : collected) {
    out.push_back(event.value);
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  const uint32_t gatherUs = micros() - gatherStartUs;
  if (gatherUs >= 100u) {
    char line[192];
    snprintf(line, sizeof(line),
             LOOP_SNAPSHOT_DIAG_FMT("#CAP,%lu,DIAG,cer_snapshot,us=%lu,ev=%u,win=%lu,bars=%u"),
             static_cast<unsigned long>(micros()), static_cast<unsigned long>(gatherUs),
             static_cast<unsigned>(out.size()), static_cast<unsigned long>(windowLength),
             static_cast<unsigned>(lastBar - firstBar + 1u));
    DebugSessionCapture::appendCaptureTextLine(line);
  }
#endif
}

}  // namespace

LOOP_COLD_MEM bool Loop::tryGatherPublishedSnapshotEventsInWindow(
    SessionMidiEventVec& out, uint32_t windowStart, uint32_t windowLength) const {
  const ImmutableLoopSnapshotRef snapshot = publishedSnapshot();
  if (!snapshot) {
    return false;
  }

  gatherPublishedSnapshotMembership(*snapshot, out, windowStart, windowLength);
  if (publishedSnapshotIsCurrent()) {
    return true;
  }

  std::vector<PassId> unpublishedPassIds;
  if (hasOverdubSession()) {
    for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionPassIds_.size(); ++i) {
      const PassId passId = overdubSessionPassIds_[i];
      if (!isRecordPassId(passes, passId)) {
        appendUniquePassId(unpublishedPassIds, passId);
      }
    }
    if (!isRecordPassId(passes, lastCommittedPassId_)) {
      appendUniquePassId(unpublishedPassIds, lastCommittedPassId_);
    }
  }
  if (pendingSnapshotPublishBox_) {
    const PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
    for (const PassId passId : box.queuedPassIds) {
      if (!isRecordPassId(passes, passId)) {
        appendUniquePassId(unpublishedPassIds, passId);
      }
    }
    for (const PassId passId : box.appliedPassIds) {
      if (!isRecordPassId(passes, passId)) {
        appendUniquePassId(unpublishedPassIds, passId);
      }
    }
    if (!isRecordPassId(passes, box.constructionPassId)) {
      appendUniquePassId(unpublishedPassIds, box.constructionPassId);
    }
  }

  std::vector<const CommittedChunkIdList*> unpublishedLists;
  for (const PassId passId : unpublishedPassIds) {
    appendChunkListForPass(passes, passId, unpublishedLists);
  }

  SessionMidiEventVec unpublishedEvents;
  if (!unpublishedLists.empty()) {
    CommittedEventRange::inWindow(unpublishedLists.data(), unpublishedLists.size(),
                                  loopLengthTicks, windowStart, windowLength)
        .appendTo(unpublishedEvents);
  }

  EditPassIdList unpublishedCompanionIds;
  if (hasOverdubSession()) {
    for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionCompanionIds_.size();
         ++i) {
      for (const EditPassId companionId : overdubSessionCompanionIds_[i]) {
        appendUniqueEditPassId(unpublishedCompanionIds, companionId);
      }
    }
  }
  if (pendingSnapshotPublishBox_) {
    for (const EditPassId companionId : pendingSnapshotPublishBox_->companionIds) {
      appendUniqueEditPassId(unpublishedCompanionIds, companionId);
    }
  }

  EditPassVec unpublishedCompanions;
  for (const EditPassId companionId : unpublishedCompanionIds) {
    for (const EditPass& row : passes.editPasses) {
      if (row.id == companionId && row.state == EditPassState::Active &&
          row.passType == EditPassType::Note) {
        unpublishedCompanions.push_back(row);
        break;
      }
    }
  }

  if (!unpublishedEvents.empty()) {
    out.insert(out.end(), unpublishedEvents.begin(), unpublishedEvents.end());
    std::sort(out.begin(), out.end(),
              [](const MidiEvent& left, const MidiEvent& right) { return left.tick < right.tick; });
  }
  if (!unpublishedCompanions.empty()) {
    applyNoteEditPassSequence(out, unpublishedCompanions, loopLengthTicks);
  }
  return true;
}
