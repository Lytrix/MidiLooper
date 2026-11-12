//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Part of `main.cpp`: the serial commands (`!ENGINE …`, files to the card's groove folder, diagnostics).
// Included once, inside `main.cpp`'s anonymous namespace, where its section stood: it uses that file's globals and
// is not a standalone header.

// ---- Files to the card's groove folder ("create the folder and move these to the SD card via
// python") ----------------------------------------------------------------------------------------------------------
// `file-open <path>` (under /GROOVES/ only, no "..": the folder and its parents made), `file-data <hex>` (appended; each
// acknowledged with the bytes so far, the host's pacing), `file-close <crc32 hex>` (closed; the CRC of what was written
// compared), `file-list <dir>` (its entries, name and size). Nothing outside /GROOVES/ is written; nothing is deleted
// or renamed (a file of the same name is replaced). Diagnostics-grade: the host waits for each acknowledgement.
FsFile gPutFile;
uint32_t gPutBytes = 0, gPutCrc = 0;
bool groovePath(const char* p) {
  return p != nullptr && std::strncmp(p, "/GROOVES/", 9) == 0 && std::strstr(p, "..") == nullptr && std::strlen(p) < 120;
}
uint32_t gClassGrainMaxUs[5] = {};  // the worst grain of each job class (hard, transition, display, persistence,
                                   // background), reset by `!ENGINE take`
// Undo-depth sizing: undo-history births since boot — counted per minute (the busiest kept) and by what made
// each one (a run the slot's previous state did not have: its kind; none: a state with fewer runs, e.g. a clear).
// Diagnostics only: one compare a main-loop pass, a scan of the 99 entries per birth. `!ENGINE history-cost`.
namespace births {
uint32_t seen = 0, bootMs = 0, minuteStart = 0, minuteCount = 0, busiest = 0, total = 0;
uint32_t kinds[8] = {};  // 0 record, 1 overdub, 3 edit, 4 length (fit), 5 groove, 6 other run, 7 fewer runs
bool armed = false;
FLASHMEM void count(uint32_t born);
void watch(uint32_t ms) {
  const uint32_t c = gEngine.history().commits();
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
  count(born);
}
FLASHMEM void count(uint32_t born) {
  total += born;
  minuteCount += born;
  if (minuteCount > busiest) busiest = minuteCount;
  uint8_t slot = 0;
  ManifestId m = kNoManifest, before = kNoManifest;
  if (!gEngine.history().newest(slot, m, &before)) return;
  const auto& d = gEngine.manifests().manifest(m);
  uint8_t kind = 7;
  for (uint8_t i = 0; i < d.runCount; ++i) {
    bool had = false;
    if (before != kNoManifest) {
      const auto& b = gEngine.manifests().manifest(before);
      for (uint8_t k = 0; k < b.runCount && !had; ++k) had = b.runs[k] == d.runs[i];
    }
    if (had) continue;
    const uint8_t rk = gEngine.runs().run(d.runs[i]).kind;
    const uint8_t mapped = rk == ContentEngine::RunKind::kFit ? 4 : rk <= 5 ? rk : 6;
    if (kind == 7 || kind == 4) kind = mapped;  // a content run wins over the fit run beside it
  }
  kinds[kind] += born;
}
}  // namespace births

FLASHMEM void reportHistoryCost() {  // `!ENGINE history-cost`
  const auto c = ContentEngine::historyCost(gEngine.history(), gEngine.manifests(), gEngine.runs(),
                                              ContentEngine::Config::kSlotCount);
  const auto& hs = gEngine.history().stats();
  const uint32_t minutes = (millis() - births::bootMs) / 60000u;
  Serial.printf("#ENGINE,history_cost,states=%u,currents=%u,history_runs=%u,history_rows=%lu,current_rows=%lu,"
                "worst_slot=%d,worst_rows=%lu,row_limit=%lu,manifests_free=%u,runs_free=%u,pages_free=%u,"
                "manifest_bytes=%u,run_bytes=%u\n",
                c.states, c.currents, c.historyRuns, static_cast<unsigned long>(c.historyRows),
                static_cast<unsigned long>(c.currentRows), c.worstSlot == 0xFF ? -1 : static_cast<int>(c.worstSlot),
                static_cast<unsigned long>(c.worstRows), static_cast<unsigned long>(Config::kCommittedRowLimit),
                gEngine.manifests().freeCount(), gEngine.runs().freeCount(), gEngine.pool().freeCount(),
                static_cast<unsigned>(sizeof(ContentEngine::ProductionManifests::Manifest)),
                static_cast<unsigned>(sizeof(ContentEngine::ProductionRuns::Run)));
  Serial.printf("#ENGINE,history_births,total=%lu,minutes=%lu,busiest_minute=%lu,record=%lu,overdub=%lu,edit=%lu,"
                "length=%lu,groove=%lu,other=%lu,fewer=%lu,evicted_undo=%lu,dropped_redo=%lu,collapsed=%lu,"
                "undo_past_retained=%lu\n",
                static_cast<unsigned long>(births::total), static_cast<unsigned long>(minutes),
                static_cast<unsigned long>(births::busiest), static_cast<unsigned long>(births::kinds[0]),
                static_cast<unsigned long>(births::kinds[1]), static_cast<unsigned long>(births::kinds[3]),
                static_cast<unsigned long>(births::kinds[4]), static_cast<unsigned long>(births::kinds[5]),
                static_cast<unsigned long>(births::kinds[6] + births::kinds[2]),
                static_cast<unsigned long>(births::kinds[7]), static_cast<unsigned long>(hs.evictedUndo),
                static_cast<unsigned long>(hs.droppedRedo), static_cast<unsigned long>(hs.collapsedWraps),
                static_cast<unsigned long>(hs.undoPastRetained));
}

