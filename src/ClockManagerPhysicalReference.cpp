// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "ClockManager.h"
#include "Utils/ClockPhysicalReferencePlatform.h"

#if defined(__IMXRT1062__)
#define CLOCK_PHYSICAL_MEM FLASHMEM
#else
#define CLOCK_PHYSICAL_MEM
#endif
namespace {
using namespace ClockPhysicalReferencePlatform;
struct PhysicalReferenceState {
  uint64_t lastTicks = 0;
  uint32_t era = 1;
  PhysicalClockReferenceStatus status = PhysicalClockReferenceStatus::NotStarted;
  bool ownsPair = false;
};
// Singleton ClockManager extension. Constructor explicitly initializes this NOLOAD state.
#if defined(__IMXRT1062__)
DMAMEM PhysicalReferenceState physicalReference;
#else
PhysicalReferenceState physicalReference;
#endif
static_assert(sizeof(PhysicalReferenceState) == 16, "Price singleton physical reference state");
class PhysicalReferenceMask {
 public:
  PhysicalReferenceMask() : saved_(maskInterrupts()) {}
  ~PhysicalReferenceMask() { restoreInterrupts(saved_); }
 private:
  uint32_t saved_;
};
CLOCK_PHYSICAL_MEM void invalidatePhysicalReference(PhysicalClockReferenceStatus reason) {
  physicalReference.era = physicalReference.era == UINT32_MAX ? 0 :
      (physicalReference.era == 0 ? 0 : physicalReference.era + 1);
  physicalReference.status = physicalReference.era == 0 ?
      PhysicalClockReferenceStatus::EraExhausted : reason;
}
CLOCK_PHYSICAL_MEM bool physicalInputConfigurationCurrent() {
  return (read(Register::ClockSelect) & kClockSelectMask) == kOscillator24MHz &&
      (read(Register::DebugControl) & kUnsupportedDebugMask) == 0;
}
CLOCK_PHYSICAL_MEM bool physicalPairConfigurationCurrent() {
  return physicalInputConfigurationCurrent() &&
      (read(Register::ClockGate) & kPitGateMask) == kPitGateMask &&
      read(Register::Module) == kFreeze &&
      read(Register::Load0) == UINT32_MAX && read(Register::Load1) == UINT32_MAX &&
      read(Register::Control0) == kEnable && read(Register::Control1) == (kEnable | kChain);
}
}

