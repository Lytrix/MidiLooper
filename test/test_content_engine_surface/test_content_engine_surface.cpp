//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.8b-1: the control surface on the production root (owner 2026-11-06: performance controls
// first, the legacy gesture map kept). Press timing as the legacy `MidiButtonProcessor` (short after the double
// window, double, triple, long on release, debounce, an orphan release dropped); the control plane (channels 13–16)
// kept from recording and thru; on the DROID notes: a slot button on a stopped transport starts the clock and records
// from the bar, the take ends at the bar and plays; another filled slot plays from the loop end; the playing slot
// toggles mute; the record button's double undoes, its long clears; track buttons select, mute (double), solo (long);
// the track switch steps to the next track; the transport button stops and starts; play / stop stops a track and
// plays its slot; an armed slot punches in on its second press; the LEDs follow (track, slot, transport, bar,
// position), only changes sent; no allocation.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <new>

#include "ContentEngine/EngineClock.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/Surface.h"

static unsigned long gHeapAllocations = 0;
static bool gTrap = false;
void* operator new(std::size_t size) {
  if (gTrap) ++gHeapAllocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace ContentEngine;
using S = Surface<EngineRoot>;

static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;
static EngineClock gClk;
static S gS;
static uint32_t gUs = 0, gLast = 0;
static uint8_t gLed[128];
static uint32_t gLedSends = 0;

static ProductionTransport& X() { return gE.transport(); }
static ProductionControl& C() { return gE.control(); }

// The main loop, 125 µs a pass, until `toUs`; live notes (on, off µs) on channel 1 into the engine.
struct Live {
  uint32_t on, off;
  uint8_t pitch;
};
static bool gExternal = false;  // a master's clock: 24 pulses a quarter
static uint32_t gNextPulseUs = 0;
static void run(uint32_t toUs, std::initializer_list<Live> live = {}) {
  ProductionHub::Sent buf[32];
  for (; gUs < toUs; gUs += 125) {
    if (gExternal && gUs >= gNextPulseUs) {
      gClk.midiPulse(gUs);
      gNextPulseUs += 8 * 2604;  // 120 BPM
    }
    uint8_t count = 0;
    uint32_t t;
    while (gClk.due(gUs, t, count)) {
      gE.clockTick(t, gUs, gClk.usPerTick());
      gLast = t;
    }
    gE.submitJobs(gLast, gUs, gClk.usPerTick());
    while (gE.runGrain(gUs)) {
    }
    for (uint8_t p = 0; p < 3; ++p) gE.transmit(p, gUs, buf, 32);
    gS.service(gUs / 1000, gLast, gUs);
    gS.leds(gLast, gUs / 1000, [](uint8_t n, uint8_t v) { gLed[n] = v, ++gLedSends; }, 8);
    for (const Live& l : live) {
      if (l.on >= gUs && l.on < gUs + 125) gE.inputNoteOn(gLast, 0x90, l.pitch, 100, gUs, gClk.usPerTick());
      if (l.off >= gUs && l.off < gUs + 125) gE.inputNoteOff(gLast, 0x80, l.pitch, gUs);
    }
  }
}
static void ms(uint32_t m) { run(gUs + m * 1000); }
// A DROID button on channel 16: pressed for `holdMs`.
static void tap(uint8_t note, uint32_t holdMs = 50) {
  gS.input(0x9F, note, 127, gUs / 1000);
  ms(holdMs);
  gS.input(0x8F, note, 0, gUs / 1000);
}
static constexpr uint32_t kTick = 2604;  // 120 BPM
static constexpr uint32_t kBarUs = 768 * kTick;

void setUp() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClk.init(12000);
  gS.init(gE, gClk);
  gUs = 1000000;
  gLast = 0;
  gExternal = false;
  for (uint8_t& v : gLed) v = 0;
  gLedSends = 0;
}
void tearDown() {}

// ---- Press timing ------------------------------------------------------------------------------------------------------
void test_press_timing_matches_the_legacy_processor() {
  PressDetector<2> d;
  d.init();
  d.configure(0, {300, 400, 600, 0, true});
  d.configure(1, {300, 400, 3000, 50, false});
  uint8_t b;
  Press p;
  d.down(0, 0);
  d.up(0, 50);
  TEST_ASSERT_FALSE(d.next(349, b, p));  // a lone tap: a short once the double window passes
  TEST_ASSERT_TRUE(d.next(350, b, p));
  TEST_ASSERT_EQUAL(Press::Short, p);
  d.down(0, 1000), d.up(0, 1050), d.down(0, 1200), d.up(0, 1250);  // double, waiting for a triple
  TEST_ASSERT_FALSE(d.next(1600, b, p));
  TEST_ASSERT_TRUE(d.next(1650, b, p));
  TEST_ASSERT_EQUAL(Press::Double, p);
  d.down(0, 2000), d.up(0, 2050), d.down(0, 2100), d.up(0, 2150), d.down(0, 2200), d.up(0, 2250);
  TEST_ASSERT_TRUE(d.next(2250, b, p));
  TEST_ASSERT_EQUAL(Press::Triple, p);
  d.down(0, 3000), d.up(0, 3600);
  TEST_ASSERT_TRUE(d.next(3600, b, p));
  TEST_ASSERT_EQUAL(Press::Long, p);
  // Button 1: no triple action, a double at once; debounce 50 ms after a release; long at 3 s.
  d.down(1, 4000), d.up(1, 4050), d.down(1, 4080);  // within the debounce: ignored
  d.up(1, 4090);                                     // its press was never taken: dropped
  TEST_ASSERT_FALSE(d.next(4100, b, p));
  d.down(1, 4200), d.up(1, 4250);
  TEST_ASSERT_TRUE(d.next(4250, b, p));
  TEST_ASSERT_EQUAL(Press::Double, p);
  d.up(1, 5000);  // an orphan release
  TEST_ASSERT_FALSE(d.next(6000, b, p));
}

