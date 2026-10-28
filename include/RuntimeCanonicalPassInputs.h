// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "LoopEventStore.h"
#include "RuntimeCompanionPasses.h"
struct LoopPasses;

/// Raw sealed passes -> separate effective pass inputs. The caller selects
/// active participating passes in canonical order and filters active Note rows.
/// Retain immutable descriptors/chunk refs/history and source token until
/// discard; invalidate the source BEFORE releasing/replacing refs or pool reuse.
/// No capture freezing, canonical publication or chunk ownership transfer here.
class RuntimeCanonicalPassInputs {
 public:
  struct SealedPass { const uint16_t* chunks; size_t count; };
  /// Completed canonical indexes from the same frozen capture. The producer
  /// advances storageGeneration before clearing/reusing the shared index array.
  struct PreparedCaptureOrder {
    const uint16_t* indexes;
    size_t count;
    const uint64_t& storageGeneration;
  };
  /// Borrow live counters; invalidate before pass/history mutation or release.
  /// The counters and canonical owners must outlive discard. Storage generation
  /// must also advance for pointer relocation without an audible revision change.
  struct CurrentSource {
    const uint32_t& playbackRevision;
    const uint32_t& sourceGeneration;
    const uint32_t& loopLengthTicks;
    const uint64_t& storageGeneration;
  };
  /// Live owner fields, not copied counters. Owners and immutable pass content
  /// must outlive discard; advance revision/epoch before mutation or release.
  /// This receipt does not retain chunks or append capture as an input pass.
  struct FrozenSource {
    const uint32_t& playbackRevision;
    const uint32_t& sourceGeneration;
    const uint32_t& transportCommandEpoch;
    const uint32_t& loopLengthTicks;
    const CaptureAdmissionStaging& capture;
  };
  RuntimeCanonicalPassInputs();
  ~RuntimeCanonicalPassInputs();
  RuntimeCanonicalPassInputs(const RuntimeCanonicalPassInputs&) = delete;
  RuntimeCanonicalPassInputs& operator=(const RuntimeCanonicalPassInputs&) = delete;
  bool prewarm(size_t capacity);
  size_t fixedExternalBytes() const { return capacity_ * (2 * sizeof(MidiEvent) + 2 * sizeof(RuntimeCompanionPasses::Pass) + sizeof(SealedPass) + 2 * sizeof(uint16_t)) + (capacity_ ? RuntimePlaybackViewConfig::kRecordsPerBridge * sizeof(const EditPass*) : 0); }
  static uint64_t requiredGrainsUpperBound(size_t capacity, size_t passCount, size_t rowCount, size_t quantum);
  static uint64_t requiredSelectedGrainsUpperBound(size_t capacity, size_t rawHistoryCount, size_t quantum);
  static uint64_t requiredFrozenOverdubGrainsUpperBound(size_t capacity, size_t rawHistoryCount, size_t quantum);
  static uint64_t requiredFrozenTailGrainsUpperBound(size_t capacity, size_t historyCount, size_t quantum);
  static uint64_t requiredPreparedFrozenTailGrainsUpperBound(size_t capacity, size_t historyCount, size_t quantum);
  bool begin(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
             const SealedPass* passes, size_t passCount, const EditPass* history, size_t rowCount,
             const PlaybackViewSourceIdentity& source, uint32_t loopLength);
  /// Freeze the LoopPasses lifetime/content externally. Selects active nonempty
  /// record first, then overdubs by unique mergeSequence; ambiguous duplicate
  /// sequences deny preparation, never reinterpret the legacy sort's ties.
  /// Filters active Note history by pointer, including collapse across skipped
  /// rows. Both source vector counts must fit their prewarmed horizons.
  bool begin(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
             const LoopPasses& passes, const PlaybackViewSourceIdentity& source, uint32_t loopLength);
  bool beginCurrent(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
                    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
                    const CurrentSource& live);
  // Called only by a successful newest-pass append, or snapshot identity
  // refresh at the same content revision, with unchanged history.
  // Retains completed normalized passes; reselects descriptors and resumes the
  // first incomplete/new pass. A frozen tail may be retained after its transfer.
  bool resumeCurrentAfterAppend(const LoopPasses& passes, uint32_t priorRevision,
      const PlaybackViewSourceIdentity& source, const CurrentSource& live,
      bool committedFrozenTail);
  size_t completedPassCount() const { return pass_; }
  const CurrentSource* currentSourceOwner() const { return current_ ? &*current_ : nullptr; }
  // Scratch has no semantic input while ready. The existing per-pass merge
  // fills it in the same Q-sized turns, retaining order within each pass.
  MidiEvent* normalizedScratch() const { return raw_; }
  RuntimeCompanionPasses::Pass* normalizedDescriptors() const { return nextOutputs_; }
  void claimNormalizedScratch(const RuntimeCompanionPasses& owner, uint64_t generation) const {
    normalizedOwner_ = &owner; normalizedGeneration_ = generation;
  }
  bool normalizedScratchMatches(const RuntimeCompanionPasses& owner, uint64_t generation) const {
    return normalizedOwner_ == &owner && normalizedGeneration_ == generation;
  }
  bool adoptCommittedNormalization(const LoopPasses& passes,
      const PlaybackViewSourceIdentity& source, const CurrentSource& live,
      const RuntimeCompanionPasses& resolved);
  bool idle() const { return phase_ == Phase::Idle; }
  /// DEC-172 correction 3: structural completeness without value revalidation.
  bool stateAllowsBinding() const { return phase_ == Phase::Complete && source_; }
  bool captureAttached() const { return includeCapture_; }
  bool borrowsCaptureOrder() const { return preparedCaptureOrder_.has_value(); }
  bool beginFrozen(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
                   const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
                   const FrozenSource& live);
  /// Includes retained frozen capture as the final/newest overdub pass. The
  /// producer must have completed IDs, wrap closure and retention decisions.
  /// Private canonical indexes preserve staging's audible order and phases.
  bool beginFrozenOverdub(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
                          const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
                          const FrozenSource& live);
  /// Reuse a complete, still-current sealed prefix. No pass selection/read or
  /// prefix history replay; appends only the newly frozen capture. Invalidates
  /// existing consumers via generation before writing any candidate output.
  /// Original source token, history and owners must remain immutable/alive.
  bool appendFrozenOverdub(const FrozenSource& live, const PreparedCaptureOrder* preparedOrder = nullptr);
  RuntimePlaybackOrderBuildResult step(size_t quantum);
  bool ready() const;
  /// DEC-170 progress probe: a compact snapshot of the builder's advance state, so a fixture/reporter can tell a
  /// *productive* grain (any counter moved) from a nonproductive `Pending` (no counter moved). Report-only — no
  /// admission, escape or policy reads it. `phase` is the internal `Phase` value; `completedPasses`/`totalPasses`
  /// are `pass_`/`passCount_`, `eventCount` is `count_`, `cursor` is `copy_` (copy/read phases) or `selectionCursor_`
  /// (select/filter phases), `readCursor` is the sealed-chunk read cursor size, `captureOrderedCount` is the frozen
  /// capture order cardinality.
  struct ProgressState {
    uint8_t phase = 0;
    size_t completedPasses = 0;
    size_t totalPasses = 0;
    size_t eventCount = 0;
    size_t cursor = 0;
    size_t readCursor = 0;
    size_t captureOrderedCount = 0;
    RuntimeCompanionApply::ProgressState apply;
    bool operator==(const ProgressState& o) const {
      return phase == o.phase && completedPasses == o.completedPasses && totalPasses == o.totalPasses &&
             eventCount == o.eventCount && cursor == o.cursor && readCursor == o.readCursor &&
             captureOrderedCount == o.captureOrderedCount && apply == o.apply;
    }
    bool operator!=(const ProgressState& o) const { return !(*this == o); }
  };
  ProgressState progressState() const;
  const CaptureAdmissionStaging* frozenCapture() const;
  /// Test/diagnostic probe: is a frozen (job-owned) capture source still bound?
  bool hasFrozenBinding() const { return live_.has_value(); }
  bool frozenSourceCurrent(const CaptureAdmissionStaging& capture, uint32_t epoch) const {
    return live_ && &live_->capture == &capture && commandEpoch_ == epoch && valid();
  }
  bool matchesCanonicalOwner(const LoopPasses& passes, const CurrentSource& owner) const {
    return ready() && loopPasses_ == &passes && current_ &&
        &current_->playbackRevision == &owner.playbackRevision &&
        &current_->sourceGeneration == &owner.sourceGeneration &&
        &current_->loopLengthTicks == &owner.loopLengthTicks &&
        &current_->storageGeneration == &owner.storageGeneration;
  }
  /// Stopped recovery may rebind only the same live epoch owner while every
  /// content/storage/capture guard still holds. Does not revive denied work or
  /// change its payload generation; the old PLAYING bridge epoch stays invalid.
  bool retargetFrozenCommandEpoch(const uint32_t& epoch, const CaptureAdmissionStaging& capture);
  bool matchesFrozenCommand(uint32_t epoch) const { return ready() && live_ && commandEpoch_ == epoch; }
  /// DEC-172 correction 3: the ownership-supersession form of `ready()`. Everything still must be
  /// the same complete frozen canonical base (source root, capture storage and epoch owner), with
  /// ONE deliberate exception: the published `playbackRevision` may have moved since preparation --
  /// that movement is exactly the record-return supersession the lossless-exchange publish proof
  /// serves, and the interval's own companion derivation stays valid against the recorded base.
  bool supersessionReady(bool checkCommandEpoch = false) const {
    return phase_ == Phase::Complete && includeCapture_ && source_ &&
        source_->sourceGeneration == expected_.sourceGeneration &&
        source_->snapshotIdentity == expected_.snapshotIdentity &&
        (!preparedCaptureOrder_ || preparedCaptureOrder_->storageGeneration == preparedCaptureOrderGeneration_) &&
        (!current_ || (current_->sourceGeneration == expected_.sourceGeneration &&
                       current_->loopLengthTicks == loopLength_ &&
                       current_->storageGeneration == storageGeneration_)) &&
        (!live_ || (live_->sourceGeneration == expected_.sourceGeneration &&
                    (!checkCommandEpoch || live_->transportCommandEpoch == commandEpoch_) &&
                    live_->loopLengthTicks == loopLength_ &&
                    live_->capture.frozen &&
                    live_->capture.captureGeneration == captureGeneration_ &&
                    live_->capture.frozenGeneration == captureGeneration_ &&
                    live_->capture.storage.eventCount == captureCount_)) &&
        (!borrowed_ || (apply_ && apply_->generation() == applyGeneration_));
  }
  bool includesFrozenOverdub() const { return ready() && includeCapture_; }
  /// DEC-172 correction 3: the same two checks without the `ready()` gate, so the ownership-
  /// supersession proof can validate the frozen capture binding while the recorded revision moved.
  bool includesFrozenOverdubBinding() const { return stateAllowsBinding() && includeCapture_; }
  const CaptureAdmissionStaging* frozenCaptureBinding() const {
    return stateAllowsBinding() && live_ ? &live_->capture : nullptr;
  }
  RuntimeCompanionApply::HistoryPrefix historyPrefix() const;
  const RuntimeCompanionPasses::Pass* data() const;
  size_t passCount() const;
  size_t eventCount() const;
  /// Raw retained capture in its private canonical order, before history/companions.
  size_t frozenCaptureEventCount() const;
  const MidiEvent* frozenCaptureEvent(size_t index) const;
  uint32_t loopLength() const { return loopLength_; }
  uint64_t generation() const { return generation_; }
  void discard();
 private:
  enum class Phase : uint8_t { Idle, SelectRecord, SelectOverdub, FilterHistory, Start, Read, CaptureOrder, CopyCaptureOrder, CaptureCopy, Apply, Copy, Complete, Denied };
  MidiEvent* raw_ = nullptr;
  MidiEvent* effective_ = nullptr;
  RuntimeCompanionPasses::Pass* outputs_ = nullptr;
  RuntimeCompanionPasses::Pass* nextOutputs_ = nullptr;
  mutable const RuntimeCompanionPasses* normalizedOwner_ = nullptr;
  mutable uint64_t normalizedGeneration_ = 0;
  SealedPass* selectedPasses_ = nullptr;
  const EditPass** selectedHistory_ = nullptr;
  uint16_t* captureOrder_ = nullptr;
  uint16_t* captureScratch_ = nullptr;
  RuntimePlaybackOrderBuilder captureOrderBuild_;
  std::optional<PreparedCaptureOrder> preparedCaptureOrder_;
  uint64_t preparedCaptureOrderGeneration_ = 0;
  size_t captureOrderedCount_ = 0;
  bool includeCapture_ = false, captureStarted_ = false;
  const LoopPasses* loopPasses_ = nullptr;
  size_t selectionCursor_ = 0, bestOverdub_ = SIZE_MAX;
  uint32_t lastSequence_ = 0;
  bool haveSequence_ = false;
  size_t capacity_ = 0, passCount_ = 0, pass_ = 0, count_ = 0, copy_ = 0;
  const SealedPass* passes_ = nullptr;
  const EditPass* history_ = nullptr;
  size_t rowCount_ = 0;
  const PlaybackViewSourceIdentity* source_ = nullptr;
  PlaybackViewSourceIdentity expected_;
  std::optional<FrozenSource> live_;
  std::optional<CurrentSource> current_;
  uint64_t storageGeneration_ = 0;
  uint32_t commandEpoch_ = 0, captureGeneration_ = 0;
  size_t captureCount_ = 0;
  uint32_t loopLength_ = 0;
  uint64_t generation_ = 0, applyGeneration_ = 0;
  RuntimeCompanionApply* apply_ = nullptr;
  std::optional<RuntimeCompanionApply::Work> work_;
  bool borrowed_ = false;
  LoopEventStoreReadCursor read_;
  Phase phase_ = Phase::Idle;
  bool valid(bool checkCommandEpoch = true) const;
  bool beginApply(size_t count);
  void releaseApply();
  RuntimePlaybackOrderBuildResult deny();
};
