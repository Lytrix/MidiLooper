//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E0: independent reference semantic model. Written from the contract register
// (openspec/changes/fixed-memory-engine/evidence/E0-contract-register.md), NOT from legacy code: no firmware headers
// are included. Host-only; std::vector is fine here (this is the oracle, not the engine).
//
// Note geometry follows the ratified storage convention (OVL-0): NoteOn at `on`, NoteOff at `off`; a note is present
// at hold h iff on <= h < off (wrap-held notes, off < on, are present iff h >= on || h < off).

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Oracle {

struct Note {
  uint32_t id;
  uint8_t run;      // pass order: 0 = Record, 1.. = later overdub runs
  uint8_t channel;  // ignored by overlap (OVL-5, DEC-033)
  uint8_t pitch;
  uint32_t on;
  uint32_t off;     // storage NoteOff tick
};

enum class Fate : uint8_t { Kept, Shortened, Hidden };

struct Effective {
  Note note;  // `off` is the effective NoteOff tick when shortened
  Fate fate;
};

constexpr uint32_t kMinLengthTicks = 8;  // OVL-6 (DEFAULT_NOTE_MIN_LENGTH_TICKS, remove enabled)

inline bool presentAt(const Note& a, uint32_t h) {
  if (a.off >= a.on) return a.on <= h && h < a.off;
  return h >= a.on || h < a.off;  // wrap-held
}

// OVL-3: A starts strictly inside B's closed span (B.on, B.off); wrap-crossing B is two linear segments.
inline bool startsStrictlyInside(const Note& a, const Note& b) {
  if (b.off >= b.on) return b.on < a.on && a.on < b.off;
  return a.on > b.on || a.on < b.off;
}

/// OVL-1..7: effective content of a set of runs. Each later note B acts on every earlier-run note A of the same pitch;
/// outcomes compose monotonically (Hide absorbs; Shorten takes the minimum end, OVL-7); a note's own fate does not cancel
/// the effect it had on earlier runs (OVL-8).
inline std::vector<Effective> effective(const std::vector<Note>& notes, uint32_t loopLength) {
  std::vector<Effective> out;
  for (const Note& a : notes) {
    Effective e{a, Fate::Kept};
    for (const Note& b : notes) {
      if (b.run <= a.run || b.pitch != a.pitch) continue;
      const uint32_t h = b.on;
      if (presentAt(a, h)) {
        // Q-OVL-3 (owner ruling 2026-11-04): the ratified rule stands: a hold in a wrap-held note's head shortens it
        // only for h > 0; a hold at 0 hides it. Keeping the pre-wrap portion is optional amendment AMD-1 (not adopted).
        const bool wrapHeldHead = a.off < a.on && h < a.off && h > 0 && h != a.on;
        if (a.on < h || wrapHeldHead) {  // OVL-1 shorten to h - 1
          const uint32_t end = h == 0 ? loopLength - 1 : h - 1;
          if (e.fate != Fate::Hidden && (e.fate == Fate::Kept || end < e.note.off)) {
            e.fate = Fate::Shortened;
            e.note.off = end;
          }
        } else {  // OVL-2 same start
          e.fate = Fate::Hidden;
        }
      } else if (startsStrictlyInside(a, b)) {
        e.fate = Fate::Hidden;  // OVL-3
      }
    }
    if (e.fate == Fate::Shortened && e.note.off >= e.note.on && e.note.off - e.note.on < kMinLengthTicks)
      e.fate = Fate::Hidden;  // OVL-6
    out.push_back(e);
  }
  return out;
}

struct Row {
  uint32_t tick;
  uint8_t cls;  // DEC-244: 0 NoteOff, 1 NoteOn
  uint8_t run;
  uint32_t noteId;
  bool generated;  // a shortened note's NoteOff
};

/// Rows of effective content falling in [a, b) (linear loop; wrap-held notes are not used by window fixtures), in
/// DEC-244 order: tick, NoteOff < NoteOn, pass order, note id.
inline std::vector<Row> windowRows(const std::vector<Effective>& eff, uint32_t a, uint32_t b) {
  std::vector<Row> rows;
  for (const Effective& e : eff) {
    if (e.fate == Fate::Hidden) continue;
    if (e.note.on >= a && e.note.on < b) rows.push_back({e.note.on, 1, e.note.run, e.note.id, false});
    if (e.note.off >= a && e.note.off < b)
      rows.push_back({e.note.off, 0, e.note.run, e.note.id, e.fate == Fate::Shortened});
  }
  std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
    if (x.tick != y.tick) return x.tick < y.tick;
    if (x.cls != y.cls) return x.cls < y.cls;
    if (x.run != y.run) return x.run < y.run;
    return x.noteId < y.noteId;
  });
  return rows;
}

inline uint32_t sourceRowsIn(const std::vector<Note>& notes, uint32_t a, uint32_t b) {
  uint32_t n = 0;
  for (const Note& x : notes) {
    n += (x.on >= a && x.on < b) ? 1u : 0u;
    n += (x.off >= a && x.off < b) ? 1u : 0u;
  }
  return n;
}

}  // namespace Oracle
