// Native production input -> real clock -> TrackManager -> Capture/reader oracle.
// No firmware continuation or dispatch policy is implemented by this suite.
#define MIDI_INGRESS_MASK_QUALIFICATION 1
#include "../test_support/MidiIngressMaskFixture.h"
#if defined(UNIFIED_INPUT_CONTINUATION_ORACLE)
#define MIDI_INPUT_CONTINUATION_QUALIFICATION 1
#endif
#include <unity.h> // Keep Unity's C-linkage setUp/tearDown declarations outside compatibility renames.
#include "firmware/Arduino.h"
#include "ClockManager.h"
#include <cmath>
#define HARNESS_LINKS_REAL_CLOCK_MANAGER
#include "Utils/MidiInputContinuation.h"
#define HARNESS_LINKS_REAL_BAR_STEP_HANDLER
// Compile the existing real Track/Loop/input/reader fixture closure, retaining
// its original tests/registrations in their suite. Only its entry/reset names
// are changed here; dead stripping drops unrelated fixture functions.
PlaybackDeadlineClock nativePlaybackDeadlineClock{}; // Legacy fixture symbols only; real ClockManager never reads it.
#define main ownershipCompatibilityMain
#define setUp ownershipCompatibilitySetUp
#define tearDown ownershipCompatibilityTearDown
#include "../test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp"
#undef main
#undef setUp
#undef tearDown
#include "../../src/ClockSourceStateMachine.cpp"
#include "../../src/ClockManager.cpp"
#include "../../src/BarStepButtonHandler.cpp"
#if MIDI_INGRESS_RECEIPTS_ENABLED
#include "../../src/Utils/MidiIngressReceipt.cpp"
#endif

float bpm = 120.0f;
uint32_t ticksPerQuarterNote = Config::INTERNAL_PPQN;

void EditManager::selectClosestNote(Track&, uint32_t) { TEST_FAIL_MESSAGE("note-edit route is off path"); }
void ControlSurfaceManager::handleBarStepNoteEditGesture(Track&, const BarStepButtonInfo&, BarStepPressType) {
  TEST_FAIL_MESSAGE("bar/step gesture is off path");
}

