// C5.3 (DEC-246 amendments 19-22): the committed rolling-window lane of the designated track reads RevisionManifest.
//
// The shared finalization suite's real Track/Loop/storage closure is compiled here with the rolling reader enabled.
// Each case installs a host reader owner (`FixtureReader`): a RevisionStore with the production-equal importer and the
// C5.2 window cursor, i.e. what `RevisionShadow` is on the flag-1 probe image. Its owner work (import, edit index) runs
// only when a case calls `serviceOwner`, which stands in for the registered idle children; the lane never calls it.
//
// Only this suite's own cases are registered; the included suite's cases are dead-stripped from the run.
#define PLAYBACK_WINDOW_ROLLING_ENABLED 1
#define main sharedCaptureFinalizationSuiteMain
#define setUp sharedCaptureFinalizationSuiteSetUp
#define tearDown sharedCaptureFinalizationSuiteTearDown
#include "../test_shared_capture_finalization/test_shared_capture_finalization.cpp"
#undef main
#undef setUp
#undef tearDown

#include "CommittedManifestLane.h"
#include "RevisionStore/ManifestWindowCursor.h"
#include "RevisionStore/RevisionImport.h"
#include "Utils/PlaybackTickContext.h"

#include <algorithm>
#include <functional>
#include <tuple>

namespace {

using LaneState = CommittedManifestLane::State;
using Cap = RevisionStore::Capacity<512, 32, 16, 128, 8, 8, 4>;

LaneState laneStateOf(RevisionStore::WindowResult r) {
  using RevisionStore::WindowResult;
  switch (r) {
    case WindowResult::IndexOwed: return LaneState::IndexOwed;
    case WindowResult::ImportOwed: return LaneState::ImportOwed;
    case WindowResult::CapacityDenied: return LaneState::CapacityDenied;
    case WindowResult::SourceMoved: return LaneState::SourceMoved;
    case WindowResult::Pending: return LaneState::WindowOwed;
    default: return LaneState::Ready;
  }
}

/// The host reader owner. `current` / `beginWindow` / `endWindow` are the lane's whole interface; `serviceOwner` is the
/// deferred owner's import and index work, which the lane never reaches.
struct FixtureReader final : CommittedManifestLane::Reader {
  RevisionStore::Store<Cap> store;
  RevisionStore::ImportSourceKey importedKey{};
  bool imported = false;
  RevisionStore::PassEvent staging[2048];
  RevisionStore::ManifestWindowCursor<Cap> cursor{staging, 2048};
  uint32_t currentCalls = 0;
  uint32_t beginCalls = 0;
  uint32_t imports = 0;
  uint32_t indexSteps = 0;
  std::vector<uint32_t> begunIdentities;

  FixtureReader() { store.reset(); }

  CommittedManifestLane::Source current(const Loop& loop) override {
    ++currentCalls;
    CommittedManifestLane::Source s{};
    if (!loop.hasCommittedPasses()) return s.state = LaneState::NoCommittedSource, s;
    const RevisionStore::ManifestId m = store.current();
    if (!imported || m == RevisionStore::kNoManifest || importedKey != RevisionStore::importSourceKeyOf(loop))
      return s.state = LaneState::ImportOwed, s;
    const RevisionStore::IndexRequest index = store.requestIndex(m);
    if (index == RevisionStore::IndexRequest::IndexCapacityDenied) return s.state = LaneState::CapacityDenied, s;
    if (index != RevisionStore::IndexRequest::NotNeeded && index != RevisionStore::IndexRequest::Ready)
      return s.state = LaneState::IndexOwed, s;
    s.state = LaneState::Ready;
    s.identity = store.identityOf(m);
    s.revision = store.manifest(m).revision;
    s.sourceRevision = importedKey.playbackRevision;
    return s;
  }
  CommittedWindowRowSource* beginWindow(uint32_t identity, uint32_t revision, uint32_t start, uint32_t length,
                                        uint32_t loopLength, LaneState& refusal) override {
    ++beginCalls;
    begunIdentities.push_back(identity);
    const auto r = cursor.begin(store, store.current(), identity, revision, start, length, loopLength);
    if (r != RevisionStore::WindowResult::Pending) {
      refusal = laneStateOf(r);
      return nullptr;
    }
    return &cursor;
  }
  LaneState windowResult() const override { return laneStateOf(cursor.result()); }
  void endWindow() override { cursor.cancel(); }

