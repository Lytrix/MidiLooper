//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackViewFill.h"
#include "Utils/NoteEditMem.h"
#include "Utils/IntervalProjection.h"

namespace {
NOTE_EDIT_MEM size_t frozenCaptureSize(const void* context) {
  return static_cast<const LoopEventStore::CaptureRange*>(context)->size();
}
NOTE_EDIT_MEM uint64_t frozenCaptureGeneration(const void* context) {
  const auto& range = *static_cast<const LoopEventStore::CaptureRange*>(context);
  return range.ready() ? range.generation() : 0;
}
NOTE_EDIT_MEM bool readFrozenCaptureEvent(const void* context, size_t index, MidiEvent& event) {
  const auto& range = *static_cast<const LoopEventStore::CaptureRange*>(context);
  bool atEnd = false;
  return range.read(range.generation(), index / LoopEventStoreConfig::CHUNK_CAPACITY,
                    index % LoopEventStoreConfig::CHUNK_CAPACITY, event, atEnd) && !atEnd;
}
}

NOTE_EDIT_MEM RuntimePlaybackViewSource RuntimePlaybackViewSource::fromCaptureRange(
    const LoopEventStore::CaptureRange& range) {
  RuntimePlaybackViewSource source;
  if (!range.ready()) return source;
  source.context = &range;
  source.size = frozenCaptureSize;
  source.readsByCopy = true;
  source.readEventAt = readFrozenCaptureEvent;
  source.generation = frozenCaptureGeneration;
  source.count = range.size();
  source.generationToken = range.generation();
  return source;
}

NOTE_EDIT_MEM bool RuntimePlaybackViewFill::begin(
    RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  if (destination_ != nullptr || destination.published) return false;
  baseline_ = nullptr;
  destination_ = nullptr;
  source_ = nullptr;
  sourceSize_ = nullptr;
  sourceEventAt_ = nullptr;
  sourceReadsByCopy_ = false;
  sourceGeneration_ = nullptr;
  sourceContext_ = nullptr;
  sourceGenerationToken_ = 0;
  sourceCount_ = 0;
  cursor_ = 0;
  copied_ = 0;
  scope_ = scope;
  const bool validCoverage = scope == RuntimePlaybackViewFillScope::RawCoverage
      ? coverage.loopLengthTicks == 0 && coverage.lengthTicks > 0 &&
        coverage.startPhase <= UINT32_MAX - (coverage.lengthTicks - 1)
      : coverage.isValid();
  if (!destination.storage.isPrewarmed() || !validCoverage ||
      (scope == RuntimePlaybackViewFillScope::AllSource && sourceCount > destination.storage.capacity) ||
      (sourceCount != 0 && source == nullptr)) {
    return false;
  }
  destination.resetMetadata();
  expected_ = sourceIdentity;
  destination.source = sourceIdentity;
  destination.coverage = coverage;
  destination.rawCoveragePrepared = scope == RuntimePlaybackViewFillScope::RawCoverage;
  destination.cursorAnchor = cursorAnchor;
  destination.ledgerBoundary = ledgerBoundary;
  destination_ = &destination;
  source_ = source;
  sourceCount_ = sourceCount;
  if (sourceCount == 0) {
    destination.sourceFilled = true;
  }
  return true;
}

NOTE_EDIT_MEM bool RuntimePlaybackViewFill::begin(
    RuntimePlaybackView& destination, const RuntimePlaybackViewSource& source,
    const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary, RuntimePlaybackViewFillScope scope) {
  if (source.context == nullptr || source.size == nullptr ||
      (source.readsByCopy ? source.readEventAt == nullptr : source.eventAt == nullptr) ||
      source.size(source.context) != source.count ||
      (source.generation != nullptr && source.generation(source.context) != source.generationToken)) {
    return false;
  }
  if (!begin(destination, reinterpret_cast<const MidiEvent*>(static_cast<uintptr_t>(1)), source.count, sourceIdentity,
             coverage, cursorAnchor, ledgerBoundary, scope)) return false;
  sourceContext_ = source.context;
  sourceSize_ = source.size;
  sourceReadsByCopy_ = source.readsByCopy;
  if (sourceReadsByCopy_) sourceReadEventAt_ = source.readEventAt;
  else sourceEventAt_ = source.eventAt;
  sourceGeneration_ = source.generation;
  sourceGenerationToken_ = source.generationToken;
  return true;
}

