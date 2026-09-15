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
  RuntimeTimingTelemetry::noteExternalClockBatch(1000, 4, 20833);
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 20833);
  TEST_ASSERT_FALSE(RuntimeTimingTelemetry::maybeEmit(1000));  // first call arms window

  RuntimeTimingTelemetry::noteClockDispatch(9000);
  RuntimeTimingTelemetry::noteUsbDeviceDrain(221000);
  RuntimeTimingTelemetry::noteUsbDeviceDispatch(154000);
  RuntimeTimingTelemetry::noteExternalClockBatch(1000 + 4 * 20833 + 100, 4, 20833);
  RuntimeTimingTelemetry::noteExternalClockDispatchEnter(1000 + 4 * 20833 + 6100);
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000 + 4 * 20833 + 100, 20833);
  RuntimeTimingTelemetry::noteUsbDeviceBatchComposition(12, 4, 8, 4, true, 6100);
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
  TEST_ASSERT_EQUAL_UINT32(0, after.extclkArrivalMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.extclkArrivalCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.extclkDispatchMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.extclkDispatchOverCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.clkBatchMax);
  TEST_ASSERT_EQUAL_UINT32(0, after.clkBatchCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.servicePeriodUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbBatchMax);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbBatchSaturatedCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbBatchClockMax);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbBatchConsecutiveClockMax);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbBatchSizeBuckets[0]);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbBatchClockBuckets[0]);
  TEST_ASSERT_EQUAL_UINT32(0, after.usbBatchClockDispatchMaxUs[0]);
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
  RuntimeTimingTelemetry::recordPlaybackRebuild(
      12345, 192, 384, 9, 3, 1,
      RuntimeTimingTelemetry::PlaybackRebuildPath::PublishedSnapshot);
  const auto queued = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_TRUE(queued.pendingPlaybackRebuild);
  TEST_ASSERT_EQUAL_UINT32(12345, queued.pendingRebuildDurationUs);
  TEST_ASSERT_EQUAL_UINT32(1, queued.pendingRebuildCount);
  TEST_ASSERT_EQUAL_UINT8(3, queued.pendingRebuildTrack);
  TEST_ASSERT_EQUAL_UINT8(1, queued.pendingRebuildSlot);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(RuntimeTimingTelemetry::PlaybackRebuildPath::PublishedSnapshot),
                        static_cast<int>(queued.pendingRebuildPath));
  RuntimeTimingTelemetry::emitPendingDeadlineOneShots();
  TEST_ASSERT_FALSE(RuntimeTimingTelemetry::peek().pendingPlaybackRebuild);
}

void test_playback_rebuild_keeps_maximum_and_counts_destinations() {
  RuntimeTimingTelemetry::recordPlaybackRebuild(
      4000, 0, 1536, 1, 2, 0,
      RuntimeTimingTelemetry::PlaybackRebuildPath::CompatibilityPasses);
  RuntimeTimingTelemetry::recordPlaybackRebuild(
      12000, 768, 1536, 2, 5, 1,
      RuntimeTimingTelemetry::PlaybackRebuildPath::NoteEditPreview, 20000, UINT32_MAX, 290, 256);
  RuntimeTimingTelemetry::recordPlaybackRebuild(
      8000, 1536, 1536, 3, 4, 2,
      RuntimeTimingTelemetry::PlaybackRebuildPath::PublishedSnapshot);
  const auto queued = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_TRUE(queued.pendingPlaybackRebuild);
  TEST_ASSERT_EQUAL_UINT32(12000, queued.pendingRebuildDurationUs);
  TEST_ASSERT_EQUAL_UINT32(3, queued.pendingRebuildCount);
  TEST_ASSERT_EQUAL_UINT32(20000, queued.pendingRebuildAtUs);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, queued.pendingRebuildPreviousPhase);
  TEST_ASSERT_EQUAL_UINT32(290, queued.pendingRebuildCurrentPhase);
  TEST_ASSERT_EQUAL_UINT32(256, queued.pendingRebuildEventCount);
  TEST_ASSERT_EQUAL_UINT8(5, queued.pendingRebuildTrack);
  TEST_ASSERT_EQUAL_UINT8(1, queued.pendingRebuildSlot);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(RuntimeTimingTelemetry::PlaybackRebuildPath::NoteEditPreview),
                        static_cast<int>(queued.pendingRebuildPath));
  RuntimeTimingTelemetry::emitPendingDeadlineOneShots();
  const auto cleared = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_FALSE(cleared.pendingPlaybackRebuild);
  TEST_ASSERT_EQUAL_UINT32(0, cleared.pendingRebuildCount);
}

