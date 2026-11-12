//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine firmware: the `teensy41-capture-serial` env (`teensy41-engine` the same build; `ENGINE_FIRMWARE`).
// One compilation unit (DEC-299, header-only engine): this file shows how the firmware starts and runs; the modules it
// names hold the detail.
//
// `setup()`: the watchdog and the boot proof, the title on the screen, the engine and its memory regions, the clock,
// the surface, the ports, then the card's slots loaded in the background.
//
// `loop()`, one pass, no interrupts of its own:
//   1. MIDI input           `MidiInput`         DIN, USB device and USB host → the clock, the transport, the engine,
//                                                the DROID's control plane to the surface
//   2. Serial commands      `SerialCommands`    `!ENGINE …`, `!STOP_STATUS`, `!REBOOT`
//   3. Due ticks            `EngineRoot`        every tick the clock has due, played in order, its output sent
//   4. Jobs and grains      `EngineRoot`        composing, persistence, display work: bounded grains, a due tick played
//                                                between them
//   5. Output               `MidiPorts`         each port's due messages; the USB heartbeat
//   6. The control surface  `ControlSurface`,   gestures, the selection saved, the library and the sets browser
//                           `LibraryOperations`
//   7. Feedback             `MidiPorts`         the DROID's fader bank, motor faders and LEDs
//   8. The screen           `ScreenView`,       a frame every 33 ms
//                           `OledScreen`
//   9. Health               `BootHealth`        the USB device's health; breadcrumbs mark each stage
// A tick waits at most one grain plus the non-engine stretch of a pass (B_max, `kBlockUs`).

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>

#include "BootHealth.h"
#include "DeviceSettings.h"
#include "Firmware.h"
#include "GpioEncoder.h"
#include "LibraryOperations.h"
#include "MidiInput.h"
#include "MidiPorts.h"
#include "SavedSelection.h"
#include "ScreenView.h"
#include "SerialCommands.h"

using namespace firmware;

// ---- Memory regions placed by attribute ---------------------------------------------------------------------------
DMAMEM uint32_t gPrevCrumbs[8];  // the previous boot's breadcrumbs (DMAMEM is not cleared at a warm reset)

namespace {

EXTMEM ContentEngine::EnginePsram gPsram;
DMAMEM ContentEngine::EngineRam2 gRam2;
EXTMEM ContentEngine::ProductionBrowser gBrowser;
DMAMEM uint8_t gDinTx[512];  // DIN's transmit buffer: a pass's messages and the feedback fit without waiting
DMAMEM uint8_t gReadBench[SerialCommands::kBenchBytes];  // `!ENGINE readbench`'s sectors

constexpr uint32_t kBlockUs = 2000;        // B_max: the longest non-engine block of a pass (input, USB, serial)
constexpr uint8_t kGrainsPerPass = 8;      // the scheduler's own slack check decides; this bounds one pass
constexpr uint32_t kDefaultTempo = 12000;  // 120 BPM until a tempo is set or an external clock arrives

// ---- The firmware's parts -----------------------------------------------------------------------------------------
Firmware fw;
Diagnostics diag;
MidiPorts ports{diag};
MidiInput input{fw, ports, diag};
GpioEncoder encoder{fw.surface};
DeviceSettings settings{fw.engine};
SavedSelection selection{fw.engine, fw.surface};
ScreenView screen{fw};
BootHealth health{fw, diag};
LibraryOperations libraryOps{fw, settings, gBrowser};
SerialCommands console{fw, diag, ports, input, settings, libraryOps, health, gReadBench};
bool bootReported = false;  // the boot load ended (or there is no card): reported, the selection restored

/// One worker grain, timed: each job class's worst grain, and a load's while the library has card work.
bool runGrain() {
  const bool working = fw.engine.libraryWorking();
  const uint8_t phase = fw.engine.setLoad().phase() >= 4 ? 2 : fw.engine.setLoad().phase() >= 2 ? 1 : 0;
  const uint32_t t = micros();
  const bool ran = fw.engine.runGrain(t);
  if (ran) {
    const uint32_t d = micros() - t;
    const uint8_t c = static_cast<uint8_t>(fw.engine.scheduler().lastClass());
    if (c < 5 && d > diag.classGrainMaxUs[c]) diag.classGrainMaxUs[c] = d;
  }
  if (ran && working) {
    const uint32_t d = micros() - t;
    if (d > libraryOps.loadGrainMaxUs) libraryOps.loadGrainMaxUs = d;
    if (fw.engine.setLoad().busy() && phase < 3 && d > libraryOps.loadPhaseMaxUs[phase]) libraryOps.loadPhaseMaxUs[phase] = d;
  }
  return ran;
}

/// No card at boot: the card's own error, then the partition table (sector 0) when the card answered.
FLASHMEM void reportCardAbsent() {
  Serial.println("#CAP,BOOT,card,absent");
  static uint8_t mbr[512];
  const unsigned beginError = SD.sdfs.sdErrorCode(), beginData = SD.sdfs.sdErrorData();
  const bool read = SD.sdfs.card() && SD.sdfs.card()->readSector(0, mbr);
  Serial.printf("#ENGINE,card,begin_error=%u,begin_data=%u,sd_error=%u,sd_data=%u,mbr_read=%u,sig=%02x%02x,part1_type=%02x,part1_lba=%lu,"
                "boot_jump=%02x,fs=%.8s\n",
                beginError, beginData, SD.sdfs.sdErrorCode(), SD.sdfs.sdErrorData(), read, mbr[510], mbr[511], mbr[450],
                static_cast<unsigned long>(mbr[454] | mbr[455] << 8 | mbr[456] << 16 | uint32_t(mbr[457]) << 24),
                mbr[0], reinterpret_cast<const char*>(mbr + 3));
}

/// A pass begins: its length since the last one, external-clock timeouts, the output burst of the last pass.
void countPass() {
  static uint32_t passStart = 0;
  const uint32_t t = micros();
  if (passStart != 0 && t - passStart > diag.passMaxUs) diag.passMaxUs = t - passStart;
  passStart = t;
  if (diag.wasExternal && !fw.clock.external()) ++diag.timeouts;
  diag.wasExternal = fw.clock.external();
  ++diag.passes;
  if (diag.outThisPass > diag.usbOutPassMax) diag.usbOutPassMax = diag.outThisPass;
  diag.outThisPass = 0;
}

}  // namespace

