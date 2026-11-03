//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"
#include "Utils/LaneGapSpans.h"

#include "EditSessionAction.h"
#include "EditSessionInteraction.h"
#include "NoteEditCurrentState.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "OverlapCandidateLookup.h"
#include "OverlapNoteIdObservation.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/InvestigationCaptureLog.h"
#include "Utils/LoopMem.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"
#include "Utils/PendingNoteCompanion.h"
#include "Utils/RuntimeTimingTelemetry.h"

#include <algorithm>
#include <cstdio>

#if defined(PIO_UNIT_TEST_NATIVE) && !defined(HARNESS_ARDUINO_STUB)
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
    // Hide absorbs an earlier Length of the same NoteId so a later sealed
    // Shorten cannot restore an inner note the long occupy already hid
    // (`002708` 83@1296–1335 under 864–1344).
    if (it->kind == PendingNoteChangeKind::Hide &&
        change.kind == PendingNoteChangeKind::Shorten) {
      return;
    }
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

LOOP_COLD_MEM bool hideSourceEndAtPitchStart(const PendingNoteChangeVec& transforms,
                                             uint8_t pitch, uint32_t startTick,
                                             uint32_t& hideEndTick) {
  bool found = false;
  hideEndTick = 0;
  for (const PendingNoteChange& change : transforms) {
    if (change.kind == PendingNoteChangeKind::Hide && change.pitch == pitch &&
        change.startTick == startTick) {
      if (!found || change.endTick > hideEndTick) {
        hideEndTick = change.endTick;
      }
      found = true;
    }
  }
  return found;
}

