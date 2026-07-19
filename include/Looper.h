//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef LOOPER_H
#define LOOPER_H

#include <Arduino.h>

/**
 * @class Looper
 * @brief Boot/load shell for SD workspace restore.
 *
 * Transport and mode control live on TrackManager / MidiButtonActions /
 * LooperStateManager — not on this class. setup() loads workspace state;
 * update() is reserved for future frame hooks (currently empty).
 */
class Looper {
public:
  Looper();

  void setup();
  void update();
};

extern Looper looper;  // Global instance

#endif // LOOPER_H
