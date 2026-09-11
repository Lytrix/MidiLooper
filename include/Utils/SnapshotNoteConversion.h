//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "Globals.h"
#include "LoopSnapshot.h"
#include "OverlapNoteIdObservation.h"
#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

/// Snapshot-note → `DisplayNote` field map. Not display policy and not Loop-owned.
/// Paint (`gatherDrawSourceBars`) and occupy (`rebuildOverdubSourceSpanCache`) share this.
inline NoteUtils::DisplayNote displayNoteFromSnapshotNote(const LoopSnapshotNote& note) {
  NoteUtils::DisplayNote out{};
  out.noteId = note.noteId;
  out.note = note.pitch;
  out.velocity = note.velocity;
  out.startTick = note.startTick;
  out.endTick = note.endTick;
  return out;
}

inline void appendDisplayNoteUniqueById(NoteUtils::DisplayNoteVec& out,
                                        const NoteUtils::DisplayNote& note) {
  if (note.noteId != kInvalidNoteId) {
    for (const auto& existing : out) {
      if (existing.noteId != note.noteId) {
        continue;
      }
      // Wrap On→loopEnd + head share one NoteId with different starts. Treating
      // id as globally unique drops the second segment (`193108` STOPPED frame
      // kept head @0 only while reconstruct had On@2151).
      if (existing.startTick == note.startTick) {
        return;
      }
    }
  }
  out.push_back(note);
}

inline void appendSnapshotNotesForBarRange(const LoopSnapshot& snapshot, uint32_t firstBar,
                                           uint32_t lastBar, NoteUtils::DisplayNoteVec& out) {
  if (firstBar > lastBar) {
    return;
  }
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    for (uint16_t pitch = 0; pitch < kLoopSnapshotPitchCount; ++pitch) {
      const auto membership = snapshot.noteMembership(static_cast<uint8_t>(pitch), bar);
      forEachSnapshotMembershipReference(
          membership.get(), [&out](const LoopSnapshotNoteRef& reference) {
            if (!reference) {
              return;
            }
            appendDisplayNoteUniqueById(out, displayNoteFromSnapshotNote(*reference));
          });
    }
  }
}

/// Prepared-hit overlap ids at one pitch. Walks hold bars only (±1 for analyze abut).
inline void collectSnapshotAnalyzeOverlapNoteIds(
    const LoopSnapshot& snapshot, uint8_t pitch, uint32_t incomingStart, uint32_t incomingEnd,
    uint32_t loopLength, NoteId excludeId, OverlapNoteIdSet& out,
    OverlapNoteIdObservation::OverlapCandidateQueryStats* stats = nullptr) {
  out.clear();
  uint32_t incomingLinearStart = 0;
  uint32_t incomingLinearEnd = 0;
  if (!OverlapNoteIdObservation::linearSoundingSpan(incomingStart, incomingEnd, loopLength,
                                                    incomingLinearStart, incomingLinearEnd)) {
    return;
  }
  const uint32_t barTicks = Config::TICKS_PER_BAR;
  const uint32_t barCount = barTicks == 0 ? 0u : (loopLength + barTicks - 1u) / barTicks;
  if (barCount == 0) {
    return;
  }

  auto visitSegment = [&](uint32_t segStart, uint32_t segEnd) {
    if (segStart >= segEnd) {
      return;
    }
    int32_t first = static_cast<int32_t>(segStart / barTicks) - 1;
    uint32_t last = segEnd / barTicks;
    if (first < 0) {
      first = 0;
    }
    if (last >= barCount) {
      last = barCount - 1u;
    }
    for (uint32_t bar = static_cast<uint32_t>(first); bar <= last; ++bar) {
      if (stats != nullptr) {
        ++stats->barsScanned;
      }
      const auto membership = snapshot.noteMembership(pitch, bar);
      forEachSnapshotMembershipReference(
          membership.get(), [&](const LoopSnapshotNoteRef& reference) {
            if (stats != nullptr) {
              ++stats->identityLookupScanCount;
            }
            if (!reference || reference->noteId == kInvalidNoteId ||
                reference->noteId == excludeId || reference->pitch != pitch) {
              return;
            }
            if (OverlapNoteIdObservation::existingNoteOverlapsIncomingHoldInclusive(
                    reference->startTick, reference->endTick, incomingStart, incomingEnd,
                    loopLength)) {
              (void)out.insert(reference->noteId);
            }
          });
    }
  };

  if (incomingLinearEnd <= loopLength) {
    visitSegment(incomingLinearStart, incomingLinearEnd);
  } else {
    visitSegment(incomingLinearStart, loopLength);
    visitSegment(0, incomingLinearEnd - loopLength);
  }
  if (stats != nullptr) {
    stats->preparedHit = true;
    stats->candidateCount = static_cast<uint32_t>(out.size());
  }
}