/// After Hide or Shorten apply, drop a longer same-pitch+start closed sibling so a
/// snapshot/sealed-preview remnant cannot sit beside the shortened row (`201956`
/// Hide; `123236` Shorten; `000402` later occupy still painted short by NoteId
/// while the longer suffix remnant stayed visible). A current closed
/// `capturePreview` identity is not this class (`185822`) — it is exempt
/// when its valid NoteId matches a closed `capturePreview.notes` row.
/// Hide also drops a closed remnant whose end is at least the hidden source
/// end when an open occupy hold or a shorter closed keeper is already at
/// that pitch+start (`001145` 63@1152). A later same-pitch occupy that
/// starts earlier and covers the inner row also drops that inner (`002708`
/// 1296–1335 under 864–1344). The covering row must be the shortest keeper
/// at its start — a longer earlier remnant must not delete the new long
/// occupy (`003622` 768–1344 remnant vs 816–1344). An open hold that has
/// not grown over the inner does not drop later same-pitch notes. A Hide
/// whose target was never in the frame does not delete a different NoteId
/// (`RC3`). Same-start shorter keepers stay (`201956` 768–864). Open
/// capture holds are kept so playhead tails can grow.
LOOP_COLD_MEM void dropLongerSamePitchStartSiblingsMarkedBySourceTransform(
    NoteUtils::DisplayNoteVec& notes, const PendingNoteChangeVec& transforms,
    size_t /*liveCaptureBegin*/, const NoteUtils::DisplayNoteVec& capturePreviewNotes) {
  if (notes.empty()) {
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
  auto isClosedLinear = [](const NoteUtils::DisplayNote& note) {
    return note.endTick >= note.startTick &&
           !DisplayWindowUtils::capturePreviewNoteIsOpenHold(note.startTick, note.endTick);
  };
  auto isShortestClosedAtPitchStart = [&](const NoteUtils::DisplayNote& candidate) {
    if (!isClosedLinear(candidate)) {
      return false;
    }
    for (const NoteUtils::DisplayNote& other : notes) {
      if (!isClosedLinear(other) || other.note != candidate.note ||
          other.startTick != candidate.startTick) {
        continue;
      }
      if (other.endTick < candidate.endTick) {
        return false;
      }
    }
    return true;
  };
  auto earlierStartOccupyCovers = [&](const NoteUtils::DisplayNote& note) {
    for (const NoteUtils::DisplayNote& other : notes) {
      if (other.note != note.note || other.startTick >= note.startTick ||
          other.endTick < note.endTick) {
        continue;
      }
      if (isShortestClosedAtPitchStart(other)) {
        return true;
      }
    }
    return false;
  };
  auto earlierStartOccupyOverlaps = [&](const NoteUtils::DisplayNote& note) {
    for (const NoteUtils::DisplayNote& other : notes) {
      if (other.note != note.note || other.startTick >= note.startTick ||
          other.endTick <= note.startTick) {
        continue;
      }
      if (isShortestClosedAtPitchStart(other)) {
        return true;
      }
    }
    return false;
  };
  auto hideCompleteCovers = [&](const NoteUtils::DisplayNote& note) {
    for (const PendingNoteChange& change : transforms) {
      if (change.kind == PendingNoteChangeKind::Hide && change.pitch == note.note &&
          change.startTick <= note.startTick && change.endTick >= note.endTick) {
        return true;
      }
    }
    return false;
  };
  NoteUtils::DisplayNoteVec kept;
  kept.reserve(notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    const NoteUtils::DisplayNote& note = notes[i];
    uint32_t hideEndTick = 0;
    if (!isClosedLinear(note)) {
      kept.push_back(note);
      continue;
    }
    if (DisplayWindowUtils::displayNoteIsClosedCapturePreviewIdentity(note, capturePreviewNotes)) {
      kept.push_back(note);
      continue;
    }
    const bool coveredByEarlierOccupy = earlierStartOccupyCovers(note);
    if (hideSourceEndAtPitchStart(transforms, note.note, note.startTick, hideEndTick)) {
      if (coveredByEarlierOccupy) {
        continue;
      }
      if (note.endTick < hideEndTick) {
        kept.push_back(note);
        continue;
      }
      bool occupyPresent = false;
      for (size_t j = 0; j < notes.size(); ++j) {
        const NoteUtils::DisplayNote& other = notes[j];
        if (other.note != note.note || other.startTick != note.startTick) {
          continue;
        }
        if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(other.startTick, other.endTick) ||
            (isClosedLinear(other) && other.endTick < hideEndTick)) {
          occupyPresent = true;
          break;
        }
      }
      if (!occupyPresent && !earlierStartOccupyOverlaps(note)) {
        kept.push_back(note);
      }
      continue;
    }
    if (hideCompleteCovers(note) && coveredByEarlierOccupy) {
      continue;
    }
    if (!sourceTransformMarksPitchStart(transforms, note.note, note.startTick)) {
      kept.push_back(note);
      continue;
    }
    bool dominated = false;
    for (size_t j = 0; j < notes.size(); ++j) {
      const NoteUtils::DisplayNote& other = notes[j];
      if (!isClosedLinear(other) || other.note != note.note ||
          other.startTick != note.startTick) {
        continue;
      }
      if (other.endTick < note.endTick) {
        dominated = true;
        break;
      }
      if (other.endTick == note.endTick && j < i) {
        const bool distinctValidIds = note.noteId != kInvalidNoteId &&
                                      other.noteId != kInvalidNoteId &&
                                      note.noteId != other.noteId;
        if (!distinctValidIds) {
          dominated = true;
          break;
        }
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
                                                     bool dropSameStartLongerAfterSourceTransform,
                                                     const NoteUtils::DisplayNoteVec& capturePreviewNotes) {
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
    // The only display notes excluded from the pending transform are an
    // open capture hold whose end is intentionally extended by
    // applyCapturePlayheadTails(), and a current closed capture-preview
    // identity that would otherwise inherit another NoteId's prefix Hide
    // (`204112` 69@48–96).
    if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(note.startTick, note.endTick)) {
      updated.push_back(note);
      continue;
    }
    const PendingNoteChange* transform =
        findSourceTransformByNoteIdSorted(transforms, note.noteId);
    if (transform == nullptr && matchHideByOverlap && loopLength != 0 &&
        i < liveCaptureBegin &&
        !DisplayWindowUtils::displayNoteIsClosedCapturePreviewIdentity(note,
                                                                      capturePreviewNotes)) {
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
    // A committed row stays authoritative until the pending replacement is a valid span.
    // Publishing a degenerate/tiny surrogate (e.g. 1536..1536 for a committed 63@1536..2208)
    // makes the overlap layer release the row before the replacement is complete. Serve the
    // committed row until the operation can be switched atomically.
    const bool replacementTooShort =
        revised.endTick <= revised.startTick ||
        (noteMinLengthRemoveEnabled &&
         (revised.endTick - revised.startTick) < noteMinLengthTicks);
    if (replacementTooShort) {
      updated.push_back(note);
      continue;
    }
    updated.push_back(revised);
  }
  notes.swap(updated);
  if (dropSameStartLongerAfterSourceTransform) {
    dropLongerSamePitchStartSiblingsMarkedBySourceTransform(notes, transforms, liveCaptureBegin,
                                                           capturePreviewNotes);
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
  // **Read-only** (tasks 4.7/4.8): the view is already the published source, composed by `NoteId` from
  // the worker's completed tiles. A hold either has prepared coverage -- in which case the view answers --
  // or its consequence is an owed prerequisite discharged when the tile completes. Nothing derives here,
  // and the old JIT window merge is not a second producer.
  if (newlyMergedPitchNotes == nullptr) {
    return;
  }
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (presentAtHoldOnly &&
        !displayNotePresentAtHold(note.startTick, note.endTick, holdPhaseTick, loopLen)) {
      continue;
    }
    newlyMergedPitchNotes->push_back(note);
  }
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
  LANE_MSG_SPAN(OverdubNoteChanges);
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

  LANE_NOTEOFF_BEGIN(overdubCoverageDeferredCount_);
  // Touched coverage (task 4.7): the completed span marks every tile it touches. Marking derives
  // nothing; it is what makes the span answerable by the worker and by the deadline fallback.
  {
    LANE_NOTEOFF_PHASE(coverageUs);
    const uint8_t newlySealed = markOverdubCoverageSpan(startTick, endTick);
    LANE_COV_ADD(NewTilesSealed, newlySealed);
    (void)newlySealed;
  }
  const uint32_t spanLength = wrapCrossing ? (loopLen - startTick + endTick) : (endTick - startTick);
  if (overdubCoverage_.sessionActive && !overdubCoverage_.spanPrepared(startTick, spanLength)) {
    // The note is accepted now; the overlap consequence is deferred **by name** and discharged when the
    // tile that carries the span has been published. Never compute against partial coverage.
    DeferredOverdubCompletion completion{};
    completion.startTick = startTick;
    completion.endTick = endTick;
    completion.noteId = incomingNoteId;
    completion.channel = channel;
    completion.pitch = pitch;
    completion.velocity = velocity;
    completion.spanOwed = true;
    bool retained = false;
    {
      LANE_NOTEOFF_PHASE(retainUs);
      retained = retainDeferredOverdubCompletion(completion);
    }
    if (retained) {
      LANE_NOTEOFF_PATH(PathRetained);
      // The interval's own Add row is not an overlap decision: it is recorded now so the note is part of
      // the session's pending set exactly once, whether or not its consequence is still owed.
      PendingNoteChange addChange{};
      addChange.kind = PendingNoteChangeKind::Add;
      addChange.noteId = incomingNoteId != kInvalidNoteId ? incomingNoteId : allocateNoteId();
      addChange.channel = channel;
      addChange.pitch = pitch;
      addChange.velocity = velocity;
      addChange.startTick = startTick;
      addChange.endTick = endTick;
      pendingNoteChanges_.push_back(addChange);
      recountOverlapHoldTotalsFromPending();
      return true;
    }
    // The bounded obligation set is full: answer synchronously against the note's own closure window
    // instead of dropping the decision. A window, never a pass walk.
    NoteUtils::DisplayNoteVec local;
    uint32_t lookupStart = 0;
    uint32_t lookupLength = 0;
    CoveragePrerequisite::closureWindow(startTick, spanLength, overdubCoverageClosureTicks_, loopLen,
                                        lookupStart, lookupLength);
    LANE_NOTEOFF_PATH(PathFallback);
    {
      LANE_NOTEOFF_PHASE(deriveUs);
      LoopContentResolution::resolveDisplayNotesInWindow(passes, loopLen, lookupStart, lookupLength,
                                                         local);
    }
    return applyOverlapConsequenceForCompletedNote(
        channel, pitch, velocity, startTick, endTick, incomingNoteId, overlapNoteIds,
        occupancyResolvedNoteIds, true, &local);
  }
  return applyOverlapConsequenceForCompletedNote(channel, pitch, velocity, startTick, endTick,
                                                 incomingNoteId, overlapNoteIds,
                                                 occupancyResolvedNoteIds, true);
}

/// One overlap consequence against an explicit candidate source. Shared by the immediate note-off path,
/// the discharge lane (prepared tiles) and the seal deadline (a bounded local window), so all three
/// decisions have one implementation and the only difference is **which source answers**.
LOOP_COLD_MEM bool Loop::applyOverlapConsequenceForCompletedNote(
    uint8_t channel, uint8_t pitch, uint8_t velocity, uint32_t startTick, uint32_t endTick,
    NoteId incomingNoteId, const OverlapNoteIdSet& overlapNoteIds,
    const OverlapNoteIdSet* occupancyResolvedNoteIds, bool pushAddChange,
    const NoteUtils::DisplayNoteVec* candidates) {
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  if (loopLen == 0 || endTick == startTick) {
    return false;
  }
  const bool wrapCrossing = endTick < startTick;
  const NoteUtils::DisplayNoteVec& source =
      candidates != nullptr ? *candidates : overdubSourceViewNotes_;

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
    for (const NoteUtils::DisplayNote& note : source) {
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
  if (wrapCrossing) {
    collectGeometricIdsForSegment(startTick, loopLen);
    if (endTick > 0) {
      collectGeometricIdsForSegment(0, endTick);
    }
  } else {
    collectGeometricIdsForSegment(startTick, endTick);
  }
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
    OverlapCandidateLookup::appendNotesForIds(source, effectiveOverlapNoteIds, selected,
                                              &notesExamined);
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

  const NoteId causingId = (incomingNoteId != kInvalidNoteId)
                               ? incomingNoteId
                               : (pushAddChange ? allocateNoteId() : kInvalidNoteId);
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

  if (pushAddChange) {
    PendingNoteChange addChange{};
    addChange.kind = PendingNoteChangeKind::Add;
    addChange.noteId = causingId;
    addChange.channel = channel;
    addChange.pitch = pitch;
    addChange.velocity = velocity;
    addChange.startTick = startTick;
    addChange.endTick = endTick;
    pendingNoteChanges_.push_back(addChange);
  }

  recountOverlapHoldTotalsFromPending();
  return true;
}

LOOP_COLD_MEM void Loop::resolveOccupancyAtNoteOn(
    uint8_t channel, uint8_t pitch, uint8_t velocity, const OverlapNoteIdSet& overlapNoteIds,
    uint32_t holdPhaseTick, OverlapNoteIdSet* occupancyResolvedNoteIds) {
  // CONTRACTS.md C3.4: the hold's occupancy decision is deferred, not dropped, whenever the only
  // reason its participant set may be incomplete is that the private predecessor source is still
  // being built. Recording the intent here -- before any early return -- is what makes the
  // deadline semantic: the note-on does not wait, the transform materializes when the scan does.
  // The incoming note's own identity is allocated at note-off and is not an input to the decision,
  // so the retained record is the causal pair it needs: channel, pitch, velocity, hold tick.
  if (!privateHoldMaterializing_ && privateSource_.phase == kPrivateRows) {
    (void)retainPrivateHoldIntent(channel, pitch, velocity, holdPhaseTick);
  }
  if (!overdubSourceViewEstablished_) {
    return;
  }
  // Touched coverage (task 4.7): occupancy at the **pressed phase** is a named prerequisite. The MIDI
  // note is already accepted; the pressed tile is already marked required. If that tile is not prepared,
  // the source-view half of the decision is owed -- recording it here is what makes the deadline
  // semantic. Ledger authority is still applied immediately below; the discharge re-runs the same
  // decision against the completed tile and a repeated transform is a same-`NoteId` upsert.
  if (overdubCoverage_.sessionActive &&
      !overdubCoverage_.tilePrepared(
          CoveragePrerequisite::tileIndexForPhase(holdPhaseTick, loopLengthTicks))) {
    DeferredOverdubCompletion completion{};
    completion.startTick = holdPhaseTick;
    completion.channel = channel;
    completion.pitch = pitch;
    completion.velocity = velocity;
    completion.noteId = kInvalidNoteId;  // assigned from the capture store at note-off
    completion.occupancyOwed = true;
    (void)retainDeferredOverdubCompletion(completion);
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
                                    static_cast<size_t>(-1), false, capturePreview.notes);
  if (overdubSourceSpanCacheValid_) {
    applyPendingHideAndShortenToNotes(overdubSourceSpanCacheNotes_, pendingNoteChanges_, loopLen,
                                      false, static_cast<size_t>(-1), false, capturePreview.notes);
  }
}

LOOP_COLD_MEM void Loop::applyNoteEditCurrentStateToOverdubSourceView(
    const NoteEditCurrentState& currentState) {
  if (!hasOverdubSourceView() || currentState.empty() || overdubSourceViewNotes_.empty()) {
    return;
  }

  PendingNoteChangeVec transforms;
  for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    const NoteEditCurrentNoteState* row = currentState.find(note.noteId);
    if (row == nullptr) {
      continue;
    }
    PendingNoteChange change{};
    change.noteId = note.noteId;
    change.pitch = row->currentSpan.pitch;
    change.velocity = row->currentSpan.velocity;
    change.startTick = row->currentSpan.startTick;
    change.endTick = row->currentSpan.endTick;
    if (currentState.isRowHiddenOrDeleted(note.noteId)) {
      change.kind = PendingNoteChangeKind::Hide;
    } else if (currentState.rowIsVisible(note.noteId) &&
               row->currentSpan.pitch == note.note &&
               row->currentSpan.startTick == note.startTick &&
               row->currentSpan.endTick < note.endTick) {
      change.kind = PendingNoteChangeKind::Shorten;
    } else {
      continue;
    }
    insertOrReplaceSourceTransformSorted(transforms, change);
  }
  if (transforms.empty()) {
    return;
  }

  const uint32_t loopLen =
      overdubSourceViewLoopLengthTicks_ != 0 ? overdubSourceViewLoopLengthTicks_ : loopLengthTicks;
  const NoteUtils::DisplayNoteVec emptyPreview;
  applyPendingHideAndShortenToNotes(overdubSourceViewNotes_, transforms, loopLen, false,
                                    static_cast<size_t>(-1), false, emptyPreview);
  if (overdubSourceSpanCacheValid_) {
    applyPendingHideAndShortenToNotes(overdubSourceSpanCacheNotes_, transforms, loopLen, false,
                                      static_cast<size_t>(-1), false, emptyPreview);
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

bool sealedPreviewHasReplacementAtPitch(const Loop& loop, uint8_t pitch, NoteId hiddenNoteId) {
  for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
    if (sealedRow.note == pitch && sealedRow.noteId != hiddenNoteId) {
      return true;
    }
  }
  return false;
}

bool notesHaveReplacementAtPitch(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                 NoteId hiddenNoteId) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.noteId != hiddenNoteId) {
      return true;
    }
  }
  return false;
}