// ---- Interrupts and the core's startup hooks ----------------------------------------------------------------------
/// The watchdog's warning, half a second before it resets the board (highest priority, so it interrupts a hung ISR
/// too, though not code running with interrupts off): where the board hung — the interrupted code's PC and LR, and its
/// exception number (0: the main loop; 16 + IRQ: an interrupt handler) — as breadcrumbs 4–6 for the next boot.
extern "C" void watchdogWarning(const uint32_t* frame) {  // the stacked r0–r3, r12, lr, pc, xPSR
  CrashReport.breadcrumb(4, frame[6]);
  CrashReport.breadcrumb(5, frame[5]);
  CrashReport.breadcrumb(6, frame[7] & 0x1FF);
  WDOG1_WICR |= BootHealth::kWicrWtis;  // acknowledged; the reset still comes (the watchdog is not fed)
}

extern "C" void usb_isr(void);
/// The core's USB device interrupt, counted (`BootHealth::usbState`).
void usbIsrCounted() {
  ++BootHealth::usbIsrs;
  usb_isr();
}

extern "C" __attribute__((naked)) void watchdogWarningIsr() {
  asm volatile(
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "b watchdogWarning\n");
}

// The core's startup hooks (weak in `startup.c`). The early hook runs before the caches and ITCM are set up, so it is
// FLASHMEM and writes RAM directly: it keeps the previous boot's breadcrumbs (the struct at the end of RAM2 survives a
// warm reset), starts this boot's clean, and arms the watchdog (WDOG1, its own 32 kHz clock), so a hang anywhere from
// here on — PSRAM, USB start, setup, the main loop — resets the board by itself instead of waiting for a PROGRAM press.
extern "C" FLASHMEM void startup_early_hook(void) {
  volatile uint32_t* bc = reinterpret_cast<volatile uint32_t*>(0x2027FFC0);
  for (int i = 0; i < 8; ++i) gPrevCrumbs[i] = bc[i];
  for (int i = 0; i < 8; ++i) bc[i] = 0;
  bc[3] = kPhaseReset;  // breadcrumb 3 (bc[0] is the mask, bc[1..6] the values)
  bc[0] = 1u << 2;
  CCM_CCGR3 |= CCM_CCGR3_WDOG1(CCM_CCGR_ON);
  WDOG1_WMCR = 0;  // the power-down counter off
  // Timeout (WT + 1) / 2 s; suspended under a debugger; no WDOG_B pin, no software reset.
  WDOG1_WCR = WDOG_WCR_WT(BootHealth::kWatchdogS * 2 - 1) | WDOG_WCR_WDBG | WDOG_WCR_WDE | WDOG_WCR_SRS | WDOG_WCR_WDA;
  WDOG1_WSR = 0x5555;  // fed here directly: nothing outside FLASHMEM runs yet
  WDOG1_WSR = 0xAAAA;
}

extern "C" FLASHMEM void startup_middle_hook(void) { BootHealth::phase(kPhaseClocks); }

