//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

/// Gate 2 — mute/solo/slot-mute are a last-layer port gate.
/// Enabled slots keep running the playback engine. Port emit is separate.
namespace PlaybackPortEmit {

inline bool engineShouldRun(bool slotEnabled) {
  return slotEnabled;
}

inline bool portShouldEmit(bool trackAudible, bool slotMuted) {
  return trackAudible && !slotMuted;
}

}  // namespace PlaybackPortEmit
