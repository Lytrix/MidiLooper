//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// What the screen is told each pass: the slot each track's piano roll shows, and the `EditView` — the surface's
// windows (copy, length popup, GROOVE), LOOP_EDIT and NOTE_EDIT's state — gathered from the surface and the engine.
// The screen (`Teensy::OledScreen`) draws a frame every 33 ms from it when the previous DMA transfer is done.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>

#include "Firmware.h"

namespace firmware {

using namespace ContentEngine;

struct ScreenView {
  explicit ScreenView(Firmware& f) : fw(f) {}

  Firmware& fw;

  /// Each track's shown slot to the screen, then this pass's edit view.
  Teensy::EditView prepare() {
    using namespace ContentEngine;
    auto& surface = fw.surface;
    for (uint8_t t = 0; t < kTrackCount; ++t)
      fw.display.show(t, surface.shown(t), surface.shownQueued(t), surface.shownQueuedPosition(t));
    Teensy::EditView ev;
    const auto& ed = surface.edit();
    ev.copy = static_cast<uint8_t>(surface.copyMode());  // copy mode's window
    ev.copySlot = surface.copySourceSlot();
    ev.copyBar = surface.copySourceBar();
    ev.copyPaste = static_cast<uint8_t>(surface.pasteMode());
    {  // the progress bar's extent — the stored loop, the current one bracketed in it
      const uint8_t t = fw.engine.control().selectedTrack();
      uint32_t scale = 0, start = 0, known = 0;
      if (surface.loopFrame(surface.shown(t), scale, start, known)) {
        ev.barScale = scale;
        ev.barStart = start;
        ev.barKnown = known;
      }
    }
    ev.copyLeft = static_cast<uint16_t>(surface.copyMsLeft(millis()) * 1000 / ControlSurface<EngineRoot>::kCopyIdleMs);
    ev.loop = surface.loopEdit();
    ev.loopTicks = surface.pendingLoopTicks();
    ev.loopStart = surface.loopStart();
    ev.popup = surface.popup();
    ev.popupSlot = surface.popupSlot();
    ev.popupLoop = surface.popupLoop();
    ev.popupChoice = ev.popup ? surface.popupChoice() : 0xFF;
    ev.popupRanges = ev.popup ? surface.popupRanges() : 0;
    ev.groove = surface.grooveOpen();
    if (ev.groove) {
      const auto& g = surface.grooveSettings();
      ev.grooveSlot = surface.grooveWindowSlot();
      ev.grooveField = surface.grooveField();
      ev.grooveTemplate = g.tmpl;
      ev.grooveAmount = g.amount;
      ev.grooveAuto = surface.grooveAuto();  // AUTO Q, the window's
      ev.grooveState = static_cast<uint8_t>(fw.engine.grooveState());
      ev.grooveProgress = surface.grooveProgress();
      ev.grooveATemplate = surface.grooveA().tmpl;
      ev.grooveAAmount = surface.grooveA().amount;
      ev.grooveHeard = static_cast<uint8_t>(surface.grooveHeard());
      ev.grooveCycling = surface.cycling();
      ev.grooveDots = surface.grooveDots();
      ev.grooveCycleBars = surface.cycleBars();
      ev.grooveTurnPos = surface.cycleTurnPos();
    }
    if (ed.active()) {
      ev.active = true;
      ev.ready = ed.editing();
      ev.slot = ed.slot();
      ev.first = ed.windowFirst();
      ev.ticks = ed.windowTicks();
      ev.bracket = ed.bracket();
      ev.length = ed.lengthMode();
      ev.steps = ed.steps();
      ev.velocity = ed.velocityMode();
      ev.encoder = static_cast<int8_t>(surface.encoderOption());
      if (surface.copyMode() == ControlSurface<EngineRoot>::Copy::Note && ed.copyCount() == 1)
        ev.copyPitch = ed.copied(0).pitch;
      EditNote n;
      if (ed.selected(n)) {
        ev.note = true;
        ev.noteTick = n.tick;
        ev.notePitch = n.pitch;
        ev.noteVelocity = n.velocity;
        ev.noteLength = n.length;
      }
    }
    return ev;
  }
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
