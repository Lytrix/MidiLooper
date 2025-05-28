#ifndef LOOPER_H
#define LOOPER_H

#include <Arduino.h>
#include "LooperState.h"

/**
 * @class Looper
 * @brief Top-level controller coordinating the MIDI looper subsystems.
 *
 * The Looper class orchestrates the global clock, track management, display updates,
 * and edit overlays by delegating to ClockManager, TrackManager, DisplayManager,
 * EditManager, and LooperStateManager. It provides simple methods (startRecording,
 * startPlayback, startOverdub, etc.) that queue state transitions (with optional
 * quantization) via the internal requestStateTransition().
 *
 * The setup() method initializes all subsystems, and update() should be called
 * regularly in the main loop to advance the looper state machine and propagate
 * clock ticks and user inputs to the appropriate modules.
 */
class Looper {
public:
  Looper();

  void setup();
  void update();

  void startRecording();
  void stopRecording();
  void startPlayback();
  void stopPlayback();
  void startOverdub();
  void stopOverdub();

  LooperState getState() const;

private:
  void handleState();
  void requestStateTransition(LooperState targetState, bool quantize);

  LooperState state;
};

extern Looper looper;  // Global instance

#endif // LOOPER_H
