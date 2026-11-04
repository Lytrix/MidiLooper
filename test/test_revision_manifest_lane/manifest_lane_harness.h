// The C5.3 lane harness, shared by `test_revision_manifest_lane` and `test_rolling_successor_lead`: the shared
// finalization suite's real Track/Loop/storage closure with the rolling reader enabled, the host reader owner
// (`FixtureReader`) and the production deferred-owner lane shape. Include once, first, from a suite's .cpp.
#pragma once

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
  uint32_t densestCalls = 0;
  uint32_t densestWindowRows(uint32_t identity, uint32_t loopLength, uint32_t span) override {
    ++densestCalls;
    const RevisionStore::ManifestId m = store.current();
    if (m == RevisionStore::kNoManifest || store.identityOf(m) != identity || loopLength == 0) return 0;
    uint32_t densest = 0;
    for (uint32_t start = 0; start < loopLength; start += Config::TICKS_PER_BAR)
      densest = std::max(densest, store.rowsInRange(m, start, std::min(loopLength, start + span)));
    return densest;
  }
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

/// Test-only arbitration: while it answers true for a tick, the chosen holder's turn serves no one (the device lane's
/// withheld grant, applied by the fixture's own predicate instead of a fixed rate).
std::function<bool(uint32_t)> gWithholdLaneTurn;

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
    if (selection == MidiServiceDrain::PlaybackHolderSelection::Service && !(gWithholdLaneTurn && gWithholdLaneTurn(tick)))
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

/// `bars` bars on `t` slot 0, `notesPerBar` evenly spaced notes per bar (bar 0 also carries the recorder's own first
/// note), stopped to STOPPED with its Record job retired and its root published.
Loop& recordTake(Track& t, uint32_t bars, uint32_t notesPerBar) {
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  const uint32_t step = kBar / notesPerBar;
  const uint32_t hold = std::max<uint32_t>(6, step / 2);
  for (uint32_t b = 0; b < bars; ++b)
    for (uint32_t n = 0; n < notesPerBar; ++n) {
      const uint32_t on = b * kBar + n * step + 3;
      if (b == 0 && on < 100) continue;  // the recorder's own note owns bar 0's head
      const uint8_t pitch = static_cast<uint8_t>(61 + (b * notesPerBar + n) % 24);
      t.noteOn(1, pitch, 100, on);
      t.noteOff(1, pitch, 0, on + hold);
    }
  const uint32_t length = bars * kBar;
  clockManager.assignCurrentTickSilently(length);
  t.stopRecordingToStopped(length);
  driveStopToRetirement(t);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  return loop;
}

/// Production's Delete edit of the first committed note of `pitch` (as NOTE_EDIT commits it); the root is republished.
void hideFirstNoteOfPitch(Loop& loop, uint8_t pitch) {
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  const MidiEvent* on = nullptr;
  for (const MidiEvent& e : rows)
    if (e.isNoteOn() && e.data.noteData.note == pitch) { on = &e; break; }
  TEST_ASSERT_NOT_NULL_MESSAGE(on, "no committed NoteOn of that pitch");
  uint32_t off = on->tick + 1;
  for (const MidiEvent& e : rows)
    if (e.isNoteOff() && e.data.noteData.note == pitch && e.tick > on->tick) { off = e.tick; break; }
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.targetNoteId = on->noteId;
  row.startTick = on->tick;
  row.endTick = off;
  row.pitch = pitch;
  row.velocity = 100;
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, row));
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
  for (uint32_t grain = 0; grain < 4096 && loop.hasPendingSnapshotPublish(); ++grain) loop.stepPendingSnapshotPublish(1);
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
