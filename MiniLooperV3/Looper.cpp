// Looper.cpp
#include "Globals.h"
#include "Looper.h"
#include "LooperState.h"

void setupLooper() {
  // Initialize stuff
}

void updateLooper() {
  handleLooperState();
}

void startRecording() {
  requestStateTransition(LOOPER_RECORDING, true);
}

void stopRecording() {
  requestStateTransition(LOOPER_PLAYING, true);
}

void startPlayback() {
  requestStateTransition(LOOPER_PLAYING, false);
}

void stopPlayback() {
  requestStateTransition(LOOPER_IDLE, false);
}

void startOverdub() {
  requestStateTransition(LOOPER_OVERDUBBING, false);
}

void stopOverdub() {
  requestStateTransition(LOOPER_PLAYING, false);
}
