//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef NOTE_EDIT_FADER_SELECT_SYNC_H
#define NOTE_EDIT_FADER_SELECT_SYNC_H

#include <cstdint>
#include <cstdlib>

namespace NoteEditFaderSelectSync {

inline bool shouldIgnoreSelectFaderEcho(int16_t incomingPitchbend, int16_t lastSentPitchbend,
                                        int16_t tolerance) {
    const int16_t diff = abs(incomingPitchbend - lastSentPitchbend);
    return diff <= tolerance;
}

}  // namespace NoteEditFaderSelectSync

#endif
