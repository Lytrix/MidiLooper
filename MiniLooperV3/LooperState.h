#ifndef LOOPERSTATE_H
#define LOOPERSTATE_H

#include <Arduino.h>

enum LooperState {
  LOOPER_IDLE,
  LOOPER_RECORDING,
  LOOPER_PLAYING,
  LOOPER_OVERDUBBING
};

// --- State Control API ---
void handleLooperState();  // Call  // Queues a transition (optionally quantized)

extern LooperState looperState;

#endif // LOOPERSTATE_H