bool sealedCompanionShortenAlreadyApplied(const NoteUtils::DisplayNoteVec& notes,
                                          const PendingNoteChange& change) {
  if (change.kind != PendingNoteChangeKind::Shorten) {
    return false;
  }
  const NoteUtils::DisplayNote* existing =
      DisplayWindowUtils::findDisplayNoteByNoteId(notes, change.noteId);
  return DisplayWindowUtils::sealedCompanionTransformAlreadyRepresented(
      false, existing != nullptr && existing->startTick == change.startTick &&
                 existing->endTick == change.endTick);
}

void filterSealedCompanionOverlayForSource(const Loop& loop, const NoteUtils::DisplayNoteVec& notes,
                                           PendingNoteChangeVec& overlay) {
  const bool uncoveredSealed =
      (loop.captureActive() || loop.hasOverdubSession()) && loop.hasSealedCapturePreview() &&
      !loop.sealedCapturePreviewIsCovered();
  PendingNoteChangeVec kept;
  kept.reserve(overlay.size());
  for (const PendingNoteChange& change : overlay) {
    if (sealedCompanionShortenAlreadyApplied(notes, change)) {
      continue;
    }
    if (change.kind == PendingNoteChangeKind::Hide &&
        DisplayWindowUtils::sealedCompanionHideShouldWaitForReplacement(
            uncoveredSealed, sealedPreviewHasReplacementAtPitch(loop, change.pitch, change.noteId),
            notesHaveReplacementAtPitch(notes, change.pitch, change.noteId))) {
      continue;
    }
    kept.push_back(change);
  }
  overlay.swap(kept);
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
  const bool uncoveredSealed =
      (captureActive() || hasOverdubSession()) && hasSealedCapturePreview() &&
      !sealedCapturePreviewIsCovered();
  const bool cacheReusable = DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
      visualCacheDirty, !visualCache.notes.empty(), true, uncoveredSealed);
  const uint32_t selectedRevision =
      publishedSnapshotIsCurrent()
          ? publishedSnapshotRevision()
          : (cacheReusable ? visualCache.builtFromRevision
                           : (publishedSnapshot() ? publishedSnapshotRevision()
                                                  : visualCache.builtFromRevision));
  return selectedRevision < lastOverdubSealPlaybackRevision_;
}