void test_the_control_plane_is_the_surfaces() {
  TEST_ASSERT_TRUE(S::controlPlane(0x9F));  // channel 16
  TEST_ASSERT_TRUE(S::controlPlane(0xEC));  // channel 13 pitch bend
  TEST_ASSERT_FALSE(S::controlPlane(0x90));
  TEST_ASSERT_FALSE(S::controlPlane(0x9B));  // channel 12
  TEST_ASSERT_FALSE(S::controlPlane(0xF8));
}

// ---- Gestures ----------------------------------------------------------------------------------------------------------
/// Runs until the take starts (at most `maxUs`).
static void untilRecording(uint32_t maxUs) {
  const uint32_t end = gUs + maxUs;
  while (gUs < end && X().mode() != ProductionTransport::Mode::Recording) run(gUs + 125);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
}
/// Track 0 slot `s` recorded for one bar through its slot button (the take ends at the bar). On a stopped transport
/// the first press arms (owner 2026-11-06: as legacy) and the second starts the transport and the take with it.
static void recordOneBar(uint8_t s, uint8_t pitch) {
  const bool wasRunning = gClk.running();
  tap(S::kSlotBase + s);
  ms(400);  // the short gesture fires after the double window
  TEST_ASSERT_EQUAL_UINT8(s, C().queuedRecordSlot(0));
  if (!wasRunning) {
    TEST_ASSERT_FALSE(gClk.running());  // armed, waiting for the transport
    run(gUs + kBarUs);
    TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());
    tap(S::kSlotBase + s);
    ms(400);
    TEST_ASSERT_TRUE(gClk.running());
    TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());  // on the clock's first tick
  }
  untilRecording(3 * kBarUs);
  const uint32_t start = gUs;
  run(start + kBarUs - 300 * kTick, {{start + 50 * kTick, start + 200 * kTick, pitch}});
  tap(S::kSlotBase + s);  // the take ends at the bar, not at the press
  ms(400);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
  run(start + kBarUs + 50 * kTick);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());
}

void test_slot_buttons_record_play_switch_and_mute() {
  recordOneBar(0, 60);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, X().contentOf(0));
  TEST_ASSERT_TRUE(X().playing(0));
  TEST_ASSERT_EQUAL_UINT8(0, X().playingSlot(0));
  // A second slot: armed Record from the playing slot's loop start; ends at the bar.
  tap(S::kSlotBase + 1);
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(1, C().queuedRecordSlot(0));
  untilRecording(2 * kBarUs);
  const uint32_t start = gUs;
  run(start + kBarUs - 200 * kTick, {{start + 20 * kTick, start + 90 * kTick, 62}});
  tap(S::kSlotBase + 1);
  run(start + kBarUs + 50 * kTick);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, X().contentOf(1));
  TEST_ASSERT_EQUAL_UINT8(1, X().playingSlot(0));
  // Slot 0 again: plays from slot 1's loop end. Meanwhile the track shows it — the queued slot (legacy: its piano roll
  // at once, the playhead parked; slice A) — while slot 1 plays on.
  TEST_ASSERT_EQUAL_UINT8(1, gS.shown(0));
  TEST_ASSERT_FALSE(gS.shownQueued(0));
  tap(S::kSlotBase + 0);
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(0, C().queuedSwitchSlot(0));
  TEST_ASSERT_EQUAL_UINT8(0, gS.shown(0));
  TEST_ASSERT_TRUE(gS.shownQueued(0));
  TEST_ASSERT_EQUAL_UINT8(1, X().playingSlot(0));
  run(gUs + kBarUs + kTick);
  TEST_ASSERT_EQUAL_UINT8(0, X().playingSlot(0));
  TEST_ASSERT_EQUAL_UINT8(0, gS.shown(0));
  TEST_ASSERT_FALSE(gS.shownQueued(0));
  // The playing slot's button toggles the track's mute.
  tap(S::kSlotBase + 0);
  ms(400);
  TEST_ASSERT_TRUE(C().muted(0));
  tap(S::kSlotBase + 0);
  ms(400);
  TEST_ASSERT_FALSE(C().muted(0));
}

// A slot's long press no longer clears a filled slot (it opens the length popup, which owns the slot button until it
// hides); its double press on the playing slot leaves it playing (slice B: double launches); record's long clears,
// undoably.
void test_slot_long_press_keeps_and_record_long_clears() {
  recordOneBar(0, 60);
  const ManifestId m = X().contentOf(0);
  gS.input(0x9F, S::kSlotBase + 0, 127, gUs / 1000);  // long: a filled slot stays — copy mode (DEC-279 amendment 1)
  ms(700);
  gS.input(0x8F, S::kSlotBase + 0, 0, gUs / 1000);
  ms(400);
  TEST_ASSERT_EQUAL_UINT16(m, X().contentOf(0));
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());  // no overdub either
  TEST_ASSERT_FALSE(gS.popup());
  TEST_ASSERT_EQUAL(S::Copy::Slot, gS.copyMode());
  tap(S::kSlotBase + 0);  // the source again: copy mode ends
  ms(100);
  TEST_ASSERT_EQUAL(S::Copy::None, gS.copyMode());
  tap(S::kSlotBase + 0);
  ms(100);
  tap(S::kSlotBase + 0);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(m, X().contentOf(0));
  TEST_ASSERT_TRUE(X().playing(0));
  TEST_ASSERT_EQUAL_UINT8(0, X().playingSlot(0));
  TEST_ASSERT_FALSE(C().muted(0));
  tap(S::kRecordNote, 700);  // record's long: clear
  ms(100);
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, X().contentOf(0));
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());
  tap(S::kRecordNote);  // undo
  ms(100);
  tap(S::kRecordNote);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(m, X().contentOf(0));
}

