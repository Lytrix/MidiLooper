//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Boot proof, breadcrumbs, the watchdog and the USB device's health — how a hang is caught, reset and placed.
// * Breadcrumbs (`CrashReport.breadcrumb`, kept across a warm reset): 1 the main-loop stage, 2 the last tick, 3 the boot
//   phase, 4–6 the USB state (or, after a watchdog reset, where it hung). The next boot prints the previous boot's
//   (`#CAP,…,DIAG,prev_crumbs,…`), so a hang can be placed after a reset.
// * The watchdog (WDOG1, its own 32 kHz clock) is armed by the core's early startup hook and fed every pass and boot
//   phase: a pass or a phase that does not come back within `kWatchdogS` resets the board; its warning interrupt, half
//   a second before, records the interrupted code's PC, LR and exception number.
// * The USB device's health (the watchdog cannot see it: the loop runs on): a wedged USB device keeps the loop and the
//   surface running while the host's open of the serial port and its writes block and the uploader cannot reboot the
//   board. Every `kUsbCheckMs` the live USB state goes to breadcrumbs 4–6; a wedge the board sees itself, held for
//   `kUsbWedgedChecks` checks, is recorded as stage `kUsbWedged` and, while stopped, resets the board.
// Boot proof on USB serial: `#CAP,<us>,DIAG,boot_reset,crash=<0|1>`, then once the card's slots are loaded
// `#CAP,<us>,BOOT,load,ok,...` with every outcome named.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>

#include "Firmware.h"

/// The previous boot's breadcrumbs, copied by `startup_early_hook` before this boot writes any (defined in `main.cpp`,
/// in DMAMEM, which is not cleared).
extern uint32_t gPrevCrumbs[8];

namespace firmware {

using namespace ContentEngine;

// Boot phases (breadcrumb 3): how far a boot came, so a hang before the main loop is placed too. `kPhaseReset` and
// `kPhaseClocks` are written by the core's startup hooks, the rest by `setup`.
enum Phase : uint32_t {
  kPhaseReset = 1,  // the reset handler ran (startup_early_hook)
  kPhaseClocks,     // clocks, caches, PSRAM, before USB (startup_middle_hook)
  kPhaseSetup,      // USB started, constructors run, setup entered
  kPhaseSerial,     // the USB serial wait is over
  kPhaseEngine,     // the engine is initialised
  kPhaseDisplay,    // the display is started
  kPhasePorts,      // DIN and the USB host are started
  kPhaseCard,       // the card is mounted (or absent)
  kPhaseLoop,       // the main loop runs (breadcrumb 1 then holds its stage)
};

struct BootHealth {
  // The watchdog's timeout: no pass and no boot phase takes this long (the longest, `!ENGINE cardfree`, ~1 s).
  static constexpr uint32_t kWatchdogS = 4;
  static constexpr uint32_t kWatchdogReset = SRC_SRSR_WDOG_RST_B;
  static constexpr uint16_t kWicrWie = 1u << 15, kWicrWtis = 1u << 14;  // WDOG1_WICR: warning interrupt enable, status
  static constexpr uint32_t kUsbCheckMs = 250;
  static constexpr uint8_t kUsbWedgedChecks = 4;
  static constexpr uint32_t kUsbIrqOff = 1, kUsbIrqStuck = 2, kUsbRxUnprimed = 4;  // wedge flags (breadcrumb 6, bits 0–2)

  BootHealth(Firmware& f, Diagnostics& d) : fw(f), diag(d) {}

  Firmware& fw;
  Diagnostics& diag;
  uint32_t resetCause = 0;  // SRC_SRSR at boot: why the board reset (`kWatchdogReset` is the watchdog's)
  uint32_t bootStartUs = 0;  // when the boot load began
  uint32_t usbWedges = 0;
  static inline volatile uint32_t usbIsrs = 0;  // USB device interrupts served (`usbIsrCounted`)

  static void feedWatchdog() {
    WDOG1_WSR = 0x5555;
    WDOG1_WSR = 0xAAAA;
  }
  /// A boot phase reached: the watchdog fed, the phase recorded.
  static void phase(uint32_t p) {
    feedWatchdog();
    CrashReport.breadcrumb(3, p);
  }

