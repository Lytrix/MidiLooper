#include <unity.h>

#include "../../src/Utils/RuntimeTimingTelemetry.cpp"

void setUp() {
  RuntimeTimingTelemetry::resetForTest();
}

void tearDown() {}

void test_midi_input_gap_recorded_between_handle_midi_input_calls() {
  RuntimeTimingTelemetry::noteMidiInputEnter(1000);
  RuntimeTimingTelemetry::noteMidiInputExit(1200);
  RuntimeTimingTelemetry::noteMidiInputEnter(5200);  // gap = 4000
  RuntimeTimingTelemetry::noteMidiInputExit(5300);

  const auto snap = RuntimeTimingTelemetry::peek(5300);
  TEST_ASSERT_EQUAL_UINT32(4000, snap.midiGapMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.midiGapOverCount);
  TEST_ASSERT_EQUAL_UINT32(200, snap.midiInputMaxUs);
}

void test_observational_over_count_uses_soft_ceiling() {
  RuntimeTimingTelemetry::noteMidiInputEnter(0);
  RuntimeTimingTelemetry::noteMidiInputExit(100);
  RuntimeTimingTelemetry::noteMidiInputEnter(100 + 6000);  // gap 6000 > soft ceiling
  RuntimeTimingTelemetry::noteMidiInputExit(100 + 6000 + 50);

  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(6000, snap.midiGapMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.midiGapOverCount);
}

