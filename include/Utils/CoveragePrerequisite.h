//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"

/// **Touched-coverage prerequisites and owed obligations**
/// (`realtime-idle-owner-admission-bound`, design D3; tasks 4.1a/4.1b).
///
/// The overdub source view is prepared from the loop-relative **tiles the session has touched**. A tile is
/// the *spatial* unit of correctness and publication atomicity; the *execution* unit is a separate
/// resumable event/operation cursor inside it (`CoverageGrain`), so a dense tile or a long closure can
/// never enlarge a turn.
///
/// Three decisions semantically need prepared coverage, and none of them may derive inline or block the
/// MIDI path:
///
///   * `OccupancyAtPhase`      — note-on: is the pressed phase occupied?
///   * `HeldParticipation`     — while held: does the held pitch participate in a source note?
///   * `SpanFinalization`      — note-off / stop: compute the overlap delta for the complete span.
///
/// When coverage is unavailable the consequence becomes an **owed obligation**, not a transient result.
/// Obligations persist until discharged or the session ends, and they are **coalesced by
/// (note identity, tile, kind)** — never queued per tick, per hold sample or per event.
namespace CoveragePrerequisite {

/// Tile granularity. Chosen for publication atomicity and closure locality, **not** against the owner
/// work budget: a tile is not an execution bound (see `CoverageGrain`).
constexpr uint32_t kTileBars = 4;

/// Envelope: the supported loop is covered by at most this many tiles, one bit each in a single 64-bit
/// requirement/prepared word. Above it the caller takes the named whole-loop fallback rather than
/// inventing partial coverage. 64 tiles * 4 bars = 256 bars of loop.
constexpr uint16_t kMaxTiles = 64;

/// Bounded obligation set. Overflow is a named refusal, never a silent drop.
constexpr uint8_t kMaxObligations = 64;

inline uint32_t tileLengthTicks() {
  return kTileBars * Config::TICKS_PER_BAR;
}

/// Number of tiles covering a loop, and therefore the width of a coverage bitmap. Returns 0 for an empty
/// loop; the caller treats a zero as "no coverage model" (the pre-session case).
inline uint16_t tileCount(uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return 0;
  }
  const uint32_t length = tileLengthTicks();
  if (length == 0) {
    return 0;
  }
  const uint32_t count = (loopLengthTicks + length - 1u) / length;
  return count > kMaxTiles ? kMaxTiles : static_cast<uint16_t>(count);
}

inline bool tileCountFitsEnvelope(uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return true;
  }
  const uint32_t length = tileLengthTicks();
  return length != 0 && ((loopLengthTicks + length - 1u) / length) <= kMaxTiles;
}

/// Tile containing a loop-relative phase tick. Phases are taken modulo the loop, so a wrapped tick maps to
/// the tile that actually holds it.
inline uint16_t tileIndexForPhase(uint32_t phaseTick, uint32_t loopLengthTicks) {
  const uint16_t count = tileCount(loopLengthTicks);
  if (count == 0) {
    return 0;
  }
  const uint32_t length = tileLengthTicks();
  const uint32_t phase = phaseTick % loopLengthTicks;
  const uint32_t index = phase / length;
  return static_cast<uint16_t>(index < count ? index : count - 1u);
}

/// Every tile whose span the half-open interval `[startTick, startTick + lengthTicks)` touches, including
/// the wrap case. Sets bits in `mask` (bit n = tile n) and returns how many bits are set.
inline uint8_t markSpanTiles(uint32_t startTick, uint32_t lengthTicks, uint32_t loopLengthTicks,
                             uint64_t& mask) {
  const uint16_t count = tileCount(loopLengthTicks);
  if (count == 0 || loopLengthTicks == 0 || lengthTicks == 0) {
    return 0;
  }
  uint64_t touched = 0;
  const uint32_t lastTick = (startTick + lengthTicks - 1u) % loopLengthTicks;
  uint16_t index = tileIndexForPhase(startTick, loopLengthTicks);
  const uint16_t lastIndex = tileIndexForPhase(lastTick, loopLengthTicks);
  // Walk forward from the first touched tile to the last, wrapping once at the end.
  for (uint16_t steps = 0; steps < count; ++steps) {
    touched |= (uint64_t(1) << index);
    if (index == lastIndex) {
      break;
    }
    index = static_cast<uint16_t>((index + 1u) % count);
  }
  mask |= touched;
  uint8_t bits = 0;
  for (uint16_t i = 0; i < count; ++i) {
    if (touched & (uint64_t(1) << i)) {
      ++bits;
    }
  }
  return bits;
}

