//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "EditSessionAction.h"
#include "EditSessionInteraction.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "OverlapCandidateLookup.h"
#include "OverlapNoteIdObservation.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/LoopMem.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"
#include "Utils/RuntimeTimingTelemetry.h"

#include <algorithm>
#include <cstdio>

#if defined(PIO_UNIT_TEST_NATIVE)
#include <cstdint>
inline uint32_t micros() { return 0; }
#else
#include <Arduino.h>
#endif

// Keep diagnostic format strings out of DTCM .rodata — see RT_TAG in RuntimeTimingTelemetry.
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define LOOP_NOTECHANGE_DIAG_FMT(literal) PSTR(literal)
#else
#define LOOP_NOTECHANGE_DIAG_FMT(literal) literal
#endif

namespace {

bool linearSoundingSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                        uint32_t& linearStart, uint32_t& linearEnd) {
  return OverlapNoteIdObservation::linearSoundingSpan(startTick, endTick, loopLength, linearStart,
                                                      linearEnd);
}

bool displayNotePresentAtHold(uint32_t startTick, uint32_t endTick, uint32_t holdStart,
                               uint32_t loopLength) {
  return OverlapNoteIdObservation::displayNotePresentAtHold(startTick, endTick, holdStart,
                                                            loopLength);
}

bool existingNoteOverlapsIncomingHold(uint32_t existingStart, uint32_t existingEnd,
                                      uint32_t incomingStart, uint32_t incomingEnd,
                                      uint32_t loopLength) {
  uint32_t existingLinearStart = 0;
  uint32_t existingLinearEnd = 0;
  uint32_t incomingLinearStart = 0;
  uint32_t incomingLinearEnd = 0;
  if (!linearSoundingSpan(existingStart, existingEnd, loopLength, existingLinearStart,
                          existingLinearEnd)) {
    return false;
  }
  if (!linearSoundingSpan(incomingStart, incomingEnd, loopLength, incomingLinearStart,
                          incomingLinearEnd)) {
    return false;
  }
  const bool direct =
      existingLinearStart < incomingLinearEnd && existingLinearEnd > incomingLinearStart;
  const bool existingShifted = existingLinearStart + loopLength < incomingLinearEnd &&
                               existingLinearEnd + loopLength > incomingLinearStart;
  const bool incomingShifted = existingLinearStart < incomingLinearEnd + loopLength &&
                               existingLinearEnd > incomingLinearStart + loopLength;
  return direct || existingShifted || incomingShifted;
}

