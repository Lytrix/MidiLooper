//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

/// Mute/solo/slot-mute suppress MIDI send on the track channel and output ports.
/// Enabled slots keep running the playback engine.
namespace PlaybackMidiOutput {

inline bool engineShouldRun(bool slotEnabled) {
  return slotEnabled;
}

inline bool shouldSend(bool trackSendsMidi, bool slotMuted) {
  return trackSendsMidi && !slotMuted;
}

}  // namespace PlaybackMidiOutput
