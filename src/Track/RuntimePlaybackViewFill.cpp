//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackViewFill.h"
#include "Loop.h"
#include "Utils/NoteEditMem.h"
#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackSourceReadGuard.h"

namespace {
NOTE_EDIT_MEM size_t liveCaptureSize(const void* context) {
  return static_cast<const Loop*>(context)->capture.store.size();
}
NOTE_EDIT_MEM const MidiEvent& liveCaptureEventAt(const void* context, size_t index) {
  return static_cast<const Loop*>(context)->capture.store.at(index);
}

NOTE_EDIT_MEM uint64_t liveCaptureGeneration(const void* context) {
  const Loop& loop = *static_cast<const Loop*>(context);
  return loop.captureActive() ? loop.captureContentGeneration() : 0;
}

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
NOTE_EDIT_MEM bool frozenBlockMayIntersect(const void* context, uint64_t generation,
    size_t firstRow, uint32_t start, uint32_t length, uint32_t loopLength, bool& mayIntersect) {
  return static_cast<const LoopEventStore::CaptureRange*>(context)->blockMayIntersect(
      generation, firstRow, start, length, loopLength, mayIntersect);
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
  source.blockMayIntersect = frozenBlockMayIntersect;
  source.count = range.size();
  source.generationToken = range.generation();
  return source;
}
NOTE_EDIT_MEM RuntimePlaybackViewSource RuntimePlaybackViewSource::fromFrozenRecordCapture(
    const LoopEventStore::CaptureRange& range, uint64_t frozenContentToken) {
  RuntimePlaybackViewSource source;
  if (!range.ready() || frozenContentToken == 0) return source;
  source = fromCaptureRange(range);
  source.origin = PlaybackWindowOrigin::Capture;
  source.captureContentToken = frozenContentToken;
  return source;
}

NOTE_EDIT_MEM RuntimePlaybackViewSource RuntimePlaybackViewSource::fromLiveCapture(const Loop& loop, bool appendStable) {
  RuntimePlaybackViewSource source;
  PlaybackSourceReadGuard guard;
  if (!loop.captureActive() || loop.captureContentGeneration() == 0 ||
      loop.capture.phase.generation() == 0) return source;
  if (!loop.capture.store.beginSourceCursor(source.captureCursor)) return source;
  source.context = &loop;
  source.size = liveCaptureSize;
  source.eventAt = liveCaptureEventAt;
  source.generation = liveCaptureGeneration;
  source.origin = PlaybackWindowOrigin::Capture;
  source.count = loop.capture.store.size();
  source.generationToken = loop.captureContentGeneration();
  source.captureContentToken = source.generationToken;
  source.mutableCapture = true;
  source.appendStableCapture = appendStable;
  source.captureProjection = loop.liveCaptureProjectionIdentity();
  return source;
}


NOTE_EDIT_MEM bool RuntimePlaybackViewFill::begin(
    RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  if (destination_ != nullptr || destination.published || scope == RuntimePlaybackViewFillScope::SnapshotWindow) return false;
  baseline_ = nullptr;
  destination_ = nullptr;
  source_ = nullptr;
  sourceSize_ = nullptr;
  sourceEventAt_ = nullptr;
  sourceGeneration_ = nullptr;
  sourceContext_ = nullptr;
  sourceGenerationToken_ = 0;
  blockMayIntersect_ = nullptr;
  inspectBlockRows_ = false;
  liveCaptureCursor_.discard();
  liveCaptureProjection_ = {};
  flags_ = 0;
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
  { // Only fixed live descriptor validation; destination setup is outside.
  PlaybackSourceReadGuard guard(source.mutableCapture);
  if ((source.origin == PlaybackWindowOrigin::Capture) !=
      (sourceIdentity.origin == PlaybackWindowOrigin::Capture)) return false;
  if (source.origin == PlaybackWindowOrigin::Capture &&
      (sourceIdentity.origin != PlaybackWindowOrigin::Capture ||
       sourceIdentity.snapshotIdentity != nullptr ||
       sourceIdentity.captureContentGeneration == 0 ||
       sourceIdentity.captureContentGeneration != source.captureContentToken ||
       source.generation == nullptr)) return false;
  if (source.context == nullptr || source.size == nullptr ||
      (source.readsByCopy ? source.readEventAt == nullptr : source.eventAt == nullptr) ||
      source.size(source.context) != source.count ||
      (source.generation != nullptr && source.generation(source.context) != source.generationToken)) {
    return false;
  }
  if (source.mutableCapture &&
      (!source.captureProjection.phaseGeneration || source.captureCursor.position() != 0 ||
       source.captureCursor.extent() != source.count ||
       !static_cast<const Loop*>(source.context)->capture.store.sourceCursorCurrent(source.captureCursor) ||
       !(source.captureProjection == static_cast<const Loop*>(source.context)->liveCaptureProjectionIdentity())))
    return false;
  }
  if (!begin(destination, reinterpret_cast<const MidiEvent*>(static_cast<uintptr_t>(1)), source.count, sourceIdentity,
             coverage, cursorAnchor, ledgerBoundary, scope)) return false;
  sourceContext_ = source.context;
  sourceSize_ = source.size;
  setFlag(kSourceReadsByCopyBit, source.readsByCopy);
  if (sourceReadsByCopy()) sourceReadEventAt_ = source.readEventAt;
  else sourceEventAt_ = source.eventAt;
  sourceGeneration_ = source.generation;
  sourceGenerationToken_ = source.generationToken;
  blockMayIntersect_ = scope == RuntimePlaybackViewFillScope::RecordCoverage
      ? source.blockMayIntersect : nullptr;
  setFlag(kGuardLiveCaptureBit, source.mutableCapture);
  setFlag(kAppendStableCaptureBit, source.mutableCapture && source.appendStableCapture);
  if (source.mutableCapture) {
    liveCaptureCursor_ = source.captureCursor;
    liveCaptureProjection_ = source.captureProjection;
  }
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

NOTE_EDIT_MEM bool RuntimePlaybackViewFill::begin(
    RuntimePlaybackView& destination, LoopSnapshot::EventWindowCursor& source,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary) {
  if (source.idle() || source.retiring() || source.complete() || source.readUnits() != 0 ||
      source.startTick() != coverage.startPhase || source.lengthTicks() != coverage.lengthTicks ||
      source.loopLengthTicks() != coverage.loopLengthTicks || destination.storage.capacity > UINT16_MAX ||
      destination.passOrigins == nullptr || destination.canonicalRanks == nullptr) return false;
  if (!begin(destination, nullptr, 0, sourceIdentity, coverage, cursorAnchor, ledgerBoundary)) return false;
  scope_ = RuntimePlaybackViewFillScope::SnapshotWindow;
  snapshotSource_ = &source;
  baselineGeneration_ = source.traversalGeneration();
  sourceCount_ = LoopSnapshot::EventWindowCursor::kMaximumReadUnits;
  destination.sourceFilled = false;
  return true;
}

NOTE_EDIT_MEM RuntimePlaybackViewFillResult RuntimePlaybackViewFill::step(size_t entryQuantum) {
  if (destination_ == nullptr || entryQuantum == 0) {
    return RuntimePlaybackViewFillResult::Denied;
  }
  if (scope_ == RuntimePlaybackViewFillScope::SnapshotWindow) {
    if (destination_->published || !(destination_->source == expected_) ||
        destination_->storage.eventCount != copied_ || snapshotSource_->idle() || snapshotSource_->retiring() ||
        snapshotSource_->traversalGeneration() != baselineGeneration_ || snapshotSource_->readUnits() != cursor_) {
      if (!destination_->published) destination_->sourceFilled = false;
      destination_ = nullptr;
      return RuntimePlaybackViewFillResult::Denied;
    }
    for (size_t unit = 0; unit < entryQuantum; ++unit) {
      LoopSnapshotEvent row{};
      const auto result = snapshotSource_->step(row);
      cursor_ = snapshotSource_->readUnits();
      if (result == LoopSnapshot::EventWindowCursor::ReadResult::Event) {
        if (!destination_->storage.append(row.value)) {
          ++copied_; // Includes the matching row that exceeded fixed capacity.
          destination_->sourceFilled = false;
          destination_ = nullptr;
          return RuntimePlaybackViewFillResult::Denied;
        }
        destination_->storage.phases[copied_] = row.canonicalOrder;
        destination_->passOrigins[copied_] = {
            static_cast<uint16_t>(row.eventId >> 16), static_cast<uint16_t>(row.eventId)};
        ++copied_;
      } else if (result == LoopSnapshot::EventWindowCursor::ReadResult::Complete) {
        destination_->sourceFilled = true;
        destination_ = nullptr;
        return RuntimePlaybackViewFillResult::Complete;
      } else if (result == LoopSnapshot::EventWindowCursor::ReadResult::Inactive) {
        destination_->sourceFilled = false;
        destination_ = nullptr;
        return RuntimePlaybackViewFillResult::Denied;
      }
    }
    return RuntimePlaybackViewFillResult::Pending;
  }
  bool sourceCurrent = false;
  {
    PlaybackSourceReadGuard guard(ownsLiveGuard());
    sourceCurrent = liveCaptureCurrent() && (ownsLiveGuard() ||
        ((sourceContext_ == nullptr || sourceSize_(sourceContext_) == sourceCount_) &&
         (sourceGeneration_ == nullptr || sourceGeneration_(sourceContext_) == sourceGenerationToken_)));
  }
  if (destination_->published || !(destination_->source == expected_) ||
      destination_->storage.eventCount != copied_ ||
      !sourceCurrent ||
      (baseline_ != nullptr && (baseline_->generation() != baselineGeneration_ || !baseline_->uneditedBaseline(expected_)))) {
    if (continuingOverlay() && !destination_->published && !sourceCurrent) {
      // A pass commit (or any re-based capture) invalidated the live-overlay half. The window must not stay complete: the
      // next build re-derives the base from the now-committed source instead of continuing to treat these rows as live.
      setRefusal(RuntimePlaybackViewFillRefusal::ContinuationStale);
      destination_->sourceFilled = false;
      destination_->complete = false;
    }
    if (!destination_->published) { destination_->sourceFilled = false; destination_->passOriginsReady = false; }
    destination_ = nullptr; return RuntimePlaybackViewFillResult::Denied;
  }
  size_t remaining = entryQuantum;
  while (remaining > 0 && cursor_ < sourceCount_) {
    // One inspected summary consumes one unit. On a proven miss the whole
    // 32-row group is accounted for; an invalid hint inspects every row.
    if (blockMayIntersect_ && !inspectBlockRows_ && cursor_ % kCaptureIndexRows == 0) {
      bool mayIntersect = true;
      (void)blockMayIntersect_(sourceContext_, sourceGenerationToken_, cursor_,
          destination_->coverage.startPhase, destination_->coverage.lengthTicks,
          destination_->coverage.loopLengthTicks, mayIntersect);
      --remaining;
      if (!mayIntersect) {
        cursor_ += std::min(kCaptureIndexRows, sourceCount_ - cursor_);
        continue;
      }
      inspectBlockRows_ = true;
      if (!remaining) break;
    }
    MidiEvent event;
    if (ownsLiveGuard()) {
      LoopEventStoreSourceResult result;
      { // Validation and one row/charged transition share the same exclusion.
        PlaybackSourceReadGuard guard;
        const auto& loop = *static_cast<const Loop*>(sourceContext_);
        result = liveCaptureCurrent()
            ? loop.capture.store.stepSourceCursor(liveCaptureCursor_, event)
            : LoopEventStoreSourceResult::Denied;
      }
      if (result == LoopEventStoreSourceResult::Chunk) {
        --remaining; // A directory advance is charged, never hidden in an ordinal read.
        continue;
      }
      if (result != LoopEventStoreSourceResult::Row) {
        destination_->sourceFilled = false;
        destination_ = nullptr;
        return RuntimePlaybackViewFillResult::Denied;
      }
    } else if (sourceReadsByCopy()) {
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
        ((scope_ == RuntimePlaybackViewFillScope::RawCoverage || appendStableCapture())
          ? event.tick >= destination_->coverage.startPhase &&
            event.tick - destination_->coverage.startPhase < destination_->coverage.lengthTicks
          : destination_->coverage.contains(IntervalProjection::playbackEventPhase(
              event.tick, destination_->coverage.loopLengthTicks))));
    if (retain) {
      if (!destination_->storage.append(event)) {
        if (continuingOverlay()) {
          // Base + overlay must fit **one** window: refuse by name and leave no partial window behind. Never truncate the
          // overlay and never evict committed rows to make it fit.
          refuseOverlay(RuntimePlaybackViewFillRefusal::CombinedCapacityExceeded);
          return RuntimePlaybackViewFillResult::Denied;
        }
        destination_->sourceFilled = false;
        destination_ = nullptr;
        return RuntimePlaybackViewFillResult::Denied;
      }
      if (baseline_ != nullptr) {
        destination_->passOrigins[copied_] = baseline_->origins()[cursor_];
        destination_->canonicalRanks[copied_] = cursor_;
      } else if (continuingOverlay()) {
        destination_->passOrigins[copied_] = {kLiveOverlayPassOrigin, static_cast<uint16_t>(copied_)};
        destination_->canonicalRanks[copied_] = kLiveOverlayCanonicalRank;
      }
      ++copied_;
    }
    ++cursor_;
    if (cursor_ % kCaptureIndexRows == 0) inspectBlockRows_ = false;
    --remaining;
  }
  if (cursor_ == sourceCount_) {
    if (continuingOverlay()) {
      // One provenance table for both halves: committed rows keep theirs (written by the base fill) and overlay rows carry
      // the sentinel. `passOriginSource` stays the base's, because that is the authority the committed rows came from. The
      // canonical committed ranks no longer describe the whole window, so the table is not claimed as a rank map.
      destination_->passOriginsReady = true;
      destination_->canonicalRanksReady = false;
    } else {
      destination_->passOriginsReady = destination_->canonicalRanksReady = baseline_ != nullptr;
      destination_->passOriginSource = baseline_ != nullptr ? expected_ : PlaybackViewSourceIdentity{};
    }
    destination_->sourceFilled = true;
    if (!appendStableCapture()) destination_ = nullptr;
    return RuntimePlaybackViewFillResult::Complete;
  }
  return RuntimePlaybackViewFillResult::Pending;
}

NOTE_EDIT_MEM bool RuntimePlaybackViewFill::liveCaptureCurrent() const {
  if (!ownsLiveGuard()) return true;
  PlaybackSourceReadGuard guard;
  if (sourceContext_ == nullptr || sourceSize_ == nullptr || sourceGeneration_ == nullptr)
    return false;
  const auto& loop = *static_cast<const Loop*>(sourceContext_); // retained by caller/Track
  auto projection = loop.liveCaptureProjectionIdentity();
  // Explicit raw-coverage Record builds do not use the advisory growing length.
  if (appendStableCapture() && projection.phase == CapturePhase::Record)
    projection.loopLengthTicks = liveCaptureProjection_.loopLengthTicks;
  return liveCaptureProjection_.phaseGeneration &&
      liveCaptureProjection_ == projection &&
      loop.capture.store.sourceCursorCurrent(liveCaptureCursor_) &&
      (appendStableCapture()
        ? sourceSize_(sourceContext_) >= sourceCount_ && sourceGeneration_(sourceContext_) != 0
        : sourceSize_(sourceContext_) == sourceCount_ &&
          sourceGeneration_(sourceContext_) == sourceGenerationToken_);
}

NOTE_EDIT_MEM bool RuntimePlaybackViewFill::extendLiveCapture(const PlaybackViewSourceIdentity& identity) {
  PlaybackSourceReadGuard guard;
  if (!appendStableCapture() || !destination_ || destination_->published || !liveCaptureCurrent() ||
      identity.origin != PlaybackWindowOrigin::Capture || identity.snapshotIdentity != nullptr ||
      identity.playbackRevision != expected_.playbackRevision ||
      identity.sourceGeneration != expected_.sourceGeneration ||
      identity.captureContentGeneration != sourceGeneration_(sourceContext_)) return false;
  const auto& loop = *static_cast<const Loop*>(sourceContext_);
  if (!loop.capture.store.extendSourceCursor(liveCaptureCursor_)) return false;
  sourceCount_ = liveCaptureCursor_.extent();
  sourceGenerationToken_ = identity.captureContentGeneration;
  expected_ = identity;
  destination_->source = identity;
  destination_->sourceFilled = cursor_ == sourceCount_;
  destination_->complete = false;
  return true;
}

NOTE_EDIT_MEM bool RuntimePlaybackViewFill::beginLiveContinuation(
    RuntimePlaybackView& destination, const RuntimePlaybackViewSource& live,
    const PlaybackViewSourceIdentity& identity) {
  PlaybackSourceReadGuard guard;
  // The committed base must already be **filled and ordered as one window**; this call continues that build, it never
  // starts a second one.
  if (destination_ != nullptr || !destination.complete || destination.published ||
      destination.passOrigins == nullptr || !destination.storage.isPrewarmed()) return false;
  // One provenance table for both halves is the contract: without it a consumer could not tell a committed row from an
  // overlay row, and the occupy/closure rule (`221334`) would have no input. Fail closed here.
  if (!destination.passOriginsReady) {
    setRefusal(RuntimePlaybackViewFillRefusal::ContinuationBaseWithoutProvenance);
    return false;
  }
  const bool liveShape = live.mutableCapture && live.appendStableCapture && live.context != nullptr &&
      live.size != nullptr && live.eventAt != nullptr && live.generation != nullptr &&
      live.generationToken != 0 && live.count <= destination.storage.capacity;
  // One identity for the whole window: the overlay half shares the base's revision/generation and adds the live capture
  // generation, so a consumer can tell which capture the overlay corresponds to.
  const bool identityShape = identity.origin == PlaybackWindowOrigin::Capture &&
      identity.snapshotIdentity == nullptr && identity.captureContentGeneration != 0 &&
      identity.captureContentGeneration == live.generationToken &&
      identity.playbackRevision == expected_.playbackRevision &&
      identity.sourceGeneration == expected_.sourceGeneration;
  const auto* loop = liveShape ? static_cast<const Loop*>(live.context) : nullptr;
  const bool cursorShape = loop != nullptr &&
      loop->capture.store.sourceCursorCurrent(live.captureCursor) &&
      loop->captureContentGeneration() == live.generationToken &&
      live.captureProjection == loop->liveCaptureProjectionIdentity();
  if (!liveShape || !identityShape || !cursorShape) {
    setRefusal(RuntimePlaybackViewFillRefusal::ContinuationIdentityMismatch);
    return false;
  }
  sourceContext_ = live.context;
  sourceSize_ = live.size;
  setFlag(kSourceReadsByCopyBit, false);
  sourceEventAt_ = live.eventAt;
  sourceGeneration_ = live.generation;
  sourceGenerationToken_ = live.generationToken;
  liveCaptureCursor_ = live.captureCursor;
  liveCaptureProjection_ = live.captureProjection;
  setFlag(kGuardLiveCaptureBit, true);
  setFlag(kAppendStableCaptureBit, true);
  setFlag(kOverlayActiveBit, true);
  setRefusal(RuntimePlaybackViewFillRefusal::None);
  // The base's provenance stays authoritative; this half only appends.
  baseline_ = nullptr;
  baselineGeneration_ = 0;
  destination_ = &destination;
  sourceCount_ = live.count;
  cursor_ = 0;
  expected_ = identity;
  destination.source = identity;
  destination.sourceFilled = cursor_ == sourceCount_;
  // Appending is a content change: the window is no longer complete until its ordered index covers the appended rows.
  destination.complete = false;
  return true;
}

NOTE_EDIT_MEM void RuntimePlaybackViewFill::refuseOverlay(RuntimePlaybackViewFillRefusal reason) {
  setRefusal(reason);
  if (destination_ != nullptr && !destination_->published) {
    // No partial window: the caller may discard, but nothing here can be mistaken for a servable candidate.
    destination_->storage.clear();
    destination_->sourceFilled = false;
    destination_->complete = false;
    destination_->passOriginsReady = false;
    destination_->canonicalRanksReady = false;
  }
  destination_ = nullptr;
}

NOTE_EDIT_MEM void RuntimePlaybackViewFill::discard() {
  // Borrowed source/destination only: detach without clearing source storage.
  *this = RuntimePlaybackViewFill{};
}