void test_reset_playback_deadline_cadence_clears_late_clock_baseline() {
  RuntimeTimingTelemetry::recordOutgoingClockSend(1000, 20833, 2604, 0);
  RuntimeTimingTelemetry::resetPlaybackDeadlineCadence();
  RuntimeTimingTelemetry::recordOutgoingClockSend(100000, 20833, 2604, 0);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().lateClkCount);
}

void test_external_clock_batch_records_cardinality_and_service_period() {
  RuntimeTimingTelemetry::noteExternalClockBatch(1000, 0, 20833);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().clkBatchMax);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().clkBatchCount);

  RuntimeTimingTelemetry::noteExternalClockBatch(1000, 3, 20833);
  RuntimeTimingTelemetry::noteExternalClockBatch(1000 + 3 * 20833, 8, 20833);
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 20833);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(8, snap.clkBatchMax);
  TEST_ASSERT_EQUAL_UINT32(2, snap.clkBatchCount);
  TEST_ASSERT_EQUAL_UINT32(0, snap.extclkArrivalMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.extclkArrivalCount);
  TEST_ASSERT_EQUAL_UINT32(20833, snap.servicePeriodUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOnMaxUs);
}

void test_external_clock_batch_arrival_late_is_not_outgoing_clock() {
  RuntimeTimingTelemetry::noteExternalClockBatch(1000, 1, 20833);
  RuntimeTimingTelemetry::noteExternalClockBatch(1000 + 20833 + 5000, 1, 20833);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(5000, snap.extclkArrivalMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.extclkArrivalCount);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateClkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateClkCount);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOnMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOffMaxUs);
  TEST_ASSERT_FALSE(snap.pendingLateEvent);
}

void test_external_clock_dispatch_delay_from_batch_arrival() {
  RuntimeTimingTelemetry::noteExternalClockBatch(1000, 2, 20833);
  RuntimeTimingTelemetry::noteExternalClockDispatchEnter(1010);
  RuntimeTimingTelemetry::noteExternalClockDispatchEnter(1000 + 7000);
  RuntimeTimingTelemetry::noteExternalClockDispatchEnter(1000 + 20000);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(7000, snap.extclkDispatchMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.extclkDispatchOverCount);
  TEST_ASSERT_EQUAL_UINT32(2, snap.clkBatchMax);
}

void test_reset_playback_deadline_cadence_clears_external_clock_arrival() {
  RuntimeTimingTelemetry::noteExternalClockBatch(1000, 1, 20833);
  RuntimeTimingTelemetry::resetPlaybackDeadlineCadence();
  RuntimeTimingTelemetry::noteExternalClockBatch(100000, 1, 20833);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().extclkArrivalMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().extclkArrivalCount);
}

