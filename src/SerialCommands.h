//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The USB serial console: `!ENGINE <verb> …` commands, `!STOP_STATUS` and `!REBOOT`, read a line at a time. Diagnostics
// and test hooks for the host scripts — they reach every part of the firmware, so the console holds them all; the
// performance itself never depends on it. Also: files to the card's groove folder, and undo-history births.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <SD.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "BootHealth.h"
#include "ContentEngine/History/HistoryCost.h"
#include "DeviceSettings.h"
#include "Firmware.h"
#include "LibraryOperations.h"
#include "MidiInput.h"
#include "MidiPorts.h"

namespace firmware {

using namespace ContentEngine;

struct SerialCommands {
  static constexpr size_t kBenchBytes = 512 * 16;  // `readbench`'s buffer: up to 16 sectors a call

  SerialCommands(Firmware& f, Diagnostics& d, MidiPorts& p, MidiInput& i, DeviceSettings& s, LibraryOperations& l,
                 BootHealth& h, uint8_t* bench)
      : fw(f), diag(d), ports(p), input(i), settings(s), library(l), health(h), benchBuf(bench) {}

  Firmware& fw;
  Diagnostics& diag;
  MidiPorts& ports;
  MidiInput& input;
  DeviceSettings& settings;
  LibraryOperations& library;
  BootHealth& health;
  uint8_t* benchBuf;  // `kBenchBytes`, in DMAMEM (`main.cpp`)
  ContentEngine::ProductionBrowser& browser = library.browser;

  /// Feeds the births counter each pass once the boot load has ended (diagnostics).
  void watchBirths(uint32_t ms) { births.watch(fw, ms); }

  FsFile putFile_;
  uint32_t putBytes_ = 0, putCrc_ = 0;
  bool groovePath(const char* p) {
    return p != nullptr && std::strncmp(p, "/GROOVES/", 9) == 0 && std::strstr(p, "..") == nullptr && std::strlen(p) < 120;
  }
  // Undo-depth sizing: undo-history births since boot — counted per minute (the busiest kept) and by what made
  // each one (a run the slot's previous state did not have: its kind; none: a state with fewer runs, e.g. a clear).
  // Diagnostics only: one compare a main-loop pass, a scan of the 99 entries per birth. `!ENGINE history-cost`.
  struct Births {
  uint32_t seen = 0, bootMs = 0, minuteStart = 0, minuteCount = 0, busiest = 0, total = 0;
  uint32_t kinds[8] = {};  // 0 record, 1 overdub, 3 edit, 4 length (fit), 5 groove, 6 other run, 7 fewer runs
  bool armed = false;
  void watch(Firmware& fw, uint32_t ms) {
    const uint32_t c = fw.engine.history().commits();
    if (!armed) {  // the boot load's states are not births
      seen = c;
      bootMs = minuteStart = ms;
      armed = true;
      return;
    }
    if (ms - minuteStart >= 60000) {
      minuteStart = ms;
      minuteCount = 0;
    }
    if (c == seen) return;
    const uint32_t born = c - seen;
    seen = c;
    count(fw, born);
  }
  FIRMWARE_COLD void count(Firmware& fw, uint32_t born) {
    total += born;
    minuteCount += born;
    if (minuteCount > busiest) busiest = minuteCount;
    uint8_t slot = 0;
    ManifestId m = kNoManifest, before = kNoManifest;
    if (!fw.engine.history().newest(slot, m, &before)) return;
    const auto& d = fw.engine.manifests().manifest(m);
    uint8_t kind = 7;
    for (uint8_t i = 0; i < d.runCount; ++i) {
      bool had = false;
      if (before != kNoManifest) {
        const auto& b = fw.engine.manifests().manifest(before);
        for (uint8_t k = 0; k < b.runCount && !had; ++k) had = b.runs[k] == d.runs[i];
      }
      if (had) continue;
      const uint8_t rk = fw.engine.runs().run(d.runs[i]).kind;
      const uint8_t mapped = rk == ContentEngine::RunKind::kFit ? 4 : rk <= 5 ? rk : 6;
      if (kind == 7 || kind == 4) kind = mapped;  // a content run wins over the fit run beside it
    }
    kinds[kind] += born;
  }
  } births;

