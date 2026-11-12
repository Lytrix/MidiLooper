//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The milestone library and what the surface asks of it: milestones and the checkpoint saved, a set loaded or returned
// to (stopped, or live at the switch point with staged tracks), get a loop and its picker (previews, auditions, KEEP),
// the working set named, and the sets browser's requests. A coordinator: it drives the engine, the surface and the
// screen together, so it holds the `Firmware` and the settings it reads.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "DeviceSettings.h"
#include "Firmware.h"

namespace firmware {

using namespace ContentEngine;

struct LibraryOperations {
  using ProductionBrowser = ContentEngine::ProductionBrowser;

  LibraryOperations(Firmware& f, DeviceSettings& s, ProductionBrowser& b) : fw(f), settings(s), browser(b) {}

  Firmware& fw;
  DeviceSettings& settings;
  ProductionBrowser& browser;

  static constexpr const char* kLibraryFolder = "/MidiLooper/library";
  static constexpr const char* kLibraryFile = "/MidiLooper/library/library.bin";
  ProductionLibrary::Status libraryWas = ProductionLibrary::Status::Idle;
  bool libraryCheckpoint = false;  // the job running writes the checkpoint (else a milestone)
  uint32_t libraryStartUs = 0;
  // The working set's base milestone (its sequence; 0: none) and set number: a save's parent, a load's
  // checkpoint. Kept on the card in the library's index header: a save names them; after a load or return
  // `workingOwed` until a header naming them is published; a boot starts in the ones the header names.
  uint32_t base = 0;
  uint16_t set = 1;
  bool workingOwed = false;
  ProductionSetLoad::Status loadWas = ProductionSetLoad::Status::Idle;
  // The sets browser and the library's index in memory (4,096 records, PSRAM). `browserOp`: the request it
  // made that is under way (its end answers the browser).
  enum class BrowserOp : uint8_t { None, Save, Load, Return, Mark };
  BrowserOp browserOp = BrowserOp::None;
  bool browserReady = false;
  // The counters at the base milestone (or the boot) — what happened since is the save suggestion's evidence.
  struct Evidence {
    uint32_t commits = 0, records = 0, recordsWithOthers = 0, explore = 0;
  };
  Evidence evidenceAt;
  Evidence evidenceNow() {
    const auto& c = fw.engine.control().stats();
    return Evidence{fw.engine.history().commits(), c.recordsStarted, c.recordsWithOthers, c.exploreGestures};
  }
  /// The working set's base moved (a milestone saved, loaded or returned to): evidence counts from here.
  void resetEvidence() { evidenceAt = evidenceNow(); }
  uint32_t loadGrainMaxUs = 0;  // the worst grain while a load or milestone had card work
  uint32_t loadPhaseMaxUs[3] = {};  // the worst grain by the load's phase before it: checkpoint / stage, clear, read
  uint8_t switchedWas = 0;      // the tracks a live load stopped, as last reported
  uint8_t stagedWas = 0;        // the tracks it switched without a gap, as last reported

