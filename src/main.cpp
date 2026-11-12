//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine firmware: the `teensy41-capture-serial` env (`teensy41-engine` the same build; `ENGINE_FIRMWARE`).
//
// One main loop, no interrupts of its own:
// 1. Input, bounded a pass: DIN (Serial8, 31,250 baud), USB device and USB host MIDI. Clock, Start, Stop, Continue go
//    to `EngineClock`; channel messages to the engine's input path at the last played tick (input at tick c arrives
//    after c is played).
// 2. Every tick the clock has due, in order (`EngineClock::due`, at most `kCatchUp` a pass): `EngineRoot::clockTick`.
// 3. The jobs (`submitJobs`) and worker grains while the scheduler grants them.
// 4. Each port's due messages to the wire: DIN and USB device. The USB host port is the DROID's: it gets only the
//    surface's LED feedback, never playback — the host driver's `write_packed` spins until the DROID takes a packet, so
//    a playback burst there hangs the loop. Every host send is admitted first (`hostSend`,
//    `UsbHostTransmitAdmission.h`): dropped when both transmit buffers are full, and the LED stage waits.
// 5. The control surface (`ControlSurface`): the DROID control plane (channels 13–16, never recorded nor thru) as the
//    DROID patch's gesture map, and its LEDs (channel 15 notes on every port) paced at one message a millisecond (the
//    DROID host's gap); the DROID's fader bank (Program Change 1 / 0 on channel 16, every port) follows NOTE_EDIT, so
//    its faders send the note-edit messages. Serial commands (`!ENGINE ...`, `!STOP_STATUS`, `!REBOOT`) remain.
// 6. The screen (`OledScreen`): a frame every 33 ms when the previous DMA transfer is done; each track shows its
//    selected slot; NOTE_EDIT shows its frozen window, bracket and selected note (start, pitch, velocity, length in the
//    status band).
// Breadcrumbs (`CrashReport.breadcrumb`, kept across a warm reset): the main-loop stage and the last tick, printed at
// the next boot (`#CAP,…,DIAG,prev_crumbs,…`), so a hang can be placed after a reset.
// Boot proof on USB serial: `#CAP,<us>,DIAG,boot_reset,crash=<0|1>`, the memory census, and once the card's slots are
// loaded `#CAP,<us>,BOOT,load,ok,...` with every outcome named. Nothing is written to the card before the owner
// approves saving (`!ENGINE approve-saving`, typed by the owner; remembered by the engine folder).
// Wall-clock time (real dates only): the Teensy RTC, valid only per `wallclock::valid` (never the firmware's build
// time). The Teensy loader sets the RTC to the computer's **local** time at every upload (in CET one hour ahead of
// UTC), so the RTC is read as local time and the shown offset (minutes, EEPROM bytes 0–3: 'T', 'Z', the offset)
// defaults to 0. `!ENGINE time` reports it, `!ENGINE time <unix>` sets the clock, `!ENGINE tz <minutes>` the offset
// (`scripts/engine_set_time.py` sets local time and offset 0, as the loader does); reported at boot as
// `#ENGINE,time,...`.

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <USBHost_t36.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <EEPROM.h>
#include <Encoder.h>

#include "ContentEngine/Timing/EngineClock.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/ControlSurface/UsbHostTransmitAdmission.h"
#include "ContentEngine/ControlSurface/ControlSurface.h"
#include "ContentEngine/Timing/WallClock.h"
#include "ContentEngine/History/HistoryCost.h"
#include "Teensy/OledScreen.h"
#include "Teensy/SdCardIo.h"

using namespace ContentEngine;

