// Hardware boundary only; tests below call actual ClockManager setup/read methods.
#include "Utils/ClockPhysicalReferencePlatform.h"
#include <array>
#include <vector>
namespace NativePhysicalClock {
using Register = ClockPhysicalReferencePlatform::Register;
std::array<uint32_t, static_cast<size_t>(Register::Count)> registers{};
std::vector<Register> reads, writes;
uint64_t ticks = 0, latched = UINT64_MAX;
uint32_t& primask = IngressMaskFixture::primask;
uint32_t cycleValue = 0, observationCpuHz = 600000000;
bool rolloverAfterHigh = false, overflowAfterLow = false, attemptCompetingLatch = false;
unsigned competingReads = 0, highReads = 0, lowReads = 0;
int midiChannel = -1;
bool valueReady = false;
uint32_t& reg(Register r) { return registers[static_cast<size_t>(r)]; }
void reset() {
  registers.fill(0); reg(Register::ClockSelect) = 0x40;
  reads.clear(); writes.clear(); ticks = 0; latched = UINT64_MAX; primask = 0;
  IngressMaskFixture::reset();
  cycleValue = UINT32_MAX - 50; observationCpuHz = 600000000;
  rolloverAfterHigh = overflowAfterLow = attemptCompetingLatch = false;
  competingReads = highReads = lowReads = 0; midiChannel = -1; valueReady = false;
  NativeIntervalTimer::beginAllowed = true; NativeIntervalTimer::beginCalls = 0;
  NativeIntervalTimer::beginHook = [] {
    // Simulated peripheral allocation boundary, not proof of the installed allocator.
    using R = Register;
    const R controls[] = {R::Control0, R::Control1, R::Control2, R::Control3};
    if (midiChannel >= 0) { reg(controls[midiChannel]) = 3; return true; }
    for (int n = 0; n < 4; ++n) if (reg(controls[n]) == 0) {
      midiChannel = n; reg(controls[n]) = 3; reg(R::Module) = 1; return true;
    }
    reg(R::Module) = 1; return false;
  };
}
}
namespace ClockPhysicalReferencePlatform {
uint32_t read(Register r) {
  using namespace NativePhysicalClock;
  reads.push_back(r);
  if (r == Register::LifetimeHigh) {
    TEST_ASSERT_EQUAL_UINT32(1, primask);
    ++highReads; latched = ~ticks;
    if (rolloverAfterHigh) ++ticks;
    if (attemptCompetingLatch && primask == 0) { latched = 0; ++competingReads; }
    return static_cast<uint32_t>(latched >> 32);
  }
  if (r == Register::LifetimeLow) {
    TEST_ASSERT_EQUAL_UINT32(1, primask);
    ++lowReads;
    if (overflowAfterLow) reg(Register::Flag1) = 1;
    return static_cast<uint32_t>(latched);
  }
  return reg(r);
}
void write(Register r, uint32_t v) {
  using namespace NativePhysicalClock;
  TEST_ASSERT_EQUAL_UINT32(1, primask);
  writes.push_back(r);
  if (r == Register::Flag0 || r == Register::Flag1) reg(r) &= ~v;
  else reg(r) = v;
  if (r == Register::Control0 && v == 1) {
    TEST_ASSERT_EQUAL_UINT32(5, reg(Register::Control1));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, reg(Register::Load0));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, reg(Register::Load1));
    ticks = 0;
  }
}
uint32_t maskInterrupts() { return IngressMaskFixture::enter(IngressMaskFixture::Kind::Physical); }
void restoreInterrupts(uint32_t old) { IngressMaskFixture::restore(IngressMaskFixture::Kind::Physical, old); }
uint32_t readCycleCounterForObservation() { NativePhysicalClock::cycleValue += 100; return NativePhysicalClock::cycleValue; }
uint32_t cpuClockHzForObservation() { return NativePhysicalClock::observationCpuHz; }
}

