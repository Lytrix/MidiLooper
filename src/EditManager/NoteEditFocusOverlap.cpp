//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditFocusInternal.h"

#include "Utils/NoteEditMem.h"

template <typename Alloc>
NOTE_EDIT_MEM void resolveOverlapNotesForPreCommit(std::vector<MidiEvent, Alloc>& sessionStoreEvents,
                                                   NoteEditFocus& focus, uint8_t channel,
                                                   uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return;
  }

  for (auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.state == OverlapNoteStoreState::Visible) {
      continue;
    }
    if (entry.state == OverlapNoteStoreState::Hidden) {
      eraseNotePairAtBaseline(sessionStoreEvents, channel, entry.baseline);
      if (entry.shortenedEndTick != 0) {
        eraseNoteEndpoint(sessionStoreEvents, channel, entry.baseline.pitch, entry.shortenedEndTick,
                          false);
      }
      continue;
    }
    if (entry.state == OverlapNoteStoreState::Shortened) {
      materializeShortenedOverlap(sessionStoreEvents, channel, entry, loopLength);
    }
  }
}

template void resolveOverlapNotesForPreCommit<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, NoteEditFocus&, uint8_t, uint32_t);
template void resolveOverlapNotesForPreCommit<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, NoteEditFocus&, uint8_t, uint32_t);
