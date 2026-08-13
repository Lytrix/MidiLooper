#include <unity.h>

#include "../../src/Utils/RuntimeTimingEnvelope.cpp"

void setUp() {
  RuntimeTimingEnvelope::resetForTest();
}

void tearDown() {}

void test_msi_gap_recorded_between_service_calls() {
  RuntimeTimingEnvelope::noteMidiServiceEnter(1000);
  RuntimeTimingEnvelope::noteMidiServiceExit(1200);
  RuntimeTimingEnvelope::noteMidiServiceEnter(5200);  // gap = 4000
  RuntimeTimingEnvelope::noteMidiServiceExit(5300);

  const auto snap = RuntimeTimingEnvelope::peek(5300);
  TEST_ASSERT_EQUAL_UINT32(4000, snap.msiMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.msiOverCount);
  TEST_ASSERT_EQUAL_UINT32(200, snap.midisvcMaxUs);
}

void test_observational_over_count_uses_soft_ceiling() {
  RuntimeTimingEnvelope::noteMidiServiceEnter(0);
  RuntimeTimingEnvelope::noteMidiServiceExit(100);
  RuntimeTimingEnvelope::noteMidiServiceEnter(100 + 6000);  // gap 6000 > soft ceiling
  RuntimeTimingEnvelope::noteMidiServiceExit(100 + 6000 + 50);

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(6000, snap.msiMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.msiOverCount);
}

