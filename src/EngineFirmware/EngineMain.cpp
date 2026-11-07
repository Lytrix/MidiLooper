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
enum Stage : uint32_t { kInput = 1, kSerial, kTicks, kJobs, kGrains, kSend, kSurface, kLeds, kScreen, kIdle };
// Diagnostics: the worst time each main-loop stage took since the last `!ENGINE take` (the time between two crumbs is
// the earlier stage's).
uint32_t gStageMaxUs[kIdle + 1] = {};
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
  } else if (std::strcmp(verb, "trace") == 0) {  // trace 1 / 0: control-plane input (diagnostics)
    gTrace = a != 0;
    report(verb, 0);
  } else if (std::strcmp(verb, "fbdump") == 0) {  // the last frame drawn: `scripts/render_display_dump.py`
    gDisplay.dump();
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
    // Worst stage times (µs): input, serial, ticks, jobs, grains, send, surface, leds, screen; saves so far.
    Serial.printf("#ENGINE,stages,input=%lu,serial=%lu,ticks=%lu,jobs=%lu,grains=%lu,send=%lu,surface=%lu,leds=%lu,"
                  "screen=%lu,saves=%lu,save_failed=%lu,heartbeats=%lu\n",
                  static_cast<unsigned long>(gStageMaxUs[kInput]), static_cast<unsigned long>(gStageMaxUs[kSerial]),
                  static_cast<unsigned long>(gStageMaxUs[kTicks]), static_cast<unsigned long>(gStageMaxUs[kJobs]),
                  static_cast<unsigned long>(gStageMaxUs[kGrains]), static_cast<unsigned long>(gStageMaxUs[kSend]),
                  static_cast<unsigned long>(gStageMaxUs[kSurface]), static_cast<unsigned long>(gStageMaxUs[kLeds]),
                  static_cast<unsigned long>(gStageMaxUs[kScreen]),
                  static_cast<unsigned long>(gEngine.persistence().stats().saves),
                  static_cast<unsigned long>(gEngine.persistence().stats().saveFailed),
                  static_cast<unsigned long>(gHeartbeats));
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
  gHostMidi.sendProgramChange(program, Surface<EngineRoot>::kProgramChannel + 1);
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
  gHostMidi.send(type, m.d1, m.d2, channel);
}

void sendLed(uint8_t note, uint8_t velocity) {
  const uint8_t status = static_cast<uint8_t>((velocity != 0 ? 0x90 : 0x80) | Surface<EngineRoot>::kLedChannel);
  sendDin(status, note, velocity);
  usbMIDI.send(status & 0xF0, note, velocity, Surface<EngineRoot>::kLedChannel + 1, 0);
  usbMIDI.send_now();
  gLastUsbTxUs = micros();
  ++gUsbOut;
  ++gOutThisPass;
  gHostMidi.send(status & 0xF0, note, velocity, Surface<EngineRoot>::kLedChannel + 1);
}

// ---- Breadcrumbs -----------------------------------------------------------------------------------------------------
DMAMEM uint32_t gPrevCrumbs[8];
void crumb(uint32_t stage) {
  const uint32_t t = micros();
  if (gStage != 0 && t - gStageAtUs > gStageMaxUs[gStage]) gStageMaxUs[gStage] = t - gStageAtUs;
  gStage = stage;
  gStageAtUs = t;
  CrashReport.breadcrumb(1, stage);
  CrashReport.breadcrumb(2, gLastTick);
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
  } else if (!gBootReported && !gCard.ready()) {
    gBootReported = true;
  }
  crumb(kSurface);
  gSurface.service(millis(), gLastTick, micros());
  if (gBootReported) saveSelection(micros());  // saved once settled, after approval
  crumb(kLeds);
  uint8_t bank;
  if (gSurface.bank(bank)) sendBank(bank);  // the DROID's fader bank follows NOTE_EDIT
  EditSurface<EngineRoot>::MotorMsg motor;
  if (gSurface.motor(millis(), motor)) sendMotor(motor);  // the DROID's motor faders follow the selection
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
    ev.steps = ed.steps();
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
}

#endif  // ENGINE_FIRMWARE