inline bool tileIsRequired(uint64_t requiredMask, uint16_t tile) {
  return tile < 64 && (requiredMask & (uint64_t(1) << tile)) != 0;
}

/// Which semantic decision a prerequisite belongs to.
enum class Kind : uint8_t {
  OccupancyAtPhase = 0,
  HeldParticipation = 1,
  SpanFinalization = 2,
};

/// Outcome of asking a prerequisite. `Owed` is a *state*, not an error: the consequence is queued and
/// will be discharged when the tile completes.
enum class Status : uint8_t { Ready, Owed };

/// Minimum stable identity that keeps obligations semantically distinct: the note, the tile, and the kind
/// of consequence. Deliberately **not** per tick, per hold sample or per event.
struct ObligationKey {
  uint32_t noteId = 0;
  uint16_t tile = 0;
  Kind kind = Kind::OccupancyAtPhase;

  bool operator==(const ObligationKey& other) const {
    return noteId == other.noteId && tile == other.tile && kind == other.kind;
  }
};

/// One outstanding consequence. `generation` qualifies it against the source generation it was owed for;
/// a generation change retains and re-owes it rather than discarding it.
struct Obligation {
  ObligationKey key{};
  uint32_t generation = 0;
  bool owed = false;
};

class ObligationSet {
 public:
  /// Insert, coalescing on the key. Returns false only on a full set (named refusal at the call site).
  bool owe(const ObligationKey& key, uint32_t generation) {
    for (uint8_t i = 0; i < count_; ++i) {
      Obligation& existing = items_[i];
      if (existing.owed && existing.key == key) {
        existing.generation = generation;  // re-owed for the current generation
        return true;
      }
    }
    if (count_ >= kMaxObligations) {
      return false;
    }
    items_[count_] = Obligation{key, generation, true};
    ++count_;
    return true;
  }

  /// Discharge exactly one consequence. Discharging an unknown key is a no-op, so a replayed completion
  /// cannot execute twice.
  bool discharge(const ObligationKey& key) {
    for (uint8_t i = 0; i < count_; ++i) {
      if (items_[i].owed && items_[i].key == key) {
        items_[i].owed = false;
        return true;
      }
    }
    return false;
  }

  /// A generation change retains the obligations and re-qualifies them.
  void requalify(uint32_t generation) {
    for (uint8_t i = 0; i < count_; ++i) {
      if (items_[i].owed) {
        items_[i].generation = generation;
      }
    }
  }

  /// Discharge every obligation whose tile is now prepared for the current generation.
  uint8_t dischargePreparedTiles(uint64_t preparedMask, uint32_t generation) {
    uint8_t discharged = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      Obligation& item = items_[i];
      if (!item.owed || item.generation != generation) {
        continue;
      }
      if (tileIsRequired(preparedMask, item.key.tile)) {
        item.owed = false;
        ++discharged;
      }
    }
    return discharged;
  }

  uint8_t owedCount() const {
    uint8_t owed = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      if (items_[i].owed) {
        ++owed;
      }
    }
    return owed;
  }

  uint8_t size() const { return count_; }
  const Obligation& at(uint8_t index) const { return items_[index]; }
  void clear() { count_ = 0; }

 private:
  Obligation items_[kMaxObligations] = {};
  uint8_t count_ = 0;
};

/// The note-on ordering rule, made explicit: the MIDI note is accepted first, the pressed tile is marked
/// required, and only then is occupancy answered. Ready requires *either* a prepared tile *or* an answer
/// from the playback ledger — a refused source-view fallback is **not** "not occupied".
inline Status occupancyAtPhase(bool tilePrepared, bool ledgerAnswered) {
  return (tilePrepared || ledgerAnswered) ? Status::Ready : Status::Owed;
}

/// Held participation consumes prepared source data only; there is no ledger fallback for participation.
inline Status heldParticipation(bool tilePrepared) {
  return tilePrepared ? Status::Ready : Status::Owed;
}

