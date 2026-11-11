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

/// A muted note (owner 2026-11-08): bit 7 of its NoteOn row's `data2`, beside the 7-bit velocity. The note is present —
/// paired, composed, drawn and editable — but `WindowPlayer` never starts it, so nothing of it is sent.
constexpr uint8_t kVelocityMuted = 0x80u;
inline bool isMuted(uint8_t data2) { return (data2 & kVelocityMuted) != 0; }
inline uint8_t velocityOf(uint8_t data2) { return static_cast<uint8_t>(data2 & 0x7Fu); }

/// Run kinds (the RunTable descriptor's `kind`).
namespace RunKind {
constexpr uint8_t kRecord = 0;
constexpr uint8_t kOverdub = 1;
constexpr uint8_t kCompacted = 2;
constexpr uint8_t kEdit = 3;  // NOTE_EDIT commit: HideNote rows + replacement notes (EDIT-1: not a derived occluder)
constexpr uint8_t kFit = 4;   // a resized state's lookups (DEC-257, `FitRun.h`): no MIDI rows, never content
constexpr uint8_t kGroove = 5;  // the groove pass (DEC-287): the effective content of the runs before it, grooved; those
                                // runs are kept and not played
}  // namespace RunKind

/// Fit run rows (DEC-257), sorted by (tick = the content RunId, status, noteId): the run reaches the loop end (filter;
/// noteId 0), a note of it crosses the loop end (cross; `seq` its NoteOff's tick), a note's held-loop status differs
/// from its flag at the loop length (held). Undefined MIDI statuses, never sent.
constexpr uint8_t kFitFilterStatus = 0xF5u;
constexpr uint8_t kFitCrossStatus = 0xF6u;
constexpr uint8_t kFitHeldStatus = 0xF7u;
/// The loop's play window in its stored notes (DEC-283), the fit run's last row: `seq` the loop's start, `noteId` the
/// frame's length (ticks) — the stored loop the start counts in; `tick` 0xFFFFFFFF so it sorts after every lookup. Absent:
/// start 0, the frame the loop.
constexpr uint8_t kFitWindowStatus = 0xF8u;
/// The loop's two remembered ranges (DEC-284 amendment 3: A the older, B the newer range set with the step / bar
/// buttons), after the window row, same layout (`seq` the start, `noteId` the length). Absent: no such range.
constexpr uint8_t kFitRangeAStatus = 0xF9u;
constexpr uint8_t kFitRangeBStatus = 0xFAu;
/// The groove setting (DEC-287): `seq` the packed `groove::Settings` (template, amount, scope, AUTO). Absent: none.
constexpr uint8_t kFitGrooveStatus = 0xFBu;
/// A groove file's template (DEC-290), after the groove row: five steps a row (signed 16-bit offsets in `noteId`, `seq`
/// and `data1` / `data2`), `origin` the row's index; the groove row's `noteId` its shape (steps | step << 16). Then
/// two label rows (ten characters each: `noteId`, `seq`, `data1`, `data2`; `origin` the index).
constexpr uint8_t kFitTemplateStatus = 0xFCu;
constexpr uint8_t kFitLabelStatus = 0xFDu;
/// The GROOVE window's two columns (owner 2026-11-11: "save B with the slot"), after the groove rows: `seq` A's packed
/// settings, `noteId` B's, `data1` the column heard (1 OFF, 2 A, 3 B). Absent: B a copy of A on opening (DEC-295).
constexpr uint8_t kFitColumnsStatus = 0xFEu;

}  // namespace ContentEngine