namespace {
void push(uint8_t route, uint8_t type, uint8_t pitch = 0, uint8_t value = 0) {
#if MIDI_INGRESS_RECEIPTS_ENABLED
  NativeMidiPort::receive(route, {type, 1, pitch, value});
#else
  NativeMidiPort::routeInbox(route).push_back({type, 1, pitch, value});
#endif
}
void poll() { midiHandler.handleMidiInput(); }
void pollUntilTick(uint32_t expected, unsigned limit) {
  for (unsigned turn = 0; turn < limit && clockManager.getCurrentTick() < expected; ++turn) poll();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, clockManager.getCurrentTick(), "bounded real-input setup did not reach its specified tick");
}
void pulses(uint8_t route, unsigned count, uint32_t expected = UINT32_MAX) {
  TEST_ASSERT_TRUE(count <= 128);
  if (expected == UINT32_MAX) expected = clockManager.getCurrentTick() + count * Config::TICKS_PER_CLOCK;
  for (unsigned i = 0; i < count; ++i) push(route, midi::Clock);
  poll();
  pollUntilTick(expected, 2 * count + 16);
}
Track& beginRecord() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  track.forceSetState(TRACK_STOPPED);
  track.startRecording(0);
  TEST_ASSERT_TRUE(track.isRecording());
  return track;
}
void assertPair(const Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch) {
  TEST_ASSERT_EQUAL_UINT32(2, loop.capture.store.size());
  const auto& on = loop.capture.store.at(0);
  const auto& off = loop.capture.store.at(1);
  TEST_ASSERT_TRUE(on.isNoteOn() && off.isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(onTick, on.tick);
  TEST_ASSERT_EQUAL_UINT32(offTick, off.tick);
  TEST_ASSERT_EQUAL_UINT8(1, on.channel);
  TEST_ASSERT_EQUAL_UINT8(1, off.channel);
  TEST_ASSERT_EQUAL_UINT8(pitch, on.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(pitch, off.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(100, on.data.noteData.velocity);
  TEST_ASSERT_EQUAL_UINT8(0, off.data.noteData.velocity);
  TEST_ASSERT_NOT_EQUAL_UINT32(kInvalidNoteId, on.noteId);
  TEST_ASSERT_EQUAL_UINT32(on.noteId, off.noteId);
}
Track& beginRunningRecord(uint8_t route, uint32_t recordOrigin = 0) {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  push(route, midi::Start);
  poll();
  pulses(route, 1, 0);
  TEST_ASSERT_EQUAL_UINT32(0, recordOrigin % Config::TICKS_PER_CLOCK);
  while (clockManager.getCurrentTick() < recordOrigin) pulses(route, 1);
  TEST_ASSERT_TRUE(track.startRecording(recordOrigin));
  TEST_ASSERT_TRUE(track.isRecording());
  return track;
}
void queuedLatePair(uint8_t route, bool requireContinuation) {
  auto& track = beginRunningRecord(route);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  // The connected owner uses the real nonzero era, never an epoch-zero substitute.
  TEST_ASSERT_TRUE(clockManager.getPlaybackDeadlineClock().epoch != 0);
  unsigned grants = 0;
  for (unsigned i = 0; i < 64; ++i) {
    const auto result = track.stepPlaybackViewMaintenance(0, nullptr, false,
        clockManager.getPlaybackDeadlineClock());
    if (result == Track::PlaybackViewTurnResult::GrainServed || result == Track::PlaybackViewTurnResult::BuildComplete) ++grants;
  }
  const auto* views = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(views);
  const uint8_t current = views->lifecycle.current;
  TEST_ASSERT_TRUE(current < views->views.size());
  const auto& served = views->views[current];
  char prerequisite[220];
  snprintf(prerequisite, sizeof(prerequisite), "cold prerequisite route=%u grants=%u len=%u complete=%u published=%u raw=%u retained=%u ready=%u rolling=%u enabled=%u", route, grants, track.getLoop(0).loopLengthTicks, served.complete, served.published, served.rawCoveragePrepared, views->lifecycle.retained, views->lifecycle.ready, unsigned(runtime->rolling.phase), trackManager.isSlotEnabled(0, 0));
  TEST_ASSERT_TRUE_MESSAGE(served.complete && served.published && !served.rawCoveragePrepared, prerequisite);
  TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, served.source.origin);
  const uint64_t storage = served.storageGeneration;
  const uint64_t token = served.source.captureContentGeneration;
  TEST_ASSERT_EQUAL(static_cast<int>(CLOCK_EXTERNAL), static_cast<int>(clockManager.getClockSource()));
  pulses(route, 88);
  TEST_ASSERT_EQUAL_UINT32(704, clockManager.getCurrentTick());
  push(route, midi::NoteOn, 60, 100);
  poll();
  for (unsigned turn = 0; turn < 16 && track.getLoop(0).capture.store.size() < 1; ++turn) poll();
  TEST_ASSERT_EQUAL_UINT32(1, track.getLoop(0).capture.store.size());
  pulses(route, 4);
  TEST_ASSERT_EQUAL_UINT32(736, clockManager.getCurrentTick());
  const auto epoch = clockManager.getPlaybackDeadlineClock().epoch;
#if !defined(I5_REAL_INPUT_OMIT_LATE_OFF_CONTROL)
  push(route, midi::NoteOff, 60, 0);
#endif
  for (unsigned i = 0; i < 4; ++i) {
#if defined(I5_REAL_INPUT_OMIT_CLOCK_CONTROL)
    if (i == 3) continue; // Deliberately incomplete test stimulus, never production behavior.
#endif
    push(route, midi::Clock);
  }
  poll();
  // Empty scan segments are allowed; the boundary is the poll that actually
  // accepts the Off, never a copied input or assumed hardware-inbox extent.
  for (unsigned turn = 0; turn < 16 && track.getLoop(0).capture.store.size() < 2; ++turn) poll();
  assertPair(track.getLoop(0), 704, 736, 60);
  TEST_ASSERT_EQUAL_UINT32(epoch, clockManager.getPlaybackDeadlineClock().epoch);
  TEST_ASSERT_EQUAL_UINT8(current, views->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(storage, served.storageGeneration);
  TEST_ASSERT_EQUAL_UINT64(token, served.source.captureContentGeneration);
  TEST_ASSERT_EQUAL_UINT32(0, served.storage.eventCount);
  TEST_ASSERT_TRUE(token != track.getLoop(0).captureContentGeneration());
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  unsigned soundings = 0;
  runtime->ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++soundings; });
  TEST_ASSERT_EQUAL_UINT32(0, soundings);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving), static_cast<int>(runtime->rolling.phase));
  TEST_ASSERT_EQUAL_UINT8(current, runtime->rolling.boundView);
  TEST_ASSERT_EQUAL_UINT64(storage, runtime->rolling.boundStorageGeneration);
  if (requireContinuation) {
    char detail[180];
    snprintf(detail, sizeof(detail), "qualified input return: route=%u poll consumed queued Clocks before main checkpoint", route);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(736, clockManager.getCurrentTick(), detail);
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
    TEST_ASSERT_TRUE(midiHandler.qualificationInputContinuationReady());
    TEST_ASSERT_EQUAL_UINT16(4, midiHandler.qualificationInputPendingCount());
    const auto receipt = midiHandler.qualificationInputGeneration();
    const auto* storageIdentity = midiHandler.qualificationInputStorageIdentity();
    TEST_ASSERT_NOT_NULL(storageIdentity);
    for (unsigned query = 0; query < 4; ++query) {
      TEST_ASSERT_EQUAL_UINT16(4, midiHandler.qualificationInputPendingCount());
      TEST_ASSERT_EQUAL_UINT64(receipt, midiHandler.qualificationInputGeneration());
      TEST_ASSERT_EQUAL_PTR(storageIdentity, midiHandler.qualificationInputStorageIdentity());
      TEST_ASSERT_EQUAL_UINT32(736, clockManager.getCurrentTick());
    }
#endif
    // Retention may be in the owning continuation, not in the hardware queue.
    for (unsigned turn = 0; turn < 8 && clockManager.getCurrentTick() < 768; ++turn) poll();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(768, clockManager.getCurrentTick(), "bounded continuation drain must retain all four Clocks exactly once");
    TEST_ASSERT_EQUAL_UINT32(epoch, clockManager.getPlaybackDeadlineClock().epoch);
    assertPair(track.getLoop(0), 704, 736, 60);
    poll();
    TEST_ASSERT_EQUAL_UINT32(768, clockManager.getCurrentTick());
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
    TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
    TEST_ASSERT_EQUAL_PTR(storageIdentity, midiHandler.qualificationInputStorageIdentity());
#endif
  } else {
    TEST_ASSERT_EQUAL_UINT32(768, clockManager.getCurrentTick());
    TEST_ASSERT_EQUAL_UINT64(768, clockManager.getPlaybackDeadlineClock().tick);
    TEST_ASSERT_TRUE(NativeMidiPort::routeInbox(route).empty());
    TEST_ASSERT_FALSE(runtime->viewBuild.active());
  }
}
void usbBatch() { queuedLatePair(0, false); }
void dinBatch() { queuedLatePair(1, false); }
void hostBatch() { queuedLatePair(2, false); }
#ifdef UNIFIED_INPUT_CONTINUATION_ORACLE
void usbContinuation() { queuedLatePair(0, true); }
void dinContinuation() { queuedLatePair(1, true); }
void hostContinuation() { queuedLatePair(2, true); }
#endif
void transportFirst(uint8_t route) {
  auto& track = beginRecord();
  clockManager.assignCurrentTickSilently(64);
  push(route, midi::NoteOn, 62, 100);
  push(route, midi::Start);
  push(route, midi::Clock);
  push(route, midi::NoteOff, 62, 0);
  poll();
  for (unsigned turn = 0; turn < 16 && track.getLoop(0).capture.store.size() < 2; ++turn) poll();
  assertPair(track.getLoop(0), route == 2 ? 64 : 0, 0, 62);
  TEST_ASSERT_EQUAL_UINT32(0, clockManager.getCurrentTick());
}
void usbTransportFirst() { transportFirst(0); }
void dinTransportFirst() { transportFirst(1); }
void hostPreservesFifoBeforeStart() { transportFirst(2); }
void routeCap(uint8_t route) {
  beginRecord();
  sequencerRunning = true;
  clockManager.requestTransitionTo(CLOCK_EXTERNAL);
  clockManager.checkClockSource();
  for (unsigned i = 0; i < 129; ++i) push(route, midi::Clock);
  poll();
#ifdef UNIFIED_INPUT_CONTINUATION_ORACLE
  pollUntilTick(1024, 274);
#endif
  TEST_ASSERT_EQUAL_UINT32(1024, clockManager.getCurrentTick());
  TEST_ASSERT_EQUAL_UINT32(1, NativeMidiPort::routeInbox(route).size());
  for (uint8_t other = 0; other < 3; ++other)
    if (other != route) TEST_ASSERT_TRUE(NativeMidiPort::routeInbox(other).empty());
  poll();
  pollUntilTick(1032, 16);
  TEST_ASSERT_EQUAL_UINT32(1032, clockManager.getCurrentTick());
}
void usbCapDoesNotSpill() { routeCap(0); }
void dinCapDoesNotSpill() { routeCap(1); }
void hostCapDoesNotSpill() { routeCap(2); }
int boundaryPitch = 0;
unsigned boundaryPitchCalls = 0;
void hostPitchBoundaryIsSigned() {
  USBHost bus;
  MIDIDevice_BigBuffer port(bus);
  boundaryPitchCalls = 0;
  port.setHandlePitchChange([](uint8_t channel, int pitch) {
    TEST_ASSERT_EQUAL_UINT8(1, channel);
    boundaryPitch = pitch;
    ++boundaryPitchCalls;
  });
  const uint8_t low[] = {0, 0, 127};
  const uint8_t high[] = {0, 64, 127};
  const int expected[] = {-8192, 0, 8191};
  for (unsigned i = 0; i < 3; ++i) {
    port.pushMessage(midi::PitchBend, 1, low[i], high[i]);
    TEST_ASSERT_TRUE(port.read());
    TEST_ASSERT_EQUAL_INT(expected[i], boundaryPitch);
    TEST_ASSERT_EQUAL_UINT32(i + 1, boundaryPitchCalls);
  }
  TEST_ASSERT_FALSE(port.read());
  TEST_ASSERT_EQUAL_UINT32(3, boundaryPitchCalls);
}
}

