//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The GPIO encoder: quadrature on pins 29 / 30 (the Encoder library's interrupts, four counts a detent) and the push on
// 31 to ground (pull-up), debounced — a level held `kDebounceMs` is taken. Turns and pushes go to the surface.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <Encoder.h>

#include "Firmware.h"

namespace firmware {

using namespace ContentEngine;

struct GpioEncoder {
  static constexpr uint8_t kPinA = 29, kPinB = 30, kButtonPin = 31;
  static constexpr uint32_t kDebounceMs = 5;

  explicit GpioEncoder(ContentEngine::ControlSurface<ContentEngine::EngineRoot>& s) : surface(s) {}

  ContentEngine::ControlSurface<ContentEngine::EngineRoot>& surface;

  void begin() { pinMode(kButtonPin, INPUT_PULLUP); }

  void service(uint32_t ms) {
    const long detents = encoder_.read() / 4;
    if (detents != detents_) {
      surface.encoderTurn(static_cast<int32_t>(detents - detents_), ms);
      detents_ = detents;
    }
    const bool raw = digitalRead(kButtonPin) == LOW;
    if (raw != raw_) {
      raw_ = raw;
      rawMs_ = ms;
    } else if (raw != down_ && ms - rawMs_ >= kDebounceMs) {
      down_ = raw;
      surface.encoderButton(raw, ms);
    }
  }

 private:
  Encoder encoder_{kPinA, kPinB};
  long detents_ = 0;
  bool down_ = false, raw_ = false;
  uint32_t rawMs_ = 0;
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
