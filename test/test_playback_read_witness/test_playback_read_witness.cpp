#define PLAYBACK_WINDOW_ROLLING_ENABLED 1
#include "Utils/PlaybackReadWitness.h"
#include <cstddef>
namespace ReadRows {
inline PlaybackReadWitness last{};
inline size_t count = 0;
inline void record(const PlaybackReadWitness& row) { last = row; ++count; }
}
#define SC_PLAYBACK_READ_ROW(row) ReadRows::record(row)
#define main sharedSuiteMain
#define setUp sharedSuiteSetUp
#define tearDown sharedSuiteTearDown
#include "../test_shared_capture_finalization/test_shared_capture_finalization.cpp"
#undef main
#undef setUp
#undef tearDown
extern "C" void setUp() { sharedSuiteSetUp(); ReadRows::count = 0; }
extern "C" void tearDown() { sharedSuiteTearDown(); }

void test_real_reader_witnesses_silent_turn_without_crediting_preparation() {
  // Existing real admission -> publication -> production frozen preparation -> E2 adoption -> exact MIDI.
  assertRecordRecoveryServesSecondBar(false);
  Track& track = trackManager.getTrack(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(track, 0);
  TEST_ASSERT_GREATER_THAN_UINT32(0, ReadRows::count);
  TEST_ASSERT_EQUAL_UINT8(0, ReadRows::last.track);
  TEST_ASSERT_EQUAL_UINT8(0, ReadRows::last.slot);
  TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, ReadRows::last.origin);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, ReadRows::last.storageGeneration);
  const size_t before = ReadRows::count;
  (void)track.stepPlaybackViewMaintenance(3160, nullptr, true, PlaybackDeadlineClock{3160, 1u});
  TEST_ASSERT_EQUAL_UINT32(before, ReadRows::count);
  MidiSendCapture::reset();
  nativeMillisSet(1234);
  nativePlaybackDeadlineClock = {3180, 1u};
  clockManager.assignCurrentTickSilently(3180);
  track.playMidiEvents(3180, true);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count()); // no new MIDI due; reader still advanced
  TEST_ASSERT_EQUAL_UINT32(before + 1, ReadRows::count);
  TEST_ASSERT_EQUAL_UINT32(1234000, ReadRows::last.atUs);
  TEST_ASSERT_EQUAL_UINT32(3180, ReadRows::last.tick);
  TEST_ASSERT_TRUE(ReadRows::last.start <= ReadRows::last.phase);
  TEST_ASSERT_TRUE(ReadRows::last.phase - ReadRows::last.start < ReadRows::last.length);

  auto* set = runtime.viewSetIfPrewarmed();
  auto& current = set->views[set->lifecycle.current];
  const auto check = [&]() { track.observePlaybackReadForTurn(0, &current.storage); };
  const size_t valid = ReadRows::count;
  current.complete = false; check(); current.complete = true;
  current.published = false; check(); current.published = true;
  const auto generation = current.source.sourceGeneration;
  current.source.sourceGeneration = generation + 1; check(); current.source.sourceGeneration = generation;
  const auto storage = current.storageGeneration;
  current.storageGeneration = 0; check(); current.storageGeneration = storage;
  const auto coverage = current.coverage;
  current.coverage.startPhase = 768; check(); current.coverage = coverage;
  track.observePlaybackReadForTurn(0, nullptr);
  track.observePlaybackReadForTurn(0, &set->views[(set->lifecycle.current + 1) % set->views.size()].storage);
  TEST_ASSERT_EQUAL_UINT32(valid, ReadRows::count);
  track.playMidiEvents(3181, false);
  TEST_ASSERT_EQUAL_UINT32(valid, ReadRows::count);
}