void physicalColdSetupAndElapsed() {
  using namespace NativePhysicalClock;
  using R = Register;
  TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  clockManager.setup();
  TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::Ready);
  TEST_ASSERT_EQUAL_INT(2, midiChannel);
  TEST_ASSERT_EQUAL_UINT32(1, reg(R::Control0)); TEST_ASSERT_EQUAL_UINT32(5, reg(R::Control1));
  TEST_ASSERT_EQUAL_UINT32(0, reg(R::Control3)); TEST_ASSERT_EQUAL_UINT32(1, reg(R::Module));
  TEST_ASSERT_EQUAL_UINT32(0, clockManager.getCurrentTick()); TEST_ASSERT_FALSE(sequencerRunning);
  const uint64_t inputs[] = {0, 23, 24, 25, (uint64_t(1) << 32) + 24};
  const uint64_t expected[] = {0, 0, 1, 1, 178956971};
  const auto era = clockManager.getPhysicalClockStamp().era;
  TEST_ASSERT_NOT_EQUAL_UINT32(0, era);
  for (unsigned n = 0; n < 5; ++n) {
    ticks = inputs[n]; const auto stamp = clockManager.getPhysicalClockStamp();
    TEST_ASSERT_EQUAL_UINT64(expected[n], stamp.microseconds); TEST_ASSERT_EQUAL_UINT32(era, stamp.era);
  }
  TEST_ASSERT_EQUAL_UINT32(0, primask);
}
void physicalResourceConflictsPreserveForeignTimers() {
  using namespace NativePhysicalClock;
  const Register controls[] = {Register::Control0, Register::Control1, Register::Control2, Register::Control3};
  for (auto control : controls) {
    reset(); clockManager.~ClockManager(); new (&clockManager) ClockManager();
    reg(control) = 7; reg(Register::Module) = 1; reg(Register::ClockGate) = 0x3000;
    clockManager.setup();
    TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::ResourceBusy);
    TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
    TEST_ASSERT_EQUAL_UINT32(7, reg(control));
    TEST_ASSERT_TRUE(writes.empty()); TEST_ASSERT_EQUAL_UINT32(1, NativeIntervalTimer::beginCalls);
  }
}
void physicalColdConfigurationRefuses() {
  using namespace NativePhysicalClock;
  for (uint32_t selector : {0u, 0x41u}) {
    reset(); clockManager.~ClockManager(); new (&clockManager) ClockManager();
    reg(Register::ClockSelect) = selector; clockManager.setup();
    TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::ConfigurationInvalid);
    TEST_ASSERT_TRUE(writes.empty()); TEST_ASSERT_EQUAL_UINT32(selector, reg(Register::ClockSelect));
  }
  reset(); clockManager.~ClockManager(); new (&clockManager) ClockManager();
  reg(Register::DebugControl) = 1u << 17; clockManager.setup();
  TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era); TEST_ASSERT_TRUE(writes.empty());
}
void physicalMidiTimerFailureDoesNotQualify() {
  NativeIntervalTimer::beginAllowed = false;
  clockManager.setup();
  TEST_ASSERT_EQUAL_UINT32(1, NativeIntervalTimer::beginCalls);
  TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::MidiTimerUnavailable);
  TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  TEST_ASSERT_EQUAL_UINT32(1, NativePhysicalClock::reg(NativePhysicalClock::Register::Control0));
}
void physicalLatchRolloverAndMasks() {
  using namespace NativePhysicalClock;
  clockManager.setup();
  ticks = (uint64_t(1) << 32) - 1; rolloverAfterHigh = attemptCompetingLatch = true;
  writes.clear(); reads.clear(); const auto before = highReads;
  auto stamp = clockManager.getPhysicalClockStamp();
  TEST_ASSERT_EQUAL_UINT64(178956970, stamp.microseconds);
  TEST_ASSERT_NOT_EQUAL_UINT32(0, stamp.era);
  TEST_ASSERT_EQUAL_UINT32(before + 1, highReads); TEST_ASSERT_EQUAL_UINT32(highReads, lowReads);
  TEST_ASSERT_EQUAL_UINT32(0, competingReads); TEST_ASSERT_EQUAL_UINT32(0, primask); TEST_ASSERT_TRUE(writes.empty());
  primask = 1; stamp = clockManager.getPhysicalClockStamp(); TEST_ASSERT_NOT_EQUAL_UINT32(0, stamp.era);
  TEST_ASSERT_EQUAL_UINT32(1, primask);
}
void physicalConfigurationFaultIsSticky() {
  using namespace NativePhysicalClock;
  const Register affected[] = {Register::Module, Register::ClockSelect, Register::ClockGate,
    Register::DebugControl, Register::Load0, Register::Load1, Register::Control0, Register::Control1};
  for (auto r : affected) {
    reset(); clockManager.~ClockManager(); new (&clockManager) ClockManager(); clockManager.setup();
    const auto original = reg(r); reg(r) = r == Register::DebugControl ? (1u << 18) : 0;
    TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
    TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::ConfigurationInvalid);
    reg(r) = original; TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  }
}
void physicalWideExhaustionBeforeAndAfterRead() {
  using namespace NativePhysicalClock;
  for (bool race : {false, true}) {
    reset(); clockManager.~ClockManager(); new (&clockManager) ClockManager(); clockManager.setup();
    if (race) overflowAfterLow = true; else reg(Register::Flag1) = 1;
    writes.clear(); TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
    TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::CounterExhausted);
    TEST_ASSERT_TRUE(writes.empty()); overflowAfterLow = false;
    reg(Register::Flag1) = 0; clockManager.setup();
    TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  }
}
void physicalLowOverflowAndBackwards() {
  using namespace NativePhysicalClock;
  clockManager.setup(); ticks = (uint64_t(1) << 32) + 24; reg(Register::Flag0) = 1;
  TEST_ASSERT_NOT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  ticks = 24; TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::CounterWentBackwards);
  ticks = (uint64_t(1) << 32) + 48; TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
}
void physicalEraInvalidationAndRequalification() {
  using namespace NativePhysicalClock;
  clockManager.setup(); ticks = 2400; auto old = clockManager.getPhysicalClockStamp();
  clockManager.invalidatePhysicalClockReference(); TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::Suspended);
  writes.clear(); clockManager.setup(); auto fresh = clockManager.getPhysicalClockStamp();
  TEST_ASSERT_EQUAL_UINT32(0, reg(Register::Control3));
  TEST_ASSERT_NOT_EQUAL_UINT32(0, fresh.era); TEST_ASSERT_NOT_EQUAL_UINT32(old.era, fresh.era);
  TEST_ASSERT_EQUAL_UINT64(100, fresh.microseconds); TEST_ASSERT_TRUE(writes.empty());
  clockManager.setPhysicalClockEraForTests(UINT32_MAX); clockManager.invalidatePhysicalClockReference();
  TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::EraExhausted);
  clockManager.setup(); TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
}
void physicalBootstrapInitializesNoLoadState() {
  clockManager.setPhysicalClockEraForTests(UINT32_MAX);
  clockManager.~ClockManager(); new (&clockManager) ClockManager();
  TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::NotStarted);
  clockManager.setup(); TEST_ASSERT_NOT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
}
#if MIDI_INGRESS_RECEIPTS_ENABLED
void physicalIngressSamplerUsesProductionCounter() {
  using namespace NativePhysicalClock;
  clockManager.setup(); ticks = 2400;
  NativeMidiPort::receive(0, {midi::Clock, 1, 0, 0}); midiHandler.handleMidiInput();
  const auto receipt = MidiIngress::lastDispatchedReceipt(0);
  TEST_ASSERT_TRUE(receipt.independentObservation()); TEST_ASSERT_EQUAL_UINT64(100, receipt.observedUs);
  TEST_ASSERT_EQUAL_UINT32(clockManager.getPhysicalClockStamp().era, receipt.clockEra);
  clockManager.invalidatePhysicalClockReference();
  NativeMidiPort::receive(0, {midi::Clock, 1, 0, 0}); midiHandler.handleMidiInput();
  TEST_ASSERT_FALSE(MidiIngress::lastDispatchedReceipt(0).independentObservation());
}
#endif