LOOP_COLD_MEM void Loop::applyPendingNoteChangesToDisplayNotes(
    NoteUtils::DisplayNoteVec& notes, size_t liveCaptureBegin) const {
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_ != 0
                               ? overdubSourceViewLoopLengthTicks_
                               : loopLengthTicks;
  // Sealed companions are the committed baseline. This-lap pending must apply
  // after them so a further Shorten of the same NoteId is not overwritten
  // (`002200` 73@768–1247 sealed, occupy hs=1056 pending 768–1055).
  if (hasSealedCompanionOverlay()) {
    PendingNoteChangeVec overlay;
    appendSealedCompanionOverlay(*this, overlay);
    filterSealedCompanionOverlayForSource(*this, notes, overlay);
    if (!overlay.empty()) {
      applyPendingHideAndShortenToNotes(notes, overlay, loopLen, false, liveCaptureBegin, true,
                                        capturePreview.notes);
    }
  }
  // Use the borrow-aware accessor: an admitted stop swaps this session's changes into the
  // CaptureStopJob and lends them back for display (`borrowPendingNoteChanges`). Reading
  // `pendingNoteChanges_` here applied the committed base's Shorten but not the frozen stop
  // preview's, leaving the preview at the stop closure (the 2304-vs-2296 6th-stop row).
  const PendingNoteChangeVec& thisLap = pendingNoteChanges();
  if (!thisLap.empty()) {
    applyPendingHideAndShortenToNotes(notes, thisLap, loopLen, true, liveCaptureBegin,
                                      true, capturePreview.notes);
  }
}