LOOP_COLD_MEM void collectPendingSortedNoteIds(const NoteUtils::DisplayNoteVec& notes,
                                               std::vector<NoteId>& out) {
  out.clear();
  out.reserve(notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId != kInvalidNoteId) {
      out.push_back(note.noteId);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

LOOP_COLD_MEM bool insertSortedUniquePendingNoteId(std::vector<NoteId>& ids, NoteId noteId) {
  const auto it = std::lower_bound(ids.begin(), ids.end(), noteId);
  if (it != ids.end() && *it == noteId) {
    return false;
  }
  ids.insert(it, noteId);
  return true;
}

bool composedShortenDegradesToHide(uint32_t startTick, uint32_t endTick) {
  if (endTick < startTick) {
    return false;
  }
  if (endTick == startTick) {
    return true;
  }
  return noteMinLengthRemoveEnabled && (endTick - startTick) < noteMinLengthTicks;
}

void composeRestrictiveSourceTransform(PendingNoteChange& existing, const PendingNoteChange& incoming) {
  if (existing.kind == PendingNoteChangeKind::Hide) {
    return;
  }
  if (incoming.kind == PendingNoteChangeKind::Hide) {
    existing = incoming;
    return;
  }
  const uint32_t composedEnd =
      existing.endTick < incoming.endTick ? existing.endTick : incoming.endTick;
  existing.endTick = composedEnd;
  if (composedShortenDegradesToHide(existing.startTick, existing.endTick)) {
    existing.kind = PendingNoteChangeKind::Hide;
  }
}

void upsertSourceTransform(PendingNoteChangeVec& pending, const PendingNoteChange& change) {
  if (change.kind != PendingNoteChangeKind::Shorten && change.kind != PendingNoteChangeKind::Hide) {
    pending.push_back(change);
    return;
  }
  PendingNoteChange incoming = change;
  if (incoming.kind == PendingNoteChangeKind::Shorten &&
      composedShortenDegradesToHide(incoming.startTick, incoming.endTick)) {
    incoming.kind = PendingNoteChangeKind::Hide;
  }
  for (PendingNoteChange& existing : pending) {
    if ((existing.kind == PendingNoteChangeKind::Shorten ||
         existing.kind == PendingNoteChangeKind::Hide) &&
        existing.noteId == incoming.noteId) {
      composeRestrictiveSourceTransform(existing, incoming);
      return;
    }
  }
  pending.push_back(incoming);
}

bool noteStartStrictlyInsideIncoming(uint32_t noteStart, uint32_t incomingStart,
                                     uint32_t incomingEnd, uint32_t loopLength) {
  if (loopLength == 0) {
    return false;
  }
  if (incomingEnd >= incomingStart) {
    return noteStart > incomingStart && noteStart < incomingEnd;
  }
  return noteStart > incomingStart || noteStart < incomingEnd;
}

LOOP_COLD_MEM void insertOrReplaceSourceTransformSorted(PendingNoteChangeVec& transforms,
                                                        const PendingNoteChange& change) {
  const auto it = std::lower_bound(
      transforms.begin(), transforms.end(), change.noteId,
      [](const PendingNoteChange& existing, NoteId noteId) {
        return existing.noteId < noteId;
      });
  if (it != transforms.end() && it->noteId == change.noteId) {
    *it = change;
    return;
  }
  transforms.insert(it, change);
}

LOOP_COLD_MEM const PendingNoteChange* findSourceTransformByNoteIdSorted(
    const PendingNoteChangeVec& transforms, NoteId noteId) {
  const auto it = std::lower_bound(
      transforms.begin(), transforms.end(), noteId,
      [](const PendingNoteChange& existing, NoteId id) {
        return existing.noteId < id;
      });
  if (it == transforms.end() || it->noteId != noteId) {
    return nullptr;
  }
  return &(*it);
}

LOOP_COLD_MEM bool hideMatchesDisplayNote(const NoteUtils::DisplayNote& note,
                                          const PendingNoteChange& hide, uint32_t loopLength) {
  if (hide.noteId != kInvalidNoteId && note.noteId == hide.noteId) {
    return true;
  }
  if (note.note != hide.pitch || loopLength == 0) {
    return false;
  }
  return existingNoteOverlapsIncomingHold(note.startTick, note.endTick, hide.startTick,
                                          hide.endTick, loopLength);
}

LOOP_COLD_MEM bool sourceTransformMarksPitchStart(const PendingNoteChangeVec& transforms,
                                                  uint8_t pitch, uint32_t startTick) {
  for (const PendingNoteChange& change : transforms) {
    if ((change.kind == PendingNoteChangeKind::Hide ||
         change.kind == PendingNoteChangeKind::Shorten) &&
        change.pitch == pitch && change.startTick == startTick) {
      return true;
    }
  }
  return false;
}

/// After Hide or Shorten apply, drop a longer same-pitch+start sibling in the committed
/// prefix so a record remnant cannot sit beside the shortened row (`201956` Hide;
/// `123236` Shorten 63@768 len 95 vs 576). Only pitch+starts named by those
/// transforms are touched — not a general shortest-wins rule (`150650` long sealed
/// hold must not lose to a shorter stale row).
LOOP_COLD_MEM void dropLongerSamePitchStartSiblingsMarkedBySourceTransform(
    NoteUtils::DisplayNoteVec& notes, const PendingNoteChangeVec& transforms,
    size_t liveCaptureBegin) {
  if (notes.size() < 2) {
    return;
  }
  bool anyNamedTransform = false;
  for (const PendingNoteChange& change : transforms) {
    if (change.kind == PendingNoteChangeKind::Hide ||
        change.kind == PendingNoteChangeKind::Shorten) {
      anyNamedTransform = true;
      break;
    }
  }
  if (!anyNamedTransform) {
    return;
  }
  const size_t committedEnd = liveCaptureBegin < notes.size() ? liveCaptureBegin : notes.size();
  if (committedEnd < 2) {
    return;
  }
  NoteUtils::DisplayNoteVec kept;
  kept.reserve(notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    const NoteUtils::DisplayNote& note = notes[i];
    if (i >= committedEnd || note.endTick < note.startTick ||
        !sourceTransformMarksPitchStart(transforms, note.note, note.startTick)) {
      kept.push_back(note);
      continue;
    }
    bool dominated = false;
    for (size_t j = 0; j < committedEnd; ++j) {
      const NoteUtils::DisplayNote& other = notes[j];
      if (other.note != note.note || other.startTick != note.startTick ||
          other.endTick < other.startTick) {
        continue;
      }
      if (other.endTick < note.endTick || (other.endTick == note.endTick && j < i)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      kept.push_back(note);
    }
  }
  notes.swap(kept);
}

LOOP_COLD_MEM void applyPendingHideAndShortenToNotes(NoteUtils::DisplayNoteVec& notes,
                                                     const PendingNoteChangeVec& pending,
                                                     uint32_t loopLength,
                                                     bool matchHideByOverlap,
                                                     size_t liveCaptureBegin,
                                                     bool dropSameStartLongerAfterSourceTransform) {
  PendingNoteChangeVec transforms;
  for (const PendingNoteChange& change : pending) {
    if ((change.kind == PendingNoteChangeKind::Shorten ||
         change.kind == PendingNoteChangeKind::Hide) &&
        change.noteId != kInvalidNoteId) {
      // Keep source transforms sorted by NoteId so stop sealing does bounded lookup work.
      insertOrReplaceSourceTransformSorted(transforms, change);
    }
  }
  if (transforms.empty() || notes.empty()) {
    return;
  }

  NoteUtils::DisplayNoteVec updated;
  updated.reserve(notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    const NoteUtils::DisplayNote& note = notes[i];
    // A pending Hide/Shorten applies to any painted note carrying the
    // target NoteId, regardless of whether that note came from the
    // committed prefix or sealed/closed capture suffix.
    // The only display note excluded from the pending transform is an
    // open capture hold whose end is intentionally extended by
    // applyCapturePlayheadTails().
    if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(note.startTick, note.endTick)) {
      updated.push_back(note);
      continue;
    }
    const PendingNoteChange* transform =
        findSourceTransformByNoteIdSorted(transforms, note.noteId);
    if (transform == nullptr && matchHideByOverlap && loopLength != 0 &&
        i < liveCaptureBegin) {
      for (const PendingNoteChange& candidate : transforms) {
        if (candidate.kind == PendingNoteChangeKind::Hide &&
            hideMatchesDisplayNote(note, candidate, loopLength)) {
          transform = &candidate;
          break;
        }
      }
    }
    if (transform == nullptr) {
      updated.push_back(note);
      continue;
    }
    if (transform->kind == PendingNoteChangeKind::Hide) {
      continue;
    }
    NoteUtils::DisplayNote revised = note;
    revised.startTick = transform->startTick;
    revised.endTick = transform->endTick;
    updated.push_back(revised);
  }
  notes.swap(updated);
  if (dropSameStartLongerAfterSourceTransform) {
    dropLongerSamePitchStartSiblingsMarkedBySourceTransform(notes, transforms, liveCaptureBegin);
  }
}

}  // namespace

LOOP_COLD_MEM void Loop::ensureOverdubSourceNotesForHold(uint32_t holdPhaseTick, uint8_t pitch,
                                                        NoteUtils::DisplayNoteVec* newlyMergedPitchNotes,
                                                        bool presentAtHoldOnly) {
  if (newlyMergedPitchNotes != nullptr) {
    newlyMergedPitchNotes->clear();
  }
  if (!overdubSourceViewEstablished_ || overdubSourceViewLoopLengthTicks_ == 0) {
    return;
  }
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
  resolveOverdubSourceWindow(holdPhaseTick, windowStart, windowLength);
  if (!overdubSourceSpanCacheReady()) {
    rebuildOverdubSourceSpanCache(windowStart, windowLength);
  }
  if (overdubSourceSpanCacheNotes_.empty()) {
    return;
  }

  auto filterAndMergeCandidates = [&](const NoteUtils::DisplayNoteVec& candidates,
                                      NoteUtils::DisplayNoteVec& toMerge) {
    toMerge.clear();
    for (const NoteUtils::DisplayNote& note : candidates) {
      if (note.note != pitch || note.noteId == kInvalidNoteId) {
        continue;
      }
      if (presentAtHoldOnly) {
        if (!displayNotePresentAtHold(note.startTick, note.endTick, holdPhaseTick, loopLen)) {
          continue;
        }
      } else if (!DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick,
                                                            windowStart, windowLength,
                                                            loopLen)) {
        continue;
      }
      if (!committedPlaybackNoteOnIdentityValid(note.noteId)) {
        continue;
      }
      bool already = false;
      for (const NoteUtils::DisplayNote& existing : overdubSourceViewNotes_) {
        if (existing.noteId == note.noteId) {
          already = true;
          break;
        }
      }
      if (!already) {
        toMerge.push_back(note);
      }
    }
  };
  NoteUtils::DisplayNoteVec toMerge;
  filterAndMergeCandidates(overdubSourceSpanCacheNotes_, toMerge);
  mergeDisplayNotesIntoOverdubSourceView(toMerge);
  if (newlyMergedPitchNotes != nullptr) {
    *newlyMergedPitchNotes = toMerge;
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  char line[224];
  snprintf(line, sizeof(line),
           LOOP_NOTECHANGE_DIAG_FMT("#CAP,%lu,DIAG,lcr,src,why=hold,from=cache,pitch=%u,merged=%u,"
                                    "notes=%u,cache=%u,bars=%u"),
           static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
           static_cast<unsigned>(toMerge.size()),
           static_cast<unsigned>(overdubSourceViewNotes_.size()),
           static_cast<unsigned>(overdubSourceSpanCacheNotes_.size()),
           static_cast<unsigned>(kOverdubSourceWindowBars));
  DebugSessionCapture::appendCaptureTextLine(line);
#endif
}

LOOP_COLD_MEM void Loop::collectOverdubSourceHoldParticipantIds(uint32_t holdPhaseTick, uint8_t pitch,
                                                               OverlapNoteIdSet& out) const {
  out.clear();
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  if (!overdubSourceViewEstablished_ || loopLen == 0) {
    return;
  }
  for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (!displayNotePresentAtHold(note.startTick, note.endTick, holdPhaseTick, loopLen)) {
      continue;
    }
    if (!committedPlaybackNoteOnIdentityValid(note.noteId)) {
      continue;
    }
    (void)out.insert(note.noteId);
  }
}