void physicalObservationBracketsAndPreservesMasks() {
  using namespace NativePhysicalClock;
  clockManager.setup();
  ticks = (uint64_t(1) << 32) - 1;
  rolloverAfterHigh = true;
  writes.clear();
  for (uint32_t incoming : {0u, 1u}) {
    primask = incoming;
    const auto observation = clockManager.observePhysicalClockForHitl();
    TEST_ASSERT_EQUAL_UINT32(incoming, primask);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PhysicalClockReferenceStatus::Ready), observation.status);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, observation.era);
    TEST_ASSERT_EQUAL_UINT32(0x27f, observation.configBits);
    TEST_ASSERT_EQUAL_UINT32(100, observation.readCycles);
    TEST_ASSERT_EQUAL_UINT32(600000000, observation.cpuHz);
    TEST_ASSERT_TRUE(observation.beforeUs <= observation.rawTicks / 24u);
    TEST_ASSERT_TRUE(observation.rawTicks / 24u <= observation.beforeUs + observation.widthUs);
    const auto musical = clockManager.getPlaybackDeadlineClock();
    TEST_ASSERT_EQUAL_UINT64(musical.tick, observation.musicalTick);
    TEST_ASSERT_EQUAL_UINT32(musical.epoch, observation.epoch);
    TEST_ASSERT_FALSE(observation.transport);
    TEST_ASSERT_TRUE(writes.empty());
  }
  TEST_ASSERT_EQUAL_UINT32(highReads, lowReads);
}
void physicalObservationRefusesFaultsAndDisabledCycles() {
  using namespace NativePhysicalClock;
  clockManager.setup();
  observationCpuHz = 0;
  auto observation = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_NOT_EQUAL_UINT32(0, observation.era);
  TEST_ASSERT_EQUAL_UINT32(0, observation.readCycles);
  TEST_ASSERT_EQUAL_UINT32(0, observation.cpuHz);
  reg(Register::Flag1) = 1;
  writes.clear();
  observation = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_EQUAL_UINT32(0, observation.era);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PhysicalClockReferenceStatus::CounterExhausted), observation.status);
  TEST_ASSERT_TRUE(writes.empty());
  TEST_ASSERT_EQUAL_UINT32(0, primask);
}