#include "rolling_record_vertical.inc"
#include "rolling_record_coverage.inc"

#if !MIDI_INPUT_CONTINUATION_QUALIFICATION
void disabledModeHasNoContinuationState() {
  TEST_ASSERT_FALSE(midiHandler.qualificationInputContinuationReady());
  TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
  TEST_ASSERT_EQUAL_UINT64(0, midiHandler.qualificationInputGeneration());
  TEST_ASSERT_NULL(midiHandler.qualificationInputStorageIdentity());
  TEST_ASSERT_EQUAL_UINT32(0, midiHandler.qualificationInputStateBytes());
}
#endif
void helperBoundedScanAndFrozenCapacity() {
  MidiInputContinuation state;
  TEST_ASSERT_TRUE(state.begin(0, 33, 7, 1000));
  for (unsigned i = 0; i < 32; ++i) TEST_ASSERT_TRUE(state.append({midi::Clock, 0, 0, 0}, 1000 + i));
  TEST_ASSERT_TRUE(state.append({midi::Start, 0, 0, 0}, 1032));
  TEST_ASSERT_FALSE(state.append({midi::Clock, 0, 0, 0}, 1033));
  state.active = true;
  const auto receipt = state.generation;
  MidiInputContinuation::Row row{};
  TEST_ASSERT_FALSE(state.next(row));
  TEST_ASSERT_EQUAL_UINT16(16, state.cursor);
  TEST_ASSERT_FALSE(state.next(row));
  TEST_ASSERT_EQUAL_UINT16(32, state.cursor);
  TEST_ASSERT_TRUE(state.next(row));
  TEST_ASSERT_EQUAL_UINT8(midi::Start, row.type);
  TEST_ASSERT_FALSE(state.next(row)); // Unacknowledged dispatch owns this row.
  TEST_ASSERT_FALSE(state.begin(1, 32, 99, 2000));
  TEST_ASSERT_FALSE(state.append({midi::Clock, 0, 0, 0}, 2000));
  TEST_ASSERT_EQUAL_UINT64(receipt, state.generation);
  state.acknowledge();
  TEST_ASSERT_EQUAL_UINT16(32, state.pending());
  TEST_ASSERT_EQUAL_UINT8(33, state.frozenCap);
  TEST_ASSERT_EQUAL_UINT32(7, state.acquiredClockEpoch);
  for (unsigned i = 0; i < 32; ++i) {
    TEST_ASSERT_TRUE(state.next(row));
    TEST_ASSERT_EQUAL_UINT8(midi::Clock, row.type);
    state.acknowledge();
  }
  TEST_ASSERT_FALSE(state.active);
  TEST_ASSERT_EQUAL_UINT16(0, state.pending());
}
void helperGenerationExhaustionIsTerminal() {
  MidiInputContinuation state;
  state.generation = UINT64_MAX;
  TEST_ASSERT_FALSE(state.begin(0, 128, 1, 100));
  TEST_ASSERT_EQUAL_UINT64(0, state.generation);
  TEST_ASSERT_FALSE(state.append({midi::Clock, 0, 0, 0}, 100));
  state.resetForSetup();
  TEST_ASSERT_EQUAL_UINT64(0, state.generation);
  TEST_ASSERT_FALSE(state.begin(0, 128, 2, 200));
  TEST_ASSERT_FALSE(state.active);
}
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
void newerInputCannotOvertakeRetainedClocks() {
  auto& track = beginRecord();
  sequencerRunning = true;
  clockManager.requestTransitionTo(CLOCK_EXTERNAL);
  clockManager.checkClockSource();
  push(0, midi::NoteOn, 65, 100);
  push(0, midi::Clock);
  push(0, midi::Clock);
  poll();
  TEST_ASSERT_EQUAL_UINT32(1, track.getLoop(0).capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(0, track.getLoop(0).capture.store.at(0).tick);
  TEST_ASSERT_EQUAL_UINT16(2, midiHandler.qualificationInputPendingCount());
  const auto receipt = midiHandler.qualificationInputGeneration();
  push(1, midi::NoteOff, 65, 0);
  poll();
  TEST_ASSERT_EQUAL_UINT32(8, clockManager.getCurrentTick());
  TEST_ASSERT_EQUAL_UINT16(1, midiHandler.qualificationInputPendingCount());
  TEST_ASSERT_EQUAL_UINT64(receipt, midiHandler.qualificationInputGeneration());
  TEST_ASSERT_EQUAL_UINT32(1, NativeMidiPort::routeInbox(1).size());
  poll();
  TEST_ASSERT_EQUAL_UINT32(16, clockManager.getCurrentTick());
  TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
  for (unsigned turn = 0; turn < 16 && track.getLoop(0).capture.store.size() < 2; ++turn) poll();
  assertPair(track.getLoop(0), 0, 16, 65);
  TEST_ASSERT_TRUE(midiHandler.qualificationInputGeneration() != receipt);
}
void stopAndEpochChangeDoNotEraseCopiedInput() {
  auto& track = beginRecord();
  sequencerRunning = true;
  clockManager.requestTransitionTo(CLOCK_EXTERNAL);
  clockManager.checkClockSource();
  clockManager.assignCurrentTickSilently(64);
  const auto epoch = clockManager.getPlaybackDeadlineClock().epoch;
  push(0, midi::Clock);
  push(0, midi::Stop);
  poll();
  TEST_ASSERT_FALSE(clockManager.isTransportRunning());
  TEST_ASSERT_EQUAL_UINT16(1, midiHandler.qualificationInputPendingCount());
  TEST_ASSERT_EQUAL_UINT32(0, NativeMidiPort::routeInbox(0).size());
  TEST_ASSERT_FALSE(track.isRecording());
  TEST_ASSERT_EQUAL_UINT32(epoch, clockManager.getPlaybackDeadlineClock().epoch);
  TEST_ASSERT_EQUAL_UINT32(64, clockManager.getCurrentTick());
  clockManager.assignCurrentTickSilently(0); // Explicit position invalidation while Clock is retained.
  TEST_ASSERT_TRUE(clockManager.getPlaybackDeadlineClock().epoch != epoch);
  TEST_ASSERT_EQUAL_UINT16(1, midiHandler.qualificationInputPendingCount());
  const auto receipt = midiHandler.qualificationInputGeneration();
  poll();
  TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
  TEST_ASSERT_EQUAL_UINT64(receipt, midiHandler.qualificationInputGeneration());
  TEST_ASSERT_EQUAL_UINT32(0, clockManager.getCurrentTick()); // Stopped pulse is consumed, never advances transport.
}
void coldSetupReusesStorageWithoutRecyclingReceipt() {
  TEST_ASSERT_TRUE(midiHandler.qualificationInputContinuationReady());
  const auto* storage = midiHandler.qualificationInputStorageIdentity();
  const auto receipt = midiHandler.qualificationInputGeneration();
  TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
  midiHandler.setup(); // Explicit cold-session boundary, no acquired messages in this case.
  TEST_ASSERT_EQUAL_PTR(storage, midiHandler.qualificationInputStorageIdentity());
  TEST_ASSERT_TRUE(midiHandler.qualificationInputGeneration() > receipt);
  TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
  TEST_ASSERT_EQUAL_UINT32(sizeof(MidiInputContinuation), midiHandler.qualificationInputStateBytes());
}
void refusedPrewarmPreservesLegacyInputAndRecovers() {
  TEST_ASSERT_FALSE(midiHandler.qualificationInputContinuationReady());
  TEST_ASSERT_NULL(midiHandler.qualificationInputStorageIdentity());
  auto& track = beginRecord();
  sequencerRunning = true;
  clockManager.requestTransitionTo(CLOCK_EXTERNAL);
  clockManager.checkClockSource();
  push(0, midi::NoteOn, 66, 100);
  push(0, midi::Clock);
  push(0, midi::NoteOff, 66, 0);
  poll();
  assertPair(track.getLoop(0), 0, 8, 66);
  TEST_ASSERT_EQUAL_UINT32(8, clockManager.getCurrentTick());
  TEST_ASSERT_FALSE(midiHandler.qualificationInputContinuationReady());
  TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
  midiHandler.setQualificationInputPrewarmDeniedForTests(false);
  midiHandler.setup(); // Cold resource reprovision with no retained/pending input.
  TEST_ASSERT_TRUE(midiHandler.qualificationInputContinuationReady());
  push(0, midi::Clock);
  poll();
  pollUntilTick(16, 16);
  assertPair(track.getLoop(0), 0, 8, 66);
}
#endif

void checkpointUsesActualClockAndLiveWindowOffer() {
  using namespace MidiServiceDrain;
  auto& track = beginRecord();
  const auto stamp = clockManager.getPlaybackDeadlineClock();
  const auto offer = track.playbackWindowServiceOffer(0, stamp);
  TEST_ASSERT_TRUE(track.isRecording());
  TEST_ASSERT_TRUE(offer.eligible && offer.canProgress); // Actual retained live-Capture demand.
  DeferredIdleOwnerCursor normal{};
  InputCheckpointCursor checkpoint{};
  CheckpointCostTable table{};
  auto decision = selectInputCheckpointOwner(normal, checkpoint, offer.eligible && offer.canProgress,
      false, false, {}, stamp, {1000, 9}, table);
  TEST_ASSERT_FALSE(decision.dispatchOwner || decision.insertedWindow);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::Snapshot), unsigned(normal.owner));
  // Mock physical/cost boundary values below prove admission mechanics only,
  // never a real independent arrival producer or a deployment WCET.
  PlaybackPhysicalDeadline ref{stamp.tick, stamp.tick + 100, stamp.epoch, 9, 1000, 200, 5, true};
  for (auto& cost : table) cost = {10, 5, true};
  decision = selectInputCheckpointOwner(normal, checkpoint, offer.eligible && offer.canProgress,
      false, false, ref, stamp, {1000, 9}, table);
  TEST_ASSERT_TRUE(decision.dispatchOwner && decision.insertedWindow);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::PlaybackViewMaintenance), unsigned(decision.owner));
  const auto saved = normal;
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick());
  const auto rebased = clockManager.getPlaybackDeadlineClock();
  TEST_ASSERT_TRUE(rebased.epoch != stamp.epoch);
  decision = selectInputCheckpointOwner(normal, checkpoint, false, false, false,
      ref, rebased, {1000, 9}, table);
  TEST_ASSERT_FALSE(decision.dispatchOwner);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(decision.admission));
  TEST_ASSERT_EQUAL_UINT8(unsigned(saved.owner), unsigned(normal.owner));
  ref.clockEpoch = rebased.epoch;
  decision = selectInputCheckpointOwner(normal, checkpoint, false, false, false,
      ref, rebased, {1200, 9}, table);
  TEST_ASSERT_FALSE(decision.dispatchOwner);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PhysicalDeadlinePassed), unsigned(decision.admission));
  TEST_ASSERT_EQUAL_UINT32(0, clockManager.getCurrentTick()); // Lagging logical time cannot extend physical lead.
}

