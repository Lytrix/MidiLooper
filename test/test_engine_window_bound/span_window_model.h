//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine CE-B1: the span-window representation, as an executable host model.
//
// A playback window holds two kinds of entries:
//   * a START entry per admitted effective NoteOn (or non-note event) whose tick lies in the window. It carries the
//     note's effective end tick inline when that end is known by the time the window is composed (it falls in this
//     window), otherwise `kOpenEnd`;
//   * a TERMINAL entry per note admitted in an earlier window whose effective end falls in this window.
// NoteOffs are not stored as rows; the consumer keeps a fixed-capacity terminal set (capacity T) of sounding admitted
// notes and fires each one at its end tick. Shortening (generated NoteOffs) therefore costs nothing for notes that start
// and are shortened inside one window; only notes sounding across the window start need a TERMINAL entry, and those
// are at most T. Hence entries(W) <= startRows(W) + T.
//
// Output admission (CE-C1 policy, polyphony part): a NoteOn is admitted only if fewer than T admitted notes are
// sounding at its tick (after the NoteOffs due at that tick, DEC-244). A refused NoteOn is named and never sounds;
// nothing stored changes. The composer and the consumer apply the same rule, so the composed windows are exact.

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../test_engine_semantic_oracle/reference_model.h"

namespace SpanModel {

using Oracle::Effective;
using Oracle::Fate;

constexpr uint32_t kOpenEnd = 0xFFFFFFFFu;

struct Entry {
  enum Kind : uint8_t { Start, Terminal } kind;
  uint32_t tick;     // Start: NoteOn tick; Terminal: end tick
  uint32_t end;      // Start only: inline end tick or kOpenEnd
  uint32_t noteId;
  uint8_t run;
  uint8_t channel;
  uint8_t pitch;
};

struct Window {
  uint32_t start, end;  // [start, end) in timeline ticks
  std::vector<Entry> entries;  // host model only; the engine's window is a fixed array of the proven capacity
  uint32_t startEntries = 0, terminalEntries = 0, refused = 0;
};

struct OutRow {
  uint32_t tick;
  uint8_t cls;  // 0 NoteOff, 1 NoteOn (DEC-244)
  uint8_t run;
  uint32_t noteId;
  uint8_t channel;
  uint8_t pitch;
  bool operator==(const OutRow& o) const {
    return tick == o.tick && cls == o.cls && run == o.run && noteId == o.noteId && channel == o.channel &&
           pitch == o.pitch;
  }
};

/// Timeline notes over `cycles` loop cycles: effective, not hidden; wrap-held notes end in the next cycle.
struct TimelineNote {
  uint32_t on, off, noteId;
  uint8_t run, channel, pitch;
};

inline std::vector<TimelineNote> timeline(const std::vector<Effective>& eff, uint32_t loopLength, uint32_t cycles) {
  std::vector<TimelineNote> out;
  for (uint32_t c = 0; c < cycles; ++c)
    for (const auto& e : eff) {
      if (e.fate == Fate::Hidden) continue;
      const uint32_t on = c * loopLength + e.note.on;
      const uint32_t off = c * loopLength + e.note.off + (e.note.off < e.note.on ? loopLength : 0);
      out.push_back({on, off, e.note.id * 64u + c, e.note.run, e.note.channel, e.note.pitch});
    }
  std::sort(out.begin(), out.end(), [](const TimelineNote& a, const TimelineNote& b) {
    if (a.on != b.on) return a.on < b.on;
    if (a.run != b.run) return a.run < b.run;
    return a.noteId < b.noteId;
  });
  return out;
}

inline bool offLess(const OutRow& a, const OutRow& b) {
  if (a.tick != b.tick) return a.tick < b.tick;
  if (a.cls != b.cls) return a.cls < b.cls;
  if (a.run != b.run) return a.run < b.run;
  return a.noteId < b.noteId;
}

/// Reference: the expanded-row emission (every NoteOn and NoteOff as a row) with the same output admission, starting
/// at timeline tick `from` (notes started before `from` are not sounded; their NoteOffs are suppressed, DEC-148).
inline std::vector<OutRow> expandedStream(const std::vector<TimelineNote>& notes, uint32_t from, uint32_t to,
                                          uint32_t T) {
  std::vector<OutRow> out;
  std::vector<TimelineNote> sounding;
  size_t i = 0;
  while (i < notes.size() && notes[i].on < from) ++i;
  for (uint32_t t = from; t < to; ++t) {
    // DEC-244: NoteOffs at t first, in pass then id order.
    std::vector<OutRow> offs;
    for (auto it = sounding.begin(); it != sounding.end();) {
      if (it->off == t) {
        offs.push_back({t, 0, it->run, it->noteId, it->channel, it->pitch});
        it = sounding.erase(it);
      } else {
        ++it;
      }
    }
    std::sort(offs.begin(), offs.end(), offLess);
    out.insert(out.end(), offs.begin(), offs.end());
    for (; i < notes.size() && notes[i].on == t; ++i) {
      if (sounding.size() >= T) continue;  // refused by output admission
      out.push_back({t, 1, notes[i].run, notes[i].noteId, notes[i].channel, notes[i].pitch});
      sounding.push_back(notes[i]);
    }
  }
  return out;
}

/// The composer: windows of `windowTicks` over [0, to), sequential, carrying the admitted sounding set (<= T).
inline std::vector<Window> composeWindows(const std::vector<TimelineNote>& notes, uint32_t to, uint32_t windowTicks,
                                          uint32_t T) {
  std::vector<Window> windows;
  std::vector<TimelineNote> sounding;  // admitted, end not yet reached
  size_t i = 0;
  for (uint32_t ws = 0; ws < to; ws += windowTicks) {
    Window w;
    w.start = ws;
    w.end = std::min(to, ws + windowTicks);
    // TERMINAL entries: admitted earlier (sounding at the window start), ending in this window.
    for (const auto& s : sounding)
      if (s.off >= w.start && s.off < w.end) {
        w.entries.push_back({Entry::Terminal, s.off, 0, s.noteId, s.run, s.channel, s.pitch});
        ++w.terminalEntries;
      }
    for (uint32_t t = w.start; t < w.end; ++t) {
      sounding.erase(std::remove_if(sounding.begin(), sounding.end(), [t](const TimelineNote& s) { return s.off == t; }),
                     sounding.end());
      for (; i < notes.size() && notes[i].on == t; ++i) {
        if (sounding.size() >= T) {
          ++w.refused;
          continue;
        }
        const bool endsHere = notes[i].off < w.end;
        w.entries.push_back({Entry::Start, t, endsHere ? notes[i].off : kOpenEnd, notes[i].noteId, notes[i].run,
                             notes[i].channel, notes[i].pitch});
        ++w.startEntries;
        sounding.push_back(notes[i]);
      }
    }
    windows.push_back(std::move(w));
  }
  return windows;
}

/// The consumer: plays windows from timeline tick `from`, with a terminal set of capacity T. It never sees a NoteOff
/// row; it fires inline ends and TERMINAL entries for notes it actually sounded. TERMINAL entries for notes it never
/// sounded (cold start) are ignored (DEC-148).
inline std::vector<OutRow> consume(const std::vector<Window>& windows, uint32_t from, uint32_t T, bool& overflow) {
  struct Sounding {
    uint32_t noteId, end;
    uint8_t run, channel, pitch;
  };
  std::vector<OutRow> out;
  std::vector<Sounding> set;  // the fixed terminal set (capacity T)
  overflow = false;
  for (const auto& w : windows) {
    if (w.end <= from) continue;
    for (uint32_t t = std::max(w.start, from); t < w.end; ++t) {
      for (const auto& e : w.entries)
        if (e.kind == Entry::Terminal && e.tick == t)
          for (auto& s : set)
            if (s.noteId == e.noteId) s.end = t;  // the end becomes known
      std::vector<OutRow> offs;
      for (auto it = set.begin(); it != set.end();) {
        if (it->end == t) {
          offs.push_back({t, 0, it->run, it->noteId, it->channel, it->pitch});
          it = set.erase(it);
        } else {
          ++it;
        }
      }
      std::sort(offs.begin(), offs.end(), offLess);
      out.insert(out.end(), offs.begin(), offs.end());
      for (const auto& e : w.entries) {
        if (e.kind != Entry::Start || e.tick != t) continue;
        if (set.size() >= T) {
          overflow = true;  // cannot happen: the composer applied the same admission
          continue;
        }
        out.push_back({t, 1, e.run, e.noteId, e.channel, e.pitch});
        set.push_back({e.noteId, e.end, e.run, e.channel, e.pitch});
      }
    }
  }
  return out;
}

}  // namespace SpanModel
