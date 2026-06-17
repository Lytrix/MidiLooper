#pragma once

#include <array>
#include <cstdint>

/// Lightweight active-note tracking for playback window sizing.
class ActiveNoteLedger {
 public:
  struct Entry {
    bool active = false;
    uint32_t startTick = 0;
    uint8_t velocity = 0;
  };

  void clear() {
    for (Entry& e : entries_) {
      e = Entry{};
    }
  }

  void noteOn(uint8_t channel, uint8_t note, uint32_t tick, uint8_t velocity) {
    if (channel == 0 || channel > 16 || note > 127) {
      return;
    }
    Entry& e = entries_[indexFor(channel, note)];
    e.active = true;
    e.startTick = tick;
    e.velocity = velocity;
  }

  void noteOff(uint8_t channel, uint8_t note) {
    if (channel == 0 || channel > 16 || note > 127) {
      return;
    }
    entries_[indexFor(channel, note)] = Entry{};
  }

  uint8_t longestActiveSpanBars(uint32_t playheadTick, uint32_t loopLengthTicks,
                                uint32_t ticksPerBar) const {
    if (ticksPerBar == 0 || loopLengthTicks == 0) {
      return 0;
    }

    uint32_t longestTicks = 0;
    for (const Entry& e : entries_) {
      if (!e.active) {
        continue;
      }
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
  static constexpr size_t kLedgerSize = 16u * 128u;
  std::array<Entry, kLedgerSize> entries_{};

  static size_t indexFor(uint8_t channel, uint8_t note) {
    return static_cast<size_t>(channel - 1) * 128u + note;
  }
};
