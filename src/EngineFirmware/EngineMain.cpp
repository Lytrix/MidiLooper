//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine firmware (fixed-memory-engine E9.8a; owner 2026-11-06: boot first, then delete). Built only in the
// `teensy41-engine` env (`ENGINE_FIRMWARE`), which leaves the legacy sources out; the legacy firmware is unchanged.
//
// One main loop, no interrupts of its own:
// 1. Input, bounded a pass: DIN (Serial8, 31,250 baud), USB device and USB host MIDI. Clock, Start, Stop, Continue go
//    to `EngineClock`; channel messages to the engine's input path at the last played tick (input at tick c arrives
//    after c is played).
// 2. Every tick the clock has due, in order (`EngineClock::due`, at most `kCatchUp` a pass): `EngineRoot::clockTick`.
// 3. The jobs (`submitJobs`) and worker grains while the scheduler grants them.
// 4. Each port's due messages to the wire: DIN and USB device. The USB host port is the DROID's: it gets only the
//    surface's LED feedback (as the legacy firmware, which never sends playback there) — the host driver's
//    `write_packed` spins until the DROID takes a packet, so playback bursts there hung the loop. Every host send is
//    admitted first (`hostSend`, `HostTx.h`): dropped when both transmit buffers are full, and the LED stage waits.
// 5. The control surface (E9.8b-1, `Surface`): the DROID control plane (channels 13–16, never recorded nor thru) as
//    the legacy gesture map, and its LEDs (channel 15 notes on every port, as the legacy firmware) paced at one message
//    a millisecond (the DROID host's gap); the DROID's fader bank (Program Change 1 / 0 on channel 16, every port) follows
//    NOTE_EDIT, so its faders send the note-edit messages (E9.8k). Serial commands (`!ENGINE ...`, `!STOP_STATUS`,
//    `!REBOOT`) remain.
// 6. The screen (E9.8c, `EngineDisplay`): a frame every 33 ms when the previous DMA transfer is done; each track shows
//    its selected slot; NOTE_EDIT (E9.8b-2) shows its frozen window, bracket and selected note (start, pitch, velocity,
//    length in the status band, E9.8k).
// Breadcrumbs (`CrashReport.breadcrumb`, kept across a warm reset): the main-loop stage and the last tick, printed at
// the next boot (`#CAP,…,DIAG,prev_crumbs,…`), so a hang can be placed after a reset.
// Boot proof on USB serial: `#CAP,<us>,DIAG,boot_reset,crash=<0|1>`, the memory census, and once the card's slots are
// loaded `#CAP,<us>,BOOT,load,ok,...` with every outcome named. Nothing is written to the card before the owner
// approves saving (`!ENGINE approve-saving`, typed by the owner; remembered by the engine folder).
// Wall-clock time (sets browser slice 1; owner 2026-11-08: real dates only): the Teensy RTC, valid only per
// `wallclock::valid` (never the firmware's build time). The Teensy loader sets the RTC to the computer's **local** time at
// every upload (measured 2026-11-08: one hour ahead of UTC in CET), so the RTC is read as local time and the shown offset
// (minutes, EEPROM bytes 0–3: 'T', 'Z', the offset) defaults to 0. `!ENGINE time` reports it, `!ENGINE time <unix>`
// sets the clock, `!ENGINE tz <minutes>` the offset (`scripts/engine_set_time.py` sets local time and offset 0, as the
// loader does); reported at boot as `#ENGINE,time,...`.

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <USBHost_t36.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <EEPROM.h>

#include "ContentEngine/EngineClock.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/HostTx.h"
#include "ContentEngine/Surface.h"
#include "ContentEngine/WallClock.h"
#include "EngineFirmware/EngineDisplay.h"
#include "EngineFirmware/SdCardIo.h"

using namespace ContentEngine;