void physicalObservationReportsInvalidInputWithoutPeripheralWrites() {
  using namespace NativePhysicalClock;
  reg(Register::ClockSelect) = 0;
  clockManager.setup();
  writes.clear();
  const auto observation = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PhysicalClockReferenceStatus::ConfigurationInvalid), observation.status);
  TEST_ASSERT_EQUAL_UINT32(0, observation.era);
  TEST_ASSERT_EQUAL_UINT32(0, observation.configBits & 64u);
  TEST_ASSERT_TRUE((observation.configBits & 512u) != 0);
  TEST_ASSERT_TRUE(writes.empty());
}

void physicalObservationNamesDebugRefusalWithoutClearingIt() {
  using namespace NativePhysicalClock;
  reg(Register::DebugControl) = 1u | (1u << 17) | (1u << 18);
  clockManager.setup();
  writes.clear();
  const auto observation = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_EQUAL_UINT32(0, observation.era);
  TEST_ASSERT_EQUAL_UINT32(7168u, observation.configBits & 7680u);
  TEST_ASSERT_EQUAL_UINT32(1u | (1u << 17) | (1u << 18), reg(Register::DebugControl));
  TEST_ASSERT_TRUE(writes.empty());
}

void physicalRunningDebugEnableDoesNotMeanHaltOrSleep() {
  using namespace NativePhysicalClock;
  reg(Register::DebugControl) = 1;
  clockManager.setup();
  TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::Ready);
  TEST_ASSERT_NOT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
  TEST_ASSERT_EQUAL_UINT32(1, reg(Register::DebugControl));
  const auto observation = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_EQUAL_UINT32(0x67f, observation.configBits);
  for (uint32_t haltOrSleep : {1u << 17, 1u << 18}) {
    reset(); clockManager.~ClockManager(); new (&clockManager) ClockManager();
    reg(Register::DebugControl) = 1u | haltOrSleep;
    clockManager.setup();
    TEST_ASSERT_TRUE(clockManager.physicalClockReferenceStatus() == PhysicalClockReferenceStatus::ConfigurationInvalid);
    TEST_ASSERT_EQUAL_UINT32(0, clockManager.getPhysicalClockStamp().era);
    TEST_ASSERT_TRUE(writes.empty());
  }
}