/// Runs until `done`, at most `maxUs`; the tick it became true.
template <typename F>
static uint32_t until(uint32_t maxUs, F done) {
  const uint32_t end = gUs + maxUs;
  while (gUs < end && !done()) run(gUs + 125);
  TEST_ASSERT_TRUE(done());
  return gLast;
}
// Slice B (owner 2026-11-10): a slot's double press launches the slot at the next launch step — 1/16 by default, a
// queued switch taken sooner than its loop end; with the bar setting, at the next bar line.
void test_slot_double_press_launches_at_the_next_launch_step() {
  TEST_ASSERT_EQUAL_UINT32(48, C().launchGrid());
  recordOneBar(0, 60);
  tap(S::kSlotBase + 1);  // a second slot, from slot 0's loop start
  ms(400);
  untilRecording(2 * kBarUs);
  const uint32_t start = gUs;
  run(start + kBarUs - 200 * kTick, {{start + 20 * kTick, start + 90 * kTick, 62}});
  tap(S::kSlotBase + 1);
  run(start + kBarUs + 50 * kTick);
  TEST_ASSERT_EQUAL_UINT8(1, X().playingSlot(0));
  // Queued with a short press (slot 1's loop end), then a double: at the next 16th, not the loop end.
  run(gUs + 100 * kTick);
  tap(S::kSlotBase + 0);
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(0, C().queuedSwitchSlot(0));
  tap(S::kSlotBase + 0);
  ms(100);
  tap(S::kSlotBase + 0);
  ms(410);  // the double fires after the triple window, well before slot 1's loop end
  TEST_ASSERT_EQUAL_UINT8(1, X().playingSlot(0));
  TEST_ASSERT_EQUAL_UINT8(0, C().queuedSwitchSlot(0));
  TEST_ASSERT_TRUE(gS.shownQueued(0));
  TEST_ASSERT_TRUE(C().ticksToQueued(0, gLast) <= 48);
  TEST_ASSERT_TRUE(X().loopLength(X().contentOf(1)) - (gLast - X().origin(0)) % X().loopLength(X().contentOf(1)) > 96);
  const uint32_t pressed = gLast;
  const uint32_t switched = until(kBarUs, [] { return X().playingSlot(0) == 0; });
  TEST_ASSERT_TRUE(switched - pressed <= 48);
  TEST_ASSERT_EQUAL_UINT32(0, switched % 48);  // on the step
  // The bar setting: the next bar line.
  C().setLaunchGrid(768);
  run(gUs + 100 * kTick);
  tap(S::kSlotBase + 1);
  ms(100);
  tap(S::kSlotBase + 1);
  until(kBarUs, [] { return C().queuedSwitchSlot(0) == 1; });
  const uint32_t barSwitched = until(2 * kBarUs, [] { return X().playingSlot(0) == 1; });
  TEST_ASSERT_EQUAL_UINT32(0, barSwitched % 768);
  // An empty slot's double acts as its short press: armed.
  tap(S::kSlotBase + 5);
  ms(100);
  tap(S::kSlotBase + 5);
  ms(450);
  TEST_ASSERT_EQUAL_UINT8(5, C().queuedRecordSlot(0));
}

// Slice C (legacy `BarStepButtonHandler`): a step button (0–15) restarts the playing slot at that 16th of the bar
// playing, a bar button (17–24) at that bar of the 8-bar page playing (modulo the loop), at the next launch step; the
// queued restart shows parked where it will start; note 16 is no button.
static void press(uint8_t note) {
  gS.input(0x9F, note, 127, gUs / 1000);
  gS.input(0x8F, note, 0, gUs / 1000);
}
static void restartsAt(uint8_t note, uint32_t position, uint32_t loop) {
  press(note);
  TEST_ASSERT_TRUE(C().switchQueued(0));
  TEST_ASSERT_EQUAL_UINT32(position, C().queuedPosition(0));
  TEST_ASSERT_TRUE(gS.shownQueued(0));
  TEST_ASSERT_EQUAL_UINT32(position, gS.shownQueuedPosition(0));
  const uint32_t pressed = gLast;
  const uint32_t at = until(kBarUs, [] { return !C().switchQueued(0); });
  TEST_ASSERT_TRUE(at - pressed <= 48);
  TEST_ASSERT_EQUAL_UINT32(position, (at - X().origin(0)) % loop);
}
void test_step_and_bar_buttons_restart_the_playing_slot_there() {
  recordOneBar(0, 60);
  run(gUs + 100 * kTick);
  restartsAt(8, 384, 768);  // the 9th 16th
  restartsAt(0, 0, 768);
  restartsAt(20, 0, 768);   // bar 4 of a 1-bar loop: its start
  // A 4-bar loop on slot 1, launched by its double press.
  TEST_ASSERT_EQUAL(ProductionTransport::Result::Ok, C().createEmpty(1, 4 * 768));
  tap(S::kSlotBase + 1);
  ms(100);
  tap(S::kSlotBase + 1);
  until(2 * kBarUs, [] { return X().playingSlot(0) == 1 && !C().switchQueued(0); });
  run(gUs + 100 * kTick);
  restartsAt(19, 2 * 768, 4 * 768);  // bar 3 of the page
  restartsAt(24, 3 * 768, 4 * 768);  // bar 8: 7 bars on, modulo the loop
  run(gUs + kBarUs / 2);            // in bar 4 now
  restartsAt(15, 3 * 768 + 15 * 48, 4 * 768);  // the last 16th of the bar playing
  press(16);  // no button
  TEST_ASSERT_FALSE(C().switchQueued(0));
  tap(S::kPlayStopNote);  // stopped: the buttons do nothing
  ms(500);
  TEST_ASSERT_FALSE(X().playing(0));
  press(3);
  TEST_ASSERT_FALSE(C().switchQueued(0));
}