void test_usb_batch_composition_buckets_and_saturation() {
  RuntimeTimingTelemetry::noteUsbDeviceBatchComposition(0, 0, 0, 0, true, 9000);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().usbBatchMax);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().usbBatchSaturatedCount);

  RuntimeTimingTelemetry::noteUsbDeviceBatchComposition(3, 0, 3, 0, false, 400);
  RuntimeTimingTelemetry::noteUsbDeviceBatchComposition(12, 2, 10, 1, false, 1500);
  RuntimeTimingTelemetry::noteUsbDeviceBatchComposition(12, 9, 3, 8, true, 6100);
  RuntimeTimingTelemetry::noteUsbDeviceBatchComposition(32, 20, 12, 20, false, 8000);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(32, snap.usbBatchMax);
  TEST_ASSERT_EQUAL_UINT32(1, snap.usbBatchSaturatedCount);
  TEST_ASSERT_EQUAL_UINT32(20, snap.usbBatchClockMax);
  TEST_ASSERT_EQUAL_UINT32(20, snap.usbBatchConsecutiveClockMax);
  TEST_ASSERT_EQUAL_UINT32(1, snap.usbBatchSizeBuckets[0]);
  TEST_ASSERT_EQUAL_UINT32(2, snap.usbBatchSizeBuckets[1]);
  TEST_ASSERT_EQUAL_UINT32(1, snap.usbBatchSizeBuckets[2]);
  TEST_ASSERT_EQUAL_UINT32(1, snap.usbBatchClockBuckets[0]);
  TEST_ASSERT_EQUAL_UINT32(1, snap.usbBatchClockBuckets[1]);
  TEST_ASSERT_EQUAL_UINT32(1, snap.usbBatchClockBuckets[2]);
  TEST_ASSERT_EQUAL_UINT32(1500, snap.usbBatchClockDispatchMaxUs[0]);
  TEST_ASSERT_EQUAL_UINT32(6100, snap.usbBatchClockDispatchMaxUs[1]);
  TEST_ASSERT_EQUAL_UINT32(8000, snap.usbBatchClockDispatchMaxUs[2]);
}

void test_track_path_nested_spans_and_residual() {
  RuntimeTimingTelemetry::addTrackPathRebuild(4000);
  RuntimeTimingTelemetry::addTrackPathOrder(500);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().trackRebuildMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::peek().trackOrderMaxUs);

  RuntimeTimingTelemetry::beginTrackPathNested();
  RuntimeTimingTelemetry::addTrackPathRebuild(4000);
  RuntimeTimingTelemetry::addTrackPathRebuild(200);
  RuntimeTimingTelemetry::addTrackPathOrder(500);
  RuntimeTimingTelemetry::addTrackPathSend(300);
  RuntimeTimingTelemetry::addTrackPathSend(100);
  RuntimeTimingTelemetry::addTrackPathSlot(800);
  RuntimeTimingTelemetry::commitTrackPathNested(6000);

  const auto first = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(4200, first.trackRebuildMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, first.trackRebuildOverCount);
  TEST_ASSERT_EQUAL_UINT32(500, first.trackOrderMaxUs);
  TEST_ASSERT_EQUAL_UINT32(400, first.trackSendMaxUs);
  TEST_ASSERT_EQUAL_UINT32(800, first.trackSlotMaxUs);
  TEST_ASSERT_EQUAL_UINT32(100, first.trackResidMaxUs);

  RuntimeTimingTelemetry::beginTrackPathNested();
  RuntimeTimingTelemetry::addTrackPathRebuild(9000);
  RuntimeTimingTelemetry::addTrackPathOrder(200);
  RuntimeTimingTelemetry::addTrackPathSend(100);
  RuntimeTimingTelemetry::addTrackPathSlot(50);
  RuntimeTimingTelemetry::commitTrackPathNested(8000);

  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(9000, snap.trackRebuildMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.trackRebuildOverCount);
  TEST_ASSERT_EQUAL_UINT32(500, snap.trackOrderMaxUs);
  TEST_ASSERT_EQUAL_UINT32(400, snap.trackSendMaxUs);
  TEST_ASSERT_EQUAL_UINT32(800, snap.trackSlotMaxUs);
  TEST_ASSERT_EQUAL_UINT32(100, snap.trackResidMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.trackResidOverCount);
}

void test_track_path_exclusive_subtracts_nested_send() {
  RuntimeTimingTelemetry::beginTrackPathNested();
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::trackPathSendUs());
  RuntimeTimingTelemetry::addTrackPathSend(250);
  TEST_ASSERT_EQUAL_UINT32(250, RuntimeTimingTelemetry::trackPathSendUs());
  TEST_ASSERT_EQUAL_UINT32(750, RuntimeTimingTelemetry::trackPathExclusiveUs(1000, 0));
  RuntimeTimingTelemetry::addTrackPathSend(100);
  TEST_ASSERT_EQUAL_UINT32(400, RuntimeTimingTelemetry::trackPathExclusiveUs(500, 250));
  RuntimeTimingTelemetry::commitTrackPathNested(2000);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::trackPathSendUs());
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeTimingTelemetry::trackPathExclusiveUs(1000, 0));
}

