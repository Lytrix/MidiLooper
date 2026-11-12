//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Part of `main.cpp`: the milestone library, loads, get a loop and its picker, the working set.
// Included once, inside `main.cpp`'s anonymous namespace, where its section stood: it uses that file's globals and
// is not a standalone header.

// ---- The milestone library ----------------------------------------------------------
constexpr const char* kLibraryFolder = "/MidiLooper/library";
constexpr const char* kLibraryFile = "/MidiLooper/library/library.bin";
ProductionLibrary::Status gLibraryWas = ProductionLibrary::Status::Idle;
bool gLibraryCheckpoint = false;  // the job running writes the checkpoint (else a milestone)
uint32_t gLibraryStartUs = 0;
// The working set's base milestone (its sequence; 0: none) and set number: a save's parent, a load's
// checkpoint. Kept on the card in the library's index header: a save names them; after a load or return
// `gWorkingOwed` until a header naming them is published; a boot starts in the ones the header names.
uint32_t gBase = 0;
uint16_t gSet = 1;
bool gWorkingOwed = false;
ProductionSetLoad::Status gLoadWas = ProductionSetLoad::Status::Idle;
// The sets browser and the library's index in memory (4,096 records, PSRAM). `gBrowserOp`: the request it
// made that is under way (its end answers the browser).
EXTMEM ProductionBrowser gBrowser;
enum class BrowserOp : uint8_t { None, Save, Load, Return, Mark };
BrowserOp gBrowserOp = BrowserOp::None;
bool gBrowserReady = false;
// The counters at the base milestone (or the boot) — what happened since is the save suggestion's evidence.
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
uint32_t gLoadGrainMaxUs = 0;  // the worst grain while a load or milestone had card work
uint32_t gLoadPhaseMaxUs[3] = {};  // the worst grain by the load's phase before it: checkpoint / stage, clear, read
uint8_t gSwitchedWas = 0;      // the tracks a live load stopped, as last reported
uint8_t gStagedWas = 0;        // the tracks it switched without a gap, as last reported

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
/// Opens the library; creating (preallocating 1 GiB) only while stopped, no take open, saving approved.
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
  // The working set the card's header names, else the newest shown milestone's set.
  const ProductionBrowser::Working w = gBrowser.bootWorking(l.header());
  gSet = w.set;
  gBase = w.base;
  Serial.printf("#ENGINE,working,boot,set=%u,base=%lu,named=%u\n", gSet, static_cast<unsigned long>(gBase),
                l.header().hasWorking ? 1 : 0);
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
    if (r.kind != library::kMilestone) {
      static const char* const kKinds[] = {"", "trash", "restore", "empty-trash"};
      Serial.printf("#ENGINE,ms,i=%u,%s=SET %02u,target=%lu,seq=%lu,in_trash=%u,time=%s\n", i, kKinds[r.kind], r.set,
                    static_cast<unsigned long>(r.parent), static_cast<unsigned long>(r.sequence),
                    gBrowser.inTrash(i) ? 1u : 0u, stamp);
      continue;
    }
    Serial.printf("#ENGINE,ms,i=%u,hidden=%u,id=SET %02u %c%02u,seq=%lu,parent=%lu,time=%s,tempo=%u.%u,loops=%u,shortest=%lu,"
                  "longest=%lu,sector=%lu,sectors=%lu\n",
                  i, gBrowser.hidden(i) ? 1u : 0u, r.set, library::stageLetter(r.stage), r.stageNumber,
                  static_cast<unsigned long>(r.sequence), static_cast<unsigned long>(r.parent), stamp, r.tempoTenths / 10, r.tempoTenths % 10, r.loops,
                  static_cast<unsigned long>(r.shortestLoop), static_cast<unsigned long>(r.longestLoop),
                  static_cast<unsigned long>(r.logSector), static_cast<unsigned long>(r.logSectors));
  }
  reportLibrary("list");
}
/// Starts saving the whole set as milestone `stage` `number` of `set`. nullptr, or why not (nothing written).
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
/// Starts a trash-side record: `kind` library::kTrash with `index` a stage (the browser's record, card order)
/// or kNone for every stage of `set`; kRestore with `index` the trash record; kEmptyTrash. The working set and what
/// plays are not touched. nullptr, or why not (nothing written).
const char* beginMark(uint8_t kind, uint16_t set, uint16_t index, uint32_t nowUs) {
  if (!gEngine.library().mounted()) return "LIBRARY NOT READY";
  if (gEngine.libraryBusy()) return "BUSY";
  if (gEngine.auditioning()) return "AUDITION OPEN";  // its source stays as it is
  const uint32_t target = index == ProductionBrowser::kNone ? 0 : gBrowser.record(index).sequence;
  const ProductionLibrary::Result r = gEngine.markLibrary(kind, set, target, wallTime());
  if (r != ProductionLibrary::Result::Ok) return libraryRefusal(r);
  gLibraryCheckpoint = false;
  gLibraryStartUs = nowUs;
  Serial.printf("#ENGINE,mark,begin,kind=%u,set=%u,target=%lu\n", kind, set, static_cast<unsigned long>(target));
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
/// checkpoint: replaces the recovery checkpoint with the whole set as it is now (diagnostics until loading).
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
/// for the switch point. nullptr, or why not (nothing changed).
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
// Get a loop: the slot a get places into, and the slots named track-slot (`2-3`: track 2, its third slot).
uint8_t gGetInto = 0;
uint8_t gGetFrom = 0;
// What a get is for: a placed copy; a preview read reported and dropped (diagnostics); an audition; the copy
// that KEEPs it.
// The get-a-loop picker: the loop under its cursor read as a preview (PickPreview), the copy its USE places (PickUse).
enum class GetFor : uint8_t { Place, Preview, Audition, Keep, PickPreview, PickUse };
GetFor gGetFor = GetFor::Place;
uint16_t gGetIndex = 0;
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
  const char* why = gEngine.loadRefusal(purpose == GetFor::Keep);
  if (why == nullptr) {
    gGetFor = purpose;
    switch (gEngine.getLoop(index, f, t, purpose == GetFor::Preview || purpose == GetFor::Audition)) {
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
  gGetIndex = index;
  gGetFrom = f;
  gGetInto = t;
  gLibraryStartUs = nowUs;
  gLoadWas = ProductionSetLoad::Status::Running;
  char a[6], b[6];
  slotName(f, a, sizeof a);
  slotName(t, b, sizeof b);
  Serial.printf("#ENGINE,get,begin,index=%u,from=%s,into=%s,for=%u,content_free=%lu,preview_free=%lu\n", index, a,
                b, static_cast<unsigned>(purpose), static_cast<unsigned long>(gEngine.reservations().contentFreePages()),
                static_cast<unsigned long>(gEngine.reservations().previewFreePages()));
}
// ---- Get a loop's picker -------------------------------------------------------------------------------------
// The preview: the picker generation seen and since when (a read waits for the cursor to rest), the one being read, the
// state read for it (held until auditioned or dropped); an audition asked for before its preview was ready.
constexpr uint32_t kPreviewRestMs = 150;
bool gPickSquares = false;  // the picker's empty slots as dim squares (`!ENGINE pick-squares 1`, for comparison)
uint32_t gPickGenSeen = 0, gPickGenSinceMs = 0, gPickGenReading = 0, gPickHeldGen = 0, gPickAuditionGen = 0;
uint32_t gPickReadGen = 0;  // the generation whose preview was read (shown, maybe auditioned): not read again
ManifestId gPickHeld = kNoManifest;
bool gPickWasOpen = false, gPickAuditionWanted = false, gPickReading = false;
uint8_t gPickInto = 0;
void pickDropHeld() {
  gEngine.releaseState(gPickHeld);
  gPickHeld = kNoManifest;
  gPickHeldGen = 0;
}
/// The picker left (any way): the audition ended, the preview gone.
void pickEnd() {
  gEngine.auditionCancel(gLastTick);
  gEngine.hidePreview();
  pickDropHeld();
  gPickAuditionWanted = false;
  gPickGenSeen = 0;
  gPickReadGen = 0;
}
/// Why an audition cannot be heard (BrowserInfo's code): 0 heard, 1 stopped, 2 the slot not playing, 3 muted, 4 + t
/// track t soloed.
uint8_t pickSilentWhy(uint8_t into) {
  const uint8_t t = static_cast<uint8_t>(into / 8);
  auto& c = gEngine.control();
  if (!gClock.running()) return 1;
  if (!gEngine.transport().playing(t) || gEngine.transport().playingSlot(t) != into) return 2;
  if (c.muted(t)) return 3;
  for (uint8_t o = 0; o < kTrackCount; ++o)
    if (o != t && c.soloed(o) && !c.soloed(t)) return static_cast<uint8_t>(4 + o);
  return 0;
}
/// The audition of the held preview, once both are there.
void pickStartAudition() {
  if (!gPickAuditionWanted || gPickHeld == kNoManifest || gPickHeldGen != gPickAuditionGen) return;
  gPickAuditionWanted = false;
  const ManifestId m = gPickHeld;
  gPickHeld = kNoManifest;  // the audition's now
  gPickHeldGen = 0;
  if (gEngine.auditionStartWith(gPickInto, m, gLastTick) != ProductionTransport::Result::Ok) {
    gBrowser.failed("AUDITION FAILED", "TRACK BUSY");
    return;
  }
  const uint8_t why = pickSilentWhy(gPickInto);
  gBrowser.setAudition(why == 0 ? ProductionBrowser::Audition::Sounding : ProductionBrowser::Audition::Silent, why);
  Serial.printf("#ENGINE,pick,audition,into=%u,sounding=%u,why=%u\n", gPickInto, why == 0 ? 1 : 0, why);
}
/// Each pass: the destination kept current; entry refused while a take is open; the loop under the cursor read once
/// the cursor rests (a result for another generation is dropped); everything let go when the picker closes.
void servicePicker(uint32_t nowMs) {
  if (!gBrowserReady) return;
  if (!gBrowser.isOpen()) {
    const uint8_t t = gEngine.control().selectedTrack(), slot = gSurface.selectedSlot(t);
    gBrowser.setPickDestination(slot, gEngine.transport().contentOf(slot) != kNoManifest);
  }
  const bool picking = gBrowser.picking();
  if (picking && !gPickWasOpen && gEngine.transport().writingSlot() != ProductionTransport::kNoSlot)
    gBrowser.failed("GET A LOOP", "STOP THE TAKE FIRST");
  if (!picking && gPickWasOpen) pickEnd();
  gPickWasOpen = picking;
  if (!picking) return;
  const uint32_t gen = gBrowser.pickGeneration();
  if (gen != gPickGenSeen) {  // a new position: the last preview's state and roll go
    gPickGenSeen = gen;
    gPickGenSinceMs = nowMs;
    pickDropHeld();
    gEngine.hidePreview();
  }
  uint32_t want;
  uint16_t rec;
  uint8_t from;
  if (!gBrowser.previewWanted(want, rec, from) || gPickReading || gPickReadGen == want || gPickGenReading == want) return;
  if (nowMs - gPickGenSinceMs < kPreviewRestMs || gEngine.libraryBusy()) return;
  const uint8_t into = gBrowser.pickDestination();
  if (gEngine.getLoop(rec, from, into, true) != ProductionSetLoad::Result::Ok) {
    gBrowser.setPreview(want, ProductionBrowser::Preview::Failed);
    return;
  }
  gGetFor = GetFor::PickPreview;
  gGetFrom = from;
  gGetInto = into;
  gLibraryStartUs = micros();
  gPickGenReading = want;
  gPickReading = true;
  gLoadWas = ProductionSetLoad::Status::Running;
  gBrowser.setPreview(want, ProductionBrowser::Preview::Reading);
}
/// The picker's requests (`serviceBrowser`): audition, use, an empty loop, the audition ended, the picker left.
void pickRequest(const ProductionBrowser::Request& q, uint32_t nowUs) {
  char into[6], line[32];
  slotName(q.into, into, sizeof into);
  switch (q.action) {
    case ProductionBrowser::Action::Audition:
      gPickAuditionWanted = true;
      gPickAuditionGen = gBrowser.pickGeneration();
      gPickInto = q.into;
      pickStartAudition();
      return;
    case ProductionBrowser::Action::Use: {
      const char* why = gEngine.loadRefusal(true);
      if (why == nullptr && !gEngine.auditioning()) why = "NO AUDITION";
      if (why == nullptr && gEngine.getLoop(q.record, q.from, q.into, false) != ProductionSetLoad::Result::Ok) why = "BUSY";
      if (why != nullptr) {
        gBrowser.failed("USE FAILED", why);
        return;
      }
      gGetFor = GetFor::PickUse;
      gGetFrom = q.from;
      gGetInto = q.into;
      gLibraryStartUs = nowUs;
      gLoadWas = ProductionSetLoad::Status::Running;
      return;
    }
    case ProductionBrowser::Action::UseEmpty: {
      gEngine.auditionCancel(gLastTick);
      const auto r = gEngine.emptyLoop(q.into, static_cast<uint32_t>(q.bars) * kTicksPerBar, gLastTick, nowUs,
                                       gClock.usPerTick());
      if (r != ProductionTransport::Result::Ok) {
        gBrowser.failed("USE FAILED", "SLOT BUSY");
        return;
      }
      Serial.printf("#ENGINE,pick,empty,into=%s,bars=%u\n", into, q.bars);
      snprintf(line, sizeof line, "EMPTY LOOP IN %s", into);
      gBrowser.succeeded();
      gDisplay.toast(line, millis());
      return;
    }
    case ProductionBrowser::Action::CancelAudition:
      gEngine.auditionCancel(gLastTick);
      gPickAuditionWanted = false;
      return;
    case ProductionBrowser::Action::LeavePicker:
      pickEnd();
      return;
    default:
      return;
  }
}
/// A get's end: placed on its slot, or nothing changed.
void endGet(ProductionSetLoad::Status st, unsigned long ms) {
  const library::Record& r = gEngine.setLoad().record();
  char from[6], into[6], line[48];
  slotName(gGetFrom, from, sizeof from);
  slotName(gGetInto, into, sizeof into);
  if (gGetFor == GetFor::PickPreview) {  // kept only for the position it was read for
    gPickReading = false;
    const uint32_t gen = gPickGenReading;
    gPickGenReading = 0;
    const ManifestId m = st == ProductionSetLoad::Status::Done ? gEngine.setLoad().takeGot() : kNoManifest;
    if (!gBrowser.picking() || gen != gBrowser.pickGeneration()) {
      gEngine.releaseState(m);
      return;
    }
    if (m == kNoManifest) {
      using Why = decltype(gEngine.setLoad().getWhy());
      gBrowser.setPreview(gen, gEngine.setLoad().getWhy() == Why::Capacity ? ProductionBrowser::Preview::TooBig
                                                                          : ProductionBrowser::Preview::Failed);
      return;
    }
    gPickHeld = m;
    gPickHeldGen = gen;
    gPickReadGen = gen;
    gEngine.showPreview(m);
    const uint32_t loop = gEngine.manifests().manifest(m).loopLengthTicks;
    gBrowser.setPreview(gen, ProductionBrowser::Preview::Ready,
                        static_cast<uint16_t>((loop + kTicksPerBar - 1) / kTicksPerBar));
    Serial.printf("#ENGINE,pick,preview,from=%s,ms=%lu,bars=%lu\n", from, ms,
                  static_cast<unsigned long>((loop + kTicksPerBar - 1) / kTicksPerBar));
    pickStartAudition();
    return;
  }
  if (gGetFor == GetFor::PickUse) {
    const ProductionTransport::Result k = st == ProductionSetLoad::Status::Done
                                              ? gEngine.auditionKeepGot(gLastTick, micros(), gClock.usPerTick())
                                              : ProductionTransport::Result::NoChange;
    if (k != ProductionTransport::Result::Ok) {
      gBrowser.failed("USE FAILED", st == ProductionSetLoad::Status::Done ? "SLOT BUSY" : "READ FAILED");
      return;
    }
    Serial.printf("#ENGINE,pick,used,from=%s,into=%s,ms=%lu\n", from, into, ms);
    snprintf(line, sizeof line, "COPIED TO %s", into);
    gBrowser.succeeded();
    gDisplay.toast(line, millis());
    return;
  }
  if (st == ProductionSetLoad::Status::Done && gGetFor == GetFor::Audition) {
    const ProductionTransport::Result a = gEngine.auditionGot(gGetInto, gLastTick);
    Serial.printf("#ENGINE,audition,%s,id=SET %02u %c%02u,from=%s,into=%s,sounding=%u,ms=%lu,preview_free=%lu\n",
                  a == ProductionTransport::Result::Ok ? "started" : "refused", r.set, library::stageLetter(r.stage),
                  r.stageNumber, from, into, gEngine.auditionSounding() ? 1 : 0, ms,
                  static_cast<unsigned long>(gEngine.reservations().previewFreePages()));
    return;
  }
  if (st == ProductionSetLoad::Status::Done && gGetFor == GetFor::Keep) {
    const ProductionTransport::Result k = gEngine.auditionKeepGot(gLastTick, micros(), gClock.usPerTick());
    Serial.printf("#ENGINE,audition,%s,into=%s,ms=%lu\n", k == ProductionTransport::Result::Ok ? "kept" : "keep_failed",
                  into, ms);
    if (k == ProductionTransport::Result::Ok) {
      snprintf(line, sizeof line, "COPIED TO %s", into);
      gDisplay.toast(line, millis());
    }
    return;
  }
  if (st == ProductionSetLoad::Status::Done && gGetFor == GetFor::Preview) {
    Serial.printf("#ENGINE,get,preview,from=%s,ms=%lu,preview_pages=%lu,content_free=%lu,preview_free=%lu\n", from, ms,
                  static_cast<unsigned long>(gEngine.runs().previewPages()),
                  static_cast<unsigned long>(gEngine.reservations().contentFreePages()),
                  static_cast<unsigned long>(gEngine.reservations().previewFreePages()));
    gEngine.showGotPreview();  // drawn as the preview slot's tiles: `preview-tiles`, `preview-hide`
    return;
  }
  if (st == ProductionSetLoad::Status::Done) {
    const ProductionTransport::Result p = gEngine.placeGot(gGetInto, gLastTick, micros(), gClock.usPerTick());
    if (p == ProductionTransport::Result::Ok) {
      Serial.printf("#ENGINE,get,done,id=SET %02u %c%02u,from=%s,into=%s,ms=%lu\n", r.set,
                    library::stageLetter(r.stage), r.stageNumber, from, into, ms);
      snprintf(line, sizeof line, "COPIED  SET %u %c%02u %s  TO %s", r.set, library::stageLetter(r.stage),
               r.stageNumber, from, into);
      gDisplay.toast(line, millis());
      return;
    }
    Serial.printf("#ENGINE,get,failed,why=SLOT %s BUSY,NOTHING CHANGED,ms=%lu\n", into, ms);
  } else {
    using Why = decltype(gEngine.setLoad().getWhy());
    const Why why = gEngine.setLoad().getWhy();
    Serial.printf("#ENGINE,get,failed,why=%s,NOTHING CHANGED,ms=%lu\n",
                  why == Why::Empty      ? "SLOT EMPTY THERE"
                  : why == Why::Capacity ? (gGetFor == GetFor::Place || gGetFor == GetFor::Keep ? "NOT ENOUGH MEMORY"
                                                                                   : "TOO BIG TO PREVIEW")
                                         : "READ FAILED",
                  ms);
  }
  gDisplay.toast("GET FAILED  NOTHING CHANGED", millis());
}
/// The loaded set's tempo and selection (as the boot restores the saved selection).
void applyLoaded(const library::BlobHeader& b) {
  if (b.tempoTenths != 0 && !gClock.external()) gClock.setTempo(static_cast<uint32_t>(b.tempoTenths) * 10u);
  if (b.surface[0] < kTrackCount) gEngine.control().selectTrack(b.surface[0]);
  for (uint8_t t = 0; t < kTrackCount; ++t)
    if (b.surface[1 + t] / 8 == t) gSurface.selectSlot(t, static_cast<uint8_t>(b.surface[1 + t] % 8));
}
/// A load's or return's end, reported once.
FLASHMEM __attribute__((noinline)) void serviceLoad() {  // from flash (RAM1 room)
  // The clock stopped while a live load waited for its switch point: it switches now.
  if (!gClock.running() && gEngine.setLoad().waiting()) gEngine.switchNow(gLastTick, micros(), gClock.usPerTick());
  const uint8_t gapped = gEngine.switchedTracks(), staged = gEngine.stagedTracks();
  if (gapped != gSwitchedWas || staged != gStagedWas) {
    // The switch point: `staged` tracks took the loaded set there without a gap, `gapped` ones stopped; then
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
  const unsigned long ms = static_cast<unsigned long>((micros() - gLibraryStartUs) / 1000);
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
        gBase = l.record().sequence;
        gSet = l.record().set;
        gWorkingOwed = true;
        Serial.printf("#ENGINE,load,done,id=SET %02u %c%02u,seq=%lu,ms=%lu,worst_grain_us=%lu\n", l.record().set,
                      library::stageLetter(l.record().stage), l.record().stageNumber,
                      static_cast<unsigned long>(l.record().sequence), ms, static_cast<unsigned long>(gLoadGrainMaxUs));
      } else {
        gBase = l.checkpoint().base;
        gSet = l.checkpoint().set;
        gWorkingOwed = true;
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
        snprintf(line, sizeof line, "LOADED  SET %u  %c%02u", l.record().set, library::stageLetter(l.record().stage),
                 l.record().stageNumber);
      } else {
        char at[8];
        wallclock::formatClock(l.checkpoint().wallTime, gTzOffset, at, sizeof at);
        snprintf(line, sizeof line, "RETURNED  SET %u  %s", l.checkpoint().set, at);
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
/// The browser: kept current while closed, and what it asks carried out.
FLASHMEM __attribute__((noinline)) void serviceBrowser(uint32_t nowUs) {  // from flash (RAM1 room)
  if (!gBrowserReady) return;
  library::CheckpointHeader cp{};
  const bool hasCheckpoint = gEngine.library().checkpoint(cp);
  if (!gBrowser.isOpen()) {
    gBrowser.setWorking(gSet, gBase);
    const Evidence now = evidenceNow(), &at = gEvidenceAt;
    const uint32_t commits = now.commits - at.commits, records = now.records - at.records;
    ProductionBrowser::Activity a;
    a.build = commits != 0;
    a.takesOnly = records != 0 && now.recordsWithOthers - at.recordsWithOthers == records && commits <= records;
    a.explore = now.explore != at.explore;
    gBrowser.setActivity(a);
  }
  Teensy::BrowserInfo info;
  info.tz = gTzOffset;
  info.now = wallTime();
  info.pickSquares = gPickSquares;
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
    snprintf(info.returnTo, sizeof info.returnTo, "TO SET %u AS LEFT %s", cp.set, at);
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
    case ProductionBrowser::Action::Trash:
      gBrowserOp = BrowserOp::Mark;
      if ((why = beginMark(library::kTrash, q.set, q.record, nowUs)) != nullptr) gBrowser.failed("TRASH FAILED", why);
      break;
    case ProductionBrowser::Action::Restore:
      gBrowserOp = BrowserOp::Mark;
      if ((why = beginMark(library::kRestore, q.set, q.record, nowUs)) != nullptr)
        gBrowser.failed("RESTORE FAILED", why);
      break;
    case ProductionBrowser::Action::Empty:
      gBrowserOp = BrowserOp::Mark;
      if ((why = beginMark(library::kEmptyTrash, 0, ProductionBrowser::kNone, nowUs)) != nullptr)
        gBrowser.failed("EMPTY FAILED", why);
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
  if (why != nullptr) gBrowserOp = BrowserOp::None;
}
/// After a load or return: the working set named in the index header once the library is free. A refusal
/// other than busy is reported and dropped (the next save names it).
void serviceWorking(uint32_t nowUs) {
  if (!gWorkingOwed || gEngine.libraryBusy()) return;
  if (!gEngine.library().mounted()) {
    gWorkingOwed = false;
    return;
  }
  const ProductionLibrary::Result r = gEngine.publishWorking(gSet, gBase);
  if (r == ProductionLibrary::Result::Busy) return;
  gWorkingOwed = false;
  if (r != ProductionLibrary::Result::Ok) {
    Serial.printf("#ENGINE,working,refused,why=%s\n", libraryRefusal(r));
    return;
  }
  gLibraryCheckpoint = false;
  gLibraryStartUs = nowUs;
  Serial.printf("#ENGINE,working,begin,set=%u,base=%lu\n", gSet, static_cast<unsigned long>(gBase));
}
/// A library job's end, reported once.
FLASHMEM __attribute__((noinline)) void serviceLibrary() {  // the library's card steps from flash (RAM1 room)
  const ProductionLibrary::Status st = gEngine.library().status();
  if (st == gLibraryWas) return;
  gLibraryWas = st;
  if (gEngine.setLoad().busy()) return;  // the checkpoint a load writes: the load reports
  if (st != ProductionLibrary::Status::Done && st != ProductionLibrary::Status::Failed) return;
  const bool ok = st == ProductionLibrary::Status::Done;
  const unsigned long ms = static_cast<unsigned long>((micros() - gLibraryStartUs) / 1000);
  if (gEngine.library().workingJob()) {  // the header alone; failed, the previous one stays current
    const library::IndexHeader& h = gEngine.library().header();
    Serial.printf("#ENGINE,working,%s,set=%u,base=%lu,ms=%lu\n", ok ? "done" : "failed", h.workingSet,
                  static_cast<unsigned long>(h.workingBase), ms);
    return;
  }
  if (gLibraryCheckpoint) {
    Serial.printf("#ENGINE,checkpoint,%s,ms=%lu\n", ok ? "done" : "failed", ms);
  } else if (gEngine.library().published().kind != library::kMilestone) {  // the working set untouched
    const library::Record& r = gEngine.library().published();
    Serial.printf("#ENGINE,mark,%s,kind=%u,set=%u,target=%lu,seq=%lu,ms=%lu\n", ok ? "done" : "failed", r.kind, r.set,
                  static_cast<unsigned long>(r.parent), static_cast<unsigned long>(r.sequence), ms);
    char line[40];
    if (r.kind == library::kEmptyTrash) {
      snprintf(line, sizeof line, "TRASH EMPTIED");
    } else {
      // The stage named (a trash record's milestone, or a restored item's), else the whole set.
      uint32_t stage = r.parent;
      if (r.kind == library::kRestore) {
        const uint16_t t = gBrowser.find(r.parent);
        stage = t != ProductionBrowser::kNone ? gBrowser.record(t).parent : 0;
      }
      const uint16_t m = stage != 0 ? gBrowser.find(stage) : ProductionBrowser::kNone;
      char id[8] = "";
      if (m != ProductionBrowser::kNone)
        snprintf(id, sizeof id, "  %c%02u", library::stageLetter(gBrowser.record(m).stage), gBrowser.record(m).stageNumber);
      snprintf(line, sizeof line, "%s  SET %u%s", r.kind == library::kTrash ? "IN TRASH" : "RESTORED", r.set, id);
    }
    if (ok) gBrowser.add(r);
    if (gBrowserOp == BrowserOp::Mark) {
      if (ok) {
        gBrowser.marked();
        gDisplay.toast(line, millis());
      } else {
        gBrowser.failed(r.kind == library::kTrash     ? "TRASH FAILED"
                        : r.kind == library::kRestore ? "RESTORE FAILED"
                                                      : "EMPTY FAILED",
                        "CARD WRITE ERROR");
      }
      gBrowserOp = BrowserOp::None;
    }
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
        snprintf(line, sizeof line, "SAVED  SET %u  %c%02u", r.set, library::stageLetter(r.stage), r.stageNumber);
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