  /// The tempo in tenths of a BPM (as LOOP_EDIT shows it).
  uint16_t tempoTenthsNow() {
    const uint64_t d = static_cast<uint64_t>(fw.clock.usPerTick()) * EngineClock::kTicksPerQuarter;
    return static_cast<uint16_t>((600000000ull + d / 2) / d);
  }
  /// The saved selection as it is saved (`saveSelection`).
  void selectionNow(uint8_t* state) {
    state[0] = fw.engine.control().selectedTrack();
    for (uint8_t t = 0; t < kTrackCount; ++t) state[1 + t] = fw.surface.selectedSlot(t);
  }
  FIRMWARE_COLD const char* libraryRefusal(ProductionLibrary::Result r) {
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
  FIRMWARE_COLD void reportLibrary(const char* why) {
    auto& l = fw.engine.library();
    const library::IndexHeader& h = l.header();
    library::CheckpointHeader cp{};
    const bool hasCheckpoint = l.checkpoint(cp);
    Serial.printf("#ENGINE,library,%s,open=%u,approved=%u,error=%u,sector=%lu,open_ms=%lu,count=%u,append=%lu,"
                  "free_sectors=%lu,checkpoint=%lu\n",
                  why, l.mounted() ? 1u : 0u, fw.engine.persistence().approved() ? 1u : 0u, fw.card.libraryError(),
                  static_cast<unsigned long>(fw.card.librarySector()),
                  static_cast<unsigned long>(fw.card.libraryOpenUs() / 1000), h.count,
                  static_cast<unsigned long>(h.appendSector),
                  static_cast<unsigned long>(library::kLogSectors - h.appendSector),
                  static_cast<unsigned long>(hasCheckpoint ? cp.sequence : 0));
  }
  /// Opens the library; creating (preallocating 1 GiB) only while stopped, no take open, saving approved.
  /// The index into memory (once, when the library opens): records in card order, so a browser row's record is its card
  /// index; one that does not read keeps its place (shown as set 00).
  void loadIndex() {
    auto& l = fw.engine.library();
    const uint32_t t = micros();
    browser.clear();
    for (uint16_t i = 0; i < l.header().count; ++i) {
      library::Entry r{};
      if (!l.readEntry(i, r)) r = library::Entry{};
      browser.add(r);
    }
    // The working set the card's header names, else the newest shown milestone's set.
    const ProductionBrowser::Working w = browser.bootWorking(l.header());
    set = w.set;
    base = w.base;
    Serial.printf("#ENGINE,working,boot,set=%u,base=%lu,named=%u\n", set, static_cast<unsigned long>(base),
                  l.header().hasWorking ? 1 : 0);
    browserReady = true;
    resetEvidence();
    fw.surface.setBrowser(&browser);
    fw.display.setBrowser(&browser);
    Serial.printf("#ENGINE,index,count=%u,sets=%u,ms=%lu\n", browser.count(), browser.setCount(),
                  static_cast<unsigned long>((micros() - t) / 1000));
  }
  void openLibrary(const char* why) {
    const bool create = fw.engine.persistence().approved() && !fw.clock.running() &&
                        fw.engine.transport().writingSlot() == ProductionTransport::kNoSlot;
    if (create && !fw.card.exists(kLibraryFolder)) fw.card.makeDir(kLibraryFolder);
    bool created = false;
    fw.engine.openLibrary(kLibraryFile, create, created);
    reportLibrary(created ? "created" : why);
    if (fw.engine.library().mounted()) loadIndex();
  }
  FIRMWARE_COLD void listMilestones() {
    auto& l = fw.engine.library();
    for (uint16_t i = 0; i < l.header().count; ++i) {
      library::Entry r;
      if (!l.readEntry(i, r)) {
        Serial.printf("#ENGINE,ms,i=%u,bad=1\n", i);
        continue;
      }
      char stamp[24];
      wallclock::formatStamp(r.wallTime, settings.tzOffset, stamp, sizeof stamp);
      if (r.kind != library::kMilestone) {
        static const char* const kKinds[] = {"", "trash", "restore", "empty-trash"};
        Serial.printf("#ENGINE,ms,i=%u,%s=SET %02u,target=%lu,seq=%lu,in_trash=%u,time=%s\n", i, kKinds[r.kind], r.set,
                      static_cast<unsigned long>(r.parent), static_cast<unsigned long>(r.sequence),
                      browser.inTrash(i) ? 1u : 0u, stamp);
        continue;
      }
      Serial.printf("#ENGINE,ms,i=%u,hidden=%u,id=SET %02u %c%02u,seq=%lu,parent=%lu,time=%s,tempo=%u.%u,loops=%u,shortest=%lu,"
                    "longest=%lu,sector=%lu,sectors=%lu\n",
                    i, browser.hidden(i) ? 1u : 0u, r.set, library::stageLetter(r.stage), r.stageNumber,
                    static_cast<unsigned long>(r.sequence), static_cast<unsigned long>(r.parent), stamp, r.tempoTenths / 10, r.tempoTenths % 10, r.loops,
                    static_cast<unsigned long>(r.shortestLoop), static_cast<unsigned long>(r.longestLoop),
                    static_cast<unsigned long>(r.logSector), static_cast<unsigned long>(r.logSectors));
    }
    reportLibrary("list");
  }
  /// Starts saving the whole set as milestone `stage` `number` of `set`. nullptr, or why not (nothing written).
  const char* beginMilestone(library::Stage stage, uint16_t set, uint8_t number, uint32_t nowUs) {
    auto& l = fw.engine.library();
    if (fw.engine.transport().writingSlot() != ProductionTransport::kNoSlot) return "STOP THE TAKE FIRST";
    if (!l.mounted()) return "LIBRARY NOT READY";
    if (fw.engine.libraryBusy()) return "BUSY";
    ProductionLibrary::Milestone m;
    m.set = set;
    m.stage = stage;
    m.stageNumber = number;
    m.parent = base;
    m.wallTime = settings.wallTime();
    m.tempoTenths = tempoTenthsNow();
    selectionNow(m.selection);
    const ProductionLibrary::Result r = fw.engine.saveMilestone(m);
    if (r != ProductionLibrary::Result::Ok) return libraryRefusal(r);
    libraryCheckpoint = false;
    libraryStartUs = nowUs;
    Serial.printf("#ENGINE,milestone,begin,id=SET %02u %c%02u,sectors=%lu\n", set, library::stageLetter(stage), number,
                  static_cast<unsigned long>(l.published().logSectors));
    return nullptr;
  }
  /// Starts a trash-side record: `kind` library::kTrash with `index` a stage (the browser's record, card order)
  /// or kNone for every stage of `set`; kRestore with `index` the trash record; kEmptyTrash. The working set and what
  /// plays are not touched. nullptr, or why not (nothing written).
  const char* beginMark(uint8_t kind, uint16_t set, uint16_t index, uint32_t nowUs) {
    if (!fw.engine.library().mounted()) return "LIBRARY NOT READY";
    if (fw.engine.libraryBusy()) return "BUSY";
    if (fw.engine.auditioning()) return "AUDITION OPEN";  // its source stays as it is
    const uint32_t target = index == ProductionBrowser::kNone ? 0 : browser.entry(index).sequence;
    const ProductionLibrary::Result r = fw.engine.markLibrary(kind, set, target, settings.wallTime());
    if (r != ProductionLibrary::Result::Ok) return libraryRefusal(r);
    libraryCheckpoint = false;
    libraryStartUs = nowUs;
    Serial.printf("#ENGINE,mark,begin,kind=%u,set=%u,target=%lu\n", kind, set, static_cast<unsigned long>(target));
    return nullptr;
  }
  /// milestone <S|B|E|T|A|F> [set]: the whole set as it is now, numbered next in its stage (the index in memory).
  FIRMWARE_COLD void saveMilestone(const char* arg, uint32_t nowUs) {
    static const char kLetters[] = "SBETAF";
    const char* at = arg != nullptr ? std::strchr(kLetters, arg[0]) : nullptr;
    if (arg == nullptr || arg[0] == 0 || at == nullptr) {
      Serial.println("#ENGINE,milestone,refused,why=STAGE S|B|E|T|A|F");
      return;
    }
    const char* setArg = std::strchr(arg, ' ');
    const uint16_t set = static_cast<uint16_t>(setArg != nullptr ? std::atoi(setArg + 1) : set);
    const auto stage = static_cast<library::Stage>(at - kLetters);
    if (const char* why = beginMilestone(stage, set, browser.nextNumber(set, stage), nowUs))
      Serial.printf("#ENGINE,milestone,refused,why=%s\n", why);
  }
  /// The checkpoint a load writes first: the working set as it is now.
  ProductionLibrary::Checkpoint checkpointNow() {
    ProductionLibrary::Checkpoint c;
    c.wallTime = settings.wallTime();
    c.base = base;
    c.set = set;
    c.tempoTenths = tempoTenthsNow();
    selectionNow(c.selection);
    return c;
  }
  /// checkpoint: replaces the recovery checkpoint with the whole set as it is now (diagnostics until loading).
  FIRMWARE_COLD void saveCheckpoint(uint32_t nowUs) {
    if (fw.engine.transport().writingSlot() != ProductionTransport::kNoSlot) {
      Serial.println("#ENGINE,checkpoint,refused,why=STOP THE TAKE FIRST");
      return;
    }
    const ProductionLibrary::Result r = fw.engine.saveCheckpoint(checkpointNow());
    if (r != ProductionLibrary::Result::Ok) {
      Serial.printf("#ENGINE,checkpoint,refused,why=%s\n", libraryRefusal(r));
      return;
    }
    libraryCheckpoint = true;
    libraryStartUs = nowUs;
    Serial.println("#ENGINE,checkpoint,begin");
  }
  /// Starts loading milestone `index` (card order) or, `load` false, returning to the checkpoint; while playing it waits
  /// for the switch point. nullptr, or why not (nothing changed).
  const char* beginLoad(bool load, uint16_t index, uint32_t nowUs) {
    if (const char* why = fw.engine.loadRefusal()) return why;
    const bool live = fw.clock.running();
    const ProductionSetLoad::Result r =
        load ? fw.engine.loadMilestone(index, checkpointNow(), live) : fw.engine.returnToCheckpoint(live);
    static const char* const kWhy[] = {"OK", "LIBRARY NOT READY", "BUSY", "NO SUCH MILESTONE", "NOTHING TO RETURN TO",
                                       "SET TOO LARGE"};
    if (r != ProductionSetLoad::Result::Ok) return kWhy[static_cast<uint8_t>(r)];
    libraryStartUs = nowUs;
    loadGrainMaxUs = 0;
    loadPhaseMaxUs[0] = loadPhaseMaxUs[1] = loadPhaseMaxUs[2] = 0;
    loadWas = ProductionSetLoad::Status::Running;
    Serial.printf("#ENGINE,%s,begin,live=%u,at=%s\n", load ? "load" : "return", live ? 1u : 0u, settings.switchAtName());
    return nullptr;
  }
  /// load <index>: milestone `index` (0-based, as `milestones` lists it) replaces the set. return: back to the set before
  /// the last load.
  FIRMWARE_COLD void startLoad(const char* verb, const char* arg, uint32_t nowUs) {
    const bool load = verb[0] == 'l';
    if (load && arg == nullptr) {
      Serial.println("#ENGINE,load,refused,why=WHICH");
      return;
    }
    if (const char* why = beginLoad(load, load ? static_cast<uint16_t>(std::atoi(arg)) : 0, nowUs))
      Serial.printf("#ENGINE,%s,refused,why=%s\n", verb, why);
  }
  // Get a loop: the slot a get places into, and the slots named track-slot (`2-3`: track 2, its third slot).
  uint8_t getInto = 0;
  uint8_t getFrom = 0;
  // What a get is for: a placed copy; a preview read reported and dropped (diagnostics); an audition; the copy
  // that KEEPs it.
  // The get-a-loop picker: the loop under its cursor read as a preview (PickPreview), the copy its USE places (PickUse).
  enum class GetFor : uint8_t { Place, Preview, Audition, Keep, PickPreview, PickUse };
  GetFor getFor = GetFor::Place;
  uint16_t getIndex = 0;
  void slotName(uint8_t s, char* out, size_t n) { snprintf(out, n, "%u-%u", s / 8 + 1, s % 8 + 1); }
  bool parseSlot(const char* a, uint8_t& s) {
    unsigned t = 0, k = 0;
    if (a == nullptr || std::sscanf(a, "%u-%u", &t, &k) != 2 || t < 1 || t > kTrackCount || k < 1 || k > 8) return false;
    s = static_cast<uint8_t>((t - 1) * 8 + (k - 1));
    return true;
  }
  /// get <stage index> <from track-slot> <into track-slot> [preview]: that slot of a saved stage onto a slot of the
  /// working set, one undo step; nothing else changes (the working set's base included). `preview` (diagnostics):
  /// read into the preview partition, reported, then dropped — nothing placed.
  void startGet(char* arg, uint32_t nowUs, GetFor purpose = GetFor::Place) {
    char* from = arg != nullptr ? std::strchr(arg, ' ') : nullptr;
    char* into = from != nullptr ? std::strchr(from + 1, ' ') : nullptr;
    uint8_t f = 0, t = 0;
    if (from == nullptr || into == nullptr || !parseSlot(from + 1, f) || !parseSlot(into + 1, t)) {
      Serial.println("#ENGINE,get,refused,why=GET <INDEX> <T-S> <T-S>");
      return;
    }
    const uint16_t index = static_cast<uint16_t>(std::atoi(arg));
    if (purpose == GetFor::Place && std::strstr(into + 1, "preview") != nullptr) purpose = GetFor::Preview;
    const char* why = fw.engine.loadRefusal(purpose == GetFor::Keep);
    if (why == nullptr) {
      getFor = purpose;
      switch (fw.engine.getLoop(index, f, t, purpose == GetFor::Preview || purpose == GetFor::Audition)) {
        case ProductionSetLoad::Result::Ok: break;
        case ProductionSetLoad::Result::NoMilestone: why = "NO SUCH STAGE"; break;
        case ProductionSetLoad::Result::NoSlot: why = "NO SUCH SLOT"; break;
        case ProductionSetLoad::Result::NotMounted: why = "LIBRARY NOT READY"; break;
        default: why = "BUSY"; break;
      }
    }
    if (why != nullptr) {
      Serial.printf("#ENGINE,%s,refused,why=%s\n", purpose == GetFor::Audition ? "audition" : "get", why);
      return;
    }
    getIndex = index;
    getFrom = f;
    getInto = t;
    libraryStartUs = nowUs;
    loadWas = ProductionSetLoad::Status::Running;
    char a[6], b[6];
    slotName(f, a, sizeof a);
    slotName(t, b, sizeof b);
    Serial.printf("#ENGINE,get,begin,index=%u,from=%s,into=%s,for=%u,content_free=%lu,preview_free=%lu\n", index, a,
                  b, static_cast<unsigned>(purpose), static_cast<unsigned long>(fw.engine.reservations().contentFreePages()),
                  static_cast<unsigned long>(fw.engine.reservations().previewFreePages()));
  }
  // ---- Get a loop's picker -------------------------------------------------------------------------------------
  // The preview: the picker generation seen and since when (a read waits for the cursor to rest), the one being read, the
  // state read for it (held until auditioned or dropped); an audition asked for before its preview was ready.
  static constexpr uint32_t kPreviewRestMs = 150;
  bool pickSquares = false;  // the picker's empty slots as dim squares (`!ENGINE pick-squares 1`, for comparison)
  uint32_t pickGenSeen = 0, pickGenSinceMs = 0, pickGenReading = 0, pickHeldGen = 0, pickAuditionGen = 0;
  uint32_t pickReadGen = 0;  // the generation whose preview was read (shown, maybe auditioned): not read again
  ManifestId pickHeld = kNoManifest;
  bool pickWasOpen = false, pickAuditionWanted = false, pickReading = false;
  uint8_t pickInto = 0;
  void pickDropHeld() {
    fw.engine.releaseState(pickHeld);
    pickHeld = kNoManifest;
    pickHeldGen = 0;
  }
  /// The picker left (any way): the audition ended, the preview gone.
  void pickEnd() {
    fw.engine.auditionCancel(fw.lastTick);
    fw.engine.hidePreview();
    pickDropHeld();
    pickAuditionWanted = false;
    pickGenSeen = 0;
    pickReadGen = 0;
  }
  /// Why an audition cannot be heard (BrowserInfo's code): 0 heard, 1 stopped, 2 the slot not playing, 3 muted, 4 + t
  /// track t soloed.
  uint8_t pickSilentWhy(uint8_t into) {
    const uint8_t t = static_cast<uint8_t>(into / 8);
    auto& c = fw.engine.control();
    if (!fw.clock.running()) return 1;
    if (!fw.engine.transport().playing(t) || fw.engine.transport().playingSlot(t) != into) return 2;
    if (c.muted(t)) return 3;
    for (uint8_t o = 0; o < kTrackCount; ++o)
      if (o != t && c.soloed(o) && !c.soloed(t)) return static_cast<uint8_t>(4 + o);
    return 0;
  }
  /// The audition of the held preview, once both are there.
  void pickStartAudition() {
    if (!pickAuditionWanted || pickHeld == kNoManifest || pickHeldGen != pickAuditionGen) return;
    pickAuditionWanted = false;
    const ManifestId m = pickHeld;
    pickHeld = kNoManifest;  // the audition's now
    pickHeldGen = 0;
    if (fw.engine.auditionStartWith(pickInto, m, fw.lastTick) != ProductionTransport::Result::Ok) {
      browser.failed("AUDITION FAILED", "TRACK BUSY");
      return;
    }
    const uint8_t why = pickSilentWhy(pickInto);
    browser.setAudition(why == 0 ? ProductionBrowser::Audition::Sounding : ProductionBrowser::Audition::Silent, why);
    Serial.printf("#ENGINE,pick,audition,into=%u,sounding=%u,why=%u\n", pickInto, why == 0 ? 1 : 0, why);
  }
  /// Each pass: the destination kept current; entry refused while a take is open; the loop under the cursor read once
  /// the cursor rests (a result for another generation is dropped); everything let go when the picker closes.
  void servicePicker(uint32_t nowMs) {
    if (!browserReady) return;
    if (!browser.isOpen()) {
      const uint8_t t = fw.engine.control().selectedTrack(), slot = fw.surface.selectedSlot(t);
      browser.setPickDestination(slot, fw.engine.transport().contentOf(slot) != kNoManifest);
    }
    const bool picking = browser.picking();
    if (picking && !pickWasOpen && fw.engine.transport().writingSlot() != ProductionTransport::kNoSlot)
      browser.failed("GET A LOOP", "STOP THE TAKE FIRST");
    if (!picking && pickWasOpen) pickEnd();
    pickWasOpen = picking;
    if (!picking) return;
    const uint32_t gen = browser.pickGeneration();
    if (gen != pickGenSeen) {  // a new position: the last preview's state and roll go
      pickGenSeen = gen;
      pickGenSinceMs = nowMs;
      pickDropHeld();
      fw.engine.hidePreview();
    }
    uint32_t want;
    uint16_t rec;
    uint8_t from;
    if (!browser.previewWanted(want, rec, from) || pickReading || pickReadGen == want || pickGenReading == want) return;
    if (nowMs - pickGenSinceMs < kPreviewRestMs || fw.engine.libraryBusy()) return;
    const uint8_t into = browser.pickDestination();
    if (fw.engine.getLoop(rec, from, into, true) != ProductionSetLoad::Result::Ok) {
      browser.setPreview(want, ProductionBrowser::Preview::Failed);
      return;
    }
    getFor = GetFor::PickPreview;
    getFrom = from;
    getInto = into;
    libraryStartUs = micros();
    pickGenReading = want;
    pickReading = true;
    loadWas = ProductionSetLoad::Status::Running;
    browser.setPreview(want, ProductionBrowser::Preview::Reading);
  }
  /// The picker's requests (`serviceBrowser`): audition, use, an empty loop, the audition ended, the picker left.
  FIRMWARE_COLD void pickRequest(const ProductionBrowser::Request& q, uint32_t nowUs) {
    char into[6], line[32];
    slotName(q.into, into, sizeof into);
    switch (q.action) {
      case ProductionBrowser::Action::Audition:
        pickAuditionWanted = true;
        pickAuditionGen = browser.pickGeneration();
        pickInto = q.into;
        pickStartAudition();
        return;
      case ProductionBrowser::Action::Use: {
        const char* why = fw.engine.loadRefusal(true);
        if (why == nullptr && !fw.engine.auditioning()) why = "NO AUDITION";
        if (why == nullptr && fw.engine.getLoop(q.entry, q.from, q.into, false) != ProductionSetLoad::Result::Ok) why = "BUSY";
        if (why != nullptr) {
          browser.failed("USE FAILED", why);
          return;
        }
        getFor = GetFor::PickUse;
        getFrom = q.from;
        getInto = q.into;
        libraryStartUs = nowUs;
        loadWas = ProductionSetLoad::Status::Running;
        return;
      }
      case ProductionBrowser::Action::UseEmpty: {
        fw.engine.auditionCancel(fw.lastTick);
        const auto r = fw.engine.emptyLoop(q.into, static_cast<uint32_t>(q.bars) * kTicksPerBar, fw.lastTick, nowUs,
                                         fw.clock.usPerTick());
        if (r != ProductionTransport::Result::Ok) {
          browser.failed("USE FAILED", "SLOT BUSY");
          return;
        }
        Serial.printf("#ENGINE,pick,empty,into=%s,bars=%u\n", into, q.bars);
        snprintf(line, sizeof line, "EMPTY LOOP IN %s", into);
        browser.succeeded();
        fw.display.toast(line, millis());
        return;
      }
      case ProductionBrowser::Action::CancelAudition:
        fw.engine.auditionCancel(fw.lastTick);
        pickAuditionWanted = false;
        return;
      case ProductionBrowser::Action::LeavePicker:
        pickEnd();
        return;
      default:
        return;
    }
  }
  /// A get's end: placed on its slot, or nothing changed.
  FIRMWARE_COLD void endGet(ProductionSetLoad::Status st, unsigned long ms) {
    const library::Entry& r = fw.engine.setLoad().entry();
    char from[6], into[6], line[48];
    slotName(getFrom, from, sizeof from);
    slotName(getInto, into, sizeof into);
    if (getFor == GetFor::PickPreview) {  // kept only for the position it was read for
      pickReading = false;
      const uint32_t gen = pickGenReading;
      pickGenReading = 0;
      const ManifestId m = st == ProductionSetLoad::Status::Done ? fw.engine.setLoad().takeGot() : kNoManifest;
      if (!browser.picking() || gen != browser.pickGeneration()) {
        fw.engine.releaseState(m);
        return;
      }
      if (m == kNoManifest) {
        using Why = decltype(fw.engine.setLoad().getWhy());
        browser.setPreview(gen, fw.engine.setLoad().getWhy() == Why::Capacity ? ProductionBrowser::Preview::TooBig
                                                                            : ProductionBrowser::Preview::Failed);
        return;
      }
      pickHeld = m;
      pickHeldGen = gen;
      pickReadGen = gen;
      fw.engine.showPreview(m);
      const uint32_t loop = fw.engine.manifests().manifest(m).loopLengthTicks;
      browser.setPreview(gen, ProductionBrowser::Preview::Ready,
                          static_cast<uint16_t>((loop + kTicksPerBar - 1) / kTicksPerBar));
      Serial.printf("#ENGINE,pick,preview,from=%s,ms=%lu,bars=%lu\n", from, ms,
                    static_cast<unsigned long>((loop + kTicksPerBar - 1) / kTicksPerBar));
      pickStartAudition();
      return;
    }
    if (getFor == GetFor::PickUse) {
      const ProductionTransport::Result k = st == ProductionSetLoad::Status::Done
                                                ? fw.engine.auditionKeepGot(fw.lastTick, micros(), fw.clock.usPerTick())
                                                : ProductionTransport::Result::NoChange;
      if (k != ProductionTransport::Result::Ok) {
        browser.failed("USE FAILED", st == ProductionSetLoad::Status::Done ? "SLOT BUSY" : "READ FAILED");
        return;
      }
      Serial.printf("#ENGINE,pick,used,from=%s,into=%s,ms=%lu\n", from, into, ms);
      snprintf(line, sizeof line, "COPIED TO %s", into);
      browser.succeeded();
      fw.display.toast(line, millis());
      return;
    }
    if (st == ProductionSetLoad::Status::Done && getFor == GetFor::Audition) {
      const ProductionTransport::Result a = fw.engine.auditionGot(getInto, fw.lastTick);
      Serial.printf("#ENGINE,audition,%s,id=SET %02u %c%02u,from=%s,into=%s,sounding=%u,ms=%lu,preview_free=%lu\n",
                    a == ProductionTransport::Result::Ok ? "started" : "refused", r.set, library::stageLetter(r.stage),
                    r.stageNumber, from, into, fw.engine.auditionSounding() ? 1 : 0, ms,
                    static_cast<unsigned long>(fw.engine.reservations().previewFreePages()));
      return;
    }
    if (st == ProductionSetLoad::Status::Done && getFor == GetFor::Keep) {
      const ProductionTransport::Result k = fw.engine.auditionKeepGot(fw.lastTick, micros(), fw.clock.usPerTick());
      Serial.printf("#ENGINE,audition,%s,into=%s,ms=%lu\n", k == ProductionTransport::Result::Ok ? "kept" : "keep_failed",
                    into, ms);
      if (k == ProductionTransport::Result::Ok) {
        snprintf(line, sizeof line, "COPIED TO %s", into);
        fw.display.toast(line, millis());
      }
      return;
    }
    if (st == ProductionSetLoad::Status::Done && getFor == GetFor::Preview) {
      Serial.printf("#ENGINE,get,preview,from=%s,ms=%lu,preview_pages=%lu,content_free=%lu,preview_free=%lu\n", from, ms,
                    static_cast<unsigned long>(fw.engine.runs().previewPages()),
                    static_cast<unsigned long>(fw.engine.reservations().contentFreePages()),
                    static_cast<unsigned long>(fw.engine.reservations().previewFreePages()));
      fw.engine.showGotPreview();  // drawn as the preview slot's tiles: `preview-tiles`, `preview-hide`
      return;
    }
    if (st == ProductionSetLoad::Status::Done) {
      const ProductionTransport::Result p = fw.engine.placeGot(getInto, fw.lastTick, micros(), fw.clock.usPerTick());
      if (p == ProductionTransport::Result::Ok) {
        Serial.printf("#ENGINE,get,done,id=SET %02u %c%02u,from=%s,into=%s,ms=%lu\n", r.set,
                      library::stageLetter(r.stage), r.stageNumber, from, into, ms);
        snprintf(line, sizeof line, "COPIED  SET %u %c%02u %s  TO %s", r.set, library::stageLetter(r.stage),
                 r.stageNumber, from, into);
        fw.display.toast(line, millis());
        return;
      }
      Serial.printf("#ENGINE,get,failed,why=SLOT %s BUSY,NOTHING CHANGED,ms=%lu\n", into, ms);
    } else {
      using Why = decltype(fw.engine.setLoad().getWhy());
      const Why why = fw.engine.setLoad().getWhy();
      Serial.printf("#ENGINE,get,failed,why=%s,NOTHING CHANGED,ms=%lu\n",
                    why == Why::Empty      ? "SLOT EMPTY THERE"
                    : why == Why::Capacity ? (getFor == GetFor::Place || getFor == GetFor::Keep ? "NOT ENOUGH MEMORY"
                                                                                     : "TOO BIG TO PREVIEW")
                                           : "READ FAILED",
                    ms);
    }
    fw.display.toast("GET FAILED  NOTHING CHANGED", millis());
  }
  /// The loaded set's tempo and selection (as the boot restores the saved selection).
  FIRMWARE_COLD void applyLoaded(const library::BlobHeader& b) {
    if (b.tempoTenths != 0 && !fw.clock.external()) fw.clock.setTempo(static_cast<uint32_t>(b.tempoTenths) * 10u);
    if (b.selection[0] < kTrackCount) fw.engine.control().selectTrack(b.selection[0]);
    for (uint8_t t = 0; t < kTrackCount; ++t)
      if (b.selection[1 + t] / 8 == t) fw.surface.selectSlot(t, static_cast<uint8_t>(b.selection[1 + t] % 8));
  }
  /// A load's or return's end, reported once.
  FIRMWARE_COLD __attribute__((noinline)) void serviceLoad() {  // from flash (RAM1 room)
    // The clock stopped while a live load waited for its switch point: it switches now.
    if (!fw.clock.running() && fw.engine.setLoad().waiting()) fw.engine.switchNow(fw.lastTick, micros(), fw.clock.usPerTick());
    const uint8_t gapped = fw.engine.switchedTracks(), staged = fw.engine.stagedTracks();
    if (gapped != switchedWas || staged != stagedWas) {
      // The switch point: `staged` tracks took the loaded set there without a gap, `gapped` ones stopped; then
      // the staged ones follow the loaded states (`followed`) and the gapped ones resume from the next bar line (`at=`).
      if ((switchedWas | stagedWas) == 0)
        Serial.printf("#ENGINE,switch,tick=%lu,staged=%02x,gapped=%02x\n", static_cast<unsigned long>(fw.lastTick), staged,
                      gapped);
      if (stagedWas != 0 && staged == 0)
        Serial.printf("#ENGINE,followed,tick=%lu,tracks=%02x\n", static_cast<unsigned long>(fw.lastTick), stagedWas);
      if (switchedWas != 0 && gapped == 0)
        Serial.printf("#ENGINE,resume,tick=%lu,tracks=%02x,at=%lu\n", static_cast<unsigned long>(fw.lastTick), switchedWas,
                      static_cast<unsigned long>((fw.lastTick / kTicksPerBar + 1) * kTicksPerBar));
      switchedWas = gapped;
      stagedWas = staged;
    }
    const ProductionSetLoad& l = fw.engine.setLoad();
    const ProductionSetLoad::Status st = l.status();
    if (st == loadWas || st == ProductionSetLoad::Status::Running) return;
    loadWas = st;
    const unsigned long ms = static_cast<unsigned long>((micros() - libraryStartUs) / 1000);
    if (l.kind() == ProductionSetLoad::Kind::Get) {
      endGet(st, ms);
      return;
    }
    const bool load = l.kind() == ProductionSetLoad::Kind::Load;
    const char* verb = load ? "load" : "return";
    switch (st) {
      case ProductionSetLoad::Status::Done:
        applyLoaded(l.blob());
        resetEvidence();
        if (load) {
          base = l.entry().sequence;
          set = l.entry().set;
          workingOwed = true;
          Serial.printf("#ENGINE,load,done,id=SET %02u %c%02u,seq=%lu,ms=%lu,worst_grain_us=%lu\n", l.entry().set,
                        library::stageLetter(l.entry().stage), l.entry().stageNumber,
                        static_cast<unsigned long>(l.entry().sequence), ms, static_cast<unsigned long>(loadGrainMaxUs));
        } else {
          base = l.checkpoint().base;
          set = l.checkpoint().set;
          workingOwed = true;
          Serial.printf("#ENGINE,return,done,base=%lu,set=%u,ms=%lu,worst_grain_us=%lu\n", static_cast<unsigned long>(base),
                        set, ms, static_cast<unsigned long>(loadGrainMaxUs));
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
    if (browserOp == BrowserOp::Load || browserOp == BrowserOp::Return) {  // the browser's request (rules 6–7)
      char line[40];
      if (st == ProductionSetLoad::Status::Done) {
        if (load) {
          snprintf(line, sizeof line, "LOADED  SET %u  %c%02u", l.entry().set, library::stageLetter(l.entry().stage),
                   l.entry().stageNumber);
        } else {
          char at[8];
          wallclock::formatClock(l.checkpoint().wallTime, settings.tzOffset, at, sizeof at);
          snprintf(line, sizeof line, "RETURNED  SET %u  %s", l.checkpoint().set, at);
        }
        browser.succeeded();
        fw.display.toast(line, millis());
      } else {
        const char* what = load ? "LOAD FAILED" : "RETURN FAILED";
        if (st == ProductionSetLoad::Status::Unchanged) browser.failed(what, "CHECKPOINT NOT WRITTEN");
        else if (st == ProductionSetLoad::Status::Restored) browser.failed(what, "CARD READ ERROR");
        else browser.failed(what, "SET NOT RESTORED", true);
      }
      browserOp = BrowserOp::None;
    }
    Serial.printf("#ENGINE,%s,grains_us,checkpoint_stage=%lu,clear=%lu,read=%lu\n", verb,
                  static_cast<unsigned long>(loadPhaseMaxUs[0]), static_cast<unsigned long>(loadPhaseMaxUs[1]),
                  static_cast<unsigned long>(loadPhaseMaxUs[2]));
    reportLibrary(verb);
  }
  /// The browser: kept current while closed, and what it asks carried out.
  FIRMWARE_COLD __attribute__((noinline)) void serviceBrowser(uint32_t nowUs) {  // from flash (RAM1 room)
    if (!browserReady) return;
    library::CheckpointHeader cp{};
    const bool hasCheckpoint = fw.engine.library().checkpoint(cp);
    if (!browser.isOpen()) {
      browser.setWorking(set, base);
      const Evidence now = evidenceNow(), &at = evidenceAt;
      const uint32_t commits = now.commits - at.commits, records = now.records - at.records;
      ProductionBrowser::Activity a;
      a.build = commits != 0;
      a.takesOnly = records != 0 && now.recordsWithOthers - at.recordsWithOthers == records && commits <= records;
      a.explore = now.explore != at.explore;
      browser.setActivity(a);
    }
    Teensy::BrowserInfo info;
    info.tz = settings.tzOffset;
    info.now = settings.wallTime();
    info.pickSquares = pickSquares;
    if (fw.engine.persistence().unsaved()) {
      snprintf(info.saved, sizeof info.saved, "UNSAVED");
    } else {
      char at[8];
      wallclock::formatClock(settings.lastSaveWall, settings.tzOffset, at, sizeof at);
      snprintf(info.saved, sizeof info.saved, settings.lastSaveWall != 0 ? "SAVED %s" : "SAVED", at);
    }
    if (hasCheckpoint) {
      char at[8];
      wallclock::formatClock(cp.wallTime, settings.tzOffset, at, sizeof at);
      snprintf(info.returnTo, sizeof info.returnTo, "TO SET %u AS LEFT %s", cp.set, at);
    }
    fw.display.setBrowserInfo(info);
    ProductionBrowser::Request q;
    if (!browser.takeRequest(q)) return;
    const char* why = nullptr;
    switch (q.action) {
      case ProductionBrowser::Action::Save:
        browserOp = BrowserOp::Save;
        if ((why = beginMilestone(q.stage, q.set, q.stageNumber, nowUs)) != nullptr) browser.failed("SAVE FAILED", why);
        break;
      case ProductionBrowser::Action::Load:
        browserOp = BrowserOp::Load;
        if ((why = beginLoad(true, q.entry, nowUs)) != nullptr) browser.failed("LOAD FAILED", why);
        break;
      case ProductionBrowser::Action::Trash:
        browserOp = BrowserOp::Mark;
        if ((why = beginMark(library::kTrash, q.set, q.entry, nowUs)) != nullptr) browser.failed("TRASH FAILED", why);
        break;
      case ProductionBrowser::Action::Restore:
        browserOp = BrowserOp::Mark;
        if ((why = beginMark(library::kRestore, q.set, q.entry, nowUs)) != nullptr)
          browser.failed("RESTORE FAILED", why);
        break;
      case ProductionBrowser::Action::Empty:
        browserOp = BrowserOp::Mark;
        if ((why = beginMark(library::kEmptyTrash, 0, ProductionBrowser::kNone, nowUs)) != nullptr)
          browser.failed("EMPTY FAILED", why);
        break;
      case ProductionBrowser::Action::Audition:
      case ProductionBrowser::Action::Use:
      case ProductionBrowser::Action::UseEmpty:
      case ProductionBrowser::Action::CancelAudition:
      case ProductionBrowser::Action::LeavePicker:
        pickRequest(q, nowUs);
        break;
      default:
        break;
    }
    if (why != nullptr) browserOp = BrowserOp::None;
  }
  /// After a load or return: the working set named in the index header once the library is free. A refusal
  /// other than busy is reported and dropped (the next save names it).
  void serviceWorking(uint32_t nowUs) {
    if (!workingOwed || fw.engine.libraryBusy()) return;
    if (!fw.engine.library().mounted()) {
      workingOwed = false;
      return;
    }
    const ProductionLibrary::Result r = fw.engine.publishWorking(set, base);
    if (r == ProductionLibrary::Result::Busy) return;
    workingOwed = false;
    if (r != ProductionLibrary::Result::Ok) {
      Serial.printf("#ENGINE,working,refused,why=%s\n", libraryRefusal(r));
      return;
    }
    libraryCheckpoint = false;
    libraryStartUs = nowUs;
    Serial.printf("#ENGINE,working,begin,set=%u,base=%lu\n", set, static_cast<unsigned long>(base));
  }
  /// A library job's end, reported once.
  FIRMWARE_COLD __attribute__((noinline)) void serviceLibrary() {  // the library's card steps from flash (RAM1 room)
    const ProductionLibrary::Status st = fw.engine.library().status();
    if (st == libraryWas) return;
    libraryWas = st;
    if (fw.engine.setLoad().busy()) return;  // the checkpoint a load writes: the load reports
    if (st != ProductionLibrary::Status::Done && st != ProductionLibrary::Status::Failed) return;
    const bool ok = st == ProductionLibrary::Status::Done;
    const unsigned long ms = static_cast<unsigned long>((micros() - libraryStartUs) / 1000);
    if (fw.engine.library().workingJob()) {  // the header alone; failed, the previous one stays current
      const library::IndexHeader& h = fw.engine.library().header();
      Serial.printf("#ENGINE,working,%s,set=%u,base=%lu,ms=%lu\n", ok ? "done" : "failed", h.workingSet,
                    static_cast<unsigned long>(h.workingBase), ms);
      return;
    }
    if (libraryCheckpoint) {
      Serial.printf("#ENGINE,checkpoint,%s,ms=%lu\n", ok ? "done" : "failed", ms);
    } else if (fw.engine.library().published().kind != library::kMilestone) {  // the working set untouched
      const library::Entry& r = fw.engine.library().published();
      Serial.printf("#ENGINE,mark,%s,kind=%u,set=%u,target=%lu,seq=%lu,ms=%lu\n", ok ? "done" : "failed", r.kind, r.set,
                    static_cast<unsigned long>(r.parent), static_cast<unsigned long>(r.sequence), ms);
      char line[40];
      if (r.kind == library::kEmptyTrash) {
        snprintf(line, sizeof line, "TRASH EMPTIED");
      } else {
        // The stage named (a trash record's milestone, or a restored item's), else the whole set.
        uint32_t stage = r.parent;
        if (r.kind == library::kRestore) {
          const uint16_t t = browser.find(r.parent);
          stage = t != ProductionBrowser::kNone ? browser.entry(t).parent : 0;
        }
        const uint16_t m = stage != 0 ? browser.find(stage) : ProductionBrowser::kNone;
        char id[8] = "";
        if (m != ProductionBrowser::kNone)
          snprintf(id, sizeof id, "  %c%02u", library::stageLetter(browser.entry(m).stage), browser.entry(m).stageNumber);
        snprintf(line, sizeof line, "%s  SET %u%s", r.kind == library::kTrash ? "IN TRASH" : "RESTORED", r.set, id);
      }
      if (ok) browser.add(r);
      if (browserOp == BrowserOp::Mark) {
        if (ok) {
          browser.marked();
          fw.display.toast(line, millis());
        } else {
          browser.failed(r.kind == library::kTrash     ? "TRASH FAILED"
                          : r.kind == library::kRestore ? "RESTORE FAILED"
                                                        : "EMPTY FAILED",
                          "CARD WRITE ERROR");
        }
        browserOp = BrowserOp::None;
      }
    } else {
      const library::Entry& r = fw.engine.library().published();
      Serial.printf("#ENGINE,milestone,%s,id=SET %02u %c%02u,seq=%lu,sectors=%lu,ms=%lu\n", ok ? "done" : "failed",
                    r.set, library::stageLetter(r.stage), r.stageNumber, static_cast<unsigned long>(r.sequence),
                    static_cast<unsigned long>(r.logSectors), ms);
      if (ok) {  // published: in the index, and the working set descends from it
        browser.add(r);
        base = r.sequence;
        set = r.set;
        resetEvidence();
      }
      if (browserOp == BrowserOp::Save) {
        if (ok) {
          char line[40];
          snprintf(line, sizeof line, "SAVED  SET %u  %c%02u", r.set, library::stageLetter(r.stage), r.stageNumber);
          browser.succeeded();
          fw.display.toast(line, millis());
        } else {
          browser.failed("SAVE FAILED", "CARD WRITE ERROR");
        }
        browserOp = BrowserOp::None;
      }
    }
    reportLibrary(ok ? "saved" : "failed");
  }

};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