LOOP_COLD_MEM bool Loop::tryCollectPreparedPresentNoteIdsAtTick(uint32_t tick, uint8_t pitch,
                                                               OverlapNoteIdSet& out) const {
  if (!LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
          tick, pitch, playbackRevision, loopLengthTicks, out)) {
    return false;
  }
  if (committedPlaybackMergedForIdentity_ == nullptr) {
    return true;
  }
  OverlapNoteIdSet filtered;
  for (size_t i = 0; i < out.size(); ++i) {
    const NoteId id = out.at(i);
    if (committedPlaybackNoteOnIdentityValid(id)) {
      (void)filtered.insert(id);
    }
  }
  out = filtered;
  return true;
}

LOOP_COLD_MEM void Loop::collectOverdubNoteOnParticipantIds(uint8_t pitch, uint8_t channel,
                                                            const ActiveNoteLedger& ledger,
                                                            OverlapNoteIdSet& out) const {
  out.clear();
  ledger.forEachActive([&](uint8_t entryChannel, uint8_t entryNote,
                           const ActiveNoteLedger::Entry& entry) {
    if (entryChannel != channel || entryNote != pitch) {
      return;
    }
    (void)out.insert(entry.noteId);
  });
}

LOOP_COLD_MEM void Loop::accumulatePendingNoteChangesFromSourceNotes(
    const NoteUtils::DisplayNoteVec& sourceNotes,
                                                       uint8_t channel, uint8_t pitch,
                                                       uint8_t velocity, uint32_t startTick,
                                                       uint32_t endTick, NoteId incomingNoteId) {
  const NoteId causingId =
      (incomingNoteId != kInvalidNoteId) ? incomingNoteId : allocateNoteId();

  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_ != 0 ? overdubSourceViewLoopLengthTicks_
                                                                 : loopLengthTicks;

  BaselineMap baseline;
  EditedGeometry edited{};
  EditedNoteSpan causingSpan{};
  causingSpan.noteId = causingId;
  causingSpan.span = NoteBaseline{pitch, velocity, startTick, endTick};
  edited.causingSpans.push_back(causingSpan);

  std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs;
#if RUNTIME_TIMING_ENABLED
  const uint32_t pairStartUs = micros();
#endif
  for (const NoteUtils::DisplayNote& note : sourceNotes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId || note.noteId == causingId) {
      continue;
    }
    if (!existingNoteOverlapsIncomingHold(note.startTick, note.endTick, startTick, endTick,
                                          loopLen)) {
      continue;
    }
    pairs.push_back(CausingTargetPair{causingId, note.noteId});
    baseline[note.noteId] = NoteBaseline{note.note, note.velocity, note.startTick, note.endTick};
  }
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_ADD_NOTE_PAIR(micros() - pairStartUs);
#endif

  if (!pairs.empty()) {
    const auto interactions = analyzeEditSessionInteractions(pairs, edited, baseline);
    const EditSessionInteractionsByTarget grouped =
        groupEditSessionInteractionsByTarget(interactions);
    for (const TargetNoteInteractionGroup& group : grouped.groups) {
      const auto baselineIt = baseline.find(group.targetNoteId);
      if (baselineIt == baseline.end()) {
        continue;
      }
      const NoteBaseline& sourceBaseline = baselineIt->second;
      // Shared NoteMinLength globals (same as NOTE_EDIT) — no overdub-specific floor.
      const ConstrainedNoteGeometry geometry = resolveConstrainedGeometry(
          group.targetNoteId, sourceBaseline, group.incoming, loopLen, noteMinLengthTicks,
          noteMinLengthRemoveEnabled);

      PendingNoteChange transform{};
      transform.noteId = group.targetNoteId;
      transform.channel = channel;
      transform.pitch = sourceBaseline.pitch;
      transform.velocity = sourceBaseline.velocity;
      transform.startTick = geometry.startTick;
      transform.endTick = geometry.endTick;

      if (!geometry.visible) {
        bool completeCover = false;
        bool overlapNoteOn = false;
        uint32_t occupyEndInclusive = sourceBaseline.endTick;
        for (const EditSessionInteraction& interaction : group.incoming) {
          if (interaction.type == InteractionType::CompleteCover) {
            completeCover = true;
            break;
          }
          if (interaction.type == InteractionType::OverlapNoteOn) {
            overlapNoteOn = true;
            if (interaction.causingSpan.endTick > 0) {
              const uint32_t candidate = interaction.causingSpan.endTick - 1;
              if (candidate < occupyEndInclusive) {
                occupyEndInclusive = candidate;
              }
            }
          }
        }
        const bool occupyShortenFits =
            !completeCover && overlapNoteOn && occupyEndInclusive > sourceBaseline.startTick &&
            !(noteMinLengthRemoveEnabled &&
              (occupyEndInclusive - sourceBaseline.startTick) < noteMinLengthTicks);
        if (occupyShortenFits) {
          transform.kind = PendingNoteChangeKind::Shorten;
          transform.startTick = sourceBaseline.startTick;
          transform.endTick = occupyEndInclusive;
        } else {
          transform.kind = PendingNoteChangeKind::Hide;
          transform.startTick = sourceBaseline.startTick;
          transform.endTick = sourceBaseline.endTick;
        }
        upsertSourceTransform(pendingNoteChanges_, transform);
        continue;
      }
      if (geometry.startTick != sourceBaseline.startTick ||
          geometry.endTick != sourceBaseline.endTick) {
        transform.kind = PendingNoteChangeKind::Shorten;
        upsertSourceTransform(pendingNoteChanges_, transform);
      }
    }
  }
}