namespace {

EXTMEM EnginePsram gPsram;
DMAMEM EngineRam2 gRam2;
EngineRoot gEngine;
EngineClock gClock;
EngineFirmware::SdCardIo gCard;
EngineFirmware::EngineDisplay gDisplay;
Surface<EngineRoot> gSurface;
uint32_t gLastLedUs = 0;
constexpr uint32_t kLedGapUs = 1000;  // the DROID USB host drops packets closer than ~1 ms (legacy MIN_PACKET_GAP)
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
// DIN's transmit buffer beyond the core's 40 bytes: a pass's 32 messages per port, the LEDs, bank and motor feedback
// (3 bytes each) fit, so `Serial8.write` stores and returns instead of waiting for the 31,250-baud line (320 us a byte).
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
// record / overdub run (owner 2026-11-06, "rock stable").
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
  if (Surface<EngineRoot>::controlPlane(status)) {  // the DROID's: never recorded, never thru
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

// ---- Serial commands (until the surface is wired, E9.8b) ---------------------------------------------------------------
char gLine[64];
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
// The global set switch point (DEC-260), kept in EEPROM bytes 4-5 ('W', 0 next bar / 1 loop end).
void loadSwitchAt() {
  if (EEPROM.read(4) != 'W') return;
  gEngine.setSwitchAt(EEPROM.read(5) == 1 ? ProductionControl::SwitchAt::LoopEnd : ProductionControl::SwitchAt::NextBar);
}
void storeSwitchAt(ProductionControl::SwitchAt at) {
  gEngine.setSwitchAt(at);
  EEPROM.update(4, 'W');
  EEPROM.update(5, at == ProductionControl::SwitchAt::LoopEnd ? 1 : 0);
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
/// A completed store write (a slot copy) is stamped with the wall time: `SAVED 14:32` (sets browser slice 1).
void stampSaves() {
  const uint32_t saves = gEngine.persistence().stats().saves;
  if (saves == gSavesSeen) return;
  gSavesSeen = saves;
  gLastSaveWall = wallTime();
}

// ---- The milestone library (DEC-258, sets browser slice 1c) ----------------------------------------------------------
constexpr const char* kLibraryFolder = "/MidiLooper/library";
constexpr const char* kLibraryFile = "/MidiLooper/library/library.bin";
ProductionLibrary::Status gLibraryWas = ProductionLibrary::Status::Idle;
bool gLibraryCheckpoint = false;  // the job running writes the checkpoint (else a milestone)
uint32_t gLibraryStartUs = 0;
// The working set's base milestone (its sequence; 0: none) and set number (DEC-259): a save's parent, a load's
// checkpoint. Held this boot only (keeping them across a reboot is a store format change, a later slice).
uint32_t gBase = 0;
uint16_t gSet = 1;
ProductionSetLoad::Status gLoadWas = ProductionSetLoad::Status::Idle;
// The sets browser (slice 3) and the library's index in memory (4,096 records, PSRAM). `gBrowserOp`: the request it
// made that is under way (its end answers the browser).
EXTMEM ProductionBrowser gBrowser;
enum class BrowserOp : uint8_t { None, Save, Load, Return };
BrowserOp gBrowserOp = BrowserOp::None;
bool gBrowserReady = false;
// Slice 4: the counters at the base milestone (or the boot) — what happened since is the save suggestion's evidence.
struct Evidence {
  uint32_t commits = 0, records = 0, recordsWithOthers = 0, explore = 0;
};
Evidence gEvidenceAt;
Evidence evidenceNow() {
  const auto& c = gEngine.control().stats();
  return Evidence{gEngine.history().commits(), c.recordsStarted, c.recordsWithOthers, c.exploreGestures};
}
/// The working set's base moved (a milestone saved, loaded or returned to): evidence counts from here.
void resetEvidence() { gEvidenceAt = evidenceNow(); }
uint32_t gLoadGrainMaxUs = 0;  // the worst grain while a load or milestone had card work (DEC-260)
uint32_t gLoadPhaseMaxUs[3] = {};  // the worst grain by the load's phase before it: checkpoint / stage, clear, read
uint8_t gSwitchedWas = 0;      // the tracks a live load stopped, as last reported
uint8_t gStagedWas = 0;        // the tracks it switched without a gap, as last reported (DEC-261)

/// The tempo in tenths of a BPM (as LOOP_EDIT shows it).
uint16_t tempoTenthsNow() {
  const uint64_t d = static_cast<uint64_t>(gClock.usPerTick()) * EngineClock::kTicksPerQuarter;
  return static_cast<uint16_t>((600000000ull + d / 2) / d);
}
/// The surface state as it is saved (`saveSelection`).
void surfaceNow(uint8_t* state) {
  state[0] = gEngine.control().selectedTrack();
  for (uint8_t t = 0; t < kTrackCount; ++t) state[1 + t] = gSurface.selectedSlot(t);
}
const char* libraryRefusal(ProductionLibrary::Result r) {
  switch (r) {
    case ProductionLibrary::Result::Ok: return "OK";
    case ProductionLibrary::Result::NotMounted: return "LIBRARY NOT READY";
    case ProductionLibrary::Result::Busy: return "BUSY";
    case ProductionLibrary::Result::IndexFull:
    case ProductionLibrary::Result::LogFull:
    case ProductionLibrary::Result::TooLarge: return "LIBRARY FULL";
    case ProductionLibrary::Result::Failed: return "SAVE FAILED";
  }
  return "?";
}
void reportLibrary(const char* why) {
  auto& l = gEngine.library();
  const library::IndexHeader& h = l.header();
  library::CheckpointHeader cp{};
  const bool hasCheckpoint = l.checkpoint(cp);
  Serial.printf("#ENGINE,library,%s,open=%u,approved=%u,error=%u,sector=%lu,open_ms=%lu,count=%u,append=%lu,"
                "free_sectors=%lu,checkpoint=%lu\n",
                why, l.mounted() ? 1u : 0u, gEngine.persistence().approved() ? 1u : 0u, gCard.libraryError(),
                static_cast<unsigned long>(gCard.librarySector()),
                static_cast<unsigned long>(gCard.libraryOpenUs() / 1000), h.count,
                static_cast<unsigned long>(h.appendSector),
                static_cast<unsigned long>(library::kLogSectors - h.appendSector),
                static_cast<unsigned long>(hasCheckpoint ? cp.sequence : 0));
}
/// Opens the library; creating (preallocating 1 GiB) only while stopped, no take open, saving approved (DEC-258).
/// The index into memory (once, when the library opens): records in card order, so a browser row's record is its card
/// index; one that does not read keeps its place (shown as set 00).
void loadIndex() {
  auto& l = gEngine.library();
  const uint32_t t = micros();
  gBrowser.clear();
  for (uint16_t i = 0; i < l.header().count; ++i) {
    library::Record r{};
    if (!l.readRecord(i, r)) r = library::Record{};
    gBrowser.add(r);
  }
  // The working set until the store keeps it (DEC-259: held for the boot only): the newest milestone's set.
  if (gBase == 0 && gBrowser.count() != 0) gSet = gBrowser.record(static_cast<uint16_t>(gBrowser.count() - 1)).set;
  gBrowserReady = true;
  resetEvidence();
  gSurface.setBrowser(&gBrowser);
  gDisplay.setBrowser(&gBrowser);
  Serial.printf("#ENGINE,index,count=%u,sets=%u,ms=%lu\n", gBrowser.count(), gBrowser.setCount(),
                static_cast<unsigned long>((micros() - t) / 1000));
}
void openLibrary(const char* why) {
  const bool create = gEngine.persistence().approved() && !gClock.running() &&
                      gEngine.transport().writingSlot() == ProductionTransport::kNoSlot;
  if (create && !gCard.exists(kLibraryFolder)) gCard.makeDir(kLibraryFolder);
  bool created = false;
  gEngine.openLibrary(kLibraryFile, create, created);
  reportLibrary(created ? "created" : why);
  if (gEngine.library().mounted()) loadIndex();
}
void listMilestones() {
  auto& l = gEngine.library();
  for (uint16_t i = 0; i < l.header().count; ++i) {
    library::Record r;
    if (!l.readRecord(i, r)) {
      Serial.printf("#ENGINE,ms,i=%u,bad=1\n", i);
      continue;
    }
    char stamp[24];
    wallclock::formatStamp(r.wallTime, gTzOffset, stamp, sizeof stamp);
    Serial.printf("#ENGINE,ms,i=%u,id=SET %02u %c%02u,seq=%lu,parent=%lu,time=%s,tempo=%u.%u,loops=%u,shortest=%lu,"
                  "longest=%lu,sector=%lu,sectors=%lu\n",
                  i, r.set, library::stageLetter(r.stage), r.stageNumber, static_cast<unsigned long>(r.sequence),
                  static_cast<unsigned long>(r.parent), stamp, r.tempoTenths / 10, r.tempoTenths % 10, r.loops,
                  static_cast<unsigned long>(r.shortestLoop), static_cast<unsigned long>(r.longestLoop),
                  static_cast<unsigned long>(r.logSector), static_cast<unsigned long>(r.logSectors));
  }
  reportLibrary("list");
}
/// Starts saving the whole set as milestone `stage` `number` of `set` (DEC-258). nullptr, or why not (nothing written).
const char* beginMilestone(library::Stage stage, uint16_t set, uint8_t number, uint32_t nowUs) {
  auto& l = gEngine.library();
  if (gEngine.transport().writingSlot() != ProductionTransport::kNoSlot) return "STOP THE TAKE FIRST";
  if (!l.mounted()) return "LIBRARY NOT READY";
  if (gEngine.libraryBusy()) return "BUSY";
  ProductionLibrary::Milestone m;
  m.set = set;
  m.stage = stage;
  m.stageNumber = number;
  m.parent = gBase;
  m.wallTime = wallTime();
  m.tempoTenths = tempoTenthsNow();
  surfaceNow(m.surface);
  const ProductionLibrary::Result r = gEngine.saveMilestone(m);
  if (r != ProductionLibrary::Result::Ok) return libraryRefusal(r);
  gLibraryCheckpoint = false;
  gLibraryStartUs = nowUs;
  Serial.printf("#ENGINE,milestone,begin,id=SET %02u %c%02u,sectors=%lu\n", set, library::stageLetter(stage), number,
                static_cast<unsigned long>(l.published().logSectors));
  return nullptr;
}
/// milestone <S|B|E|T|A|F> [set]: the whole set as it is now, numbered next in its stage (the index in memory).
void saveMilestone(const char* arg, uint32_t nowUs) {
  static const char kLetters[] = "SBETAF";
  const char* at = arg != nullptr ? std::strchr(kLetters, arg[0]) : nullptr;
  if (arg == nullptr || arg[0] == 0 || at == nullptr) {
    Serial.println("#ENGINE,milestone,refused,why=STAGE S|B|E|T|A|F");
    return;
  }
  const char* setArg = std::strchr(arg, ' ');
  const uint16_t set = static_cast<uint16_t>(setArg != nullptr ? std::atoi(setArg + 1) : gSet);
  const auto stage = static_cast<library::Stage>(at - kLetters);
  if (const char* why = beginMilestone(stage, set, gBrowser.nextNumber(set, stage), nowUs))
    Serial.printf("#ENGINE,milestone,refused,why=%s\n", why);
}
/// The checkpoint a load writes first: the working set as it is now.
ProductionLibrary::Checkpoint checkpointNow() {
  ProductionLibrary::Checkpoint c;
  c.wallTime = wallTime();
  c.base = gBase;
  c.set = gSet;
  c.tempoTenths = tempoTenthsNow();
  surfaceNow(c.surface);
  return c;
}
/// checkpoint: replaces the recovery checkpoint with the whole set as it is now (diagnostics until loading, DEC-258).
void saveCheckpoint(uint32_t nowUs) {
  if (gEngine.transport().writingSlot() != ProductionTransport::kNoSlot) {
    Serial.println("#ENGINE,checkpoint,refused,why=STOP THE TAKE FIRST");
    return;
  }
  const ProductionLibrary::Result r = gEngine.saveCheckpoint(checkpointNow());
  if (r != ProductionLibrary::Result::Ok) {
    Serial.printf("#ENGINE,checkpoint,refused,why=%s\n", libraryRefusal(r));
    return;
  }
  gLibraryCheckpoint = true;
  gLibraryStartUs = nowUs;
  Serial.println("#ENGINE,checkpoint,begin");
}
/// Starts loading milestone `index` (card order) or, `load` false, returning to the checkpoint; while playing it waits
/// for the switch point (DEC-260 / 261). nullptr, or why not (nothing changed).
const char* beginLoad(bool load, uint16_t index, uint32_t nowUs) {
  if (const char* why = gEngine.loadRefusal()) return why;
  const bool live = gClock.running();
  const ProductionSetLoad::Result r =
      load ? gEngine.loadMilestone(index, checkpointNow(), live) : gEngine.returnToCheckpoint(live);
  static const char* const kWhy[] = {"OK", "LIBRARY NOT READY", "BUSY", "NO SUCH MILESTONE", "NOTHING TO RETURN TO",
                                     "SET TOO LARGE"};
  if (r != ProductionSetLoad::Result::Ok) return kWhy[static_cast<uint8_t>(r)];
  gLibraryStartUs = nowUs;
  gLoadGrainMaxUs = 0;
  gLoadPhaseMaxUs[0] = gLoadPhaseMaxUs[1] = gLoadPhaseMaxUs[2] = 0;
  gLoadWas = ProductionSetLoad::Status::Running;
  Serial.printf("#ENGINE,%s,begin,live=%u,at=%s\n", load ? "load" : "return", live ? 1u : 0u, switchAtName());
  return nullptr;
}
/// load <index>: milestone `index` (0-based, as `milestones` lists it) replaces the set. return: back to the set before
/// the last load.
void startLoad(const char* verb, const char* arg, uint32_t nowUs) {
  const bool load = verb[0] == 'l';
  if (load && arg == nullptr) {
    Serial.println("#ENGINE,load,refused,why=WHICH");
    return;
  }
  if (const char* why = beginLoad(load, load ? static_cast<uint16_t>(std::atoi(arg)) : 0, nowUs))
    Serial.printf("#ENGINE,%s,refused,why=%s\n", verb, why);
}
/// The loaded set's tempo and selection (as the boot restores the saved selection).
void applyLoaded(const library::BlobHeader& b) {
  if (b.tempoTenths != 0 && !gClock.external()) gClock.setTempo(static_cast<uint32_t>(b.tempoTenths) * 10u);
  if (b.surface[0] < kTrackCount) gEngine.control().selectTrack(b.surface[0]);
  for (uint8_t t = 0; t < kTrackCount; ++t)
    if (b.surface[1 + t] / 8 == t) gSurface.selectSlot(t, static_cast<uint8_t>(b.surface[1 + t] % 8));
}
/// A load's or return's end, reported once.
void serviceLoad() {
  // The clock stopped while a live load waited for its switch point: it switches now.
  if (!gClock.running() && gEngine.setLoad().waiting()) gEngine.switchNow(gLastTick, micros(), gClock.usPerTick());
  const uint8_t gapped = gEngine.switchedTracks(), staged = gEngine.stagedTracks();
  if (gapped != gSwitchedWas || staged != gStagedWas) {
    // The switch point: `staged` tracks took the loaded set there without a gap (DEC-261), `gapped` ones stopped; then
    // the staged ones follow the loaded states (`followed`) and the gapped ones resume from the next bar line (`at=`).
    if ((gSwitchedWas | gStagedWas) == 0)
      Serial.printf("#ENGINE,switch,tick=%lu,staged=%02x,gapped=%02x\n", static_cast<unsigned long>(gLastTick), staged,
                    gapped);
    if (gStagedWas != 0 && staged == 0)
      Serial.printf("#ENGINE,followed,tick=%lu,tracks=%02x\n", static_cast<unsigned long>(gLastTick), gStagedWas);
    if (gSwitchedWas != 0 && gapped == 0)
      Serial.printf("#ENGINE,resume,tick=%lu,tracks=%02x,at=%lu\n", static_cast<unsigned long>(gLastTick), gSwitchedWas,
                    static_cast<unsigned long>((gLastTick / kTicksPerBar + 1) * kTicksPerBar));
    gSwitchedWas = gapped;
    gStagedWas = staged;
  }
  const ProductionSetLoad& l = gEngine.setLoad();
  const ProductionSetLoad::Status st = l.status();
  if (st == gLoadWas || st == ProductionSetLoad::Status::Running) return;
  gLoadWas = st;
  const bool load = l.kind() == ProductionSetLoad::Kind::Load;
  const unsigned long ms = static_cast<unsigned long>((micros() - gLibraryStartUs) / 1000);
  const char* verb = load ? "load" : "return";
  switch (st) {
    case ProductionSetLoad::Status::Done:
      applyLoaded(l.blob());
      resetEvidence();
      if (load) {
        gBase = l.record().sequence;
        gSet = l.record().set;
        Serial.printf("#ENGINE,load,done,id=SET %02u %c%02u,seq=%lu,ms=%lu,worst_grain_us=%lu\n", l.record().set,
                      library::stageLetter(l.record().stage), l.record().stageNumber,
                      static_cast<unsigned long>(l.record().sequence), ms, static_cast<unsigned long>(gLoadGrainMaxUs));
      } else {
        gBase = l.checkpoint().base;
        gSet = l.checkpoint().set;
        Serial.printf("#ENGINE,return,done,base=%lu,set=%u,ms=%lu,worst_grain_us=%lu\n", static_cast<unsigned long>(gBase),
                      gSet, ms, static_cast<unsigned long>(gLoadGrainMaxUs));
      }
      break;
    case ProductionSetLoad::Status::Unchanged:
      Serial.printf("#ENGINE,%s,failed,why=CHECKPOINT NOT WRITTEN,NOTHING CHANGED,ms=%lu\n", verb, ms);
      break;
    case ProductionSetLoad::Status::Restored:
      applyLoaded(l.blob());
      Serial.printf("#ENGINE,load,failed,why=SLOT %u UNREADABLE (%u),RESTORED,ms=%lu\n", l.failedSlot(),
                    static_cast<unsigned>(l.failedWhy()), ms);
      break;
    case ProductionSetLoad::Status::Lost:
      Serial.printf("#ENGINE,%s,failed,why=SET NOT RESTORED,ms=%lu\n", verb, ms);
      break;
    default:
      break;
  }
  if (gBrowserOp == BrowserOp::Load || gBrowserOp == BrowserOp::Return) {  // the browser's request (rules 6–7)
    char line[40];
    if (st == ProductionSetLoad::Status::Done) {
      if (load) {
        snprintf(line, sizeof line, "LOADED  SET %02u  %c%02u", l.record().set, library::stageLetter(l.record().stage),
                 l.record().stageNumber);
      } else {
        char at[8];
        wallclock::formatClock(l.checkpoint().wallTime, gTzOffset, at, sizeof at);
        snprintf(line, sizeof line, "RETURNED  SET %02u  %s", l.checkpoint().set, at);
      }
      gBrowser.succeeded();
      gDisplay.toast(line, millis());
    } else {
      const char* what = load ? "LOAD FAILED" : "RETURN FAILED";
      if (st == ProductionSetLoad::Status::Unchanged) gBrowser.failed(what, "CHECKPOINT NOT WRITTEN");
      else if (st == ProductionSetLoad::Status::Restored) gBrowser.failed(what, "CARD READ ERROR");
      else gBrowser.failed(what, "SET NOT RESTORED", true);
    }
    gBrowserOp = BrowserOp::None;
  }
  Serial.printf("#ENGINE,%s,grains_us,checkpoint_stage=%lu,clear=%lu,read=%lu\n", verb,
                static_cast<unsigned long>(gLoadPhaseMaxUs[0]), static_cast<unsigned long>(gLoadPhaseMaxUs[1]),
                static_cast<unsigned long>(gLoadPhaseMaxUs[2]));
  reportLibrary(verb);
}
/// The browser (slice 3): kept current while closed, and what it asks carried out.
void serviceBrowser(uint32_t nowUs) {
  if (!gBrowserReady) return;
  library::CheckpointHeader cp{};
  const bool hasCheckpoint = gEngine.library().checkpoint(cp);
  if (!gBrowser.isOpen()) {
    gBrowser.setWorking(gSet, gBase, hasCheckpoint);
    const Evidence now = evidenceNow(), &at = gEvidenceAt;
    const uint32_t commits = now.commits - at.commits, records = now.records - at.records;
    ProductionBrowser::Activity a;
    a.build = commits != 0;
    a.takesOnly = records != 0 && now.recordsWithOthers - at.recordsWithOthers == records && commits <= records;
    a.explore = now.explore != at.explore;
    gBrowser.setActivity(a);
  }
  EngineFirmware::BrowserInfo info;
  info.tz = gTzOffset;
  info.now = wallTime();
  if (gEngine.persistence().unsaved()) {
    snprintf(info.saved, sizeof info.saved, "UNSAVED");
  } else {
    char at[8];
    wallclock::formatClock(gLastSaveWall, gTzOffset, at, sizeof at);
    snprintf(info.saved, sizeof info.saved, gLastSaveWall != 0 ? "SAVED %s" : "SAVED", at);
  }
  if (hasCheckpoint) {
    char at[8];
    wallclock::formatClock(cp.wallTime, gTzOffset, at, sizeof at);
    snprintf(info.returnTo, sizeof info.returnTo, "TO SET %02u AS LEFT %s", cp.set, at);
  }
  gDisplay.setBrowserInfo(info);
  ProductionBrowser::Request q;
  if (!gBrowser.takeRequest(q)) return;
  const char* why = nullptr;
  switch (q.action) {
    case ProductionBrowser::Action::Save:
      gBrowserOp = BrowserOp::Save;
      if ((why = beginMilestone(q.stage, q.set, q.stageNumber, nowUs)) != nullptr) gBrowser.failed("SAVE FAILED", why);
      break;
    case ProductionBrowser::Action::Load:
      gBrowserOp = BrowserOp::Load;
      if ((why = beginLoad(true, q.record, nowUs)) != nullptr) gBrowser.failed("LOAD FAILED", why);
      break;
    case ProductionBrowser::Action::Return:
      gBrowserOp = BrowserOp::Return;
      if ((why = beginLoad(false, 0, nowUs)) != nullptr) gBrowser.failed("RETURN FAILED", why);
      break;
    default:
      break;
  }
  if (why != nullptr) gBrowserOp = BrowserOp::None;
}
/// A library job's end, reported once.
void serviceLibrary() {
  const ProductionLibrary::Status st = gEngine.library().status();
  if (st == gLibraryWas) return;
  gLibraryWas = st;
  if (gEngine.setLoad().busy()) return;  // the checkpoint a load writes: the load reports
  if (st != ProductionLibrary::Status::Done && st != ProductionLibrary::Status::Failed) return;
  const bool ok = st == ProductionLibrary::Status::Done;
  const unsigned long ms = static_cast<unsigned long>((micros() - gLibraryStartUs) / 1000);
  if (gLibraryCheckpoint) {
    Serial.printf("#ENGINE,checkpoint,%s,ms=%lu\n", ok ? "done" : "failed", ms);
  } else {
    const library::Record& r = gEngine.library().published();
    Serial.printf("#ENGINE,milestone,%s,id=SET %02u %c%02u,seq=%lu,sectors=%lu,ms=%lu\n", ok ? "done" : "failed",
                  r.set, library::stageLetter(r.stage), r.stageNumber, static_cast<unsigned long>(r.sequence),
                  static_cast<unsigned long>(r.logSectors), ms);
    if (ok) {  // published: in the index, and the working set descends from it
      gBrowser.add(r);
      gBase = r.sequence;
      gSet = r.set;
      resetEvidence();
    }
    if (gBrowserOp == BrowserOp::Save) {
      if (ok) {
        char line[40];
        snprintf(line, sizeof line, "SAVED  SET %02u  %c%02u", r.set, library::stageLetter(r.stage), r.stageNumber);
        gBrowser.succeeded();
        gDisplay.toast(line, millis());
      } else {
        gBrowser.failed("SAVE FAILED", "CARD WRITE ERROR");
      }
      gBrowserOp = BrowserOp::None;
    }
  }
  reportLibrary(ok ? "saved" : "failed");
}

void report(const char* what, uint8_t result) {
  Serial.printf("#ENGINE,%s,result=%u,tick=%lu\n", what, result, static_cast<unsigned long>(gLastTick));
}

void command(char* line, uint32_t nowUs) {
  const uint32_t us = gClock.usPerTick();
  if (std::strcmp(line, "!STOP_STATUS") == 0) {
    Serial.printf("#STOP_STATUS,state=%s\n", gClock.running() ? "RUNNING" : "STOPPED");
    return;
  }
  if (std::strcmp(line, "!REBOOT") == 0) {
    if (gClock.running()) return;  // stopped only, as the legacy firmware
    Serial.println("[Engine] host-requested reboot");
    Serial.flush();
    delay(20);
    quietReboot(kRebooting);
  }
  if (std::strncmp(line, "!ENGINE ", 8) != 0) return;
  char* verb = line + 8;
  char* arg = std::strchr(verb, ' ');
  if (arg != nullptr) *arg++ = 0;
  const int a = arg != nullptr ? std::atoi(arg) : 0;
  const char* second = arg != nullptr ? std::strchr(arg, ' ') : nullptr;
  const int b = second != nullptr ? std::atoi(second + 1) : 0;
  auto& c = gEngine.control();
  if (std::strcmp(verb, "start") == 0) {
    gClock.start(nowUs);
    c.transportStart(gClock.nextTick(), nowUs, us);
    report(verb, 0);
  } else if (std::strcmp(verb, "stop") == 0) {
    gClock.stop();
    c.transportStop(gLastTick, nowUs, us);
    report(verb, 0);
  } else if (std::strcmp(verb, "bpm") == 0) {
    gClock.setTempo(static_cast<uint32_t>(a) * 100u);
    report(verb, 0);
  } else if (std::strcmp(verb, "select") == 0) {  // select T: the track the screen and live input follow
    c.selectTrack(static_cast<uint8_t>(a));
    report(verb, 0);
  } else if (std::strcmp(verb, "launch") == 0) {  // launch T S: the track switches to the slot at its loop end
    if (b / 8 == a) gSurface.selectSlot(static_cast<uint8_t>(a), static_cast<uint8_t>(b % 8));
    report(verb, static_cast<uint8_t>(c.queueSwitch(static_cast<uint8_t>(a), static_cast<uint8_t>(b),
                                                    ProductionControl::Quantize::NextGrid, gLastTick)));
  } else if (std::strcmp(verb, "record") == 0) {  // record T S: from the next bar
    c.selectTrack(static_cast<uint8_t>(a));
    if (b / 8 == a) gSurface.selectSlot(static_cast<uint8_t>(a), static_cast<uint8_t>(b % 8));
    report(verb, static_cast<uint8_t>(c.queueRecord(static_cast<uint8_t>(a), static_cast<uint8_t>(b), gLastTick)));
  } else if (std::strcmp(verb, "overdub") == 0) {
    c.selectTrack(static_cast<uint8_t>(a));
    report(verb, static_cast<uint8_t>(c.startOverdub(static_cast<uint8_t>(a), gLastTick)));
  } else if (std::strcmp(verb, "end") == 0) {  // end T: a take ends at the bar
    report(verb, static_cast<uint8_t>(c.queueStop(static_cast<uint8_t>(a), gLastTick)));
  } else if (std::strcmp(verb, "undo") == 0) {
    report(verb, static_cast<uint8_t>(c.undo(static_cast<uint8_t>(a), gLastTick, nowUs, us)));
  } else if (std::strcmp(verb, "redo") == 0) {
    report(verb, static_cast<uint8_t>(c.redo(static_cast<uint8_t>(a), gLastTick, nowUs, us)));
  } else if (std::strcmp(verb, "trace") == 0) {  // trace 1 / 0: control-plane input (diagnostics)
    gTrace = a != 0;
    report(verb, 0);
  } else if (std::strcmp(verb, "fbdump") == 0) {  // the last frame drawn: `scripts/render_display_dump.py`
    gDisplay.dump();
  } else if (std::strcmp(verb, "oled-test") == 0) {  // oled-test 1: blocking white; oled-test 0: DMA stripes
    gDisplay.selfTest(a != 0);
    gDisplay.hold(millis(), 3000);
  } else if (std::strcmp(verb, "oled-clock") == 0 || std::strcmp(verb, "oled-phase") == 0 ||
             std::strcmp(verb, "frame-ms") == 0 || std::strcmp(verb, "oled-flip") == 0) {
    // Panel timing (diagnostics): oled-clock 0xB1; frame-ms 16; oled-flip 1 / 0 (off-screen frames); no arg reports.
    const long v = arg != nullptr ? std::strtol(arg, nullptr, 0) : 0;
    const bool clock = verb[5] == 'c', phase = verb[5] == 'p', flip = verb[5] == 'f';
    gDisplay.tune(clock ? static_cast<uint8_t>(v) : 0, phase ? static_cast<uint8_t>(v) : 0,
                  !clock && !phase && !flip ? static_cast<uint32_t>(v) : 0, flip && arg != nullptr ? static_cast<int>(v) : -1);
  } else if (std::strcmp(verb, "time") == 0) {  // time: report; time <unix UTC>: set the RTC
    if (arg != nullptr) rtc_set(std::strtoul(arg, nullptr, 10));
    reportTime();
  } else if (std::strcmp(verb, "tz") == 0) {  // tz <minutes east of UTC>: the shown offset, kept in EEPROM
    if (arg != nullptr) storeTzOffset(static_cast<int16_t>(a));
    reportTime();
  } else if (std::strcmp(verb, "approve-saving") == 0) {  // the owner's decision, typed by the owner
    report(verb, gEngine.approveSaving() ? 0 : 1);
    if (!gEngine.library().mounted()) openLibrary("approved");  // right after the approval, when stopped (DEC-258)
  } else if (std::strcmp(verb, "library") == 0) {  // the milestone library's state
    reportLibrary("status");
  } else if (std::strcmp(verb, "library-open") == 0) {  // opens it, creating it when stopped and approved
    if (!gEngine.library().mounted()) openLibrary("opened");
    else reportLibrary("status");
  } else if (std::strcmp(verb, "milestone") == 0) {  // milestone <S|B|E|T|A|F> [set]
    saveMilestone(arg, nowUs);
  } else if (std::strcmp(verb, "milestones") == 0) {
    listMilestones();
  } else if (std::strcmp(verb, "checkpoint") == 0) {
    saveCheckpoint(nowUs);
  } else if (std::strcmp(verb, "switch-at") == 0) {  // switch-at bar|loop: where a live load switches (DEC-260)
    if (arg != nullptr && std::strcmp(arg, "bar") == 0) storeSwitchAt(ProductionControl::SwitchAt::NextBar);
    if (arg != nullptr && std::strcmp(arg, "loop") == 0) storeSwitchAt(ProductionControl::SwitchAt::LoopEnd);
    Serial.printf("#ENGINE,switch-at,%s\n", switchAtName());
  } else if (std::strcmp(verb, "dump-slot") == 0) {  // dump-slot S: the slot's current state, its runs and rows
    const ManifestId m = gEngine.history().current(static_cast<uint8_t>(a));
    if (m == kNoManifest) {
      Serial.println("#ENGINE,dump,none");
    } else {
      const auto& mf = gEngine.manifests().manifest(m);
      Serial.printf("#ENGINE,dump,slot=%d,identity=%lu,loop=%lu,runs=%u\n", a, static_cast<unsigned long>(mf.identity),
                    static_cast<unsigned long>(mf.loopLengthTicks), mf.runCount);
      for (uint8_t i = 0; i < mf.runCount; ++i) {
        const auto& r = gEngine.runs().run(mf.runs[i]);
        Serial.printf("#ENGINE,dump,run=%u,id=%u,kind=%u,rows=%lu\n", i, mf.runs[i], static_cast<unsigned>(r.kind),
                      static_cast<unsigned long>(r.rowCount));
        for (uint32_t k = 0; k < r.rowCount; ++k) {
          const PassEvent& e = gEngine.runs().rowAt(mf.runs[i], k, gEngine.pool());
          Serial.printf("#R,%u,%lu,%lu,%lu,%u,%u,%u,%u\n", i, static_cast<unsigned long>(e.tick),
                        static_cast<unsigned long>(e.noteId), static_cast<unsigned long>(e.seq), e.status, e.data1,
                        e.data2, e.origin);
        }
      }
      Serial.println("#ENGINE,dump,end");
    }
  } else if (std::strcmp(verb, "switch-stage") == 0) {  // switch-stage 0|1: staged (gapless) live switches (DEC-261)
    if (arg != nullptr) gEngine.setStaging(a != 0);
    Serial.printf("#ENGINE,switch-stage,%u\n", gEngine.staging() ? 1u : 0u);
  } else if (std::strcmp(verb, "load") == 0 || std::strcmp(verb, "return") == 0) {
    startLoad(verb, arg, nowUs);
  } else if (std::strcmp(verb, "take") == 0) {  // the take and its overlay (diagnostics)
    auto& x = gEngine.transport();
    const uint8_t w = x.writingSlot();
    const ManifestId m = w == ProductionTransport::kNoSlot ? kNoManifest : x.contentOf(w);
    const ManifestId h = w == ProductionTransport::kNoSlot ? kNoManifest : gEngine.history().current(w);
    Serial.printf("#ENGINE,clock,timeout_age_us=%lu\n", static_cast<unsigned long>(gClock.timeoutAgeUs()));
    // Worst stage times (µs): input, serial, ticks, jobs, grains, send, surface, leds, screen; saves so far.
    Serial.printf("#ENGINE,stages,input=%lu,serial=%lu,ticks=%lu,jobs=%lu,grains=%lu,send=%lu,surface=%lu,leds=%lu,"
                  "screen=%lu,saves=%lu,save_failed=%lu,heartbeats=%lu,host_dropped=%lu,host_deferred=%lu,host_pipe=%u\n",
                  static_cast<unsigned long>(gStageMaxUs[kInput]), static_cast<unsigned long>(gStageMaxUs[kSerial]),
                  static_cast<unsigned long>(gStageMaxUs[kTicks]), static_cast<unsigned long>(gStageMaxUs[kJobs]),
                  static_cast<unsigned long>(gStageMaxUs[kGrains]), static_cast<unsigned long>(gStageMaxUs[kSend]),
                  static_cast<unsigned long>(gStageMaxUs[kSurface]), static_cast<unsigned long>(gStageMaxUs[kLeds]),
                  static_cast<unsigned long>(gStageMaxUs[kScreen]),
                  static_cast<unsigned long>(gEngine.persistence().stats().saves),
                  static_cast<unsigned long>(gEngine.persistence().stats().saveFailed),
                  static_cast<unsigned long>(gHeartbeats), static_cast<unsigned long>(gHostDropped),
                  static_cast<unsigned long>(gHostDeferred), gHostMidi.*field(TxPipe{}) != nullptr ? 1u : 0u);
    for (uint32_t& m : gStageMaxUs) m = 0;
    const auto& ct = gCard.times();  // the card operations' worst times (µs)
    Serial.printf("#ENGINE,card,exists=%lu,open_read=%lu,sector_read=%lu,sector_write=%lu,store_open=%lu,store=%u\n",
                  static_cast<unsigned long>(ct.exists), static_cast<unsigned long>(ct.openRead),
                  static_cast<unsigned long>(ct.sectorRead), static_cast<unsigned long>(ct.sectorWrite),
                  static_cast<unsigned long>(ct.store), gEngine.persistence().stats().store);
    Serial.printf("#ENGINE,card_write,next=%lu,jump=%lu,not_ready=%lu,stops=%lu\n",
                  static_cast<unsigned long>(ct.nextWrite), static_cast<unsigned long>(ct.jumpWrite),
                  static_cast<unsigned long>(ct.notReady), static_cast<unsigned long>(ct.stops));
    Serial.printf("#ENGINE,store,error=%u,first_sector=%lu,fat_type=%u,cluster_sectors=%u,clusters=%lu\n",
                  gCard.storeError(), static_cast<unsigned long>(gCard.storeSector()), SD.sdfs.fatType(),
                  static_cast<unsigned>(SD.sdfs.sectorsPerCluster()), static_cast<unsigned long>(SD.sdfs.clusterCount()));
    gCard.clearTimes();
    Serial.printf("#ENGINE,take,pass_max_us=%lu,pulses=%lu,timeouts=%lu,running=%u,external=%u,us_per_tick=%lu,tick=%lu\n",
                  static_cast<unsigned long>(gPassMaxUs), static_cast<unsigned long>(gPulses),
                  static_cast<unsigned long>(gTimeouts), gClock.running(), gClock.external(),
                  static_cast<unsigned long>(gClock.usPerTick()), static_cast<unsigned long>(gLastTick));
    gPassMaxUs = 0;
    for (uint8_t i = 0; i < gGapCount; ++i)
      Serial.printf("#ENGINE,gap,at_ms=%lu,gap_us=%lu,passes=%lu,usb_in=%lu,usb_out=%lu,out_burst=%lu\n",
                    static_cast<unsigned long>(gGaps[i].atMs), static_cast<unsigned long>(gGaps[i].gapUs),
                    static_cast<unsigned long>(gGaps[i].passes), static_cast<unsigned long>(gGaps[i].usbIn),
                    static_cast<unsigned long>(gGaps[i].usbOut), static_cast<unsigned long>(gGaps[i].outBurst));
    gGapCount = 0;
    Serial.printf("#ENGINE,take,slot=%u,mode=%u,pos=%lu,content_loop=%lu,history=%u,overlay=%u,dropped=%lu,frames=%lu,"
                  "focus=%u\n",
                  w, static_cast<unsigned>(x.mode()), static_cast<unsigned long>(x.writePosition(gLastTick)),
                  static_cast<unsigned long>(m == kNoManifest ? 0 : gEngine.manifests().manifest(m).loopLengthTicks),
                  h == kNoManifest ? 0u : 1u, gEngine.overlay().count(),
                  static_cast<unsigned long>(gEngine.overlay().dropped()), static_cast<unsigned long>(gDisplay.frames()),
                  gEngine.tiles().detailSlot(0));
  } else if (std::strcmp(verb, "history") == 0) {  // history pressure (E4.7, diagnostics; changes nothing)
    const auto& h = gEngine.history();
    const auto& hs = h.stats();
    Serial.printf("#ENGINE,history,held=%u,high_water=%u,depth=%u,evicted_undo=%lu,evicted_redo=%lu,dropped_redo=%lu,"
                  "collapsed=%lu,undo_past_retained=%lu,last_lost_slot=%d,last_past_slot=%d\n",
                  h.heldStates(), hs.highWater, ContentEngine::ProductionHistory::kDepth,
                  static_cast<unsigned long>(hs.evictedUndo), static_cast<unsigned long>(hs.evictedRedo),
                  static_cast<unsigned long>(hs.droppedRedo), static_cast<unsigned long>(hs.collapsedWraps),
                  static_cast<unsigned long>(hs.undoPastRetained),
                  hs.lastLostSlot == ContentEngine::ProductionHistory::kNone ? -1 : hs.lastLostSlot,
                  hs.lastPastSlot == ContentEngine::ProductionHistory::kNone ? -1 : hs.lastPastSlot);
    // Per populated slot (content now, or steps held, or steps lost): slot:undo/redo/lost; then how the retained steps
    // (undo + redo) spread over those slots.
    static constexpr uint8_t kBuckets = 7;  // 0, 1, 2, 3-4, 5-8, 9-16, 17+
    uint16_t spread[kBuckets] = {};
    uint8_t populated = 0;
    Serial.print("#ENGINE,history_slots");
    for (uint8_t s = 0; s < ContentEngine::Config::kSlotCount; ++s) {
      const uint8_t u = h.undoDepth(s), r = h.redoDepth(s);
      const uint16_t lost = h.lostSteps(s);
      if (h.current(s) == ContentEngine::kNoManifest && u == 0 && r == 0 && lost == 0) continue;
      ++populated;
      Serial.printf(",%u:%u/%u/%u", s, u, r, lost);
      const uint8_t n = u + r;
      ++spread[n == 0 ? 0 : n == 1 ? 1 : n == 2 ? 2 : n <= 4 ? 3 : n <= 8 ? 4 : n <= 16 ? 5 : 6];
    }
    Serial.println();
    Serial.printf("#ENGINE,history_spread,populated=%u,steps_0=%u,steps_1=%u,steps_2=%u,steps_3_4=%u,steps_5_8=%u,"
                  "steps_9_16=%u,steps_17_up=%u\n",
                  populated, spread[0], spread[1], spread[2], spread[3], spread[4], spread[5], spread[6]);
  } else if (std::strcmp(verb, "readbench") == 0) {  // readbench N K: store read speed (read only; blocks ~N ms)
    // N sectors in a row from the store's slot area, K a "grain" (one call each, then `readSectors(K)`), then 64
    // single sectors scattered across the store: the cost of fetching history from the card (diagnostics).
    DMAMEM static uint8_t buf[512 * 16];
    const uint32_t n = a > 0 ? static_cast<uint32_t>(a) : 1024, k = b > 0 && b <= 16 ? static_cast<uint32_t>(b) : 8;
    const uint32_t base = gCard.storeSector();
    if (base == 0) {
      Serial.println("#ENGINE,readbench,no_store");
      return;
    }
    gCard.quiesce();
    SdCard* card = SD.sdfs.card();
    for (uint8_t mode = 0; mode < 2; ++mode) {
      uint32_t first = 0, worstGrain = 0, failed = 0;
      const uint32_t t0 = micros();
      for (uint32_t i = 0; i < n; i += k) {
        const uint32_t g = micros();
        if (mode == 0) {
          for (uint32_t j = 0; j < k; ++j) failed += !card->readSector(base + 2 + i + j, buf + 512 * j);
        } else {
          failed += !card->readSectors(base + 2 + i, buf, k);
        }
        const uint32_t d = micros() - g;
        if (i == 0) first = d; else if (d > worstGrain) worstGrain = d;
      }
      const uint32_t total = micros() - t0;
      card->syncDevice();
      Serial.printf("#ENGINE,readbench,mode=%s,sectors=%lu,per_grain=%lu,first_grain_us=%lu,worst_grain_us=%lu,"
                    "total_us=%lu,kb_per_s=%lu,failed=%lu\n",
                    mode == 0 ? "one_by_one" : "multi", static_cast<unsigned long>(n), static_cast<unsigned long>(k),
                    static_cast<unsigned long>(first), static_cast<unsigned long>(worstGrain),
                    static_cast<unsigned long>(total), static_cast<unsigned long>(total ? n * 512000ull / total : 0),
                    static_cast<unsigned long>(failed));
    }
    uint32_t worst = 0, sum = 0, failed = 0, x = 12345;
    for (uint8_t i = 0; i < 64; ++i) {
      x = x * 1103515245u + 12345u;
      const uint32_t s = base + 2 + (x >> 8) % (ContentEngine::ProductionPersistence::kStoreSectors - 2);
      const uint32_t g = micros();
      failed += !card->readSector(s, buf);
      card->syncDevice();
      const uint32_t d = micros() - g;
      sum += d;
      if (d > worst) worst = d;
    }
    Serial.printf("#ENGINE,readbench,mode=scattered,reads=64,avg_us=%lu,worst_us=%lu,failed=%lu\n",
                  static_cast<unsigned long>(sum / 64), static_cast<unsigned long>(worst),
                  static_cast<unsigned long>(failed));
  } else if (std::strcmp(verb, "cardfree") == 0) {  // the card's free space (scans the FAT: ~1 s, read only)
    gCard.quiesce();  // SdFat reads the FAT: the store's stream is stopped first
    const uint32_t t = millis();
    const int32_t free = SD.sdfs.freeClusterCount();
    Serial.printf("#ENGINE,cardfree,free_clusters=%ld,cluster_sectors=%u,clusters=%lu,ms=%lu\n",
                  static_cast<long>(free), static_cast<unsigned>(SD.sdfs.sectorsPerCluster()),
                  static_cast<unsigned long>(SD.sdfs.clusterCount()), static_cast<unsigned long>(millis() - t));
  } else if (std::strcmp(verb, "boot") == 0) {  // the boot's reset cause and the previous boot's breadcrumbs
    reportBootReset();
  } else if (std::strcmp(verb, "hang") == 0) {  // hang [irq]: the watchdog's test — the loop stops (stopped only)
    if (gClock.running()) return;
    const bool irq = arg != nullptr && std::strcmp(arg, "irq") == 0;
    Serial.printf("#ENGINE,hang,irq=%u,watchdog_s=%lu\n", irq ? 1u : 0u, static_cast<unsigned long>(kWatchdogS));
    Serial.flush();
    if (irq) __disable_irq();  // as a hang with interrupts off: USB is dead too, only the watchdog resets the board
    for (;;) {
    }
  } else if (std::strcmp(verb, "status") == 0) {
    EditNote n{};
    const bool note = gSurface.edit().selected(n);
    Serial.printf("#ENGINE,status,tick=%lu,running=%u,external=%u,us_per_tick=%lu,approved=%u,frames=%lu,"
                  "frame_draw_worst_us=%lu,edit=%u,edit_window=%lu+%lu,bracket=%lu,note=%d,pitch=%u,len=%lu\n",
                  static_cast<unsigned long>(gLastTick), gClock.running(), gClock.external(),
                  static_cast<unsigned long>(us), gEngine.persistence().approved(),
                  static_cast<unsigned long>(gDisplay.frames()), static_cast<unsigned long>(gDisplay.worstDrawUs()),
                  gSurface.edit().editing() ? 2u : gSurface.edit().entering() ? 1u : 0u,
                  static_cast<unsigned long>(gSurface.edit().windowFirst()),
                  static_cast<unsigned long>(gSurface.edit().windowTicks()),
                  static_cast<unsigned long>(gSurface.edit().bracket()), note ? static_cast<int>(n.tick) : -1,
                  note ? n.pitch : 0u, note ? static_cast<unsigned long>(n.length) : 0ul);
  }
}

void pollSerial(uint32_t nowUs) {
  for (uint8_t i = 0; i < 64 && Serial.available(); ++i) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      gLine[gLineLength] = 0;
      if (gLineLength != 0) command(gLine, nowUs);
      gLineLength = 0;
    } else if (gLineLength + 1 < sizeof gLine) {
      gLine[gLineLength++] = ch;
    }
  }
}

void selectFirstSlots();
// After boot load: the saved selection (selected track, each track's slot), else each track's first slot with content
// (slots T x 8 .. T x 8 + 7).
void restoreSelection() {
  uint8_t state[ProductionPersistence::kStateBytes];
  if (!gEngine.persistence().restoredSurfaceState(state) || state[0] >= kTrackCount) {
    selectFirstSlots();
    return;
  }
  gEngine.control().selectTrack(state[0]);
  for (uint8_t t = 0; t < kTrackCount; ++t)
    if (state[1 + t] / 8 == t) gSurface.selectSlot(t, static_cast<uint8_t>(state[1 + t] % 8));
}
/// The selection as the surface has it now, for saving (selected track, then each track's engine slot).
void saveSelection(uint32_t nowUs) {
  uint8_t state[ProductionPersistence::kStateBytes];
  state[0] = gEngine.control().selectedTrack();
  for (uint8_t t = 0; t < kTrackCount; ++t) state[1 + t] = gSurface.selectedSlot(t);
  gEngine.setSurfaceState(state, nowUs);
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
  const uint8_t status = static_cast<uint8_t>(0xC0 | Surface<EngineRoot>::kProgramChannel);
  sendDin(status, program, 0);
  usbMIDI.sendProgramChange(program, Surface<EngineRoot>::kProgramChannel + 1, 0);
  usbMIDI.send_now();
  gLastUsbTxUs = micros();
  ++gUsbOut;
  ++gOutThisPass;
  hostSend(0xC0, program, 0, Surface<EngineRoot>::kProgramChannel + 1);
}

/// A motor-fader message (NOTE_EDIT feedback) on every port, as the LEDs.
void sendMotor(const EditSurface<EngineRoot>::MotorMsg& m) {
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
  const uint8_t status = static_cast<uint8_t>((velocity != 0 ? 0x90 : 0x80) | Surface<EngineRoot>::kLedChannel);
  sendDin(status, note, velocity);
  usbMIDI.send(status & 0xF0, note, velocity, Surface<EngineRoot>::kLedChannel + 1, 0);
  usbMIDI.send_now();
  gLastUsbTxUs = micros();
  ++gUsbOut;
  ++gOutThisPass;
  hostSend(status & 0xF0, note, velocity, Surface<EngineRoot>::kLedChannel + 1);
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
/// time after the write — the loop used to run on, its card, USB and PSRAM traffic cut off wherever the reset landed);
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

// The USB device's health (the watchdog cannot see it: the loop runs on). Wedged twice on 2026-11-09 during reboot
// soaks: the loop and the surface ran, but the host's open of the serial port and its writes blocked and the uploader
// could not reboot the board (the 2026-11-08/09 incidents). Every `kUsbCheckMs` the live USB state goes to breadcrumbs
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
/// where a hang stopped it. A stage with phase < kPhaseLoop is the boot before that one's (the hang came before the loop).
/// c4–c6: after a watchdog reset where it hung — PC, LR, exception number (0: the main loop; 16 + IRQ: a handler), or
/// the USB state if interrupts were off (the warning never ran); otherwise the last USB state (`usbState`), the
/// wedge's with stage 12 (`kUsbWedged`).
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

/// One worker grain; its time counts towards a load's worst grain while the library has card work (DEC-260).
bool timedGrain() {
  const bool working = gEngine.libraryWorking();
  const uint8_t phase = gEngine.setLoad().phase() >= 4 ? 2 : gEngine.setLoad().phase() >= 2 ? 1 : 0;
  const uint32_t t = micros();
  const bool ran = gEngine.runGrain(t);
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

void setup() {
  phase(kPhaseSetup);
  gResetCause = SRC_SRSR;
  SRC_SRSR = gResetCause;  // write-one-to-clear: the next boot reports its own cause
  attachInterruptVector(IRQ_WDOG1, watchdogWarningIsr);
  NVIC_SET_PRIORITY(IRQ_WDOG1, 0);
  WDOG1_WICR = kWicrWie | kWicrWtis | 1;  // the warning 0.5 s before the reset
  NVIC_ENABLE_IRQ(IRQ_WDOG1);
  attachInterruptVector(IRQ_USB1, usbIsrCounted);
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
  // Each track plays on its own MIDI channel, track n on channel n (legacy `TrackManager` default), whatever channel a
  // note was recorded on; live thru follows the selected track's channel.
  for (uint8_t t = 0; t < kTrackCount; ++t) gEngine.output().setChannel(t, static_cast<uint8_t>(t + 1));
  gClock.init(kDefaultTempo, kTicksPerBar);
  loadSwitchAt();
  gSurface.init(gEngine, gClock);
  gDisplay.begin();
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
    openLibrary("boot");  // created here only once saving is approved (boot is stopped, DEC-258)
  } else if (!gBootReported && !gCard.ready()) {
    gBootReported = true;
  }
  crumb(kSurface);
  gSurface.service(millis(), gLastTick, micros());
  if (gBootReported) saveSelection(micros());  // saved once settled, after approval
  stampSaves();
  serviceLibrary();
  serviceLoad();
  serviceBrowser(micros());
  crumb(kLeds);
  uint8_t bank;
  if (gSurface.bank(bank)) sendBank(bank);  // the DROID's fader bank follows NOTE_EDIT
  EditSurface<EngineRoot>::MotorMsg motor;
  if (gSurface.motor(millis(), motor)) sendMotor(motor);  // the DROID's motor faders follow the selection
  if (micros() - gLastLedUs >= kLedGapUs) {
    if (!hostTxRoom()) ++gHostDeferred;  // the DROID is not taking packets: the surface keeps its LEDs until it does
    else if (gSurface.leds(gLastTick, millis(), sendLed, 1) != 0) gLastLedUs = micros();
  }
  for (uint8_t t = 0; t < kTrackCount; ++t) gDisplay.show(t, gSurface.selectedSlot(t));
  EngineFirmware::EditView ev;
  const auto& ed = gSurface.edit();
  ev.loop = gSurface.loopEdit();
  ev.loopTicks = gSurface.pendingLoopTicks();
  ev.loopStart = gSurface.loopStart();
  ev.popup = gSurface.popup();
  ev.popupSlot = gSurface.popupSlot();
  ev.popupLoop = gSurface.popupLoop();
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
  gDisplay.service(gEngine, gClock, gLastTick, millis(), ev);
  crumb(kIdle);
  checkUsb(millis());
}

#endif  // ENGINE_FIRMWARE
