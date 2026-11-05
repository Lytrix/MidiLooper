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
// 4. Each port's due messages to the wire.
// 5. Serial commands (`!ENGINE ...`, `!STOP_STATUS`, `!REBOOT`) until the surface is wired (E9.8b).
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
#include "EngineFirmware/SdCardIo.h"

using namespace ContentEngine;

namespace {

EXTMEM EnginePsram gPsram;
DMAMEM EngineRam2 gRam2;
EngineRoot gEngine;
EngineClock gClock;
EngineFirmware::SdCardIo gCard;
USBHost gHost;
MIDIDevice_BigBuffer gHostMidi(gHost);

constexpr uint32_t kBlockUs = 2000;       // B_max: the longest non-engine block of a pass (input, USB, serial)
constexpr uint8_t kInputPerPass = 32;     // messages a port a pass
constexpr uint8_t kSendPerPass = 32;      // messages a port a pass
constexpr uint8_t kGrainsPerPass = 8;     // the scheduler's own slack check decides; this bounds one pass
constexpr uint32_t kDefaultTempo = 12000;  // 120 BPM until a tempo is set or an external clock arrives

uint32_t gLastTick = 0;  // the last tick played (input arriving now belongs after it)
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
  n = gEngine.transmit(kPortUsbHost, nowUs, buf, kSendPerPass);
  for (uint8_t i = 0; i < n; ++i)
    gHostMidi.send(buf[i].status & 0xF0, buf[i].data1, buf[i].data2, (buf[i].status & 0x0F) + 1);
}

// ---- Input ----------------------------------------------------------------------------------------------------------
void realtime(uint8_t status, uint32_t nowUs) {
  switch (status) {
    case 0xF8:
      gClock.midiPulse(nowUs);
      break;
    case 0xFA:
      gClock.midiStart(nowUs);
      gEngine.control().transportStart(gClock.nextTick(), nowUs, gClock.usPerTick());
      break;
    case 0xFB:
      gClock.midiContinue(nowUs);
      gEngine.control().transportStart(gClock.nextTick(), nowUs, gClock.usPerTick());
      break;
    case 0xFC:
      gClock.midiStop(nowUs);
      gEngine.control().transportStop(gLastTick, nowUs, gClock.usPerTick());
      break;
    default:
      break;
  }
}

void channelMessage(uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs) {
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
  } else if (std::strcmp(verb, "launch") == 0) {  // launch T S: the track switches to the slot at its loop end
    report(verb, static_cast<uint8_t>(c.queueSwitch(static_cast<uint8_t>(a), static_cast<uint8_t>(b),
                                                    ProductionControl::Quantize::NextGrid, gLastTick)));
  } else if (std::strcmp(verb, "record") == 0) {  // record T S: from the next bar
    c.selectTrack(static_cast<uint8_t>(a));
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
  } else if (std::strcmp(verb, "approve-saving") == 0) {  // the owner's decision, typed by the owner
    report(verb, gEngine.approveSaving() ? 0 : 1);
  } else if (std::strcmp(verb, "status") == 0) {
    Serial.printf("#ENGINE,status,tick=%lu,running=%u,external=%u,us_per_tick=%lu,approved=%u\n",
                  static_cast<unsigned long>(gLastTick), gClock.running(), gClock.external(),
                  static_cast<unsigned long>(us), gEngine.persistence().approved());
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
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  const bool crashed = static_cast<bool>(CrashReport);
  Serial.printf("#CAP,%lu,DIAG,boot_reset,crash=%u\n", static_cast<unsigned long>(micros()), crashed ? 1u : 0u);
  if (crashed) Serial.print(CrashReport);
  if (!gEngine.init(gPsram, gRam2, kBlockUs)) {
    Serial.println("#CAP,BOOT,engine,init_failed");
    return;
  }
  EngineRoot::census([](EngineRoot::Region r, const char* name, size_t bytes) {
    Serial.printf("#CAP,MEM_REGION,%s,%s,%u\n", r == EngineRoot::Region::Psram ? "psram" : "ram2", name,
                  static_cast<unsigned>(bytes));
  });
  gClock.init(kDefaultTempo, kTicksPerBar);
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
  uint32_t now = micros();
  pollInput(now);
  pollSerial(now);
  uint8_t count = 0;
  uint32_t tick;
  while (gClock.due(micros(), tick, count)) {
    gEngine.clockTick(tick, micros(), gClock.usPerTick());
    gLastTick = tick;
    transmitAll(micros());
  }
  now = micros();
  gEngine.submitJobs(gLastTick, now, gClock.usPerTick());
  for (uint8_t g = 0; g < kGrainsPerPass && gEngine.runGrain(micros()); ++g) {
    uint8_t c2 = 0;
    if (gClock.due(micros(), tick, c2)) {  // a tick came due: play it before the next grain
      gEngine.clockTick(tick, micros(), gClock.usPerTick());
      gLastTick = tick;
    }
  }
  transmitAll(micros());
  if (!gBootReported && gCard.ready() && !gEngine.persistence().booting()) {
    gBootReported = true;
    reportBootLoad();
  } else if (!gBootReported && !gCard.ready()) {
    gBootReported = true;
  }
}

#endif  // ENGINE_FIRMWARE
