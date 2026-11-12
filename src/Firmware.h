//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine firmware's long-lived parts, created once in `main.cpp` and handed to the modules that use them:
// `Firmware` (the engine and what it plays through) and `Diagnostics` (the main loop's own counters, read by
// `!ENGINE take` and the clock-gap report).

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <cstdint>

// The engine root first: the surface's headers rely on the declarations it brings in.
#include "ContentEngine/Timing/EngineClock.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/ControlSurface/ControlSurface.h"
#include "Teensy/OledScreen.h"
#include "Teensy/SdCardIo.h"

/// Off the hot path, in flash (RAM1 room): a module's member functions run at boot, on a command or a library step.
/// Its own section, as `CE_COLD`: an inline member cannot share `FLASHMEM`'s with the core's non-inline functions;
/// `noclone`, because a local clone of an inline member cannot share it with the others either.
#define FIRMWARE_COLD __attribute__((section(".flashmem.firmware"), noclone))

namespace firmware {

using namespace ContentEngine;

/// The main loop's stages, as breadcrumb 1 and as the index of their worst time (`Diagnostics::stageMaxUs`).
enum Stage : uint32_t { kInput = 1, kSerial, kTicks, kJobs, kGrains, kSend, kSurface, kLeds, kScreen, kIdle, kRebooting, kUsbWedged };

struct Firmware {
  ContentEngine::EngineRoot engine;
  ContentEngine::EngineClock clock;
  Teensy::SdCardIo card;
  Teensy::OledScreen display;
  ContentEngine::ControlSurface<ContentEngine::EngineRoot> surface;
  uint32_t lastTick = 0;  // the last tick played: input arriving now belongs after it
};

/// Counters the main loop keeps about itself. Diagnostics only: nothing decides on them.
struct Diagnostics {
  uint32_t passes = 0, passMaxUs = 0;  // main-loop passes, and the longest since `!ENGINE take`
  uint32_t stageMaxUs[kUsbWedged + 1] = {};  // the worst time each stage took (the time between two crumbs)
  uint32_t stageAtUs = 0, stage = 0;
  uint32_t classGrainMaxUs[5] = {};  // the worst grain of each job class (hard, transition, display, persistence,
                                     // background), reset by `!ENGINE take`
  uint32_t pulses = 0, timeouts = 0;  // MIDI clock pulses, and external clock timeouts
  bool wasExternal = false;           // the clock followed an external source on the last pass
  // Clock-gap diagnostics: counters since the last pulse, snapshotted when two pulses are > 200 ms apart.
  uint32_t usbIn = 0, usbOut = 0, usbOutPassMax = 0, outThisPass = 0, lastPulseUs = 0;
  uint32_t passesAtPulse = 0, usbInAtPulse = 0, usbOutAtPulse = 0;
  struct Gap {
    uint32_t atMs, gapUs, passes, usbIn, usbOut, outBurst;
  };
  Gap gaps[8];
  uint8_t gapCount = 0;
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
