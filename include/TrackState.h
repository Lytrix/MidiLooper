//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file TrackState.h
 * @brief TrackState enum - extracted so Loop.h can use it without circular Track.h dependency.
 */
#ifndef TRACK_STATE_H
#define TRACK_STATE_H

#pragma once

// Track states with clear transitions
enum TrackState {
  TRACK_EMPTY,              // Initial state Empty track
  TRACK_STOPPED,            // No recording or playback
  TRACK_ARMED,              // Ready to start recording
  TRACK_RECORDING,          // Recording first layer
  TRACK_STOPPED_RECORDING,  // First layer recorded, ready for playback or overdub
  TRACK_PLAYING,            // Playing back recorded content
  TRACK_OVERDUBBING,        // Recording additional layers while playing
  NUM_TRACK_STATES          // Always helpful to validate range
};

#endif  // TRACK_STATE_H
