#ifndef LOOPER_H
#define LOOPER_H

#include <Arduino.h>
#include "LooperState.h"

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

  LooperState _state;
};

extern Looper looper;  // Global instance

#endif // LOOPER_H