void physicalMotionReportsRealInternalAndExternalClock() {
  using namespace NativePhysicalClock;
  clockManager.setup();
  clockManager.setBpmFloat(120.0f);
  sequencerRunning = true;
  ticks = 24000000;
  clockManager.updateInternalClock();
  auto first = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_TRUE(first.transport);
  TEST_ASSERT_EQUAL_UINT8(CLOCK_INTERNAL, first.source);
  TEST_ASSERT_EQUAL_UINT64(1, first.musicalTick);
  TEST_ASSERT_EQUAL_UINT32(2604, first.periodUs);
  ticks += 24000000;
  clockManager.updateInternalClock();
  const auto second = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_EQUAL_UINT64(2, second.musicalTick);
  TEST_ASSERT_EQUAL_UINT32(first.epoch, second.epoch);
  TEST_ASSERT_EQUAL_UINT32(first.era, second.era);
  TEST_ASSERT_TRUE(second.rawTicks > first.rawTicks);
  clockManager.onMidiStart();
  clockManager.onMidiClockPulse();
  clockManager.onMidiClockPulse();
  first = clockManager.observePhysicalClockForHitl();
  TEST_ASSERT_EQUAL_UINT8(CLOCK_EXTERNAL, first.source);
  TEST_ASSERT_EQUAL_UINT64(8, first.musicalTick);
  TEST_ASSERT_NOT_EQUAL_UINT32(second.epoch, first.epoch);
  TEST_ASSERT_TRUE(first.periodUs > 0);
  TEST_ASSERT_EQUAL_UINT32(0, primask);
}

void physicalMotionGuardRejectsNonselectedCaptureAndPendingActions() {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    trackManager.getTrack(i).setState(TRACK_EMPTY);
    trackManager.getTrack(i).clearQueuedPlaybackStart();
    trackManager.cancelPendingRecordArm(i);
  }
  TEST_ASSERT_EQUAL_UINT8(3, trackManager.physicalMotionGuardForHitl());
  auto& other = trackManager.getTrack(Config::NUM_TRACKS - 1);
  TEST_ASSERT_TRUE(other.setState(TRACK_ARMED));
  TEST_ASSERT_EQUAL_UINT8(0, trackManager.physicalMotionGuardForHitl());
  trackManager.cancelPendingRecordArm(Config::NUM_TRACKS - 1);
  TEST_ASSERT_TRUE(other.setState(TRACK_ARMED));
  TEST_ASSERT_TRUE(other.setState(TRACK_STOPPED));
  TEST_ASSERT_TRUE(other.setState(TRACK_PLAYING));
  TEST_ASSERT_EQUAL_UINT8(1, trackManager.physicalMotionGuardForHitl());
  TEST_ASSERT_TRUE(other.setState(TRACK_STOPPED));
  other.queuePlaybackStartAtGrid(0, 0);
  TEST_ASSERT_EQUAL_UINT8(1, trackManager.physicalMotionGuardForHitl());
  other.clearQueuedPlaybackStart();
  trackManager.queueRecordingTrack(Config::NUM_TRACKS - 1, 0);
  TEST_ASSERT_EQUAL_UINT8(0, trackManager.physicalMotionGuardForHitl());
  trackManager.cancelPendingRecordArm(Config::NUM_TRACKS - 1);
  TEST_ASSERT_EQUAL_UINT8(3, trackManager.physicalMotionGuardForHitl());
  trackManager.queueStopRecordingTrack(Config::NUM_TRACKS - 1);
  TEST_ASSERT_EQUAL_UINT8(1, trackManager.physicalMotionGuardForHitl());
  trackManager.handleTransportStop();
  TEST_ASSERT_EQUAL_UINT8(3, trackManager.physicalMotionGuardForHitl());
}

#include "../../src/StorageManager/HitlMotionGuard.h"
namespace {
struct MotionStorageScalars {
  static uint16_t busy;
  static bool isDeferredSaveActive() { return busy & 1; }
  static bool hasDeferredSaveWork() { return busy & 2; }
  static bool hasActivePersistenceWorkItem() { return busy & 4; }
  static bool hasQueuedCaptureChunksToPersist() { return busy & 8; }
  static bool hasPendingLoopSlotRestore() { return busy & 16; }
  static bool hasPendingUndoSnapshotHydrate() { return busy & 32; }
  static bool isRevisionCommitActive() { return busy & 64; }
  static bool isRevisionLoadActive() { return busy & 128; }
};
uint16_t MotionStorageScalars::busy = 0;
}
void physicalMotionGuardComposesEveryStorageAndClockDependency() {
  using namespace StorageManagerInternal;
  MotionStorageScalars::busy = 0;
  TEST_ASSERT_TRUE(physicalMotionStorageIdleForHitl<MotionStorageScalars>(false));
  TEST_ASSERT_EQUAL_UINT32(15, physicalMotionGuardBitsForHitl(3, false, true));
  clockManager.pendingStart = true;
  TEST_ASSERT_EQUAL_UINT32(7, physicalMotionGuardBitsForHitl(3, clockManager.pendingStart, true));
  clockManager.pendingStart = false;
  for (uint16_t flag = 1; flag <= 128; flag <<= 1) {
    MotionStorageScalars::busy = flag;
    const bool idle = physicalMotionStorageIdleForHitl<MotionStorageScalars>(false);
    TEST_ASSERT_FALSE(idle);
    TEST_ASSERT_EQUAL_UINT32(11, physicalMotionGuardBitsForHitl(3, clockManager.pendingStart, idle));
  }
  MotionStorageScalars::busy = 0;
  TEST_ASSERT_FALSE(physicalMotionStorageIdleForHitl<MotionStorageScalars>(true));
}