  /// The deferred owner's turn: import a moved source, then build an owed edit index. Never from the lane.
  void serviceOwner(const Loop& loop) {
    if (loop.hasCommittedPasses() && (!imported || importedKey != RevisionStore::importSourceKeyOf(loop))) {
      TEST_ASSERT_EQUAL(RevisionStore::ImportResult::Ok, RevisionStore::importLoop(loop, store));
      importedKey = RevisionStore::importSourceKeyOf(loop);
      imported = true;
      ++imports;
      return;
    }
    while (store.editIndexOwed()) {
      ++indexSteps;
      if (store.stepEditIndex(256) != RevisionStore::IndexStep::Pending) break;
    }
  }
  uint32_t identity() const { return store.identityOf(store.current()); }
};

FixtureReader& reader() {
  static FixtureReader* r = nullptr;
  if (r == nullptr) r = new FixtureReader();
  return *r;
}

void installReader() {
  FixtureReader& r = reader();
  r.~FixtureReader();
  new (&r) FixtureReader();
  CommittedManifestLane::gReader = &r;
  CommittedManifestLane::gWindowOwner = nullptr;
  CommittedManifestLane::gWindowSource = nullptr;
  CommittedManifestLane::gServable = {0, 0, 0, static_cast<uint8_t>(LaneState::ImportOwed)};
  CommittedManifestLane::gCounters = {};
}

void uninstallReader() {
  CommittedManifestLane::gReader = nullptr;
  CommittedManifestLane::gWindowOwner = nullptr;
  CommittedManifestLane::gWindowSource = nullptr;
}

// The production deferred-owner offer/select/step shape (`main.cpp`'s window lane), as the rolling Record fixtures use.
struct RollingOwnerLane {
  MidiServiceDrain::PlaybackHolderCursor cursor{};
  void serviceTracks(uint32_t tick) {
    const auto clock = clockManager.getPlaybackDeadlineClock();
    MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
      Track& track = trackManager.getTrack(i);
      (void)track.serviceRollingPlaybackWindow(clock);
      track.serviceAdmittedDeadlineMisses(clock);
    }
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
      offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(tick, clock);
    uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
    const auto selection = MidiServiceDrain::selectPlaybackHolderRoundRobin(cursor, offers, Config::NUM_TRACKS, chosen);
    if (selection == MidiServiceDrain::PlaybackHolderSelection::Service)
      (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(tick, nullptr, true, clock);
  }
};

constexpr uint32_t kBar = Config::TICKS_PER_BAR;
constexpr uint32_t kBars = 4;
constexpr uint32_t kLength = kBars * kBar;

/// A four-bar take on track 0 slot 0, stopped to STOPPED, its Record job retired and its root published: one note per
/// bar at `bar + 12 .. bar + 96`, pitch `60 + bar`.
Loop& recordFourBarTake(Track& t) {
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  for (uint32_t b = 1; b < kBars; ++b) {
    t.noteOn(1, static_cast<uint8_t>(60 + b), 100, b * kBar + 12);
    t.noteOff(1, static_cast<uint8_t>(60 + b), 0, b * kBar + 96);
  }
  clockManager.assignCurrentTickSilently(kLength);
  t.stopRecordingToStopped(kLength);
  driveStopToRetirement(t);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT32(kLength, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  return loop;
}

struct PlaybackObservation {
  std::vector<std::pair<uint32_t, MidiEvent>> sent;  // every MIDI event the track emitted, with its send tick
  uint32_t manifestServedTicks = 0;
  uint32_t snapshotServedTicks = 0;
  uint32_t snapshotCursorTicks = 0;  // ticks on which the root's window cursor was in use
  uint32_t wrongIdentityTicks = 0;   // a served manifest window whose identity was not the servable one
  std::vector<uint32_t> servedIdentities;  // each distinct manifest identity a serving window carried, in order
  std::vector<uint32_t> manifestTicks;     // the ticks a manifest window was serving
};

/// Run the transport from `from` to `to` on the production owners. `ownerEvery` > 0 gives the reader owner a turn every
/// that many ticks (0: never).
void play(Track& t, uint32_t from, uint32_t to, PlaybackObservation& obs, uint32_t ownerEvery = 1,
          const std::function<void(uint32_t)>& afterLane = {}) {
  RollingOwnerLane lane;
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  for (uint32_t tick = from; tick < to; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    if (ownerEvery != 0 && tick % ownerEvery == 0 && CommittedManifestLane::gReader == &reader())
      reader().serviceOwner(t.getLoop(0));
    lane.serviceTracks(tick);
    if (afterLane) afterLane(tick);
    const size_t before = MidiSendCapture::count();
    t.playMidiEvents(tick, true);
    for (size_t i = before; i < MidiSendCapture::count(); ++i) obs.sent.emplace_back(tick, MidiSendCapture::at(i));
    if (!runtime.coverageSource.idle()) ++obs.snapshotCursorTicks;
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval coverage{};
    if (t.publishedPlaybackWindowForSlot(0, served, coverage) && served.origin == PlaybackWindowOrigin::Committed &&
        runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving) {
      if (served.manifestIdentity != 0) {
        ++obs.manifestServedTicks;
        obs.manifestTicks.push_back(tick);
        if (served.snapshotIdentity != nullptr) ++obs.wrongIdentityTicks;
        if (obs.servedIdentities.empty() || obs.servedIdentities.back() != served.manifestIdentity)
          obs.servedIdentities.push_back(served.manifestIdentity);
      } else {
        ++obs.snapshotServedTicks;
      }
    }
  }
}

/// The NoteOns emitted at loop phase `[from, to)` of traversal `pass` (0-based), as (phase, pitch).
std::vector<std::pair<uint32_t, uint8_t>> noteOnsInTraversal(const PlaybackObservation& obs, uint32_t pass) {
  std::vector<std::pair<uint32_t, uint8_t>> out;
  for (const auto& [tick, e] : obs.sent) {
    if (!e.isNoteOn() || tick < pass * kLength || tick >= (pass + 1) * kLength) continue;
    out.emplace_back(tick - pass * kLength, e.data.noteData.note);
  }
  return out;
}

/// Every NoteOn emitted, as (loop phase, pitch).
std::vector<std::pair<uint32_t, uint8_t>> noteOnsOf(const PlaybackObservation& obs) {
  std::vector<std::pair<uint32_t, uint8_t>> out;
  for (const auto& [tick, e] : obs.sent)
    if (e.isNoteOn()) out.emplace_back(tick % kLength, e.data.noteData.note);
  return out;
}

/// The take's own NoteOn at that phase (one per bar, pitch `60 + bar`).
bool isTakeNote(uint32_t phase, uint8_t pitch) {
  return phase % kBar == 12 && pitch == 60 + phase / kBar;
}

NoteId recordNoteIdOfPitch(const Loop& loop, uint8_t pitch) {
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  for (const MidiEvent& e : rows)
    if (e.isNoteOn() && e.data.noteData.note == pitch) return e.noteId;
  TEST_FAIL_MESSAGE("no committed NoteOn of that pitch");
  return kInvalidNoteId;
}

/// Production's own Delete edit (a Hide of a committed note), as NOTE_EDIT commits it; the root is republished.
void deleteTakeNote(Loop& loop, uint8_t pitch) {
  const uint32_t bar = pitch - 60;
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.targetNoteId = recordNoteIdOfPitch(loop, pitch);
  row.startTick = bar * kBar + 12;
  row.endTick = bar * kBar + 96;
  row.pitch = pitch;
  row.velocity = 100;
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, row));
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
  for (uint32_t grain = 0; grain < 4096 && loop.hasPendingSnapshotPublish(); ++grain) loop.stepPendingSnapshotPublish(1);
}