#if MIDI_INGRESS_RECEIPTS_ENABLED
struct IngressDispatchRow { byte type, channel, data1, data2; InputSource source; MidiIngressReceipt receipt; };
IngressDispatchRow ingressDispatchRows[3];
unsigned ingressDispatchCount = 0;
void observeIngressDispatch(byte type, byte channel, byte data1, byte data2, InputSource source, const MidiIngressReceipt& receipt) {
  TEST_ASSERT_TRUE(ingressDispatchCount < 3);
  ingressDispatchRows[ingressDispatchCount++] = {type, channel, data1, data2, source, receipt};
}
void assertIngressDispatch(uint8_t route, uint64_t observed, bool current = true) {
  const auto receipt = MidiIngress::lastDispatchedReceipt(route);
  TEST_ASSERT_TRUE(receipt.associated());
  TEST_ASSERT_EQUAL(current, receipt.current());
  TEST_ASSERT_EQUAL_UINT64(observed, receipt.observedUs);
  TEST_ASSERT_EQUAL_UINT32(17, receipt.clockEra);
}
void ingressPairing(uint8_t route) {
  ingressDispatchCount = 0;
  midiHandler.setIngressDispatchObserverForTests(observeIngressDispatch);
  inputReceiveTime = 100;
  push(route, midi::Clock);
  inputReceiveTime = 200;
  push(route, midi::Continue);
  inputReceiveTime = 300;
  push(route, midi::Clock);
  inputReceiveTime = 99999; // Poll/dispatch must never refresh receive observations.
  poll();
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
  assertIngressDispatch(route, route == 2 ? 100 : 200);
  TEST_ASSERT_EQUAL_UINT16(2, midiHandler.qualificationInputPendingCount());
  const auto* owner = MidiIngress::registeredOwner(route);
  midi_ingress_invalidate(route, owner); // Accepted payload copies survive; currency does not.
  poll();
  assertIngressDispatch(route, route == 2 ? 200 : 100, false);
  poll();
  assertIngressDispatch(route, 300, false);
  TEST_ASSERT_EQUAL_UINT16(0, midiHandler.qualificationInputPendingCount());
#else
  assertIngressDispatch(route, 300);
#endif
  TEST_ASSERT_EQUAL_UINT32(3, ingressDispatchCount);
  for (unsigned i = 0; i < 3; ++i) {
    const auto& row = ingressDispatchRows[i];
    const unsigned original = route == 2 || i == 2 ? i : 1 - i;
    TEST_ASSERT_EQUAL_UINT8(original == 1 ? midi::Continue : midi::Clock, row.type);
    TEST_ASSERT_EQUAL_UINT8(route == 2 ? 0 : 1, row.channel);
    TEST_ASSERT_EQUAL_UINT8(0, row.data1); TEST_ASSERT_EQUAL_UINT8(0, row.data2);
    TEST_ASSERT_EQUAL_UINT8(route, row.source);
    TEST_ASSERT_TRUE(row.receipt.associated());
    TEST_ASSERT_EQUAL_UINT64(100 * (original + 1), row.receipt.observedUs);
    TEST_ASSERT_EQUAL_UINT32(17, row.receipt.clockEra);
  }
  midiHandler.setIngressDispatchObserverForTests(nullptr);
}
void usbIngressPairing() { ingressPairing(0); }
void dinIngressPairing() { ingressPairing(1); }
void hostIngressPairing() { ingressPairing(2); }
#endif