void test_selected_prepared_pair_has_a_real_reader_witness() {
  Track& track = trackManager.getTrack(0);
  prepareSlot(track, 0, true);
  track.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(track);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(track, 0);
  auto& loop = track.getLoop(0);
  auto* set = runtime.viewSetIfPrewarmed();
  set->lifecycle.current = RuntimePlaybackViewLifecycle::kNoView;
  TEST_ASSERT_TRUE(runtime.prewarmPreparedPlaybackPair());
  const PlaybackViewSourceIdentity source{loop.playbackRevision,
      TrackCaptureStopTestAccess::playbackGenerationOf(track), loop.publishedSnapshotIdentity()};
  const MidiEvent events[] = {MidiEvent::NoteOn(12, 1, 60, 100), MidiEvent::NoteOff(96, 1, 60, 0)};
  const uint16_t order[] = {0, 1};
  TEST_ASSERT_TRUE(runtime.publishPreparedPlaybackPair(events, 2, order, 2,
      source, PlaybackCoverageInterval{0, 768, 768}, 100));
  loop.lastTickInLoop = 100;
  nativePlaybackDeadlineClock = {};
  clockManager.assignCurrentTickSilently(120);
  MidiSendCapture::reset();
  track.playMidiEvents(120, true);
  TEST_ASSERT_EQUAL_UINT32(1, ReadRows::count);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  TEST_ASSERT_EQUAL_UINT64(runtime.preparedPair.generation, ReadRows::last.storageGeneration);
  TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Committed, ReadRows::last.origin);
  TEST_ASSERT_EQUAL_UINT32(120, ReadRows::last.tick);

  // Guard-level bridge controls: only the exact complete selected compiled context can be credited.
  set->bridgeLifecycle.selected = 0;
  auto& bridge = set->bridges[0];
  bridge.source = source; bridge.coverage = {0, 768, 768}; bridge.storageGeneration = 91;
  bridge.complete = bridge.selected = bridge.streamPrepared = true;
  track.observePlaybackReadForTurn(0, nullptr, &bridge);
  TEST_ASSERT_EQUAL_UINT32(2, ReadRows::count);
  bridge.complete = false; track.observePlaybackReadForTurn(0, nullptr, &bridge); bridge.complete = true;
  bridge.selected = false; track.observePlaybackReadForTurn(0, nullptr, &bridge); bridge.selected = true;
  bridge.streamPrepared = false; track.observePlaybackReadForTurn(0, nullptr, &bridge); bridge.streamPrepared = true;
  set->bridgeLifecycle.selected = 1; track.observePlaybackReadForTurn(0, nullptr, &bridge);
  TEST_ASSERT_EQUAL_UINT32(2, ReadRows::count);
}

void test_current_snapshot_availability_is_not_claimed_as_a_completed_late_reader() {
  Track& track = trackManager.getTrack(0);
  prepareSlot(track, 0, false);
  track.forceSetState(TRACK_STOPPED);
  start(track, 0, false, 0, 60);
  track.noteOn(1, 62, 100, 780);
  track.noteOff(1, 62, 0, 1008);
  clockManager.assignCurrentTickSilently(1536);
  track.stopRecordingToStopped(1536);
  driveStopToRetirement(track);
  auto& loop = track.getLoop(0);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  track.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(track);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(track, 0);
  runtime.viewSetIfPrewarmed()->lifecycle.current = RuntimePlaybackViewLifecycle::kNoView;
  runtime.clearPreparedPlaybackPair();
  loop.lastTickInLoop = UINT32_MAX;
  clockManager.assignCurrentTickSilently(900);
  nativePlaybackDeadlineClock = {};
  MidiSendCapture::reset();
  track.playMidiEvents(900, true);
  TEST_ASSERT_NOT_NULL(runtime.mergedMidiEvents.builtFromSnapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32(1, ReadRows::count);
  TEST_ASSERT_TRUE(ReadRows::last.availableOnly);
  // The establishing turn **records** the phase: it is the one turn that emits nothing (no catch-up burst), and every
  // later turn is free to serve. Leaving the phase unknown here is what silenced the rest of the pass until the wrap
  // (measured 3.076 s on the qualification image, `captures/i5-dec143-sealed-extent-base-01`). The availability-is-not-
  // a-read contract this case exists for is the row assertion above, unchanged.
  TEST_ASSERT_EQUAL_UINT32(900, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  auto& cache = runtime.mergedMidiEvents;
  const auto identity = cache.builtFromSnapshotIdentity;
  cache.builtFromSnapshotIdentity = nullptr;
  track.observePlaybackSnapshotAvailability(0, 901);
  cache.builtFromSnapshotIdentity = identity;
  const auto revision = cache.builtFromRevision;
  cache.builtFromRevision = revision + 1;
  track.observePlaybackSnapshotAvailability(0, 901);
  cache.builtFromRevision = revision;
  const auto length = cache.windowLengthTicks;
  cache.windowLengthTicks = 768;
  track.observePlaybackSnapshotAvailability(0, 901);
  cache.windowLengthTicks = length;
  mergedMidiEventsBuildInProgress = true;
  track.observePlaybackSnapshotAvailability(0, 901);
  mergedMidiEventsBuildInProgress = false;
  TEST_ASSERT_EQUAL_UINT32(1, ReadRows::count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_real_reader_witnesses_silent_turn_without_crediting_preparation);
  RUN_TEST(test_selected_prepared_pair_has_a_real_reader_witness);
  RUN_TEST(test_current_snapshot_availability_is_not_claimed_as_a_completed_late_reader);
  return UNITY_END();
}
