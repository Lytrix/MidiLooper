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

}  // namespace ContentEngine