LOOP_COLD_MEM bool Loop::accumulatePendingNoteChangesForIncomingNote(
    uint8_t channel, uint8_t pitch, uint8_t velocity, uint32_t startTick, uint32_t endTick,
    NoteId incomingNoteId, const OverlapNoteIdSet& overlapNoteIds,
    const OverlapNoteIdSet* occupancyResolvedNoteIds) {
  if (!overdubSourceViewEstablished_ || overdubSourceViewLoopLengthTicks_ == 0) {
    return false;
  }
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  if (endTick == startTick || loopLen == 0) {
    return false;
  }

  const bool wrapCrossing = endTick < startTick;
  if (!wrapCrossing && startTick >= endTick) {
    return false;
  }
  if (wrapCrossing && startTick >= loopLen) {
    return false;
  }

#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
  ++overlapHoldTotals_.noteOffs;
  const uint32_t idCount = static_cast<uint32_t>(overlapNoteIds.size());
  if (idCount > overlapHoldTotals_.maxIds) {
    overlapHoldTotals_.maxIds = idCount;
  }
  if (overlapNoteIds.overflowed()) {
    ++overlapHoldTotals_.overflows;
  }
#endif

  const bool noteOnAlreadyResolvedOccupancy = occupancyResolvedNoteIds != nullptr;
  auto occupancyAlreadyResolved = [&](NoteId noteId) -> bool {
    if (noteId == kInvalidNoteId || occupancyResolvedNoteIds == nullptr) {
      return false;
    }
    return occupancyResolvedNoteIds->contains(noteId);
  };

  OverlapNoteIdSet effectiveOverlapNoteIds;
  for (size_t i = 0; i < overlapNoteIds.size(); ++i) {
    const NoteId id = overlapNoteIds.at(i);
    if (occupancyAlreadyResolved(id)) {
      continue;
    }
    (void)effectiveOverlapNoteIds.insert(id);
  }

  auto collectGeometricIdsForSegment = [&](uint32_t segStart, uint32_t segEnd) {
    if (segStart >= segEnd) {
      return;
    }
    for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
      if (note.note != pitch || note.noteId == kInvalidNoteId) {
        continue;
      }
      if (occupancyAlreadyResolved(note.noteId)) {
        continue;
      }
      if (noteOnAlreadyResolvedOccupancy &&
          !noteStartStrictlyInsideIncoming(note.startTick, startTick, endTick, loopLen)) {
        continue;
      }
      if (!committedPlaybackNoteOnIdentityValid(note.noteId)) {
        continue;
      }
      if (!existingNoteOverlapsIncomingHold(note.startTick, note.endTick, segStart, segEnd,
                                            loopLen)) {
        continue;
      }
      (void)effectiveOverlapNoteIds.insert(note.noteId);
    }
  };

  const bool longLoop = loopLen > overdubSourceWindowLengthTicks();
  auto sourceViewHasNoteId = [&](NoteId id) -> bool {
    for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
      if (note.noteId == id) {
        return true;
      }
    }
    return false;
  };
  auto occupyIdsLackSourceRows = [&]() -> bool {
    for (size_t i = 0; i < overlapNoteIds.size(); ++i) {
      if (!sourceViewHasNoteId(overlapNoteIds.at(i))) {
        return true;
      }
    }
    return false;
  };
  auto maybeEnsureHoldSegmentInSourceView = [&](uint32_t holdPhaseTick) {
    if (!longLoop) {
      return;
    }
    if (overlapNoteIds.size() > 0 && !occupyIdsLackSourceRows()) {
      return;
    }
    ensureOverdubSourceNotesForHold(holdPhaseTick, pitch, nullptr, false);
  };
  auto completeConsumeParticipantIds = [&]() {
    if (wrapCrossing) {
      maybeEnsureHoldSegmentInSourceView(startTick);
      if (endTick > 0) {
        maybeEnsureHoldSegmentInSourceView(0);
      }
      collectGeometricIdsForSegment(startTick, loopLen);
      if (endTick > 0) {
        collectGeometricIdsForSegment(0, endTick);
      }
    } else {
      maybeEnsureHoldSegmentInSourceView(startTick);
      collectGeometricIdsForSegment(startTick, endTick);
    }
  };
  completeConsumeParticipantIds();
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
  if (effectiveOverlapNoteIds.overflowed()) {
    ++overlapHoldTotals_.overflows;
  }