namespace {

EXTMEM EnginePsram gPsram;
DMAMEM EngineRam2 gRam2;
EngineRoot gEngine;
EngineClock gClock;
Teensy::SdCardIo gCard;
Teensy::OledScreen gDisplay;
ControlSurface<EngineRoot> gSurface;
uint32_t gLastLedUs = 0;
constexpr uint32_t kLedGapUs = 1000;  // the DROID USB host drops packets closer than ~1 ms
USBHost gHost;
MIDIDevice_BigBuffer gHostMidi(gHost);
// The host driver's transmit state is private; an explicit instantiation may name private members, so these tags read
// it without patching the library (`hosttx::hasRoom`, `HostTx.h`).
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
uint32_t gHostDropped = 0, gHostDeferred = 0;  // diagnostics (`!ENGINE take`): host messages dropped, LED passes waited

/// The host driver's transmit state; call with interrupts off (a completion empties a buffer, the timer fills one).
hosttx::State hostTxState() {
  const MIDIDeviceBase& d = gHostMidi;
  return {d.*field(TxPipe{}) != nullptr, d.*field(Tx1{}), d.*field(Tx2{}), static_cast<uint32_t>(d.*field(TxSize{}) / 4)};
}

/// True when the host port takes a message now (the LED stage waits while it does not).
bool hostTxRoom() {
  __disable_irq();
  const bool room = hosttx::hasRoom(hostTxState());
  __enable_irq();
  return room;
}

/// One channel message to the DROID, or none (counted) when `write_packed` would wait. The check and the send run with
/// interrupts off, so the driver's timer cannot fill the last buffer between them (`write_packed` re-enables them).
void hostSend(uint8_t type, uint8_t d1, uint8_t d2, uint8_t channel) {
  __disable_irq();
  if (!hosttx::hasRoom(hostTxState())) {
    __enable_irq();
    ++gHostDropped;
    return;
  }
  gHostMidi.send(type, d1, d2, channel);
  __enable_irq();
}

constexpr uint32_t kBlockUs = 2000;       // B_max: the longest non-engine block of a pass (input, USB, serial)
constexpr uint8_t kInputPerPass = 32;     // messages a port a pass
constexpr uint8_t kSendPerPass = 32;      // messages a port a pass
// The watchdog's timeout: no pass and no boot phase takes this long (the longest, `!ENGINE cardfree`, ~1 s).
constexpr uint32_t kWatchdogS = 4;
// DIN's transmit buffer beyond the core's 40 bytes: a pass's 32 messages per port, the LEDs, bank and motor feedback (3
// bytes each) fit, so `Serial8.write` stores and returns instead of waiting for the 31,250-baud line (320 us a byte).
DMAMEM uint8_t gDinTx[512];
bool reportBootReset();
void crumb(uint32_t stage);
[[noreturn]] void quietReboot(uint32_t stage);
constexpr uint8_t kGrainsPerPass = 8;     // the scheduler's own slack check decides; this bounds one pass
constexpr uint32_t kDefaultTempo = 12000;  // 120 BPM until a tempo is set or an external clock arrives

uint32_t gLastTick = 0;
enum Stage : uint32_t { kInput = 1, kSerial, kTicks, kJobs, kGrains, kSend, kSurface, kLeds, kScreen, kIdle, kRebooting, kUsbWedged };
// Diagnostics: the worst time each main-loop stage took since the last `!ENGINE take` (the time between two crumbs is
// the earlier stage's).
uint32_t gStageMaxUs[kUsbWedged + 1] = {};
uint32_t gStageAtUs = 0, gStage = 0;
// USB heartbeat: while following an external clock, an Active Sensing byte goes to the USB host whenever the port has
// been quiet for `kHeartbeatUs`. Without it, Ableton's USB stream (clock and notes) stalled while the Teensy sent
// nothing — a late pulse paused the ticks, so the port went quiet — until the clock timeout resumed output: every gap
// ended one pulse after the timeout (500 ms → 518 ms gaps, 250 ms → 268 ms). With it: no gap in a 4-minute
// record / overdub run ("rock stable").
constexpr uint32_t kHeartbeatUs = 20000;
uint32_t gLastUsbTxUs = 0, gHeartbeats = 0;
uint32_t gPassMaxUs = 0, gPulses = 0, gTimeouts = 0;  // diagnostics (`!ENGINE take`)
// Clock-gap diagnostics: counters since the last pulse, snapshotted when two pulses are > 200 ms apart.
uint32_t gPasses = 0, gUsbIn = 0, gUsbOut = 0, gUsbOutPassMax = 0, gLastPulseUs = 0;
uint32_t gPassesAtPulse = 0, gUsbInAtPulse = 0, gUsbOutAtPulse = 0;
struct Gap {
  uint32_t atMs, gapUs, passes, usbIn, usbOut, outBurst;
};
Gap gGaps[8];
uint8_t gGapCount = 0;
uint32_t gOutThisPass = 0;
bool gWasExternal = false;  // the last tick played (input arriving now belongs after it)
bool gBootReported = false;
uint32_t gBootStartUs = 0;

// ---- Output ---------------------------------------------------------------------------------------------------------
void sendDin(uint8_t status, uint8_t d1, uint8_t d2) {
  Serial8.write(status);
  Serial8.write(d1);
  const uint8_t type = status & 0xF0;
  if (type != 0xC0 && type != 0xD0) Serial8.write(d2);
}

void transmitAll(uint32_t nowUs) {
  ProductionHub::Sent buf[kSendPerPass];
  uint8_t n = gEngine.transmit(kPortDin, nowUs, buf, kSendPerPass);
  for (uint8_t i = 0; i < n; ++i) sendDin(buf[i].status, buf[i].data1, buf[i].data2);
  n = gEngine.transmit(kPortUsbDevice, nowUs, buf, kSendPerPass);
  for (uint8_t i = 0; i < n; ++i)
    usbMIDI.send(buf[i].status & 0xF0, buf[i].data1, buf[i].data2, (buf[i].status & 0x0F) + 1, 0);
  if (n != 0) {
    usbMIDI.send_now();
    gLastUsbTxUs = micros();
  }
  gUsbOut += n;
  gOutThisPass += n;
  (void)gEngine.transmit(kPortUsbHost, nowUs, buf, kSendPerPass);  // drained: playback never goes to the DROID
}

// ---- Input ----------------------------------------------------------------------------------------------------------
void realtime(uint8_t status, uint32_t nowUs) {
  switch (status) {
    case 0xF8: {
      ++gPulses;
      const uint32_t t = micros();
      if (gLastPulseUs != 0 && t - gLastPulseUs > 200000 && gGapCount < 8)
        gGaps[gGapCount++] = Gap{millis(), t - gLastPulseUs, gPasses - gPassesAtPulse, gUsbIn - gUsbInAtPulse,
                                 gUsbOut - gUsbOutAtPulse, gUsbOutPassMax};
      gLastPulseUs = t;
      gPassesAtPulse = gPasses;
      gUsbInAtPulse = gUsbIn;
      gUsbOutAtPulse = gUsbOut;
      gUsbOutPassMax = 0;
      gClock.midiPulse(nowUs);
      break;
    }
    case 0xFA:  // Start: a running transport stops first, so no track plays across the jump to the next bar line
      if (gClock.running()) gEngine.control().transportStop(gLastTick, nowUs, gClock.usPerTick());
      gClock.midiStart(nowUs);
      gEngine.control().transportStart(gClock.nextTick(), nowUs, gClock.usPerTick());
      gSurface.transportStarted(gClock.nextTick());
      break;
    case 0xFB:
      if (gClock.running()) break;
      gClock.midiContinue(nowUs);
      gEngine.control().transportStart(gClock.nextTick(), nowUs, gClock.usPerTick());
      gSurface.transportStarted(gClock.nextTick());
      break;
    case 0xFC:
      if (gClock.running()) gEngine.control().transportStop(gLastTick, nowUs, gClock.usPerTick());
      gClock.midiStop(nowUs);
      break;
    default:
      break;
  }
}

bool gTrace = false;  // `!ENGINE trace 1`: every control-plane message and what the edit surface did with it

void channelMessage(uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs) {
  if (ControlSurface<EngineRoot>::controlPlane(status)) {  // the DROID's: never recorded, never thru
    gSurface.input(status, d1, d2, millis());
    if (gTrace) {
      const auto& ed = gSurface.edit();
      EditNote n{};
      const bool sel = ed.selected(n);
      Serial.printf("#IN,%lu,%02x,%u,%u,editing=%u,sel=%u,why=%u,refused=%u,depth=%u,tick=%lu,pitch=%u,bracket=%lu\n",
                    static_cast<unsigned long>(millis()), status, d1, d2, ed.editing(), sel,
                    static_cast<unsigned>(ed.lastWhy()), ed.lastRefusal(), gEngine.noteEdit().depth(),
                    sel ? static_cast<unsigned long>(n.tick) : 0ul, sel ? n.pitch : 0u,
                    static_cast<unsigned long>(ed.bracket()));
    }
    return;
  }
  const uint8_t type = status & 0xF0;
  if (type == 0x90)
    gEngine.inputNoteOn(gLastTick, status, d1, d2, nowUs, gClock.usPerTick());
  else if (type == 0x80)
    gEngine.inputNoteOff(gLastTick, status, d1, nowUs);
  else
    gEngine.control().inputEvent(gLastTick, status, d1, d2, nowUs);
}

// DIN: a byte parser with running status; realtime bytes may arrive between a message's bytes.
struct DinParser {
  uint8_t status = 0, data[2] = {}, have = 0;
  void feed(uint8_t b, uint32_t nowUs) {
    if (b >= 0xF8) {
      realtime(b, nowUs);
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
    channelMessage(status, data[0], need == 2 ? data[1] : 0, nowUs);
  }
} gDin;

void pollInput(uint32_t nowUs) {
  for (uint8_t i = 0; i < kInputPerPass && Serial8.available(); ++i) gDin.feed(static_cast<uint8_t>(Serial8.read()), nowUs);
  for (uint8_t i = 0; i < kInputPerPass && usbMIDI.read(); ++i) {
    ++gUsbIn;
    const uint8_t type = usbMIDI.getType();
    if (type >= 0xF8) {
      realtime(type, nowUs);
    } else if (type >= 0x80 && type < 0xF0) {
      channelMessage(static_cast<uint8_t>(type | ((usbMIDI.getChannel() - 1) & 0x0F)), usbMIDI.getData1(),
                     usbMIDI.getData2(), nowUs);
    }
  }
  gHost.Task();
  for (uint8_t i = 0; i < kInputPerPass && gHostMidi.read(); ++i) {
    const uint8_t type = gHostMidi.getType();
    if (type >= 0xF8) {
      realtime(type, nowUs);
    } else if (type >= 0x80 && type < 0xF0) {
      channelMessage(static_cast<uint8_t>(type | ((gHostMidi.getChannel() - 1) & 0x0F)), gHostMidi.getData1(),
                     gHostMidi.getData2(), nowUs);
    }
  }
}

// ---- The GPIO encoder (pins 29 / 30 and the push on 31) -----------------------------------------------------
// Quadrature on 29 / 30 (the Encoder library's interrupts, four counts a detent), the push
// on 31 to ground (pull-up), debounced: a level held `kEncoderDebounceMs` is taken.
constexpr uint8_t kEncoderPinA = 29, kEncoderPinB = 30, kEncoderButtonPin = 31;
constexpr uint32_t kEncoderDebounceMs = 5;
Encoder gEncoder(kEncoderPinA, kEncoderPinB);
long gEncoderDetents = 0;
bool gEncoderDown = false, gEncoderRaw = false;
uint32_t gEncoderRawMs = 0;
void serviceEncoder(uint32_t ms) {
  const long detents = gEncoder.read() / 4;
  if (detents != gEncoderDetents) {
    gSurface.encoderTurn(static_cast<int32_t>(detents - gEncoderDetents), ms);
    gEncoderDetents = detents;
  }
  const bool raw = digitalRead(kEncoderButtonPin) == LOW;
  if (raw != gEncoderRaw) {
    gEncoderRaw = raw;
    gEncoderRawMs = ms;
  } else if (raw != gEncoderDown && ms - gEncoderRawMs >= kEncoderDebounceMs) {
    gEncoderDown = raw;
    gSurface.encoderButton(raw, ms);
  }
}

// ---- Serial commands (until the surface is wired) ---------------------------------------------------------------
char gLine[160];  // a `file-data` line: 64 bytes in hex (grooves to the card)
uint8_t gLineLength = 0;

// ---- Wall-clock time -------------------------------------------------------------------------------------------------
int16_t gTzOffset = 0;  // minutes east of UTC
void loadTzOffset() {
  if (EEPROM.read(0) != 'T' || EEPROM.read(1) != 'Z') return;
  gTzOffset = wallclock::clampOffset(static_cast<int16_t>(EEPROM.read(2) | (EEPROM.read(3) << 8)));
}
void storeTzOffset(int16_t minutes) {
  gTzOffset = wallclock::clampOffset(minutes);
  EEPROM.update(0, 'T');
  EEPROM.update(1, 'Z');
  EEPROM.update(2, static_cast<uint8_t>(gTzOffset & 0xFF));
  EEPROM.update(3, static_cast<uint8_t>((gTzOffset >> 8) & 0xFF));
}
// The global set switch point, kept in EEPROM bytes 4-5 ('W', 0 next bar / 1 loop end).
void loadSwitchAt() {
  if (EEPROM.read(4) != 'W') return;
  gEngine.setSwitchAt(EEPROM.read(5) == 1 ? ProductionControl::SwitchAt::LoopEnd : ProductionControl::SwitchAt::NextBar);
}
void storeSwitchAt(ProductionControl::SwitchAt at) {
  gEngine.setSwitchAt(at);
  EEPROM.update(4, 'W');
  EEPROM.update(5, at == ProductionControl::SwitchAt::LoopEnd ? 1 : 0);
}
// The global launch quantisation (1/16 default, 1/8, 1/4, bar), kept in EEPROM bytes 6-7 ('L', the
// divisions of a bar: 16, 8, 4 or 1). A slot's double press launches at the next step; a queued switch shows it.
constexpr uint8_t kLaunchDivisions[] = {16, 8, 4, 1};
bool launchDivisionValid(uint32_t d) {
  for (uint8_t v : kLaunchDivisions)
    if (v == d) return true;
  return false;
}
void applyLaunchDivision(uint8_t d) { gEngine.control().setLaunchGrid(kTicksPerBar / d); }
void loadLaunchGrid() {
  if (EEPROM.read(6) != 'L' || !launchDivisionValid(EEPROM.read(7))) return;
  applyLaunchDivision(EEPROM.read(7));
}
void storeLaunchGrid(uint8_t d) {
  applyLaunchDivision(d);
  EEPROM.update(6, 'L');
  EEPROM.update(7, d);
}
const char* launchGridName() {
  switch (kTicksPerBar / gEngine.control().launchGrid()) {
    case 16: return "16";
    case 8: return "8";
    case 4: return "4";
    default: return "bar";
  }
}
const char* switchAtName() { return gEngine.switchAt() == ProductionControl::SwitchAt::LoopEnd ? "loop" : "bar"; }
uint32_t gLastSaveWall = 0;   // the wall time of the store's last completed slot write (0: none this boot, or no clock)
uint32_t gSavesSeen = 0;      // `Persistence::stats().saves` when last looked at
/// The RTC's time, or 0 when it is not valid (`wallclock::valid`).
uint32_t wallTime() {
  const uint32_t t = static_cast<uint32_t>(rtc_get());
  return wallclock::valid(t) ? t : 0;
}
void reportTime() {
  const uint32_t raw = static_cast<uint32_t>(rtc_get());
  char stamp[24], saved[8];
  wallclock::formatStamp(raw, gTzOffset, stamp, sizeof stamp);
  wallclock::formatClock(gLastSaveWall, gTzOffset, saved, sizeof saved);
  Serial.printf("#ENGINE,time,unix=%lu,valid=%u,offset_min=%d,local=%s,last_save=%lu,saved=%s\n",
                static_cast<unsigned long>(raw), wallclock::valid(raw) ? 1u : 0u, gTzOffset, stamp,
                static_cast<unsigned long>(gLastSaveWall), saved);
}
/// A completed store write (a slot copy) is stamped with the wall time: `SAVED 14:32`.
void stampSaves() {
  const uint32_t saves = gEngine.persistence().stats().saves;
  if (saves == gSavesSeen) return;
  gSavesSeen = saves;
  gLastSaveWall = wallTime();
}

#include "LibraryOperations.h"

#include "SerialCommands.h"

void selectFirstSlots();
// Boot shows the last selected slot, no switch on screen: the saved selection applied as
// soon as the boot load's first grain has read it, before the performance screen draws its first frame.
bool gSelectionKnown = false;
/// The saved selection (selected track, each track's slot), once read; false: none yet (or none saved).
bool applySavedSelection() {
  uint8_t state[ProductionPersistence::kStateBytes];
  if (!gEngine.persistence().restoredSurfaceState(state) || state[0] >= kTrackCount) return false;
  gEngine.control().selectTrack(state[0]);
  for (uint8_t t = 0; t < kTrackCount; ++t)
    if (state[1 + t] / 8 == t) gSurface.selectSlot(t, static_cast<uint8_t>(state[1 + t] % 8));
  for (uint8_t t = 0; t < kTrackCount; ++t)  // the muted tracks
    gEngine.control().setMute(t, (state[ProductionPersistence::kStateBytes - 1] >> t & 1u) != 0, micros());
  if (const uint8_t bars = gEngine.persistence().restoredCycleBars()) gSurface.setCycleBars(bars);
  return true;
}
// After boot load: without a saved selection, each track's first slot with content (slots T x 8 .. T x 8 + 7); the
// saved one was applied as soon as it was read (`gSelectionKnown`).
void restoreSelection() {
  if (!gSelectionKnown && !applySavedSelection()) selectFirstSlots();
  gSelectionKnown = true;
}
/// The selection as the surface has it now, for saving (selected track, then each track's engine slot).
void saveSelection(uint32_t nowUs) {
  uint8_t state[ProductionPersistence::kStateBytes];
  state[0] = gEngine.control().selectedTrack();
  for (uint8_t t = 0; t < kTrackCount; ++t) state[1 + t] = gSurface.selectedSlot(t);
  uint8_t mutes = 0;
  for (uint8_t t = 0; t < kTrackCount; ++t)
    if (gEngine.control().muted(t)) mutes |= static_cast<uint8_t>(1u << t);
  state[ProductionPersistence::kStateBytes - 1] = mutes;
  gEngine.setSurfaceState(state, nowUs);
  gEngine.setEngineSettings(gSurface.cycleBars(), nowUs);  // the GROOVE cycle's bars
}

void selectFirstSlots() {
  for (uint8_t t = 0; t < kTrackCount; ++t)
    for (uint8_t s = 0; s < 8; ++s)
      if (gEngine.transport().contentOf(static_cast<uint8_t>(t * 8 + s)) != kNoManifest) {
        gSurface.selectSlot(t, s);
        break;
      }
}

/// The DROID's fader bank (Program Change, channel 16) on every port, as the LEDs.
void sendBank(uint8_t program) {
  const uint8_t status = static_cast<uint8_t>(0xC0 | ControlSurface<EngineRoot>::kProgramChannel);
  sendDin(status, program, 0);
  usbMIDI.sendProgramChange(program, ControlSurface<EngineRoot>::kProgramChannel + 1, 0);
  usbMIDI.send_now();
  gLastUsbTxUs = micros();
  ++gUsbOut;
  ++gOutThisPass;
  hostSend(0xC0, program, 0, ControlSurface<EngineRoot>::kProgramChannel + 1);
}

/// A motor-fader message (NOTE_EDIT feedback) on every port, as the LEDs.
void sendMotor(const NoteEditSurface<EngineRoot>::MotorMsg& m) {
  sendDin(m.status, m.d1, m.d2);
  const uint8_t type = m.status & 0xF0, channel = static_cast<uint8_t>((m.status & 0x0F) + 1);
  usbMIDI.send(type, m.d1, m.d2, channel, 0);
  usbMIDI.send_now();
  gLastUsbTxUs = micros();
  ++gUsbOut;
  ++gOutThisPass;
  hostSend(type, m.d1, m.d2, channel);
}

void sendLed(uint8_t note, uint8_t velocity) {
  const uint8_t status = static_cast<uint8_t>((velocity != 0 ? 0x90 : 0x80) | ControlSurface<EngineRoot>::kLedChannel);
  sendDin(status, note, velocity);
  usbMIDI.send(status & 0xF0, note, velocity, ControlSurface<EngineRoot>::kLedChannel + 1, 0);
  usbMIDI.send_now();
  gLastUsbTxUs = micros();
  ++gUsbOut;
  ++gOutThisPass;
  hostSend(status & 0xF0, note, velocity, ControlSurface<EngineRoot>::kLedChannel + 1);
}

// ---- Breadcrumbs and the watchdog -----------------------------------------------------------------------------------
// The previous boot's breadcrumbs, copied by `startup_early_hook` before this boot writes any (DMAMEM is not cleared).
DMAMEM uint32_t gPrevCrumbs[8];
uint32_t gResetCause = 0;  // SRC_SRSR at boot: why the board reset (`kWatchdogReset` is the watchdog's)
constexpr uint32_t kWatchdogReset = SRC_SRSR_WDOG_RST_B;
// Boot phases (breadcrumb 3): how far a boot came, so a hang before the main loop is placed too. `kPhaseReset` and
// `kPhaseClocks` are written by the core's startup hooks, the rest by `setup`.
enum Phase : uint32_t {
  kPhaseReset = 1,  // the reset handler ran (startup_early_hook)
  kPhaseClocks,     // clocks, caches, PSRAM, before USB (startup_middle_hook)
  kPhaseSetup,      // USB started, constructors run, setup entered
  kPhaseSerial,     // the USB serial wait is over
  kPhaseEngine,     // the engine is initialised
  kPhaseDisplay,    // the display is started
  kPhasePorts,      // DIN and the USB host are started
  kPhaseCard,       // the card is mounted (or absent)
  kPhaseLoop,       // the main loop runs (breadcrumb 1 then holds its stage)
};

/// The watchdog's service: a pass (or a boot phase) that does not come back within `kWatchdogS` resets the board.
constexpr uint16_t kWicrWie = 1u << 15, kWicrWtis = 1u << 14;  // WDOG1_WICR: warning interrupt enable, its status
inline void feedWatchdog() {
  WDOG1_WSR = 0x5555;
  WDOG1_WSR = 0xAAAA;
}

void phase(uint32_t p) {
  feedWatchdog();
  CrashReport.breadcrumb(3, p);
}

/// Resets the board now: the given stage stays as the last crumb; nothing runs after the request (it takes effect some
/// time after the write, so the loop must not run on: its card, USB and PSRAM traffic would be cut off wherever the
/// reset lands);
/// the watchdog backs it up.
[[noreturn]] void quietReboot(uint32_t stage) {
  crumb(stage);
  __disable_irq();
  asm volatile("dsb" ::: "memory");
  SCB_AIRCR = 0x05FA0004;
  asm volatile("dsb" ::: "memory");
  for (;;) {
  }
}

// The USB device's health (the watchdog cannot see it: the loop runs on). A wedged USB device keeps the loop and the
// surface running while the host's open of the serial port and its writes block and the uploader cannot reboot the
// board. Every `kUsbCheckMs` the live USB state goes to breadcrumbs
// 4–6, so the boot after a PROGRAM press shows it (`usbState`); a wedge the board can see itself, held for
// `kUsbWedgedChecks` checks, is recorded as stage `kUsbWedged` and, while stopped, resets the board.
constexpr uint32_t kUsbCheckMs = 250;
constexpr uint8_t kUsbWedgedChecks = 4;
constexpr uint32_t kUsbIrqOff = 1, kUsbIrqStuck = 2, kUsbRxUnprimed = 4;  // wedge flags (breadcrumb 6, bits 0–2)
uint32_t gUsbCheckMs = 0, gUsbStuckStatus = 0, gUsbWedges = 0;
uint8_t gUsbWedgedFor = 0;
volatile uint32_t gUsbIsrs = 0;  // USB device interrupts served (`usbIsrCounted`)
uint32_t gUsbIsrsAtCheck = 0;

/// Breadcrumbs 4–6: (4) USBSTS bits 0–15, PORTSC1 bits 0–9 (connect, enabled, suspend, reset, high speed) at 16–25 and
/// its port speed at 28–29; (5) ENDPTSTATUS (primed endpoints: receive 0–7, transmit 16–23); (6) the wedge flags,
/// the interrupt enabled (bit 4) and pending (bit 5) in the NVIC, the configuration (bits 8–15), the serial bytes
/// waiting (16–23, at most 255) and the USB interrupts served since the last check (24–31, at most 255).
void usbState(uint32_t flags) {
  const uint32_t portsc = USB1_PORTSC1;
  const uint32_t isrs = gUsbIsrs, served = isrs - gUsbIsrsAtCheck;
  gUsbIsrsAtCheck = isrs;
  const int waiting = Serial.available();
  CrashReport.breadcrumb(4, (USB1_USBSTS & 0xFFFF) | (portsc & 0x3FF) << 16 | ((portsc >> 26) & 3) << 28);
  CrashReport.breadcrumb(5, USB1_ENDPTSTATUS);
  CrashReport.breadcrumb(6, flags | (NVIC_IS_ENABLED(IRQ_USB1) ? 0x10u : 0u) | (NVIC_IS_PENDING(IRQ_USB1) ? 0x20u : 0u) |
                                static_cast<uint32_t>(usb_configuration) << 8 |
                                static_cast<uint32_t>(waiting > 255 ? 255 : waiting) << 16 |
                                (served > 255 ? 255u : served) << 24);
}

void checkUsb(uint32_t nowMs) {
  if (nowMs - gUsbCheckMs < kUsbCheckMs) return;
  gUsbCheckMs = nowMs;
  uint32_t flags = 0;
  const uint32_t pending = USB1_USBSTS & USB1_USBINTR;
  if (usb_configuration) {  // enumerated: the device must be serviced
    if (!NVIC_IS_ENABLED(IRQ_USB1)) flags |= kUsbIrqOff;
    // An enabled interrupt the handler has not cleared since the last check (it clears its bits within microseconds).
    if (pending != 0 && (pending & gUsbStuckStatus) != 0) flags |= kUsbIrqStuck;
    // The serial receive endpoint (3) with no transfer primed and nothing buffered: the host's data can never land.
    if ((USB1_ENDPTSTATUS & (1u << 3)) == 0 && Serial.available() == 0) flags |= kUsbRxUnprimed;
  }
  gUsbStuckStatus = pending;
  usbState(flags);
  if (flags == 0) {
    gUsbWedgedFor = 0;
    return;
  }
  if (++gUsbWedgedFor < kUsbWedgedChecks) return;
  gUsbWedgedFor = 0;
  ++gUsbWedges;
  if (!gClock.running()) quietReboot(kUsbWedged);  // while playing, DIN plays on; the reset waits for a stop
}

}  // namespace

/// The watchdog's warning, half a second before it resets the board (highest priority, so it interrupts a hung ISR
/// too, though not code running with interrupts off): where the board hung — the interrupted code's PC and LR, and its
/// exception number (0: the main loop; 16 + IRQ: an interrupt handler) — as breadcrumbs 4–6 for the next boot.
extern "C" void watchdogWarning(const uint32_t* frame) {  // the stacked r0–r3, r12, lr, pc, xPSR
  CrashReport.breadcrumb(4, frame[6]);
  CrashReport.breadcrumb(5, frame[5]);
  CrashReport.breadcrumb(6, frame[7] & 0x1FF);
  WDOG1_WICR |= kWicrWtis;  // acknowledged; the reset still comes (the watchdog is not fed)
}

extern "C" void usb_isr(void);
/// The core's USB device interrupt, counted (`usbState`).
void usbIsrCounted() {
  ++gUsbIsrs;
  usb_isr();
}

extern "C" __attribute__((naked)) void watchdogWarningIsr() {
  asm volatile(
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "b watchdogWarning\n");
}

namespace {

/// The boot's reset rows (also `!ENGINE boot`, for a host that opened the port after the boot printed them): the reset
/// cause, and the previous boot's last main-loop stage and tick and how far it booted (bitmask, stage, tick, phase) —
/// where a hang stopped it. A stage with phase < kPhaseLoop is the boot before that one's (the hang came before the
/// loop). c4–c6: after a watchdog reset where it hung — PC, LR, exception number (0: the main loop; 16 + IRQ: a
/// handler), or the USB state if interrupts were off (the warning never ran); otherwise the last USB state
/// (`usbState`), the wedge's with stage 12 (`kUsbWedged`).
bool reportBootReset() {
  const bool crashed = static_cast<bool>(CrashReport);
  Serial.printf("#CAP,%lu,DIAG,boot_reset,crash=%u,cause=%lx,watchdog=%u\n", static_cast<unsigned long>(micros()),
                crashed ? 1u : 0u, static_cast<unsigned long>(gResetCause), (gResetCause & kWatchdogReset) ? 1u : 0u);
  Serial.printf("#CAP,%lu,DIAG,prev_crumbs,mask=%lx,stage=%lu,tick=%lu,phase=%lu,c4=%lx,c5=%lx,c6=%lx\n",
                static_cast<unsigned long>(micros()), static_cast<unsigned long>(gPrevCrumbs[0]),
                static_cast<unsigned long>(gPrevCrumbs[1]), static_cast<unsigned long>(gPrevCrumbs[2]),
                static_cast<unsigned long>(gPrevCrumbs[3]), static_cast<unsigned long>(gPrevCrumbs[4]),
                static_cast<unsigned long>(gPrevCrumbs[5]), static_cast<unsigned long>(gPrevCrumbs[6]));
  return crashed;
}

void crumb(uint32_t stage) {
  const uint32_t t = micros();
  if (gStage != 0 && t - gStageAtUs > gStageMaxUs[gStage]) gStageMaxUs[gStage] = t - gStageAtUs;
  gStage = stage;
  gStageAtUs = t;
  CrashReport.breadcrumb(1, stage);
  CrashReport.breadcrumb(2, gLastTick);
}

/// One worker grain; its time counts towards a load's worst grain while the library has card work.
bool timedGrain() {
  const bool working = gEngine.libraryWorking();
  const uint8_t phase = gEngine.setLoad().phase() >= 4 ? 2 : gEngine.setLoad().phase() >= 2 ? 1 : 0;
  const uint32_t t = micros();
  const bool ran = gEngine.runGrain(t);
  if (ran) {  // each job class's worst single grain (`!ENGINE take`)
    const uint32_t d = micros() - t;
    const uint8_t c = static_cast<uint8_t>(gEngine.scheduler().lastClass());
    if (c < 5 && d > gClassGrainMaxUs[c]) gClassGrainMaxUs[c] = d;
  }
  if (ran && working) {
    const uint32_t d = micros() - t;
    if (d > gLoadGrainMaxUs) gLoadGrainMaxUs = d;
    if (gEngine.setLoad().busy() && phase < 3 && d > gLoadPhaseMaxUs[phase]) gLoadPhaseMaxUs[phase] = d;
  }
  return ran;
}

void reportBootLoad() {
  const auto& st = gEngine.persistence().stats();
  Serial.printf("#CAP,%lu,BOOT,load,ok,card=%u,approved=%u,loads=%lu,load_failed_mask=%llx,v1_imported=%lu,"
                "v1_skipped_mask=%llx,v1_edited_mask=%llx,v1_failed_mask=%llx,held_whole=%lu,store=%u,migrated=%lu,us=%lu\n",
                static_cast<unsigned long>(micros()), gCard.ready(), gEngine.persistence().approved(),
                static_cast<unsigned long>(st.loads), static_cast<unsigned long long>(st.loadFailedMask),
                static_cast<unsigned long>(st.v1Imported), static_cast<unsigned long long>(st.v1SkippedMask),
                static_cast<unsigned long long>(st.v1EditedMask), static_cast<unsigned long long>(st.v1FailedMask),
                static_cast<unsigned long>(st.v1HeldWhole), st.store, static_cast<unsigned long>(st.migrated),
                static_cast<unsigned long>(micros() - gBootStartUs));
}

}  // namespace

// The core's startup hooks (weak in `startup.c`). The early hook runs before the caches and ITCM are set up, so it is
// FLASHMEM and writes RAM directly: it keeps the previous boot's breadcrumbs (the struct at the end of RAM2 survives a
// warm reset), starts this boot's clean, and arms the watchdog (WDOG1, its own 32 kHz clock), so a hang anywhere from
// here on — PSRAM, USB start, setup, the main loop — resets the board by itself instead of waiting for a PROGRAM press.
extern "C" FLASHMEM void startup_early_hook(void) {
  volatile uint32_t* bc = reinterpret_cast<volatile uint32_t*>(0x2027FFC0);
  for (int i = 0; i < 8; ++i) gPrevCrumbs[i] = bc[i];
  for (int i = 0; i < 8; ++i) bc[i] = 0;
  bc[3] = kPhaseReset;  // breadcrumb 3 (bc[0] is the mask, bc[1..6] the values)
  bc[0] = 1u << 2;
  CCM_CCGR3 |= CCM_CCGR3_WDOG1(CCM_CCGR_ON);
  WDOG1_WMCR = 0;  // the power-down counter off
  // Timeout (WT + 1) / 2 s; suspended under a debugger; no WDOG_B pin, no software reset.
  WDOG1_WCR = WDOG_WCR_WT(kWatchdogS * 2 - 1) | WDOG_WCR_WDBG | WDOG_WCR_WDE | WDOG_WCR_SRS | WDOG_WCR_WDA;
  WDOG1_WSR = 0x5555;  // fed here directly: nothing outside FLASHMEM runs yet
  WDOG1_WSR = 0xAAAA;
}

extern "C" FLASHMEM void startup_middle_hook(void) { phase(kPhaseClocks); }

FLASHMEM void setup() {  // once at boot, from flash (RAM1 room)
  phase(kPhaseSetup);
  gResetCause = SRC_SRSR;
  SRC_SRSR = gResetCause;  // write-one-to-clear: the next boot reports its own cause
  attachInterruptVector(IRQ_WDOG1, watchdogWarningIsr);
  NVIC_SET_PRIORITY(IRQ_WDOG1, 0);
  WDOG1_WICR = kWicrWie | kWicrWtis | 1;  // the warning 0.5 s before the reset
  NVIC_ENABLE_IRQ(IRQ_WDOG1);
  attachInterruptVector(IRQ_USB1, usbIsrCounted);
  // The title first: before the wait for USB serial (up to 3 s without a host), until the boot load
  // has ended.
  gDisplay.begin();
  gDisplay.drawTitle();
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    feedWatchdog();
    delay(10);
  }
  phase(kPhaseSerial);
  const bool crashed = reportBootReset();
  if (crashed) Serial.print(CrashReport);
  loadTzOffset();
  reportTime();
  if (!gEngine.init(gPsram, gRam2, kBlockUs)) {
    Serial.println("#CAP,BOOT,engine,init_failed");
    phase(kPhaseLoop);  // the loop still runs (and feeds the watchdog); nothing else starts
    return;
  }
  phase(kPhaseEngine);
  EngineRoot::census([](EngineRoot::Region r, const char* name, size_t bytes) {
    Serial.printf("#CAP,MEM_REGION,%s,%s,%u\n", r == EngineRoot::Region::Psram ? "psram" : "ram2", name,
                  static_cast<unsigned>(bytes));
  });
  // Each track plays on its own MIDI channel, track n on channel n, whatever channel a
  // note was recorded on; live thru follows the selected track's channel.
  for (uint8_t t = 0; t < kTrackCount; ++t) gEngine.output().setChannel(t, static_cast<uint8_t>(t + 1));
  gClock.init(kDefaultTempo, kTicksPerBar);
  loadSwitchAt();
  pinMode(kEncoderButtonPin, INPUT_PULLUP);
  loadLaunchGrid();
  gSurface.init(gEngine, gClock);
  phase(kPhaseDisplay);
  Serial8.begin(31250);
  Serial8.addMemoryForWrite(gDinTx, sizeof gDinTx);
  gHost.begin();
  phase(kPhasePorts);
  gBootStartUs = micros();
  if (gCard.begin()) {
    gEngine.attachCard(gCard);
    gEngine.startBootLoad();
  } else {
    Serial.println("#CAP,BOOT,card,absent");
    // Why: the card's own error, then the partition table (sector 0) when the card answered.
    static uint8_t mbr[512];
    const unsigned beginError = SD.sdfs.sdErrorCode(), beginData = SD.sdfs.sdErrorData();
    const bool read = SD.sdfs.card() && SD.sdfs.card()->readSector(0, mbr);
    Serial.printf("#ENGINE,card,begin_error=%u,begin_data=%u,sd_error=%u,sd_data=%u,mbr_read=%u,sig=%02x%02x,part1_type=%02x,part1_lba=%lu,"
                  "boot_jump=%02x,fs=%.8s\n",
                  beginError, beginData, SD.sdfs.sdErrorCode(), SD.sdfs.sdErrorData(), read, mbr[510], mbr[511], mbr[450],
                  static_cast<unsigned long>(mbr[454] | mbr[455] << 8 | mbr[456] << 16 | uint32_t(mbr[457]) << 24),
                  mbr[0], reinterpret_cast<const char*>(mbr + 3));
  }
  phase(kPhaseCard);
}