/// Finalization needs every tile the span touches prepared for the current generation.
inline Status spanFinalization(uint64_t preparedMask, uint64_t spanMask) {
  return (preparedMask & spanMask) == spanMask ? Status::Ready : Status::Owed;
}

inline uint8_t popcount64(uint64_t value) {
  uint8_t bits = 0;
  while (value != 0) {
    value &= (value - 1u);
    ++bits;
  }
  return bits;
}

/// **Conservative closure** (task 4.3).
///
/// A committed note that begins *before* a required tile and sustains into it must still be a candidate.
/// Lookup is therefore widened backward by the **maximum committed source-note span**. This is
/// conservative, not exact: a global maximum may over-admit, it may never under-admit.
///
/// If the maximum span is at least the loop length, closure **saturates to the whole loop** rather than
/// producing an ambiguous modular range: with a note that can reach anywhere, "pad by loop length" has no
/// meaning in modular arithmetic.
inline uint32_t closurePadTicks(uint32_t maxCommittedNoteSpanTicks, uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || maxCommittedNoteSpanTicks == 0) {
    return 0;
  }
  return maxCommittedNoteSpanTicks >= loopLengthTicks ? loopLengthTicks : maxCommittedNoteSpanTicks;
}

/// The interval lookup must read for a tile: the tile itself widened backward by the closure pad, taken
/// modulo the loop. A saturated closure returns the whole loop.
inline void closureWindow(uint32_t tileStartTick, uint32_t tileLengthTicks,
                          uint32_t maxCommittedNoteSpanTicks, uint32_t loopLengthTicks,
                          uint32_t& lookupStart, uint32_t& lookupLength) {
  if (loopLengthTicks == 0) {
    lookupStart = 0;
    lookupLength = 0;
    return;
  }
  const uint32_t pad = closurePadTicks(maxCommittedNoteSpanTicks, loopLengthTicks);
  if (pad >= loopLengthTicks) {
    lookupStart = 0;
    lookupLength = loopLengthTicks;
    return;
  }
  const uint32_t total = tileLengthTicks + pad;
  if (total >= loopLengthTicks) {
    lookupStart = 0;
    lookupLength = loopLengthTicks;
    return;
  }
  lookupStart = (tileStartTick + loopLengthTicks - pad) % loopLengthTicks;
  lookupLength = total;
}

/// The lookup window a **publication** tile must read: the tile widened by the closure pad on
/// **both** sides. The backward pad finds a note that starts before the tile and sustains into it; the
/// forward pad is what lets the tile pair a note whose On starts inside the tile and ends after it.
/// Without it, that note is omitted by pass-local pairing and published by no tile at all (measured: a
/// note from tick 0 to 5000 on a 6144-tick loop vanished from the source view). A saturated closure reads
/// the whole loop.
inline void closureWindowAround(uint32_t tileStartTick, uint32_t tileLengthTicks,
                                uint32_t maxCommittedNoteSpanTicks, uint32_t loopLengthTicks,
                                uint32_t& lookupStart, uint32_t& lookupLength) {
  if (loopLengthTicks == 0) {
    lookupStart = 0;
    lookupLength = 0;
    return;
  }
  const uint32_t pad = closurePadTicks(maxCommittedNoteSpanTicks, loopLengthTicks);
  if (pad == 0) {
    lookupStart = tileStartTick % loopLengthTicks;
    lookupLength = tileLengthTicks;
    return;
  }
  if (pad >= loopLengthTicks) {
    lookupStart = 0;
    lookupLength = loopLengthTicks;
    return;
  }
  const uint32_t total = tileLengthTicks + 2u * pad;
  if (total >= loopLengthTicks) {
    lookupStart = 0;
    lookupLength = loopLengthTicks;
    return;
  }
  lookupStart = (tileStartTick + loopLengthTicks - pad) % loopLengthTicks;
  lookupLength = total;
}

/// **Coverage state for one overdub session** (task 4.2).
///
/// Required coverage is "phases touched so far", established by three cheap transitions, none of which
/// derives anything:
///
///   * `markNoteOn`        — marks the **pressed-phase tile**;
///   * `markHeldAdvance`   — marks each **newly crossed tile** while a note is held;
///   * `markSpanSealed`    — marks every tile the completed span touches, including the wrap case.
///
/// Required is monotonic: it never shrinks during a session. A generation change keeps required coverage
/// (it describes what the session touched) and clears prepared, so every required tile is re-owed.
struct CoverageState {
  uint64_t required = 0;
  uint64_t prepared = 0;
  uint32_t generation = 0;
  uint32_t loopLengthTicks = 0;
  bool sessionActive = false;