// DEC-276 (owner 2026-11-10: the transport button could not stop a MIDI-started external clock): under a running
// external clock the transport button is a local stop — every track ends, the clock runs on — and the next press
// brings the tracks back at the next bar line; under a stopped one it does nothing (owner: no popups).
void test_the_transport_button_stops_and_starts_locally_under_an_external_clock() {
  recordOneBar(0, 60);  // on the internal clock: slot 0 plays
  TEST_ASSERT_TRUE(X().playing(0));
  // The master starts: its Start as the firmware handles it, then pulses.
  gExternal = true;
  gNextPulseUs = gUs;
  C().transportStop(gLast, gUs, gClk.usPerTick());
  gClk.midiStart(gUs);
  C().transportStart(gClk.nextTick(), gUs, gClk.usPerTick());
  gS.transportStarted(gClk.nextTick());
  run(gUs + kBarUs + kBarUs / 3);
  TEST_ASSERT_TRUE(gClk.external());
  TEST_ASSERT_TRUE(X().playing(0));
  tap(S::kTransportNote);
  ms(400);
  TEST_ASSERT_FALSE(X().playing(0));  // stopped locally
  TEST_ASSERT_TRUE(gClk.running());    // the master's clock runs on
  const uint32_t stoppedAt = gLast;
  run(gUs + kBarUs / 2);
  TEST_ASSERT_FALSE(X().playing(0));
  TEST_ASSERT_TRUE(gLast > stoppedAt);
  tap(S::kTransportNote);
  ms(400);
  TEST_ASSERT_TRUE(C().switchQueued(0));
  const uint32_t back = until(2 * kBarUs, [] { return X().playing(0); });
  TEST_ASSERT_EQUAL_UINT32(0, back % 768);                         // on the bar line
  TEST_ASSERT_EQUAL_UINT32(0, (back - X().origin(0)) % 768);      // from the loop start
  // The master stops: the button does nothing; its Start starts.
  gClk.midiStop(gUs);
  C().transportStop(gLast, gUs, gClk.usPerTick());
  tap(S::kTransportNote);
  ms(400);
  TEST_ASSERT_FALSE(gClk.running());
  TEST_ASSERT_FALSE(X().playing(0));
}

