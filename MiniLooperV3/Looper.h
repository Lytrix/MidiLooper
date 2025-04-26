// Looper.h
#ifndef LOOPER_H
#define LOOPER_H

#include <Arduino.h>
#include "LooperState.h"

// Functions to control the looper
void setupLooper();
void updateLooper();
void startRecording();
void stopRecording();
void startPlayback();
void stopPlayback();
void startOverdub();
void stopOverdub();

extern LooperState looperState; // so other files (like Display) can know

#endif
