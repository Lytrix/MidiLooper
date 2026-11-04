//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The 16-byte committed source row of the content engine (DEC-245 layer 1, DEC-252). A run keeps Midi rows and
// HideNote rows in separate regions; a HideNote row hides every Midi row carrying `noteId` in earlier passes (NOTE_EDIT).
// `seq` is the append ordinal inside the source pass and `origin` the source pass's ordinal inside a compacted run
// (0 in an ordinary run): both survive compaction unchanged, so provenance and DEC-244 order are kept.
// SysEx is not representable.

#pragma once

#include <cstdint>

namespace ContentEngine {

struct PassEvent {
  uint32_t tick = 0;
  uint32_t noteId = 0;
  uint32_t seq = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  uint8_t origin = 0;
};
static_assert(sizeof(PassEvent) == 16, "PassEvent is a fixed 16-byte row");

/// `origin` flag bits (bits 0..5 stay the compaction ordinal).
/// An engine-generated closure row (Stop / FinalInterval).
constexpr uint8_t kOriginGeneratedClosure = 0x80u;
/// A NoteOff whose note was held for at least one loop length (Q-LEN-1). Loop replay re-strikes such a note once per
/// loop, ending each iteration one tick before the next, and never sends this NoteOff against a newer iteration. The
/// row's tick, status and data stay as recorded.
constexpr uint8_t kOriginHeldLoop = 0x40u;

/// A HideNote row of an Edit run: `noteId` names the target; every row of that noteId in earlier runs is hidden. Hide rows
/// sit at the start of their run (tick 0); 0xF4 is an undefined MIDI status, never sent.
constexpr uint8_t kHideNoteStatus = 0xF4u;

/// Run kinds (the RunTable descriptor's `kind`).
namespace RunKind {
constexpr uint8_t kRecord = 0;
constexpr uint8_t kOverdub = 1;
constexpr uint8_t kCompacted = 2;
constexpr uint8_t kEdit = 3;  // NOTE_EDIT commit: HideNote rows + replacement notes (EDIT-1: not a derived occluder)
}  // namespace RunKind

}  // namespace ContentEngine
