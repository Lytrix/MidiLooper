//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Track.h"

// -------------------------
// Track class implementation
// -------------------------
Track::Track() :
  ignorePlaybackMidiInput(false),
  muted(false),
  midiChannel(1),
  activeLoopIndex(0),
  trackState(TRACK_EMPTY),
  jamStartTick(UINT32_MAX),
  jamLength(0),
  jamTick(0),
  jamPlaybackActive(false),
  alignLoopOriginOnNextStop(false),
  recordAddedNoteOnCount(0) {
}

#if defined(__IMXRT1062__)
FLASHMEM
#endif
Track::~Track() {
  // Body runs before LoopPool member destruction: no borrowed live callback may
  // survive physical source destruction. Cancellation never queries that source.
  playbackRuntime.cancelLiveCaptureBuilds();
}