void test_clock_and_tracks_accumulate_independently() {
  RuntimeTimingEnvelope::noteClockDispatch(800);
  RuntimeTimingEnvelope::noteClockDispatch(1200);
  RuntimeTimingEnvelope::noteTracksUpdate(400);
  RuntimeTimingEnvelope::noteTracksUpdate(900);
  RuntimeTimingEnvelope::noteClockPulse();
  RuntimeTimingEnvelope::noteClockPulse();

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(1200, snap.clkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(900, snap.tracksMaxUs);
  TEST_ASSERT_EQUAL_UINT32(2, snap.clockPulses);
}

void test_usb_device_subsegments_accumulate_independently() {
  RuntimeTimingEnvelope::noteUsbDeviceRead(40);
  RuntimeTimingEnvelope::noteUsbDeviceRead(80);
  RuntimeTimingEnvelope::noteUsbDeviceDispatch(154000);
  RuntimeTimingEnvelope::noteUsbDeviceDispatch(108000);

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(80, snap.usbreadMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.usbreadOverCount);
  TEST_ASSERT_EQUAL_UINT32(154000, snap.usbdispMaxUs);
  TEST_ASSERT_EQUAL_UINT32(2, snap.usbdispOverCount);
}

void test_usb_device_nested_sums_commit_as_one_sample() {
  RuntimeTimingEnvelope::beginUsbDeviceNested();
  RuntimeTimingEnvelope::addUsbDeviceCapture(100);
  RuntimeTimingEnvelope::addUsbDeviceCapture(200);
  RuntimeTimingEnvelope::addUsbDeviceThru(50);
  RuntimeTimingEnvelope::addUsbDeviceThru(60);
  RuntimeTimingEnvelope::addUsbDeviceClock(4000);
  RuntimeTimingEnvelope::addUsbDeviceClock(5000);
  RuntimeTimingEnvelope::addUsbDeviceNote(100);
  RuntimeTimingEnvelope::addUsbDeviceCc(40);
  RuntimeTimingEnvelope::addUsbDeviceTransport(20);
  RuntimeTimingEnvelope::commitUsbDeviceNested();

  RuntimeTimingEnvelope::beginUsbDeviceNested();
  RuntimeTimingEnvelope::addUsbDeviceCapture(10000);
  RuntimeTimingEnvelope::addUsbDeviceClock(90000);
  RuntimeTimingEnvelope::commitUsbDeviceNested();

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(10000, snap.usbcapMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.usbcapOverCount);
  TEST_ASSERT_EQUAL_UINT32(110, snap.usbthruMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.usbthruOverCount);
  TEST_ASSERT_EQUAL_UINT32(90000, snap.usbclkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(2, snap.usbclkOverCount);
  TEST_ASSERT_EQUAL_UINT32(100, snap.usbnoteMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.usbnoteOverCount);
  TEST_ASSERT_EQUAL_UINT32(40, snap.usbccMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.usbccOverCount);
  TEST_ASSERT_EQUAL_UINT32(20, snap.usbtransMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.usbtransOverCount);
}

void test_note_off_nested_sums_commit_and_gate_inactive_window() {
  RuntimeTimingEnvelope::addNoteAppend(5000);
  RuntimeTimingEnvelope::addNoteChange(6000);
  RuntimeTimingEnvelope::addNoteRecon(7000);
  RuntimeTimingEnvelope::addNotePair(8000);

  const auto outside = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(0, outside.noteappendMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, outside.notechgMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, outside.notereconMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, outside.notepairMaxUs);

  RuntimeTimingEnvelope::beginUsbDeviceNested();
  RuntimeTimingEnvelope::addNoteAppend(100);
  RuntimeTimingEnvelope::addNoteAppend(200);
  RuntimeTimingEnvelope::addNoteChange(300);
  RuntimeTimingEnvelope::addNoteRecon(4000);
  RuntimeTimingEnvelope::addNoteRecon(5000);
  RuntimeTimingEnvelope::addNotePair(50);
  RuntimeTimingEnvelope::addNotePair(60);
  RuntimeTimingEnvelope::commitUsbDeviceNested();

  RuntimeTimingEnvelope::beginUsbDeviceNested();
  RuntimeTimingEnvelope::addNoteChange(90000);
  RuntimeTimingEnvelope::commitUsbDeviceNested();

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(300, snap.noteappendMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.noteappendOverCount);
  TEST_ASSERT_EQUAL_UINT32(90000, snap.notechgMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.notechgOverCount);
  TEST_ASSERT_EQUAL_UINT32(9000, snap.notereconMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.notereconOverCount);
  TEST_ASSERT_EQUAL_UINT32(110, snap.notepairMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.notepairOverCount);
}

void test_midi_service_drains_accumulate_independently() {
  RuntimeTimingEnvelope::noteUsbDeviceDrain(221000);
  RuntimeTimingEnvelope::noteUsbDeviceDrain(110000);
  RuntimeTimingEnvelope::noteDinDrain(40);
  RuntimeTimingEnvelope::noteDinDrain(80);
  RuntimeTimingEnvelope::noteUsbHostTask(1200);
  RuntimeTimingEnvelope::noteUsbHostTask(900);
  RuntimeTimingEnvelope::noteUsbHostDrain(800);
  RuntimeTimingEnvelope::noteUsbHostDrain(6001);

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(221000, snap.usbdevMaxUs);
  TEST_ASSERT_EQUAL_UINT32(2, snap.usbdevOverCount);
  TEST_ASSERT_EQUAL_UINT32(80, snap.dinMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.dinOverCount);
  TEST_ASSERT_EQUAL_UINT32(1200, snap.hosttaskMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.hosttaskOverCount);
  TEST_ASSERT_EQUAL_UINT32(6001, snap.hostdrainMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.hostdrainOverCount);
}

void test_maybe_emit_rate_limits_and_resets_window() {
  RuntimeTimingEnvelope::noteClockDispatch(9000);
  RuntimeTimingEnvelope::noteUsbDeviceDrain(221000);
  RuntimeTimingEnvelope::noteUsbDeviceDispatch(154000);
  TEST_ASSERT_FALSE(RuntimeTimingEnvelope::maybeEmit(1000));  // first call arms window

  RuntimeTimingEnvelope::noteClockDispatch(9000);
  RuntimeTimingEnvelope::noteUsbDeviceDrain(221000);
  RuntimeTimingEnvelope::noteUsbDeviceDispatch(154000);
  TEST_ASSERT_FALSE(
      RuntimeTimingEnvelope::maybeEmit(1000 + RuntimeTimingEnvelope::kEmitIntervalUs - 1));

  TEST_ASSERT_TRUE(RuntimeTimingEnvelope::maybeEmit(1000 + RuntimeTimingEnvelope::kEmitIntervalUs));

  const auto after = RuntimeTimingEnvelope::peek(1000 + RuntimeTimingEnvelope::kEmitIntervalUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.clkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.clkOverCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbdevMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.dinMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.hosttaskMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.hostdrainMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbreadMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbdispMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbcapMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbthruMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbclkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbnoteMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbccMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbtransMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.noteappendMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.notechgMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.notereconMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.notepairMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.idleMaintMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.loadFrameMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.persistSaveMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.clockPulses);
}

void test_loop_remainder_spans_accumulate_independently() {
  RuntimeTimingEnvelope::noteIdleMaint(800);
  RuntimeTimingEnvelope::noteIdleMaint(1200);
  RuntimeTimingEnvelope::noteLoadFrame(3981504);
  RuntimeTimingEnvelope::notePersistSave(80);
  RuntimeTimingEnvelope::notePersistSave(40);

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(1200, snap.idleMaintMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.idleMaintOverCount);
  TEST_ASSERT_EQUAL_UINT32(3981504, snap.loadFrameMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.loadFrameOverCount);
  TEST_ASSERT_EQUAL_UINT32(80, snap.persistSaveMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.persistSaveOverCount);
}

void test_emit_interval_constant() {
  TEST_ASSERT_EQUAL_UINT32(5000000u, RuntimeTimingEnvelope::kEmitIntervalUs);
  TEST_ASSERT_EQUAL_UINT32(5000u, RuntimeTimingEnvelope::kObservationalSoftCeilingUs);
  TEST_ASSERT_EQUAL_UINT32(50000u, RuntimeTimingEnvelope::kLoopRemainderOneShotUs);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_msi_gap_recorded_between_service_calls);
  RUN_TEST(test_observational_over_count_uses_soft_ceiling);
  RUN_TEST(test_clock_and_tracks_accumulate_independently);
  RUN_TEST(test_midi_service_drains_accumulate_independently);
  RUN_TEST(test_usb_device_subsegments_accumulate_independently);
  RUN_TEST(test_usb_device_nested_sums_commit_as_one_sample);
  RUN_TEST(test_note_off_nested_sums_commit_and_gate_inactive_window);
  RUN_TEST(test_maybe_emit_rate_limits_and_resets_window);
  RUN_TEST(test_loop_remainder_spans_accumulate_independently);
  RUN_TEST(test_emit_interval_constant);
  return UNITY_END();
}