void startTransport(Track& t) {
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  MidiSendCapture::reset();
}

}  // namespace

extern "C" void setUp() {
  sharedCaptureFinalizationSuiteSetUp();
  uninstallReader();
}
extern "C" void tearDown() {
  uninstallReader();
  sharedCaptureFinalizationSuiteTearDown();
}

// 1. Current-boot committed content: the routed slot is served from the manifest only. Every served committed window
//    carries the exact manifest identity it read and no root; the root's window cursor is never used; the steady-state
//    traversal plays the take exactly, as the snapshot lane does for the same take.
void test_c5_3_current_boot_content_is_served_from_the_manifest_only() {
  // Oracle first: the same take through the snapshot lane (no reader installed).
  PlaybackObservation snapshotLane;
  {
    Track& t = trackManager.getTrack(0);
    recordFourBarTake(t);
    startTransport(t);
    play(t, 0, 3 * kLength, snapshotLane, 0);
    TEST_ASSERT_EQUAL_UINT32(0, snapshotLane.manifestServedTicks);
    TEST_ASSERT_GREATER_THAN_UINT32(0, snapshotLane.snapshotServedTicks);
  }
  sharedCaptureFinalizationSuiteSetUp();
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);  // the owner imported the current source before playback
  startTransport(t);
  PlaybackObservation manifestLane;
  play(t, 0, 3 * kLength, manifestLane);

  char detail[256];
  snprintf(detail, sizeof(detail), "served manifest=%u snapshot=%u cursor=%u begun=%u completed=%u refused=%u gated=%u",
           manifestLane.manifestServedTicks, manifestLane.snapshotServedTicks, manifestLane.snapshotCursorTicks,
           CommittedManifestLane::gCounters.windowsBegun, CommittedManifestLane::gCounters.windowsCompleted,
           CommittedManifestLane::gCounters.legacyRefused, CommittedManifestLane::gCounters.legacyGated);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, manifestLane.manifestServedTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, manifestLane.snapshotServedTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, manifestLane.snapshotCursorTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, manifestLane.wrongIdentityTicks, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, CommittedManifestLane::gCounters.windowsBegun, detail);
  // The exact identity: the served window names the manifest the reader published, nothing else.
  PlaybackViewSourceIdentity served{};
  PlaybackCoverageInterval coverage{};
  TEST_ASSERT_TRUE(t.publishedPlaybackWindowForSlot(0, served, coverage));
  TEST_ASSERT_EQUAL_UINT32(reader().identity(), served.manifestIdentity);
  TEST_ASSERT_NULL(served.snapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, served.playbackRevision);
  // Inside every interval a manifest window served, the MIDI is the snapshot lane's for the same ticks, event for event.
  // Outside them the routed lane is silent by contract (amendment 23: lane-owned gaps are named misses, never merged).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kBars, noteOnsInTraversal(snapshotLane, 2).size(), "the oracle plays one note per bar");
  const auto servedAt = [&](uint32_t tick) {
    return std::binary_search(manifestLane.manifestTicks.begin(), manifestLane.manifestTicks.end(), tick);
  };
  std::vector<std::tuple<uint32_t, uint8_t, uint8_t>> expected, actual, outside;
  for (const auto& [tick, e] : snapshotLane.sent)
    if (servedAt(tick)) expected.emplace_back(tick, static_cast<uint8_t>(e.type), e.data.noteData.note);
  for (const auto& [tick, e] : manifestLane.sent)
    (servedAt(tick) ? actual : outside).emplace_back(tick, static_cast<uint8_t>(e.type), e.data.noteData.note);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, expected.size(), detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.size(), actual.size(), detail);
  TEST_ASSERT_TRUE_MESSAGE(expected == actual, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, outside.size(), "a routed-lane gap sounded: something other than a window played");
}