void physicalMotionGuardRejectsPendingSlotSwitch() {
  const uint8_t other = Config::NUM_TRACKS - 1;
  trackManager.requestSlotSwitch(other, 1, SlotQuantization::LoopEnd, 0);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(other));
  TEST_ASSERT_EQUAL_UINT8(0, trackManager.physicalMotionGuardForHitl() & 2);
  trackManager.clearPendingSlotSwitch(other);
  TEST_ASSERT_FALSE(trackManager.hasPendingSlotSwitch(other));
  TEST_ASSERT_EQUAL_UINT8(2, trackManager.physicalMotionGuardForHitl() & 2);
}

void physicalMotionGuardRejectsRealStopInFlight() {
  auto& track = beginRecord();
  inputNote(62, 12, 96);
  track.stopRecordingToStopped(768);
  TEST_ASSERT_TRUE(track.hasStopInFlight());
  TEST_ASSERT_EQUAL_UINT8(0, trackManager.physicalMotionGuardForHitl() & 2);
}

void physicalMotionGuardRejectsPendingGeometryWithoutLiveCapture() {
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::retainGeometryForMotionTest(0, 0);
  TEST_ASSERT_FALSE(track.hasStopInFlight());
  TEST_ASSERT_EQUAL_UINT8(1, trackManager.physicalMotionGuardForHitl());
  trackManager.servicePendingRecordStopGeometry();
  TEST_ASSERT_EQUAL_UINT8(3, trackManager.physicalMotionGuardForHitl());
}

