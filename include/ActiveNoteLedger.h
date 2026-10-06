#pragma once

#include "MidiEvent.h"

#include "Utils/IntervalProjection.h"

#include <array>
#include <cstdint>

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
#include "Utils/DebugSessionCapture.h"
#include <cstdio>
#endif

/// Open-NoteOn ledger: zero or more applied NoteOns whose Off has not yet been applied.
/// Logical identities, not "audible on the MIDI wire." Sparse table; not one slot per pitch.
class ActiveNoteLedger {
 public:
  static constexpr size_t kMaxOpenNotes = 128;

  struct Entry {
    uint8_t channel = 0;
    uint8_t note = 0;
    NoteId noteId = kInvalidNoteId;
    uint32_t startTick = 0;
    uint8_t velocity = 0;
  };

  void clear() {
    for (size_t i = 0; i < count_; ++i) {
      entries_[i] = Entry{};
    }
    count_ = 0;
    overflowed_ = false;
  }

  bool overflowed() const { return overflowed_; }

  /// Push an open NoteOn. Same identity already open → no-op (catch-up then clock
  /// must not stack a second Entry). Refuses when full — never evicts.
  void noteOn(uint8_t channel, uint8_t note, NoteId noteId, uint32_t tick, uint8_t velocity) {
    if (channel == 0 || channel > 16 || note > 127) {
      return;
    }
    if (noteId != kInvalidNoteId && findIndexByNoteId(channel, note, noteId) >= 0) {
      return;
    }
    if (count_ >= kMaxOpenNotes) {
      overflowed_ = true;
      logOpenNoteOverflow(channel, note, noteId);
      return;
    }
    Entry& e = entries_[count_++];
    e.channel = channel;
    e.note = note;
    e.noteId = noteId;
    e.startTick = tick;
    e.velocity = velocity;
  }

  bool isActive(uint8_t channel, uint8_t note) const {
    return findNewestIndex(channel, note) >= 0;
  }

  /// Newest open identity on the lane. Compatibility accessor only — not occupy.
  /// Authoritative set: forEachActive.
  NoteId noteId(uint8_t channel, uint8_t note) const {
    const int i = findNewestIndex(channel, note);
    return i >= 0 ? entries_[static_cast<size_t>(i)].noteId : kInvalidNoteId;
  }

  /// The handoff's **closure plan**: the open, identity-tagged Entries the incoming full-loop stream cannot discharge
  /// before the pass it is being served in ends — either its committed NoteOn is absent (the replacement removed the
  /// obligation's content) **or** its committed NoteOff lies at or behind `servedThroughPhase` (its own tick has already
  /// been traversed, so `advancePlaybackCursor` skips it for the rest of the pass). A value, bounded by `kMaxOpenNotes`
  /// and allocation-free — and stated **once**, so the owner that *plans*, the owner that *closes* on the MIDI wire
  /// (the Track's handoff reconcile, which also supplies the phase) and the owner that *drops* cannot disagree about
  /// which obligations they are. Each Entry carries the ledger's own `(channel, pitch, noteId)`, so a closure emitted
  /// from this plan closes exactly the sounding that Entry names.
  struct HandoffClosurePlan {
    struct Entry {
      uint8_t channel = 0;
      uint8_t note = 0;
      NoteId noteId = kInvalidNoteId;
    };
    std::array<Entry, kMaxOpenNotes> entries{};
    size_t count = 0;
  };

  /// Plan the handoff's closures. `servedThroughPhase` is the phase the walk has already served on this loop (its own
  /// term: `reanchorPlaybackIndex`'s threshold, i.e. `loop.lastTickInLoop`), and `UINT32_MAX` means **nothing has been
  /// served** — the pass is starting or restarting, so every closure is still reachable and only the structural half
  /// applies. Untagged Entries are never planned: their absence is not evidence. A stream that carries a tagged NoteOn
  /// and **no** NoteOff for it is never planned from the closure half either: the absence of a closure is not evidence
  /// that the note ends. Firmware: ActiveNoteLedger.cpp (FLASHMEM).
#if defined(PIO_UNIT_TEST_NATIVE)
  HandoffClosurePlan planHandoffClosures(const MidiEvent* events, size_t eventCount,
                                         uint32_t servedThroughPhase, uint32_t loopLength) const {
    return planHandoffClosuresBody(events, eventCount, servedThroughPhase, loopLength);
  }
#else
  HandoffClosurePlan planHandoffClosures(const MidiEvent* events, size_t eventCount,
                                         uint32_t servedThroughPhase, uint32_t loopLength) const;
#endif

  /// The **structural half** by name, for callers that owe no wire half and therefore have no traversal position to
  /// reason about: the same predicate with nothing served yet. Its plan is a *subset* of the serving handoff's — see
  /// `planHandoffClosures`. Firmware: ActiveNoteLedger.cpp (FLASHMEM).
#if defined(PIO_UNIT_TEST_NATIVE)
  HandoffClosurePlan planOpenNotesMissingFromCommittedNoteOns(const MidiEvent* events,
                                                              size_t eventCount) const {
    return planOpenNotesMissingFromCommittedNoteOnsBody(events, eventCount);
  }
#else
  HandoffClosurePlan planOpenNotesMissingFromCommittedNoteOns(const MidiEvent* events,
                                                              size_t eventCount) const;
#endif

