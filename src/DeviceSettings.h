//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Settings kept on the device itself (EEPROM) and wall-clock time.
// * EEPROM bytes 0–3 'T', 'Z', the time-zone offset in minutes; 4–5 'W', the set switch point (0 next bar, 1 loop
//   end); 6–7 'L', the launch quantisation as divisions of a bar (16, 8, 4 or 1).
// * Wall-clock time (real dates only): the Teensy RTC, valid only per `wallclock::valid` (never the firmware's build
//   time). The Teensy loader sets the RTC to the computer's local time at every upload, so the RTC is read as local
//   time and the shown offset defaults to 0. `!ENGINE time` reports it, `!ENGINE time <unix>` sets the clock,
//   `!ENGINE tz <minutes>` the offset (`scripts/engine_set_time.py` sets local time and offset 0, as the loader does).

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <EEPROM.h>

#include "ContentEngine/Timing/WallClock.h"
#include "Firmware.h"

namespace firmware {

using namespace ContentEngine;

struct DeviceSettings {
  static constexpr uint8_t kLaunchDivisions[4] = {16, 8, 4, 1};

  explicit DeviceSettings(ContentEngine::EngineRoot& e) : engine(e) {}

  ContentEngine::EngineRoot& engine;
  int16_t tzOffset = 0;        // minutes east of UTC
  uint32_t lastSaveWall = 0;   // the wall time of the store's last completed slot write (0: none this boot, or no clock)

  // ---- Time zone ----
  FIRMWARE_COLD void loadTzOffset() {
    if (EEPROM.read(0) != 'T' || EEPROM.read(1) != 'Z') return;
    tzOffset = ContentEngine::wallclock::clampOffset(static_cast<int16_t>(EEPROM.read(2) | (EEPROM.read(3) << 8)));
  }
  FIRMWARE_COLD void storeTzOffset(int16_t minutes) {
    tzOffset = ContentEngine::wallclock::clampOffset(minutes);
    EEPROM.update(0, 'T');
    EEPROM.update(1, 'Z');
    EEPROM.update(2, static_cast<uint8_t>(tzOffset & 0xFF));
    EEPROM.update(3, static_cast<uint8_t>((tzOffset >> 8) & 0xFF));
  }

  // ---- The global set switch point ----
  FIRMWARE_COLD void loadSwitchAt() {
    if (EEPROM.read(4) != 'W') return;
    engine.setSwitchAt(EEPROM.read(5) == 1 ? ContentEngine::ProductionControl::SwitchAt::LoopEnd
                                           : ContentEngine::ProductionControl::SwitchAt::NextBar);
  }
  FIRMWARE_COLD void storeSwitchAt(ContentEngine::ProductionControl::SwitchAt at) {
    engine.setSwitchAt(at);
    EEPROM.update(4, 'W');
    EEPROM.update(5, at == ContentEngine::ProductionControl::SwitchAt::LoopEnd ? 1 : 0);
  }
  const char* switchAtName() const {
    return engine.switchAt() == ContentEngine::ProductionControl::SwitchAt::LoopEnd ? "loop" : "bar";
  }

  // ---- The global launch quantisation (a slot's double press launches at the next step) ----
  static bool launchDivisionValid(uint32_t d) {
    for (uint8_t v : kLaunchDivisions)
      if (v == d) return true;
    return false;
  }
  FIRMWARE_COLD void loadLaunchGrid() {
    if (EEPROM.read(6) != 'L' || !launchDivisionValid(EEPROM.read(7))) return;
    applyLaunchDivision(EEPROM.read(7));
  }
  FIRMWARE_COLD void storeLaunchGrid(uint8_t d) {
    applyLaunchDivision(d);
    EEPROM.update(6, 'L');
    EEPROM.update(7, d);
  }
  const char* launchGridName() {
    switch (ContentEngine::kTicksPerBar / engine.control().launchGrid()) {
      case 16: return "16";
      case 8: return "8";
      case 4: return "4";
      default: return "bar";
    }
  }

  // ---- Wall-clock time ----
  /// The RTC's time, or 0 when it is not valid (`wallclock::valid`).
  static uint32_t wallTime() {
    const uint32_t t = static_cast<uint32_t>(rtc_get());
    return ContentEngine::wallclock::valid(t) ? t : 0;
  }
  FIRMWARE_COLD void reportTime() {
    namespace wallclock = ContentEngine::wallclock;
    const uint32_t raw = static_cast<uint32_t>(rtc_get());
    char stamp[24], saved[8];
    wallclock::formatStamp(raw, tzOffset, stamp, sizeof stamp);
    wallclock::formatClock(lastSaveWall, tzOffset, saved, sizeof saved);
    Serial.printf("#ENGINE,time,unix=%lu,valid=%u,offset_min=%d,local=%s,last_save=%lu,saved=%s\n",
                  static_cast<unsigned long>(raw), wallclock::valid(raw) ? 1u : 0u, tzOffset, stamp,
                  static_cast<unsigned long>(lastSaveWall), saved);
  }
  /// A completed store write (a slot copy) is stamped with the wall time: `SAVED 14:32`.
  void stampSaves() {
    const uint32_t saves = engine.persistence().stats().saves;
    if (saves == savesSeen_) return;
    savesSeen_ = saves;
    lastSaveWall = wallTime();
  }

 private:
  void applyLaunchDivision(uint8_t d) { engine.control().setLaunchGrid(ContentEngine::kTicksPerBar / d); }
  uint32_t savesSeen_ = 0;  // `Persistence::stats().saves` when last looked at
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