// 2. Manifest unavailable: the owner owes the import, the build gate names it, the tick sees no qualified window and
//    builds nothing -- no reader call, no window read, no merged (root) fallback. Once the owner imports, it is served.
void test_c5_3_owed_manifest_is_a_named_miss_and_the_tick_builds_nothing() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  startTransport(t);
  PlaybackObservation owed;
  play(t, 0, kLength, owed, /*ownerEvery=*/0);
  TEST_ASSERT_EQUAL_UINT32(0, reader().imports);
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  TEST_ASSERT_EQUAL_UINT32(0, owed.sent.size());
  TEST_ASSERT_EQUAL_UINT32(0, owed.manifestServedTicks + owed.snapshotServedTicks + owed.snapshotCursorTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, CommittedManifestLane::gCounters.legacyRefused);
  // Named: the servable state is the owed import, and no demand reached a build (the requester refuses an unservable
  // source before it positions one; a demand retained earlier would be refused by the gate's manifest term).
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LaneState::ImportOwed), CommittedManifestLane::gServable.state);
  const PlaybackBuildRefusal gate = t.playbackBuildGateRefusal(0, clockManager.getPlaybackDeadlineClock());
  TEST_ASSERT_TRUE(gate == PlaybackBuildRefusal::NoDemand || gate == PlaybackBuildRefusal::ManifestImportOwed);
  // The interrupt tick alone (DEC-241): no reader call at all, and nothing is built or owed to the merged owner.
  const uint32_t calls = reader().currentCalls;
  const uint32_t misses = PlaybackTickContext::gState.misses;
  for (uint32_t tick = kLength; tick < kLength + 64; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    PlaybackTickContext::gState.inInterrupt = true;
    t.playMidiEvents(tick, true);
    PlaybackTickContext::gState.inInterrupt = false;
  }
  TEST_ASSERT_EQUAL_UINT32(calls, reader().currentCalls);
  TEST_ASSERT_EQUAL_UINT32(misses, PlaybackTickContext::gState.misses);
  TEST_ASSERT_EQUAL_UINT8(0, PlaybackTickContext::gState.owed[0]);
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  // The owner prepares the manifest; the lane then serves it.
  PlaybackObservation served;
  play(t, kLength + 64, 4 * kLength, served);
  TEST_ASSERT_EQUAL_UINT32(1, reader().imports);
  TEST_ASSERT_GREATER_THAN_UINT32(0, served.manifestServedTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, noteOnsOf(served).size());
  for (const auto& [phase, pitch] : noteOnsOf(served)) TEST_ASSERT_TRUE(isTakeNote(phase, pitch));
  (void)loop;
}