#endif

  NoteUtils::DisplayNoteVec selected;
  if (OverlapCandidateLookup::shouldLookupSpans(effectiveOverlapNoteIds)) {
    size_t notesExamined = 0;
#if RUNTIME_TIMING_ENABLED || defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
    const uint32_t lookupStartUs = micros();
#endif
    OverlapCandidateLookup::appendNotesForIds(overdubSourceViewNotes_, effectiveOverlapNoteIds,
                                              selected, &notesExamined);
#if RUNTIME_TIMING_ENABLED || defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
    const uint32_t lookupUs = micros() - lookupStartUs;
#endif
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
    ++overlapHoldTotals_.lookedUp;
    const uint32_t examined = static_cast<uint32_t>(notesExamined);
    overlapHoldTotals_.sumExamined += examined;
    if (examined > overlapHoldTotals_.maxExamined) {
      overlapHoldTotals_.maxExamined = examined;
    }
    overlapHoldTotals_.sumLookupUs += lookupUs;
    if (lookupUs > overlapHoldTotals_.maxLookupUs) {
      overlapHoldTotals_.maxLookupUs = lookupUs;
    }
#endif
  } else {
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
    ++overlapHoldTotals_.emptySets;
#endif
  }
  const size_t idSelectedCount = selected.size();

  uint32_t scanOnly = 0;
  uint32_t lateNotes = 0;
  for (size_t i = idSelectedCount; i < selected.size(); ++i) {
    if (overlapNoteIds.contains(selected[i].noteId)) {
      ++lateNotes;
    } else {
      ++scanOnly;
    }
  }
  uint32_t idsWithoutNotes = 0;
  for (size_t i = 0; i < effectiveOverlapNoteIds.size(); ++i) {
    const NoteId id = effectiveOverlapNoteIds.at(i);
    bool selectedHasId = false;
    for (const NoteUtils::DisplayNote& note : selected) {
      if (note.noteId == id) {
        selectedHasId = true;
        break;
      }
    }
    if (!selectedHasId) {
      ++idsWithoutNotes;
    }
  }
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
  overlapHoldTotals_.scanOnlyCandidates += scanOnly;
  overlapHoldTotals_.lateNoteCandidates += lateNotes;
  overlapHoldTotals_.idsWithoutNotes += idsWithoutNotes;
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  const bool jitEligible = (loopLen > overdubSourceWindowLengthTicks());
  // Attribution only when a path other than the occupy-id lookup mattered.
  if (scanOnly != 0 || lateNotes != 0 || idsWithoutNotes != 0) {
    char line[176];
    snprintf(line, sizeof(line),
             LOOP_NOTECHANGE_DIAG_FMT(
                 "#CAP,%lu,DIAG,consume,select,pitch=%u,ids=%u,idsel=%u,scan=%u,late=%u,norow=%u,"
                 "s=%lu,e=%lu,jit=%u"),
             static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
             static_cast<unsigned>(overlapNoteIds.size()),
             static_cast<unsigned>(idSelectedCount), static_cast<unsigned>(scanOnly),
             static_cast<unsigned>(lateNotes), static_cast<unsigned>(idsWithoutNotes),
             static_cast<unsigned long>(startTick), static_cast<unsigned long>(endTick),
             jitEligible ? 1u : 0u);
    DebugSessionCapture::appendCaptureTextLine(line);
  }
  if (idsWithoutNotes != 0) {
    uint32_t logged = 0;
    constexpr uint32_t kMaxConsumeDetailLogs = 8;
    for (size_t i = 0; i < overlapNoteIds.size() && logged < kMaxConsumeDetailLogs; ++i) {
      const NoteId id = overlapNoteIds.at(i);
      bool selectedHasId = false;
      for (const NoteUtils::DisplayNote& note : selected) {
        if (note.noteId == id) {
          selectedHasId = true;
          break;
        }
      }
      if (selectedHasId) {
        continue;
      }
      char line[160];
      snprintf(line, sizeof(line),
               LOOP_NOTECHANGE_DIAG_FMT(
                   "#CAP,%lu,DIAG,consume,norowid,pitch=%u,id=%u,s=%lu,e=%lu,jit=%u"),
               static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
               static_cast<unsigned>(id), static_cast<unsigned long>(startTick),
               static_cast<unsigned long>(endTick), jitEligible ? 1u : 0u);
      DebugSessionCapture::appendCaptureTextLine(line);
      ++logged;
    }
  }
  if (scanOnly != 0 || lateNotes != 0) {
    uint32_t logged = 0;
    constexpr uint32_t kMaxConsumeDetailLogs = 8;
    for (size_t i = idSelectedCount; i < selected.size() && logged < kMaxConsumeDetailLogs; ++i) {
      const NoteUtils::DisplayNote& note = selected[i];
      const bool inIds = overlapNoteIds.contains(note.noteId);
      char line[192];
      snprintf(line, sizeof(line),
               LOOP_NOTECHANGE_DIAG_FMT(
                   "#CAP,%lu,DIAG,consume,scanadd,pitch=%u,id=%u,in_ids=%u,start=%lu,end=%lu,"
                   "jit=%u"),
               static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
               static_cast<unsigned>(note.noteId), inIds ? 1u : 0u,
               static_cast<unsigned long>(note.startTick),
               static_cast<unsigned long>(note.endTick), jitEligible ? 1u : 0u);
      DebugSessionCapture::appendCaptureTextLine(line);
      ++logged;
    }
  }