// Copy mode (DEC-279 amendment 1, owner 2026-11-10): a slot held 600 ms starts it — its LED pulses, no long press of
// its own — and its release keeps it; a short press of another slot copies onto it (the rows into the target's own
// runs, the loop kept, one undo step; its LED flashes three times) and copy mode ends. Meanwhile a track button selects
// another track, whose slot takes the copy. The source again, or a long press of anything (its own gesture dropped),
// ends it without a copy.
static uint32_t rowsOf(uint8_t slot) {
  const ManifestId m = X().contentOf(slot);
  if (m == kNoManifest) return 0;
  const auto& d = gE.manifests().manifest(m);
  uint32_t rows = 0;
  for (uint8_t i = 0; i < d.runCount; ++i) rows += gE.runs().run(d.runs[i]).rowCount;
  return rows;
}
static void shortPress(uint8_t note) {
  gS.input(0x9F, note, 127, gUs / 1000);
  ms(60);
  gS.input(0x8F, note, 0, gUs / 1000);
  ms(20);
}
void test_copy_mode_copies_slots_across_tracks() {
  recordOneBar(0, 60);
  const uint32_t rows = rowsOf(0);
  TEST_ASSERT_TRUE(rows > 0);
  gS.input(0x9F, S::kSlotBase + 0, 127, gUs / 1000);
  ms(650);
  TEST_ASSERT_EQUAL(S::Copy::Slot, gS.copyMode());  // while still held
  gS.input(0x8F, S::kSlotBase + 0, 0, gUs / 1000);
  ms(500);
  TEST_ASSERT_EQUAL(S::Copy::Slot, gS.copyMode());
  TEST_ASSERT_FALSE(gS.popup());
  bool bright = false, dim = false;  // the source pulses
  for (int i = 0; i < 12; ++i, ms(50)) bright |= gLed[S::kSlotBase] == 127, dim |= gLed[S::kSlotBase] == 8;
  TEST_ASSERT_TRUE(bright && dim);
  shortPress(S::kSlotBase + 3);
  TEST_ASSERT_EQUAL_UINT32(rows, rowsOf(3));
  TEST_ASSERT_NOT_EQUAL(X().contentOf(0), X().contentOf(3));
  const ManifestId m3 = X().contentOf(3);
  for (uint8_t i = 0; i < gE.manifests().manifest(m3).runCount; ++i)
    TEST_ASSERT_EQUAL_UINT8(3, gE.runs().run(gE.manifests().manifest(m3).runs[i]).slot);  // its own runs
  uint8_t flashes = 0, last = 0;  // three flashes on the target
  for (int i = 0; i < 40; ++i, ms(10)) {
    if (gLed[S::kSlotBase + 3] == 127 && last != 127) ++flashes;
    last = gLed[S::kSlotBase + 3];
  }
  TEST_ASSERT_EQUAL_UINT8(3, flashes);
  TEST_ASSERT_EQUAL(S::Copy::None, gS.copyMode());  // done: the window closes
  TEST_ASSERT_EQUAL_UINT8(0, X().playingSlot(0));    // nothing launched
  shortPress(S::kSlotBase + 4);  // an ordinary press now: no copy
  ms(400);
  TEST_ASSERT_EQUAL_UINT32(0, rowsOf(4));
  // Across tracks: copy mode again, track 2 selected, its slot 2 pressed.
  gS.input(0x9F, S::kSlotBase + 0, 127, gUs / 1000);
  ms(650);
  gS.input(0x8F, S::kSlotBase + 0, 0, gUs / 1000);
  ms(100);
  tap(S::kTrackBase + 1);  // track 2
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(1, C().selectedTrack());
  shortPress(S::kSlotBase + 2);  // onto track 2's slot 2 (engine slot 10)
  TEST_ASSERT_EQUAL_UINT32(rows, rowsOf(10));
  TEST_ASSERT_FALSE(X().playing(1));
  TEST_ASSERT_EQUAL(ProductionTransport::Result::Ok, C().undo(10, gLast, gUs, gClk.usPerTick()));  // one step
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, X().contentOf(10));
  // A long press of anything ends it, its own gesture dropped (no solo).
  gS.input(0x9F, S::kSlotBase + 0, 127, gUs / 1000);  // track 2's slot 1 (empty) as the source
  ms(650);
  gS.input(0x8F, S::kSlotBase + 0, 0, gUs / 1000);
  ms(100);
  TEST_ASSERT_EQUAL(S::Copy::Slot, gS.copyMode());
  tap(S::kTrackBase + 1, 700);
  ms(100);
  TEST_ASSERT_EQUAL(S::Copy::None, gS.copyMode());
  TEST_ASSERT_FALSE(C().soloed(1));
  shortPress(S::kSlotBase + 5);  // an ordinary press again: no copy
  ms(400);
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, X().contentOf(13));
}

void test_an_armed_slot_punches_in_on_its_second_press() {
  recordOneBar(0, 60);
  tap(S::kSlotBase + 2);
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(2, C().queuedRecordSlot(0));
  tap(S::kSlotBase + 2);
  ms(400);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
  TEST_ASSERT_EQUAL_UINT8(2, X().writingSlot());
}

void test_the_record_button_arms_then_starts_or_cancels() {
  // Stopped: the record button on an empty slot arms; its second press starts the transport and the take.
  tap(S::kRecordNote);
  ms(450);
  TEST_ASSERT_FALSE(gClk.running());
  TEST_ASSERT_EQUAL_UINT8(0, C().queuedRecordSlot(0));
  tap(S::kRecordNote);
  ms(450);
  TEST_ASSERT_TRUE(gClk.running());
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
  tap(S::kRecordNote);  // the take ends at the bar
  run(gUs + kBarUs);
  // Running, track 1 stopped: an arm on its empty slot is cancelled by the second press.
  tap(S::kTrackBase + 1);
  ms(400);
  tap(S::kRecordNote);
  ms(450);
  TEST_ASSERT_EQUAL_UINT8(8, C().queuedRecordSlot(1));
  tap(S::kRecordNote);
  ms(450);
  TEST_ASSERT_EQUAL_UINT8(0xFF, C().queuedRecordSlot(1));
  // A transport start re-queues an armed Record on its first tick.
  tap(S::kTransportNote);  // stop
  ms(400);
  tap(S::kSlotBase + 0);  // track 1 slot 8: armed while stopped
  ms(400);
  tap(S::kTransportNote);  // start
  ms(400);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
  TEST_ASSERT_EQUAL_UINT8(8, X().writingSlot());
}

void test_record_button_toggles_overdub_undoes_and_clears() {
  recordOneBar(0, 60);
  const uint8_t depth = gE.history().undoDepth(0);
  tap(S::kRecordNote);  // a playing slot with content: overdub
  ms(450);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Overdubbing, X().mode());
  run(gUs + kBarUs, {{gUs + 30 * kTick, gUs + 60 * kTick, 64}});
  tap(S::kRecordNote);  // the overdub ends at once
  ms(450);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());
  run(gUs + 100 * kTick);
  TEST_ASSERT_TRUE(gE.history().undoDepth(0) > depth);
  const uint8_t after = gE.history().undoDepth(0);
  tap(S::kRecordNote), ms(100), tap(S::kRecordNote);  // double: undo (after the triple window)
  ms(500);
  TEST_ASSERT_EQUAL_UINT8(after - 1, gE.history().undoDepth(0));
  tap(S::kRecordNote, 700);  // long: clear (undoable: an empty state)
  ms(50);
  TEST_ASSERT_EQUAL(kNoManifest, X().contentOf(0));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gE.history().current(0));
  tap(S::kSlotBase + 0);  // a cleared slot is empty: its button arms Record
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(0, C().queuedRecordSlot(0));
}