// 3. Content that is not committed yet (a slot whose restore has not applied is the same state: no passes) is
//    `NoCommittedSource`; once production commits it the source key moves, the owner imports, and the lane serves it.
void test_c5_3_no_committed_source_then_imported_content_is_served() {
  installReader();
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  TEST_ASSERT_EQUAL(LaneState::NoCommittedSource, t.refreshManifestLane(0));
  TEST_ASSERT_EQUAL(PlaybackBuildRefusal::NoDemand, t.playbackBuildGateRefusal(0, clockManager.getPlaybackDeadlineClock()));
  Loop& loop = recordFourBarTake(t);
  TEST_ASSERT_EQUAL(LaneState::ImportOwed, t.refreshManifestLane(0));
  reader().serviceOwner(loop);
  TEST_ASSERT_EQUAL(LaneState::Ready, t.refreshManifestLane(0));
  TEST_ASSERT_EQUAL_UINT32(reader().identity(), CommittedManifestLane::gServable.identity);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 3 * kLength, obs);
  TEST_ASSERT_GREATER_THAN_UINT32(0, obs.manifestServedTicks);
  TEST_ASSERT_EQUAL_UINT32(0, obs.snapshotServedTicks + obs.snapshotCursorTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, noteOnsOf(obs).size());
  for (const auto& [phase, pitch] : noteOnsOf(obs)) TEST_ASSERT_TRUE(isTakeNote(phase, pitch));
}

