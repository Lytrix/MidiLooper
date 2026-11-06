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
//    `write_packed` spins until the DROID takes a packet, so playback bursts there hung the loop.
// 5. The control surface (E9.8b-1, `Surface`): the DROID control plane (channels 13–16, never recorded nor thru) as
//    the legacy gesture map, and its LEDs (channel 15 notes on every port, as the legacy firmware) paced at one message
//    a millisecond (the DROID host's gap). Serial commands (`!ENGINE ...`, `!STOP_STATUS`, `!REBOOT`) remain.
// 6. The screen (E9.8c, `EngineDisplay`): a frame every 33 ms when the previous DMA transfer is done; each track shows
//    its selected slot; NOTE_EDIT (E9.8b-2) shows its frozen window, bracket and selected note.
// Breadcrumbs (`CrashReport.breadcrumb`, kept across a warm reset): the main-loop stage and the last tick, printed at
// the next boot (`#CAP,…,DIAG,prev_crumbs,…`), so a hang can be placed after a reset.
// Boot proof on USB serial: `#CAP,<us>,DIAG,boot_reset,crash=<0|1>`, the memory census, and once the card's slots are
// loaded `#CAP,<us>,BOOT,load,ok,...` with every outcome named. Nothing is written to the card before the owner
// approves saving (`!ENGINE approve-saving`, typed by the owner; remembered by the engine folder).

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <USBHost_t36.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ContentEngine/EngineClock.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/Surface.h"
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

constexpr uint32_t kBlockUs = 2000;       // B_max: the longest non-engine block of a pass (input, USB, serial)
constexpr uint8_t kInputPerPass = 32;     // messages a port a pass
constexpr uint8_t kSendPerPass = 32;      // messages a port a pass
constexpr uint8_t kGrainsPerPass = 8;     // the scheduler's own slack check decides; this bounds one pass
constexpr uint32_t kDefaultTempo = 12000;  // 120 BPM until a tempo is set or an external clock arrives

uint32_t gLastTick = 0;
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
  if (n != 0) usbMIDI.send_now();
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

