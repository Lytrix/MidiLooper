//  Copyright (c)  2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "RuntimePlaybackOrderBuilder.h"

/// Resumable equivalent of the companion resolver's LIFO NoteOff lookup.
/// The owner holds source events/retention immutable until completion. Lane
/// filtering, run discovery, Off/On walks, and wrap fallback all spend quantum.
class RuntimeNotePairLookup {
 public:
  RuntimeNotePairLookup();
  RuntimeNotePairLookup(const RuntimeNotePairLookup&) = delete;
  RuntimeNotePairLookup& operator=(const RuntimeNotePairLookup&) = delete;
  ~RuntimeNotePairLookup();
  bool prewarm(size_t capacity);
  bool available() const { return lane_ != nullptr && (phase_ == Phase::Idle || phase_ == Phase::Complete); }
  bool begin(const MidiEvent* events, size_t count, size_t targetOn,
             const uint8_t* retained = nullptr);
  bool beginWrapHead(const MidiEvent* events, size_t count, size_t targetOn,
                     uint32_t loopLength, const uint8_t* retained = nullptr);
  /// Reconstruction-specific wrap lookup (not the Delete wrap-head rule).
  /// targetOn == SIZE_MAX selects the preferred tail automatically; otherwise
  /// it checks that On using the Off's channel, as pitch-local reconstruction does.
  bool beginReconstructionWrap(const MidiEvent* events, size_t count, size_t offIndex,
                               uint32_t loopLength, size_t targetOn = SIZE_MAX,
                               const uint8_t* retained = nullptr);
  uint32_t reconstructedHeadTick() const { return reconstructedHeadTick_; }
  size_t lastStepInputCount() const { return lastStepInputCount_; }
  RuntimePlaybackOrderBuildResult step(size_t inputQuantum);
  int result() const { return result_; }
  void discard();

 private:
  enum class Phase : uint8_t { Idle, Collect, RunStart, RunEnd, Offs, Ons, Wrap, WrapTail, WrapHead, ReconstructionOns, ReconstructionOffs, Complete };
  uint16_t* lane_ = nullptr;
  uint16_t* stack_ = nullptr;
  uint8_t* paired_ = nullptr;
  size_t capacity_ = 0;
  const MidiEvent* events_ = nullptr;
  const uint8_t* retained_ = nullptr;
  size_t count_ = 0;
  size_t target_ = 0;
  size_t cursor_ = 0;
  size_t laneCount_ = 0;
  size_t stackCount_ = 0;
  size_t runStart_ = 0;
  size_t runEnd_ = 0;
  uint32_t runTick_ = 0;
  uint32_t loopLength_ = 0;
  uint32_t latestTailTick_ = 0;
  size_t lastStepInputCount_ = 0;
  size_t reconstructionTarget_ = SIZE_MAX;
  size_t reconstructionBestOn_ = SIZE_MAX;
  uint32_t reconstructedHeadTick_ = 0;
  uint32_t occupiedHeadTick_ = 0;
  bool reconstructionMidOn_ = false;
  bool reconstructionLinearCloser_ = false;
  size_t untaggedCount_ = 0;
  int untagged_ = -1;
  int result_ = -1;
  Phase phase_ = Phase::Idle;
};

// This header declares two owners: `RuntimeNotePairLookup` above and `RuntimeNoteDeleteLookup`
// below. They live in one header/TU because they share the pair-lookup contract and are always
// used together; the file name reflects the primary owner only. Splitting them into their own TU
// was considered and declined: a new TU defaults into ITCM unless it matches the linker flash
// allow-list, so the move would need its own RAM1 measurement for no ownership gain.
/// Copy-on-write Delete preparation: original source indexes only. No erase,
/// canonical publication, or bridge publication occurs here. The shared pair
/// lookup is exclusively borrowed until completion/discard.
class RuntimeNoteDeleteLookup {
 public:
  RuntimeNoteDeleteLookup() = default;
  RuntimeNoteDeleteLookup(const RuntimeNoteDeleteLookup&) = delete;
  RuntimeNoteDeleteLookup& operator=(const RuntimeNoteDeleteLookup&) = delete;
  bool begin(RuntimeNotePairLookup& pairLookup, const MidiEvent* events, size_t count,
             NoteId noteId, uint32_t loopLength, const uint8_t* retained = nullptr);
  RuntimePlaybackOrderBuildResult step(size_t inputQuantum);
  void discard();
  bool ready() const { return phase_ == Phase::Complete; }
  bool available() const { return phase_ == Phase::Idle || phase_ == Phase::Complete; }
  int onIndex() const { return on_; }
  int offIndex() const { return off_; }
  int wrapOffIndex() const { return wrapOff_; }

 private:
  enum class Phase : uint8_t { Idle, FindOn, Pair, Wrap, Complete };
  RuntimeNotePairLookup* pair_ = nullptr;
  const MidiEvent* events_ = nullptr;
  const uint8_t* retained_ = nullptr;
  size_t count_ = 0;
  size_t cursor_ = 0;
  NoteId noteId_ = kInvalidNoteId;
  uint32_t loopLength_ = 0;
  uint32_t maxTick_ = 0;
  int on_ = -1;
  int off_ = -1;
  int wrapOff_ = -1;
  Phase phase_ = Phase::Idle;
};