// 4. The manifest moves while a window read is in progress: that read's candidate is retired by name and never
//    published; every window served afterwards carries the new identity.
void test_c5_3_a_manifest_move_during_preparation_never_publishes_the_stale_candidate() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  const uint32_t first = reader().identity();
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  bool moved = false;
  PlaybackObservation obs;
  play(t, 0, 3 * kLength, obs, 1, [&](uint32_t) {
    if (moved || CommittedManifestLane::gWindowOwner != &runtime ||
        runtime.coveragePreparation != LoopPlaybackRuntime::CoveragePreparation::Counting) return;
    TEST_ASSERT_EQUAL_UINT32(first, runtime.frozenViewRequest.source.manifestIdentity);
    reader().imported = false;  // the owner re-imports the same source: a new manifest identity, same content
    reader().serviceOwner(loop);
    moved = true;
  });
  TEST_ASSERT_TRUE_MESSAGE(moved, "the fixture needs a read in progress");
  const uint32_t second = reader().identity();
  TEST_ASSERT_NOT_EQUAL_UINT32(first, second);
  TEST_ASSERT_GREATER_THAN_UINT32(0, obs.manifestServedTicks);
  for (uint32_t identity : obs.servedIdentities)
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(second, identity, "a window read from the moved manifest was served");
  TEST_ASSERT_EQUAL_UINT32(0, obs.wrongIdentityTicks);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
      1, CommittedManifestLane::gCounters.owed[static_cast<uint8_t>(LaneState::SourceMoved)]);
}

// 5. The manifest has a hide and its edit index is owed: the lane names `IndexOwed`, reads nothing and never scans;
//    once the owner publishes the index the lane serves it, and the hidden note never sounds.
void test_c5_3_index_owed_then_ready_serves_the_hide_without_scanning() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  deleteTakeNote(loop, 62);
  reader().serviceOwner(loop);  // import only: the index is owed
  TEST_ASSERT_TRUE(reader().store.editIndexOwed());
  TEST_ASSERT_EQUAL(LaneState::IndexOwed, t.refreshManifestLane(0));
  startTransport(t);
  PlaybackObservation owed;
  play(t, 0, kLength, owed, /*ownerEvery=*/0);
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  TEST_ASSERT_EQUAL_UINT32(0, owed.sent.size());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LaneState::IndexOwed), CommittedManifestLane::gServable.state);
  PlaybackObservation served;
  play(t, kLength, 5 * kLength, served);
  TEST_ASSERT_GREATER_THAN_UINT32(0, reader().indexSteps);
  TEST_ASSERT_TRUE(reader().store.editIndexReady(reader().store.current()));
  TEST_ASSERT_GREATER_THAN_UINT32(0, served.manifestServedTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, noteOnsOf(served).size());
  for (const auto& [phase, pitch] : noteOnsOf(served)) {
    TEST_ASSERT_TRUE(isTakeNote(phase, pitch));
    TEST_ASSERT_NOT_EQUAL_UINT8(62, pitch);
  }
}