  /// The reset cause read and cleared, and the watchdog's warning interrupt and the counted USB interrupt attached.
  FIRMWARE_COLD void begin(void (*watchdogIsr)(), void (*usbIsr)()) {
    resetCause = SRC_SRSR;
    SRC_SRSR = resetCause;  // write-one-to-clear: the next boot reports its own cause
    attachInterruptVector(IRQ_WDOG1, watchdogIsr);
    NVIC_SET_PRIORITY(IRQ_WDOG1, 0);
    WDOG1_WICR = kWicrWie | kWicrWtis | 1;  // the warning 0.5 s before the reset
    NVIC_ENABLE_IRQ(IRQ_WDOG1);
    attachInterruptVector(IRQ_USB1, usbIsr);
  }

  /// The main loop entered stage `stage`: the time of the stage before counted, breadcrumbs 1 and 2 written.
  void crumb(uint32_t stage) {
    const uint32_t t = micros();
    if (diag.stage != 0 && t - diag.stageAtUs > diag.stageMaxUs[diag.stage]) diag.stageMaxUs[diag.stage] = t - diag.stageAtUs;
    diag.stage = stage;
    diag.stageAtUs = t;
    CrashReport.breadcrumb(1, stage);
    CrashReport.breadcrumb(2, fw.lastTick);
  }

  /// Resets the board now: the given stage stays as the last crumb; nothing runs after the request (it takes effect
  /// some time after the write, so the loop must not run on: its card, USB and PSRAM traffic would be cut off wherever
  /// the reset lands); the watchdog backs it up.
  [[noreturn]] void quietReboot(uint32_t stage) {
    crumb(stage);
    __disable_irq();
    asm volatile("dsb" ::: "memory");
    SCB_AIRCR = 0x05FA0004;
    asm volatile("dsb" ::: "memory");
    for (;;) {
    }
  }

  /// Breadcrumbs 4–6: (4) USBSTS bits 0–15, PORTSC1 bits 0–9 (connect, enabled, suspend, reset, high speed) at 16–25
  /// and its port speed at 28–29; (5) ENDPTSTATUS (primed endpoints: receive 0–7, transmit 16–23); (6) the wedge flags,
  /// the interrupt enabled (bit 4) and pending (bit 5) in the NVIC, the configuration (bits 8–15), the serial bytes
  /// waiting (16–23, at most 255) and the USB interrupts served since the last check (24–31, at most 255).
  void usbState(uint32_t flags) {
    const uint32_t portsc = USB1_PORTSC1;
    const uint32_t isrs = usbIsrs, served = isrs - usbIsrsAtCheck_;
    usbIsrsAtCheck_ = isrs;
    const int waiting = Serial.available();
    CrashReport.breadcrumb(4, (USB1_USBSTS & 0xFFFF) | (portsc & 0x3FF) << 16 | ((portsc >> 26) & 3) << 28);
    CrashReport.breadcrumb(5, USB1_ENDPTSTATUS);
    CrashReport.breadcrumb(6, flags | (NVIC_IS_ENABLED(IRQ_USB1) ? 0x10u : 0u) | (NVIC_IS_PENDING(IRQ_USB1) ? 0x20u : 0u) |
                                  static_cast<uint32_t>(usb_configuration) << 8 |
                                  static_cast<uint32_t>(waiting > 255 ? 255 : waiting) << 16 |
                                  (served > 255 ? 255u : served) << 24);
  }