/// C3.4's materialization unit: one pending-change upsert against one retained hold's target. The
/// hold's participant set is re-derived from the completed source, so the decision is the same one
/// the note-on would have made had the source been built then.
LOOP_COLD_MEM Loop::PrivateSourceStep Loop::stepPrivatePredecessorHolds(size_t quantum) {
  // C3.4 (review finding 2): the participants come from the session view, and the view is only
  // refreshed by `rebuildOverdubSourceView`. Refresh it once per materialization episode -- before
  // the first participant query -- so the decision is derived from the source that answers *now*:
  // the private prefix while the scan owns the cache, the committed origin after a supersession.
  if (!privateSource_.holdViewRefreshed) {
    const uint32_t playhead = lastTickInLoop != UINT32_MAX ? lastTickInLoop : 0;
    rebuildOverdubSourceView(playhead, "private_hold");
    privateSource_.holdViewRefreshed = true;
  }
  size_t units = 0;
  while (units < quantum) {
    if (!privateSource_.holdLoaded) {
      if (privateSource_.holdIndex >= privateHoldCount_) {
        // Every retained hold is materialized exactly once.
        privateSource_.unitsLast = units;
        privateHoldCount_ = 0;
        privateHoldTargets_.clear();
        privateHoldTargetCursor_ = 0;
        privateSource_.holdIndex = 0;
        if (privateSource_.phase == kPrivateHolds) {
          // The scan completed, so the built source is now a complete private cache.
          privateSource_.phase = kPrivateComplete;
          overdubSourceSpanCacheFullLoop_ = true;
          overdubSourceSpanCacheValid_ = true;
          overdubSourceSpanCacheOrigin_ = SourceOrigin::PrivatePredecessor;
          overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
          overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
          overdubSourceSpanCachePublishedRevision_ = 0;
          overdubSourceSpanCachePublishedIdentity_ = nullptr;
        }
        // Otherwise the scan was superseded and the committed origin answers: the holds were still
        // materialized (against it), and no private stamp may be left behind.
        return PrivateSourceStep::Complete;
      }
      const PrivateSourceHold& hold = privateHolds_[privateSource_.holdIndex];
      privateHoldTargets_.clear();
      privateHoldTargetCursor_ = 0;
      collectOverdubSourceHoldParticipantIds(hold.holdPhaseTick, hold.pitch,
                                             privateHoldTargets_);
      privateSource_.holdLoaded = true;
      ++units;
      continue;
    }
    if (privateHoldTargetCursor_ < privateHoldTargets_.size()) {
      const PrivateSourceHold& hold = privateHolds_[privateSource_.holdIndex];
      const NoteId target = privateHoldTargets_.at(privateHoldTargetCursor_);
      ++privateHoldTargetCursor_;
      // The session's *own* accepted notes are `Add` rows in this interval's live set, and the
      // overlap resolver never targets them (at note-on they are not yet in the view). A deferred
      // decision must not either: it would shorten or hide the very note being recorded. They are
      // examined (this is still one unit) and skipped.
      if (target != kInvalidNoteId && !privateOwnAcceptedNote(target)) {
        OverlapNoteIdSet single;
        (void)single.insert(target);
        privateHoldMaterializing_ = true;
        resolveOccupancyAtNoteOn(hold.channel, hold.pitch, hold.velocity, single,
                                 hold.holdPhaseTick, nullptr);
        privateHoldMaterializing_ = false;
      }
      ++units;
      continue;
    }
    // This hold is done; the next iteration loads the following one (or finishes).
    privateSource_.holdLoaded = false;
    ++privateSource_.holdIndex;
  }
  privateSource_.unitsLast = units;
  return PrivateSourceStep::Pending;
}

