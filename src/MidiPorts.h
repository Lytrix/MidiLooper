//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The three MIDI ports' output: DIN (Serial8, 31,250 baud), the USB device (the computer) and the USB host (the
// DROID). Playback goes to DIN and the USB device; the DROID gets only the surface's feedback (LEDs, fader bank,
// motor faders), never playback — the host driver's `write_packed` spins until the DROID takes a packet, so a
// playback burst there hangs the loop. Every host send is admitted first (`UsbHostTransmitAdmission.h`).

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <USBHost_t36.h>

#include "ContentEngine/ControlSurface/UsbHostTransmitAdmission.h"
#include "Firmware.h"

namespace firmware {

using namespace ContentEngine;

namespace midiports {

// The host driver's transmit state is private; an explicit instantiation may name private members, so these tags read
// it without patching the library (`hosttx::hasRoom`).
template <typename Tag, typename Tag::type M>
struct DriverField {
  friend typename Tag::type field(Tag) { return M; }
};
struct Tx1 {
  using type = volatile uint8_t MIDIDeviceBase::*;
  friend type field(Tx1);
};
struct Tx2 {
  using type = volatile uint8_t MIDIDeviceBase::*;
  friend type field(Tx2);
};
struct TxSize {
  using type = uint16_t MIDIDeviceBase::*;
  friend type field(TxSize);
};
struct TxPipe {
  using type = Pipe_t* MIDIDeviceBase::*;
  friend type field(TxPipe);
};
template struct DriverField<Tx1, &MIDIDeviceBase::tx1_count>;
template struct DriverField<Tx2, &MIDIDeviceBase::tx2_count>;
template struct DriverField<TxSize, &MIDIDeviceBase::tx_size>;
template struct DriverField<TxPipe, &MIDIDeviceBase::txpipe>;

}  // namespace midiports

struct MidiPorts {
  static constexpr uint8_t kSendPerPass = 32;  // messages a port a pass
  static constexpr uint32_t kLedGapUs = 1000;  // the DROID USB host drops packets closer than ~1 ms
  // USB heartbeat: while following an external clock, an Active Sensing byte goes to the computer whenever the port has
  // been quiet for `kHeartbeatUs`. Without it, Ableton's USB stream (clock and notes) stalls while the Teensy sends
  // nothing — a late pulse pauses the ticks, so the port goes quiet — until the clock timeout resumes output.
  static constexpr uint32_t kHeartbeatUs = 20000;

  explicit MidiPorts(Diagnostics& d) : diag(d) {}

  Diagnostics& diag;
  USBHost host;
  MIDIDevice_BigBuffer hostMidi{host};
  uint32_t lastUsbTxUs = 0, heartbeats = 0;
  uint32_t lastLedUs = 0;
  uint32_t hostDropped = 0, hostDeferred = 0;  // diagnostics (`!ENGINE take`): host messages dropped, LED passes waited

  /// DIN and the USB host started; `dinTx` extends DIN's transmit buffer beyond the core's 40 bytes, so a pass's
  /// messages are stored instead of waiting for the 31,250-baud line (320 us a byte).
  void begin(uint8_t* dinTx, size_t dinTxBytes) {
    Serial8.begin(31250);
    Serial8.addMemoryForWrite(dinTx, dinTxBytes);
    host.begin();
  }

  /// The host driver's transmit state; call with interrupts off (a completion empties a buffer, the timer fills one).
  hosttx::State hostTxState() {
    using namespace midiports;
    const MIDIDeviceBase& d = hostMidi;
    return {d.*field(TxPipe{}) != nullptr, d.*field(Tx1{}), d.*field(Tx2{}), static_cast<uint32_t>(d.*field(TxSize{}) / 4)};
  }

  /// The host port's transmit pipe is open (the DROID enumerated).
  bool hostPipeOpen() const {
    using namespace midiports;
    const MIDIDeviceBase& d = hostMidi;
    return d.*field(TxPipe{}) != nullptr;
  }

  /// True when the host port takes a message now (the LED stage waits while it does not).
  bool hostTxRoom() {
    __disable_irq();
    const bool room = hosttx::hasRoom(hostTxState());
    __enable_irq();
    return room;
  }