  void checkUsb(uint32_t nowMs) {
    if (nowMs - usbCheckMs_ < kUsbCheckMs) return;
    usbCheckMs_ = nowMs;
    uint32_t flags = 0;
    const uint32_t pending = USB1_USBSTS & USB1_USBINTR;
    if (usb_configuration) {  // enumerated: the device must be serviced
      if (!NVIC_IS_ENABLED(IRQ_USB1)) flags |= kUsbIrqOff;
      // An enabled interrupt the handler has not cleared since the last check (it clears its bits within microseconds).
      if (pending != 0 && (pending & usbStuckStatus_) != 0) flags |= kUsbIrqStuck;
      // The serial receive endpoint (3) with no transfer primed and nothing buffered: the host's data can never land.
      if ((USB1_ENDPTSTATUS & (1u << 3)) == 0 && Serial.available() == 0) flags |= kUsbRxUnprimed;
    }
    usbStuckStatus_ = pending;
    usbState(flags);
    if (flags == 0) {
      usbWedgedFor_ = 0;
      return;
    }
    if (++usbWedgedFor_ < kUsbWedgedChecks) return;
    usbWedgedFor_ = 0;
    ++usbWedges;
    if (!fw.clock.running()) quietReboot(kUsbWedged);  // while playing, DIN plays on; the reset waits for a stop
  }

  /// The boot's reset rows (also `!ENGINE boot`, for a host that opened the port after the boot printed them): the
  /// reset cause, and the previous boot's last main-loop stage and tick and how far it booted (bitmask, stage, tick,
  /// phase) — where a hang stopped it. A stage with phase < kPhaseLoop is the boot before that one's (the hang came
  /// before the loop). c4–c6: after a watchdog reset where it hung — PC, LR, exception number (0: the main loop;
  /// 16 + IRQ: a handler), or the USB state if interrupts were off (the warning never ran); otherwise the last USB
  /// state (`usbState`), the wedge's with stage 12 (`kUsbWedged`).
  FIRMWARE_COLD bool reportBootReset() {
    const bool crashed = static_cast<bool>(CrashReport);
    Serial.printf("#CAP,%lu,DIAG,boot_reset,crash=%u,cause=%lx,watchdog=%u\n", static_cast<unsigned long>(micros()),
                  crashed ? 1u : 0u, static_cast<unsigned long>(resetCause), (resetCause & kWatchdogReset) ? 1u : 0u);
    Serial.printf("#CAP,%lu,DIAG,prev_crumbs,mask=%lx,stage=%lu,tick=%lu,phase=%lu,c4=%lx,c5=%lx,c6=%lx\n",
                  static_cast<unsigned long>(micros()), static_cast<unsigned long>(gPrevCrumbs[0]),
                  static_cast<unsigned long>(gPrevCrumbs[1]), static_cast<unsigned long>(gPrevCrumbs[2]),
                  static_cast<unsigned long>(gPrevCrumbs[3]), static_cast<unsigned long>(gPrevCrumbs[4]),
                  static_cast<unsigned long>(gPrevCrumbs[5]), static_cast<unsigned long>(gPrevCrumbs[6]));
    return crashed;
  }

  FIRMWARE_COLD void reportBootLoad() {
    const auto& st = fw.engine.persistence().stats();
    Serial.printf("#CAP,%lu,BOOT,load,ok,card=%u,approved=%u,loads=%lu,load_failed_mask=%llx,v1_imported=%lu,"
                  "v1_skipped_mask=%llx,v1_edited_mask=%llx,v1_failed_mask=%llx,held_whole=%lu,store=%u,migrated=%lu,us=%lu\n",
                  static_cast<unsigned long>(micros()), fw.card.ready(), fw.engine.persistence().approved(),
                  static_cast<unsigned long>(st.loads), static_cast<unsigned long long>(st.loadFailedMask),
                  static_cast<unsigned long>(st.v1Imported), static_cast<unsigned long long>(st.v1SkippedMask),
                  static_cast<unsigned long long>(st.v1EditedMask), static_cast<unsigned long long>(st.v1FailedMask),
                  static_cast<unsigned long>(st.v1HeldWhole), st.store, static_cast<unsigned long>(st.migrated),
                  static_cast<unsigned long>(micros() - bootStartUs));
  }

 private:
  uint32_t usbCheckMs_ = 0, usbStuckStatus_ = 0, usbIsrsAtCheck_ = 0;
  uint8_t usbWedgedFor_ = 0;
};

}  // namespace firmware

#endif  // ENGINE_FIRMWARE