void loop() {
  static uint32_t passStart = 0;
  static bool looping = false;
  if (!looping) {
    looping = true;
    phase(kPhaseLoop);
  }
  feedWatchdog();
  {
    const uint32_t t = micros();
    if (passStart != 0 && t - passStart > gPassMaxUs) gPassMaxUs = t - passStart;
    passStart = t;
    if (gWasExternal && !gClock.external()) ++gTimeouts;
    gWasExternal = gClock.external();
    ++gPasses;
    if (gOutThisPass > gUsbOutPassMax) gUsbOutPassMax = gOutThisPass;
    gOutThisPass = 0;
  }
  uint32_t now = micros();
  crumb(kInput);
  pollInput(now);
  crumb(kSerial);
  pollSerial(now);
  crumb(kTicks);
  uint8_t count = 0;
  uint32_t tick;
  while (gClock.due(micros(), tick, count)) {
    gEngine.clockTick(tick, micros(), gClock.usPerTick());
    gLastTick = tick;
    transmitAll(micros());
  }
  now = micros();
  crumb(kJobs);
  gEngine.submitJobs(gLastTick, now, gClock.usPerTick());
  crumb(kGrains);
  for (uint8_t g = 0; g < kGrainsPerPass && timedGrain(); ++g) {
    uint8_t c2 = 0;
    if (gClock.due(micros(), tick, c2)) {  // a tick came due: play it before the next grain
      gEngine.clockTick(tick, micros(), gClock.usPerTick());
      gLastTick = tick;
    }
  }
  crumb(kSend);
  transmitAll(micros());
  if (gClock.external() && micros() - gLastUsbTxUs >= kHeartbeatUs) {
    usbMIDI.sendRealTime(usbMIDI.ActiveSensing);
    usbMIDI.send_now();
    gLastUsbTxUs = micros();
    ++gHeartbeats;
  }
  if (!gBootReported && gCard.ready() && !gEngine.persistence().booting()) {
    gBootReported = true;
    reportBootLoad();
    restoreSelection();
    openLibrary("boot");  // created here only once saving is approved (boot is stopped)
  } else if (!gBootReported && !gCard.ready()) {
    gBootReported = true;
  }
  crumb(kSurface);
  serviceEncoder(millis());
  gSurface.service(millis(), gLastTick, micros());
  if (gBootReported) saveSelection(micros());  // saved once settled, after approval
  if (gBootReported) births::watch(millis());
  stampSaves();
  serviceLibrary();
  serviceLoad();
  serviceWorking(micros());
  servicePicker(millis());
  serviceBrowser(micros());
  crumb(kLeds);
  uint8_t bank;
  if (gSurface.bank(bank)) sendBank(bank);  // the DROID's fader bank follows NOTE_EDIT
  NoteEditSurface<EngineRoot>::MotorMsg motor;
  if (gSurface.motor(millis(), motor)) sendMotor(motor);  // the DROID's motor faders follow the selection
  if (micros() - gLastLedUs >= kLedGapUs) {
    if (!hostTxRoom()) ++gHostDeferred;  // the DROID is not taking packets: the surface keeps its LEDs until it does
    else if (gSurface.leds(gLastTick, millis(), sendLed, 1) != 0) gLastLedUs = micros();
  }
  for (uint8_t t = 0; t < kTrackCount; ++t) gDisplay.show(t, gSurface.shown(t), gSurface.shownQueued(t), gSurface.shownQueuedPosition(t));
  Teensy::EditView ev;
  const auto& ed = gSurface.edit();
  ev.copy = static_cast<uint8_t>(gSurface.copyMode());  // copy mode's window
  ev.copySlot = gSurface.copySourceSlot();
  ev.copyBar = gSurface.copySourceBar();
  ev.copyPaste = static_cast<uint8_t>(gSurface.pasteMode());
  {  // the progress bar's frame — the stored loop, the current one bracketed in it
    const uint8_t t = gEngine.control().selectedTrack();
    uint32_t scale = 0, start = 0, known = 0;
    if (gSurface.loopFrame(gSurface.shown(t), scale, start, known)) {
      ev.barScale = scale;
      ev.barStart = start;
      ev.barKnown = known;
    }
  }
  ev.copyLeft = static_cast<uint16_t>(gSurface.copyMsLeft(millis()) * 1000 / ControlSurface<EngineRoot>::kCopyIdleMs);
  ev.loop = gSurface.loopEdit();
  ev.loopTicks = gSurface.pendingLoopTicks();
  ev.loopStart = gSurface.loopStart();
  ev.popup = gSurface.popup();
  ev.popupSlot = gSurface.popupSlot();
  ev.popupLoop = gSurface.popupLoop();
  ev.popupChoice = ev.popup ? gSurface.popupChoice() : 0xFF;
  ev.popupRanges = ev.popup ? gSurface.popupRanges() : 0;
  ev.groove = gSurface.grooveOpen();
  if (ev.groove) {
    const auto& g = gSurface.grooveSettings();
    ev.grooveSlot = gSurface.grooveWindowSlot();
    ev.grooveField = gSurface.grooveField();
    ev.grooveTemplate = g.tmpl;
    ev.grooveAmount = g.amount;
    ev.grooveAuto = gSurface.grooveAuto();  // AUTO Q, the window's
    ev.grooveState = static_cast<uint8_t>(gEngine.grooveState());
    ev.grooveProgress = gSurface.grooveProgress();
    ev.grooveATemplate = gSurface.grooveA().tmpl;
    ev.grooveAAmount = gSurface.grooveA().amount;
    ev.grooveHeard = static_cast<uint8_t>(gSurface.grooveHeard());
    ev.grooveCycling = gSurface.cycling();
    ev.grooveDots = gSurface.grooveDots();
    ev.grooveCycleBars = gSurface.cycleBars();
    ev.grooveTurnPos = gSurface.cycleTurnPos();
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
    ev.encoder = static_cast<int8_t>(gSurface.encoderOption());
    if (gSurface.copyMode() == ControlSurface<EngineRoot>::Copy::Note && ed.copyCount() == 1) ev.copyPitch = ed.copied(0).pitch;
    EditNote n;
    if (ed.selected(n)) {
      ev.note = true;
      ev.noteTick = n.tick;
      ev.notePitch = n.pitch;
      ev.noteVelocity = n.velocity;
      ev.noteLength = n.length;
    }
  }
  crumb(kScreen);
  if (!gSelectionKnown && (applySavedSelection() || gBootReported)) gSelectionKnown = true;
  // The title stays until the boot load has ended; the first frame then opens on the saved selection.
  if (gSelectionKnown && gBootReported) gDisplay.service(gEngine, gClock, gLastTick, millis(), ev);
  crumb(kIdle);
  checkUsb(millis());
}

#endif  // ENGINE_FIRMWARE