#endif

  NoteUtils::DisplayNoteVec noteOffSelected;
  noteOffSelected.reserve(selected.size());
  for (const NoteUtils::DisplayNote& note : selected) {
    if (occupancyAlreadyResolved(note.noteId)) {
      continue;
    }
    if (noteOnAlreadyResolvedOccupancy &&
        !noteStartStrictlyInsideIncoming(note.startTick, startTick, endTick, loopLen)) {
      continue;
    }
    noteOffSelected.push_back(note);
  }

  const NoteId causingId =
      (incomingNoteId != kInvalidNoteId) ? incomingNoteId : allocateNoteId();
  if (wrapCrossing) {
    accumulatePendingNoteChangesFromSourceNotes(noteOffSelected, channel, pitch, velocity, startTick,
                                                loopLen, causingId);
    if (endTick > 0) {
      accumulatePendingNoteChangesFromSourceNotes(noteOffSelected, channel, pitch, velocity, 0,
                                                  endTick, causingId);
    }
  } else {
    accumulatePendingNoteChangesFromSourceNotes(noteOffSelected, channel, pitch, velocity, startTick,
                                                endTick, causingId);
  }

  PendingNoteChange addChange{};
  addChange.kind = PendingNoteChangeKind::Add;
  addChange.noteId = causingId;
  addChange.channel = channel;
  addChange.pitch = pitch;
  addChange.velocity = velocity;
  addChange.startTick = startTick;
  addChange.endTick = endTick;
  pendingNoteChanges_.push_back(addChange);

  recountOverlapHoldTotalsFromPending();
  return true;
}

LOOP_COLD_MEM void Loop::resolveOccupancyAtNoteOn(
    uint8_t channel, uint8_t pitch, uint8_t velocity, const OverlapNoteIdSet& overlapNoteIds,
    uint32_t holdPhaseTick, OverlapNoteIdSet* occupancyResolvedNoteIds) {
  if (!overdubSourceViewEstablished_) {
    return;
  }
  NoteUtils::DisplayNoteVec selected;
  if (OverlapCandidateLookup::shouldLookupSpans(overlapNoteIds)) {
    OverlapCandidateLookup::appendNotesForIds(overdubSourceViewNotes_, overlapNoteIds, selected,
                                              nullptr);
  }
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  size_t write = 0;
  for (size_t read = 0; read < selected.size(); ++read) {
    if (displayNotePresentAtHold(selected[read].startTick, selected[read].endTick, holdPhaseTick,
                                 loopLen)) {
      if (write != read) {
        selected[write] = selected[read];
      }
      ++write;
    }
  }
  selected.resize(write);
  // Ledger can be empty while source-view geometry already has a present-at-hold
  // participant (`104129` lcr,part n=0 a=1). Compare surviving present rows to
  // the id set so a non-present ledger row cannot skip fallback (`134436`).
  if (selected.empty() || selected.size() < overlapNoteIds.size()) {
    OverlapNoteIdSet geometricIds;
    collectOverdubSourceHoldParticipantIds(holdPhaseTick, pitch, geometricIds);
    if (OverlapCandidateLookup::shouldLookupSpans(geometricIds)) {
      OverlapCandidateLookup::appendNotesForIds(overdubSourceViewNotes_, geometricIds, selected,
                                                nullptr);
    }
  }
  if (selected.empty()) {
    return;
  }
  (void)pitch;
  (void)velocity;
  for (const NoteUtils::DisplayNote& note : selected) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    PendingNoteChange transform{};
    transform.noteId = note.noteId;
    transform.channel = channel;
    transform.pitch = note.note;
    transform.velocity = note.velocity;
    transform.startTick = note.startTick;
    const bool wrapHeld = note.endTick < note.startTick;
    const bool wrapHeldHeadShorten =
        wrapHeld && holdPhaseTick > 0 && holdPhaseTick != note.startTick;
    if (note.startTick < holdPhaseTick || wrapHeldHeadShorten) {
      transform.kind = PendingNoteChangeKind::Shorten;
      transform.endTick = holdPhaseTick - 1;
    } else {
      transform.kind = PendingNoteChangeKind::Hide;
      transform.endTick = note.endTick;
    }
    upsertSourceTransform(pendingNoteChanges_, transform);
    if (occupancyResolvedNoteIds != nullptr) {
      (void)occupancyResolvedNoteIds->insert(note.noteId);
    }
  }
  recountOverlapHoldTotalsFromPending();
}