// 6. Both edit-index slots are held by pinned readers: the newest manifest's index is `IndexCapacityDenied`, named on
//    the lane as `CapacityDenied`, and neither pinned index is evicted. Releasing one pin lets the owner build it.
void test_c5_3_both_index_slots_pinned_is_capacity_denied_without_eviction() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  RevisionStore::Store<Cap>& store = reader().store;
  RevisionStore::ManifestId pinned[2] = {RevisionStore::kNoManifest, RevisionStore::kNoManifest};
  const uint8_t pitches[3] = {61, 62, 63};
  for (int i = 0; i < 3; ++i) {
    deleteTakeNote(loop, pitches[i]);
    reader().serviceOwner(loop);  // import
    if (i == 2) break;
    reader().serviceOwner(loop);  // index
    TEST_ASSERT_TRUE(store.editIndexReady(store.current()));
    pinned[i] = store.pin(store.current());
  }
  TEST_ASSERT_EQUAL(LaneState::CapacityDenied, t.refreshManifestLane(0));
  TEST_ASSERT_TRUE(store.editIndexReady(pinned[0]));
  TEST_ASSERT_TRUE(store.editIndexReady(pinned[1]));
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  store.unpin(pinned[0]);
  reader().serviceOwner(loop);
  TEST_ASSERT_EQUAL(LaneState::Ready, t.refreshManifestLane(0));
  TEST_ASSERT_TRUE(store.editIndexReady(pinned[1]));
  store.unpin(pinned[1]);
}

// 7. Production commits a change while a manifest window serves. The served copy stays immutable to its end (the
//    rolling contract); the source move is named, no window is begun on the old identity afterwards, and every later
//    window carries the new manifest, without the deleted note.
void test_c5_3_a_source_move_while_serving_cannot_publish_a_stale_window() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  const uint32_t first = reader().identity();
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  uint32_t movedAt = 0;
  size_t begunBefore = 0;
  PlaybackObservation obs;
  play(t, 0, 5 * kLength, obs, 1, [&](uint32_t tick) {
    if (movedAt != 0 || runtime.rolling.phase != LoopPlaybackRuntime::RollingPhase::Serving) return;
    deleteTakeNote(loop, 63);  // the key moves: ImportOwed until the owner's next turn imports it
    TEST_ASSERT_EQUAL(LaneState::ImportOwed, t.refreshManifestLane(0));
    begunBefore = reader().begunIdentities.size();
    movedAt = tick;
  });
  TEST_ASSERT_NOT_EQUAL_UINT32(0, movedAt);
  const uint32_t second = reader().identity();
  TEST_ASSERT_NOT_EQUAL_UINT32(first, second);
  TEST_ASSERT_GREATER_THAN_UINT32(begunBefore, reader().begunIdentities.size());
  for (size_t i = begunBefore; i < reader().begunIdentities.size(); ++i)
    TEST_ASSERT_EQUAL_UINT32(second, reader().begunIdentities[i]);
  TEST_ASSERT_EQUAL_UINT32(2, obs.servedIdentities.size());
  TEST_ASSERT_EQUAL_UINT32(first, obs.servedIdentities[0]);
  TEST_ASSERT_EQUAL_UINT32(second, obs.servedIdentities[1]);
  // Pitch 63 may sound only from the window that was already serving at the move (its immutable copy).
  for (const auto& [tick, e] : obs.sent) {
    if (!e.isNoteOn() || e.data.noteData.note != 63) continue;
    TEST_ASSERT_TRUE_MESSAGE(tick < movedAt + 2 * kBar, "the deleted note sounded from a window read after the move");
  }
}

