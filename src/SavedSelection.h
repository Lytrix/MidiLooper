//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The surface's selection across a reboot: the selected track, each track's selected slot, the muted tracks and the
// GROOVE cycle's bars, saved with the engine's surface state. At boot the saved selection is applied as soon as the
// boot load's first grain has read it, before the performance screen draws its first frame, so the screen opens on it
// with no switch to see; without one, each track's first slot with content.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>

#include "Firmware.h"

namespace firmware {

using namespace ContentEngine;

struct SavedSelection {
  SavedSelection(ContentEngine::EngineRoot& e, ContentEngine::ControlSurface<ContentEngine::EngineRoot>& s)
      : engine(e), surface(s) {}

  ContentEngine::EngineRoot& engine;
  ContentEngine::ControlSurface<ContentEngine::EngineRoot>& surface;
  bool known = false;  // the selection is applied (saved or first slots): the screen may open

  /// The saved selection, once read; false: none yet (or none saved).
  bool applySaved() {
    using namespace ContentEngine;
    uint8_t state[ProductionPersistence::kStateBytes];
    if (!engine.persistence().restoredSurfaceState(state) || state[0] >= kTrackCount) return false;
    engine.control().selectTrack(state[0]);
    for (uint8_t t = 0; t < kTrackCount; ++t)
      if (state[1 + t] / 8 == t) surface.selectSlot(t, static_cast<uint8_t>(state[1 + t] % 8));
    for (uint8_t t = 0; t < kTrackCount; ++t)  // the muted tracks
      engine.control().setMute(t, (state[ProductionPersistence::kStateBytes - 1] >> t & 1u) != 0, micros());
    if (const uint8_t bars = engine.persistence().restoredCycleBars()) surface.setCycleBars(bars);
    return true;
  }
  /// After the boot load: the saved selection if it was not applied yet, else each track's first slot with content.
  void restore() {
    if (!known && !applySaved()) selectFirstSlots();
    known = true;
  }
  /// The selection as the surface has it now, for saving (selected track, then each track's engine slot).
  void save(uint32_t nowUs) {
    using namespace ContentEngine;
    uint8_t state[ProductionPersistence::kStateBytes];
    state[0] = engine.control().selectedTrack();
    for (uint8_t t = 0; t < kTrackCount; ++t) state[1 + t] = surface.selectedSlot(t);
    uint8_t mutes = 0;
    for (uint8_t t = 0; t < kTrackCount; ++t)
      if (engine.control().muted(t)) mutes |= static_cast<uint8_t>(1u << t);
    state[ProductionPersistence::kStateBytes - 1] = mutes;
    engine.setSurfaceState(state, nowUs);
    engine.setEngineSettings(surface.cycleBars(), nowUs);  // the GROOVE cycle's bars
  }

 private:
  /// Each track's first slot with content (slots T x 8 .. T x 8 + 7).
  void selectFirstSlots() {
    using namespace ContentEngine;
    for (uint8_t t = 0; t < kTrackCount; ++t)
      for (uint8_t s = 0; s < 8; ++s)
        if (engine.transport().contentOf(static_cast<uint8_t>(t * 8 + s)) != kNoManifest) {
          surface.selectSlot(t, s);
          break;
        }
  }
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