  /// One channel message to the DROID, or none (counted) when `write_packed` would wait. The check and the send run
  /// with interrupts off, so the driver's timer cannot fill the last buffer between them (`write_packed` re-enables
  /// them).
  void hostSend(uint8_t type, uint8_t d1, uint8_t d2, uint8_t channel) {
    __disable_irq();
    if (!hosttx::hasRoom(hostTxState())) {
      __enable_irq();
      ++hostDropped;
      return;
    }
    hostMidi.send(type, d1, d2, channel);
    __enable_irq();
  }

  void sendDin(uint8_t status, uint8_t d1, uint8_t d2) {
    Serial8.write(status);
    Serial8.write(d1);
    const uint8_t type = status & 0xF0;
    if (type != 0xC0 && type != 0xD0) Serial8.write(d2);
  }

  /// Each port's due playback messages to the wire; the host port's are drained (playback never goes to the DROID).
  void transmit(ContentEngine::EngineRoot& engine, uint32_t nowUs) {
    using namespace ContentEngine;
    ProductionHub::Sent buf[kSendPerPass];
    uint8_t n = engine.transmit(kPortDin, nowUs, buf, kSendPerPass);
    for (uint8_t i = 0; i < n; ++i) sendDin(buf[i].status, buf[i].data1, buf[i].data2);
    n = engine.transmit(kPortUsbDevice, nowUs, buf, kSendPerPass);
    for (uint8_t i = 0; i < n; ++i)
      usbMIDI.send(buf[i].status & 0xF0, buf[i].data1, buf[i].data2, (buf[i].status & 0x0F) + 1, 0);
    if (n != 0) {
      usbMIDI.send_now();
      lastUsbTxUs = micros();
    }
    diag.usbOut += n;
    diag.outThisPass += n;
    (void)engine.transmit(kPortUsbHost, nowUs, buf, kSendPerPass);
  }

  /// Active Sensing to the computer while an external clock runs and the port has been quiet `kHeartbeatUs`.
  void heartbeat(bool external) {
    if (!external || micros() - lastUsbTxUs < kHeartbeatUs) return;
    usbMIDI.sendRealTime(usbMIDI.ActiveSensing);
    usbMIDI.send_now();
    lastUsbTxUs = micros();
    ++heartbeats;
  }

  // ---- The surface's feedback: every port, as the DROID listens on all of them --------------------------------------
  /// The DROID's fader bank (Program Change, channel 16).
  void sendBank(uint8_t program) {
    using Surface = ContentEngine::ControlSurface<ContentEngine::EngineRoot>;
    const uint8_t status = static_cast<uint8_t>(0xC0 | Surface::kProgramChannel);
    sendDin(status, program, 0);
    usbMIDI.sendProgramChange(program, Surface::kProgramChannel + 1, 0);
    usbSent();
    hostSend(0xC0, program, 0, Surface::kProgramChannel + 1);
  }
  /// A motor-fader message (NOTE_EDIT and GROOVE feedback).
  void sendMotor(const ContentEngine::ControlSurfaceNoteEdit<ContentEngine::EngineRoot>::MotorMsg& m) {
    sendDin(m.status, m.d1, m.d2);
    const uint8_t type = m.status & 0xF0, channel = static_cast<uint8_t>((m.status & 0x0F) + 1);
    usbMIDI.send(type, m.d1, m.d2, channel, 0);
    usbSent();
    hostSend(type, m.d1, m.d2, channel);
  }
  /// An LED (channel 15 note).
  void sendLed(uint8_t note, uint8_t velocity) {
    using Surface = ContentEngine::ControlSurface<ContentEngine::EngineRoot>;
    const uint8_t status = static_cast<uint8_t>((velocity != 0 ? 0x90 : 0x80) | Surface::kLedChannel);
    sendDin(status, note, velocity);
    usbMIDI.send(status & 0xF0, note, velocity, Surface::kLedChannel + 1, 0);
    usbSent();
    hostSend(status & 0xF0, note, velocity, Surface::kLedChannel + 1);
  }

 private:
  void usbSent() {
    usbMIDI.send_now();
    lastUsbTxUs = micros();
    ++diag.usbOut;
    ++diag.outThisPass;
  }
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
