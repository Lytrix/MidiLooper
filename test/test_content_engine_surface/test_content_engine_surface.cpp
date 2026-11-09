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
static void run(uint32_t toUs, std::initializer_list<Live> live = {}) {
  ProductionHub::Sent buf[32];
  for (; gUs < toUs; gUs += 125) {
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
  // Slot 0 again: plays from slot 1's loop end.
  tap(S::kSlotBase + 0);
  ms(400);
  TEST_ASSERT_EQUAL_UINT8(0, C().queuedSwitchSlot(0));
  run(gUs + kBarUs + kTick);
  TEST_ASSERT_EQUAL_UINT8(0, X().playingSlot(0));
  // The playing slot's button toggles the track's mute.
  tap(S::kSlotBase + 0);
  ms(400);
  TEST_ASSERT_TRUE(C().muted(0));
  tap(S::kSlotBase + 0);
  ms(400);
  TEST_ASSERT_FALSE(C().muted(0));
}

// A slot's double press clears it (owner 2026-11-08), undoably; its long press no longer clears a filled slot (it opens
// the length popup, which owns the slot button until it hides).
void test_slot_double_press_clears() {
  recordOneBar(0, 60);
  const ManifestId m = X().contentOf(0);
  gS.input(0x9F, S::kSlotBase + 0, 127, gUs / 1000);  // long: a filled slot stays
  ms(700);
  gS.input(0x8F, S::kSlotBase + 0, 0, gUs / 1000);
  ms(400);
  TEST_ASSERT_EQUAL_UINT16(m, X().contentOf(0));
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());  // no overdub either
  TEST_ASSERT_TRUE(gS.popup());
  ms(3100);  // the popup hides: the slot button is the slot's again
  TEST_ASSERT_FALSE(gS.popup());
  tap(S::kSlotBase + 0);
  ms(100);
  tap(S::kSlotBase + 0);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, X().contentOf(0));
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());
  tap(S::kRecordNote);  // undo
  ms(100);
  tap(S::kRecordNote);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(m, X().contentOf(0));
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
  gBrowser.setWorking(1, 1, false);
  gS.setBrowser(&gBrowser);
  tap(S::kTrackSwitchNote, 700);
  ms(400);
  TEST_ASSERT_TRUE(gS.browsing());
  TEST_ASSERT_FALSE(C().muted(0));  // the long press is the browser's now
  tap(38);  // edit: down
  ms(500);
  TEST_ASSERT_EQUAL_UINT16(1, gBrowser.selected());
  tap(S::kRecordNote);  // record: up, not a take
  ms(500);
  TEST_ASSERT_EQUAL_UINT16(0, gBrowser.selected());
  TEST_ASSERT_FALSE(gClk.running());
  TEST_ASSERT_EQUAL(ProductionControl::kNone, C().queuedRecordSlot(0));
  tap(S::kTrackSwitchNote);  // + SAVE MILESTONE: the popup
  ms(800);
  TEST_ASSERT_EQUAL(ProductionBrowser::View::Popup, gBrowser.view());
  TEST_ASSERT_EQUAL_UINT8(0, C().selectedTrack());  // not the next track
  tap(S::kTrackSwitchNote, 700);
  ms(400);
  TEST_ASSERT_EQUAL(ProductionBrowser::View::Top, gBrowser.view());
  tap(S::kTrackSwitchNote, 700);
  ms(400);
  TEST_ASSERT_FALSE(gS.browsing());
  ProductionBrowser::Request q;
  TEST_ASSERT_FALSE(gBrowser.takeRequest(q));
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
  RUN_TEST(test_slot_double_press_clears);
  RUN_TEST(test_an_armed_slot_punches_in_on_its_second_press);
  RUN_TEST(test_the_record_button_arms_then_starts_or_cancels);
  RUN_TEST(test_record_button_toggles_overdub_undoes_and_clears);
  RUN_TEST(test_track_buttons_select_mute_and_solo);
  RUN_TEST(test_transport_and_play_stop_buttons);
  RUN_TEST(test_a_transport_start_plays_every_unmuted_track);
  RUN_TEST(test_leds_follow_and_only_changes_are_sent);
  RUN_TEST(test_the_evidence_counts_takes_with_others_and_gestures_not_the_transport);
  RUN_TEST(test_the_browser_takes_record_track_and_edit_while_open);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