void fileCommand(const char* verb, char* arg) {
  gCard.quiesce();
  if (std::strcmp(verb, "file-open") == 0) {
    if (gPutFile) gPutFile.close();
    gPutBytes = gPutCrc = 0;
    bool ok = gCard.ready() && groovePath(arg);
    if (ok) {  // the parents, one level at a time
      char dir[128];
      std::strncpy(dir, arg, sizeof dir - 1);
      dir[sizeof dir - 1] = 0;
      for (char* slash = std::strchr(dir + 1, '/'); slash != nullptr; slash = std::strchr(slash + 1, '/')) {
        *slash = 0;
        if (!SD.exists(dir)) SD.mkdir(dir);
        *slash = '/';
      }
      gPutFile = SD.sdfs.open(arg, O_WRONLY | O_CREAT | O_TRUNC);
      ok = static_cast<bool>(gPutFile);
    }
    Serial.printf("#ENGINE,file,open,%s\n", ok ? "ok" : "fail");
  } else if (std::strcmp(verb, "file-data") == 0) {
    uint8_t buf[64];
    uint32_t n = 0;
    for (const char* h = arg; h != nullptr && h[0] && h[1] && n < sizeof buf; h += 2) {
      char pair[3] = {h[0], h[1], 0};
      buf[n++] = static_cast<uint8_t>(std::strtoul(pair, nullptr, 16));
    }
    const bool ok = gPutFile && gPutFile.write(buf, n) == n;
    if (ok) {
      gPutCrc = ContentEngine::crc32Update(gPutCrc, buf, n);
      gPutBytes += n;
    }
    Serial.printf("#ENGINE,file,data,%s,%lu\n", ok ? "ok" : "fail", static_cast<unsigned long>(gPutBytes));
  } else if (std::strcmp(verb, "file-close") == 0) {
    const uint32_t want = arg != nullptr ? static_cast<uint32_t>(std::strtoul(arg, nullptr, 16)) : 0;
    const bool open = static_cast<bool>(gPutFile);
    if (open) gPutFile.close();
    Serial.printf("#ENGINE,file,close,%s,bytes=%lu,crc=%08lX\n", open && want == gPutCrc ? "ok" : "fail",
                  static_cast<unsigned long>(gPutBytes), static_cast<unsigned long>(gPutCrc));
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
  Serial.printf("#ENGINE,%s,result=%u,tick=%lu\n", what, result, static_cast<unsigned long>(gLastTick));
}

FLASHMEM __attribute__((noinline)) void command(char* line, uint32_t nowUs) {  // serial commands from flash (RAM1 room)
  const uint32_t us = gClock.usPerTick();
  if (std::strcmp(line, "!STOP_STATUS") == 0) {
    Serial.printf("#STOP_STATUS,state=%s\n", gClock.running() ? "RUNNING" : "STOPPED");
    return;
  }
  if (std::strcmp(line, "!REBOOT") == 0) {
    if (gClock.running()) return;  // stopped only
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
  if (std::strncmp(verb, "file-", 5) == 0) {
    fileCommand(verb, arg);
    return;
  }
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
  } else if (std::strcmp(verb, "end") == 0) {  // end T: as REC (`ControlSurface::endTake`) — a Record take at the bar, an overdub at once
    const auto& x = gEngine.transport();
    const bool overdub = x.mode() == ProductionTransport::Mode::Overdubbing && x.writingTrack() == a;
    report(verb, static_cast<uint8_t>(overdub ? c.stopNow(gLastTick, nowUs, us)
                                              : c.queueStop(static_cast<uint8_t>(a), gLastTick)));
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
    gDisplay.poke(cmd, d, n);
  } else if (std::strcmp(verb, "time") == 0) {  // time: report; time <unix UTC>: set the RTC
    if (arg != nullptr) rtc_set(std::strtoul(arg, nullptr, 10));
    reportTime();
  } else if (std::strcmp(verb, "tz") == 0) {  // tz <minutes east of UTC>: the shown offset, kept in EEPROM
    if (arg != nullptr) storeTzOffset(static_cast<int16_t>(a));
    reportTime();
  } else if (std::strcmp(verb, "approve-saving") == 0) {  // the owner's decision, typed by the owner
    report(verb, gEngine.approveSaving() ? 0 : 1);
    if (!gEngine.library().mounted()) openLibrary("approved");  // right after the approval, when stopped
  } else if (std::strcmp(verb, "library") == 0) {  // the milestone library's state
    reportLibrary("status");
  } else if (std::strcmp(verb, "library-open") == 0) {  // opens it, creating it when stopped and approved
    if (!gEngine.library().mounted()) openLibrary("opened");
    else reportLibrary("status");
  } else if (std::strcmp(verb, "milestone") == 0) {  // milestone <S|B|E|T|A|F> [set]
    saveMilestone(arg, nowUs);
  } else if (std::strcmp(verb, "milestones") == 0) {
    listMilestones();
  } else if (std::strcmp(verb, "trash") == 0) {  // trash <set> [index]: the set's stages, or stage `index`
    const uint16_t set = static_cast<uint16_t>(arg != nullptr ? std::atoi(arg) : 0);
    const char* at = arg != nullptr ? std::strchr(arg, ' ') : nullptr;
    const uint16_t index = at != nullptr ? static_cast<uint16_t>(std::atoi(at + 1)) : ProductionBrowser::kNone;
    const char* why = set == 0 ? "WHICH" : nullptr;
    if (why == nullptr && index != ProductionBrowser::kNone &&
        (gBrowser.hidden(index) || gBrowser.record(index).set != set))
      why = "NO SUCH STAGE";
    if (why == nullptr) why = beginMark(library::kTrash, set, index, nowUs);
    if (why != nullptr) Serial.printf("#ENGINE,mark,refused,why=%s\n", why);
  } else if (std::strcmp(verb, "restore") == 0) {  // restore <index>: trash record `index` back out
    const uint16_t index = static_cast<uint16_t>(arg != nullptr ? std::atoi(arg) : ProductionBrowser::kNone);
    const char* why = arg == nullptr || !gBrowser.inTrash(index) ? "NOT IN THE TRASH" : nullptr;
    if (why == nullptr) why = beginMark(library::kRestore, gBrowser.record(index).set, index, nowUs);
    if (why != nullptr) Serial.printf("#ENGINE,mark,refused,why=%s\n", why);
  } else if (std::strcmp(verb, "empty-trash") == 0) {
    const char* why = beginMark(library::kEmptyTrash, 0, ProductionBrowser::kNone, nowUs);
    if (why != nullptr) Serial.printf("#ENGINE,mark,refused,why=%s\n", why);
  } else if (std::strcmp(verb, "checkpoint") == 0) {
    saveCheckpoint(nowUs);
  } else if (std::strcmp(verb, "switch-at") == 0) {  // switch-at bar|loop: where a live load switches
    if (arg != nullptr && std::strcmp(arg, "bar") == 0) storeSwitchAt(ProductionControl::SwitchAt::NextBar);
    if (arg != nullptr && std::strcmp(arg, "loop") == 0) storeSwitchAt(ProductionControl::SwitchAt::LoopEnd);
    Serial.printf("#ENGINE,switch-at,%s\n", switchAtName());
  } else if (std::strcmp(verb, "launch-q") == 0) {  // launch-q 16|8|4|bar: the launch quantisation (EEPROM)
    if (arg != nullptr && std::strcmp(arg, "bar") == 0) storeLaunchGrid(1);
    else if (arg != nullptr && launchDivisionValid(static_cast<uint32_t>(a)) && a != 1) storeLaunchGrid(static_cast<uint8_t>(a));
    Serial.printf("#ENGINE,launch-q,%s\n", launchGridName());
  } else if (std::strcmp(verb, "groove") == 0) {  // groove TEMPLATE AMOUNT [SCOPE]: the shown slot
    ContentEngine::groove::Settings s;
    s.tmpl = static_cast<uint8_t>(a);
    s.amount = static_cast<uint8_t>(b > 100 ? 100 : b < 0 ? 0 : b);
    const char* third = second != nullptr ? std::strchr(second + 1, ' ') : nullptr;
    s.scope = static_cast<uint8_t>(third != nullptr ? std::atoi(third + 1) : 0);
    const uint8_t slot = gSurface.shown(c.selectedTrack());
    const auto r = gSurface.applyGroove(slot, s);  // started: built in the background
    Serial.printf("#ENGINE,groove,slot=%u,template=%u,amount=%u,scope=%u,result=%u\n", slot, s.tmpl, s.amount, s.scope,
                  static_cast<unsigned>(r));
  } else if (std::strcmp(verb, "groove-status") == 0) {  // the groove job: state, grains, the last switch tick
    Serial.printf("#ENGINE,groove-status,state=%u,grains=%lu,at=%lu,tick=%lu\n",
                  static_cast<unsigned>(gEngine.grooveState()), static_cast<unsigned long>(gEngine.grooveGrains()),
                  static_cast<unsigned long>(gEngine.groovePublishedAt()), static_cast<unsigned long>(gLastTick));
  } else if (std::strcmp(verb, "leds") == 0) {  // leds: the bar and step LEDs wanted, the shown slot's frame
    uint8_t want[128] = {};
    gSurface.ledsWanted(gLastTick, millis(), want);
    const uint8_t t = gEngine.control().selectedTrack(), slot = gSurface.shown(t);
    uint32_t scale = 0, start = 0, known = 0;
    gSurface.loopFrame(slot, scale, start, known);
    Serial.printf("#ENGINE,leds,slot=%u,frame=%lu,start=%lu,bars=", slot, static_cast<unsigned long>(scale),
                  static_cast<unsigned long>(start));
    for (uint8_t i = 0; i < 8; ++i) Serial.printf("%u%s", want[40 + i], i < 7 ? "/" : ",steps=");
    for (uint8_t i = 0; i < 16; ++i) Serial.printf("%u%s", want[i], i < 15 ? "/" : "\n");
  } else if (std::strcmp(verb, "track") == 0) {  // track T: its playing and queued slots (diagnostics)
    const uint8_t t = static_cast<uint8_t>(a);
    if (t >= kTrackCount) return;
    const auto& x = gEngine.transport();
    auto& c = gEngine.control();
    const ManifestId m = x.playing(t) ? x.contentOf(x.playingSlot(t)) : kNoManifest;
    const uint32_t loop = m == kNoManifest ? 0 : x.loopLength(m);
    Serial.printf("#ENGINE,track,%u,tick=%lu,playing=%d,slot=%u,loop=%lu,pos=%lu,queued=%u,queued_pos=%lu,to_queued=%lu,"
                  "launch_q=%s,muted=%u\n",
                  t, static_cast<unsigned long>(gLastTick), x.playing(t) ? 1 : 0, x.playingSlot(t),
                  static_cast<unsigned long>(loop),
                  static_cast<unsigned long>(loop == 0 ? 0 : (gLastTick - x.origin(t)) % loop), c.queuedSwitchSlot(t),
                  static_cast<unsigned long>(c.queuedPosition(t)),
                  static_cast<unsigned long>(c.ticksToQueued(t, gLastTick)), launchGridName(), c.muted(t) ? 1u : 0u);
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
  } else if (std::strcmp(verb, "switch-stage") == 0) {  // switch-stage 0|1: staged (gapless) live switches
    if (arg != nullptr) gEngine.setStaging(a != 0);
    Serial.printf("#ENGINE,switch-stage,%u\n", gEngine.staging() ? 1u : 0u);
  } else if (std::strcmp(verb, "get") == 0) {  // get <index> <T-S> <T-S>: a loop from a saved stage
    startGet(arg, nowUs);
  } else if (std::strcmp(verb, "audition") == 0) {  // audition <index> <T-S> <T-S>: heard on the slot, not kept
    startGet(arg, nowUs, GetFor::Audition);
  } else if (std::strcmp(verb, "audition-keep") == 0) {  // the auditioned loop read again as content and kept
    if (!gEngine.auditioning()) {
      Serial.println("#ENGINE,audition,refused,why=NO AUDITION");
    } else {
      char again[24];
      char f[6], t[6];
      slotName(gGetFrom, f, sizeof f);
      slotName(gGetInto, t, sizeof t);
      snprintf(again, sizeof again, "%u %s %s", gGetIndex, f, t);
      startGet(again, nowUs, GetFor::Keep);
    }
  } else if (std::strcmp(verb, "preview-tiles") == 0) {  // the preview roll's tiles, bar by bar
    const auto& t = gEngine.tiles();
    Serial.printf("#ENGINE,preview,shown=%u,detail1=%u,wanted=%lu\n", gEngine.previewShown() ? 1 : 0, t.detailSlot(1),
                  static_cast<unsigned long>(t.wanted(EngineRoot::kPreviewSlot)));
    for (uint8_t bar = 0; bar < 8; ++bar) {
      const auto d = t.drawSource(EngineRoot::kPreviewSlot, bar);
      Serial.printf("#ENGINE,preview,bar=%u,kind=%u,identity=%lu,segments=%u\n", bar, static_cast<unsigned>(d.kind),
                    static_cast<unsigned long>(d.identity), d.header != nullptr ? d.header->count : 0);
    }
  } else if (std::strcmp(verb, "preview-hide") == 0) {
    gEngine.hidePreview();
    Serial.println("#ENGINE,preview,hidden");
  } else if (std::strcmp(verb, "pick-squares") == 0) {  // pick-squares 1 / 0: the picker's empty slots
    gPickSquares = a != 0;
    Serial.printf("#ENGINE,pick,squares=%u\n", gPickSquares ? 1 : 0);
  } else if (std::strcmp(verb, "audition-cancel") == 0) {
    Serial.printf("#ENGINE,audition,%s\n",
                  gEngine.auditionCancel(gLastTick) == ProductionTransport::Result::Ok ? "cancelled" : "none");
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
    Serial.printf("#ENGINE,grain_worst,hard=%lu,transition=%lu,display=%lu,persistence=%lu,background=%lu\n",
                  static_cast<unsigned long>(gClassGrainMaxUs[0]), static_cast<unsigned long>(gClassGrainMaxUs[1]),
                  static_cast<unsigned long>(gClassGrainMaxUs[2]), static_cast<unsigned long>(gClassGrainMaxUs[3]),
                  static_cast<unsigned long>(gClassGrainMaxUs[4]));
    for (uint32_t& m : gClassGrainMaxUs) m = 0;
    {  // the run-once store
      const auto& ps = gEngine.persistence().stats();
      const auto& hs = gEngine.history().stats();
      Serial.printf("#ENGINE,cold,states=%u,demoted=%lu,pressure=%lu,content_free=%lu,slot_depth=%lu,undo_at_cold=%lu,"
                    "loaded=%lu,bad=%lu,restarts=%lu\n",
                    gEngine.persistence().coldTier().total(), static_cast<unsigned long>(hs.demoted),
                    static_cast<unsigned long>(hs.pressureDemoted),
                    static_cast<unsigned long>(gEngine.reservations().contentFreePages()),
                    static_cast<unsigned long>(hs.slotDepth), static_cast<unsigned long>(hs.undoAtCold),
                    static_cast<unsigned long>(ps.coldLoaded), static_cast<unsigned long>(ps.coldBad),
                    static_cast<unsigned long>(ps.coldRestarts));
      Serial.printf("#ENGINE,fetch,fetches=%lu,states=%lu,sectors=%lu,refused=%lu,cancelled=%lu,busy=%lu,active=%u\n",
                    static_cast<unsigned long>(ps.fetches), static_cast<unsigned long>(ps.fetchedStates),
                    static_cast<unsigned long>(ps.fetchSectors), static_cast<unsigned long>(ps.fetchRefused),
                    static_cast<unsigned long>(ps.fetchCancelled), static_cast<unsigned long>(ps.fetchBusy),
                    gEngine.persistence().fetching() ? 1u : 0u);
      Serial.printf("#ENGINE,runstore,runs_written=%lu,run_sectors=%lu,runs_kept=%lu,store_full=%lu,bad_run=%lu,"
                    "migrated_slots=%lu,renamed=%lu,clusters=%lu\n",
                    static_cast<unsigned long>(ps.runsWritten), static_cast<unsigned long>(ps.runSectors),
                    static_cast<unsigned long>(ps.runsKept), static_cast<unsigned long>(ps.storeFull),
                    static_cast<unsigned long>(ps.badRun), static_cast<unsigned long>(ps.migratedSlots),
                    static_cast<unsigned long>(ps.storeRenamed),
                    static_cast<unsigned long>(gEngine.persistence().runAreaClusters()));
    }
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
  } else if (std::strcmp(verb, "history-cost") == 0) {  // undo-depth sizing (diagnostics; changes nothing)
    reportHistoryCost();
  } else if (std::strcmp(verb, "history") == 0) {  // history pressure (diagnostics; changes nothing)
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