void channelMessage(uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs) {
  if (Surface<EngineRoot>::controlPlane(status)) {  // the DROID's: never recorded, never thru
    gSurface.input(status, d1, d2, millis());
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
    SCB_AIRCR = 0x05FA0004;
    return;
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
  } else if (std::strcmp(verb, "oled-test") == 0) {  // oled-test 1: blocking white; oled-test 0: DMA stripes
    gDisplay.selfTest(a != 0);
    gDisplay.hold(millis(), 3000);
  } else if (std::strcmp(verb, "approve-saving") == 0) {  // the owner's decision, typed by the owner
    report(verb, gEngine.approveSaving() ? 0 : 1);
  } else if (std::strcmp(verb, "take") == 0) {  // the take and its overlay (diagnostics)
    auto& x = gEngine.transport();
    const uint8_t w = x.writingSlot();
    const ManifestId m = w == ProductionTransport::kNoSlot ? kNoManifest : x.contentOf(w);
    const ManifestId h = w == ProductionTransport::kNoSlot ? kNoManifest : gEngine.history().current(w);
    Serial.printf("#ENGINE,clock,timeout_age_us=%lu\n", static_cast<unsigned long>(gClock.timeoutAgeUs()));
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

// After boot load: each track selects its first slot with content (slots T x 8 .. T x 8 + 7).
void selectFirstSlots() {
  for (uint8_t t = 0; t < kTrackCount; ++t)
    for (uint8_t s = 0; s < 8; ++s)
      if (gEngine.transport().contentOf(static_cast<uint8_t>(t * 8 + s)) != kNoManifest) {
        gSurface.selectSlot(t, s);
        break;
      }
}

void sendLed(uint8_t note, uint8_t velocity) {
  const uint8_t status = static_cast<uint8_t>((velocity != 0 ? 0x90 : 0x80) | Surface<EngineRoot>::kLedChannel);
  sendDin(status, note, velocity);
  usbMIDI.send(status & 0xF0, note, velocity, Surface<EngineRoot>::kLedChannel + 1, 0);
  usbMIDI.send_now();
  ++gUsbOut;
  ++gOutThisPass;
  gHostMidi.send(status & 0xF0, note, velocity, Surface<EngineRoot>::kLedChannel + 1);
}

// ---- Breadcrumbs -----------------------------------------------------------------------------------------------------
enum Stage : uint32_t { kInput = 1, kSerial, kTicks, kJobs, kGrains, kSend, kSurface, kLeds, kScreen, kIdle };
DMAMEM uint32_t gPrevCrumbs[8];
void crumb(uint32_t stage) {
  CrashReport.breadcrumb(1, stage);
  CrashReport.breadcrumb(2, gLastTick);
}

void reportBootLoad() {
  const auto& st = gEngine.persistence().stats();
  Serial.printf("#CAP,%lu,BOOT,load,ok,card=%u,approved=%u,loads=%lu,load_failed_mask=%llx,v1_imported=%lu,"
                "v1_skipped_mask=%llx,v1_edited_mask=%llx,v1_failed_mask=%llx,held_whole=%lu,us=%lu\n",
                static_cast<unsigned long>(micros()), gCard.ready(), gEngine.persistence().approved(),
                static_cast<unsigned long>(st.loads), static_cast<unsigned long long>(st.loadFailedMask),
                static_cast<unsigned long>(st.v1Imported), static_cast<unsigned long long>(st.v1SkippedMask),
                static_cast<unsigned long long>(st.v1EditedMask), static_cast<unsigned long long>(st.v1FailedMask),
                static_cast<unsigned long>(st.v1HeldWhole), static_cast<unsigned long>(micros() - gBootStartUs));
}

}  // namespace

void setup() {
  // The previous boot's breadcrumbs, before this boot writes any: the struct (end of RAM2) survives a warm reset.
  {
    volatile uint32_t* bc = reinterpret_cast<volatile uint32_t*>(0x2027FFC0);
    for (int i = 0; i < 8; ++i) gPrevCrumbs[i] = bc[i];
    for (int i = 0; i < 7; ++i) bc[i] = 0;  // this boot's crumbs start clean (the mask too)
  }
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  const bool crashed = static_cast<bool>(CrashReport);
  Serial.printf("#CAP,%lu,DIAG,boot_reset,crash=%u\n", static_cast<unsigned long>(micros()), crashed ? 1u : 0u);
  // The previous boot's last main-loop stage and tick (bitmask, stage, tick): where a hang stopped it.
  Serial.printf("#CAP,%lu,DIAG,prev_crumbs,mask=%lx,stage=%lu,tick=%lu\n", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(gPrevCrumbs[0]), static_cast<unsigned long>(gPrevCrumbs[1]),
                static_cast<unsigned long>(gPrevCrumbs[2]));
  if (crashed) Serial.print(CrashReport);
  if (!gEngine.init(gPsram, gRam2, kBlockUs)) {
    Serial.println("#CAP,BOOT,engine,init_failed");
    return;
  }
  EngineRoot::census([](EngineRoot::Region r, const char* name, size_t bytes) {
    Serial.printf("#CAP,MEM_REGION,%s,%s,%u\n", r == EngineRoot::Region::Psram ? "psram" : "ram2", name,
                  static_cast<unsigned>(bytes));
  });
  // Each track plays on its own MIDI channel, track n on channel n (legacy `TrackManager` default), whatever channel a
  // note was recorded on; live thru follows the selected track's channel.
  for (uint8_t t = 0; t < kTrackCount; ++t) gEngine.output().setChannel(t, static_cast<uint8_t>(t + 1));
  gClock.init(kDefaultTempo, kTicksPerBar);
  gSurface.init(gEngine, gClock);
  gDisplay.begin();
  Serial8.begin(31250);
  gHost.begin();
  gBootStartUs = micros();
  if (gCard.begin()) {
    gEngine.attachCard(gCard);
    gEngine.startBootLoad();
  } else {
    Serial.println("#CAP,BOOT,card,absent");
  }
}

void loop() {
  static uint32_t passStart = 0;
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
  for (uint8_t g = 0; g < kGrainsPerPass && gEngine.runGrain(micros()); ++g) {
    uint8_t c2 = 0;
    if (gClock.due(micros(), tick, c2)) {  // a tick came due: play it before the next grain
      gEngine.clockTick(tick, micros(), gClock.usPerTick());
      gLastTick = tick;
    }
  }
  crumb(kSend);
  transmitAll(micros());
  if (!gBootReported && gCard.ready() && !gEngine.persistence().booting()) {
    gBootReported = true;
    reportBootLoad();
    selectFirstSlots();
  } else if (!gBootReported && !gCard.ready()) {
    gBootReported = true;
  }
  crumb(kSurface);
  gSurface.service(millis(), gLastTick, micros());
  crumb(kLeds);
  if (micros() - gLastLedUs >= kLedGapUs && gSurface.leds(gLastTick, millis(), sendLed, 1) != 0) gLastLedUs = micros();
  for (uint8_t t = 0; t < kTrackCount; ++t) gDisplay.show(t, gSurface.selectedSlot(t));
  EngineFirmware::EditView ev;
  const auto& ed = gSurface.edit();
  if (ed.active()) {
    ev.active = true;
    ev.ready = ed.editing();
    ev.slot = ed.slot();
    ev.first = ed.windowFirst();
    ev.ticks = ed.windowTicks();
    ev.bracket = ed.bracket();
    ev.length = ed.lengthMode();
    EditNote n;
    if (ed.selected(n)) {
      ev.note = true;
      ev.noteTick = n.tick;
      ev.notePitch = n.pitch;
    }
  }
  crumb(kScreen);
  gDisplay.service(gEngine, gClock, gLastTick, millis(), ev);
  crumb(kIdle);
}

#endif  // ENGINE_FIRMWARE