void test_note_lateness_independent_of_external_clock_arrival() {
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 2604);
  RuntimeTimingTelemetry::noteExternalClockBatch(1000, 1, 20833);
  RuntimeTimingTelemetry::noteExternalClockBatch(1000 + 20833 + 4000, 1, 20833);
  RuntimeTimingTelemetry::recordNoteSendLateness(true, 1100, 8);
  RuntimeTimingTelemetry::recordNoteSendLateness(false, 1000 + 2604 + 50, 16);
  const auto snap = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOnMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateOnCount);
  TEST_ASSERT_EQUAL_UINT32(50, snap.lateOffMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.lateOffCount);
  TEST_ASSERT_EQUAL_UINT32(4000, snap.extclkArrivalMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.extclkArrivalCount);
  TEST_ASSERT_EQUAL_UINT32(0, snap.lateClkMaxUs);
}

void test_remaining_until_next_playback_service_unknown_without_cadence() {
  TEST_ASSERT_EQUAL_UINT32(
      0xFFFFFFFFu, RuntimeTimingTelemetry::remainingUntilNextPlaybackServiceUs(1000));
}

void test_remaining_until_next_playback_service_uses_due_plus_period() {
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 20000);
  TEST_ASSERT_EQUAL_UINT32(
      16000u, RuntimeTimingTelemetry::remainingUntilNextPlaybackServiceUs(5000));
  TEST_ASSERT_EQUAL_UINT32(
      0u, RuntimeTimingTelemetry::remainingUntilNextPlaybackServiceUs(21000));
  TEST_ASSERT_EQUAL_UINT32(
      0u, RuntimeTimingTelemetry::remainingUntilNextPlaybackServiceUs(22000));
}

void test_emit_interval_constant() {
  TEST_ASSERT_EQUAL_UINT32(5000000u, RuntimeTimingTelemetry::kEmitIntervalUs);
  TEST_ASSERT_EQUAL_UINT32(5000u, RuntimeTimingTelemetry::kObservationalSoftCeilingUs);
  TEST_ASSERT_EQUAL_UINT32(50000u, RuntimeTimingTelemetry::kLoopRemainderOneShotUs);
}

void test_late_sample_retains_send_due_deadline_across_delayed_emit() {
  RuntimeTimingTelemetry::resetForTest();
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1000, 100);
  RuntimeTimingTelemetry::recordNoteSendLateness(false, 1150, 42);
  // Later clock/service activity must not rewrite the queued sample.
  RuntimeTimingTelemetry::notePlaybackServiceEnter(1200, 100);
  RuntimeTimingTelemetry::recordNoteSendLateness(true, 1350, 43);
  const auto sample = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_EQUAL_UINT32(1150, sample.pendingLateSendUs);
  TEST_ASSERT_EQUAL_UINT32(1000, sample.pendingLateDueUs);
  TEST_ASSERT_EQUAL_UINT32(1100, sample.pendingLateDeadlineUs);
}

void test_late_clock_sample_retains_wrapped_deadline() {
  RuntimeTimingTelemetry::resetForTest();
  RuntimeTimingTelemetry::recordOutgoingClockSend(0xFFFFFF00u, 200, 100, 0);
  RuntimeTimingTelemetry::recordOutgoingClockSend(100, 200, 100, 8);
  const auto sample = RuntimeTimingTelemetry::peek();
  TEST_ASSERT_TRUE(sample.pendingLateEvent);
  TEST_ASSERT_EQUAL_UINT32(100, sample.pendingLateSendUs);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFC8u, sample.pendingLateDueUs);
  TEST_ASSERT_EQUAL_UINT32(44, sample.pendingLateDeadlineUs);
}