void test_track_buttons_select_mute_and_solo() {
  tap(S::kTrackBase + 3);
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(3, C().selectedTrack());
  tap(S::kTrackBase + 5), ms(100), tap(S::kTrackBase + 5);  // double: mute
  ms(50);
  TEST_ASSERT_TRUE(C().muted(5));
  tap(S::kTrackBase + 6, 700);  // long: solo
  ms(50);
  TEST_ASSERT_TRUE(C().soloed(6));
  TEST_ASSERT_EQUAL_UINT8(3, C().selectedTrack());
  tap(S::kTrackSwitchNote);  // the next track
  ms(450);
  TEST_ASSERT_EQUAL_UINT8(4, C().selectedTrack());
}

void test_transport_and_play_stop_buttons() {
  recordOneBar(0, 60);
  tap(S::kTransportNote);  // stop
  ms(400);
  TEST_ASSERT_FALSE(gClk.running());
  TEST_ASSERT_FALSE(X().playing(0));
  tap(S::kTransportNote);  // start: the track plays again from the next bar line
  ms(400);
  TEST_ASSERT_TRUE(gClk.running());
  run(gUs + kBarUs);
  TEST_ASSERT_TRUE(X().playing(0));
  tap(S::kSlotBase + 3);  // armed on slot 3 ...
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(3, C().queuedRecordSlot(0));
  gS.selectSlot(0, 0);
  tap(S::kPlayStopNote);  // ... the selected track stops, its queued Record dropped
  ms(500);
  TEST_ASSERT_FALSE(X().playing(0));
  TEST_ASSERT_EQUAL_UINT8(0xFF, C().queuedRecordSlot(0));
  tap(S::kPlayStopNote);  // and plays its selected slot again
  ms(500);
  run(gUs + 60 * kTick);
  TEST_ASSERT_TRUE(X().playing(0));
}

// Owner 2026-11-06: a transport start (the button, MIDI Start) plays every track that is not muted on its selected slot.
void test_a_transport_start_plays_every_unmuted_track() {
  recordOneBar(0, 60);  // track 0, slot 0
  // Track 1 records slot 8, track 2 slot 16 (each from the bar, on the running transport).
  for (uint8_t t : {1, 2}) {
    tap(S::kTrackBase + t);
    ms(400);
    tap(S::kSlotBase + 0);
    ms(400);
    run(gUs + kBarUs);  // the take starts at the next bar
    TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
    gE.inputNoteOn(gLast, 0x90, 64, 100, gUs, gClk.usPerTick());
    run(gUs + 100 * kTick);
    gE.inputNoteOff(gLast, 0x80, 64, gUs);
    tap(S::kSlotBase + 0);  // ends at the bar
    run(gUs + kBarUs);
  }
  // Tracks 1 and 2 stopped on their own (play / stop); then the transport stops; track 2 is muted.
  for (uint8_t t : {1, 2}) {
    tap(S::kTrackBase + t);
    ms(400);
    tap(S::kPlayStopNote);
    ms(500);
    TEST_ASSERT_FALSE(X().playing(t));
  }
  tap(S::kTransportNote);  // stop: nothing plays
  ms(400);
  for (uint8_t t = 0; t < 3; ++t) TEST_ASSERT_FALSE(X().playing(t));
  C().setMute(2, true, gUs);
  tap(S::kTransportNote);  // start: track 0 resumes, stopped track 1 starts too; muted, stopped track 2 does not
  ms(400);
  run(gUs + 10 * kTick);
  TEST_ASSERT_TRUE(X().playing(0));
  TEST_ASSERT_TRUE(X().playing(1));
  TEST_ASSERT_EQUAL_UINT8(8, X().playingSlot(1));
  TEST_ASSERT_FALSE(X().playing(2));
}

void test_leds_follow_and_only_changes_are_sent() {
  recordOneBar(0, 60);
  run(gUs + 100 * kTick);
  TEST_ASSERT_EQUAL_UINT8(127, gLed[S::kTrackBase + 0]);  // selected track
  TEST_ASSERT_EQUAL_UINT8(127, gLed[S::kSlotBase + 0]);   // selected slot
  TEST_ASSERT_EQUAL_UINT8(127, gLed[S::kTransportNote]);
  TEST_ASSERT_EQUAL_UINT8(127, gLed[40]);                 // the current (only) bar
  uint8_t positions = 0;
  for (uint8_t n = 16; n < 32; ++n) positions += gLed[n] == 127;
  TEST_ASSERT_EQUAL_UINT8(1, positions);
  // The selected track moves on: track 0 keeps its content LED.
  tap(S::kTrackBase + 1);
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(32, gLed[S::kTrackBase + 0]);
  TEST_ASSERT_EQUAL_UINT8(127, gLed[S::kTrackBase + 1]);
  // Nothing changes while stopped and idle: nothing more is sent.
  tap(S::kTransportNote);
  ms(600);
  TEST_ASSERT_EQUAL_UINT8(0, gLed[S::kTransportNote]);
  const uint32_t sends = gLedSends;
  ms(500);
  TEST_ASSERT_EQUAL_UINT32(sends, gLedSends);
}

