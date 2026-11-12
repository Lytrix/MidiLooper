//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// MIDI input from the three ports, bounded a pass: DIN, the USB device and the USB host. Clock, Start, Stop and
// Continue go to the clock and the transport; the DROID's control plane (channels 13–16: never recorded, never thru) to
// the surface; every other channel message to the engine at the last played tick (input at tick c arrives after c is
// played).

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>

#include "Firmware.h"
#include "MidiPorts.h"

namespace firmware {

using namespace ContentEngine;

struct MidiInput {
  static constexpr uint8_t kInputPerPass = 32;  // messages a port a pass

  MidiInput(Firmware& f, MidiPorts& p, Diagnostics& d) : fw(f), ports(p), diag(d) {}

  Firmware& fw;
  MidiPorts& ports;
  Diagnostics& diag;
  bool trace = false;  // `!ENGINE trace 1`: every control-plane message and what the edit surface did with it

  void poll(uint32_t nowUs) {
    for (uint8_t i = 0; i < kInputPerPass && Serial8.available(); ++i) din_.feed(*this, static_cast<uint8_t>(Serial8.read()), nowUs);
    for (uint8_t i = 0; i < kInputPerPass && usbMIDI.read(); ++i) {
      ++diag.usbIn;
      const uint8_t type = usbMIDI.getType();
      if (type >= 0xF8) {
        realtime(type, nowUs);
      } else if (type >= 0x80 && type < 0xF0) {
        channelMessage(static_cast<uint8_t>(type | ((usbMIDI.getChannel() - 1) & 0x0F)), usbMIDI.getData1(),
                       usbMIDI.getData2(), nowUs);
      }
    }
    ports.host.Task();
    for (uint8_t i = 0; i < kInputPerPass && ports.hostMidi.read(); ++i) {
      const uint8_t type = ports.hostMidi.getType();
      if (type >= 0xF8) {
        realtime(type, nowUs);
      } else if (type >= 0x80 && type < 0xF0) {
        channelMessage(static_cast<uint8_t>(type | ((ports.hostMidi.getChannel() - 1) & 0x0F)), ports.hostMidi.getData1(),
                       ports.hostMidi.getData2(), nowUs);
      }
    }
  }

 private:
  void realtime(uint8_t status, uint32_t nowUs) {
    auto& clock = fw.clock;
    switch (status) {
      case 0xF8: {
        ++diag.pulses;
        const uint32_t t = micros();
        if (diag.lastPulseUs != 0 && t - diag.lastPulseUs > 200000 && diag.gapCount < 8)
          diag.gaps[diag.gapCount++] =
              Diagnostics::Gap{millis(), t - diag.lastPulseUs, diag.passes - diag.passesAtPulse,
                               diag.usbIn - diag.usbInAtPulse, diag.usbOut - diag.usbOutAtPulse, diag.usbOutPassMax};
        diag.lastPulseUs = t;
        diag.passesAtPulse = diag.passes;
        diag.usbInAtPulse = diag.usbIn;
        diag.usbOutAtPulse = diag.usbOut;
        diag.usbOutPassMax = 0;
        clock.midiPulse(nowUs);
        break;
      }
      case 0xFA:  // Start: a running transport stops first, so no track plays across the jump to the next bar line
        if (clock.running()) fw.engine.control().transportStop(fw.lastTick, nowUs, clock.usPerTick());
        clock.midiStart(nowUs);
        fw.engine.control().transportStart(clock.nextTick(), nowUs, clock.usPerTick());
        fw.surface.transportStarted(clock.nextTick());
        break;
      case 0xFB:
        if (clock.running()) break;
        clock.midiContinue(nowUs);
        fw.engine.control().transportStart(clock.nextTick(), nowUs, clock.usPerTick());
        fw.surface.transportStarted(clock.nextTick());
        break;
      case 0xFC:
        if (clock.running()) fw.engine.control().transportStop(fw.lastTick, nowUs, clock.usPerTick());
        clock.midiStop(nowUs);
        break;
      default:
        break;
    }
  }

  void channelMessage(uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs) {
    using namespace ContentEngine;
    if (ControlSurface<EngineRoot>::controlPlane(status)) {  // the DROID's: never recorded, never thru
      fw.surface.input(status, d1, d2, millis());
      if (trace) {
        const auto& ed = fw.surface.edit();
        EditNote n{};
        const bool sel = ed.selected(n);
        Serial.printf("#IN,%lu,%02x,%u,%u,editing=%u,sel=%u,why=%u,refused=%u,depth=%u,tick=%lu,pitch=%u,bracket=%lu\n",
                      static_cast<unsigned long>(millis()), status, d1, d2, ed.editing(), sel,
                      static_cast<unsigned>(ed.lastWhy()), ed.lastRefusal(), fw.engine.noteEdit().depth(),
                      sel ? static_cast<unsigned long>(n.tick) : 0ul, sel ? n.pitch : 0u,
                      static_cast<unsigned long>(ed.bracket()));
      }
      return;
    }
    const uint8_t type = status & 0xF0;
    if (type == 0x90)
      fw.engine.inputNoteOn(fw.lastTick, status, d1, d2, nowUs, fw.clock.usPerTick());
    else if (type == 0x80)
      fw.engine.inputNoteOff(fw.lastTick, status, d1, nowUs);
    else
      fw.engine.control().inputEvent(fw.lastTick, status, d1, d2, nowUs);
  }

  // DIN: a byte parser with running status; realtime bytes may arrive between a message's bytes.
  struct DinParser {
    uint8_t status = 0, data[2] = {}, have = 0;
    void feed(MidiInput& in, uint8_t b, uint32_t nowUs) {
      if (b >= 0xF8) {
        in.realtime(b, nowUs);
        return;
      }
      if (b >= 0xF0) {  // system common / SysEx: not used, ends running status
        status = 0;
        return;
      }
      if (b & 0x80) {
        status = b;
        have = 0;
        return;
      }
      if (status == 0) return;
      data[have++] = b;
      const uint8_t type = status & 0xF0;
      const uint8_t need = (type == 0xC0 || type == 0xD0) ? 1 : 2;
      if (have < need) return;
      have = 0;
      in.channelMessage(status, data[0], need == 2 ? data[1] : 0, nowUs);
    }
  };
  DinParser din_;
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
