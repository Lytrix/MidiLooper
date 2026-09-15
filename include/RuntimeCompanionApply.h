// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "RuntimeCompanionReconstruction.h"
#include "RuntimeCompanionOverlapWalk.h"
#include "EditPass.h"

/// Fixed unpublished candidate for canonical Delete/Length companions or a frozen batch.
/// Callers keep source identity/content immutable and exclusively lend helpers.
/// Output is valid until the next successful begin/discard; it is not a published view.
/// Completed output cannot alias the source of another begin on this instance.
class RuntimeCompanionApply {
 public:
  enum class Kind : uint8_t { Delete, Length, Pitch, Velocity, NoteRange };
  struct Work {
    RuntimeNotePairLookup& pair;
    RuntimeNoteDeleteLookup& deletion;
    RuntimeCompanionSpanWalk& spans;
    RuntimeCompanionReconstruction& reconstruction;
    RuntimeCompanionOverlapWalk& overlaps;
  };
  RuntimeCompanionApply();
  ~RuntimeCompanionApply();
  RuntimeCompanionApply(const RuntimeCompanionApply&) = delete;
  RuntimeCompanionApply& operator=(const RuntimeCompanionApply&) = delete;
  bool prewarm(size_t eventCapacity);
  bool available() const { return phase_ == Phase::Idle || phase_ == Phase::Complete || phase_ == Phase::Denied; }
  static size_t requiredGrainsUpperBound(size_t eventCapacity, size_t quantum);
  static uint64_t requiredHistoryGrainsUpperBound(size_t eventCapacity, size_t rowCount, size_t quantum);
  struct HistoryPrefix {
    const EditPass* rows = nullptr;
    const EditPass* const* selected = nullptr;
    size_t count = 0;
  };
  static uint64_t requiredContinuationGrainsUpperBound(size_t capacity, size_t historyCount,
                                                       size_t rowCount, size_t quantum);
  /// Events already reflect this immutable history. Scan it only for sequence
  /// membership; do not apply its event mutations a second time.
  bool beginContinuationBatch(const Work& work, const MidiEvent* events, size_t count,
                  const PlaybackViewSourceIdentity& source, const EditPass* rows,
                  size_t rowCount, uint32_t loopLength, HistoryPrefix history);
  bool begin(const Work& work, const MidiEvent* events, size_t count,
             const PlaybackViewSourceIdentity& source, Kind kind, NoteId target,
             uint32_t newEnd, uint32_t loopLength);
  /// canonicalSequence preserves the existing sequence guard: after Delete,
  /// later rows for that identity are skipped. Add ceil(R/Q)+2 to the per-row
  /// grain bound for the bounded prior-Delete scan. No supported row creates Ons.
  bool beginBatch(const Work& work, const MidiEvent* events, size_t count,
                  const PlaybackViewSourceIdentity& source, const EditPass* rows,
                  size_t rowCount, uint32_t loopLength, bool canonicalSequence = false);
  /// Canonical active Note history: Create/Delete and Length/Pitch/Velocity/
  /// NoteRange updates. Caller filters inactive/non-Note rows beforehand.
  /// Consecutive Pitch/Velocity updates collapse exactly as in the reference.
  /// Create appends then stably orders raw ticks (DEC-061), even when empty.
  /// Its added NoteOn identities reactivate rows after earlier Delete actions.
  /// Unsupported rows deny the entire unpublished result.
  bool beginHistoryBatch(const Work& work, const MidiEvent* events, size_t count,
                        const PlaybackViewSourceIdentity& source, const EditPass* rows,
                        size_t rowCount, uint32_t loopLength);
  /// Borrow a filtered row-pointer list without copying Create payload vectors.
  bool beginSelectedHistoryBatch(const Work& work, const MidiEvent* events, size_t count,
                        const PlaybackViewSourceIdentity& source, const EditPass* const* rows,
                        size_t rowCount, uint32_t loopLength);
  RuntimePlaybackOrderBuildResult step(size_t inputQuantum);
  bool ready(const PlaybackViewSourceIdentity& source) const;
  const MidiEvent* data() const;
  const uint32_t* provenance() const; // UINT32_MAX = created event or synthesized Off.
  size_t size() const;
  void discard();
  uint64_t generation() const { return generation_; }
  bool isHistoryBatch() const { return history_; }
  bool matchesInput(const MidiEvent* events, size_t count,
                    const PlaybackViewSourceIdentity& source) const;

 private:
  enum class Phase : uint8_t { Idle, Copy, NextRow, CollapseRows, PriorDeletes, CreateAppend, InferLength, Find, Pair, Write, HistoryWrite, Reconstruct, Overlaps,
                              DeleteLookup, DeleteRows, Compact, Order, ClearVisit,
                              Permute, Complete, Denied };
  enum class Find : uint8_t { Initial, Action, Final };
  uint64_t generation_ = 0;
  FixedPlaybackEventStorage candidate_;
  uint32_t* provenance_ = nullptr;
  RuntimePlaybackOrderBuilder order_;
  RuntimeNotePairLookup* pair_ = nullptr;
  RuntimeNoteDeleteLookup* deletion_ = nullptr;
  RuntimeCompanionSpanWalk* spans_ = nullptr;
  RuntimeCompanionReconstruction* reconstruction_ = nullptr;
  RuntimeCompanionOverlapWalk* overlaps_ = nullptr;
  const MidiEvent* input_ = nullptr;
  size_t inputCount_ = 0, cursor_ = 0, write_ = 0;
  const PlaybackViewSourceIdentity* source_ = nullptr;
  PlaybackViewSourceIdentity expected_;
  bool batch_ = false, canonicalSequence_ = false;
  bool history_ = false;
  bool identityDeleted_ = false;
  HistoryPrefix prefix_;
  size_t historyPayloadCursor_ = 0;
  uint32_t newStart_ = 0;
  uint8_t newValue_ = 0;
  const EditPass* rows_ = nullptr;
  const EditPass* const* selectedRows_ = nullptr;
  const EditPass* rowAt(size_t index) const { return selectedRows_ ? selectedRows_[index] : rows_ + index; }
  size_t rowCount_ = 0, rowCursor_ = 0;
  uint32_t loopHint_ = 0;
  Kind kind_ = Kind::Length;
  NoteId target_ = kInvalidNoteId, findId_ = kInvalidNoteId;
  uint32_t newEnd_ = 0, writeEnd_ = 0, loopLength_ = 0, maxTick_ = 0, start_ = 0;
  uint8_t pitch_ = 0, deleteCursor_ = 0;
  int on_ = -1, off_ = -1;
  int deleteRows_[3] = {-1, -1, -1};
  bool cycleActive_ = false;
  size_t cycleStart_ = 0, cycleDestination_ = 0;
  MidiEvent cycleEvent_;
  uint32_t cycleProvenance_ = 0;
  Phase phase_ = Phase::Idle;
  Find find_ = Find::Initial;
  bool sourceMatches() const;
  void resetWork();
  void releaseWork();
  RuntimePlaybackOrderBuildResult deny();
  void startFind(Find purpose, NoteId id);
  bool startDelete(NoteId id);
  void resumeOverlaps();
  void complete();
  bool startBatchRow();
  bool selectBatchRow();
};