// ---- The save suggestion's evidence (sets browser slice 4) ------------------------------------------------------------
/// A Record counts as begun with others only while another track plays; the transport's own start and stop are not
/// exploring; a mute is.
void test_the_evidence_counts_takes_with_others_and_gestures_not_the_transport() {
  recordOneBar(0, 60);  // track 0 alone
  TEST_ASSERT_EQUAL_UINT32(1, C().stats().recordsStarted);
  TEST_ASSERT_EQUAL_UINT32(0, C().stats().recordsWithOthers);
  const uint32_t explore = C().stats().exploreGestures;
  tap(S::kTransportNote);  // stop
  ms(500);
  tap(S::kTransportNote);  // start: the tracks resume, not a gesture
  ms(500);
  TEST_ASSERT_TRUE(X().playing(0));
  TEST_ASSERT_EQUAL_UINT32(explore, C().stats().exploreGestures);
  tap(S::kTrackBase + 1);  // select track 1: not evidence
  ms(500);
  tap(S::kSlotBase + 0);  // track 1 slot 0 arms; it records at the bar while track 0 plays
  ms(400);
  untilRecording(3 * kBarUs);
  TEST_ASSERT_EQUAL_UINT32(2, C().stats().recordsStarted);
  TEST_ASSERT_EQUAL_UINT32(1, C().stats().recordsWithOthers);
  TEST_ASSERT_EQUAL_UINT32(explore, C().stats().exploreGestures);
  tap(S::kTrackBase + 0);
  tap(S::kTrackBase + 0);  // a double: mute track 0
  ms(500);
  TEST_ASSERT_TRUE(C().muted(0));
  TEST_ASSERT_EQUAL_UINT32(explore + 1, C().stats().exploreGestures);
}

// ---- The sets browser (slice 3) ---------------------------------------------------------------------------------------
static ProductionBrowser gBrowser;
/// A long track switch opens it (an open edit left first), record / edit move, a short track switch acts, a long one
/// closes the popup, then the browser; nothing reaches the transport or the mute meanwhile.
void test_the_browser_takes_record_track_and_edit_while_open() {
  gBrowser.clear();
  library::Record r{};
  r.sequence = 1;
  r.set = 1;
  gBrowser.add(r);
  gBrowser.setWorking(1, 1);
  gS.setBrowser(&gBrowser);
  tap(S::kTrackSwitchNote, 700);
  ms(400);
  TEST_ASSERT_TRUE(gS.browsing());
  TEST_ASSERT_FALSE(C().muted(0));  // the long press is the browser's now
  TEST_ASSERT_EQUAL_UINT16(1, gBrowser.selected());  // SAVE
  tap(38);  // edit: down, over the LOAD divider to the working set
  ms(500);
  TEST_ASSERT_EQUAL_UINT16(3, gBrowser.selected());
  tap(S::kRecordNote);  // record: up, not a take
  ms(500);
  TEST_ASSERT_EQUAL_UINT16(1, gBrowser.selected());
  TEST_ASSERT_FALSE(gClk.running());
  TEST_ASSERT_EQUAL(ProductionControl::kNone, C().queuedRecordSlot(0));
  tap(S::kTrackSwitchNote);  // SAVE: the popup
  ms(800);
  TEST_ASSERT_EQUAL(ProductionBrowser::View::Popup, gBrowser.view());
  TEST_ASSERT_EQUAL_UINT8(0, C().selectedTrack());  // not the next track
  tap(S::kTrackSwitchNote, 700);  // a long press exits every window, back to playing (DEC-279 amendment 2)
  ms(400);
  TEST_ASSERT_FALSE(gS.browsing());
  tap(S::kTrackSwitchNote, 700);
  ms(400);
  TEST_ASSERT_TRUE(gS.browsing());
  tap(38, 700);  // EDIT's long press too
  ms(400);
  TEST_ASSERT_FALSE(gS.browsing());
  TEST_ASSERT_FALSE(gS.edit().active());  // not NOTE_EDIT either
  // The working set's stage: a double TRACK moves it to the trash at once.
  tap(S::kTrackSwitchNote, 700);
  ms(400);
  tap(38);  // the working set
  ms(500);
  tap(S::kTrackSwitchNote);  // its stages
  ms(800);
  TEST_ASSERT_EQUAL(ProductionBrowser::View::Set, gBrowser.view());
  TEST_ASSERT_EQUAL_UINT16(3, gBrowser.selected());
  ProductionBrowser::Request q;
  tap(S::kTrackSwitchNote);
  ms(80);
  tap(S::kTrackSwitchNote);
  ms(800);
  TEST_ASSERT_TRUE(gBrowser.takeRequest(q));
  TEST_ASSERT_EQUAL(ProductionBrowser::Action::Trash, q.action);
  TEST_ASSERT_EQUAL_UINT16(1, q.set);
  TEST_ASSERT_EQUAL_UINT16(0, q.record);
  gBrowser.failed("TRASH FAILED", "BUSY");
  tap(S::kTrackSwitchNote, 700);  // a failure: a long press closes
  ms(400);
  TEST_ASSERT_FALSE(gS.browsing());
  tap(S::kTrackSwitchNote, 700);  // `< CLOSE`: TRACK closes
  ms(400);
  TEST_ASSERT_TRUE(gS.browsing());
  tap(S::kRecordNote);
  ms(500);
  tap(S::kTrackSwitchNote);
  ms(800);
  TEST_ASSERT_FALSE(gS.browsing());
  TEST_ASSERT_FALSE(gBrowser.takeRequest(q));
  gS.setBrowser(nullptr);
}