#include "physical_clock_fixture.inc"
#include "../../src/ClockManagerPhysicalReference.cpp"
#include "../../src/ClockManagerPhysicalObservation.cpp"

#include "acquisition_record_fixture.inc"
void setUp() {
#if MIDI_INGRESS_RECEIPTS_ENABLED
  midiHandler.setIngressDispatchObserverForTests(nullptr);
#endif
  NativePhysicalClock::reset();
  clockManager.~ClockManager();
  new (&clockManager) ClockManager();
  sequencerRunning = false;
  ownershipCompatibilitySetUp();
  for (uint8_t route = 0; route < 3; ++route) NativeMidiPort::routeInbox(route).clear();
#if MIDI_INGRESS_RECEIPTS_ENABLED
  for (uint8_t route = 0; route < 3; ++route) NativeMidiPort::receiptSlots(route).clear();
#endif
#if defined(I5_INPUT_PREWARM_REFUSAL_PROFILE)
  midiHandler.setQualificationInputPrewarmDeniedForTests(true);
#else
  midiHandler.setQualificationInputPrewarmDeniedForTests(false);
#endif
  midiHandler.setup();
  midiHandler.beginUsbHost();
  MidiSendCapture::reset();
#if MIDI_INGRESS_RECEIPTS_ENABLED
  MidiIngress::setClockSampler(inputReceiveStamp);
  inputReceiveTime = 1000;
#endif
}
void tearDown() {}
int main(int, char**) {
  UNITY_BEGIN();
#if defined(I5_INPUT_PREWARM_REFUSAL_PROFILE)
  RUN_TEST(refusedPrewarmPreservesLegacyInputAndRecovers);
#else
#ifndef UNIFIED_INPUT_CONTINUATION_ORACLE
  RUN_TEST(usbBatch);
  RUN_TEST(dinBatch);
  RUN_TEST(hostBatch);
  RUN_TEST(disabledModeHasNoContinuationState);
#endif
  RUN_TEST(usbRollingRecordVertical);
  RUN_TEST(dinRollingRecordVertical);
  RUN_TEST(hostRollingRecordVertical);
  RUN_TEST(usbRollingRecordMatchingStop);
  RUN_TEST(rollingRecordEpochChangeCancelsPrivateBorrow);
  RUN_TEST(rollingRecordCoversLaterBarsAndRetainsOwedInterval);
  RUN_TEST(rollingRecordKeepsHeldIdentityAcrossLiveIntervals);
  RUN_TEST(rollingRecordMatchingStopKeepsSubintervalCurrent);
  RUN_TEST(rollingRecordMismatchingStopPreparesCorrectedGeometry);
  RUN_TEST(rollingRecordLateIntervalDoesNotAdoptMidTraversal);
  RUN_TEST(rollingRecordGrowthPreservesNonzeroRawOrigin);
  RUN_TEST(rollingRecordEmptyMiddleIntervalIsCompleteSilence);
  RUN_TEST(rollingRecordGrowthDoesNotRetriggerAlreadySoundingIdentity);
  RUN_TEST(rollingRecordProductionVisualTurnPreservesSuccessor);
  RUN_TEST(rollingRecordSecondTakeReclaimsCompletedIdleShell);
  RUN_TEST(rollingRecordLegacyVisualTurnBeforeFirstOwner);
  RUN_TEST(rollingRecordInitialPublicationBeforeClockKeepsNextDeadline);
  RUN_TEST(rollingRecordHeldMismatchStopGetsFrozenReader);
  RUN_TEST(rollingRecordHeldMismatchStopWithDeferredOwners);
  RUN_TEST(rollingRecordHeldStopHoldsOwedRootSuccessorAcrossTransportStop);
  RUN_TEST(privateRootReturnDemandDoesNotOutliveItsJobIntoNextTake);
  RUN_TEST(privateRootReturnClearWhileJobLiveThenNextTakeGrows);
  RUN_TEST(staleCommittedDemandDoesNotBlockNextTakeLiveFrame);
  RUN_TEST(privateRootReturnDemandIsOwedOnlyForTheBoundFrozenReader);
  RUN_TEST(refusedBindPreSealLiveCopyNamesTheSameTakeOnly);
  RUN_TEST(refusedBindUnextendedLiveCopyKeepsTheOlderToken);
  RUN_TEST(ordinaryAdmissionCannotServeShortLoopRootReturnWork);
  RUN_TEST(usbQueuedRecordPreparationRetriesAtRealClockBoundary);
  RUN_TEST(helperBoundedScanAndFrozenCapacity);
  RUN_TEST(helperGenerationExhaustionIsTerminal);
  RUN_TEST(usbTransportFirst);
  RUN_TEST(dinTransportFirst);
  RUN_TEST(hostPreservesFifoBeforeStart);
  RUN_TEST(usbCapDoesNotSpill);
  RUN_TEST(dinCapDoesNotSpill);
  RUN_TEST(hostCapDoesNotSpill);
  RUN_TEST(hostPitchBoundaryIsSigned);
  RUN_TEST(checkpointUsesActualClockAndLiveWindowOffer);
  RUN_TEST(physicalColdSetupAndElapsed);
  RUN_TEST(physicalResourceConflictsPreserveForeignTimers);
  RUN_TEST(physicalColdConfigurationRefuses);
  RUN_TEST(physicalMidiTimerFailureDoesNotQualify);
  RUN_TEST(physicalLatchRolloverAndMasks);
  RUN_TEST(physicalConfigurationFaultIsSticky);
  RUN_TEST(physicalWideExhaustionBeforeAndAfterRead);
  RUN_TEST(physicalLowOverflowAndBackwards);
  RUN_TEST(physicalEraInvalidationAndRequalification);
  RUN_TEST(physicalBootstrapInitializesNoLoadState);
  RUN_TEST(physicalMotionGuardRejectsPendingGeometryWithoutLiveCapture);
  RUN_TEST(physicalMotionGuardRejectsRealStopInFlight);
  RUN_TEST(physicalMotionGuardComposesEveryStorageAndClockDependency);
  RUN_TEST(physicalMotionGuardRejectsPendingSlotSwitch);
  RUN_TEST(physicalMotionReportsRealInternalAndExternalClock);
  RUN_TEST(physicalMotionGuardRejectsNonselectedCaptureAndPendingActions);
  RUN_TEST(physicalObservationBracketsAndPreservesMasks);
  RUN_TEST(physicalObservationRefusesFaultsAndDisabledCycles);
  RUN_TEST(physicalObservationReportsInvalidInputWithoutPeripheralWrites);
  RUN_TEST(physicalObservationNamesDebugRefusalWithoutClearingIt);
  RUN_TEST(physicalRunningDebugEnableDoesNotMeanHaltOrSleep);
#if MIDI_INGRESS_RECEIPTS_ENABLED
  RUN_TEST(usbIngressPairing);
  RUN_TEST(dinIngressPairing);
  RUN_TEST(hostIngressPairing);
#if MIDI_INGRESS_RECEIPTS_ENABLED
  RUN_TEST(completeDirectRefusalSkipsSamplerAndRecovers);
  RUN_TEST(completeDirectConfigurationFaultDoesNotBlessReceipt);
  RUN_TEST(completePacketHooksNestRealCounterAndPreserveIncomingMask);
  RUN_TEST(completeDirectHookNestRealCounterAndPreserveIncomingMask);
  RUN_TEST(completeHookNullAndRefusedMetadataDoNotReadPhysicalCounter);
  RUN_TEST(completeHookPhysicalFaultRestoresMaskWithoutBlessingReceipt);
  RUN_TEST(physicalIngressSamplerUsesProductionCounter);
#endif
#endif
#ifdef UNIFIED_INPUT_CONTINUATION_ORACLE
  RUN_TEST(usbContinuation);
  RUN_TEST(newerInputCannotOvertakeRetainedClocks);
  RUN_TEST(stopAndEpochChangeDoNotEraseCopiedInput);
  RUN_TEST(coldSetupReusesStorageWithoutRecyclingReceipt);
  RUN_TEST(dinContinuation);
  RUN_TEST(hostContinuation);
#endif
#endif // Ordinary/qualification suite versus isolated prewarm refusal profile.
  return UNITY_END();
}