  /// Drop exactly the planned obligations (by their own identity). Untagged Entries stay. Reuse eraseAt. Returns the
  /// number actually dropped, which is what makes a plan-taken-twice or an already-gone entry observable rather than
  /// silent. Firmware: ActiveNoteLedger.cpp (FLASHMEM).
#if defined(PIO_UNIT_TEST_NATIVE)
  size_t erasePlannedOpenNotes(const HandoffClosurePlan& plan) { return erasePlannedOpenNotesBody(plan); }
#else
  size_t erasePlannedOpenNotes(const HandoffClosurePlan& plan);
#endif

  /// The plan and its drop as one call, for callers that owe **no** wire half (the ledger alone, and the paths whose
  /// slot is not being served). The serving path uses the two halves separately so it can close each obligation before
  /// dropping it — see the Track's handoff reconcile. Firmware: ActiveNoteLedger.cpp (FLASHMEM).
#if defined(PIO_UNIT_TEST_NATIVE)
  void eraseOpenNotesMissingFromCommittedNoteOns(const MidiEvent* events, size_t eventCount) {
    (void)erasePlannedOpenNotesBody(planOpenNotesMissingFromCommittedNoteOnsBody(events, eventCount));
  }
#else
  void eraseOpenNotesMissingFromCommittedNoteOns(const MidiEvent* events, size_t eventCount);
#endif

  /// Untagged NoteOff resolution: LIFO pop the most recent open On on this lane.
  void noteOff(uint8_t channel, uint8_t note) {
    const int i = findNewestIndex(channel, note);
    if (i >= 0) {
      eraseAt(static_cast<size_t>(i));
    }
  }

  /// Apply a playback NoteOn/NoteOff. Returns false if the event must not be emitted.
  /// NoteOff resolution:
  /// - known identity + found → remove that exact Entry
  /// - no identity → LIFO pop on the lane
  /// - known identity + not found → orphan, no mutation
  /// Firmware: defined in ActiveNoteLedger.cpp (FLASHMEM). Native: inline below.
#if defined(PIO_UNIT_TEST_NATIVE)
  bool applyPlaybackEvent(uint8_t channel, const MidiEvent& evt) {
    return applyPlaybackEventBody(channel, evt);
  }
#else
  bool applyPlaybackEvent(uint8_t channel, const MidiEvent& evt);
#endif

  template <typename Fn>
  void forEachActive(Fn&& fn) const {
    for (size_t i = 0; i < count_; ++i) {
      const Entry& e = entries_[i];
      fn(e.channel, e.note, e);
    }
  }

  uint8_t longestActiveSpanBars(uint32_t playheadTick, uint32_t loopLengthTicks,
                                uint32_t ticksPerBar) const {
    if (ticksPerBar == 0 || loopLengthTicks == 0) {
      return 0;
    }

    uint32_t longestTicks = 0;
    for (size_t i = 0; i < count_; ++i) {
      const Entry& e = entries_[i];
      const uint32_t span = (playheadTick >= e.startTick)
                                ? (playheadTick - e.startTick)
                                : (loopLengthTicks - e.startTick + playheadTick);
      if (span > longestTicks) {
        longestTicks = span;
      }
    }
    // +1 bar to keep upcoming note-offs inside the active window.
    const uint32_t bars = (longestTicks / ticksPerBar) + 1;
    return static_cast<uint8_t>(bars > 255 ? 255 : bars);
  }

 private:
  std::array<Entry, kMaxOpenNotes> entries_{};
  size_t count_ = 0;
  bool overflowed_ = false;

  int findNewestIndex(uint8_t channel, uint8_t note) const {
    if (channel == 0 || channel > 16 || note > 127) {
      return -1;
    }
    for (size_t i = count_; i > 0; --i) {
      const Entry& e = entries_[i - 1];
      if (e.channel == channel && e.note == note) {
        return static_cast<int>(i - 1);
      }
    }
    return -1;
  }

  int findIndexByNoteId(uint8_t channel, uint8_t note, NoteId noteId) const {
    if (channel == 0 || channel > 16 || note > 127 || noteId == kInvalidNoteId) {
      return -1;
    }
    for (size_t i = count_; i > 0; --i) {
      const Entry& e = entries_[i - 1];
      if (e.channel == channel && e.note == note && e.noteId == noteId) {
        return static_cast<int>(i - 1);
      }
    }
    return -1;
  }

  void eraseAt(size_t index) {
    if (index >= count_) {
      return;
    }
    for (size_t j = index + 1; j < count_; ++j) {
      entries_[j - 1] = entries_[j];
    }
    --count_;
    entries_[count_] = Entry{};
  }

