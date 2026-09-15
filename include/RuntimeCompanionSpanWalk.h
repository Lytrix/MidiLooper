// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "RuntimeNotePairLookup.h"
#include "Utils/IntervalProjection.h"

enum class RuntimeCompanionSpanResult : uint8_t { Pending, SpanReady, Complete, Denied };

/// Pitch-local equivalent of the ordinary reconstruction canonical-span walk.
/// Source/retention stay immutable; each completed span is held until consumed.
/// The shared wrap lookup is exclusively borrowed until completion/discard.
class RuntimeCompanionSpanWalk {
 public:
  RuntimeCompanionSpanWalk();
  ~RuntimeCompanionSpanWalk();
  RuntimeCompanionSpanWalk(const RuntimeCompanionSpanWalk&) = delete;
  RuntimeCompanionSpanWalk& operator=(const RuntimeCompanionSpanWalk&) = delete;
  bool available() const { return phase_ == Phase::Idle || phase_ == Phase::Complete; }
  bool prewarm(size_t capacity);
  bool begin(RuntimeNotePairLookup& wrapLookup, const MidiEvent* events, size_t count,
             uint8_t pitch, uint32_t loopLength, const uint8_t* retained = nullptr);
  RuntimeCompanionSpanResult step(size_t inputQuantum);
  const CanonicalNoteSpan& span() const { return span_; }
  bool spanReady() const { return spanReady_; }
  bool consumeSpan();
  size_t lastStepInputCount() const { return visited_; }
  void discard();

 private:
  enum class Phase : uint8_t { Idle, Initialize, Events, FindPair, SelectPair, Query,
                              DeferEnd, MarkWrapped, EqualTick, Leftovers, Finish, Complete };
  enum class Query : uint8_t { Empty, Stack, Preferred, Pair };
  enum Flag : uint8_t { Active = 1, Wrapped = 2, Consumed = 4 };
  uint8_t* flags_ = nullptr;
  size_t capacity_ = 0;
  RuntimeNotePairLookup* wrap_ = nullptr;
  const MidiEvent* events_ = nullptr;
  const uint8_t* retained_ = nullptr;
  size_t count_ = 0, cursor_ = 0, scan_ = 0, visited_ = 0, activeCount_ = 0;
  size_t off_ = 0, pair_ = SIZE_MAX, back_ = SIZE_MAX;
  uint32_t loopLength_ = 0, tailStart_ = 0, head_ = 0, wrappedTick_ = 0;
  uint8_t pitch_ = 0, offZeroTotal_ = 0, offZeroPaired_ = 0, remainingZero_ = 0;
  bool beyond_ = false, preferred_ = false, removeWrappedActive_ = false, spanReady_ = false;
  CanonicalNoteSpan span_;
  Phase phase_ = Phase::Idle;
  Query query_ = Query::Empty;
  bool retained(size_t index) const;
  bool startQuery(Query query, size_t target = SIZE_MAX);
  void emit(size_t on, uint32_t endExclusive, bool open, bool split, Phase next);
  void emitPair(bool wrapped);
  void finishAutoQuery();
};