void test_stop_phase_counts_unknown_and_crossed_without_losing_max_sample() {
  using namespace RuntimeTimingTelemetry;
  resetForTest();
  recordStopGrain(0, 1, 2, 3, 4, 0, 100, 130, 20, false);
  recordStopGrain(0, 1, 2, 3, 4, 0, 200, 205, 0xFFFFFFFFu, false);
  recordStopGrain(0, 1, 2, 3, 4, 0, 300, 310, 10, false);
  auto sample = peekStopPhase(0);
  TEST_ASSERT_EQUAL_UINT32(3, sample.calls);
  TEST_ASSERT_EQUAL_UINT32(1, sample.crossed);
  TEST_ASSERT_EQUAL_UINT32(1, sample.unknown);
  TEST_ASSERT_EQUAL_UINT32(100, sample.startUs);
  TEST_ASSERT_EQUAL_UINT32(130, sample.endUs);
  TEST_ASSERT_EQUAL_UINT32(20, sample.slackUs);
  // A new command epoch must never mix destinations in one summary.
  recordStopGrain(0, 1, 2, 3, 5, 1, 400, 410, 20, false);
  TEST_ASSERT_EQUAL_UINT32(1, peekStopPhase(0).calls);
  TEST_ASSERT_EQUAL_UINT32(5, peekStopPhase(0).epoch);
  recordStopGrain(0, 1, 2, 3, 5, 1, 500, 510, 20, true);
  TEST_ASSERT_EQUAL_UINT32(0, peekStopPhase(0).calls);
}

void test_stop_boundary_retains_service_due_deadline_and_trace_lifetime() {
  using namespace RuntimeTimingTelemetry;
  resetForTest();
  notePlaybackServiceEnter(1000, 200);
  recordStopStateTransition(0, 1, 6, 7, 48, 1010);
  recordStopGrain(0, 1, 2, 3, 4, 0, 1020, 1040, 180, false);
  auto boundary = peekStopBoundary();
  TEST_ASSERT_EQUAL_UINT32(1, boundary.sequence);
  TEST_ASSERT_EQUAL_UINT32(1000, boundary.actualUs);
  TEST_ASSERT_EQUAL_UINT32(1000, boundary.dueUs);
  TEST_ASSERT_EQUAL_UINT32(1200, boundary.deadlineUs);
  TEST_ASSERT_EQUAL_UINT32(200, boundary.periodUs);
  TEST_ASSERT_TRUE(boundary.traceActive);

  // The next actual service remains distinguishable from its scheduled instant.
  notePlaybackServiceEnter(1210, 200);
  boundary = peekStopBoundary();
  TEST_ASSERT_EQUAL_UINT32(2, boundary.sequence);
  TEST_ASSERT_EQUAL_UINT32(1210, boundary.actualUs);
  TEST_ASSERT_EQUAL_UINT32(1200, boundary.dueUs);
  TEST_ASSERT_EQUAL_UINT32(1400, boundary.deadlineUs);
  recordNoteSendLateness(false, 1220, 48);
  recordStopGrain(0, 1, 2, 3, 4, 0, 1221, 1230, 170, true);
  TEST_ASSERT_TRUE(peekStopBoundary().traceActive);
  // Intermediate completion must retain the queue and trace until a terminal edge.
  notePlaybackServiceEnter(1410, 200);
  TEST_ASSERT_EQUAL_UINT32(3, peekStopBoundary().sequence);
  recordStopStateTransition(0, 1, 7, 5, 49, 1420);
  recordStopGrain(0, 1, 6, 3, 4, 0, 1411, 1421, 189, true);
  TEST_ASSERT_FALSE(peekStopBoundary().traceActive);
}