// ---- Start ----------------------------------------------------------------------------------------------------------
FLASHMEM void setup() {  // once at boot, from flash (RAM1 room)
  BootHealth::phase(kPhaseSetup);
  health.begin(watchdogWarningIsr, usbIsrCounted);
  // The title first: before the wait for USB serial (up to 3 s without a host), until the boot load has ended.
  fw.display.begin();
  fw.display.drawTitle();
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    BootHealth::feedWatchdog();
    delay(10);
  }
  BootHealth::phase(kPhaseSerial);
  if (health.reportBootReset()) Serial.print(CrashReport);
  settings.loadTzOffset();
  settings.reportTime();

  if (!fw.engine.init(gPsram, gRam2, kBlockUs)) {
    Serial.println("#CAP,BOOT,engine,init_failed");
    BootHealth::phase(kPhaseLoop);  // the loop still runs (and feeds the watchdog); nothing else starts
    return;
  }
  BootHealth::phase(kPhaseEngine);
  EngineRoot::census([](EngineRoot::Region r, const char* name, size_t bytes) {
    Serial.printf("#CAP,MEM_REGION,%s,%s,%u\n", r == EngineRoot::Region::Psram ? "psram" : "ram2", name,
                  static_cast<unsigned>(bytes));
  });
  // Each track plays on its own MIDI channel, track n on channel n, whatever channel a note was recorded on; live thru
  // follows the selected track's channel.
  for (uint8_t t = 0; t < kTrackCount; ++t) fw.engine.output().setChannel(t, static_cast<uint8_t>(t + 1));
  fw.clock.init(kDefaultTempo, kTicksPerBar);
  settings.loadSwitchAt();
  encoder.begin();
  settings.loadLaunchGrid();
  fw.surface.init(fw.engine, fw.clock);
  BootHealth::phase(kPhaseDisplay);

  ports.begin(gDinTx, sizeof gDinTx);
  BootHealth::phase(kPhasePorts);

  health.bootStartUs = micros();
  if (fw.card.begin()) {
    fw.engine.attachCard(fw.card);
    fw.engine.startBootLoad();  // the slots load in grains; `loop` reports it when done
  } else {
    reportCardAbsent();
  }
  BootHealth::phase(kPhaseCard);
}

// ---- Run ------------------------------------------------------------------------------------------------------------
void loop() {
  static bool looping = false;
  if (!looping) {
    looping = true;
    BootHealth::phase(kPhaseLoop);
  }
  BootHealth::feedWatchdog();
  countPass();
  uint32_t now = micros();

  health.crumb(kInput);  // 1. MIDI input
  input.poll(now);

  health.crumb(kSerial);  // 2. Serial commands
  console.poll(now);

  health.crumb(kTicks);  // 3. Due ticks
  uint8_t count = 0;
  uint32_t tick;
  while (fw.clock.due(micros(), tick, count)) {
    fw.engine.clockTick(tick, micros(), fw.clock.usPerTick());
    fw.lastTick = tick;
    ports.transmit(fw.engine, micros());
  }
  now = micros();

  health.crumb(kJobs);  // 4. Jobs and grains
  fw.engine.submitJobs(fw.lastTick, now, fw.clock.usPerTick());
  health.crumb(kGrains);
  for (uint8_t g = 0; g < kGrainsPerPass && runGrain(); ++g) {
    uint8_t c2 = 0;
    if (fw.clock.due(micros(), tick, c2)) {  // a tick came due: play it before the next grain
      fw.engine.clockTick(tick, micros(), fw.clock.usPerTick());
      fw.lastTick = tick;
    }
  }

  health.crumb(kSend);  // 5. Output
  ports.transmit(fw.engine, micros());
  ports.heartbeat(fw.clock.external());
  if (!bootReported && fw.card.ready() && !fw.engine.persistence().booting()) {
    bootReported = true;
    health.reportBootLoad();
    selection.restore();
    libraryOps.openLibrary("boot");  // created here only once saving is approved (boot is stopped)
  } else if (!bootReported && !fw.card.ready()) {
    bootReported = true;
  }

  health.crumb(kSurface);  // 6. The control surface
  encoder.service(millis());
  fw.surface.service(millis(), fw.lastTick, micros());
  if (bootReported) selection.save(micros());  // saved once settled, after approval
  if (bootReported) console.watchBirths(millis());
  settings.stampSaves();
  libraryOps.serviceLibrary();
  libraryOps.serviceLoad();
  libraryOps.serviceWorking(micros());
  libraryOps.servicePicker(millis());
  libraryOps.serviceBrowser(micros());

  health.crumb(kLeds);  // 7. Feedback to the DROID
  uint8_t bank;
  if (fw.surface.bank(bank)) ports.sendBank(bank);  // the DROID's fader bank follows NOTE_EDIT
  ControlSurfaceNoteEdit<EngineRoot>::MotorMsg motor;
  if (fw.surface.motor(millis(), motor)) ports.sendMotor(motor);  // the DROID's motor faders follow the selection
  if (micros() - ports.lastLedUs >= MidiPorts::kLedGapUs) {
    if (!ports.hostTxRoom())
      ++ports.hostDeferred;  // the DROID is not taking packets: the surface keeps its LEDs until it does
    else if (fw.surface.leds(fw.lastTick, millis(), [](uint8_t note, uint8_t v) { ports.sendLed(note, v); }, 1) != 0)
      ports.lastLedUs = micros();
  }
  const Teensy::EditView view = screen.prepare();

  health.crumb(kScreen);  // 8. The screen
  if (!selection.known && (selection.applySaved() || bootReported)) selection.known = true;
  // The title stays until the boot load has ended; the first frame then opens on the saved selection.
  if (selection.known && bootReported) fw.display.service(fw.engine, fw.clock, fw.lastTick, millis(), view);

  health.crumb(kIdle);  // 9. Health
  health.checkUsb(millis());
}

#endif  // ENGINE_FIRMWARE