CLOCK_PHYSICAL_MEM void ClockManager::initializePhysicalClockReference() {
  // Only the singleton's cold construction; never a runtime counter reset API.
  physicalReference = PhysicalReferenceState{};
}
CLOCK_PHYSICAL_MEM void ClockManager::preparePhysicalClockReference() {
  const PhysicalReferenceMask mask;
  const auto previous = physicalReference.status;
  invalidatePhysicalReference(PhysicalClockReferenceStatus::NotStarted);
  if (physicalReference.era == 0) return;
  if (previous == PhysicalClockReferenceStatus::CounterExhausted ||
      previous == PhysicalClockReferenceStatus::CounterWentBackwards) {
    physicalReference.status = previous; // Terminal within this producer lifetime.
    return;
  }
  if (!physicalInputConfigurationCurrent()) {
    physicalReference.status = PhysicalClockReferenceStatus::ConfigurationInvalid;
    return;
  }
  if (physicalReference.ownsPair) {
    // Requalification preserves elapsed count: never disable the shared module
    // while the existing MIDI timer may be running, and never reclaim a changed pair.
    if (!physicalPairConfigurationCurrent()) {
      physicalReference.status = PhysicalClockReferenceStatus::ConfigurationInvalid;
      return;
    }
    if ((read(Register::Flag1) & kTimeout) != 0) {
      physicalReference.status = PhysicalClockReferenceStatus::CounterExhausted;
      return;
    }
  } else {
    // Initial setup is cold-only: MDIS/configuration must not stop any foreign timer.
    // Match the framework's bus-clock prerequisite before touching PIT registers.
    const auto gate = read(Register::ClockGate);
    if ((gate & kPitGateMask) != kPitGateMask) write(Register::ClockGate, gate | kPitGateMask);
    if (read(Register::Control0) != 0 || read(Register::Control1) != 0 ||
        read(Register::Control2) != 0 || read(Register::Control3) != 0) {
      physicalReference.status = PhysicalClockReferenceStatus::ResourceBusy;
      return;
    }
    write(Register::Module, kModuleDisable | kFreeze);
    write(Register::Load1, UINT32_MAX);
    write(Register::Load0, UINT32_MAX);
    write(Register::Flag1, kTimeout);
    write(Register::Flag0, kTimeout);
    write(Register::Module, kFreeze);
    // NXP RM53.4.5: start chained high before low; TIE off, no callbacks.
    write(Register::Control1, kEnable | kChain);
    write(Register::Control0, kEnable);
    physicalReference.ownsPair = true;
  }
  physicalReference.status = PhysicalClockReferenceStatus::AwaitingMidiTimer;
}
CLOCK_PHYSICAL_MEM void ClockManager::finishPhysicalClockReference(bool midiTimerStarted) {
  const PhysicalReferenceMask mask;
  if (physicalReference.status != PhysicalClockReferenceStatus::AwaitingMidiTimer) return;
  if (!midiTimerStarted) {
    invalidatePhysicalReference(PhysicalClockReferenceStatus::MidiTimerUnavailable);
    return;
  }
  // Stock IntervalTimer allocation writes the shared MCR even when it fails.
  if (!physicalPairConfigurationCurrent()) {
    invalidatePhysicalReference(PhysicalClockReferenceStatus::ConfigurationInvalid);
    return;
  }
  physicalReference.status = PhysicalClockReferenceStatus::Ready;
  (void)getPhysicalClockStamp(); // Validates count/exhaustion before any first observation.
}
CLOCK_PHYSICAL_MEM PhysicalClockStamp ClockManager::getPhysicalClockStamp() const {
  const PhysicalReferenceMask mask;
  if (physicalReference.status != PhysicalClockReferenceStatus::Ready || physicalReference.era == 0) return {};
  if (!physicalPairConfigurationCurrent()) {
    invalidatePhysicalReference(PhysicalClockReferenceStatus::ConfigurationInvalid);
    return {};
  }
  if ((read(Register::Flag1) & kTimeout) != 0) {
    invalidatePhysicalReference(PhysicalClockReferenceStatus::CounterExhausted);
    return {};
  }
  // One coherent, hardware-latched high -> low read. The mask prevents another
  // ISR/main reader replacing the shared low latch. No carry retry or hot write.
  const uint32_t high = read(Register::LifetimeHigh);
  const uint32_t low = read(Register::LifetimeLow);
  const uint64_t ticks = ~((uint64_t(high) << 32) | low);
  if ((read(Register::Flag1) & kTimeout) != 0) {
    invalidatePhysicalReference(PhysicalClockReferenceStatus::CounterExhausted);
    return {};
  }
  if (!physicalPairConfigurationCurrent()) {
    invalidatePhysicalReference(PhysicalClockReferenceStatus::ConfigurationInvalid);
    return {};
  }
  if (ticks < physicalReference.lastTicks) {
    invalidatePhysicalReference(PhysicalClockReferenceStatus::CounterWentBackwards);
    return {};
  }
  physicalReference.lastTicks = ticks;
  // Nominal 24MHz, floor quantization. Accuracy/uncertainty is not a deadline bound.
  return {ticks / 24u, physicalReference.era};
}
CLOCK_PHYSICAL_MEM PhysicalClockReferenceStatus ClockManager::physicalClockReferenceStatus() const {
  const PhysicalReferenceMask mask;
  return physicalReference.status;
}
CLOCK_PHYSICAL_MEM void ClockManager::invalidatePhysicalClockReference() {
  const PhysicalReferenceMask mask;
  const auto previous = physicalReference.status;
  invalidatePhysicalReference(PhysicalClockReferenceStatus::Suspended);
  if (physicalReference.era != 0 && (previous == PhysicalClockReferenceStatus::CounterExhausted ||
      previous == PhysicalClockReferenceStatus::CounterWentBackwards)) physicalReference.status = previous;
}
#if defined(PIO_UNIT_TEST_NATIVE)
void ClockManager::setPhysicalClockEraForTests(uint32_t era) {
  const PhysicalReferenceMask mask;
  physicalReference.era = era;
}
#endif