LOOP_COLD_MEM void Loop::recountOverlapHoldTotalsFromPending() {
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
  overlapHoldTotals_.add = 0;
  overlapHoldTotals_.shorten = 0;
  overlapHoldTotals_.hide = 0;
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind == PendingNoteChangeKind::Add) {
      ++overlapHoldTotals_.add;
    } else if (change.kind == PendingNoteChangeKind::Shorten) {
      ++overlapHoldTotals_.shorten;
    } else if (change.kind == PendingNoteChangeKind::Hide) {
      ++overlapHoldTotals_.hide;
    }
  }
#endif
}

LOOP_COLD_MEM __attribute__((noinline)) void Loop::emitOverlapHoldTotals() const {
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
  const OverlapHoldTotals& totals = overlapHoldTotals_;
  SC_OVERLAP_HOLD(totals.noteOffs, totals.emptySets, totals.maxIds, totals.overflows,
                  totals.lookedUp, totals.maxExamined, totals.sumExamined, totals.maxLookupUs,
                  totals.sumLookupUs, totals.add, totals.shorten, totals.hide);
#endif
}

LOOP_COLD_MEM void Loop::applyPendingNoteChangesToOverdubSourceView() {
  if (!overdubSourceViewEstablished_) {
    return;
  }
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_ != 0
                               ? overdubSourceViewLoopLengthTicks_
                               : loopLengthTicks;
  NoteUtils::DisplayNoteVec added;
  added.reserve(pendingNoteChanges_.size());
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind != PendingNoteChangeKind::Add || change.noteId == kInvalidNoteId) {
      continue;
    }
    NoteUtils::DisplayNote note{};
    note.noteId = change.noteId;
    note.note = change.pitch;
    note.velocity = change.velocity;
    note.startTick = change.startTick;
    note.endTick = change.endTick;
    added.push_back(note);
  }
  if (!added.empty()) {
    mergeDisplayNotesIntoOverdubSourceView(added);
    if (overdubSourceSpanCacheValid_) {
      std::vector<NoteId> spanCacheNoteIds;
      collectPendingSortedNoteIds(overdubSourceSpanCacheNotes_, spanCacheNoteIds);
      for (const NoteUtils::DisplayNote& candidate : added) {
        if (candidate.noteId == kInvalidNoteId) {
          continue;
        }
        if (insertSortedUniquePendingNoteId(spanCacheNoteIds, candidate.noteId)) {
          overdubSourceSpanCacheNotes_.push_back(candidate);
        }
      }
    }
  }
  applyPendingHideAndShortenToNotes(overdubSourceViewNotes_, pendingNoteChanges_, loopLen, false,
                                    static_cast<size_t>(-1), false);
  if (overdubSourceSpanCacheValid_) {
    applyPendingHideAndShortenToNotes(overdubSourceSpanCacheNotes_, pendingNoteChanges_, loopLen,
                                      false, static_cast<size_t>(-1), false);
  }
}

namespace {

const EditPass* findActiveSealedCompanionRow(const Loop& loop, EditPassId id) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.id == id && row.state == EditPassState::Active) {
      return &row;
    }
  }
  return nullptr;
}

void appendSealedCompanionOverlay(const Loop& loop, PendingNoteChangeVec& overlay) {
  overlay.reserve(loop.sealedCompanionOverlayIds_.size());
  for (const EditPassId id : loop.sealedCompanionOverlayIds_) {
    const EditPass* row = findActiveSealedCompanionRow(loop, id);
    if (row == nullptr || row->targetNoteId == kInvalidNoteId) {
      continue;
    }
    PendingNoteChange change{};
    change.noteId = row->targetNoteId;
    change.pitch = row->pitch;
    change.velocity = row->velocity;
    change.startTick = row->startTick;
    change.endTick = row->endTick;
    if (row->actionType == EditActionType::Delete) {
      change.kind = PendingNoteChangeKind::Hide;
    } else if (row->actionType == EditActionType::Update &&
               row->propertyType == EditPropertyType::Length) {
      change.kind = PendingNoteChangeKind::Shorten;
    } else {
      continue;
    }
    overlay.push_back(change);
  }
}

}  // namespace

LOOP_COLD_MEM bool Loop::hasSealedCompanionOverlay() const {
  return !sealedCompanionOverlayIds_.empty();
}

LOOP_COLD_MEM const EditPassIdList& Loop::sealedCompanionOverlayIds() const {
  return sealedCompanionOverlayIds_;
}

