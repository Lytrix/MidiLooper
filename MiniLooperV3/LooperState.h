// LooperState.h
#ifndef LOOPERSTATE_H
#define LOOPERSTATE_H

#include <Arduino.h>

enum LooperState {
  LOOPER_IDLE,
  LOOPER_RECORDING,
  LOOPER_PLAYING,
  LOOPER_OVERDUBBING
};

// Functions
void handleLooperState();
void requestStateTransition(LooperState newState, bool quantize = false); //quantize to bar

extern LooperState looperState;

#endif
