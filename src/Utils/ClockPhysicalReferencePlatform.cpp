// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Utils/ClockPhysicalReferencePlatform.h"
#if defined(__IMXRT1062__)
#include <Arduino.h>
namespace ClockPhysicalReferencePlatform {
static_assert(kPitGateMask == CCM_CCGR1_PIT(3), "PIT gate mapping");
static_assert(kClockSelectMask == (CCM_CSCMR1_PERCLK_CLK_SEL | CCM_CSCMR1_PERCLK_PODF(63)), "PERCLK mapping");
static_assert(kFreeze == PIT_MCR_FRZ && kModuleDisable == PIT_MCR_MDIS, "PIT module mapping");
static_assert(kEnable == PIT_TCTRL_TEN && kChain == PIT_TCTRL_CHN, "PIT channel mapping");
FLASHMEM uint32_t read(Register reg) {
  switch (reg) {
    case Register::Module: return PIT_MCR;
    case Register::ClockSelect: return CCM_CSCMR1;
    case Register::ClockGate: return CCM_CCGR1;
    case Register::DebugControl: return *reinterpret_cast<volatile uint32_t*>(0xE000EDF0u); // ARM DHCSR, read only.
    case Register::Load0: return PIT_LDVAL0;
    case Register::Control0: return PIT_TCTRL0;
    case Register::Flag0: return PIT_TFLG0;
    case Register::Load1: return PIT_LDVAL1;
    case Register::Control1: return PIT_TCTRL1;
    case Register::Flag1: return PIT_TFLG1;
    case Register::Control2: return PIT_TCTRL2;
    case Register::Control3: return PIT_TCTRL3;
    case Register::LifetimeHigh: return PIT_LTMR64H;
    case Register::LifetimeLow: return PIT_LTMR64L;
    case Register::Count: return 0;
  }
  return 0;
}
FLASHMEM void write(Register reg, uint32_t value) {
  switch (reg) {
    case Register::Module: PIT_MCR = value; break;
    case Register::ClockGate: CCM_CCGR1 = value; break;
    case Register::Load0: PIT_LDVAL0 = value; break;
    case Register::Control0: PIT_TCTRL0 = value; break;
    case Register::Flag0: PIT_TFLG0 = value; break;
    case Register::Load1: PIT_LDVAL1 = value; break;
    case Register::Control1: PIT_TCTRL1 = value; break;
    case Register::Flag1: PIT_TFLG1 = value; break;
    default: break; // Counter owner never writes other clocks/channels/debug registers.
  }
  __asm__ volatile("dsb" ::: "memory");
}
FLASHMEM uint32_t maskInterrupts() {
  uint32_t saved;
  __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(saved) :: "memory");
  return saved;
}
FLASHMEM void restoreInterrupts(uint32_t saved) {
  __asm__ volatile("msr primask, %0" :: "r"(saved) : "memory");
}
#if defined(SESSION_CAPTURE)
FLASHMEM uint32_t readCycleCounterForObservation() {
  __asm__ volatile("dsb\n\tisb" ::: "memory");
  return ARM_DWT_CYCCNT;
}
FLASHMEM uint32_t cpuClockHzForObservation() {
  return (ARM_DEMCR & ARM_DEMCR_TRCENA) != 0 &&
      (ARM_DWT_CTRL & ARM_DWT_CTRL_CYCCNTENA) != 0 ? F_CPU_ACTUAL : 0;
}
#endif
}
#endif