void test_overlapping_stops_preserve_trace_and_undrained_loss() {
  using namespace RuntimeTimingTelemetry;
  resetForTest();
  notePlaybackServiceEnter(1000, 200);
  recordStopStateTransition(0, 0, 6, 7, 0, 1010);
  recordStopStateTransition(1, 0, 6, 7, 0, 1020);
  recordStopStateTransition(0, 0, 7, 5, 0, 1030);
  TEST_ASSERT_TRUE(peekStopBoundary().traceActive);
  for (uint32_t i = 0; i < 140; ++i) notePlaybackServiceEnter(1200 + 200 * i, 200);
  const auto dropped = peekStopBoundary().droppedRecords;
  TEST_ASSERT_TRUE(dropped > 0);
  recordStopStateTransition(1, 0, 7, 1, 0, 9400);
  TEST_ASSERT_FALSE(peekStopBoundary().traceActive);
  recordStopStateTransition(0, 0, 6, 7, 0, 9500);
  TEST_ASSERT_TRUE(peekStopBoundary().droppedRecords >= dropped);
  emitPendingDeadlineOneShots();
  recordStopStateTransition(0, 0, 7, 5, 0, 9600);
  TEST_ASSERT_FALSE(peekStopBoundary().traceActive);
}

void test_grain_boundary_snapshot_survives_service_and_cadence_reset() {
  using namespace RuntimeTimingTelemetry;
  resetForTest();
  notePlaybackServiceEnter(1000, 200);
  uint32_t startUs = 1020;
  const auto admitted = captureStopGrainBoundary(startUs);
  notePlaybackServiceEnter(1210, 200);
  TEST_ASSERT_EQUAL_UINT32(1, admitted.sequence);
  TEST_ASSERT_EQUAL_UINT32(1200, admitted.deadlineUs);
  TEST_ASSERT_EQUAL_UINT32(2, peekStopBoundary().sequence);
  recordStopGrain(0, 0, 1, 1, 0, 0, startUs, 1230, 180, false, &admitted);
  TEST_ASSERT_EQUAL_UINT32(1, peekStopPhase(0).crossed);
  resetPlaybackDeadlineCadence();
  TEST_ASSERT_EQUAL_UINT32(0, captureStopGrainBoundary(startUs).periodUs);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_grain_boundary_snapshot_survives_service_and_cadence_reset);
  RUN_TEST(test_overlapping_stops_preserve_trace_and_undrained_loss);
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
  RUN_TEST(test_remaining_until_next_playback_service_unknown_without_cadence);
  RUN_TEST(test_remaining_until_next_playback_service_uses_due_plus_period);
  RUN_TEST(test_emit_interval_constant);
  RUN_TEST(test_stop_phase_counts_unknown_and_crossed_without_losing_max_sample);
  RUN_TEST(test_stop_boundary_retains_service_due_deadline_and_trace_lifetime);
  RUN_TEST(test_late_sample_retains_send_due_deadline_across_delayed_emit);
  RUN_TEST(test_late_clock_sample_retains_wrapped_deadline);
  RUN_TEST(test_note_send_within_tick_is_on_time);
  RUN_TEST(test_note_send_after_next_tick_is_late_and_queues_one_shot);
  RUN_TEST(test_first_late_one_shot_does_not_repeat_until_window_emit);
  RUN_TEST(test_outgoing_clock_late_only_past_one_tick_window);
  RUN_TEST(test_on_off_clock_lateness_are_independent);
  RUN_TEST(test_playback_rebuild_queues_one_shot_without_lateness);
  RUN_TEST(test_playback_rebuild_keeps_maximum_and_counts_destinations);
  RUN_TEST(test_reset_playback_deadline_cadence_clears_late_clock_baseline);
  RUN_TEST(test_external_clock_batch_records_cardinality_and_service_period);
  RUN_TEST(test_external_clock_batch_arrival_late_is_not_outgoing_clock);
  RUN_TEST(test_external_clock_dispatch_delay_from_batch_arrival);
  RUN_TEST(test_reset_playback_deadline_cadence_clears_external_clock_arrival);
  RUN_TEST(test_note_lateness_independent_of_external_clock_arrival);
  RUN_TEST(test_usb_batch_composition_buckets_and_saturation);
  RUN_TEST(test_track_path_nested_spans_and_residual);
  RUN_TEST(test_track_path_exclusive_subtracts_nested_send);
  return UNITY_END();
}