NOTE_EDIT_MEM bool RuntimePlaybackViewFill::begin(
    RuntimePlaybackView& destination, const RuntimeCompanionPasses& baseline,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  if (!baseline.uneditedBaseline(sourceIdentity) || destination.passOrigins == nullptr ||
      destination.canonicalRanks == nullptr || baseline.size() > UINT16_MAX ||
      (scope == RuntimePlaybackViewFillScope::RawCoverage ||
       scope == RuntimePlaybackViewFillScope::RecordCoverage) ||
      (scope == RuntimePlaybackViewFillScope::AllSource &&
       coverage.lengthTicks != coverage.loopLengthTicks)) return false;
  if (!begin(destination, baseline.data(), baseline.size(), sourceIdentity, coverage,
             cursorAnchor, ledgerBoundary, scope)) return false;
  baseline_ = &baseline; baselineGeneration_ = baseline.generation();
  // Even an empty typed source acquires its provenance receipt on step.
  destination.sourceFilled = false;
  return true;
}

NOTE_EDIT_MEM RuntimePlaybackViewFillResult RuntimePlaybackViewFill::step(size_t entryQuantum) {
  if (destination_ == nullptr || entryQuantum == 0) {
    return RuntimePlaybackViewFillResult::Denied;
  }
  if (destination_->published || !(destination_->source == expected_) ||
      destination_->storage.eventCount != copied_ ||
      (sourceContext_ != nullptr && sourceSize_(sourceContext_) != sourceCount_) ||
      (sourceGeneration_ != nullptr && sourceGeneration_(sourceContext_) != sourceGenerationToken_) ||
      (baseline_ != nullptr && (baseline_->generation() != baselineGeneration_ || !baseline_->uneditedBaseline(expected_)))) {
    if (!destination_->published) { destination_->sourceFilled = false; destination_->passOriginsReady = false; }
    destination_ = nullptr; return RuntimePlaybackViewFillResult::Denied;
  }
  size_t remaining = entryQuantum;
  while (remaining > 0 && cursor_ < sourceCount_) {
    MidiEvent event;
    if (sourceReadsByCopy_) {
      if (!sourceReadEventAt_(sourceContext_, cursor_, event)) {
        destination_->sourceFilled = false;
        destination_ = nullptr;
        return RuntimePlaybackViewFillResult::Denied;
      }
    } else {
      event = sourceEventAt_ != nullptr ? sourceEventAt_(sourceContext_, cursor_) : source_[cursor_];
    }
    const bool inRecord = scope_ != RuntimePlaybackViewFillScope::RecordCoverage ||
        LoopEventStore::projectRecordStopTick(event, destination_->coverage.loopLengthTicks);
    const bool retain = inRecord && (scope_ == RuntimePlaybackViewFillScope::AllSource ||
        (scope_ == RuntimePlaybackViewFillScope::RawCoverage
          ? event.tick >= destination_->coverage.startPhase &&
            event.tick - destination_->coverage.startPhase < destination_->coverage.lengthTicks
          : destination_->coverage.contains(IntervalProjection::playbackEventPhase(
              event.tick, destination_->coverage.loopLengthTicks))));
    if (retain) {
      if (!destination_->storage.append(event)) {
        destination_->sourceFilled = false;
        destination_ = nullptr;
        return RuntimePlaybackViewFillResult::Denied;
      }
      if (baseline_ != nullptr) {
        destination_->passOrigins[copied_] = baseline_->origins()[cursor_];
        destination_->canonicalRanks[copied_] = cursor_;
      }
      ++copied_;
    }
    ++cursor_;
    --remaining;
  }
  if (cursor_ == sourceCount_) {
    destination_->passOriginsReady = destination_->canonicalRanksReady = baseline_ != nullptr;
    destination_->passOriginSource = baseline_ != nullptr ? expected_ : PlaybackViewSourceIdentity{};
    destination_->sourceFilled = true;
    destination_ = nullptr;
    return RuntimePlaybackViewFillResult::Complete;
  }
  return RuntimePlaybackViewFillResult::Pending;
}

NOTE_EDIT_MEM void RuntimePlaybackViewFill::discard() {
  // Borrowed source/destination only: detach without clearing source storage.
  *this = RuntimePlaybackViewFill{};
}