#if MIDI_INGRESS_RECEIPTS_ENABLED
namespace {
unsigned hookSamplerCalls = 0;
PhysicalClockStamp completeHookPhysicalSampler() {
  TEST_ASSERT_EQUAL_UINT32(1, IngressMaskFixture::primask);
  ++hookSamplerCalls;
  return clockManager.getPhysicalClockStamp();
}
void expectCompleteHookMask(uint32_t incoming, bool nested,
                            IngressMaskFixture::Kind inner = IngressMaskFixture::Kind::Physical) {
  using namespace IngressMaskFixture;
  TEST_ASSERT_EQUAL_UINT32(nested ? 4 : 2, events.size());
  TEST_ASSERT_TRUE(events.front().kind == Kind::Ingress && events.front().enter);
  TEST_ASSERT_EQUAL_UINT32(incoming, events.front().before);
  TEST_ASSERT_EQUAL_UINT32(1, events.front().after);
  if (nested) {
    TEST_ASSERT_TRUE(events[1].kind == inner && events[1].enter);
    TEST_ASSERT_EQUAL_UINT32(1, events[1].before);
    TEST_ASSERT_EQUAL_UINT32(1, events[1].after);
    TEST_ASSERT_TRUE(events[2].kind == inner && !events[2].enter);
    TEST_ASSERT_EQUAL_UINT32(1, events[2].before);
    TEST_ASSERT_EQUAL_UINT32(1, events[2].after);
  }
  TEST_ASSERT_TRUE(events.back().kind == Kind::Ingress && !events.back().enter);
  TEST_ASSERT_EQUAL_UINT32(1, events.back().before);
  TEST_ASSERT_EQUAL_UINT32(incoming, events.back().after);
  TEST_ASSERT_EQUAL_UINT32(incoming, primask);
}
}
void completePacketHooksNestRealCounterAndPreserveIncomingMask() {
  clockManager.setup(); NativePhysicalClock::ticks = 2400;
  MidiIngress::setClockSampler(completeHookPhysicalSampler);
  const uint32_t capacities[] = {6, 64, 400};
  for (uint8_t route = 0; route < 3; ++route) for (uint32_t incoming : {0u, 1u}) {
    const void* owner = MidiIngress::registeredOwner(route);
    MidiIngress::setCountersForTests(route, 40, 100);
    NativePhysicalClock::reads.clear(); NativePhysicalClock::writes.clear();
    hookSamplerCalls = 0; IngressMaskFixture::reset(incoming);
    midi_ingress_packet(route, owner, capacities[route], 0);
    expectCompleteHookMask(incoming, true);
    TEST_ASSERT_EQUAL_UINT32(1, hookSamplerCalls);
    TEST_ASSERT_EQUAL_UINT32(20, NativePhysicalClock::reads.size());
    TEST_ASSERT_TRUE(NativePhysicalClock::writes.empty());
    midi_ingress_publish(route, owner, 0, 2, 1);
    midi_ingress_consume(route, owner, 0, 0);
    IngressMaskFixture::reset(incoming);
    const auto receipt = MidiIngress::lastReceipt(route);
    expectCompleteHookMask(incoming, true, IngressMaskFixture::Kind::Ingress);
    TEST_ASSERT_TRUE(receipt.independentObservation());
    TEST_ASSERT_EQUAL_UINT64(100, receipt.observedUs);
    TEST_ASSERT_EQUAL_UINT64(101, receipt.receiveSequence);
    TEST_ASSERT_EQUAL_UINT32(40, receipt.queueGeneration);
    TEST_ASSERT_EQUAL_UINT16(2, receipt.eventOffset);
    IngressMaskFixture::reset(incoming);
    MidiIngress::noteDispatch(route, receipt);
    expectCompleteHookMask(incoming, true, IngressMaskFixture::Kind::Ingress);
    IngressMaskFixture::reset(incoming);
    TEST_ASSERT_TRUE(MidiIngress::lastDispatchedReceipt(route).independentObservation());
    expectCompleteHookMask(incoming, true, IngressMaskFixture::Kind::Ingress);
    TEST_ASSERT_EQUAL_UINT32(1, hookSamplerCalls);
    TEST_ASSERT_EQUAL_UINT32(incoming, IngressMaskFixture::primask);
  }
}
void completeDirectHookNestRealCounterAndPreserveIncomingMask() {
  clockManager.setup(); NativePhysicalClock::ticks = 4800;
  MidiIngress::setClockSampler(completeHookPhysicalSampler);
  const void* owner = MidiIngress::registeredOwner(1);
  for (uint32_t incoming : {0u, 1u}) {
    NativePhysicalClock::reads.clear(); hookSamplerCalls = 0;
    IngressMaskFixture::reset(incoming);
    midi_ingress_direct(1, owner);
    expectCompleteHookMask(incoming, true);
    TEST_ASSERT_EQUAL_UINT32(1, hookSamplerCalls);
    TEST_ASSERT_EQUAL_UINT32(20, NativePhysicalClock::reads.size());
    const auto receipt = MidiIngress::lastReceipt(1);
    TEST_ASSERT_TRUE(receipt.independentObservation());
    TEST_ASSERT_TRUE(receipt.flags & MidiIngressReceipt::LateHardwareRead);
    TEST_ASSERT_EQUAL_UINT64(200, receipt.observedUs);
    TEST_ASSERT_EQUAL_UINT32(incoming, IngressMaskFixture::primask);
  }
}
void completeHookNullAndRefusedMetadataDoNotReadPhysicalCounter() {
  clockManager.setup();
  MidiIngress::setClockSampler(nullptr);
  for (uint32_t incoming : {0u, 1u}) {
    NativePhysicalClock::reads.clear(); IngressMaskFixture::reset(incoming);
    midi_ingress_packet(0, nullptr, 6, 0);
    expectCompleteHookMask(incoming, false);
    TEST_ASSERT_TRUE(NativePhysicalClock::reads.empty());
    midi_ingress_publish(0, nullptr, 0, 0, 1); midi_ingress_consume(0, nullptr, 0, 0);
    TEST_ASSERT_TRUE(MidiIngress::lastReceipt(0).clock == MidiIngressReceipt::Clock::RawMicros32);
  }
  MidiIngress::setClockSampler(completeHookPhysicalSampler);
  for (uint32_t incoming : {0u, 1u}) {
    NativePhysicalClock::reads.clear(); hookSamplerCalls = 0; IngressMaskFixture::reset(incoming);
    midi_ingress_packet(0, nullptr, 7, 0);
    expectCompleteHookMask(incoming, false);
    TEST_ASSERT_EQUAL_UINT32(0, hookSamplerCalls);
    TEST_ASSERT_TRUE(NativePhysicalClock::reads.empty());
    midi_ingress_reset(0, nullptr, 6);
  }
}
void completeHookPhysicalFaultRestoresMaskWithoutBlessingReceipt() {
  clockManager.setup(); MidiIngress::setClockSampler(completeHookPhysicalSampler);
  NativePhysicalClock::reg(ClockPhysicalReferencePlatform::Register::Flag1) = 1;
  for (uint32_t incoming : {0u, 1u}) {
    NativePhysicalClock::writes.clear(); hookSamplerCalls = 0; IngressMaskFixture::reset(incoming);
    midi_ingress_packet(0, nullptr, 6, 0);
    expectCompleteHookMask(incoming, true);
    TEST_ASSERT_EQUAL_UINT32(1, hookSamplerCalls);
    TEST_ASSERT_TRUE(NativePhysicalClock::writes.empty());
    midi_ingress_publish(0, nullptr, 0, 0, 1); midi_ingress_consume(0, nullptr, 0, 0);
    const auto receipt = MidiIngress::lastReceipt(0);
    TEST_ASSERT_TRUE(receipt.associated() && receipt.current());
    TEST_ASSERT_FALSE(receipt.independentObservation());
    TEST_ASSERT_EQUAL_UINT32(0, receipt.clockEra);
  }
}
#endif