  FIRMWARE_COLD void reportHistoryCost() {  // `!ENGINE history-cost`
    const auto c = ContentEngine::historyCost(fw.engine.history(), fw.engine.manifests(), fw.engine.runs(),
                                                ContentEngine::Config::kSlotCount);
    const auto& hs = fw.engine.history().stats();
    const uint32_t minutes = (millis() - births.bootMs) / 60000u;
    Serial.printf("#ENGINE,history_cost,states=%u,currents=%u,history_runs=%u,history_rows=%lu,current_rows=%lu,"
                  "worst_slot=%d,worst_rows=%lu,row_limit=%lu,manifests_free=%u,runs_free=%u,pages_free=%u,"
                  "manifest_bytes=%u,run_bytes=%u\n",
                  c.states, c.currents, c.historyRuns, static_cast<unsigned long>(c.historyRows),
                  static_cast<unsigned long>(c.currentRows), c.worstSlot == 0xFF ? -1 : static_cast<int>(c.worstSlot),
                  static_cast<unsigned long>(c.worstRows), static_cast<unsigned long>(Config::kCommittedRowLimit),
                  fw.engine.manifests().freeCount(), fw.engine.runs().freeCount(), fw.engine.pool().freeCount(),
                  static_cast<unsigned>(sizeof(ContentEngine::ProductionManifests::Manifest)),
                  static_cast<unsigned>(sizeof(ContentEngine::ProductionRuns::Run)));
    Serial.printf("#ENGINE,history_births,total=%lu,minutes=%lu,busiest_minute=%lu,record=%lu,overdub=%lu,edit=%lu,"
                  "length=%lu,groove=%lu,other=%lu,fewer=%lu,evicted_undo=%lu,dropped_redo=%lu,collapsed=%lu,"
                  "undo_past_retained=%lu\n",
                  static_cast<unsigned long>(births.total), static_cast<unsigned long>(minutes),
                  static_cast<unsigned long>(births.busiest), static_cast<unsigned long>(births.kinds[0]),
                  static_cast<unsigned long>(births.kinds[1]), static_cast<unsigned long>(births.kinds[3]),
                  static_cast<unsigned long>(births.kinds[4]), static_cast<unsigned long>(births.kinds[5]),
                  static_cast<unsigned long>(births.kinds[6] + births.kinds[2]),
                  static_cast<unsigned long>(births.kinds[7]), static_cast<unsigned long>(hs.evictedUndo),
                  static_cast<unsigned long>(hs.droppedRedo), static_cast<unsigned long>(hs.collapsedWraps),
                  static_cast<unsigned long>(hs.undoPastRetained));
  }

  FIRMWARE_COLD void fileCommand(const char* verb, char* arg) {
    fw.card.quiesce();
    if (std::strcmp(verb, "file-open") == 0) {
      if (putFile_) putFile_.close();
      putBytes_ = putCrc_ = 0;
      bool ok = fw.card.ready() && groovePath(arg);
      if (ok) {  // the parents, one level at a time
        char dir[128];
        std::strncpy(dir, arg, sizeof dir - 1);
        dir[sizeof dir - 1] = 0;
        for (char* slash = std::strchr(dir + 1, '/'); slash != nullptr; slash = std::strchr(slash + 1, '/')) {
          *slash = 0;
          if (!SD.exists(dir)) SD.mkdir(dir);
          *slash = '/';
        }
        putFile_ = SD.sdfs.open(arg, O_WRONLY | O_CREAT | O_TRUNC);
        ok = static_cast<bool>(putFile_);
      }
      Serial.printf("#ENGINE,file,open,%s\n", ok ? "ok" : "fail");
    } else if (std::strcmp(verb, "file-data") == 0) {
      uint8_t buf[64];
      uint32_t n = 0;
      for (const char* h = arg; h != nullptr && h[0] && h[1] && n < sizeof buf; h += 2) {
        char pair[3] = {h[0], h[1], 0};
        buf[n++] = static_cast<uint8_t>(std::strtoul(pair, nullptr, 16));
      }
      const bool ok = putFile_ && putFile_.write(buf, n) == n;
      if (ok) {
        putCrc_ = ContentEngine::crc32Update(putCrc_, buf, n);
        putBytes_ += n;
      }
      Serial.printf("#ENGINE,file,data,%s,%lu\n", ok ? "ok" : "fail", static_cast<unsigned long>(putBytes_));
    } else if (std::strcmp(verb, "file-close") == 0) {
      const uint32_t want = arg != nullptr ? static_cast<uint32_t>(std::strtoul(arg, nullptr, 16)) : 0;
      const bool open = static_cast<bool>(putFile_);
      if (open) putFile_.close();
      Serial.printf("#ENGINE,file,close,%s,bytes=%lu,crc=%08lX\n", open && want == putCrc_ ? "ok" : "fail",
                    static_cast<unsigned long>(putBytes_), static_cast<unsigned long>(putCrc_));
    } else if (std::strcmp(verb, "file-list") == 0) {
      File dir = SD.open(arg != nullptr ? arg : "/GROOVES");
      uint32_t count = 0;
      if (dir && dir.isDirectory()) {
        for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
          Serial.printf("#ENGINE,file,entry,%s,%s,%lu\n", e.isDirectory() ? "dir" : "file", e.name(),
                        static_cast<unsigned long>(e.size()));
          ++count;
          e.close();
        }
        dir.close();
      }
      Serial.printf("#ENGINE,file,list,%lu\n", static_cast<unsigned long>(count));
    }
  }

  void report(const char* what, uint8_t result) {
    Serial.printf("#ENGINE,%s,result=%u,tick=%lu\n", what, result, static_cast<unsigned long>(fw.lastTick));
  }