  bool applyPlaybackEventBody(uint8_t channel, const MidiEvent& evt) {
    if (evt.isNoteOff()) {
      const uint8_t note = evt.data.noteData.note;
      if (evt.noteId != kInvalidNoteId) {
        const int i = findIndexByNoteId(channel, note, evt.noteId);
        if (i < 0) {
          return false;
        }
        eraseAt(static_cast<size_t>(i));
        return true;
      }
      const int i = findNewestIndex(channel, note);
      if (i < 0) {
        return false;
      }
      eraseAt(static_cast<size_t>(i));
      return true;
    }
    if (evt.isNoteOn()) {
      noteOn(channel, evt.data.noteData.note, evt.noteId, evt.tick, evt.data.noteData.velocity);
      return true;
    }
    return true;
  }

  /// The one scan that answers both halves of the handoff's predicate: for each tagged open Entry it looks for its
  /// identity in the incoming stream and records whether a NoteOn is there at all and whether any of its NoteOffs is
  /// still reachable (ahead of `servedThroughPhase`, or any of them when nothing has been served yet). O(open notes ×
  /// stream events), which is the nested shape the structural prune already ran on this path, and it replaces it.
  HandoffClosurePlan planHandoffClosuresBody(const MidiEvent* events, size_t eventCount,
                                             uint32_t servedThroughPhase, uint32_t loopLength) const {
    HandoffClosurePlan plan;
    if (eventCount > 0 && events == nullptr) {
      return plan;
    }
    for (size_t i = 0; i < count_; ++i) {
      const Entry& entry = entries_[i];
      if (entry.noteId == kInvalidNoteId) {
        continue;
      }
      bool noteOnPresent = false;
      bool closurePresent = false;
      bool closureReachable = false;
      for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
        // Both halves settled and neither can flip: the stream carries the obligation *and* a reachable closure for it, so
        // nothing later can make this Entry planned. Without this exit the common shape (identity present, its note-off
        // ahead) would cost the whole open-notes x stream product instead of the shipped scan's early exit.
        if (noteOnPresent && closureReachable) {
          break;
        }
        const MidiEvent& evt = events[eventIndex];
        if (evt.noteId != entry.noteId) {
          continue;
        }
        if (evt.isNoteOn()) {
          noteOnPresent = true;
          continue;
        }
        if (!evt.isNoteOff()) {
          continue;
        }
        closurePresent = true;
        if (servedThroughPhase == UINT32_MAX ||
            IntervalProjection::playbackEventPhase(evt.tick, loopLength) > servedThroughPhase) {
          closureReachable = true;
        }
      }
      // The stream cannot discharge this obligation: the content that carried it is gone, or the note-off that would
      // close it has already been traversed and cannot be reached again before the pass restarts (and a stream that
      // carries no note-off at all says nothing -- a note that never ends is not a note that had to end).
      const bool undeliverable = !noteOnPresent || (closurePresent && !closureReachable);
      if (!undeliverable || plan.count >= kMaxOpenNotes) {
        continue;
      }
      plan.entries[plan.count].channel = entry.channel;
      plan.entries[plan.count].note = entry.note;
      plan.entries[plan.count].noteId = entry.noteId;
      ++plan.count;
    }
    return plan;
  }

  HandoffClosurePlan planOpenNotesMissingFromCommittedNoteOnsBody(const MidiEvent* events,
                                                                  size_t eventCount) const {
    // Nothing has been served: the closure half can never fire, so this is exactly the structural plan it always was.
    return planHandoffClosuresBody(events, eventCount, UINT32_MAX, /*loopLength=*/0);
  }

  size_t erasePlannedOpenNotesBody(const HandoffClosurePlan& plan) {
    size_t erased = 0;
    for (size_t i = 0; i < plan.count; ++i) {
      const HandoffClosurePlan::Entry& planned = plan.entries[i];
      const int index = findIndexByNoteId(planned.channel, planned.note, planned.noteId);
      if (index < 0) {
        // The obligation is already gone (a plan taken after its own discharge): nothing to drop, and nothing to log.
        continue;
      }
      logOpenNoteErase(planned.noteId);
      eraseAt(static_cast<size_t>(index));
      ++erased;
    }
    return erased;
  }
  static void logOpenNoteOverflow(uint8_t channel, uint8_t note, NoteId noteId) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    char line[160];
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,ledger,overflow,ch=%u,pitch=%u,id=%lu",
             static_cast<unsigned long>(micros()), static_cast<unsigned>(channel),
             static_cast<unsigned>(note), static_cast<unsigned long>(noteId));
    DebugSessionCapture::appendCaptureTextLine(line);
#else
    (void)channel;
    (void)note;
    (void)noteId;
#endif
  }

  static void logOpenNoteErase(NoteId noteId) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    char line[160];
    snprintf(line, sizeof(line), "#CAP,%lu,DIAG,ledger,erase,id=%lu",
             static_cast<unsigned long>(micros()), static_cast<unsigned long>(noteId));
    DebugSessionCapture::appendCaptureTextLine(line);
#else
    (void)noteId;
#endif
  }
};