#if MIDI_INGRESS_RECEIPTS_ENABLED
void completeDirectRefusalSkipsSamplerAndRecovers() {
  clockManager.setup(); MidiIngress::setClockSampler(completeHookPhysicalSampler);
  const void* owner = MidiIngress::registeredOwner(1);
  char foreign;
  for (uint32_t incoming : {0u, 1u}) for (unsigned refusal = 0; refusal < 3; ++refusal) {
    MidiIngress::setCountersForTests(1, 40, refusal == 2 ? UINT64_MAX : 100);
    midi_ingress_reset(1, owner, refusal == 1 ? 65 : 64);
    hookSamplerCalls = 0; NativePhysicalClock::reads.clear(); IngressMaskFixture::reset(incoming);
    midi_ingress_direct(1, refusal == 0 ? &foreign : owner);
    expectCompleteHookMask(incoming, false);
    TEST_ASSERT_EQUAL_UINT32(0, hookSamplerCalls);
    TEST_ASSERT_TRUE(NativePhysicalClock::reads.empty());
    MidiIngress::setCountersForTests(1, 40, 100); midi_ingress_reset(1, owner, 64);
    hookSamplerCalls = 0; IngressMaskFixture::reset(incoming);
    midi_ingress_direct(1, owner); expectCompleteHookMask(incoming, true);
    TEST_ASSERT_EQUAL_UINT32(1, hookSamplerCalls);
    TEST_ASSERT_TRUE(MidiIngress::lastReceipt(1).independentObservation());
  }
}
void completeDirectConfigurationFaultDoesNotBlessReceipt() {
  clockManager.setup(); MidiIngress::setClockSampler(completeHookPhysicalSampler);
  NativePhysicalClock::reg(ClockPhysicalReferencePlatform::Register::Control1) = 1;
  const void* owner = MidiIngress::registeredOwner(1);
  for (uint32_t incoming : {0u, 1u}) {
    hookSamplerCalls = 0; NativePhysicalClock::writes.clear(); IngressMaskFixture::reset(incoming);
    midi_ingress_direct(1, owner); expectCompleteHookMask(incoming, true);
    TEST_ASSERT_EQUAL_UINT32(1, hookSamplerCalls);
    TEST_ASSERT_TRUE(NativePhysicalClock::writes.empty());
    const auto receipt = MidiIngress::lastReceipt(1);
    TEST_ASSERT_TRUE(receipt.associated() && receipt.current());
    TEST_ASSERT_FALSE(receipt.independentObservation());
    TEST_ASSERT_TRUE(receipt.flags & MidiIngressReceipt::LateHardwareRead);
  }
}
#endif