/// DEC-272: copy mode's LOAD LOOP (a slot's long press, then TRACK) opens the picker for that slot; there NOTELEN,
/// PLAY/STOP, REC and EDIT are the moves (up, down, left, right) — none reaches the transport, a take or a track — and a
/// long press exits to playing.
void test_a_slots_long_press_opens_the_picker_and_four_buttons_move_in_it() {
  gBrowser.clear();
  library::Record r{};
  r.sequence = 1;
  r.set = 1;
  gBrowser.add(r);
  gBrowser.setWorking(1, 1);
  gS.setBrowser(&gBrowser);
  TEST_ASSERT_EQUAL_UINT8(2, gBrowser.startPreset());  // the surface's new-loop length, 4 bars
  tap(S::kSlotBase, 700);  // track 1's slot 1, empty: copy mode (DEC-279 amendment 1)
  ms(100);
  TEST_ASSERT_EQUAL(S::Copy::Slot, gS.copyMode());
  tap(S::kTrackSwitchNote);  // LOAD LOOP: the picker for the source slot
  ms(400);
  TEST_ASSERT_EQUAL(S::Copy::None, gS.copyMode());
  TEST_ASSERT_TRUE(gS.browsing());
  TEST_ASSERT_EQUAL(ProductionBrowser::View::Picker, gBrowser.view());
  TEST_ASSERT_EQUAL_UINT8(0, gBrowser.pickDestination());
  TEST_ASSERT_EQUAL_UINT8(gBrowser.startPreset(), gBrowser.pickPreset());
  TEST_ASSERT_TRUE(X().contentOf(0) == kNoManifest);  // no empty loop made by the long press any more
  tap(S::kRecordNote);  // left: the presets' edge
  ms(600);
  TEST_ASSERT_EQUAL_UINT8(gBrowser.startPreset(), gBrowser.pickPreset());
  TEST_ASSERT_EQUAL(ProductionControl::kNone, C().queuedRecordSlot(0));  // not a take
  tap(38);  // right: into the grid, the nearest track (4)
  ms(600);
  TEST_ASSERT_EQUAL_UINT8(ProductionBrowser::kNoPreset, gBrowser.pickPreset());
  TEST_ASSERT_EQUAL_UINT8(3, gBrowser.pickTrack());
  tap(S::kLengthNote);  // up
  ms(600);
  TEST_ASSERT_EQUAL_UINT8(2, gBrowser.pickTrack());
  tap(S::kPlayStopNote);  // down, not a play
  ms(600);
  TEST_ASSERT_EQUAL_UINT8(3, gBrowser.pickTrack());
  TEST_ASSERT_FALSE(gClk.running());
  TEST_ASSERT_FALSE(X().playing(0));
  tap(S::kTrackSwitchNote, 700);  // a long press exits to playing (DEC-279 amendment 2)
  ms(400);
  TEST_ASSERT_FALSE(gS.browsing());
  ProductionBrowser::Request q;
  // Over a filled destination a preset asks first; there REC selects YES and EDIT CANCEL, as in every confirmation —
  // not the picker's moves.
  gBrowser.openPickerFromSlot(9, true);
  for (int i = 0; i < 2; ++i) {
    tap(S::kRecordNote);  // left: slot 1, then the presets
    ms(600);
  }
  TEST_ASSERT_TRUE(gBrowser.pickPreset() != ProductionBrowser::kNoPreset);
  tap(S::kTrackSwitchNote);
  ms(800);
  TEST_ASSERT_EQUAL(ProductionBrowser::View::Confirm, gBrowser.view());
  tap(S::kRecordNote);  // YES
  ms(600);
  TEST_ASSERT_EQUAL_UINT8(0, gBrowser.confirmSelected());
  tap(38);  // CANCEL
  ms(600);
  TEST_ASSERT_EQUAL_UINT8(1, gBrowser.confirmSelected());
  TEST_ASSERT_TRUE(gBrowser.pickPreset() != ProductionBrowser::kNoPreset);  // the cursor stayed
  gBrowser.close();
  gS.setBrowser(nullptr);
}

void test_no_heap() {
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  recordOneBar(0, 60);
  tap(S::kTrackBase + 2);
  ms(400);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_press_timing_matches_the_legacy_processor);
  RUN_TEST(test_the_control_plane_is_the_surfaces);
  RUN_TEST(test_slot_buttons_record_play_switch_and_mute);
  RUN_TEST(test_slot_long_press_keeps_and_record_long_clears);
  RUN_TEST(test_slot_double_press_launches_at_the_next_launch_step);
  RUN_TEST(test_step_and_bar_buttons_restart_the_playing_slot_there);
  RUN_TEST(test_copy_mode_copies_slots_across_tracks);
  RUN_TEST(test_the_transport_button_stops_and_starts_locally_under_an_external_clock);
  RUN_TEST(test_an_armed_slot_punches_in_on_its_second_press);
  RUN_TEST(test_the_record_button_arms_then_starts_or_cancels);
  RUN_TEST(test_record_button_toggles_overdub_undoes_and_clears);
  RUN_TEST(test_track_buttons_select_mute_and_solo);
  RUN_TEST(test_transport_and_play_stop_buttons);
  RUN_TEST(test_a_transport_start_plays_every_unmuted_track);
  RUN_TEST(test_leds_follow_and_only_changes_are_sent);
  RUN_TEST(test_the_evidence_counts_takes_with_others_and_gestures_not_the_transport);
  RUN_TEST(test_the_browser_takes_record_track_and_edit_while_open);
  RUN_TEST(test_a_slots_long_press_opens_the_picker_and_four_buttons_move_in_it);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
