#pragma once

#include "MidiEvent.h"

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
};