void test_clock_and_tracks_accumulate_independently() {
  RuntimeTimingTelemetry::noteClockDispatch(800);
  RuntimeTimingTelemetry::noteClockDispatch(1200);
  RuntimeTimingTelemetry::noteTracksUpdate(400);
  RuntimeTimingTelemetry::noteTracksUpdate(900);
  RuntimeTimingTelemetry::noteClockPulse();
  RuntimeTimingTelemetry::noteClockPulse();

  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(1200, snap.clkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(900, snap.tracksMaxUs);
  TEST_ASSERT_EQUAL_UINT32(2, snap.clockPulses);
}

void test_usb_device_subsegments_accumulate_independently() {
  RuntimeTimingTelemetry::noteUsbDeviceRead(40);
  RuntimeTimingTelemetry::noteUsbDeviceRead(80);
  RuntimeTimingTelemetry::noteUsbDeviceDispatch(154000);
  RuntimeTimingTelemetry::noteUsbDeviceDispatch(108000);

  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(80, snap.usbreadMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.usbreadOverCount);
  TEST_ASSERT_EQUAL_UINT32(154000, snap.usbdispMaxUs);
  TEST_ASSERT_EQUAL_UINT32(2, snap.usbdispOverCount);
}

void test_usb_device_nested_sums_commit_as_one_sample() {
  RuntimeTimingTelemetry::beginUsbDeviceNested();
  RuntimeTimingTelemetry::addUsbDeviceCapture(100);
  RuntimeTimingTelemetry::addUsbDeviceCapture(200);
  RuntimeTimingTelemetry::addUsbDeviceThru(50);
  RuntimeTimingTelemetry::addUsbDeviceThru(60);
  RuntimeTimingTelemetry::addUsbDeviceClock(4000);
  RuntimeTimingTelemetry::addUsbDeviceClock(5000);
  RuntimeTimingTelemetry::addUsbDeviceNote(100);
  RuntimeTimingTelemetry::addUsbDeviceCc(40);
  RuntimeTimingTelemetry::addUsbDeviceTransport(20);
  RuntimeTimingTelemetry::commitUsbDeviceNested();

  RuntimeTimingTelemetry::beginUsbDeviceNested();
  RuntimeTimingTelemetry::addUsbDeviceCapture(10000);
  RuntimeTimingTelemetry::addUsbDeviceClock(90000);
  RuntimeTimingTelemetry::commitUsbDeviceNested();

  const auto snap = RuntimeTimingTelemetry::peek();
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
  RuntimeTimingTelemetry::addNoteAppend(5000);
  RuntimeTimingTelemetry::addNoteChange(6000);
  RuntimeTimingTelemetry::addNoteRecon(7000);
  RuntimeTimingTelemetry::addNotePair(8000);

  const auto outside = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(0, outside.noteappendMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, outside.notechgMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, outside.notereconMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, outside.notepairMaxUs);

  RuntimeTimingTelemetry::beginUsbDeviceNested();
  RuntimeTimingTelemetry::addNoteAppend(100);
  RuntimeTimingTelemetry::addNoteAppend(200);
  RuntimeTimingTelemetry::addNoteChange(300);
  RuntimeTimingTelemetry::addNoteRecon(4000);
  RuntimeTimingTelemetry::addNoteRecon(5000);
  RuntimeTimingTelemetry::addNotePair(50);
  RuntimeTimingTelemetry::addNotePair(60);
  RuntimeTimingTelemetry::commitUsbDeviceNested();

  RuntimeTimingTelemetry::beginUsbDeviceNested();
  RuntimeTimingTelemetry::addNoteChange(90000);
  RuntimeTimingTelemetry::commitUsbDeviceNested();

  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(300, snap.noteappendMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.noteappendOverCount);
  TEST_ASSERT_EQUAL_UINT32(90000, snap.notechgMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.notechgOverCount);
  TEST_ASSERT_EQUAL_UINT32(9000, snap.notereconMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.notereconOverCount);
  TEST_ASSERT_EQUAL_UINT32(110, snap.notepairMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.notepairOverCount);
}

void test_midi_input_drains_accumulate_independently() {
  RuntimeTimingTelemetry::noteUsbDeviceDrain(221000);
  RuntimeTimingTelemetry::noteUsbDeviceDrain(110000);
  RuntimeTimingTelemetry::noteDinDrain(40);
  RuntimeTimingTelemetry::noteDinDrain(80);
  RuntimeTimingTelemetry::noteUsbHostTask(1200);
  RuntimeTimingTelemetry::noteUsbHostTask(900);
  RuntimeTimingTelemetry::noteUsbHostDrain(800);
  RuntimeTimingTelemetry::noteUsbHostDrain(6001);

  const auto snap = RuntimeTimingTelemetry::peek();
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
  RuntimeTimingTelemetry::noteClockDispatch(9000);
  RuntimeTimingTelemetry::noteUsbDeviceDrain(221000);
  RuntimeTimingTelemetry::noteUsbDeviceDispatch(154000);
  TEST_ASSERT_FALSE(RuntimeTimingTelemetry::maybeEmit(1000));  // first call arms window

  RuntimeTimingTelemetry::noteClockDispatch(9000);
  RuntimeTimingTelemetry::noteUsbDeviceDrain(221000);
  RuntimeTimingTelemetry::noteUsbDeviceDispatch(154000);
  TEST_ASSERT_FALSE(
      RuntimeTimingTelemetry::maybeEmit(1000 + RuntimeTimingTelemetry::kEmitIntervalUs - 1));

  TEST_ASSERT_TRUE(RuntimeTimingTelemetry::maybeEmit(1000 + RuntimeTimingTelemetry::kEmitIntervalUs));

  const auto after = RuntimeTimingTelemetry::peek(1000 + RuntimeTimingTelemetry::kEmitIntervalUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.midiGapMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.midiInputMaxUs);
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
  TEST_ASSERT_EQUAL_UINT32(0, after.lateOnMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.lateOnCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.lateOffMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.lateOffCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.lateClkCount);
  TEST_ASSERT_TRUE(after.lateOneShotArmed);
}

void test_loop_remainder_spans_accumulate_independently() {
  RuntimeTimingTelemetry::noteIdleMaint(800);
  RuntimeTimingTelemetry::noteIdleMaint(1200);
  RuntimeTimingTelemetry::noteLoadFrame(3981504);
  RuntimeTimingTelemetry::notePersistSave(80);
  RuntimeTimingTelemetry::notePersistSave(40);

  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(1200, snap.idleMaintMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.idleMaintOverCount);
  TEST_ASSERT_EQUAL_UINT32(3981504, snap.loadFrameMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.loadFrameOverCount);
  TEST_ASSERT_EQUAL_UINT32(80, snap.persistSaveMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.persistSaveOverCount);
}

void test_idle_maint_child_rem_is_callable() {
  RuntimeTimingTelemetry::recordIdleMaintChildRem(0, 0);
  RuntimeTimingTelemetry::recordIdleMaintChildRem(1, 0);
  RuntimeTimingTelemetry::recordIdleMaintChildRem(2, 0);
}

void test_note_send_within_tick_is_on_time() {
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 2604);
  RuntimeTimingTelemetry::recordNoteSendLateness(true, 1100, 8);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOnMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOnCount);
  TEST_ASSERT_FALSE(snap.pendingLateEvent);
  TEST_ASSERT_TRUE(snap.lateOneShotArmed);
}

void test_note_send_after_next_tick_is_late_and_queues_one_shot() {
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 2604);
  RuntimeTimingTelemetry::recordNoteSendLateness(true, 1000 + 2604 + 5000, 16);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(5000, snap.lateOnMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.lateOnCount);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOffCount);
  TEST_ASSERT_TRUE(snap.pendingLateEvent);
  TEST_ASSERT_FALSE(snap.lateOneShotArmed);
}