  /// A new session starts with no coverage for its generation.
  void beginSession(uint32_t loopLen, uint32_t gen) {
    required = 0;
    prepared = 0;
    generation = gen;
    loopLengthTicks = loopLen;
    sessionActive = loopLen != 0;
  }

  void endSession() {
    required = 0;
    prepared = 0;
    loopLengthTicks = 0;
    sessionActive = false;
  }

  /// Returns the tiles newly required (0 when nothing changed, which is the "a wrap adds nothing" case).
  uint8_t markNoteOn(uint32_t pressedPhaseTick) {
    uint64_t marked = 0;
    (void)markSpanTiles(pressedPhaseTick, 1, loopLengthTicks, marked);
    const uint64_t newly = marked & ~required;
    required |= marked;
    return popcount64(newly);
  }

  /// Held advancement: the tile the playhead is in *now* becomes required. Called as the note is held,
  /// so a wrap that carries a held note into an untouched phase adds exactly that tile.
  uint8_t markHeldAdvance(uint32_t phaseTick) {
    return markNoteOn(phaseTick);
  }

  /// NoteOff / stop: the complete span is sealed, so every tile it touches becomes required.
  uint8_t markSpanSealed(uint32_t startTick, uint32_t lengthTicks) {
    uint64_t marked = 0;
    (void)markSpanTiles(startTick, lengthTicks, loopLengthTicks, marked);
    const uint64_t newly = marked & ~required;
    required |= marked;
    return popcount64(newly);
  }

  /// Published atomically per tile by the worker when its grain completes.
  void publishPrepared(uint16_t tile) {
    if (tile < kMaxTiles) {
      prepared |= (uint64_t(1) << tile);
    }
  }

  /// A source-generation change: required coverage is session state and survives; prepared coverage for
  /// the old generation is no longer authoritative, so every required tile becomes owed again.
  void changeGeneration(uint32_t gen) {
    generation = gen;
    prepared = 0;
  }

  bool tileRequired(uint16_t tile) const { return tileIsRequired(required, tile); }
  bool tilePrepared(uint16_t tile) const { return tileIsRequired(prepared, tile); }
  bool tileOwed(uint16_t tile) const { return tileRequired(tile) && !tilePrepared(tile); }

  /// Ready only when every tile the span touches is prepared — never on partial coverage.
  bool spanPrepared(uint32_t startTick, uint32_t lengthTicks) const {
    uint64_t span = 0;
    (void)markSpanTiles(startTick, lengthTicks, loopLengthTicks, span);
    return (prepared & span) == span;
  }

  /// Worker selection: the next owed tile at or after `from`, wrapping once. `kMaxTiles` when none.
  uint16_t nextOwedTile(uint16_t from) const {
    const uint16_t count = tileCount(loopLengthTicks);
    for (uint16_t step = 0; step < count; ++step) {
      const uint16_t tile = static_cast<uint16_t>((from + step) % count);
      if (tileOwed(tile)) {
        return tile;
      }
    }
    return kMaxTiles;
  }

  uint8_t owedCount() const { return popcount64(required & ~prepared); }
};

/// **Execution grain** (task 4.1b): the worker's per-turn bound is expressed in inspected events and
/// operations, not in tiles or microseconds. The tile decides *what* is published atomically; this
/// decides *how much* one turn may execute.
constexpr uint16_t kGrainEventsPerTurn = 64;
constexpr uint16_t kGrainOperationsPerTurn = 64;

struct GrainCursor {
  uint16_t tile = 0;
  uint16_t events = 0;
  uint16_t operations = 0;
  uint32_t chunk = 0;
  uint32_t chunkEvent = 0;
  uint32_t generation = 0;
  bool active = false;
};

/// True when the cursor has spent its declared grain and must yield, regardless of the tile's size.
inline bool grainExhausted(const GrainCursor& cursor) {
  return cursor.events >= kGrainEventsPerTurn || cursor.operations >= kGrainOperationsPerTurn;
}

}  // namespace CoveragePrerequisite