  FIRMWARE_COLD __attribute__((noinline)) void command(char* line, uint32_t nowUs) {  // serial commands from flash (RAM1 room)
    const uint32_t us = fw.clock.usPerTick();
    if (std::strcmp(line, "!STOP_STATUS") == 0) {
      Serial.printf("#STOP_STATUS,state=%s\n", fw.clock.running() ? "RUNNING" : "STOPPED");
      return;
    }
    if (std::strcmp(line, "!REBOOT") == 0) {
      if (fw.clock.running()) return;  // stopped only
      Serial.println("[Engine] host-requested reboot");
      Serial.flush();
      delay(20);
      health.quietReboot(kRebooting);
    }
    if (std::strncmp(line, "!ENGINE ", 8) != 0) return;
    char* verb = line + 8;
    char* arg = std::strchr(verb, ' ');
    if (arg != nullptr) *arg++ = 0;
    const int a = arg != nullptr ? std::atoi(arg) : 0;
    const char* second = arg != nullptr ? std::strchr(arg, ' ') : nullptr;
    const int b = second != nullptr ? std::atoi(second + 1) : 0;
    auto& c = fw.engine.control();
    if (std::strncmp(verb, "file-", 5) == 0) {
      fileCommand(verb, arg);
      return;
    }
    if (std::strcmp(verb, "start") == 0) {
      fw.clock.start(nowUs);
      c.transportStart(fw.clock.nextTick(), nowUs, us);
      report(verb, 0);
    } else if (std::strcmp(verb, "stop") == 0) {
      fw.clock.stop();
      c.transportStop(fw.lastTick, nowUs, us);
      report(verb, 0);
    } else if (std::strcmp(verb, "bpm") == 0) {
      fw.clock.setTempo(static_cast<uint32_t>(a) * 100u);
      report(verb, 0);
    } else if (std::strcmp(verb, "select") == 0) {  // select T: the track the screen and live input follow
      c.selectTrack(static_cast<uint8_t>(a));
      report(verb, 0);
    } else if (std::strcmp(verb, "launch") == 0) {  // launch T S: the track switches to the slot at its loop end
      if (b / 8 == a) fw.surface.selectSlot(static_cast<uint8_t>(a), static_cast<uint8_t>(b % 8));
      report(verb, static_cast<uint8_t>(c.queueSwitch(static_cast<uint8_t>(a), static_cast<uint8_t>(b),
                                                      ProductionControl::Quantize::NextGrid, fw.lastTick)));
    } else if (std::strcmp(verb, "record") == 0) {  // record T S: from the next bar
      c.selectTrack(static_cast<uint8_t>(a));
      if (b / 8 == a) fw.surface.selectSlot(static_cast<uint8_t>(a), static_cast<uint8_t>(b % 8));
      report(verb, static_cast<uint8_t>(c.queueRecord(static_cast<uint8_t>(a), static_cast<uint8_t>(b), fw.lastTick)));
    } else if (std::strcmp(verb, "overdub") == 0) {
      c.selectTrack(static_cast<uint8_t>(a));
      report(verb, static_cast<uint8_t>(c.startOverdub(static_cast<uint8_t>(a), fw.lastTick)));
    } else if (std::strcmp(verb, "end") == 0) {  // end T: as REC (`ControlSurface::endTake`) — a Record take at the bar, an overdub at once
      const auto& x = fw.engine.transport();
      const bool overdub = x.mode() == ProductionTransport::Mode::Overdubbing && x.writingTrack() == a;
      report(verb, static_cast<uint8_t>(overdub ? c.stopNow(fw.lastTick, nowUs, us)
                                                : c.queueStop(static_cast<uint8_t>(a), fw.lastTick)));
    } else if (std::strcmp(verb, "undo") == 0) {
      report(verb, static_cast<uint8_t>(c.undo(static_cast<uint8_t>(a), fw.lastTick, nowUs, us)));
    } else if (std::strcmp(verb, "redo") == 0) {
      report(verb, static_cast<uint8_t>(c.redo(static_cast<uint8_t>(a), fw.lastTick, nowUs, us)));
    } else if (std::strcmp(verb, "trace") == 0) {  // trace 1 / 0: control-plane input (diagnostics)
      input.trace = a != 0;
      report(verb, 0);
    } else if (std::strcmp(verb, "fbdump") == 0) {  // the last frame drawn: `scripts/render_display_dump.py`
      fw.display.dump();
    } else if (std::strcmp(verb, "oled-test") == 0) {  // oled-test 1: blocking white; oled-test 0: DMA stripes
      fw.display.selfTest(a != 0);
      fw.display.hold(millis(), 3000);
    } else if (std::strcmp(verb, "oled-clock") == 0 || std::strcmp(verb, "oled-phase") == 0 ||
               std::strcmp(verb, "frame-ms") == 0 || std::strcmp(verb, "oled-flip") == 0) {
      // Panel timing (diagnostics): oled-clock 0xB1; frame-ms 16; oled-flip 1 / 0 (off-screen frames); no arg reports.
      const long v = arg != nullptr ? std::strtol(arg, nullptr, 0) : 0;
      const bool clock = verb[5] == 'c', phase = verb[5] == 'p', flip = verb[5] == 'f';
      fw.display.tune(clock ? static_cast<uint8_t>(v) : 0, phase ? static_cast<uint8_t>(v) : 0,
                    !clock && !phase && !flip ? static_cast<uint32_t>(v) : 0, flip && arg != nullptr ? static_cast<int>(v) : -1);
    } else if (std::strcmp(verb, "oled-reg") == 0 && arg != nullptr) {  // oled-reg 0xC1 0x9F: any register (diagnostics)
      char* p = nullptr;
      const uint8_t cmd = static_cast<uint8_t>(std::strtol(arg, &p, 0));
      uint8_t d[2] = {};
      uint8_t n = 0;
      for (char* q = p; n < 2; p = q) {
        const long v = std::strtol(p, &q, 0);
        if (q == p) break;
        d[n++] = static_cast<uint8_t>(v);
      }
      fw.display.poke(cmd, d, n);
    } else if (std::strcmp(verb, "time") == 0) {  // time: report; time <unix UTC>: set the RTC
      if (arg != nullptr) rtc_set(std::strtoul(arg, nullptr, 10));
      settings.reportTime();
    } else if (std::strcmp(verb, "tz") == 0) {  // tz <minutes east of UTC>: the shown offset, kept in EEPROM
      if (arg != nullptr) settings.storeTzOffset(static_cast<int16_t>(a));
      settings.reportTime();
    } else if (std::strcmp(verb, "approve-saving") == 0) {  // the owner's decision, typed by the owner
      report(verb, fw.engine.approveSaving() ? 0 : 1);
      if (!fw.engine.library().mounted()) library.openLibrary("approved");  // right after the approval, when stopped
    } else if (std::strcmp(verb, "library") == 0) {  // the milestone library's state
      library.reportLibrary("status");
    } else if (std::strcmp(verb, "library-open") == 0) {  // opens it, creating it when stopped and approved
      if (!fw.engine.library().mounted()) library.openLibrary("opened");
      else library.reportLibrary("status");
    } else if (std::strcmp(verb, "milestone") == 0) {  // milestone <S|B|E|T|A|F> [set]
      library.saveMilestone(arg, nowUs);
    } else if (std::strcmp(verb, "milestones") == 0) {
      library.listMilestones();
    } else if (std::strcmp(verb, "trash") == 0) {  // trash <set> [index]: the set's stages, or stage `index`
      const uint16_t set = static_cast<uint16_t>(arg != nullptr ? std::atoi(arg) : 0);
      const char* at = arg != nullptr ? std::strchr(arg, ' ') : nullptr;
      const uint16_t index = at != nullptr ? static_cast<uint16_t>(std::atoi(at + 1)) : ProductionBrowser::kNone;
      const char* why = set == 0 ? "WHICH" : nullptr;
      if (why == nullptr && index != ProductionBrowser::kNone &&
          (browser.hidden(index) || browser.entry(index).set != set))
        why = "NO SUCH STAGE";
      if (why == nullptr) why = library.beginMark(library::kTrash, set, index, nowUs);
      if (why != nullptr) Serial.printf("#ENGINE,mark,refused,why=%s\n", why);
    } else if (std::strcmp(verb, "restore") == 0) {  // restore <index>: trash record `index` back out
      const uint16_t index = static_cast<uint16_t>(arg != nullptr ? std::atoi(arg) : ProductionBrowser::kNone);
      const char* why = arg == nullptr || !browser.inTrash(index) ? "NOT IN THE TRASH" : nullptr;
      if (why == nullptr) why = library.beginMark(library::kRestore, browser.entry(index).set, index, nowUs);
      if (why != nullptr) Serial.printf("#ENGINE,mark,refused,why=%s\n", why);
    } else if (std::strcmp(verb, "empty-trash") == 0) {
      const char* why = library.beginMark(library::kEmptyTrash, 0, ProductionBrowser::kNone, nowUs);
      if (why != nullptr) Serial.printf("#ENGINE,mark,refused,why=%s\n", why);
    } else if (std::strcmp(verb, "checkpoint") == 0) {
      library.saveCheckpoint(nowUs);
    } else if (std::strcmp(verb, "switch-at") == 0) {  // switch-at bar|loop: where a live load switches
      if (arg != nullptr && std::strcmp(arg, "bar") == 0) settings.storeSwitchAt(ProductionControl::SwitchAt::NextBar);
      if (arg != nullptr && std::strcmp(arg, "loop") == 0) settings.storeSwitchAt(ProductionControl::SwitchAt::LoopEnd);
      Serial.printf("#ENGINE,switch-at,%s\n", settings.switchAtName());
    } else if (std::strcmp(verb, "launch-q") == 0) {  // launch-q 16|8|4|bar: the launch quantisation (EEPROM)
      if (arg != nullptr && std::strcmp(arg, "bar") == 0) settings.storeLaunchGrid(1);
      else if (arg != nullptr && DeviceSettings::launchDivisionValid(static_cast<uint32_t>(a)) && a != 1) settings.storeLaunchGrid(static_cast<uint8_t>(a));
      Serial.printf("#ENGINE,launch-q,%s\n", settings.launchGridName());
    } else if (std::strcmp(verb, "groove") == 0) {  // groove TEMPLATE AMOUNT [SCOPE]: the shown slot
      ContentEngine::groove::Settings s;
      s.tmpl = static_cast<uint8_t>(a);
      s.amount = static_cast<uint8_t>(b > 100 ? 100 : b < 0 ? 0 : b);
      const char* third = second != nullptr ? std::strchr(second + 1, ' ') : nullptr;
      s.scope = static_cast<uint8_t>(third != nullptr ? std::atoi(third + 1) : 0);
      const uint8_t slot = fw.surface.shown(c.selectedTrack());
      const auto r = fw.surface.applyGroove(slot, s);  // started: built in the background
      Serial.printf("#ENGINE,groove,slot=%u,template=%u,amount=%u,scope=%u,result=%u\n", slot, s.tmpl, s.amount, s.scope,
                    static_cast<unsigned>(r));
    } else if (std::strcmp(verb, "groove-status") == 0) {  // the groove job: state, grains, the last switch tick
      Serial.printf("#ENGINE,groove-status,state=%u,grains=%lu,at=%lu,tick=%lu\n",
                    static_cast<unsigned>(fw.engine.grooveState()), static_cast<unsigned long>(fw.engine.grooveGrains()),
                    static_cast<unsigned long>(fw.engine.groovePublishedAt()), static_cast<unsigned long>(fw.lastTick));
    } else if (std::strcmp(verb, "leds") == 0) {  // leds: the bar and step LEDs wanted, the shown slot's frame
      uint8_t want[128] = {};
      fw.surface.ledsWanted(fw.lastTick, millis(), want);
      const uint8_t t = fw.engine.control().selectedTrack(), slot = fw.surface.shown(t);
      uint32_t scale = 0, start = 0, known = 0;
      fw.surface.loopFrame(slot, scale, start, known);
      Serial.printf("#ENGINE,leds,slot=%u,frame=%lu,start=%lu,bars=", slot, static_cast<unsigned long>(scale),
                    static_cast<unsigned long>(start));
      for (uint8_t i = 0; i < 8; ++i) Serial.printf("%u%s", want[40 + i], i < 7 ? "/" : ",steps=");
      for (uint8_t i = 0; i < 16; ++i) Serial.printf("%u%s", want[i], i < 15 ? "/" : "\n");
    } else if (std::strcmp(verb, "track") == 0) {  // track T: its playing and queued slots (diagnostics)
      const uint8_t t = static_cast<uint8_t>(a);
      if (t >= kTrackCount) return;
      const auto& x = fw.engine.transport();
      auto& c = fw.engine.control();
      const ManifestId m = x.playing(t) ? x.contentOf(x.playingSlot(t)) : kNoManifest;
      const uint32_t loop = m == kNoManifest ? 0 : x.loopLength(m);
      Serial.printf("#ENGINE,track,%u,tick=%lu,playing=%d,slot=%u,loop=%lu,pos=%lu,queued=%u,queued_pos=%lu,to_queued=%lu,"
                    "launch_q=%s,muted=%u\n",
                    t, static_cast<unsigned long>(fw.lastTick), x.playing(t) ? 1 : 0, x.playingSlot(t),
                    static_cast<unsigned long>(loop),
                    static_cast<unsigned long>(loop == 0 ? 0 : (fw.lastTick - x.origin(t)) % loop), c.queuedSwitchSlot(t),
                    static_cast<unsigned long>(c.queuedPosition(t)),
                    static_cast<unsigned long>(c.ticksToQueued(t, fw.lastTick)), settings.launchGridName(), c.muted(t) ? 1u : 0u);
    } else if (std::strcmp(verb, "dump-slot") == 0) {  // dump-slot S: the slot's current state, its runs and rows
      const ManifestId m = fw.engine.history().current(static_cast<uint8_t>(a));
      if (m == kNoManifest) {
        Serial.println("#ENGINE,dump,none");
      } else {
        const auto& mf = fw.engine.manifests().manifest(m);
        Serial.printf("#ENGINE,dump,slot=%d,identity=%lu,loop=%lu,runs=%u\n", a, static_cast<unsigned long>(mf.identity),
                      static_cast<unsigned long>(mf.loopLengthTicks), mf.runCount);
        for (uint8_t i = 0; i < mf.runCount; ++i) {
          const auto& r = fw.engine.runs().run(mf.runs[i]);
          Serial.printf("#ENGINE,dump,run=%u,id=%u,kind=%u,rows=%lu\n", i, mf.runs[i], static_cast<unsigned>(r.kind),
                        static_cast<unsigned long>(r.rowCount));
          for (uint32_t k = 0; k < r.rowCount; ++k) {
            const PassEvent& e = fw.engine.runs().rowAt(mf.runs[i], k, fw.engine.pool());
            Serial.printf("#R,%u,%lu,%lu,%lu,%u,%u,%u,%u\n", i, static_cast<unsigned long>(e.tick),
                          static_cast<unsigned long>(e.noteId), static_cast<unsigned long>(e.seq), e.status, e.data1,
                          e.data2, e.origin);
          }
        }
        Serial.println("#ENGINE,dump,end");
      }
    } else if (std::strcmp(verb, "switch-stage") == 0) {  // switch-stage 0|1: staged (gapless) live switches
      if (arg != nullptr) fw.engine.setStaging(a != 0);
      Serial.printf("#ENGINE,switch-stage,%u\n", fw.engine.staging() ? 1u : 0u);
    } else if (std::strcmp(verb, "get") == 0) {  // get <index> <T-S> <T-S>: a loop from a saved stage
      library.startGet(arg, nowUs);
    } else if (std::strcmp(verb, "audition") == 0) {  // audition <index> <T-S> <T-S>: heard on the slot, not kept
      library.startGet(arg, nowUs, LibraryOperations::GetFor::Audition);
    } else if (std::strcmp(verb, "audition-keep") == 0) {  // the auditioned loop read again as content and kept
      if (!fw.engine.auditioning()) {
        Serial.println("#ENGINE,audition,refused,why=NO AUDITION");
      } else {
        char again[24];
        char f[6], t[6];
        library.slotName(library.getFrom, f, sizeof f);
        library.slotName(library.getInto, t, sizeof t);
        snprintf(again, sizeof again, "%u %s %s", library.getIndex, f, t);
        library.startGet(again, nowUs, LibraryOperations::GetFor::Keep);
      }
    } else if (std::strcmp(verb, "preview-tiles") == 0) {  // the preview roll's tiles, bar by bar
      const auto& t = fw.engine.tiles();
      Serial.printf("#ENGINE,preview,shown=%u,detail1=%u,wanted=%lu\n", fw.engine.previewShown() ? 1 : 0, t.detailSlot(1),
                    static_cast<unsigned long>(t.wanted(EngineRoot::kPreviewSlot)));
      for (uint8_t bar = 0; bar < 8; ++bar) {
        const auto d = t.drawSource(EngineRoot::kPreviewSlot, bar);
        Serial.printf("#ENGINE,preview,bar=%u,kind=%u,identity=%lu,segments=%u\n", bar, static_cast<unsigned>(d.kind),
                      static_cast<unsigned long>(d.identity), d.header != nullptr ? d.header->count : 0);
      }
    } else if (std::strcmp(verb, "preview-hide") == 0) {
      fw.engine.hidePreview();
      Serial.println("#ENGINE,preview,hidden");
    } else if (std::strcmp(verb, "pick-squares") == 0) {  // pick-squares 1 / 0: the picker's empty slots
      library.pickSquares = a != 0;
      Serial.printf("#ENGINE,pick,squares=%u\n", library.pickSquares ? 1 : 0);
    } else if (std::strcmp(verb, "audition-cancel") == 0) {
      Serial.printf("#ENGINE,audition,%s\n",
                    fw.engine.auditionCancel(fw.lastTick) == ProductionTransport::Result::Ok ? "cancelled" : "none");
    } else if (std::strcmp(verb, "load") == 0 || std::strcmp(verb, "return") == 0) {
      library.startLoad(verb, arg, nowUs);
    } else if (std::strcmp(verb, "take") == 0) {  // the take and its overlay (diagnostics)
      auto& x = fw.engine.transport();
      const uint8_t w = x.writingSlot();
      const ManifestId m = w == ProductionTransport::kNoSlot ? kNoManifest : x.contentOf(w);
      const ManifestId h = w == ProductionTransport::kNoSlot ? kNoManifest : fw.engine.history().current(w);
      Serial.printf("#ENGINE,clock,timeout_age_us=%lu\n", static_cast<unsigned long>(fw.clock.timeoutAgeUs()));
      // Worst stage times (µs): input, serial, ticks, jobs, grains, send, surface, leds, screen; saves so far.
      Serial.printf("#ENGINE,stages,input=%lu,serial=%lu,ticks=%lu,jobs=%lu,grains=%lu,send=%lu,surface=%lu,leds=%lu,"
                    "screen=%lu,saves=%lu,save_failed=%lu,heartbeats=%lu,host_dropped=%lu,host_deferred=%lu,host_pipe=%u\n",
                    static_cast<unsigned long>(diag.stageMaxUs[kInput]), static_cast<unsigned long>(diag.stageMaxUs[kSerial]),
                    static_cast<unsigned long>(diag.stageMaxUs[kTicks]), static_cast<unsigned long>(diag.stageMaxUs[kJobs]),
                    static_cast<unsigned long>(diag.stageMaxUs[kGrains]), static_cast<unsigned long>(diag.stageMaxUs[kSend]),
                    static_cast<unsigned long>(diag.stageMaxUs[kSurface]), static_cast<unsigned long>(diag.stageMaxUs[kLeds]),
                    static_cast<unsigned long>(diag.stageMaxUs[kScreen]),
                    static_cast<unsigned long>(fw.engine.persistence().stats().saves),
                    static_cast<unsigned long>(fw.engine.persistence().stats().saveFailed),
                    static_cast<unsigned long>(ports.heartbeats), static_cast<unsigned long>(ports.hostDropped),
                    static_cast<unsigned long>(ports.hostDeferred), ports.hostPipeOpen() ? 1u : 0u);
      for (uint32_t& m : diag.stageMaxUs) m = 0;
      const auto& ct = fw.card.times();  // the card operations' worst times (µs)
      Serial.printf("#ENGINE,card,exists=%lu,open_read=%lu,sector_read=%lu,sector_write=%lu,store_open=%lu,store=%u\n",
                    static_cast<unsigned long>(ct.exists), static_cast<unsigned long>(ct.openRead),
                    static_cast<unsigned long>(ct.sectorRead), static_cast<unsigned long>(ct.sectorWrite),
                    static_cast<unsigned long>(ct.store), fw.engine.persistence().stats().store);
      Serial.printf("#ENGINE,grain_worst,hard=%lu,transition=%lu,display=%lu,persistence=%lu,background=%lu\n",
                    static_cast<unsigned long>(diag.classGrainMaxUs[0]), static_cast<unsigned long>(diag.classGrainMaxUs[1]),
                    static_cast<unsigned long>(diag.classGrainMaxUs[2]), static_cast<unsigned long>(diag.classGrainMaxUs[3]),
                    static_cast<unsigned long>(diag.classGrainMaxUs[4]));
      for (uint32_t& m : diag.classGrainMaxUs) m = 0;
      {  // the run-once store
        const auto& ps = fw.engine.persistence().stats();
        const auto& hs = fw.engine.history().stats();
        Serial.printf("#ENGINE,cold,states=%u,demoted=%lu,pressure=%lu,content_free=%lu,slot_depth=%lu,undo_at_cold=%lu,"
                      "loaded=%lu,bad=%lu,restarts=%lu\n",
                      fw.engine.persistence().coldTier().total(), static_cast<unsigned long>(hs.demoted),
                      static_cast<unsigned long>(hs.pressureDemoted),
                      static_cast<unsigned long>(fw.engine.reservations().contentFreePages()),
                      static_cast<unsigned long>(hs.slotDepth), static_cast<unsigned long>(hs.undoAtCold),
                      static_cast<unsigned long>(ps.coldLoaded), static_cast<unsigned long>(ps.coldBad),
                      static_cast<unsigned long>(ps.coldRestarts));
        Serial.printf("#ENGINE,fetch,fetches=%lu,states=%lu,sectors=%lu,refused=%lu,cancelled=%lu,busy=%lu,active=%u\n",
                      static_cast<unsigned long>(ps.fetches), static_cast<unsigned long>(ps.fetchedStates),
                      static_cast<unsigned long>(ps.fetchSectors), static_cast<unsigned long>(ps.fetchRefused),
                      static_cast<unsigned long>(ps.fetchCancelled), static_cast<unsigned long>(ps.fetchBusy),
                      fw.engine.persistence().fetching() ? 1u : 0u);
        Serial.printf("#ENGINE,runstore,runs_written=%lu,run_sectors=%lu,runs_kept=%lu,store_full=%lu,bad_run=%lu,"
                      "migrated_slots=%lu,renamed=%lu,clusters=%lu\n",
                      static_cast<unsigned long>(ps.runsWritten), static_cast<unsigned long>(ps.runSectors),
                      static_cast<unsigned long>(ps.runsKept), static_cast<unsigned long>(ps.storeFull),
                      static_cast<unsigned long>(ps.badRun), static_cast<unsigned long>(ps.migratedSlots),
                      static_cast<unsigned long>(ps.storeRenamed),
                      static_cast<unsigned long>(fw.engine.persistence().runAreaClusters()));
      }
      Serial.printf("#ENGINE,card_write,next=%lu,jump=%lu,not_ready=%lu,stops=%lu\n",
                    static_cast<unsigned long>(ct.nextWrite), static_cast<unsigned long>(ct.jumpWrite),
                    static_cast<unsigned long>(ct.notReady), static_cast<unsigned long>(ct.stops));
      Serial.printf("#ENGINE,store,error=%u,first_sector=%lu,fat_type=%u,cluster_sectors=%u,clusters=%lu\n",
                    fw.card.storeError(), static_cast<unsigned long>(fw.card.storeSector()), SD.sdfs.fatType(),
                    static_cast<unsigned>(SD.sdfs.sectorsPerCluster()), static_cast<unsigned long>(SD.sdfs.clusterCount()));
      fw.card.clearTimes();
      Serial.printf("#ENGINE,take,pass_max_us=%lu,pulses=%lu,timeouts=%lu,running=%u,external=%u,us_per_tick=%lu,tick=%lu\n",
                    static_cast<unsigned long>(diag.passMaxUs), static_cast<unsigned long>(diag.pulses),
                    static_cast<unsigned long>(diag.timeouts), fw.clock.running(), fw.clock.external(),
                    static_cast<unsigned long>(fw.clock.usPerTick()), static_cast<unsigned long>(fw.lastTick));
      diag.passMaxUs = 0;
      for (uint8_t i = 0; i < diag.gapCount; ++i)
        Serial.printf("#ENGINE,gap,at_ms=%lu,gap_us=%lu,passes=%lu,usb_in=%lu,usb_out=%lu,out_burst=%lu\n",
                      static_cast<unsigned long>(diag.gaps[i].atMs), static_cast<unsigned long>(diag.gaps[i].gapUs),
                      static_cast<unsigned long>(diag.gaps[i].passes), static_cast<unsigned long>(diag.gaps[i].usbIn),
                      static_cast<unsigned long>(diag.gaps[i].usbOut), static_cast<unsigned long>(diag.gaps[i].outBurst));
      diag.gapCount = 0;
      Serial.printf("#ENGINE,take,slot=%u,mode=%u,pos=%lu,content_loop=%lu,history=%u,overlay=%u,dropped=%lu,frames=%lu,"
                    "focus=%u\n",
                    w, static_cast<unsigned>(x.mode()), static_cast<unsigned long>(x.writePosition(fw.lastTick)),
                    static_cast<unsigned long>(m == kNoManifest ? 0 : fw.engine.manifests().manifest(m).loopLengthTicks),
                    h == kNoManifest ? 0u : 1u, fw.engine.overlay().count(),
                    static_cast<unsigned long>(fw.engine.overlay().dropped()), static_cast<unsigned long>(fw.display.frames()),
                    fw.engine.tiles().detailSlot(0));
    } else if (std::strcmp(verb, "history-cost") == 0) {  // undo-depth sizing (diagnostics; changes nothing)
      reportHistoryCost();
    } else if (std::strcmp(verb, "history") == 0) {  // history pressure (diagnostics; changes nothing)
      const auto& h = fw.engine.history();
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
      Serial.print("#ENGINE,history_cold");  // slot:cold steps, where any
      for (uint8_t s = 0; s < ContentEngine::Config::kSlotCount; ++s)
        if (h.coldCount(s) != 0) Serial.printf(",%u:%u", s, h.coldCount(s));
      Serial.println();
      Serial.printf("#ENGINE,history_spread,populated=%u,steps_0=%u,steps_1=%u,steps_2=%u,steps_3_4=%u,steps_5_8=%u,"
                    "steps_9_16=%u,steps_17_up=%u\n",
                    populated, spread[0], spread[1], spread[2], spread[3], spread[4], spread[5], spread[6]);
    } else if (std::strcmp(verb, "readbench") == 0) {  // readbench N K: store read speed (read only; blocks ~N ms)
      // N sectors in a row from the store's slot area, K a "grain" (one call each, then `readSectors(K)`), then 64
      // single sectors scattered across the store: the cost of fetching history from the card (diagnostics).
      uint8_t* const buf = benchBuf;
      const uint32_t n = a > 0 ? static_cast<uint32_t>(a) : 1024, k = b > 0 && b <= 16 ? static_cast<uint32_t>(b) : 8;
      const uint32_t base = fw.card.storeSector();
      if (base == 0) {
        Serial.println("#ENGINE,readbench,no_store");
        return;
      }
      fw.card.quiesce();
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
      fw.card.quiesce();  // SdFat reads the FAT: the store's stream is stopped first
      const uint32_t t = millis();
      const int32_t free = SD.sdfs.freeClusterCount();
      Serial.printf("#ENGINE,cardfree,free_clusters=%ld,cluster_sectors=%u,clusters=%lu,ms=%lu\n",
                    static_cast<long>(free), static_cast<unsigned>(SD.sdfs.sectorsPerCluster()),
                    static_cast<unsigned long>(SD.sdfs.clusterCount()), static_cast<unsigned long>(millis() - t));
    } else if (std::strcmp(verb, "boot") == 0) {  // the boot's reset cause and the previous boot's breadcrumbs
      health.reportBootReset();
    } else if (std::strcmp(verb, "hang") == 0) {  // hang [irq]: the watchdog's test — the loop stops (stopped only)
      if (fw.clock.running()) return;
      const bool irq = arg != nullptr && std::strcmp(arg, "irq") == 0;
      Serial.printf("#ENGINE,hang,irq=%u,watchdog_s=%lu\n", irq ? 1u : 0u, static_cast<unsigned long>(BootHealth::kWatchdogS));
      Serial.flush();
      if (irq) __disable_irq();  // as a hang with interrupts off: USB is dead too, only the watchdog resets the board
      for (;;) {
      }
    } else if (std::strcmp(verb, "status") == 0) {
      EditNote n{};
      const bool note = fw.surface.edit().selected(n);
      Serial.printf("#ENGINE,status,tick=%lu,running=%u,external=%u,us_per_tick=%lu,approved=%u,frames=%lu,"
                    "frame_draw_worst_us=%lu,edit=%u,edit_window=%lu+%lu,bracket=%lu,note=%d,pitch=%u,len=%lu\n",
                    static_cast<unsigned long>(fw.lastTick), fw.clock.running(), fw.clock.external(),
                    static_cast<unsigned long>(us), fw.engine.persistence().approved(),
                    static_cast<unsigned long>(fw.display.frames()), static_cast<unsigned long>(fw.display.worstDrawUs()),
                    fw.surface.edit().editing() ? 2u : fw.surface.edit().entering() ? 1u : 0u,
                    static_cast<unsigned long>(fw.surface.edit().windowFirst()),
                    static_cast<unsigned long>(fw.surface.edit().windowTicks()),
                    static_cast<unsigned long>(fw.surface.edit().bracket()), note ? static_cast<int>(n.tick) : -1,
                    note ? n.pitch : 0u, note ? static_cast<unsigned long>(n.length) : 0ul);
    }
  }

  void poll(uint32_t nowUs) {
    for (uint8_t i = 0; i < 64 && Serial.available(); ++i) {
      const char ch = static_cast<char>(Serial.read());
      if (ch == '\r') continue;
      if (ch == '\n') {
        line_[lineLength_] = 0;
        if (lineLength_ != 0) command(line_, nowUs);
        lineLength_ = 0;
      } else if (lineLength_ + 1 < sizeof line_) {
        line_[lineLength_++] = ch;
      }
    }
  }

 private:
  char line_[160];  // a `file-data` line: 64 bytes in hex (grooves to the card)
  uint8_t lineLength_ = 0;
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