/// C3.4's deadline edge. The deferred lane can be starved (a published predecessor's presentation
/// retirement owns every turn until it finishes), so the seal -- which is the contract's second named
/// deadline -- drives the materialization itself: at most one declared grain per retained intent, so
/// the work is bounded by `kPrivateSourceHoldCapacity` grains, and only what is still unreachable is
/// denied by name.
LOOP_COLD_MEM void Loop::materializeDeferredPrivateHoldsAtDeadline() {
  if (privateHoldCount_ == 0) {
    return;
  }
  for (size_t grain = 0; grain < kPrivateSourceHoldCapacity && privateHoldCount_ > 0; ++grain) {
    if (stepPrivatePredecessorHolds(kDeferredStopPreparationQuantum) !=
        PrivateSourceStep::Pending) {
      break;
    }
  }
  if (privateHoldCount_ > 0) {
    privateSourceSealDenied_ = true;
    notePrivateSourceDenied(PrivateSourceDeny::OutputUnavailable);
  }
}

/// True when this interval's live pending set already carries `noteId` as its own accepted note.
LOOP_COLD_MEM bool Loop::privateOwnAcceptedNote(NoteId noteId) const {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind == PendingNoteChangeKind::Add && change.noteId == noteId) {
      return true;
    }
  }
  return false;
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
  // CONTRACTS.md C3.4: this is the second deadline edge. If the private predecessor source is
  // still owed here, the deferred decisions were never materialized -- the seal applies committed
  // content only and reports the denial by name rather than applying a stale, empty or
  // cache-only transform.
  // The deadline's own materialization first, then the denial: a decision that is still reachable is
  // taken here (against whatever source answers now) and only the unreachable one is denied.
  materializeDeferredPrivateHoldsAtDeadline();
  // The same deadline rule for touched coverage: a note whose span was completed before its tile was
  // published is answered now from a bounded window of its own span. Nothing is dropped and nothing is
  // applied twice (the discharge removes its obligation).
  materializeDeferredOverdubCompletionsAtDeadline();
  if (privateSourceOwedForSeal()) {
    privateSourceSealDenied_ = true;
    notePrivateSourceDenied(PrivateSourceDeny::OutputUnavailable);
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
    (void)preparePendingNoteCompanion(change, row);
    const EditPassId id = saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(row),
                                           EditPassType::Note, true, batchHeapReserveAdmitted);
    if (id != kInvalidEditPassId) {
      sealedIds.push_back(id);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
      if (companionRowsLogged < kMaxCompanionRowsLogged) {
        InvestigationCaptureLog::logSealCompanionRow(
            static_cast<unsigned>(id), static_cast<unsigned>(change.noteId),
            change.kind == PendingNoteChangeKind::Hide,
            static_cast<unsigned>(change.endTick));
        ++companionRowsLogged;
      }
#endif
    }
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  if (companionRowsToSeal > 0) {
    InvestigationCaptureLog::logSealCompanionBatch(
        static_cast<unsigned>(companionRowsToSeal), static_cast<unsigned>(sealedIds.size()),
        micros() - companionSealStartUs);
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

#if defined(LANE_GAP_MIDI_SPLIT)
// Overdub NoteOff split (owner ruling 2026-11-03, measurement image only): per entry occupancy of the deferred
// completion set (0..16, clamped) and per path, count / total / max; plus the 4 worst NoteOffs. RAM2 (NOLOAD): reset at
// the transport start with the MIDI spans, and every index is clamped, so stale boot bytes cannot address outside it.
namespace {
constexpr uint8_t kNoteOffOccupancyBins = 17;  // Loop::kDeferredOverdubCompletionCapacity + 1
struct NoteOffBin { uint32_t count, totalUs, maxUs; };
struct NoteOffPathStat { uint32_t count, totalUs, maxUs, coverageUs, retainUs, deriveUs; };
struct NoteOffWorst { uint32_t us, coverageUs, retainUs, deriveUs; uint8_t occupancy, path; };
struct NoteOffSpans {
  NoteOffBin byOccupancy[kNoteOffOccupancyBins];
  NoteOffPathStat byPath[LaneGapSpans::PathCount];
  NoteOffWorst worst[4];
  uint8_t highWater;
};
DMAMEM NoteOffSpans sNoteOffSpans;
constexpr uint8_t kIdleOwnerBins = 8;
DMAMEM uint32_t sCoverageCounters[LaneGapSpans::CoverageCounterCount];
DMAMEM uint32_t sIdleOwnersWhileOverdubbing[kIdleOwnerBins];
// Flash strings throughout the dump: a new RAM1 literal snaps DTCM by 4 KiB (RAM1 budget gate).
FLASHMEM const char* noteOffPathName(uint8_t p) {
  return p == LaneGapSpans::PathRetained ? PSTR("retained")
         : p == LaneGapSpans::PathFallback ? PSTR("fallback") : PSTR("prepared");
}
}  // namespace

FLASHMEM void LaneGapSpans::noteOverdubNoteOff(const NoteOffProbe& probe, uint32_t totalUs, uint8_t occupancyAfter) {
  auto& m = sNoteOffSpans;
  const uint8_t occupancy = probe.occupancyAtEntry < kNoteOffOccupancyBins ? probe.occupancyAtEntry
                                                                            : kNoteOffOccupancyBins - 1;
  const uint8_t path = probe.path < PathCount ? probe.path : PathPrepared;
  auto& bin = m.byOccupancy[occupancy];
  ++bin.count;
  bin.totalUs += totalUs;
  if (totalUs > bin.maxUs) bin.maxUs = totalUs;
  auto& st = m.byPath[path];
  ++st.count;
  st.totalUs += totalUs;
  if (totalUs > st.maxUs) st.maxUs = totalUs;
  st.coverageUs += probe.coverageUs;
  st.retainUs += probe.retainUs;
  st.deriveUs += probe.deriveUs;
  const uint8_t high = probe.occupancyAtEntry > occupancyAfter ? probe.occupancyAtEntry : occupancyAfter;
  if (high > m.highWater) m.highWater = high;
  uint8_t least = 0;
  for (uint8_t i = 1; i < 4; ++i) if (m.worst[i].us < m.worst[least].us) least = i;
  if (totalUs > m.worst[least].us)
    m.worst[least] = {totalUs, probe.coverageUs, probe.retainUs, probe.deriveUs, occupancy, path};
}

FLASHMEM __attribute__((noinline)) void LaneGapSpans::countCoverage(uint8_t counter, uint32_t by) {
  if (counter < CoverageCounterCount) sCoverageCounters[counter] += by;
}

FLASHMEM __attribute__((noinline)) void LaneGapSpans::countIdleOwner(uint8_t owner) {
  ++sIdleOwnersWhileOverdubbing[owner < kIdleOwnerBins ? owner : kIdleOwnerBins - 1];
}

FLASHMEM void LaneGapSpans::resetNoteOffSpans() {
  sNoteOffSpans = NoteOffSpans{};
  for (auto& v : sCoverageCounters) v = 0;
  for (auto& v : sIdleOwnersWhileOverdubbing) v = 0;
}

FLASHMEM void LaneGapSpans::dumpNoteOffSpans() {
  const auto& m = sNoteOffSpans;
  // remainder = total - coverage - retain - derive (the immediate consequence and everything else)
  for (uint8_t p = 0; p < PathCount; ++p) {
    const auto& st = m.byPath[p];
    if (st.count == 0) continue;
    const uint32_t named = st.coverageUs + st.retainUs + st.deriveUs;
    Serial.printf_P(PSTR("#CAP,%lu,DIAG,noteoff_spans,path=%s,n=%lu,total_us=%lu,max_us=%lu,coverage=%lu,retain=%lu,"
                  "derive=%lu,remainder=%lu\n"),
                  (unsigned long)micros(), noteOffPathName(p), (unsigned long)st.count, (unsigned long)st.totalUs,
                  (unsigned long)st.maxUs, (unsigned long)st.coverageUs, (unsigned long)st.retainUs,
                  (unsigned long)st.deriveUs, (unsigned long)(st.totalUs > named ? st.totalUs - named : 0));
  }
  Serial.printf_P(PSTR("#CAP,%lu,DIAG,noteoff_spans,occupancy,high_water=%u,bins="), (unsigned long)micros(),
                unsigned(m.highWater));
  bool first = true;
  for (uint8_t o = 0; o < kNoteOffOccupancyBins; ++o) {
    const auto& bin = m.byOccupancy[o];
    if (bin.count == 0) continue;
    Serial.printf_P(PSTR("%s%u:%lu:%lu:%lu"), first ? PSTR("") : PSTR("/"), unsigned(o), (unsigned long)bin.count,
                  (unsigned long)bin.totalUs, (unsigned long)bin.maxUs);
    first = false;
  }
  Serial.println();
  // Worker trace: entered/gate_reached/gate_passed/turn_no_session/turn_private_yield/turn_no_owed/turn_advanced/
  // turn_published/discharge_calls/discharged/blocked_occupancy/blocked_span/generation_changes/new_tiles_sealed.
  Serial.printf_P(PSTR("#CAP,%lu,DIAG,noteoff_spans,worker,counters="), (unsigned long)micros());
  for (uint8_t c = 0; c < CoverageCounterCount; ++c)
    Serial.printf_P(PSTR("%s%lu"), c ? PSTR("/") : PSTR(""), (unsigned long)sCoverageCounters[c]);
  Serial.printf_P(PSTR(",owners="));
  for (uint8_t o = 0; o < kIdleOwnerBins; ++o)
    Serial.printf_P(PSTR("%s%lu"), o ? PSTR("/") : PSTR(""), (unsigned long)sIdleOwnersWhileOverdubbing[o]);
  Serial.println();
  for (const auto& w : m.worst) {
    if (w.us == 0) continue;
    Serial.printf_P(PSTR("#CAP,%lu,DIAG,noteoff_spans,worst,us=%lu,occupancy=%u,path=%s,coverage=%lu,retain=%lu,derive=%lu\n"),
                  (unsigned long)micros(), (unsigned long)w.us, unsigned(w.occupancy),
                  noteOffPathName(w.path), (unsigned long)w.coverageUs, (unsigned long)w.retainUs,
                  (unsigned long)w.deriveUs);
  }
}
#endif