// 8. A note the last served window left sounding, whose NoteOff falls in a routed-lane gap, is closed when the gap
//    begins (the gap is silent; its NoteOff is never emitted late or skipped).
void test_c5_3_a_note_sounding_into_a_gap_is_closed_not_left_hanging() {
  installReader();
  Track& t = trackManager.getTrack(0);
  constexpr uint32_t length = 5 * kBar;  // windows on the loop-local grid: 0, 1536, 3072 (terminal one bar)
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  t.noteOn(1, 71, 100, 2 * kBar - 120);  // each crosses a window boundary, so a window ends with it sounding
  t.noteOff(1, 71, 0, 2 * kBar + 150);
  t.noteOn(1, 70, 100, 4 * kBar - 120);
  t.noteOff(1, 70, 0, 4 * kBar + 150);
  clockManager.assignCurrentTickSilently(length);
  t.stopRecordingToStopped(length);
  driveStopToRetirement(t);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 6 * length, obs);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  int open70 = 0;  // both held notes, by pitch
  int open71 = 0;
  for (const auto& [tick, e] : obs.sent) {
    int* open = e.data.noteData.note == 70 ? &open70 : e.data.noteData.note == 71 ? &open71 : nullptr;
    if (open == nullptr) continue;
    if (e.isNoteOn()) ++*open;
    else if (e.isNoteOff() && *open > 0) --*open;
  }
  open70 += open71;
  TEST_ASSERT_GREATER_THAN_UINT32(0, obs.manifestServedTicks);
  int ons = 0, offs = 0;
  for (const auto& [tick, e] : obs.sent)
    if (e.data.noteData.note == 70 || e.data.noteData.note == 71) (e.isNoteOn() ? ons : offs) += 1;
  char counts[160];
  snprintf(counts, sizeof(counts), "ons=%d offs=%d open=%d closures=%u ledger=%zu", ons, offs, open70,
           CommittedManifestLane::gCounters.gapClosures, runtime.ledger.openCount());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, open70, counts);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, CommittedManifestLane::gCounters.gapClosures, counts);
  // The run ends inside a gap or a window; either way nothing the lane emitted is left open past a gap entry.
  TEST_ASSERT_TRUE(runtime.ledger.openCount() <= 1);
}

// 9. A one-bar slot's cold full-loop candidate prepared from the root before a reader was installed: once the slot is
//    routed, that snapshot ready is stale and cancelled, and the lane re-prepares it from the manifest.
void test_c5_3_a_snapshot_cold_ready_is_cancelled_once_the_slot_is_routed() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  clockManager.assignCurrentTickSilently(kBar);
  t.stopRecordingToStopped(kBar);
  driveStopToRetirement(t);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT32(kBar, loop.loopLengthTicks);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const auto readyView = [&]() -> const RuntimePlaybackView* {
    const auto* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr || set->lifecycle.ready >= set->views.size()) return nullptr;
    const auto& v = set->views[set->lifecycle.ready];
    return v.complete && !v.published ? &v : nullptr;
  };
  nativePlaybackDeadlineClock = {0, 0};
  const auto ownerTurn = [&]() {  // the cold owner shape before the first audible edge
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  };
  for (unsigned turn = 0; turn < 4096 && readyView() == nullptr; ++turn) ownerTurn();
  char state[160];
  snprintf(state, sizeof(state), "roll=%u ready=%u", unsigned(runtime.rolling.phase), readyView() != nullptr);
  TEST_ASSERT_NOT_NULL_MESSAGE(readyView(), state);
  TEST_ASSERT_NOT_NULL(readyView()->source.snapshotIdentity);  // prepared from the root
  installReader();
  reader().serviceOwner(loop);
  for (unsigned turn = 0; turn < 4096; ++turn) {
    ownerTurn();
    const RuntimePlaybackView* ready = readyView();
    if (ready != nullptr && ready->source.manifestIdentity != 0) break;
  }
  TEST_ASSERT_NOT_NULL(readyView());
  TEST_ASSERT_NULL(readyView()->source.snapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32(reader().identity(), readyView()->source.manifestIdentity);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_c5_3_current_boot_content_is_served_from_the_manifest_only);
  RUN_TEST(test_c5_3_owed_manifest_is_a_named_miss_and_the_tick_builds_nothing);
  RUN_TEST(test_c5_3_no_committed_source_then_imported_content_is_served);
  RUN_TEST(test_c5_3_a_manifest_move_during_preparation_never_publishes_the_stale_candidate);
  RUN_TEST(test_c5_3_index_owed_then_ready_serves_the_hide_without_scanning);
  RUN_TEST(test_c5_3_both_index_slots_pinned_is_capacity_denied_without_eviction);
  RUN_TEST(test_c5_3_a_source_move_while_serving_cannot_publish_a_stale_window);
  RUN_TEST(test_c5_3_a_note_sounding_into_a_gap_is_closed_not_left_hanging);
  RUN_TEST(test_c5_3_a_snapshot_cold_ready_is_cancelled_once_the_slot_is_routed);
  return UNITY_END();
}