LOOP_COLD_MEM bool Loop::sealedCompanionOverlayRequired() const {
  if (!hasSealedCompanionOverlay() || lastOverdubSealPlaybackRevision_ == 0) {
    return false;
  }
  const uint32_t selectedRevision = publishedSnapshotIsCurrent()
                                        ? publishedSnapshotRevision()
                                        : visualCache.builtFromRevision;
  return selectedRevision < lastOverdubSealPlaybackRevision_;
}

LOOP_COLD_MEM void Loop::applyPendingNoteChangesToDisplayNotes(
    NoteUtils::DisplayNoteVec& notes, size_t liveCaptureBegin) const {
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_ != 0
                               ? overdubSourceViewLoopLengthTicks_
                               : loopLengthTicks;
  if (!pendingNoteChanges_.empty()) {
    applyPendingHideAndShortenToNotes(notes, pendingNoteChanges_, loopLen, true, liveCaptureBegin,
                                      true);
  }
  if (!hasSealedCompanionOverlay()) {
    return;
  }
  // Overlay ids persist across wraps (`140729`). Revision >= Rseal must not
  // drop a Hide whose target is still in the painted prefix (`201956`
  // draw_src id 71 63@768→1151 next to 768→864).
  PendingNoteChangeVec overlay;
  appendSealedCompanionOverlay(*this, overlay);
  if (overlay.empty()) {
    return;
  }
  applyPendingHideAndShortenToNotes(notes, overlay, loopLen, false, liveCaptureBegin, true);
}

LOOP_COLD_MEM EditPassIdList Loop::sealPendingNoteChangesToEditPasses(
    bool updateOverdubSourceView) {
  EditPassIdList sealedIds;
  size_t companionRowsToSeal = 0;
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind == PendingNoteChangeKind::Shorten ||
        change.kind == PendingNoteChangeKind::Hide) {
      ++companionRowsToSeal;
    }
  }
  if (updateOverdubSourceView && !pendingNoteChanges_.empty()) {
    applyPendingNoteChangesToOverdubSourceView();
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  const uint32_t companionSealStartUs = micros();
  (void)companionSealStartUs;
  uint32_t companionRowsLogged = 0;
  constexpr uint32_t kMaxCompanionRowsLogged = 8;
#endif
  if (companionRowsToSeal > 0) {
    sealedIds.reserve(companionRowsToSeal);
    passes.editPasses.reserve(passes.editPasses.size() + companionRowsToSeal);
  }
  const size_t companionBytesNeeded =
      Config::HEAP_RESERVE_BYTES + companionRowsToSeal * sizeof(EditPass);
  const bool batchHeapReserveAdmitted =
      companionRowsToSeal > 0 &&
      MemoryMonitor::getInternalHeapFreeBytes() >= companionBytesNeeded;
  (void)batchHeapReserveAdmitted;
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind != PendingNoteChangeKind::Shorten &&
        change.kind != PendingNoteChangeKind::Hide) {
      continue;
    }
    EditPass row{};
    row.passType = EditPassType::Note;
    row.targetNoteId = change.noteId;
    row.pitch = change.pitch;
    row.velocity = change.velocity;
    row.startTick = change.startTick;
    row.endTick = change.endTick;
    if (change.kind == PendingNoteChangeKind::Hide) {
      row.actionType = EditActionType::Delete;
      row.propertyType = EditPropertyType::None;
    } else {
      row.actionType = EditActionType::Update;
      row.propertyType = EditPropertyType::Length;
    }
    const EditPassId id = saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(row),
                                           EditPassType::Note, true, batchHeapReserveAdmitted);
    if (id != kInvalidEditPassId) {
      sealedIds.push_back(id);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
      if (companionRowsLogged < kMaxCompanionRowsLogged) {
        char line[128];
        const char* kind =
            (change.kind == PendingNoteChangeKind::Hide) ? "hide" : "shorten";
        snprintf(line, sizeof(line),
                 LOOP_NOTECHANGE_DIAG_FMT(
                     "#CAP,%lu,DIAG,seal_companion,id=%u,target=%u,kind=%s,end=%u"),
                 static_cast<unsigned long>(micros()), static_cast<unsigned>(id),
                 static_cast<unsigned>(change.noteId), kind,
                 static_cast<unsigned>(change.endTick));
        DebugSessionCapture::appendCaptureTextLine(line);
        ++companionRowsLogged;
      }
#endif
    }
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  if (companionRowsToSeal > 0) {
    char line[160];
    snprintf(line, sizeof(line),
             LOOP_NOTECHANGE_DIAG_FMT("#CAP,%lu,DIAG,seal_companion_batch,rows=%u,sealed=%u,us=%lu"),
             static_cast<unsigned long>(micros()),
             static_cast<unsigned>(companionRowsToSeal),
             static_cast<unsigned>(sealedIds.size()),
             static_cast<unsigned long>(micros() - companionSealStartUs));
    DebugSessionCapture::appendCaptureTextLine(line);
  }
#endif
  if (!sealedIds.empty()) {
    ++playbackRevision;
    editStateDirty_ = true;
    notifyCommittedContentChanged();
  }
  // Keep cache reuse across overdub sessions only when companion sealing succeeded
  // for every pending Shorten/Hide row (otherwise force a rebuild next entry).
  if (updateOverdubSourceView && hasOverdubSession() && companionRowsToSeal > 0 &&
      sealedIds.size() == companionRowsToSeal &&
      overdubSourceSpanCacheValid_ &&
      overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks) {
    overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  }
  pendingNoteChanges_.clear();
  return sealedIds;
}