void test_first_late_one_shot_does_not_repeat_until_window_emit() {
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 2604);
  RuntimeTimingTelemetry::recordNoteSendLateness(false, 1000 + 2604 + 100, 24);
  TEST_ASSERT_TRUE(RuntimeTimingTelemetry::peek().pendingLateEvent);
  RuntimeTimingTelemetry::emitPendingDeadlineOneShots();
  TEST_ASSERT_FALSE(RuntimeTimingTelemetry::peek().pendingLateEvent);

  RuntimeTimingTelemetry::recordNoteSendLateness(false, 1000 + 2604 + 200, 32);
  TEST_ASSERT_FALSE(RuntimeTimingTelemetry::peek().pendingLateEvent);
  TEST_ASSERT_EQUAL_UINT32(2, RuntimeTimingTelemetry::peek().lateOffCount);
  TEST_ASSERT_EQUAL_UINT32(200, RuntimeTimingTelemetry::peek().lateOffMaxUs);

  TEST_ASSERT_FALSE(RuntimeTimingTelemetry::maybeEmit(1));
  TEST_ASSERT_TRUE(RuntimeTimingTelemetry::maybeEmit(1 + RuntimeTimingTelemetry::kEmitIntervalUs));
  const auto after = RuntimeTimingTelemetry::peek(1 + RuntimeTimingTelemetry::kEmitIntervalUs);
  TEST_ASSERT_TRUE(after.lateOneShotArmed);
  TEST_ASSERT_EQUAL_UINT32(0, after.lateOffCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.lateOffMaxUs);
}

void test_outgoing_clock_late_only_past_one_tick_window() {
  RuntimeTimingTelemetry::recordOutgoingClockSend(1000, 20833, 2604, 0);
  RuntimeTimingTelemetry::recordOutgoingClockSend(1000 + 20833 + 100, 20833, 2604, 8);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().lateClkCount);

  RuntimeTimingTelemetry::recordOutgoingClockSend(1000 + 2 * 20833 + 5000, 20833, 2604, 16);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(5000 - 2604, snap.lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.lateClkCount);
  TEST_ASSERT_TRUE(snap.pendingLateEvent);
}

void test_on_off_clock_lateness_are_independent() {
  RuntimeTimingTelemetry::notePlaybackServiceEnter(0, 2604);
  RuntimeTimingTelemetry::recordNoteSendLateness(true, 2604 + 10, 1);
  RuntimeTimingTelemetry::recordNoteSendLateness(false, 100, 2);
  RuntimeTimingTelemetry::recordOutgoingClockSend(0, 20833, 2604, 0);
  RuntimeTimingTelemetry::recordOutgoingClockSend(20833 + 2604 + 7, 20833, 2604, 8);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(10, snap.lateOnMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.lateOnCount);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOffMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOffCount);
  TEST_ASSERT_EQUAL_UINT32(7, snap.lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.lateClkCount);
}

void test_playback_rebuild_queues_one_shot_without_lateness() {
  RuntimeTimingTelemetry::recordPlaybackRebuild(12345, 192, 384, 9);
  TEST_ASSERT_TRUE(RuntimeTimingTelemetry::peek().pendingPlaybackRebuild);
  RuntimeTimingTelemetry::emitPendingDeadlineOneShots();
  TEST_ASSERT_FALSE(RuntimeTimingTelemetry::peek().pendingPlaybackRebuild);
}

void test_reset_playback_deadline_cadence_clears_late_clock_baseline() {
  RuntimeTimingTelemetry::recordOutgoingClockSend(1000, 20833, 2604, 0);
  RuntimeTimingTelemetry::resetPlaybackDeadlineCadence();
  RuntimeTimingTelemetry::recordOutgoingClockSend(100000, 20833, 2604, 0);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().lateClkCount);
}

void test_emit_interval_constant() {
  TEST_ASSERT_EQUAL_UINT32(5000000u, RuntimeTimingTelemetry::kEmitIntervalUs);
  TEST_ASSERT_EQUAL_UINT32(5000u, RuntimeTimingTelemetry::kObservationalSoftCeilingUs);
  TEST_ASSERT_EQUAL_UINT32(50000u, RuntimeTimingTelemetry::kLoopRemainderOneShotUs);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_midi_input_gap_recorded_between_handle_midi_input_calls);
  RUN_TEST(test_observational_over_count_uses_soft_ceiling);
  RUN_TEST(test_clock_and_tracks_accumulate_independently);
  RUN_TEST(test_midi_input_drains_accumulate_independently);
  RUN_TEST(test_usb_device_subsegments_accumulate_independently);
  RUN_TEST(test_usb_device_nested_sums_commit_as_one_sample);
  RUN_TEST(test_note_off_nested_sums_commit_and_gate_inactive_window);
  RUN_TEST(test_maybe_emit_rate_limits_and_resets_window);
  RUN_TEST(test_loop_remainder_spans_accumulate_independently);
  RUN_TEST(test_idle_maint_child_rem_is_callable);
  RUN_TEST(test_emit_interval_constant);
  RUN_TEST(test_note_send_within_tick_is_on_time);
  RUN_TEST(test_note_send_after_next_tick_is_late_and_queues_one_shot);
  RUN_TEST(test_first_late_one_shot_does_not_repeat_until_window_emit);
  RUN_TEST(test_outgoing_clock_late_only_past_one_tick_window);
  RUN_TEST(test_on_off_clock_lateness_are_independent);
  RUN_TEST(test_playback_rebuild_queues_one_shot_without_lateness);
  RUN_TEST(test_reset_playback_deadline_cadence_clears_late_clock_baseline);
  return UNITY_END();
}
