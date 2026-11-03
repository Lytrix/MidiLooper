//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// Opt-in real Track/clock profile; the ordinary low-level prewarm suite stays unchanged.
// Supply the capture harness's firmware include path and dead-strip flags (see native_test_link.py).
#if defined(FROZEN_RECORD_READER_SURVIVAL_ORACLE)
#define UNIFIED_RECORD_RETURN_READER_ORACLE 1
#define main sharedCaptureFinalizationSuiteMain
#include "Utils/PlaybackTickContext.h"
#include "RevisionStore/RevisionStore.h"
#include "../test_shared_capture_finalization/test_shared_capture_finalization.cpp"
#undef main

static void exerciseFrozenReaderMainClockSurvival(unsigned invalidation, bool gradualFinalization = false) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 24 * bar;
  const uint32_t checkedBars = gradualFinalization ? 16 : 4;
  std::array<NoteId, 24> recordIds{};
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  for (uint32_t b = 1; b < 24; ++b) {
    t.noteOn(1, static_cast<uint8_t>(60 + b), 100, b * bar + 12);
    recordIds[b] = t.getLoop(0).capture.store.at(2 * b).noteId;
    t.noteOff(1, static_cast<uint8_t>(60 + b), 0, b * bar + 96);
  }
  Loop& loop = t.getLoop(0);
  t.noteOn(1, 90, 100, length - 40);
  t.noteOff(1, 90, 0, length - 1);
  loop.loopLengthTicks = length;
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  clockManager.assignCurrentTickSilently(length + 473);
  t.stopRecording(length + 473);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  auto* presentation = TrackCaptureStopTestAccess::presentation(t, 0);
  TEST_ASSERT_NOT_NULL(presentation);
  TEST_ASSERT_TRUE(presentation->rawRange.ready());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  nativePlaybackDeadlineClock = {length + 473, 1};
  t.playMidiEvents(length + 473, true);
  // Direct production Track admission is intentional: manager immediate-Start is a separate gate.
  t.startOverdubbing(length + 473);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_EQUAL(CapturePhase::Overdub, loop.capture.phase);
  MidiSendCapture::reset();
  // Keep Snapshot service controlled independently. Exercise the production
  // main requester too while advancing finalization a grain at a time.
  for (uint32_t tick = length + 474; tick < length + (checkedBars + 1) * bar; ++tick) {
    nativePlaybackDeadlineClock.tick = tick;
    if (tick == length + bar) MidiSendCapture::reset();
    clockManager.assignCurrentTickSilently(tick);
    if (gradualFinalization) {
      if (tick == length + 2 * bar + 200) {
        t.noteOn(1, 100, 100, tick);
        t.noteOff(1, 100, 0, tick + 40);
        t.stopOverdubbing();
      }
      if (tick == length + 3 * bar + 200) t.startOverdubbing(tick);
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
      if (t.isPlaying() && !t.isOverdubbing())
        (void)t.requestActiveCommittedPlaybackWindow(tick, nativePlaybackDeadlineClock);
    }
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    t.playMidiEvents(tick, true);
    // The partial launch bar is setup; assert every edge after each subsequent
    // tick, not just aggregate content after a publication/drain burst.
    if (tick >= length + bar) {
      const uint32_t b = (tick - length) / bar;
      const uint32_t phase = (tick - length) % bar;
      const size_t count = (b - 1) * 2 + (phase >= 12) + (phase >= 96);
      char detail[240];
      const auto* views = runtime.viewSetIfPrewarmed();
      snprintf(detail, sizeof(detail), "tick=%u roll=%u current=%u ready=%u bound=%u pending=%u boundReturn=%u", tick,
          unsigned(runtime.rolling.phase), views->lifecycle.current, views->lifecycle.ready,
          runtime.rolling.boundView, t.pendingViewRequest(0) != nullptr,
          runtime.hasBoundRecordReturnWindowForPendingJob());
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(count, MidiSendCapture::count(), detail);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(2 * checkedBars, MidiSendCapture::count());
  for (uint32_t b = 1; b <= checkedBars; ++b) {
    const auto& on = MidiSendCapture::at((b - 1) * 2);
    const auto& off = MidiSendCapture::at((b - 1) * 2 + 1);
    TEST_ASSERT_TRUE(on.isNoteOn() && off.isNoteOff());
    TEST_ASSERT_EQUAL_UINT32(b * bar + 12, on.tick);
    TEST_ASSERT_EQUAL_UINT32(b * bar + 96, off.tick);
    TEST_ASSERT_EQUAL_UINT8(1, on.channel);
    TEST_ASSERT_EQUAL_UINT8(1, off.channel);
    TEST_ASSERT_EQUAL_UINT8(60 + b, on.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(60 + b, off.data.noteData.note);
    TEST_ASSERT_EQUAL(recordIds[b], on.noteId);
    TEST_ASSERT_EQUAL(recordIds[b], off.noteId);
    TEST_ASSERT_EQUAL_UINT8(100, on.data.noteData.velocity);
    TEST_ASSERT_EQUAL_UINT8(0, off.data.noteData.velocity);
  }
  // The successor still accepts input after the observed interval.
  t.noteOn(1, 100, 100, length + (checkedBars + 1) * bar);
  t.noteOff(1, 100, 0, length + (checkedBars + 1) * bar + 40);
  TEST_ASSERT_EQUAL_UINT32(2, loop.capture.store.size());
  unsigned sounding = 0;
  runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++sounding; });
  TEST_ASSERT_EQUAL_UINT32(0, sounding);
  if (!gradualFinalization) TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(presentation->rawRange.ready());
  TEST_ASSERT_EQUAL(LoopPlaybackRuntime::RollingPhase::Serving, runtime.rolling.phase);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT8(set->lifecycle.current, runtime.rolling.boundView);
  TEST_ASSERT_EQUAL_UINT64(set->views[set->lifecycle.current].storageGeneration,
                         runtime.rolling.boundStorageGeneration);
  if (invalidation == 1) ++presentation->generation;
  if (invalidation == 2) t.forceSetState(TRACK_STOPPED);
  if (invalidation == 3) ++nativePlaybackDeadlineClock.epoch;
  if (invalidation != 0) {
    TEST_ASSERT_NOT_EQUAL(Track::RollingOutcome::Serving,
        t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock));
    TEST_ASSERT_EQUAL(LoopPlaybackRuntime::RollingPhase::Idle, runtime.rolling.phase);
  }
}
void test_frozen_record_reader_survives_alternating_main_and_overdub_ticks() {
  exerciseFrozenReaderMainClockSurvival(0);
}
void test_frozen_record_reader_still_cancels_stale_receipt() {
  exerciseFrozenReaderMainClockSurvival(1);
}
void test_frozen_record_reader_still_cancels_transport_stop() {
  exerciseFrozenReaderMainClockSurvival(2);
}
void test_frozen_record_reader_still_cancels_clock_epoch_change() {
  exerciseFrozenReaderMainClockSurvival(3);
}
void test_record_reader_survives_gradual_finalization_and_successor_reentry() {
  exerciseFrozenReaderMainClockSurvival(0, true);
}
// 121645 first-bar handoff: Record stops to STOPPED just before the wrap and transport Start resumes at the same
// tick. Start re-anchors the projection (`reanchorPlaybackProjection`), so phase zero is the Start tick and the
// take's bar 0 is owed at once, while the Record job is still private. Unlike the survival oracle above, this
// asserts the take's bar-0 events: they must sound once at their original phases, and nothing may repeat at the
// following boundary. No stop drain or pre-admitted window before the clock.
// `heldClockTicks`: the host clock keeps arriving while the transport is stopped (the owner rig's
// `--hold-clock-on-settle`), so the device tick runs on with owner turns. `clockResetOnStart`: an external
// Start resets the device tick to 0 with a new deadline epoch before the tracks start (121645 / rig run 2).
// `replaceClearedTake`: the slot first held a published, played take of the same length (pitches 90+),
// which the production `Track::clear` removed before this take was recorded (rig run 1 replayed it).
// `advanceStopJob`: each owner turn also gives the Record job one grain (Snapshot owner when publication is
// backlogged, else one stop-worker step), so the take publishes mid-scenario as it does on the device.
static void exerciseFirstBarAfterStopToStoppedRestart(uint32_t stopBeforeWrap, unsigned ownerTurnsPerTick = 1,
                                                      uint32_t heldClockTicks = 0, bool clockResetOnStart = false,
                                                      bool replaceClearedTake = false, bool advanceStopJob = false,
                                                      uint32_t jobGrainsBeforeStart = UINT32_MAX,
                                                      uint32_t notePhase = 12) {
  const uint32_t offPhase = notePhase + 84;
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 20 * bar;
  std::array<NoteId, 20> recordIds{};
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  if (replaceClearedTake) {
    t.forceSetState(TRACK_STOPPED);
    start(t, 0, false, 0, 90);
    for (uint32_t b = 1; b < 20; ++b) {
      t.noteOn(1, static_cast<uint8_t>(90 + b % 8), 100, b * bar + 12);
      t.noteOff(1, static_cast<uint8_t>(90 + b % 8), 0, b * bar + 96);
    }
    clockManager.assignCurrentTickSilently(length - 24);
    t.stopRecording(length - 24);
    TEST_ASSERT_TRUE(t.isPlaying());
    nativePlaybackDeadlineClock = {length - 24, 1};
    // Production playback ticks until the take is published and its stop retired, then two more bars.
    uint32_t settled = UINT32_MAX;
    for (uint32_t tick = length - 23; tick < 4 * length && (settled == UINT32_MAX || tick < settled); ++tick) {
      if (settled == UINT32_MAX && t.getLoop(0).publishedSnapshotIsCurrent() && !t.hasStopInFlight() &&
          !TrackCaptureStopTestAccess::recordWorkPending(t))
        settled = tick + 2 * bar;
      if (t.hasSnapshotPublicationBacklogOnAnySlot())
        t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
      else if (t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t))
        TrackCaptureStopTestAccess::step(t);
      nativePlaybackDeadlineClock.tick = tick;
      clockManager.assignCurrentTickSilently(tick);
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
      (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
      (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
      t.playMidiEvents(tick, true);
    }
    char setup[160];
    snprintf(setup, sizeof(setup), "cleared-take setup never settled: published=%d stop=%d work=%d",
        t.getLoop(0).publishedSnapshotIsCurrent(), t.hasStopInFlight(), TrackCaptureStopTestAccess::recordWorkPending(t));
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(UINT32_MAX, settled, setup);
    t.stopPlaying();
    t.clear();
    TEST_ASSERT_EQUAL_UINT32(0, t.getLoop(0).loopLengthTicks);
    nativePlaybackDeadlineClock = {0, 2};
    clockManager.assignCurrentTickSilently(0);
  }
  t.forceSetState(TRACK_STOPPED);
  t.setActiveLoopIndex(0);
  t.startRecording(0);
  TEST_ASSERT_TRUE(t.isRecording());
  for (uint32_t b = 0; b < 20; ++b) {
    t.noteOn(1, static_cast<uint8_t>(60 + b), 100, b * bar + notePhase);
    recordIds[b] = t.getLoop(0).capture.store.at(2 * b).noteId;
    t.noteOff(1, static_cast<uint8_t>(60 + b), 0, b * bar + offPhase);
  }
  Loop& loop = t.getLoop(0);
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  const uint32_t stopTick = length - stopBeforeWrap;
  stop(t, false, stopTick);
  TEST_ASSERT_TRUE(t.isStopped());
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  nativePlaybackDeadlineClock = {stopTick, 1};
  bool started = false;
  uint32_t stoppedJobGrains = 0;
  const auto turn = [&](uint32_t tick) {
    const bool jobGrain = advanceStopJob && (started || stoppedJobGrains < jobGrainsBeforeStart);
    if (jobGrain && !started) ++stoppedJobGrains;
    if (jobGrain) {
      if (t.hasSnapshotPublicationBacklogOnAnySlot())
        t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
      else if (t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t))
        TrackCaptureStopTestAccess::step(t);
    }
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
  };
  // The stopped interval before Start: owner turns at a frozen clock, then any held host clock.
  for (unsigned i = 0; i < 16; ++i) turn(stopTick);
  for (uint32_t tick = stopTick + 1; tick <= stopTick + heldClockTicks; ++tick) {
    nativePlaybackDeadlineClock.tick = tick;
    clockManager.assignCurrentTickSilently(tick);
    for (unsigned i = 0; i < ownerTurnsPerTick; ++i) turn(tick);
    t.playMidiEvents(tick, true);
  }
  const uint32_t startTick = clockResetOnStart ? 0u : stopTick + heldClockTicks;
  if (clockResetOnStart) nativePlaybackDeadlineClock = {0, nativePlaybackDeadlineClock.epoch + 1};
  clockManager.assignCurrentTickSilently(startTick);
  t.startPlaying(startTick);
  started = true;
  TEST_ASSERT_TRUE(t.isPlaying());
  const uint32_t origin = static_cast<uint32_t>(t.getProjectionCycleStartTick());
  TEST_ASSERT_EQUAL_UINT32(startTick, origin);
  MidiSendCapture::reset();
  t.playMidiEvents(startTick, true);
  for (uint32_t tick = startTick + 1; tick < origin + 2 * bar; ++tick) {
    nativePlaybackDeadlineClock.tick = tick;
    clockManager.assignCurrentTickSilently(tick);
    // The device's idle owner can take several turns inside one MIDI tick.
    for (unsigned i = 0; i < ownerTurnsPerTick; ++i) turn(tick);
    t.playMidiEvents(tick, true);
    {
      const uint32_t b = (tick - origin) / bar;
      const uint32_t phase = (tick - origin) % bar;
      const size_t count = b * 2 + (phase >= notePhase) + (phase >= offPhase);
      char detail[240];
      const auto* views = runtime.viewSetIfPrewarmed();
      const auto role = [&](uint8_t i, uint32_t& start, uint32_t& flags) {
        start = UINT32_MAX;
        flags = 0;
        if (i >= views->views.size()) return;
        const auto& v = views->views[i];
        start = v.coverage.startPhase;
        flags = (v.complete ? 1u : 0u) | (v.published ? 2u : 0u) | (v.rawCoveragePrepared ? 4u : 0u) |
            (v.firstRecordReturnWindow ? 8u : 0u) | (v.source.origin == PlaybackWindowOrigin::Capture ? 16u : 0u);
      };
      uint32_t curStart = 0, curFlags = 0, readyStart = 0, readyFlags = 0;
      role(views->lifecycle.current, curStart, curFlags);
      role(views->lifecycle.ready, readyStart, readyFlags);
      snprintf(detail, sizeof(detail),
          "jobGrains=%u stopBeforeWrap=%u tick=%u roll=%u current=%u@%u/%u ready=%u@%u/%u bound=%u boundReturn=%u",
          stoppedJobGrains, stopBeforeWrap, tick, unsigned(runtime.rolling.phase), views->lifecycle.current, curStart, curFlags,
          views->lifecycle.ready, readyStart, readyFlags, runtime.rolling.boundView,
          runtime.hasBoundRecordReturnWindowForPendingJob());
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(count, MidiSendCapture::count(), detail);
    }
  }
  for (size_t i = 0; i < MidiSendCapture::count(); ++i)
    TEST_ASSERT_LESS_THAN_UINT8_MESSAGE(90, MidiSendCapture::at(i).data.noteData.note, "cleared take replayed");
  TEST_ASSERT_EQUAL_UINT32(4, MidiSendCapture::count());
  for (uint32_t b = 0; b < 2; ++b) {
    const auto& on = MidiSendCapture::at(b * 2);
    const auto& off = MidiSendCapture::at(b * 2 + 1);
    TEST_ASSERT_TRUE(on.isNoteOn() && off.isNoteOff());
    TEST_ASSERT_EQUAL_UINT32(b * bar + notePhase, on.tick);
    TEST_ASSERT_EQUAL_UINT32(b * bar + offPhase, off.tick);
    TEST_ASSERT_EQUAL_UINT8(1, on.channel);
    TEST_ASSERT_EQUAL_UINT8(1, off.channel);
    TEST_ASSERT_EQUAL_UINT8(60 + b, on.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(60 + b, off.data.noteData.note);
    TEST_ASSERT_EQUAL(recordIds[b], on.noteId);
    TEST_ASSERT_EQUAL(recordIds[b], off.noteId);
    TEST_ASSERT_EQUAL_UINT8(100, on.data.noteData.velocity);
    TEST_ASSERT_EQUAL_UINT8(0, off.data.noteData.velocity);
  }
}
void test_first_record_bar_sounds_after_stop_to_stopped_restart_near_the_wrap() {
  exerciseFirstBarAfterStopToStoppedRestart(24);
}
void test_first_record_bar_sounds_after_stop_to_stopped_restart_at_the_wrap_edge() {
  exerciseFirstBarAfterStopToStoppedRestart(2);
}
void test_first_record_bar_sounds_after_restart_with_several_owner_turns_per_tick() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 3);
}
// Owner rig shape: the host clock is held for ~3.4 bars while stopped (the bar-0 window is prepared then),
// and the external Start resets the device tick to 0.
void test_first_record_bar_sounds_after_held_clock_and_start_clock_reset() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 2600, true);
}
void test_first_record_bar_sounds_after_held_clock_without_clock_reset() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 2600, false);
}
// Rig run 1 / run 2: the same flows over a slot whose previous same-length take was played and cleared.
void test_first_record_bar_sounds_over_a_cleared_take() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 0, false, true);
}
void test_first_record_bar_sounds_over_a_cleared_take_with_held_clock_and_reset() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 2600, true, true);
}
// The device's Record job advances while stopped and publishes around Start; the native owner turns above
// never publish it. These give the job one grain per owner turn.
void test_first_record_bar_sounds_while_the_record_job_publishes() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 0, false, false, true);
}
void test_first_record_bar_sounds_rig_shape_with_publishing_job() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 2600, true, true, true);
}
void test_first_record_bar_sounds_rig_shape_with_publishing_job_no_cleared_take() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 2600, true, false, true);
}
// Sweep the Record job's progress at the Start edge: exactly `k` job grains while stopped, then one per
// owner turn. The device met Start with the job mid-finalization; this covers every phase it can be in.
// Rig run 3 (121645 sequence on the fixed image): the bar-1 window was prepared only after Start, so an
// event in the first ticks of bar 1 was missed (device: first NoteOn at phase 24). A note at phase 2 pins it.
void test_first_record_bar_early_event_sounds_after_stop_to_stopped_restart() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 0, false, false, false, UINT32_MAX, 2);
}
void test_first_record_bar_early_event_sounds_rig_shape() {
  exerciseFirstBarAfterStopToStoppedRestart(24, 1, 2600, true, false, true, UINT32_MAX, 2);
}
void test_first_record_bar_sounds_for_every_stop_job_phase_at_start() {
  for (uint32_t k = 0; k <= 400; k += 1) {
    if (k != 0) { tearDown(); setUp(); }
    exerciseFirstBarAfterStopToStoppedRestart(24, 1, 2600, true, false, true, k);
  }
}
// chain-57/58 (owner slice-1 sessions): a dense Record take stopped to STOPPED wedged at the private canonical
// order's preflight (`preparation_denied deny=order_denied at=1`). The device takes are ~640 events (three
// chunks); the fixtures above record ~40 (one chunk). This drives the production stop job one grain at a time over
// a take of the device's density. `drainMidPass` mirrors the passing sessions, whose mid-pass persistence wrote
// the take's sealed chunks during recording; the denied sessions logged no mid-pass write at all.
static void exerciseDenseRecordStopToStopped(bool drainMidPass) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 20 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  t.setActiveLoopIndex(0);
  t.startRecording(0);
  TEST_ASSERT_TRUE(t.isRecording());
  uint32_t pairs = 0;
  for (uint32_t b = 0; b < 20; ++b) {
    for (uint32_t k = 0; k < 16; ++k) {
      const uint32_t on = b * bar + k * (bar / 16);
      const uint8_t pitch = static_cast<uint8_t>(84 - k);
      clockManager.assignCurrentTickSilently(on);
      t.noteOn(1, pitch, 100, on);
      t.noteOff(1, pitch, 0, on + (bar / 16) - 8);
      ++pairs;
    }
    if (drainMidPass) {
      uint16_t id = 0;
      while (PersistenceQueue::beginWriteQueuedChunk(id)) PersistenceQueue::markChunkPersisted(id);
    }
  }
  Loop& loop = t.getLoop(0);
  stop(t, false, length - 24);
  TEST_ASSERT_TRUE(t.isStopped());
  uint64_t turns = 0;
  while ((t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t)) && turns < 60000) {
    ++turns;
    if (t.hasSnapshotPublicationBacklogOnAnySlot())
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    else
      TrackCaptureStopTestAccess::step(t);
  }
  char msg[200];
  snprintf(msg, sizeof(msg), "dense Record stop wedged: drain=%d turns=%llu jobs=%zu phase=%u length=%u queued=%u",
           drainMidPass, static_cast<unsigned long long>(turns), TrackCaptureStopTestAccess::jobs(t),
           TrackCaptureStopTestAccess::jobs(t) ? static_cast<unsigned>(TrackCaptureStopTestAccess::phase(t)) : 99u,
           static_cast<unsigned>(loop.loopLengthTicks), static_cast<unsigned>(PersistenceQueue::queueDepth()));
  TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t), msg);
  TEST_ASSERT_TRUE_MESSAGE(loop.hasCommittedPasses(), "the dense take must commit");
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  TEST_ASSERT_EQUAL_UINT32(2 * pairs, static_cast<uint32_t>(rows.size()));
}
// chain-57/58, second hypothesis (owner): the slot clear starts a workspace save while the transport is stopped and
// Record arms before that save reaches the recording slot. This runs the **production** save scheduler alongside the
// stop job, with the clear's own persistence request (`handleClearTrack`: dirty, admit, request), from boot state.
// Regression control: it commits for every pause point, and the during-capture flush drains every sealed chunk, so
// it does not reproduce the device's zero mid-pass rows (see the plan's 2026-10-31 19:50 correction).
static void exerciseRecordStopWhileClearSaveInFlight(unsigned saveStepsBeforeRecord) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 20 * bar;
  auto& session = StorageManagerInternal::storageSession;
  const bool writesBlocked = session.bootRecovery.writesBlocked;
  session.bootRecovery.writesBlocked = false;
  // Boot state, as in every denied device session (first take after boot): no journal bound or open, empty card.
  StorageManagerInternal::resetMidPassChunkPersistState();
  SD.reset();
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  t.setActiveLoopIndex(0);
  StorageManager::markTrackSlotsMaterialDirty(0);
  StorageManager::admitTrackSlotPersistence(0);
  StorageManager::requestDeferredSaveState(looperState.getLooperState());
  const bool savePendingAtClear = StorageManager::hasDeferredSaveWork();
  for (unsigned i = 0; i < saveStepsBeforeRecord; ++i)
    StorageManager::processDeferredSaveState(looperState.getLooperState());
  t.startRecording(0);
  TEST_ASSERT_TRUE(t.isRecording());
  for (uint32_t b = 0; b < 20; ++b) {
    for (uint32_t k = 0; k < 16; ++k) {
      const uint32_t on = b * bar + k * (bar / 16);
      clockManager.assignCurrentTickSilently(on);
      t.noteOn(1, static_cast<uint8_t>(84 - k), 100, on);
      t.noteOff(1, static_cast<uint8_t>(84 - k), 0, on + (bar / 16) - 8);
    }
    StorageManager::processDeferredSaveState(looperState.getLooperState());
  }
  Loop& loop = t.getLoop(0);
  stop(t, false, length - 24);
  TEST_ASSERT_TRUE(t.isStopped());
  uint64_t turns = 0;
  while ((t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t)) && turns < 60000) {
    ++turns;
    StorageManager::processDeferredSaveState(looperState.getLooperState());
    if (t.hasSnapshotPublicationBacklogOnAnySlot())
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    else
      TrackCaptureStopTestAccess::step(t);
  }
  char msg[220];
  snprintf(msg, sizeof(msg),
           "Record stop wedged with the clear's save in flight: steps_before=%u save_pending_at_clear=%d turns=%llu "
           "jobs=%zu phase=%u chunk_queue=%u save_pending=%d",
           saveStepsBeforeRecord, savePendingAtClear, static_cast<unsigned long long>(turns),
           TrackCaptureStopTestAccess::jobs(t),
           TrackCaptureStopTestAccess::jobs(t) ? static_cast<unsigned>(TrackCaptureStopTestAccess::phase(t)) : 99u,
           static_cast<unsigned>(PersistenceQueue::queueDepth()), StorageManager::hasDeferredSaveWork());
  const bool wedged = t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t);
  session.bootRecovery.writesBlocked = writesBlocked;
  TEST_ASSERT_TRUE_MESSAGE(savePendingAtClear, "fixture precondition: the clear's save request was admitted");
  TEST_ASSERT_FALSE_MESSAGE(wedged, msg);
  TEST_ASSERT_TRUE_MESSAGE(loop.hasCommittedPasses(), "the take must commit");
}
void test_record_stop_commits_while_the_clear_save_is_in_flight() {
  for (unsigned steps : {0u, 1u, 2u, 4u, 8u}) {
    if (steps != 0) { tearDown(); setUp(); }
    exerciseRecordStopWhileClearSaveInFlight(steps);
  }
}
void test_dense_record_stop_to_stopped_commits_without_mid_pass_persistence() {
  exerciseDenseRecordStopToStopped(false);
}
void test_dense_record_stop_to_stopped_commits_with_mid_pass_persistence() {
  exerciseDenseRecordStopToStopped(true);
}
// Review control for the mid-window Capture fallback: a PLAYING stop with a note held across it (so the
// inline live handoff is refused) and input after bar 1 (so the raw bar is stale). A live rolling view
// can carry the frozen token; it must never be served after the stop. Bars 1-2 sound once each and
// nothing is left sounding.
void test_held_note_playing_stop_never_serves_the_live_capture_view() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 20 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  for (uint32_t b = 1; b < 20; ++b) {
    t.noteOn(1, static_cast<uint8_t>(60 + b), 100, b * bar + 12);
    t.noteOff(1, static_cast<uint8_t>(60 + b), 0, b * bar + 96);
  }
  Loop& loop = t.getLoop(0);
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  t.noteOn(1, 100, 100, length - 60);
  const uint32_t stopTick = length - 24;
  clockManager.assignCurrentTickSilently(stopTick);
  t.stopRecording(stopTick);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  nativePlaybackDeadlineClock = {stopTick, 1};
  MidiSendCapture::reset();
  t.playMidiEvents(stopTick, true);
  for (uint32_t tick = stopTick + 1; tick < length + 2 * bar; ++tick) {
    nativePlaybackDeadlineClock.tick = tick;
    clockManager.assignCurrentTickSilently(tick);
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    t.playMidiEvents(tick, true);
  }
  unsigned ons[2] = {0, 0};
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const auto& e = MidiSendCapture::at(i);
    if (!e.isNoteOn()) continue;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(100, e.data.noteData.note, "held-at-stop note re-triggered");
    if (e.data.noteData.note == 60 || e.data.noteData.note == 61) {
      ++ons[e.data.noteData.note - 60];
      TEST_ASSERT_EQUAL_UINT32((e.data.noteData.note - 60) * bar + 12, e.tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1, ons[0]);
  TEST_ASSERT_EQUAL_UINT32(1, ons[1]);
  unsigned sounding = 0;
  runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++sounding; });
  TEST_ASSERT_EQUAL_UINT32(0, sounding);
}
static void exerciseRecordPublicationAfterOverdubStop(bool restartOverdub) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  for (uint32_t b = 1; b < 24; ++b) {
    t.noteOn(1, static_cast<uint8_t>(60 + b), 100, b * bar + 12);
    t.noteOff(1, static_cast<uint8_t>(60 + b), 0, b * bar + 96);
  }
  Loop& loop = t.getLoop(0);
  t.noteOn(1, 90, 100, length - 40);
  t.noteOff(1, 90, 0, length - 1); // pin the final bar through canonical length normalization
  loop.loopLengthTicks = length;
  clockManager.assignCurrentTickSilently(length + 200);
  t.stopRecording(length + 200);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  t.startOverdubbing(length + 200);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  MidiSendCapture::reset();
  for (uint32_t tick = length + 201; tick < length + 6 * bar; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
    if (tick == length + bar + 200) t.noteOn(1, 100, 100, tick);
    if (tick == length + bar + 240) t.noteOff(1, 100, 0, tick);
    if (tick == length + 2 * bar + 600) {
      t.stopOverdubbing();
      TEST_ASSERT_TRUE(t.isPlaying());
      // Actual serialized worker: A publishes while B is sealed and before the next bar.
      for (unsigned turn = 0; turn < 20000 && !loop.hasCommittedPasses(); ++turn)
        t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
      TEST_ASSERT_TRUE(loop.hasCommittedPasses());
      TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
    }
    if (restartOverdub && tick == length + 4 * bar + 200) t.startOverdubbing(tick);
    if (t.isPlaying() && !t.isOverdubbing())
      (void)t.requestActiveCommittedPlaybackWindow(tick, nativePlaybackDeadlineClock);
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    t.playMidiEvents(tick, true);
    // Drive B's real finalizer and A's root publication at the production owner seam too.
    if (tick > length + 2 * bar + 600) {
      t.processDeferredIdleMaintenance(0, false,
          (tick % 2) ? MidiServiceDrain::DeferredIdleOwner::VisualCache
                     : MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    }
    size_t recordEdges = 0;
    for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
      const auto& e = MidiSendCapture::at(i);
      if ((e.isNoteOn() || e.isNoteOff()) && e.data.noteData.note >= 61 && e.data.noteData.note <= 65) {
        const uint32_t b = e.data.noteData.note - 60;
        TEST_ASSERT_EQUAL_UINT32(b * bar + (e.isNoteOn() ? 12 : 96), e.tick);
        TEST_ASSERT_EQUAL_UINT8(1, e.channel);
        TEST_ASSERT_EQUAL_UINT8(e.isNoteOn() ? 100 : 0, e.data.noteData.velocity);
        ++recordEdges;
      }
    }
    const uint32_t phase = tick - length;
    size_t expected = 0;
    for (uint32_t b = 1; b <= 5; ++b)
      expected += (phase >= b * bar + 12) + (phase >= b * bar + 96);
    auto& rt = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    const auto* set = rt.viewSetIfPrewarmed();
    char detail[180];
    snprintf(detail, sizeof(detail), "tick=%u state=%u root=%u edges=%zu rolling=%u current=%u ready=%u boundReturn=%u",
        tick, unsigned(t.getState()), loop.publishedSnapshotIsCurrent(), recordEdges,
        unsigned(rt.rolling.phase), set->lifecycle.current, set->lifecycle.ready,
        rt.hasBoundRecordReturnWindowForPendingJob());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, recordEdges, detail);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(restartOverdub, t.isOverdubbing());
}

void test_record_stream_survives_nonempty_overdub_stop_and_late_record_publication() {
  exerciseRecordPublicationAfterOverdubStop(false);
}
void test_record_stream_survives_later_overdub_during_root_return() {
  exerciseRecordPublicationAfterOverdubStop(true);
}
void test_record_note_stream_survives_four_sequential_overdub_publications() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 8 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);
  std::array<NoteId, 8> recordIds{};
  recordIds[0] = loop.capture.store.at(0).noteId;
  for (uint32_t b = 1; b < recordIds.size(); ++b) {
    t.noteOn(1, static_cast<uint8_t>(60 + b), 100, b * bar + 12);
    recordIds[b] = loop.capture.store.at(2 * b).noteId;
    t.noteOff(1, static_cast<uint8_t>(60 + b), 0, b * bar + 96);
  }
  t.noteOn(1, 90, 100, length - 40);
  t.noteOff(1, 90, 0, length - 1);
  loop.loopLengthTicks = length;
  uint32_t now = length + 200;
  clockManager.assignCurrentTickSilently(now);
  t.stopRecording(now);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  t.startOverdubbing(now);
  TEST_ASSERT_TRUE(t.isOverdubbing());

  auto serviceTick = [&](uint32_t tick) {
    now = tick;
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    t.playMidiEvents(tick, true);
    const auto owner = (tick & 1u) ? MidiServiceDrain::DeferredIdleOwner::VisualCache
                                   : MidiServiceDrain::DeferredIdleOwner::Snapshot;
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  };
  auto drainStopsWithPlayback = [&]() {
    size_t turns = 0;
    const size_t limit = 50000;
    while ((t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t) ||
            t.hasSnapshotPublicationBacklogOnAnySlot()) && turns++ < limit) {
      serviceTick(now + 1);
    }
    TEST_ASSERT_TRUE_MESSAGE(turns < limit, "four-OD fixture must retire stop and presentation owners");
    TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(), "each OD commit root must become current");
  };

  for (uint32_t cycle = 0; cycle < 4; ++cycle) {
    const uint32_t startTick = now + 32;
    clockManager.assignCurrentTickSilently(startTick);
    now = startTick;
    t.startOverdubbing(startTick);
    TEST_ASSERT_TRUE(t.isOverdubbing());
    const uint8_t pitch = static_cast<uint8_t>(100 + cycle);
    clockManager.assignCurrentTickSilently(startTick + 12);
    t.noteOn(1, pitch, 100, startTick + 12);
    clockManager.assignCurrentTickSilently(startTick + 96);
    t.noteOff(1, pitch, 0, startTick + 96);
    const uint32_t stopTick = startTick + bar + 100;
    for (uint32_t tick = startTick + 97; tick <= stopTick; ++tick) serviceTick(tick);
    t.stopOverdubbing();
    TEST_ASSERT_TRUE(t.isPlaying());
    drainStopsWithPlayback();
    TEST_ASSERT_TRUE_MESSAGE(loop.passes.hasRecordPass(), "Record pass must survive every OD commit");
    TEST_ASSERT_EQUAL_UINT32(cycle + 1, loop.passes.overdubPasses.size());
  }

  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  MidiSendCapture::reset();
  const uint32_t replayStart = ((now / length) + 1) * length;
  for (uint32_t tick = replayStart; tick < replayStart + length; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    t.playMidiEvents(tick, true);
  }
  std::vector<MidiEvent> recordEvents;
  recordEvents.reserve(16);
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if ((event.isNoteOn() || event.isNoteOff()) &&
        event.data.noteData.note >= 60 && event.data.noteData.note <= 67)
      recordEvents.push_back(event);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(16, recordEvents.size(),
      "four committed overdubs must not remove any of the eight Record pairs from playback");
  for (uint32_t b = 0; b < recordIds.size(); ++b) {
    const MidiEvent& on = recordEvents[2 * b];
    const MidiEvent& off = recordEvents[2 * b + 1];
    TEST_ASSERT_TRUE(on.isNoteOn());
    TEST_ASSERT_TRUE(off.isNoteOff());
    TEST_ASSERT_EQUAL_UINT32(b * bar + 12, on.tick);
    TEST_ASSERT_EQUAL_UINT32(b * bar + 96, off.tick);
    TEST_ASSERT_EQUAL_UINT8(60 + b, on.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(60 + b, off.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(1, on.channel);
    TEST_ASSERT_EQUAL_UINT8(1, off.channel);
    TEST_ASSERT_EQUAL(recordIds[b], on.noteId);
    TEST_ASSERT_EQUAL(recordIds[b], off.noteId);
  }
  // Owner report 2026-11-01: committed overdubs are not audible. The four overdub pitches (100..103) are committed
  // passes of the current root, so one replay traversal must sound each of them once.
  uint32_t overdubOns = 0, overdubOffs = 0;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if (event.data.noteData.note < 100 || event.data.noteData.note > 103) continue;
    if (event.isNoteOn()) ++overdubOns;
    else if (event.isNoteOff()) ++overdubOffs;
  }
  char detail[120];
  snprintf(detail, sizeof(detail), "overdub replay ons=%u offs=%u passes=%u", overdubOns, overdubOffs,
           unsigned(loop.passes.overdubPasses.size()));
  TEST_MESSAGE(detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, overdubOns, "each committed overdub sounds once per traversal");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, overdubOffs, "each committed overdub sounds once per traversal");
}

void test_record_note_stream_survives_four_stops_before_predecessor_publication() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 25 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);
  std::array<NoteId, 25> recordIds{};
  recordIds[0] = loop.capture.store.at(0).noteId;
  for (uint32_t b = 1; b < recordIds.size(); ++b) {
    t.noteOn(1, static_cast<uint8_t>(60 + b), 100, b * bar + 12);
    recordIds[b] = loop.capture.store.at(2 * b).noteId;
    t.noteOff(1, static_cast<uint8_t>(60 + b), 0, b * bar + 96);
  }
  t.noteOn(1, 90, 100, length - 40);
  t.noteOff(1, 90, 0, length - 1);
  loop.loopLengthTicks = length;
  uint32_t now = length + 200;
  clockManager.assignCurrentTickSilently(now);
  t.stopRecording(now);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(t.hasStopInFlight());
  TEST_ASSERT_FALSE(loop.passes.hasRecordPass());
  t.startOverdubbing(now);
  TEST_ASSERT_TRUE(t.isOverdubbing());

  // Admit four real Stop descriptors while the Record predecessor remains at the FIFO head.
  // Do not service deferred owners between these button-equivalent Track actions.
  for (uint32_t cycle = 0; cycle < 4; ++cycle) {
    const uint32_t startTick = now + 32;
    clockManager.assignCurrentTickSilently(startTick);
    t.startOverdubbing(startTick);
    TEST_ASSERT_TRUE(t.isOverdubbing());
    const uint8_t pitch = static_cast<uint8_t>(100 + cycle);
    clockManager.assignCurrentTickSilently(startTick + 12);
    t.noteOn(1, pitch, 100, startTick + 12);
    clockManager.assignCurrentTickSilently(startTick + 96);
    t.noteOff(1, pitch, 0, startTick + 96);
    now = startTick + bar + 100;
    clockManager.assignCurrentTickSilently(now);
    t.stopOverdubbing();
    TEST_ASSERT_TRUE(t.isPlaying());
  }
  TEST_ASSERT_EQUAL_UINT32(5, TrackCaptureStopTestAccess::jobs(t));
  TEST_ASSERT_FALSE_MESSAGE(loop.passes.hasRecordPass(),
      "Record must remain private while the four successor stops are admitted");

  // Advance one complete subsequent loop while the production playback and deferred owners
  // interleave. Capture only the actual emitted MIDI; display/frame/source counters are irrelevant.
  MidiSendCapture::reset();
  const uint32_t replayStart = ((now / length) + 1) * length;
  uint32_t routeTicks[5] = {};
  for (uint32_t tick = replayStart; tick < replayStart + length; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    const auto routeBefore = PlaybackTickContext::routeCounts();
    t.playMidiEvents(tick, true);
    const auto routeAfter = PlaybackTickContext::routeCounts();
    for (int r = 0; r < 5; ++r) routeTicks[r] += routeAfter.route[r] - routeBefore.route[r];
    const auto owner = (tick & 1u) ? MidiServiceDrain::DeferredIdleOwner::VisualCache
                                   : MidiServiceDrain::DeferredIdleOwner::Snapshot;
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.passes.hasRecordPass(), "Record pass must remain committed after four Stops");
  TEST_ASSERT_EQUAL_UINT32(4, loop.passes.overdubPasses.size());
  std::vector<MidiEvent> recordEvents;
  recordEvents.reserve(50);
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if ((event.isNoteOn() || event.isNoteOff()) &&
        event.data.noteData.note >= 60 && event.data.noteData.note <= 84)
      recordEvents.push_back(event);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(50, recordEvents.size(),
      "all 25 committed Record NoteOn/Off pairs must remain in audible playback after four Stops");
  {
    // Owner report 2026-11-01 (long-loop shape): the four committed overdubs must also sound in the replay.
    uint32_t overdubOns = 0;
    for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
      const MidiEvent& event = MidiSendCapture::at(i);
      if (event.isNoteOn() && event.data.noteData.note >= 100 && event.data.noteData.note <= 103) ++overdubOns;
    }
    char detail[120];
    snprintf(detail, sizeof(detail), "long-loop overdub replay ons=%u root=%u routes view=%u bridge=%u paged=%u merged=%u none=%u",
             overdubOns, loop.publishedSnapshotIsCurrent() ? 1u : 0u, routeTicks[0], routeTicks[1], routeTicks[2],
             routeTicks[3], routeTicks[4]);
    TEST_MESSAGE(detail);
    // BASELINE (owner report 2026-11-01, overdubs not audible): with the Record publication still pending, the frozen
    // Record readers keep serving Record-only copies over bars 0-2 after the overdubs committed there, so one replay
    // traversal sounds 1 of the 4 committed overdubs (the 8-bar sibling sounds 4 of 4). The repair inverts this to 4.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, overdubOns, "BASELINE: committed overdubs masked by the frozen Record readers");
  }
  {
    // revision-manifest-prototype B.2c: the same committed passes as a RevisionStore manifest. Record + the four
    // overdubs (merge order) become revision 5 at their commits; every two-bar window of that manifest must hold each
    // committed row inside it, so all four overdubs are present whatever the Record publication is doing.
    using namespace RevisionStore;
    using B2cCap = Capacity<256, 32, 8, 64, 8, 8, 8>;
    static Store<B2cCap> store;
    static PassEvent window[512];
    store.reset();
    std::vector<const CommittedChunkIdList*> passLists{&loop.passes.recordPass.committedChunkIds};
    std::vector<const OverdubPass*> overdubs;
    for (const OverdubPass& pass : loop.passes.overdubPasses) overdubs.push_back(&pass);
    std::sort(overdubs.begin(), overdubs.end(),
              [](const OverdubPass* a, const OverdubPass* b) { return a->mergeSequence < b->mergeSequence; });
    for (const OverdubPass* pass : overdubs) passLists.push_back(&pass->committedChunkIds);
    uint32_t committedRows = 0;
    for (size_t p = 0; p < passLists.size(); ++p) {
      const CommittedChunkIdList* one[] = {passLists[p]};
      SessionMidiEventVec rows;
      CommittedEventRange::full(one, 1, length).appendTo(rows);
      TEST_ASSERT_EQUAL(AdmitResult::Ok, store.beginPass(p == 0 ? PassKind::Record : PassKind::Overdub, length));
      for (const MidiEvent& e : rows) {
        const uint8_t status = static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
        TEST_ASSERT_EQUAL(AppendResult::Ok, store.appendMidi(e.tick, status, e.data.noteData.note,
                                                             e.data.noteData.velocity, e.isNoteOn() ? e.noteId : 0));
      }
      committedRows += static_cast<uint32_t>(rows.size());
      store.commitPass();
    }
    TEST_ASSERT_EQUAL_UINT32(5, store.manifest(store.current()).revision);
    uint32_t windowRows = 0, storeOverdubOns = 0;
    for (uint32_t from = 0; from < length; from += 2 * bar) {
      const ComposeStamp stamp = store.composeWindow(store.current(), from, std::min(from + 2 * bar, length), window, 512);
      TEST_ASSERT_EQUAL(ComposeStatus::Complete, stamp.status);
      TEST_ASSERT_EQUAL_UINT32(5, stamp.revision);
      windowRows += stamp.count;
      for (uint32_t i = 0; i < stamp.count; ++i) {
        if ((window[i].status & 0xF0) == 0x90 && window[i].data2 > 0 && window[i].data1 >= 100 && window[i].data1 <= 103) {
          ++storeOverdubOns;
        }
      }
    }
    // The production content authority at the same moment: is the content right and only the serving wrong?
    SessionMidiEventVec effective;
    const bool effectiveOk = loop.tryCopyCurrentSnapshotEditViewEvents(effective);
    uint32_t effectiveOverdubOns = 0;
    for (const MidiEvent& e : effective) {
      if (e.isNoteOn() && e.data.noteData.note >= 100 && e.data.noteData.note <= 103) ++effectiveOverdubOns;
    }
    char detail[180];
    snprintf(detail, sizeof(detail),
             "B2C store rev=5 committed_rows=%u window_rows=%u overdub_ons=%u; production effective_view ok=%u rows=%u "
             "overdub_ons=%u; production replay 1",
             committedRows, windowRows, storeOverdubOns, effectiveOk ? 1u : 0u, unsigned(effective.size()),
             effectiveOverdubOns);
    TEST_MESSAGE(detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(committedRows, windowRows, "every committed row is in exactly one window");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, storeOverdubOns, "the manifest holds all four committed overdubs");
  }
  for (uint32_t b = 0; b < recordIds.size(); ++b) {
    const MidiEvent& on = recordEvents[2 * b];
    const MidiEvent& off = recordEvents[2 * b + 1];
    TEST_ASSERT_TRUE(on.isNoteOn());
    TEST_ASSERT_TRUE(off.isNoteOff());
    TEST_ASSERT_EQUAL_UINT32(b * bar + 12, on.tick);
    TEST_ASSERT_EQUAL_UINT32(b * bar + 96, off.tick);
    TEST_ASSERT_EQUAL_UINT8(60 + b, on.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(60 + b, off.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(1, on.channel);
    TEST_ASSERT_EQUAL_UINT8(1, off.channel);
    TEST_ASSERT_EQUAL(recordIds[b], on.noteId);
    TEST_ASSERT_EQUAL(recordIds[b], off.noteId);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_record_stream_survives_nonempty_overdub_stop_and_late_record_publication);
  RUN_TEST(test_record_stream_survives_later_overdub_during_root_return);
  RUN_TEST(test_frozen_record_reader_survives_alternating_main_and_overdub_ticks);
  RUN_TEST(test_frozen_record_reader_still_cancels_stale_receipt);
  RUN_TEST(test_frozen_record_reader_still_cancels_transport_stop);
  RUN_TEST(test_record_note_stream_survives_four_sequential_overdub_publications);
  RUN_TEST(test_record_note_stream_survives_four_stops_before_predecessor_publication);
  RUN_TEST(test_frozen_record_reader_still_cancels_clock_epoch_change);
  RUN_TEST(test_record_reader_survives_gradual_finalization_and_successor_reentry);
  RUN_TEST(test_first_record_bar_sounds_after_stop_to_stopped_restart_near_the_wrap);
  RUN_TEST(test_first_record_bar_sounds_after_stop_to_stopped_restart_at_the_wrap_edge);
  RUN_TEST(test_first_record_bar_sounds_after_restart_with_several_owner_turns_per_tick);
  RUN_TEST(test_first_record_bar_sounds_after_held_clock_and_start_clock_reset);
  RUN_TEST(test_first_record_bar_sounds_after_held_clock_without_clock_reset);
  RUN_TEST(test_first_record_bar_sounds_over_a_cleared_take);
  RUN_TEST(test_first_record_bar_sounds_over_a_cleared_take_with_held_clock_and_reset);
  RUN_TEST(test_first_record_bar_sounds_while_the_record_job_publishes);
  RUN_TEST(test_first_record_bar_sounds_rig_shape_with_publishing_job);
  RUN_TEST(test_first_record_bar_sounds_rig_shape_with_publishing_job_no_cleared_take);
  RUN_TEST(test_first_record_bar_sounds_for_every_stop_job_phase_at_start);
  RUN_TEST(test_first_record_bar_early_event_sounds_after_stop_to_stopped_restart);
  RUN_TEST(test_first_record_bar_early_event_sounds_rig_shape);
  RUN_TEST(test_held_note_playing_stop_never_serves_the_live_capture_view);
  RUN_TEST(test_dense_record_stop_to_stopped_commits_without_mid_pass_persistence);
  RUN_TEST(test_dense_record_stop_to_stopped_commits_with_mid_pass_persistence);
  RUN_TEST(test_record_stop_commits_while_the_clear_save_is_in_flight);
  return UNITY_END();
}
#else
#define PLAYBACK_SOURCE_MASK_QUALIFICATION 1
#include "../test_support/PlaybackSourceMaskFixture.h"
#include <unity.h>
#include <random>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/Loop.cpp"
#include "../../src/Track/RuntimePlaybackView.cpp"
#include "../../src/Track/RuntimePlaybackOrderBuilder.cpp"
#include "../../src/Track/RuntimePlaybackBridge.cpp"
#include "../../src/Track/RuntimeCaptureCompanions.cpp"
#include "../../src/Loop/LoopPreparedCompanionHistory.cpp"
#include "../../src/Track/RuntimeNotePairLookup.cpp"
#include "../../src/Track/RuntimeCompanionOverlapWalk.cpp"
#include "../../src/Track/RuntimeCompanionSpanWalk.cpp"
#include "../../src/Track/RuntimeCompanionReconstruction.cpp"
#include "../../src/Track/RuntimeCompanionApply.cpp"
#include "../../src/Track/RuntimeCompanionBridgeMap.cpp"
#include "../../src/Track/RuntimeCompanionPasses.cpp"
#include "../../src/Track/RuntimeCanonicalPassInputs.cpp"
#include "../../src/Utils/PlaybackCursorAdvance.cpp"
#include "../../src/Track/RuntimePlaybackViewFill.cpp"
#include "../../src/Track/RuntimePlaybackViewBuild.cpp"
#include "../../src/Track/TrackPlaybackRuntime.cpp"
#include "../../src/StorageManager/PersistedLoopSnapshotRelease.cpp"
#include "../../src/PassReclaim.cpp"
#include "../../src/Track/TrackUndoRetirement.cpp"
#include "../../src/Track/TrackUndoPublication.cpp"
#include "RevisionStore/RevisionImport.h"
#include "RevisionStore/ManifestWindowCursor.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "../../src/Track/TrackPreparedStopCommit.cpp"
#include "../../src/Track/TrackPreparedUndo.cpp"
#include "Loop.h"
#include "TrackPlaybackRuntime.h"
#include "Utils/MidiServiceDrain.h"
#include "Utils/DisplayWindowUtils.h"

static_assert(PLAYBACK_WINDOW_ROLLING_ENABLED == 0, "default build keeps rolling disabled");

// Production deferred cleanup consumes the admitted job's owned capture preview.
// These fixtures assert canonical/bridge/undo bookkeeping, so the preview content
// is irrelevant; one shared buffer keeps the call sites unchanged otherwise.
static CapturePreview stopCleanupPreview;

void test_playback_runtime_slot_is_stable_after_prewarm_touch() {
  TrackPlaybackRuntime runtime;
  LoopPlaybackRuntime& first = runtime.slot(0);
  LoopPlaybackRuntime& second = runtime.slot(0);
  TEST_ASSERT_EQUAL_PTR(&first, &second);
}

void test_playback_order_alloc_is_stable_after_prewarm_touch() {
  Loop loop;
  PlaybackOrderVec& first = loop.getPlaybackOrder();
  PlaybackOrderVec& second = loop.getPlaybackOrder();
  TEST_ASSERT_EQUAL_PTR(&first, &second);
}

void test_boot_playback_prewarm_does_not_allocate_capture_workers_and_upgrades_in_place() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmCaptureStaging());
  TEST_ASSERT_NULL(runtime.viewSetIfPrewarmed());
  auto* staging = runtime.captureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL(staging);
  const auto* events = staging->storage.events;
  TEST_ASSERT_EQUAL(0, runtime.frozenCaptureCompanions.capacity());
  TEST_ASSERT_TRUE(runtime.prewarmCaptureStaging());
  TEST_ASSERT_EQUAL_PTR(events, runtime.captureStagingIfPrewarmed()->storage.events);
  TEST_ASSERT_TRUE(staging->storage.append(MidiEvent::NoteOn(0, 1, 60, 100)));
  TEST_ASSERT_FALSE(runtime.prewarmViews());
  TEST_ASSERT_EQUAL(1, staging->storage.eventCount);
  TEST_ASSERT_NULL(runtime.viewSetIfPrewarmed());
  staging->reset();
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_NOT_NULL(runtime.viewSetIfPrewarmed());
  TEST_ASSERT_EQUAL_PTR(events, runtime.viewSetIfPrewarmed()->frozenCaptureStaging().storage.events);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewConfig::kRecordsPerBridge, runtime.frozenCaptureCompanions.capacity());
}

void test_capture_staging_roles_exchange_without_sharing_physical_storage() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto* live = runtime.liveCaptureStagingIfPrewarmed();
  auto* frozen = runtime.captureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL(live);
  TEST_ASSERT_NOT_NULL(frozen);
  // Distinct physical staging: a frozen job can never alias live ingress rows.
  TEST_ASSERT_TRUE(live->storage.events != frozen->storage.events);
  TEST_ASSERT_TRUE(live->storage.order != frozen->storage.order);

  TEST_ASSERT_TRUE(live->storage.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(live->storage.append(MidiEvent::NoteOff(40, 1, 60, 0)));
  live->captureGeneration = 5;
  TEST_ASSERT_TRUE(live->freeze(5));
  runtime.exchangeCaptureStagingRoles();

  // Constant-work exchange: the frozen job keeps its rows and the successor
  // ingress role is the previous, still-clean buffer.
  TEST_ASSERT_EQUAL_PTR(live, runtime.captureStagingIfPrewarmed());
  TEST_ASSERT_EQUAL_PTR(frozen, runtime.liveCaptureStagingIfPrewarmed());
  TEST_ASSERT_TRUE(runtime.captureStagingIfPrewarmed()->frozen);
  TEST_ASSERT_EQUAL_UINT32(2, runtime.captureStagingIfPrewarmed()->storage.eventCount);
  TEST_ASSERT_FALSE(runtime.liveCaptureStagingIfPrewarmed()->frozen);
  TEST_ASSERT_EQUAL_UINT32(0, runtime.liveCaptureStagingIfPrewarmed()->storage.eventCount);

  // Successor ingress rows land in the other buffer and survive the job's
  // retired staging being reset.
  TEST_ASSERT_TRUE(runtime.liveCaptureStagingIfPrewarmed()->storage.append(
      MidiEvent::NoteOn(193, 1, 62, 100)));
  runtime.captureStagingIfPrewarmed()->reset();
  TEST_ASSERT_EQUAL_UINT32(1, runtime.liveCaptureStagingIfPrewarmed()->storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(193, runtime.liveCaptureStagingIfPrewarmed()->storage.events[0].tick);
}

void test_staging_only_runtime_finishes_capture_orders_without_admitting_a_bridge() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmCaptureStaging());
  auto& staging = *runtime.captureStagingIfPrewarmed();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(20, 1, 60, 100)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOff(90, 1, 60, 0)));
  TEST_ASSERT_TRUE(staging.freeze(0));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(0));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCaptureCanonicalOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(100, 0));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCaptureOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(runtime.frozenCaptureCandidateReady(0));
  TEST_ASSERT_NULL(runtime.viewSetIfPrewarmed());
  TEST_ASSERT_FALSE(runtime.canSelectPreparedFullLoopBridge({}, 100, {}));
}

void test_full_take_admissibility_follows_the_windowed_policy() {
  // RC-MEM-2 ties the whole-take reservation to the same policy that decides whether a
  // full-loop window can be built at all: past `kMaxDetailedWindowBars` the windowed
  // representation is the only authoritative one.
  Loop loop;
  loop.loopLengthTicks = DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(loop.fullTakePlaybackViewAdmissible());
  loop.loopLengthTicks += 1;
  TEST_ASSERT_FALSE(loop.fullTakePlaybackViewAdmissible());
  loop.loopLengthTicks = 24 * Config::TICKS_PER_BAR;
  TEST_ASSERT_FALSE(loop.fullTakePlaybackViewAdmissible());
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(loop.fullTakePlaybackViewAdmissible());
  loop.loopLengthTicks = 0;
  TEST_ASSERT_TRUE(loop.fullTakePlaybackViewAdmissible());
}

void test_whole_take_reservation_is_released_once_the_loop_is_known_long() {
  // A record arm on an EMPTY slot has no length yet, so the whole-take bundle is legitimately
  // reserved. Once the loop is known to be long (the next cold admission, e.g. overdub start)
  // the reservation must be returned rather than held for the session -- otherwise the
  // 715 KB would stay for the whole session even though nothing can publish into it.
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set.wholeTakeViewsReserved());
  const size_t wholeTakeBytes = set.fixedExternalBytes();

  TEST_ASSERT_TRUE(runtime.prewarmViews(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                       /*fullTakeAdmissible=*/false));
  TEST_ASSERT_FALSE(set.wholeTakeViewsReserved());
  TEST_ASSERT_TRUE(set.fixedExternalBytes() < wholeTakeBytes);
  for (uint8_t i = 0; i < RuntimePlaybackViewConfig::kBridgeCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(0, set.bridges[i].capacity);
    TEST_ASSERT_EQUAL_UINT32(0, set.bridgeBases[i].storage.capacity);
    TEST_ASSERT_NULL(set.bridgeBases[i].passOrigins);
    TEST_ASSERT_NULL(set.bridgeBases[i].canonicalRanks);
  }
  TEST_ASSERT_FALSE(set.beginCandidateBridge());
  // The windowed views and both stagings survive the release.
  for (uint8_t i = 0; i < RuntimePlaybackViewConfig::kViewCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kEventsPerView,
                             set.views[i].storage.capacity);
  }
  TEST_ASSERT_TRUE(set.captureStagingsPrewarmed());
}

void test_whole_take_release_reprewarm_recreates_side_arrays() {
  RuntimePlaybackViewSet set;
  TEST_ASSERT_TRUE(set.prewarm(true));
  for (int cycle = 0; cycle < 3; ++cycle) {
    TEST_ASSERT_TRUE(set.releaseWholeTakeViews());
    for (const auto& base : set.bridgeBases) {
      TEST_ASSERT_NULL(base.passOrigins);
      TEST_ASSERT_NULL(base.canonicalRanks);
      TEST_ASSERT_FALSE(base.passOriginsReady);
      TEST_ASSERT_FALSE(base.canonicalRanksReady);
    }
    TEST_ASSERT_TRUE(set.prewarm(true));
    for (auto& base : set.bridgeBases) {
      TEST_ASSERT_NOT_NULL(base.passOrigins);
      TEST_ASSERT_NOT_NULL(base.canonicalRanks);
      base.passOrigins[RuntimePlaybackViewConfig::kEventsPerView - 1] = {1, 2};
      base.canonicalRanks[RuntimePlaybackViewConfig::kEventsPerView - 1] = 3;
    }
  }
}

void test_release_refuses_while_a_bridge_is_selected() {
  // The selected bridge is the live authoritative representation; releasing its backing
  // would be a use-after-free, so the release must be refused until it is deselected.
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  set.bridgeLifecycle.selected = 0;
  const auto* origins = set.bridgeBases[0].passOrigins;
  const auto* ranks = set.bridgeBases[0].canonicalRanks;
  TEST_ASSERT_FALSE(set.releaseWholeTakeViews());
  TEST_ASSERT_EQUAL_PTR(origins, set.bridgeBases[0].passOrigins);
  TEST_ASSERT_EQUAL_PTR(ranks, set.bridgeBases[0].canonicalRanks);
  TEST_ASSERT_TRUE(set.wholeTakeViewsReserved());
  set.bridgeLifecycle.reset();
  TEST_ASSERT_TRUE(set.releaseWholeTakeViews());
  TEST_ASSERT_FALSE(set.wholeTakeViewsReserved());
}

void test_windowed_only_slot_reserves_no_whole_take_representation() {
  // RC-MEM-2: a loop that can never publish a full-take view (every full-take publisher
  // requires an exactly full-loop coverage interval) must reserve only what the windowed lanes
  // and capture admission actually use: the three rolling views and both capture stagings.
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                       /*fullTakeAdmissible=*/false));
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set.prewarmed);
  TEST_ASSERT_FALSE(set.wholeTakeViewsReserved());

  for (uint8_t i = 0; i < RuntimePlaybackViewConfig::kViewCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kEventsPerView,
                             set.views[i].storage.capacity);
  }
  TEST_ASSERT_TRUE(set.captureStagingsPrewarmed());

  // The whole-take part is genuinely absent, not merely unused.
  for (uint8_t i = 0; i < RuntimePlaybackViewConfig::kBridgeCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(0, set.bridges[i].capacity);
    TEST_ASSERT_EQUAL_UINT32(0, set.bridgeBases[i].storage.capacity);
  }

  // `fixedExternalBytes()` is the windowed reservation only, and the difference from a
  // whole-take slot is exactly the two bridges plus their two immutable bases (512,000 B on
  // the device).
  constexpr size_t eventBytes = sizeof(MidiEvent) + sizeof(uint16_t) + sizeof(uint32_t) +
                                sizeof(uint16_t) + sizeof(uint8_t);
  const size_t windowedBytes = set.fixedExternalBytes();
  const size_t viewsAndStaging =
      RuntimePlaybackViewConfig::kViewCount * RuntimePlaybackViewConfig::kEventsPerView *
          (eventBytes + sizeof(PlaybackPassEventOrigin) + sizeof(uint16_t)) +
      RuntimePlaybackViewConfig::kCaptureStagingEvents * eventBytes * set.captureStagings.size();
  TEST_ASSERT_EQUAL_UINT64(viewsAndStaging, windowedBytes);

  LoopPlaybackRuntime wholeTake;
  TEST_ASSERT_TRUE(wholeTake.prewarmViews());
  TEST_ASSERT_TRUE(wholeTake.viewSetIfPrewarmed()->wholeTakeViewsReserved());
  const size_t wholeTakeBytes = wholeTake.viewSetIfPrewarmed()->fixedExternalBytes();
  TEST_ASSERT_TRUE(wholeTakeBytes > windowedBytes);
  const size_t expectedDead =
      RuntimePlaybackViewConfig::kBridgeCount * RuntimePlaybackViewConfig::kEventsPerView *
          (eventBytes + sizeof(PlaybackPassEventOrigin) + sizeof(uint16_t)) +
      RuntimePlaybackViewConfig::kBridgeCount *
          (RuntimePlaybackViewConfig::kRecordsPerBridge *
           (sizeof(PlaybackBridgeRecord) + eventBytes + 2 * sizeof(uint32_t) +
            3 * sizeof(uint16_t) + sizeof(uint8_t)) +
           RuntimePlaybackViewConfig::kEventsPerView *
               (6 * sizeof(uint16_t) + sizeof(uint32_t) + 2 * sizeof(uint8_t)));
  TEST_ASSERT_EQUAL_UINT64(expectedDead, wholeTakeBytes - windowedBytes);
}

void test_windowed_only_slot_refuses_the_whole_take_publishers() {
  // The reservation and the publication contract are the same decision. The whole-take
  // representation is the bridges (incremental full-take publication) and the prepared pair
  // (which requires `isFullLoopMergedPlaybackWindow`), and both must deny for a slot that
  // reserved neither. The `MidiEvent*` stopped-oracle overload is deliberately *not* in this
  // set: it publishes whatever window the caller gathered, which is how the windowed lane
  // paints, so it must keep working with a windowed-only reservation.
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                       /*fullTakeAdmissible=*/false));
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();

  LoopPlaybackRuntime wholeTake;
  TEST_ASSERT_TRUE(wholeTake.prewarmViews());
  RuntimePlaybackViewSet& wholeTakeSet = *wholeTake.viewSetIfPrewarmed();

  // Differential on the whole-take representation itself: the same call is admitted for a
  // whole-take slot and refused for a windowed-only one.
  TEST_ASSERT_TRUE(wholeTakeSet.beginCandidateBridge());
  TEST_ASSERT_FALSE(set.beginCandidateBridge());
  TEST_ASSERT_FALSE(set.selectCandidateBridge(EmissionCursor{}));

  // A full-loop canonical oracle publication requires an exactly full-loop coverage, which
  // only a whole-take reservation can back.
  LoopPasses passes;
  const MidiEvent source[] = {MidiEvent::NoteOn(20, 1, 60, 100),
                              MidiEvent::NoteOff(20, 1, 60)};
  const PlaybackViewSourceIdentity identity{17, 31, reinterpret_cast<const void*>(0x1234)};
  const PlaybackCoverageInterval fullCoverage{0, 768, 768};
  const RuntimeCanonicalPassInputs::CurrentSource live{17, 31, 768, 0};
  TEST_ASSERT_FALSE(runtime.publishStoppedCanonicalOracleView(passes, identity, live, source, 2,
                                                             fullCoverage));

  // The windowed oracle publication still works: a partial coverage is a legitimate
  // windowed representation, and the three views are reserved for exactly this.
  const PlaybackCoverageInterval windowedCoverage{0, 384, 768};
  TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(source, 2, identity, windowedCoverage));
  TEST_ASSERT_EQUAL_UINT32(384,
                           set.views[set.lifecycle.current].coverage.lengthTicks);
}

void test_windowed_slot_upgrades_to_whole_take_at_a_cold_admission() {
  // A slot admitted windowed-only (a long loop) can be armed again for a loop that has since
  // become short. Record arm and overdub start are cold capture-admission boundaries, so the
  // upgrade allocates there and the full-take publication then succeeds.
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                       /*fullTakeAdmissible=*/false));
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  TEST_ASSERT_FALSE(set.wholeTakeViewsReserved());
  const size_t windowedBytes = set.fixedExternalBytes();
  const auto* liveEvents = set.liveCaptureStaging().storage.events;

  TEST_ASSERT_TRUE(runtime.prewarmViews(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                       /*fullTakeAdmissible=*/true));
  TEST_ASSERT_TRUE(set.wholeTakeViewsReserved());
  TEST_ASSERT_TRUE(set.fixedExternalBytes() > windowedBytes);
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kRecordsPerBridge, set.bridges[0].capacity);
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kEventsPerView,
                           set.bridgeBases[0].storage.capacity);
  // The upgrade reuses the admitted staging instead of resetting it.
  TEST_ASSERT_EQUAL_PTR(liveEvents, set.liveCaptureStaging().storage.events);

  // The upgrade really re-arms the whole-take path: the bridge candidate that the windowed
  // slot refused is now admitted, and the full-loop view becomes selectable.
  TEST_ASSERT_TRUE(set.beginCandidateBridge());
  const MidiEvent source[] = {MidiEvent::NoteOn(20, 1, 60, 100)};
  const PlaybackViewSourceIdentity identity{17, 31, reinterpret_cast<const void*>(0x1234)};
  TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(source, 1, identity,
                                                    PlaybackCoverageInterval{0, 768, 768}));
  TEST_ASSERT_NOT_NULL(runtime.publishedFullLoopView(identity, 768));
}

void test_runtime_view_set_prewarm_has_fixed_capacity_and_no_growth() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_NULL(runtime.viewSetIfPrewarmed());
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& views = *runtime.viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(views.prewarmed);
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kRecordsPerBridge, runtime.frozenCaptureCompanions.capacity());
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kEventsPerView,
                           views.views[0].storage.capacity);
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kRecordsPerBridge,
                           views.bridges[0].capacity);
  const size_t expectedBytes =
      (RuntimePlaybackViewConfig::kViewCount + RuntimePlaybackViewConfig::kBridgeCount) * RuntimePlaybackViewConfig::kEventsPerView *
          (sizeof(MidiEvent) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t) + sizeof(PlaybackPassEventOrigin) + sizeof(uint16_t)) +
      RuntimePlaybackViewConfig::kBridgeCount *
          (RuntimePlaybackViewConfig::kRecordsPerBridge *
              (sizeof(PlaybackBridgeRecord) + sizeof(MidiEvent) + 2 * sizeof(uint16_t) +
               3 * sizeof(uint32_t) + 2 * sizeof(uint8_t) + 3 * sizeof(uint16_t)) +
           RuntimePlaybackViewConfig::kEventsPerView * (6 * sizeof(uint16_t) + sizeof(uint32_t) + 2 * sizeof(uint8_t))) +
      RuntimePlaybackViewConfig::kCaptureStagingEvents *
          (sizeof(MidiEvent) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t)) *
          views.captureStagings.size();
  TEST_ASSERT_EQUAL_UINT64(expectedBytes, views.fixedExternalBytes());

  FixedPlaybackEventStorage& storage = views.views[0].storage;
  const size_t capacity = storage.capacity;
  for (size_t i = 0; i < capacity; ++i) {
    TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(static_cast<uint32_t>(i), 1, 60, 100)));
  }
  TEST_ASSERT_FALSE(storage.append(MidiEvent::NoteOn(9999, 1, 60, 100)));
  TEST_ASSERT_EQUAL_UINT32(capacity, storage.capacity);
}

void test_active_runtime_access_does_not_create_an_unprewarmed_view_set() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_NULL(runtime.viewSetIfPrewarmed());
  TEST_ASSERT_NULL(runtime.viewSet);
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_NOT_NULL(runtime.viewSetIfPrewarmed());
}

void test_stopped_oracle_publishes_a_fixed_current_view_without_growth() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  const MidiEvent source[] = {MidiEvent::NoteOn(20, 1, 60, 100),
                              MidiEvent::NoteOff(20, 1, 60),
                              MidiEvent::ControlChange(10, 1, 7, 100)};
  const PlaybackViewSourceIdentity identity{17, 31, reinterpret_cast<const void*>(0x1234)};
  const PlaybackCoverageInterval coverage{0, 768, 768};
  const size_t capacity = set.views[set.lifecycle.current].storage.capacity;

  TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(source, 3, identity, coverage));
  const RuntimePlaybackView& current = set.views[set.lifecycle.current];
  TEST_ASSERT_TRUE(current.published);
  TEST_ASSERT_TRUE(current.complete);
  TEST_ASSERT_EQUAL_UINT32(capacity, current.storage.capacity);
  TEST_ASSERT_EQUAL_UINT32(3, current.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(2, current.storage.order[0]);
  TEST_ASSERT_EQUAL_UINT32(1, current.storage.order[1]);
  TEST_ASSERT_EQUAL_UINT32(0, current.storage.order[2]);
  TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(source, 3, identity, coverage));
  TEST_ASSERT_EQUAL_UINT32(capacity, current.storage.capacity);
}

void test_full_loop_view_selector_requires_exact_identity_and_coverage() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  const MidiEvent source[] = {MidiEvent::NoteOn(20, 1, 60, 100)};
  const PlaybackViewSourceIdentity identity{17, 31, reinterpret_cast<const void*>(0x1234)};
  const PlaybackCoverageInterval fullCoverage{0, 768, 768};
  TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(source, 1, identity, fullCoverage));
  TEST_ASSERT_NOT_NULL(runtime.publishedFullLoopView(identity, 768));

  TEST_ASSERT_NULL(runtime.publishedFullLoopView(
      PlaybackViewSourceIdentity{18, 31, reinterpret_cast<const void*>(0x1234)}, 768));
  TEST_ASSERT_NULL(runtime.publishedFullLoopView(
      PlaybackViewSourceIdentity{17, 32, reinterpret_cast<const void*>(0x1234)}, 768));
  TEST_ASSERT_NULL(runtime.publishedFullLoopView(
      PlaybackViewSourceIdentity{17, 31, reinterpret_cast<const void*>(0x5678)}, 768));
  TEST_ASSERT_NULL(runtime.publishedFullLoopView(identity, 767));

  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  RuntimePlaybackView& current = set.views[set.lifecycle.current];
  current.coverage = PlaybackCoverageInterval{32, 736, 768};
  TEST_ASSERT_NULL(runtime.publishedFullLoopView(identity, 768));
}

void test_runtime_view_coverage_wrap_and_ledger_summary_are_stable() {
  PlaybackCoverageInterval coverage{1400, 300, 1536};
  TEST_ASSERT_TRUE(coverage.contains(1400));
  TEST_ASSERT_TRUE(coverage.contains(1535));
  TEST_ASSERT_TRUE(coverage.contains(0));
  TEST_ASSERT_TRUE(coverage.contains(163));
  TEST_ASSERT_FALSE(coverage.contains(164));

  ActiveNoteLedger ledger;
  MidiEvent on = MidiEvent::NoteOn(12, 1, 60, 99);
  on.noteId = 7;
  ledger.applyPlaybackEvent(1, on);
  LedgerBoundarySummary summary;
  summary.capture(ledger);
  TEST_ASSERT_TRUE(summary.matches(ledger));
  ledger.noteOff(1, 60);
  TEST_ASSERT_FALSE(summary.matches(ledger));
}

void test_capture_note_reservation_releases_its_slot_after_matching_off() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  CaptureNoteReservation* first = staging.reserve(5, 1, 60, 1, 1, 1);
  TEST_ASSERT_NOT_NULL(first);
  staging.close(*first);
  TEST_ASSERT_NULL(staging.matchingOpenReservation(5, 1, 60));
  TEST_ASSERT_NOT_NULL(staging.reserve(6, 1, 60, 1, 1, 1));
}

void test_capture_note_reservation_rejects_when_its_full_lifetime_will_not_fit() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  staging.storage.eventCount = staging.storage.capacity - 1;
  TEST_ASSERT_NULL(staging.reserve(7, 1, 60, 2, 2, 1));
  TEST_ASSERT_NOT_NULL(staging.reserve(7, 1, 60, 1, 2, 1));
}

void test_capture_reservation_can_precede_canonical_note_identity_consumption() {
  Loop loop;
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  CaptureAdmissionStaging& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  const NoteId prospectiveId = loop.peekNextNoteId();
  TEST_ASSERT_NOT_NULL(staging.reserve(prospectiveId, 1, 60, 2, 2, 1));
  TEST_ASSERT_EQUAL_UINT32(prospectiveId, loop.peekNextNoteId());
  TEST_ASSERT_EQUAL_UINT32(prospectiveId, loop.allocateNoteId());
  TEST_ASSERT_NOT_NULL(staging.matchingOpenReservation(prospectiveId, 1, 60));
}

void test_capture_note_reservations_cover_every_open_note_slot() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  for (size_t index = 0; index < RuntimePlaybackViewConfig::kMaxCaptureNoteReservations;
       ++index) {
    TEST_ASSERT_NOT_NULL(staging.reserve(static_cast<NoteId>(index + 1), 1,
                                         static_cast<uint8_t>(index), 2, 2, 1));
  }
  TEST_ASSERT_NULL(staging.reserve(1000, 1, 127, 2, 2, 1));
}

void test_capture_staging_freeze_keeps_only_reserved_closures_available() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  staging.captureGeneration = 23;
  CaptureNoteReservation* first = staging.reserve(41, 1, 60, 2, 2, 1);
  CaptureNoteReservation* second = staging.reserve(42, 1, 61, 2, 2, 1);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32(2, staging.openReservationCount());

  TEST_ASSERT_FALSE(staging.freeze(22));
  TEST_ASSERT_TRUE(staging.freeze(23));
  TEST_ASSERT_TRUE(staging.frozen);
  TEST_ASSERT_EQUAL_UINT32(23, staging.frozenGeneration);
  TEST_ASSERT_NULL(staging.reserve(43, 1, 62, 2, 2, 1));

  CaptureNoteReservation* closure = staging.firstOpenReservation();
  TEST_ASSERT_NOT_NULL(closure);
  TEST_ASSERT_EQUAL_UINT32(41, closure->noteId);
  staging.close(*closure);
  TEST_ASSERT_EQUAL_UINT32(1, staging.openReservationCount());
  TEST_ASSERT_EQUAL_UINT32(42, staging.firstOpenReservation()->noteId);
}

void test_frozen_capture_staging_consumes_only_the_reserved_terminal_noteoff() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  CaptureNoteReservation* reservation = staging.reserve(71, 2, 64, 2, 2, 1);
  TEST_ASSERT_NOT_NULL(reservation);
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(10, 2, 64, 100)));
  staging.captureGeneration = 5;
  TEST_ASSERT_TRUE(staging.freeze(5));

  MidiEvent wrongNote = MidiEvent::NoteOff(20, 2, 65);
  wrongNote.noteId = 71;
  TEST_ASSERT_FALSE(staging.appendReservedNoteOff(wrongNote));
  TEST_ASSERT_EQUAL_UINT32(1, staging.openReservationCount());

  MidiEvent terminal = MidiEvent::NoteOff(20, 2, 64);
  terminal.noteId = 71;
  TEST_ASSERT_TRUE(staging.appendReservedNoteOff(terminal));
  TEST_ASSERT_EQUAL_UINT32(0, staging.openReservationCount());
  TEST_ASSERT_EQUAL_UINT32(2, staging.storage.eventCount);
  TEST_ASSERT_FALSE(staging.appendReservedNoteOff(terminal));
}

void test_capture_staging_reserves_independent_events_before_freeze() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  TEST_ASSERT_TRUE(staging.reserveIndependentEvent());
  TEST_ASSERT_EQUAL_UINT32(1, staging.reservedBridgeCredits);
  staging.releaseIndependentEvent();
  TEST_ASSERT_EQUAL_UINT32(0, staging.reservedBridgeCredits);

  staging.captureGeneration = 7;
  TEST_ASSERT_TRUE(staging.freeze(7));
  TEST_ASSERT_FALSE(staging.reserveIndependentEvent());
}

void test_capture_staging_reseeds_a_cross_wrap_note_with_a_fresh_closure_reservation() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  MidiEvent held = MidiEvent::NoteOn(0, 2, 64, 90);
  held.noteId = 94;
  TEST_ASSERT_TRUE(staging.appendContinuationNoteOn(held));
  TEST_ASSERT_EQUAL_UINT32(1, staging.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, staging.openReservationCount());
  TEST_ASSERT_NOT_NULL(staging.matchingOpenReservation(94, 2, 64));

  MidiEvent terminal = MidiEvent::NoteOff(26, 2, 64);
  terminal.noteId = 94;
  TEST_ASSERT_TRUE(staging.appendReservedNoteOff(terminal));
  TEST_ASSERT_EQUAL_UINT32(2, staging.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, staging.openReservationCount());
}

void test_frozen_capture_filter_excludes_a_completed_short_pair() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  MidiEvent on = MidiEvent::NoteOn(100, 1, 60, 100);
  on.noteId = 91;
  TEST_ASSERT_TRUE(staging.appendContinuationNoteOn(on));
  MidiEvent off = MidiEvent::NoteOff(104, 1, 60);
  off.noteId = 91;
  TEST_ASSERT_TRUE(staging.appendReservedNoteOff(off, false));

  RuntimePlaybackOrderBuilder canonical;
  TEST_ASSERT_TRUE(canonical.beginCanonicalCapture(staging.storage));
  while (!canonical.complete()) {
    (void)canonical.step(1);
  }
  TEST_ASSERT_EQUAL_UINT32(0, canonical.orderedEventCount());
}

void test_filtered_capture_rewrite_trims_rows_in_later_grains() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  MidiEvent on = MidiEvent::NoteOn(100, 1, 60, 100);
  on.noteId = 92;
  TEST_ASSERT_TRUE(set.frozenCaptureStaging().appendContinuationNoteOn(on));
  MidiEvent off = MidiEvent::NoteOff(104, 1, 60);
  off.noteId = 92;
  TEST_ASSERT_TRUE(set.frozenCaptureStaging().appendReservedNoteOff(off, false));
  set.frozenCaptureStaging().captureGeneration = 61;
  TEST_ASSERT_TRUE(set.frozenCaptureStaging().freeze(61));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(61));
  while (!runtime.frozenCaptureCanonicalOrderComplete()) {
    (void)runtime.stepFrozenCaptureCanonicalOrder(1);
  }
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_TRUE(store.append(off));
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  for (size_t step = 0; step < 8 && result == LoopEventStoreRewriteResult::Pending; ++step) {
    result = runtime.stepFrozenCaptureCanonicalRewrite(store, 61, 1);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_TRUE(store.empty());
}

void test_canonical_copy_uses_reserved_output_at_maximum_staging_capacity() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prewarmCanonicalCaptureOutput());
  auto& staging = *runtime.captureStagingIfPrewarmed();
  const auto count = RuntimePlaybackViewConfig::kCaptureStagingEvents;
  for (size_t i = 0; i < count; ++i)
    TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(count - i, 1, 7, i % 128)));
  staging.captureGeneration = 61;
  TEST_ASSERT_TRUE(staging.freeze(61));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(61));
  while (!runtime.frozenCaptureCanonicalOrderComplete())
    (void)runtime.stepFrozenCaptureCanonicalOrder(32);
  LoopEventStore::ChunkReservation pressure;
  TEST_ASSERT_TRUE(pressure.reserveChunks(LoopEventStore::freeChunkCount()));
  const auto capacity = runtime.canonicalCaptureOutput.store.chunkIds().capacity();
  auto result = LoopEventStoreRewriteResult::Pending;
  size_t grains = 0;
  while (result == LoopEventStoreRewriteResult::Pending && grains++ <= count) {
    const auto before = runtime.canonicalCaptureOutput.store.size();
    result = runtime.stepFrozenCaptureCanonicalCopy(61, 32);
    TEST_ASSERT_TRUE(runtime.canonicalCaptureOutput.store.size() - before <= 32);
    TEST_ASSERT_EQUAL(0, LoopEventStore::freeChunkCount());
    TEST_ASSERT_EQUAL(capacity, runtime.canonicalCaptureOutput.store.chunkIds().capacity());
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL((count + 31) / 32, grains);
  TEST_ASSERT_EQUAL(count, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureChunks.availableChunks());
  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL(i + 1, runtime.canonicalCaptureOutput.store.at(i).tick);
    TEST_ASSERT_EQUAL(count - i, staging.storage.events[i].tick);
  }
}

void test_canonical_copy_denies_stale_generation_and_replaced_output() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prewarmCanonicalCaptureOutput());
  auto& staging = *runtime.captureStagingIfPrewarmed();
  for (size_t i = 0; i < 64; ++i)
    TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(64 - i, 1, 7, 1)));
  staging.captureGeneration = 62;
  TEST_ASSERT_TRUE(staging.freeze(62));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(62));
  while (!runtime.frozenCaptureCanonicalOrderComplete())
    (void)runtime.stepFrozenCaptureCanonicalOrder(32);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending, runtime.stepFrozenCaptureCanonicalCopy(62, 32));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, runtime.stepFrozenCaptureCanonicalCopy(63, 32));
  TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
  runtime.canonicalCaptureOutput.store.clear();
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutput.store.append(MidiEvent::ControlChange(999, 1, 7, 2)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, runtime.stepFrozenCaptureCanonicalCopy(62, 32));
  TEST_ASSERT_EQUAL(1, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(999, runtime.canonicalCaptureOutput.store.at(0).tick);
}

void test_canonical_copy_of_filtered_pair_consumes_no_chunks() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prewarmCanonicalCaptureOutput());
  auto& staging = *runtime.captureStagingIfPrewarmed();
  auto on = MidiEvent::NoteOn(100, 1, 60, 100); on.noteId = 93;
  auto off = MidiEvent::NoteOff(101, 1, 60); off.noteId = 93;
  TEST_ASSERT_TRUE(staging.appendContinuationNoteOn(on));
  TEST_ASSERT_TRUE(staging.appendReservedNoteOff(off, false));
  staging.captureGeneration = 63;
  TEST_ASSERT_TRUE(staging.freeze(63));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(63));
  while (!runtime.frozenCaptureCanonicalOrderComplete())
    (void)runtime.stepFrozenCaptureCanonicalOrder(1);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, runtime.stepFrozenCaptureCanonicalCopy(63, 1));
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutput.store.empty());
  TEST_ASSERT_EQUAL(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS, runtime.canonicalCaptureChunks.availableChunks());
}

void test_frozen_capture_order_uses_only_fixed_staging_storage() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  CaptureAdmissionStaging& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(20, 1, 60, 100)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOff(20, 1, 60)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(10, 1, 1, 3)));
  staging.captureGeneration = 19;
  TEST_ASSERT_TRUE(staging.freeze(19));

  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(768, 18));
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 19));
  TEST_ASSERT_FALSE(runtime.frozenCaptureCandidateReady(19));
  while (!runtime.frozenCaptureOrderComplete()) {
    const RuntimePlaybackOrderBuildResult result = runtime.stepFrozenCaptureOrder(1);
    TEST_ASSERT_TRUE(result == RuntimePlaybackOrderBuildResult::Pending ||
                     result == RuntimePlaybackOrderBuildResult::Complete);
  }
  TEST_ASSERT_EQUAL_UINT32(2, staging.storage.order[0]);
  TEST_ASSERT_EQUAL_UINT32(1, staging.storage.order[1]);
  TEST_ASSERT_EQUAL_UINT32(0, staging.storage.order[2]);

  const uint16_t firstOrder = staging.storage.order[0];
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 19));
  TEST_ASSERT_TRUE(runtime.frozenCaptureOrderComplete());
  TEST_ASSERT_EQUAL_UINT32(firstOrder, staging.storage.order[0]);
  TEST_ASSERT_TRUE(runtime.frozenCaptureCandidateReady(19));
  TEST_ASSERT_FALSE(runtime.frozenCaptureCandidateReady(20));
}

void test_frozen_capture_order_waits_for_every_reserved_terminal_closure() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  CaptureAdmissionStaging& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  CaptureNoteReservation* reservation = staging.reserve(33, 1, 60, 2, 2, 1);
  TEST_ASSERT_NOT_NULL(reservation);
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(4, 1, 60, 100)));
  staging.captureGeneration = 9;
  TEST_ASSERT_TRUE(staging.freeze(9));

  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(768, 9));
  staging.close(*reservation);
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 9));
}

void test_frozen_capture_canonical_order_rewrites_store_in_fixed_grains() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  LoopEventStore store;
  const MidiEvent late = MidiEvent::NoteOn(30, 1, 60, 100);
  const MidiEvent earlyFirst = MidiEvent::ControlChange(10, 1, 7, 90);
  const MidiEvent earlySecond = MidiEvent::NoteOff(10, 1, 60);
  TEST_ASSERT_TRUE(store.append(late));
  TEST_ASSERT_TRUE(store.append(earlyFirst));
  TEST_ASSERT_TRUE(store.append(earlySecond));

  FixedPlaybackEventStorage frozen;
  TEST_ASSERT_TRUE(frozen.prewarm(3));
  TEST_ASSERT_TRUE(frozen.append(late));
  TEST_ASSERT_TRUE(frozen.append(earlyFirst));
  TEST_ASSERT_TRUE(frozen.append(earlySecond));

  RuntimePlaybackOrderBuilder order;
  TEST_ASSERT_TRUE(order.beginCanonicalCapture(frozen));
  while (!order.complete()) {
    const RuntimePlaybackOrderBuildResult result = order.step(1);
    TEST_ASSERT_TRUE(result == RuntimePlaybackOrderBuildResult::Pending ||
                     result == RuntimePlaybackOrderBuildResult::Complete);
  }
  // Equal raw ticks retain frozen append order, matching stable_sort in the
  // legacy capture oracle rather than the audible Off-before-On ordering.
  TEST_ASSERT_EQUAL_UINT32(1, frozen.order[0]);
  TEST_ASSERT_EQUAL_UINT32(2, frozen.order[1]);
  TEST_ASSERT_EQUAL_UINT32(0, frozen.order[2]);

  LoopEventStoreRewriteCursor rewrite;
  TEST_ASSERT_TRUE(store.beginRewriteFromOrderedSource(rewrite, frozen.eventCount));
  TEST_ASSERT_TRUE(rewrite.active);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    store.stepRewriteFromOrderedSource(rewrite, frozen.events, frozen.order,
                                                       frozen.eventCount, frozen.eventCount, 1));
  TEST_ASSERT_EQUAL_UINT32(10, store.at(0).tick);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    store.stepRewriteFromOrderedSource(rewrite, frozen.events, frozen.order,
                                                       frozen.eventCount, frozen.eventCount, 1));
  LoopEventStoreRewriteResult rewriteResult = LoopEventStoreRewriteResult::Pending;
  for (size_t step = 0; step < 4 && rewriteResult == LoopEventStoreRewriteResult::Pending;
       ++step) {
    rewriteResult = store.stepRewriteFromOrderedSource(
        rewrite, frozen.events, frozen.order, frozen.eventCount, frozen.eventCount, 1);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, rewriteResult);
  TEST_ASSERT_FALSE(rewrite.active);
  TEST_ASSERT_EQUAL_UINT32(10, store.at(0).tick);
  TEST_ASSERT_EQUAL(midi::ControlChange, store.at(0).type);
  TEST_ASSERT_EQUAL_UINT32(10, store.at(1).tick);
  TEST_ASSERT_EQUAL(midi::NoteOff, store.at(1).type);
  TEST_ASSERT_EQUAL_UINT32(30, store.at(2).tick);
  TEST_ASSERT_EQUAL(midi::NoteOn, store.at(2).type);
}

void test_runtime_keeps_canonical_and_audible_frozen_orders_serial() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  CaptureAdmissionStaging& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(30, 1, 60, 100)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOff(10, 1, 60)));
  staging.captureGeneration = 41;
  TEST_ASSERT_TRUE(staging.freeze(41));

  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(41));
  while (!runtime.frozenCaptureCanonicalOrderComplete()) {
    const RuntimePlaybackOrderBuildResult result = runtime.stepFrozenCaptureCanonicalOrder(1);
    TEST_ASSERT_TRUE(result == RuntimePlaybackOrderBuildResult::Pending ||
                     result == RuntimePlaybackOrderBuildResult::Complete);
  }
  TEST_ASSERT_EQUAL_UINT32(1, staging.storage.order[0]);
  TEST_ASSERT_EQUAL_UINT32(0, staging.storage.order[1]);

  // The audible candidate deliberately replaces the same fixed order with
  // phase semantics only after canonical preparation has completed.
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 41));
  while (!runtime.frozenCaptureOrderComplete()) {
    const RuntimePlaybackOrderBuildResult result = runtime.stepFrozenCaptureOrder(1);
    TEST_ASSERT_TRUE(result == RuntimePlaybackOrderBuildResult::Pending ||
                     result == RuntimePlaybackOrderBuildResult::Complete);
  }
  TEST_ASSERT_TRUE(runtime.frozenCaptureCandidateReady(41));
}

void test_runtime_rewrites_frozen_canonical_capture_in_one_declared_quantum() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore capture;
  const MidiEvent late = MidiEvent::NoteOn(30, 1, 60, 100);
  const MidiEvent early = MidiEvent::NoteOff(10, 1, 60);
  TEST_ASSERT_TRUE(capture.append(late));
  TEST_ASSERT_TRUE(capture.append(early));

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  CaptureAdmissionStaging& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(late));
  TEST_ASSERT_TRUE(staging.storage.append(early));
  staging.captureGeneration = 52;
  TEST_ASSERT_TRUE(staging.freeze(52));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(52));
  while (!runtime.frozenCaptureCanonicalOrderComplete()) {
    (void)runtime.stepFrozenCaptureCanonicalOrder(1);
  }

  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    runtime.stepFrozenCaptureCanonicalRewrite(capture, 52, 1));
  LoopEventStore foreign;
  TEST_ASSERT_TRUE(foreign.append(MidiEvent::ControlChange(77, 1, 7, 80)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    runtime.stepFrozenCaptureCanonicalRewrite(foreign, 52, 1));
  TEST_ASSERT_EQUAL_UINT32(77, foreign.at(0).tick);
  LoopEventStoreRewriteResult rewriteResult = LoopEventStoreRewriteResult::Pending;
  for (size_t step = 0; step < 4 && rewriteResult == LoopEventStoreRewriteResult::Pending;
       ++step) {
    rewriteResult = runtime.stepFrozenCaptureCanonicalRewrite(capture, 52, 1);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, rewriteResult);
  TEST_ASSERT_EQUAL_UINT32(10, capture.at(0).tick);
  TEST_ASSERT_EQUAL_UINT32(30, capture.at(1).tick);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    runtime.stepFrozenCaptureCanonicalRewrite(capture, 52, 1));
}

void test_prepared_capture_canonicalization_prevents_legacy_sort_at_seal() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(30, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(10, 1, 60)));
  TEST_ASSERT_TRUE(loop.captureEventsSortDirty);

  loop.noteCaptureEventsCanonicalized(30);
  TEST_ASSERT_FALSE(loop.captureEventsSortDirty);
  TEST_ASSERT_FALSE(loop.ensureCaptureEventsSorted());
}

void test_ping_pong_order_preserves_permutation_and_private_source() {
  for (size_t count : {0u, 1u, 2u, 3u, 4u, 7u, 8u, 9u, 31u, 32u, 33u, 2048u}) {
    for (size_t quantum : {1u, 7u, 48u}) {
      for (unsigned mode = 0; mode < 4; ++mode) {
        RuntimePlaybackView view;
        TEST_ASSERT_TRUE(view.prewarm(2048));
        std::vector<uint16_t> indexes(2048, UINT16_MAX), scratch(2048, UINT16_MAX), expected;
        std::vector<MidiEvent> input;
        for (size_t i = 0; i < count; ++i) {
          const uint32_t tick = mode == 2 ? static_cast<uint32_t>(i * 2654435761u) % 1024
                                          : static_cast<uint32_t>((count - i) / 2);
          const auto event = i % 3 == 0 ? MidiEvent::NoteOff(tick, 1, 60)
                                      : MidiEvent::NoteOn(tick, 1, 60, 90);
          TEST_ASSERT_TRUE(view.storage.append(event));
          view.storage.phases[i] = tick;
          view.storage.order[i] = view.storage.mergeScratch[i] = UINT16_MAX;
          input.push_back(event);
          if (mode == 3 && i % 5 == 0) view.storage.discardEvent(i);
          else expected.push_back(static_cast<uint16_t>(i));
        }
        std::stable_sort(expected.begin(), expected.end(), [&](uint16_t a, uint16_t b) {
          if (input[a].tick != input[b].tick) return input[a].tick < input[b].tick;
          if (mode != 1 && input[a].isNoteOff() != input[b].isNoteOff()) return input[a].isNoteOff();
          return a < b;
        });
        RuntimePlaybackOrderBuilder order;
        if (mode == 1) TEST_ASSERT_TRUE(order.beginCanonicalCapture(view.storage));
        else if (mode == 3) TEST_ASSERT_TRUE(order.beginIndexes(view.storage, indexes.data(), scratch.data(), 4096, nullptr));
        else TEST_ASSERT_TRUE(order.begin(view, 4096, mode == 2 ? RuntimePlaybackOrderMode::RawPlaybackTick
                                                               : RuntimePlaybackOrderMode::PlaybackPhase));
        auto* const output = mode == 3 ? indexes.data() : view.storage.order;
        auto* const backing = view.storage.events;
        const size_t limit = RuntimePlaybackOrderBuilder::requiredGrains(count, quantum);
        size_t turns = 0;
        while (!order.complete() && turns <= limit) {
          const auto result = order.step(quantum);
          TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
          if ((mode == 0 || mode == 2) && result == RuntimePlaybackOrderBuildResult::Pending)
            TEST_ASSERT_FALSE(view.complete);
          ++turns;
        }
        TEST_ASSERT_TRUE(order.complete());
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(limit, turns);
        TEST_ASSERT_EQUAL_UINT32(expected.size(), order.orderedEventCount());
        for (size_t i = 0; i < expected.size(); ++i) TEST_ASSERT_EQUAL_UINT16(expected[i], output[i]);
        TEST_ASSERT_EQUAL_PTR(backing, view.storage.events);
        for (size_t i = 0; i < count; ++i) {
          TEST_ASSERT_EQUAL_UINT32(input[i].tick, view.storage.events[i].tick);
          TEST_ASSERT_EQUAL(input[i].type, view.storage.events[i].type);
          if (mode == 3) {
            TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, view.storage.order[i]);
            TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, view.storage.mergeScratch[i]);
            TEST_ASSERT_EQUAL_UINT32(input[i].tick, view.storage.phases[i]);
          }
        }
      }
    }
  }
}

void test_ping_pong_order_measures_dense_work_and_resets_direction() {
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(2048));
  RuntimePlaybackOrderBuilder order;
  for (size_t count : {2u, 4u, 8u, 16u, 1024u, 2048u}) {
    view.storage.eventCount = 0;
    for (size_t i = 0; i < count; ++i)
      TEST_ASSERT_TRUE(view.storage.append(MidiEvent::NoteOn(count - i, 1, 60, 90)));
    TEST_ASSERT_TRUE(order.beginCanonicalCapture(view.storage));
    // Interrupt after the first level, when the input direction is scratch.
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, order.step(count * 2));
    order.discard();
    TEST_ASSERT_TRUE(order.beginCanonicalCapture(view.storage));
    size_t units = 0;
    while (!order.complete() && units < 50000) {
      TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, order.step(1));
      ++units;
    }
    TEST_ASSERT_TRUE(order.complete());
    size_t levels = 0;
    for (size_t n = count; n > 1; n /= 2) ++levels;
    const size_t expected = count * (1 + levels + levels % 2);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, units, "one write per merge level, at most one final normalization");
    TEST_ASSERT_EQUAL_UINT32(expected, RuntimePlaybackOrderBuilder::workEntriesUpperBound(count));
    for (size_t i = 0; i < count; ++i) TEST_ASSERT_EQUAL_UINT16(count - i - 1, view.storage.order[i]);
    if (count == 2048) {
      TEST_ASSERT_EQUAL_UINT32(26624, units);
      TEST_ASSERT_EQUAL_UINT32(555, RuntimePlaybackOrderBuilder::requiredGrains(count, 48));
    }
  }
}

void test_ordered_inputs_finish_in_fill_grains_without_touching_merge_scratch() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    for (size_t quantum : {size_t(1), size_t(32)}) {
      RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(2048));
      uint16_t indexes[2048], scratch[2048];
      for (size_t i = 0; i < 2048; ++i) {
        const auto tick = static_cast<uint32_t>(mode == 1 ? i / 2 : 2 * i);
        TEST_ASSERT_TRUE(view.storage.append(i % 2 ? MidiEvent::NoteOff(tick, 1, 60)
                                                   : MidiEvent::NoteOn(tick, 1, 60, 90)));
        view.storage.phases[i] = tick;
        view.storage.mergeScratch[i] = scratch[i] = UINT16_MAX;
      }
      RuntimePlaybackOrderBuilder builder;
      if (mode == 0) TEST_ASSERT_TRUE(builder.begin(view, 8192));
      else if (mode == 1) TEST_ASSERT_TRUE(builder.beginCanonicalCapture(view.storage));
      else TEST_ASSERT_TRUE(builder.beginIndexes(view.storage, indexes, scratch, 8192, nullptr));
      const size_t grains = 1 + (2048 - 1) / quantum;
      for (size_t turn = 0; turn < grains; ++turn) {
        TEST_ASSERT_EQUAL(turn + 1 == grains ? RuntimePlaybackOrderBuildResult::Complete
                                            : RuntimePlaybackOrderBuildResult::Pending, builder.step(quantum));
      }
      TEST_ASSERT_EQUAL_UINT32(2048, builder.orderedEventCount());
      if (mode == 0) TEST_ASSERT_TRUE(view.complete);
      for (size_t i = 0; i < 2048; ++i) {
        TEST_ASSERT_EQUAL_UINT16(i, mode == 2 ? indexes[i] : view.storage.order[i]);
        TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, view.storage.mergeScratch[i]);
        TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, scratch[i]);
      }
    }
  }
}

void test_order_fill_checks_retained_adjacency_and_resets_sort_detection() {
  FixedPlaybackEventStorage storage; TEST_ASSERT_TRUE(storage.prewarm(4));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(10, 1, 60, 90)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(100, 1, 62, 90)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOff(20, 1, 60)));
  storage.discardEvent(1);
  RuntimePlaybackOrderBuilder builder;
  TEST_ASSERT_TRUE(builder.begin(storage, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, builder.step(2));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(1));
  TEST_ASSERT_EQUAL_UINT32(2, builder.orderedEventCount());
  TEST_ASSERT_EQUAL_UINT16(2, storage.order[1]);
  // The retained sequence is now inverted. Reusing a completed builder must
  // fall back to merge and must not carry its previous ordered result.
  storage.events[2].tick = 5;
  TEST_ASSERT_TRUE(builder.begin(storage, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, builder.step(3));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = builder.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT16(2, storage.order[0]);
  storage.events[2].tick = 20;
  TEST_ASSERT_TRUE(builder.begin(storage, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(3));
}

void test_order_fill_detects_rank_inversions_at_equal_tick() {
  FixedPlaybackEventStorage storage; TEST_ASSERT_TRUE(storage.prewarm(4));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(10, 1, 60, 90)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(10, 1, 62, 90)));
  uint32_t ranks[2] = {2, 1};
  RuntimePlaybackOrderBuilder builder; TEST_ASSERT_TRUE(builder.begin(storage, 768, ranks));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, builder.step(2));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = builder.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT16(1, storage.order[0]);
}

void test_private_delta_order_freezes_append_waves_without_prefix_replay() {
  FixedPlaybackEventStorage storage; TEST_ASSERT_TRUE(storage.prewarm(8));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(100, 1, 60, 90)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(200, 1, 62, 90)));
  RuntimePlaybackOrderBuilder builder; TEST_ASSERT_TRUE(builder.begin(storage, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(2));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOff(10, 1, 60)));
  TEST_ASSERT_TRUE(builder.extendPrivateOrder());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, builder.step(1));
  TEST_ASSERT_EQUAL_UINT16(0, storage.order[0]);
  TEST_ASSERT_EQUAL_UINT16(1, storage.order[1]);
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(90, 1, 64, 90)));
  TEST_ASSERT_FALSE(builder.extendPrivateOrder()); // Cannot replace an in-flight frozen suffix.
  size_t turns = 1;
  while (!builder.complete() && turns < 30) { TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1)); ++turns; }
  TEST_ASSERT_TRUE(builder.complete());
  TEST_ASSERT_EQUAL_UINT32(7, turns); // One suffix fill + three merge writes + three copy-back writes.
  TEST_ASSERT_EQUAL_UINT32(3, builder.orderedSourceExtent());
  TEST_ASSERT_EQUAL_UINT32(3, builder.orderedEventCount());
  const uint16_t first[] = {2, 0, 1}; TEST_ASSERT_EQUAL_UINT16_ARRAY(first, storage.order, 3);
  TEST_ASSERT_TRUE(builder.extendPrivateOrder());
  turns = 0;
  while (!builder.complete() && turns < 30) { TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1)); ++turns; }
  TEST_ASSERT_TRUE(builder.complete()); TEST_ASSERT_EQUAL_UINT32(9, turns);
  const uint16_t second[] = {2, 3, 0, 1}; TEST_ASSERT_EQUAL_UINT16_ARRAY(second, storage.order, 4);
  TEST_ASSERT_EQUAL_UINT32(4, builder.orderedSourceExtent());
  TEST_ASSERT_TRUE(builder.extendPrivateOrder()); TEST_ASSERT_TRUE(builder.complete());
  TEST_ASSERT_EQUAL_UINT16_ARRAY(second, storage.order, 4);
}

// Task 3.15e slice 3 — **the readable prefix is frozen**: an extension orders the appended tail only and concatenates it,
// so a row that the full prefix merge would have moved in front of a readable row stays in the tail instead. This is the
// order-builder half of the W1 race; the contract is "the already-readable prefix never moves", not "the tail is
// canonically placed relative to the prefix".
void test_3_15e_slice3_prefix_preserving_extension_never_moves_a_published_prefix() {
  FixedPlaybackEventStorage storage;
  TEST_ASSERT_TRUE(storage.prewarm(8));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(100, 1, 60, 90)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(200, 1, 62, 90)));
  RuntimePlaybackOrderBuilder builder;
  TEST_ASSERT_TRUE(builder.begin(storage, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(2));
  const uint16_t readable[] = {0, 1};
  TEST_ASSERT_EQUAL_UINT16_ARRAY(readable, storage.order, 2);

  // An equal-tick Off at the prefix's own tick: the full merge would put it before the prefix On (class rule), but the
  // prefix is already readable, so it may only enter the tail.
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOff(100, 1, 60)));
  TEST_ASSERT_TRUE(builder.extendPrivateOrderPreservingPrefix());
  size_t turns = 0;
  while (!builder.complete() && turns < 30) {
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1));
    ++turns;
  }
  TEST_ASSERT_TRUE(builder.complete());
  TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE(readable, storage.order, 2,
                                         "an extension must never move an already-readable row");
  TEST_ASSERT_EQUAL_UINT16(2, storage.order[2]);
  TEST_ASSERT_EQUAL_UINT32(3, builder.orderedEventCount());

  // A second wave that would sort at the very front globally: still tail-only.
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(50, 1, 64, 90)));
  TEST_ASSERT_TRUE(builder.extendPrivateOrderPreservingPrefix());
  turns = 0;
  while (!builder.complete() && turns < 30) {
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1));
    ++turns;
  }
  TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE(readable, storage.order, 2,
                                         "every published row keeps its position across waves");
  // The readable prefix is whatever the frontier covered at the previous extension; a later wave is appended after it,
  // even when its tick is earlier. That is exactly what "the prefix never moves" costs, and it is deliberate.
  TEST_ASSERT_EQUAL_UINT16(2, storage.order[2]);
  TEST_ASSERT_EQUAL_UINT16(3, storage.order[3]);
  TEST_ASSERT_EQUAL_UINT32(4, builder.orderedEventCount());
  TEST_ASSERT_EQUAL_UINT32(4, builder.orderedSourceExtent());

  // Within one wave the tail is canonically ordered among itself: equal-tick Off before On.
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(150, 1, 65, 90)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOff(150, 1, 65)));
  TEST_ASSERT_TRUE(builder.extendPrivateOrderPreservingPrefix());
  turns = 0;
  while (!builder.complete() && turns < 40) {
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1));
    ++turns;
  }
  TEST_ASSERT_TRUE(builder.complete());
  TEST_ASSERT_EQUAL_UINT16_ARRAY(readable, storage.order, 2);
  const uint16_t appeared[] = {2, 3, 5, 4}; // previous waves, then this wave's Off before its On
  TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE(appeared, storage.order + 2, 4,
                                         "each wave is ordered among itself (Off before On at one tick)");
  TEST_ASSERT_EQUAL_UINT32(6, builder.orderedEventCount());
}

void test_private_delta_order_wrap_off_on_and_index_write_quantum() {
  FixedPlaybackEventStorage storage; TEST_ASSERT_TRUE(storage.prewarm(8));
  for (size_t i = 0; i < storage.capacity; ++i) storage.order[i] = storage.mergeScratch[i] = UINT16_MAX;
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(0, 1, 60, 90)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(20, 1, 62, 90)));
  RuntimePlaybackOrderBuilder builder; TEST_ASSERT_TRUE(builder.begin(storage, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(2));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOff(20, 1, 62)));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOff(768, 1, 60)));
  TEST_ASSERT_TRUE(builder.extendPrivateOrder());
  size_t turns = 0;
  while (!builder.complete() && turns < 60) {
    uint16_t oldOrder[8], oldScratch[8];
    memcpy(oldOrder, storage.order, sizeof(oldOrder)); memcpy(oldScratch, storage.mergeScratch, sizeof(oldScratch));
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1));
    size_t changed = 0;
    for (size_t i = 0; i < 8; ++i) changed += (oldOrder[i] != storage.order[i]) + (oldScratch[i] != storage.mergeScratch[i]);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1, changed);
    ++turns;
  }
  TEST_ASSERT_TRUE(builder.complete()); TEST_ASSERT_EQUAL_UINT32(14, turns);
  const uint16_t expected[] = {3, 0, 2, 1}; // Wrap Off before On; same-tick Off before On.
  TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, storage.order, 4);
  TEST_ASSERT_EQUAL_UINT32(0, storage.phases[3]);
}

void test_private_delta_order_filters_and_refuses_non_private_or_stale_extent() {
  FixedPlaybackEventStorage storage; TEST_ASSERT_TRUE(storage.prewarm(8));
  RuntimePlaybackOrderBuilder builder;
  TEST_ASSERT_FALSE(builder.extendPrivateOrder());
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(100, 1, 60, 90)));
  TEST_ASSERT_TRUE(builder.begin(storage, 768)); TEST_ASSERT_FALSE(builder.extendPrivateOrder());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(1));
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOn(1, 1, 62, 90))); storage.discardEvent(1);
  TEST_ASSERT_TRUE(builder.extendPrivateOrder());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(1));
  TEST_ASSERT_EQUAL_UINT32(1, builder.orderedEventCount()); TEST_ASSERT_EQUAL_UINT32(2, builder.orderedSourceExtent());
  TEST_ASSERT_EQUAL_UINT16(0, storage.order[0]);
  TEST_ASSERT_TRUE(storage.append(MidiEvent::NoteOff(20, 1, 60)));
  TEST_ASSERT_TRUE(builder.extendPrivateOrder()); storage.eventCount = 2;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1));
  TEST_ASSERT_FALSE(builder.extendPrivateOrder());
  uint16_t indexes[8] = {}, scratch[8] = {};
  TEST_ASSERT_TRUE(builder.beginIndexes(storage, indexes, scratch, 768, nullptr));
  while (!builder.complete()) TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(8));
  TEST_ASSERT_FALSE(builder.extendPrivateOrder());
  LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& view = runtime.viewSetIfPrewarmed()->views[0];
  TEST_ASSERT_TRUE(view.storage.append(MidiEvent::NoteOn(0, 1, 60, 90)));
  TEST_ASSERT_TRUE(builder.begin(view, 768)); TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, builder.step(8));
  TEST_ASSERT_TRUE(view.complete); TEST_ASSERT_FALSE(builder.extendPrivateOrder()); TEST_ASSERT_TRUE(view.complete);
}

void test_private_delta_order_matrix_matches_independent_order_and_work_bound() {
  for (auto mode : {RuntimePlaybackOrderMode::PlaybackPhase, RuntimePlaybackOrderMode::RawPlaybackTick,
                    RuntimePlaybackOrderMode::CanonicalCaptureTick})
    for (size_t prefix : {size_t(0), size_t(1), size_t(2), size_t(7), size_t(16)})
      for (size_t delta : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(9), size_t(33)})
        for (size_t quantum : {size_t(1), size_t(3), size_t(48)}) {
          FixedPlaybackEventStorage storage; TEST_ASSERT_TRUE(storage.prewarm(64));
          uint32_t ranks[64]; for (size_t i = 0; i < 64; ++i) ranks[i] = 64 - i;
          auto appendRow = [&](size_t i) {
            const uint32_t tick = i % 4 == 0 ? 768 : (i % 4 == 1 ? 0 : (37 * i) % 769);
            TEST_ASSERT_TRUE(storage.append(i % 3 == 0 ? MidiEvent::NoteOff(tick, 1, 60 + i % 12)
                : MidiEvent::NoteOn(tick, 1, 60 + i % 12, 90)));
            if (i % 5 == 2) storage.discardEvent(i);
          };
          RuntimePlaybackOrderBuilder builder;
          auto assertIndependent = [&](size_t extent) {
            uint16_t expected[64]; size_t retained = 0;
            for (size_t i = 0; i < extent; ++i) if (storage.retained[i]) expected[retained++] = i;
            std::sort(expected, expected + retained, [&](uint16_t a, uint16_t b) {
              const auto& l = storage.events[a]; const auto& r = storage.events[b];
              const uint32_t lp = mode == RuntimePlaybackOrderMode::PlaybackPhase ? l.tick % 768 : l.tick;
              const uint32_t rp = mode == RuntimePlaybackOrderMode::PlaybackPhase ? r.tick % 768 : r.tick;
              if (lp != rp) return lp < rp;
              if (mode != RuntimePlaybackOrderMode::CanonicalCaptureTick) {
                const unsigned lk = l.isNoteOff() ? 0 : 1, rk = r.isNoteOff() ? 0 : 1;
                if (lk != rk) return lk < rk;
                if (l.tick != r.tick) return l.tick < r.tick;
                if (ranks[a] != ranks[b]) return ranks[a] < ranks[b];
              }
              return a < b;
            });
            TEST_ASSERT_EQUAL_UINT32(retained, builder.orderedEventCount());
            if (retained != 0) TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, storage.order, retained);
          };
          for (size_t i = 0; i < prefix; ++i) appendRow(i);
          TEST_ASSERT_TRUE(builder.begin(storage, 768, ranks, mode));
          size_t guard = 0;
          while (!builder.complete() && guard++ < 200) TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(48));
          TEST_ASSERT_TRUE(builder.complete());
          const size_t prefixEvents = builder.orderedEventCount();
          for (size_t i = prefix; i < prefix + delta; ++i) appendRow(i);
          TEST_ASSERT_TRUE(builder.extendPrivateOrder());
          const size_t work = RuntimePlaybackOrderBuilder::extensionWorkEntriesUpperBound(prefixEvents, delta);
          const size_t limit = work == 0 ? 0 : 1 + (work - 1) / quantum;
          size_t turns = 0; bool laterWave = false;
          while (!builder.complete() && turns < limit) {
            TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(quantum)); ++turns;
            if (turns == 1 + (prefix + delta) % 7 && !builder.complete()) {
              for (size_t i = prefix + delta; i < prefix + delta + 3; ++i) appendRow(i);
              laterWave = true;
            }
          }
          TEST_ASSERT_TRUE(builder.complete()); TEST_ASSERT_EQUAL_UINT32(prefix + delta, builder.orderedSourceExtent());
          assertIndependent(prefix + delta);
          if (laterWave) {
            TEST_ASSERT_TRUE(builder.extendPrivateOrder()); guard = 0;
            while (!builder.complete() && guard++ < 300) TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, builder.step(1));
            TEST_ASSERT_TRUE(builder.complete()); assertIndependent(prefix + delta + 3);
          }
        }
  const size_t max = std::numeric_limits<size_t>::max();
  TEST_ASSERT_EQUAL_UINT64(max, RuntimePlaybackOrderBuilder::extensionWorkEntriesUpperBound(max, 1));
  TEST_ASSERT_EQUAL_UINT64(max, RuntimePlaybackOrderBuilder::extensionWorkEntriesUpperBound(1, max));
  TEST_ASSERT_EQUAL_UINT32(0, RuntimePlaybackOrderBuilder::extensionWorkEntriesUpperBound(max, 0));
}

void test_runtime_order_builder_yields_and_preserves_same_tick_off_before_on() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackView& view = runtime.viewSetIfPrewarmed()->views[0];
  TEST_ASSERT_TRUE(view.storage.append(MidiEvent::NoteOn(20, 1, 60, 100)));
  TEST_ASSERT_TRUE(view.storage.append(MidiEvent::NoteOff(20, 1, 60)));
  TEST_ASSERT_TRUE(view.storage.append(MidiEvent::ControlChange(10, 1, 1, 1)));
  view.sourceFilled = true;

  RuntimePlaybackOrderBuilder builder;
  TEST_ASSERT_TRUE(builder.begin(view, 768));
  size_t turns = 0;
  while (!builder.complete()) {
    const RuntimePlaybackOrderBuildResult result = builder.step(1);
    TEST_ASSERT_TRUE(result == RuntimePlaybackOrderBuildResult::Pending ||
                     result == RuntimePlaybackOrderBuildResult::Complete);
    ++turns;
    TEST_ASSERT_LESS_THAN_UINT32(100, turns);
  }
  TEST_ASSERT_GREATER_THAN_UINT32(1, turns);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.order[0]);
  TEST_ASSERT_EQUAL_UINT32(1, view.storage.order[1]);
  TEST_ASSERT_EQUAL_UINT32(0, view.storage.order[2]);
}

void test_runtime_order_builder_preserves_equal_phase_off_before_on_across_wrap() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackView& view = runtime.viewSetIfPrewarmed()->views[0];
  TEST_ASSERT_TRUE(view.storage.append(MidiEvent::NoteOn(0, 1, 60, 100)));
  TEST_ASSERT_TRUE(view.storage.append(MidiEvent::NoteOff(768, 1, 60)));
  view.sourceFilled = true;

  RuntimePlaybackOrderBuilder builder;
  TEST_ASSERT_TRUE(builder.begin(view, 768));
  while (!builder.complete()) {
    (void)builder.step(1);
  }
  TEST_ASSERT_EQUAL_UINT32(1, view.storage.order[0]);
  TEST_ASSERT_EQUAL_UINT32(0, view.storage.order[1]);
}

void test_runtime_view_build_retains_empty_non_note_and_repeated_note_id_instances() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackView& empty = runtime.viewSetIfPrewarmed()->views[0];
  const PlaybackCoverageInterval coverage{0, 768, 768};
  RuntimePlaybackViewBuild emptyBuild;
  TEST_ASSERT_TRUE(emptyBuild.begin(empty, nullptr, 0, PlaybackViewSourceIdentity{1, 1, nullptr},
                                    coverage, EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, emptyBuild.stepFill(1));
  TEST_ASSERT_TRUE(empty.complete);

  MidiEvent source[] = {MidiEvent::ControlChange(10, 1, 7, 100),
                        MidiEvent::NoteOn(20, 1, 60, 100),
                        MidiEvent::NoteOff(20, 1, 60),
                        MidiEvent::ControlChange(20, 1, 7, 101),
                        MidiEvent::NoteOn(788, 1, 61, 100)};
  source[1].noteId = 42;
  source[2].noteId = 42;
  source[4].noteId = 42;
  RuntimePlaybackView& view = runtime.viewSetIfPrewarmed()->views[1];
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, source, 5, PlaybackViewSourceIdentity{2, 2, nullptr},
                               coverage, EmissionCursor{}, LedgerBoundarySummary{}));
  while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill) {
    (void)build.stepFill(2);
  }
  while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order) {
    (void)build.stepOrder(2);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_EQUAL_UINT32(5, view.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, view.storage.order[0]);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.order[1]);
  TEST_ASSERT_EQUAL_UINT32(1, view.storage.order[2]);
  TEST_ASSERT_EQUAL_UINT32(4, view.storage.order[3]);
  TEST_ASSERT_EQUAL_UINT32(3, view.storage.order[4]);
}

void test_runtime_view_fill_yields_and_denies_an_unreserved_source() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackView& view = runtime.viewSetIfPrewarmed()->views[0];
  const MidiEvent source[] = {
      MidiEvent::ControlChange(1, 1, 1, 1), MidiEvent::NoteOff(2, 1, 60),
      MidiEvent::NoteOn(2, 1, 60, 100)};
  const PlaybackCoverageInterval coverage{0, 768, 768};
  RuntimePlaybackViewFill fill;
  TEST_ASSERT_TRUE(fill.begin(view, source, 3, PlaybackViewSourceIdentity{1, 2, nullptr}, coverage,
                              EmissionCursor{3, 4, 5, 6, 7}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Pending, fill.step(2));
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Complete, fill.step(2));
  TEST_ASSERT_TRUE(view.sourceFilled);
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_EQUAL_UINT32(3, view.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(2, view.source.sourceGeneration);

  RuntimePlaybackOrderBuilder builder;
  TEST_ASSERT_TRUE(builder.begin(view, 768));
  while (!builder.complete()) {
    (void)builder.step(4);
  }
  TEST_ASSERT_TRUE(view.complete);

  TEST_ASSERT_FALSE(fill.begin(view, source, view.storage.capacity + 1, PlaybackViewSourceIdentity{},
                               coverage, EmissionCursor{}, LedgerBoundarySummary{}));
}

void test_view_build_keeps_fill_and_order_as_separate_resumable_grains() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackView& view = runtime.viewSetIfPrewarmed()->views[2];
  const MidiEvent source[] = {MidiEvent::NoteOn(20, 1, 60, 100),
                              MidiEvent::NoteOff(20, 1, 60)};
  RuntimePlaybackViewBuild build;
  const PlaybackCoverageInterval coverage{0, 768, 768};
  TEST_ASSERT_TRUE(build.begin(view, source, 2, PlaybackViewSourceIdentity{1, 1, nullptr},
                               coverage, EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, build.nextGrain());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, build.stepFill(1));
  TEST_ASSERT_EQUAL_UINT32(1, view.storage.eventCount);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.stepFill(1));
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  TEST_ASSERT_FALSE(view.complete);
  while (build.active()) {
    const RuntimePlaybackViewBuildGrain result = build.stepOrder(1);
    TEST_ASSERT_TRUE(result == RuntimePlaybackViewBuildGrain::Order ||
                     result == RuntimePlaybackViewBuildGrain::Complete);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_EQUAL_UINT32(1, view.storage.order[0]);
  TEST_ASSERT_EQUAL_UINT32(0, view.storage.order[1]);
}

void test_coverage_contract_accounts_for_denied_turns_and_phase_wrap() {
  const CoverageServiceContract contract{8, 2, 2, 2, 1, 12};
  TEST_ASSERT_TRUE(contract.isDeclared());
  TEST_ASSERT_EQUAL_UINT32(5, contract.requiredGrains());
  // Five grains with two denied turns before each need 132 phase ticks.
  TEST_ASSERT_TRUE(contract.canCompleteBeforeHandoff(700, 100, 768));
  TEST_ASSERT_FALSE(contract.canCompleteBeforeHandoff(700, 60, 768));
  // A source-change refill that begins at the latest permitted completion has
  // an additional bridge/promotion grain and is denied when its lead is short.
  TEST_ASSERT_FALSE(contract.canCompleteBeforeHandoff(100, 240, 768, 1));
}

void test_runtime_retains_only_a_coverage_proven_pending_build() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  const MidiEvent source[] = {MidiEvent::NoteOff(20, 1, 60), MidiEvent::NoteOn(20, 1, 60, 100)};
  const PlaybackCoverageInterval coverage{0, 768, 768};
  const CoverageServiceContract denied{64, 2, 2, 2, 1, 64};
  TEST_ASSERT_FALSE(runtime.beginCoverageBuild(denied, 700, 100, source, 2,
                                               PlaybackViewSourceIdentity{1, 1, nullptr}, coverage,
                                               EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, runtime.nextCoverageBuildGrain());

  const CoverageServiceContract admitted{8, 0, 1, 1, 1, 8};
  TEST_ASSERT_TRUE(runtime.beginCoverageBuild(admitted, 700, 100, source, 2,
                                              PlaybackViewSourceIdentity{1, 1, nullptr}, coverage,
                                              EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_FALSE(runtime.beginCoverageBuild(admitted, 700, 100, source, 2,
                                               PlaybackViewSourceIdentity{2, 2, nullptr}, coverage,
                                               EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, runtime.nextCoverageBuildGrain());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, runtime.stepCoverageBuildFill(2));
  while (runtime.nextCoverageBuildGrain() == RuntimePlaybackViewBuildGrain::Order) {
    (void)runtime.stepCoverageBuildOrder(1);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, runtime.nextCoverageBuildGrain());
}

void test_three_view_rotation_keeps_retired_current_idle_until_build_completes() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  set.views[0].complete = true;
  set.views[0].published = true;
  set.views[1].complete = true;
  set.views[2].complete = false;

  TEST_ASSERT_TRUE(set.handoffReadyCoverage());
  TEST_ASSERT_EQUAL_UINT8(1, set.lifecycle.current);
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(2, set.lifecycle.build);
  TEST_ASSERT_EQUAL_UINT8(0, set.lifecycle.idle);
  TEST_ASSERT_FALSE(set.publishCompletedBuildAsReady());

  set.views[2].complete = true;
  TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady());
  TEST_ASSERT_EQUAL_UINT8(1, set.lifecycle.current);
  TEST_ASSERT_EQUAL_UINT8(2, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(0, set.lifecycle.build);
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set.lifecycle.idle);
}

void test_source_change_rotation_keeps_both_successive_pending_builds_distinct() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  RuntimePlaybackView& current = set.views[0];
  RuntimePlaybackView& ready = set.views[1];
  RuntimePlaybackView& sourceChange = set.views[2];
  current.source.sourceGeneration = 10;  // C
  ready.source.sourceGeneration = 10;    // N
  sourceChange.source.sourceGeneration = 11;  // X, not ready at C -> N
  current.complete = current.published = true;
  ready.complete = true;
  sourceChange.complete = false;

  TEST_ASSERT_TRUE(set.handoffReadyCoverage());  // C -> N; X remains pending.
  TEST_ASSERT_EQUAL_UINT8(1, set.lifecycle.current);
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(0, set.lifecycle.idle);
  TEST_ASSERT_EQUAL_UINT32(10, set.views[1].source.sourceGeneration);

  sourceChange.complete = true;  // X completes midway through N.
  TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady());
  TEST_ASSERT_EQUAL_UINT8(2, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(0, set.lifecycle.build);
  TEST_ASSERT_EQUAL_UINT32(11, set.views[2].source.sourceGeneration);

  // Retired C starts Y. It remains pending through N -> X.
  set.views[0].source.sourceGeneration = 11;
  set.views[0].complete = false;
  TEST_ASSERT_TRUE(set.handoffReadyCoverage());
  TEST_ASSERT_EQUAL_UINT8(2, set.lifecycle.current);
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(1, set.lifecycle.idle);
  TEST_ASSERT_EQUAL_UINT8(0, set.lifecycle.build);

  set.views[0].complete = true;  // Y completes midway through X.
  TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady());
  TEST_ASSERT_EQUAL_UINT8(0, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(1, set.lifecycle.build);
  TEST_ASSERT_EQUAL_UINT32(11, set.views[0].source.sourceGeneration);
}

void test_paused_build_cannot_mutate_published_current_or_ready_view() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  RuntimePlaybackView& current = set.views[set.lifecycle.current];
  RuntimePlaybackView& ready = set.views[set.lifecycle.ready];
  RuntimePlaybackView& build = set.views[set.lifecycle.build];
  TEST_ASSERT_TRUE(current.storage.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(ready.storage.append(MidiEvent::NoteOff(20, 1, 60)));
  current.complete = current.published = true;
  ready.complete = true;

  const MidiEvent source[] = {MidiEvent::ControlChange(30, 1, 1, 2),
                              MidiEvent::ControlChange(40, 1, 1, 3)};
  RuntimePlaybackViewFill fill;
  const PlaybackCoverageInterval coverage{0, 768, 768};
  TEST_ASSERT_TRUE(fill.begin(build, source, 2, PlaybackViewSourceIdentity{4, 5, nullptr},
                              coverage, EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Pending, fill.step(1));

  TEST_ASSERT_EQUAL_UINT32(1, current.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(10, current.storage.events[0].tick);
  TEST_ASSERT_TRUE(current.published);
  TEST_ASSERT_EQUAL_UINT32(1, ready.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(20, ready.storage.events[0].tick);
  TEST_ASSERT_EQUAL_UINT32(1, build.storage.eventCount);
}

void test_second_bridge_candidate_does_not_mutate_selected_bridge() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  const EmissionCursor firstCursor{1, 100, 0, 10, 1};
  TEST_ASSERT_TRUE(set.beginCandidateBridge());
  FixedPlaybackBridgeStorage& first = set.bridges[0];
  TEST_ASSERT_TRUE(first.append(PlaybackBridgeRecord{}));
  first.coverage = PlaybackCoverageInterval{0, 768, 768};
  first.source.sourceGeneration = 1;
  first.complete = true;
  TEST_ASSERT_TRUE(set.selectCandidateBridge(firstCursor));
  TEST_ASSERT_EQUAL_UINT8(0, set.bridgeLifecycle.selected);

  TEST_ASSERT_TRUE(set.beginCandidateBridge());
  FixedPlaybackBridgeStorage& second = set.bridges[1];
  second.coverage = PlaybackCoverageInterval{0, 768, 768};
  second.source.sourceGeneration = 2;
  second.complete = true;
  TEST_ASSERT_EQUAL_UINT32(1, first.recordCount);
  TEST_ASSERT_TRUE(set.selectCandidateBridge(EmissionCursor{1, 120, 0, 11, 2}));
  TEST_ASSERT_EQUAL_UINT8(1, set.bridgeLifecycle.selected);
  TEST_ASSERT_EQUAL_UINT32(1, first.recordCount);
}

void test_pending_stop_transport_stop_invalidates_playing_completion() {
  PendingStopAdmission stop;
  const StopAdmissionContract contract{2, 5, 1, 64};
  TEST_ASSERT_TRUE(stop.begin(17, 384, 42, 2, 2, contract));
  TEST_ASSERT_EQUAL(PendingStopPhase::ReservedClosure, stop.phase);
  TEST_ASSERT_FALSE(stop.finalizationComplete());
  TEST_ASSERT_FALSE(stop.canCommit(42, PendingStopDestination::Playing));
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  TEST_ASSERT_EQUAL(PendingStopPhase::ReservedClosure, stop.phase);
  TEST_ASSERT_EQUAL_UINT32(1, stop.remainingClosureGrains);
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalOrder, stop.phase);
  TEST_ASSERT_FALSE(stop.finalizationComplete());
  TEST_ASSERT_FALSE(stop.consumeClosureGrain());
  while (stop.remainingOrderGrains != 0) {
    TEST_ASSERT_TRUE(stop.consumeOrderGrain());
  }
  TEST_ASSERT_TRUE(stop.finalizationComplete());
  TEST_ASSERT_FALSE(stop.consumeOrderGrain());
  TEST_ASSERT_TRUE(stop.canCommit(42, PendingStopDestination::Playing));

  stop.requestTransportStop();
  TEST_ASSERT_FALSE(stop.canCommit(42, PendingStopDestination::Playing));
  TEST_ASSERT_TRUE(MidiServiceDrain::aroundIdleMaintenance(false, stop.pending));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowDeferredSave(false, true, stop.pending));
  TEST_ASSERT_EQUAL_UINT32(43, stop.transportCommandEpoch);
  TEST_ASSERT_TRUE(stop.canCommit(43, PendingStopDestination::Stopped));
  // Transport Stop remains asserted while the deferred owner runs. Repeated
  // delivery must retain the epoch accepted by its frozen canonical workers.
  stop.requestTransportStop();
  TEST_ASSERT_EQUAL_UINT32(43, stop.transportCommandEpoch);
  TEST_ASSERT_TRUE(stop.canCommit(43, PendingStopDestination::Stopped));

  stop.reset();
  TEST_ASSERT_FALSE(stop.pending);
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredSave(false, true, stop.pending));
}

void test_pending_stop_rejects_an_undeclared_or_over_capacity_tail() {
  PendingStopAdmission stop;
  const StopAdmissionContract contract{1, 1, 1, 64};
  TEST_ASSERT_FALSE(stop.begin(5, 100, 1,
                               PendingStopAdmission::kMaximumClosureGrains + 1, 1, contract));
  TEST_ASSERT_FALSE(stop.pending);
  TEST_ASSERT_TRUE(stop.begin(5, 100, 1, 0, 0, contract));
  TEST_ASSERT_TRUE(stop.finalizationComplete());
}

void test_pending_stop_orders_only_after_reserved_closures() {
  PendingStopAdmission stop;
  const StopAdmissionContract contract{2, 16, 1, 1};
  TEST_ASSERT_TRUE(stop.begin(7, 200, 9, 1, 2, contract));
  TEST_ASSERT_GREATER_THAN_UINT32(0, stop.remainingOrderGrains);
  TEST_ASSERT_FALSE(stop.consumeOrderGrain());
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  TEST_ASSERT_TRUE(stop.consumeOrderGrain());
}

void test_stop_admission_contract_counts_fixed_order_work() {
  const StopAdmissionContract contract{4, 2, 1, 4};
  TEST_ASSERT_TRUE(contract.isDeclared());
  // Three entries: fill plus two alternating merge passes =9 operations,
  // three 4-entry grains. Add rewrite1 and trim2 to both sorts: nine grains.
  TEST_ASSERT_EQUAL_UINT32(9, contract.requiredOrderGrains(3));
  TEST_ASSERT_TRUE(contract.canFinalize(1, 3));

  const StopAdmissionContract adequate{4, 11, 1, 4};
  TEST_ASSERT_TRUE(adequate.canFinalize(1, 3));
  TEST_ASSERT_EQUAL_UINT32(16, adequate.maximumTotalGrains());
}

void test_stop_contract_rejects_saturated_counts_and_invalid_quantum() {
  const StopAdmissionContract zeroQuantum{1, 1, 1, 0};
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, zeroQuantum.requiredOrderGrains(1));
  TEST_ASSERT_FALSE(zeroQuantum.canFinalize(0, 1));
  const StopAdmissionContract overflowing{1, UINT16_MAX, 1, 1};
  TEST_ASSERT_FALSE(overflowing.isDeclared());
  TEST_ASSERT_EQUAL_UINT16(0, overflowing.maximumTotalGrains());
  TEST_ASSERT_FALSE(overflowing.canFinalize(0, RuntimePlaybackViewConfig::kCaptureStagingEvents + 1));
  const StopAdmissionContract maximum{1, UINT16_MAX - 2, 1, 32};
  TEST_ASSERT_TRUE(maximum.isDeclared());
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, maximum.maximumTotalGrains());
  for (size_t count : {size_t(RuntimePlaybackViewConfig::kCaptureStagingEvents + 1), SIZE_MAX}) {
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, maximum.requiredOrderGrains(count));
    TEST_ASSERT_FALSE(maximum.canFinalize(0, count));
    PendingStopAdmission stop;
    TEST_ASSERT_FALSE(stop.begin(1, 100, 1, 0, count, maximum));
    TEST_ASSERT_FALSE(stop.pending);
  }
  const StopAdmissionContract closureOverflow{UINT16_MAX - 2, 1, 1, 32};
  TEST_ASSERT_FALSE(closureOverflow.canFinalize(RuntimePlaybackViewConfig::kMaxCaptureNoteReservations + 1, 2048));
}

void test_ranked_tail_bound_uses_declared_pass_and_base_horizons() {
  const auto compact = StopAdmissionContract::requiredRankedTailGrainsForPasses(2048, 2, 2048, 0, 0, 32);
  TEST_ASSERT_TRUE(compact < StopAdmissionContract::requiredRankedTailGrains(2048, 0, 0, 32));
  TEST_ASSERT_TRUE(compact > 3072); // Even two passes do not fit existing production credits.
  TEST_ASSERT_TRUE(compact < UINT16_MAX);
  for (size_t capacity : {size_t(1), size_t(16), size_t(128), size_t(2048)}) {
    const auto full = StopAdmissionContract::requiredRankedTailGrains(capacity, 1, 1, 32);
    TEST_ASSERT_EQUAL_UINT64(full, StopAdmissionContract::requiredRankedTailGrainsForPasses(capacity, capacity, capacity, 1, 1, 32));
    const auto onePass = StopAdmissionContract::requiredRankedTailGrainsForPasses(capacity, 1, capacity, 1, 1, 32);
    const auto emptyBase = StopAdmissionContract::requiredRankedTailGrainsForPasses(capacity, 1, 0, 1, 1, 32);
    TEST_ASSERT_TRUE(emptyBase <= onePass); TEST_ASSERT_TRUE(onePass <= full);
    TEST_ASSERT_TRUE(full <= StopAdmissionContract::requiredRankedTailGrainsForPasses(capacity, capacity, capacity, 1, 1, 1));
  }
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrainsForPasses(0, 0, 0, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrainsForPasses(2049, 1, 1, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrainsForPasses(16, 17, 0, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrainsForPasses(16, 1, 17, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrainsForPasses(16, 1, 1, 2049, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrainsForPasses(16, 1, 1, 0, 2049, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrainsForPasses(16, 1, 1, 0, 0, 0));
}

void test_stop_reserves_ranked_preparation_separately_and_preserves_stop_precedence() {
  const StopAdmissionContract contract{PendingStopAdmission::kMaximumClosureGrains, 3072, 1, 32};
  PendingStopAdmission stop;
  const auto ranked = StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(2048, 2, 2048, 0, 0, 32);
  TEST_ASSERT_TRUE(stop.begin(4, 100, 7, 1, 2048, contract, ranked));
  TEST_ASSERT_EQUAL_UINT16(4960, stop.remainingRankedPreparationGrains);
  TEST_ASSERT_EQUAL_UINT16(contract.requiredOrderGrains(2048), stop.remainingOrderGrains);
  TEST_ASSERT_FALSE(stop.consumeRankedPreparationGrain());
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  while (stop.remainingOrderGrains) TEST_ASSERT_TRUE(stop.consumeOrderGrain());
  TEST_ASSERT_FALSE(stop.canCommit(7, PendingStopDestination::Playing));
  TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain());
  stop.requestTransportStop();
  TEST_ASSERT_EQUAL_UINT16(4959, stop.remainingRankedPreparationGrains);
  while (stop.remainingRankedPreparationGrains) TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain());
  TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain()); // Estimate exhaustion is not a work limit.
  TEST_ASSERT_FALSE(stop.canCommit(7, PendingStopDestination::Playing));
  TEST_ASSERT_FALSE(stop.canCommit(8, PendingStopDestination::Playing));
  TEST_ASSERT_TRUE(stop.canCommit(8, PendingStopDestination::Stopped));
  stop.reset();
  TEST_ASSERT_EQUAL_UINT16(0, stop.remainingRankedPreparationGrains);
  TEST_ASSERT_FALSE(stop.consumeRankedPreparationGrain());
  TEST_ASSERT_TRUE(stop.begin(5, 200, 8, 0, 1, contract));
  TEST_ASSERT_EQUAL_UINT16(0, stop.remainingRankedPreparationGrains); // Legacy callers reserve no ranked work.
}

void test_stop_accepts_long_ranked_service_without_horizon_rejection() {
  const StopAdmissionContract contract{PendingStopAdmission::kMaximumClosureGrains, 3072, 1, 32};
  PendingStopAdmission stop;
  for (uint64_t grains : {uint64_t(6241), uint64_t(7648), uint64_t(UINT16_MAX) + 1}) {
    TEST_ASSERT_TRUE(stop.begin(4, 100, 7, 0, 1, contract, grains));
    TEST_ASSERT_TRUE(stop.pending);
    TEST_ASSERT_EQUAL_UINT64(grains, stop.remainingRankedPreparationGrains);
    stop.reset();
  }
  TEST_ASSERT_FALSE(stop.begin(4, 100, 7, 0, 1, contract, UINT64_MAX));
  TEST_ASSERT_FALSE(stop.pending);
  const StopAdmissionContract full{1, UINT16_MAX - 2, 1, 32};
  TEST_ASSERT_TRUE(stop.begin(4, 100, 7, 0, 1, full, 1));
  stop.reset();
  const StopAdmissionContract exact{1, UINT16_MAX - 2 - 6240, 1, 32};
  TEST_ASSERT_TRUE(stop.begin(4, 100, 7, 0, 1, exact, 6240));
  TEST_ASSERT_FALSE(stop.begin(5, 200, 8, 0, 1, contract, 0));
  TEST_ASSERT_EQUAL_UINT32(4, stop.frozenGeneration);
  TEST_ASSERT_EQUAL_UINT64(6240, stop.remainingRankedPreparationGrains);
}

void test_stop_completed_workers_release_large_unused_estimates_without_waiting() {
  PendingStopAdmission stop;
  const StopAdmissionContract contract{2,3072,1,32};
  TEST_ASSERT_TRUE(stop.begin(4,100,7,1,2,contract,73039,14806,9));
  stop.setPhase(PendingStopPhase::PrepareTransfer);
  TEST_ASSERT_FALSE(stop.retireCompletedPreparation()); // Closure is still owed.
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  TEST_ASSERT_FALSE(stop.retireCompletedPreparation()); // Ordering has not finished.
  stop.setPhase(PendingStopPhase::PrepareTransfer);
  TEST_ASSERT_TRUE(stop.retireCompletedPreparation());
  TEST_ASSERT_EQUAL_UINT64(0,stop.remainingOrderGrains);
  TEST_ASSERT_EQUAL_UINT64(73039,stop.remainingRankedPreparationGrains);
  TEST_ASSERT_FALSE(stop.canCommit(7,PendingStopDestination::Playing));
  stop.setPhase(PendingStopPhase::CompanionHistory);
  TEST_ASSERT_TRUE(stop.retireCompletedPreparation());
  TEST_ASSERT_FALSE(stop.canCommit(7,PendingStopDestination::Playing));
  stop.setPhase(PendingStopPhase::CompanionBridge);
  TEST_ASSERT_TRUE(stop.retireCompletedPreparation());
  TEST_ASSERT_TRUE(stop.canCommit(7,PendingStopDestination::Playing));
  TEST_ASSERT_EQUAL_UINT64(9,stop.remainingHistoryRecycleGrains);
}

void test_empty_stop_retires_skipped_tail_before_playing_or_transport_stop_commit() {
  const StopAdmissionContract contract{2,3072,1,32};
  for (bool transportStop : {false, true}) {
    PendingStopAdmission stop;
    // Empty successor after wrap: the setup credit previously blocked atomic transfer.
    TEST_ASSERT_TRUE(stop.begin(4,100,7,0,0,contract,0,1,1));
    TEST_ASSERT_FALSE(stop.retireSkippedCompanionPreparation(true));
    stop.setPhase(PendingStopPhase::PrepareTransfer);
    TEST_ASSERT_TRUE(stop.retireCompletedPreparation());
    TEST_ASSERT_TRUE(stop.retireSkippedCompanionPreparation(true));
    if (transportStop) stop.requestTransportStop();
    stop.setPhase(PendingStopPhase::AtomicTransfer);
    TEST_ASSERT_TRUE(stop.canCommit(transportStop ? 8 : 7,
        transportStop ? PendingStopDestination::Stopped : PendingStopDestination::Playing));
    Loop loop;
    LoopPlaybackRuntime runtime;
    const EmissionCursor cursor{};
    PreparedCapturePassTransfer transferred{};
    // Execute the same routing gate as Track, with no prepared bridge: a wrap
    // already committed all rows, so there is no new audible source to admit.
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::requiresBridgeRefresh(loop, runtime, stop, cursor));
    TEST_ASSERT_TRUE(TrackPreparedStopCommit::advanceAtomicTransfer(
        loop, runtime, stop, 0, cursor, transferred));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PendingStopPhase::DeferredCleanup),
        static_cast<uint8_t>(stop.phase));
  }
  PendingStopAdmission filtered;
  TEST_ASSERT_TRUE(filtered.begin(4,100,7,1,2,contract,6240,12,9));
  filtered.setPhase(PendingStopPhase::PrepareTransfer);
  TEST_ASSERT_FALSE(filtered.retireSkippedCompanionPreparation(true));
  TEST_ASSERT_TRUE(filtered.consumeClosureGrain());
  filtered.setPhase(PendingStopPhase::PrepareTransfer);
  TEST_ASSERT_FALSE(filtered.retireSkippedCompanionPreparation(true));
  TEST_ASSERT_TRUE(filtered.retireCompletedPreparation());
  TEST_ASSERT_FALSE(filtered.retireSkippedCompanionPreparation(false));
  TEST_ASSERT_TRUE(filtered.retireSkippedCompanionPreparation(true));
  TEST_ASSERT_TRUE(filtered.canCommit(7,PendingStopDestination::Playing));
}

void test_transport_stop_before_tail_retires_only_skipped_preparation() {
  PendingStopAdmission stop;
  const StopAdmissionContract contract{2,3072,1,32};
  TEST_ASSERT_TRUE(stop.begin(4,100,7,1,2,contract,73039,14806,9));
  TEST_ASSERT_FALSE(stop.retireSkippedCompanionPreparation(false));
  stop.requestTransportStop();
  TEST_ASSERT_FALSE(stop.retireSkippedCompanionPreparation(false));
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  stop.setPhase(PendingStopPhase::PrepareTransfer);
  TEST_ASSERT_FALSE(stop.retireSkippedCompanionPreparation(false)); // Order still owed.
  TEST_ASSERT_TRUE(stop.retireCompletedPreparation());
  TEST_ASSERT_TRUE(stop.retireSkippedCompanionPreparation(false));
  TEST_ASSERT_TRUE(stop.canCommit(8,PendingStopDestination::Stopped));
  TEST_ASSERT_FALSE(stop.canCommit(7,PendingStopDestination::Playing));
  TEST_ASSERT_FALSE(stop.canCommit(8,PendingStopDestination::Playing));
  TEST_ASSERT_EQUAL_UINT64(9,stop.remainingHistoryRecycleGrains);
  stop.reset();
  TEST_ASSERT_FALSE(stop.retireCompletedPreparation());
  TEST_ASSERT_FALSE(stop.retireSkippedCompanionPreparation(false));
}

void test_prepared_ranked_tail_bound_preserves_other_children_and_invalid_inputs() {
  TEST_ASSERT_EQUAL_UINT16(6240, StopAdmissionContract::kProvisionalRankedPreparationGrains);
  TEST_ASSERT_TRUE(StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(2048, 2, 2048, 0, 0, 32)
      <= StopAdmissionContract::kProvisionalRankedPreparationGrains);
  TEST_ASSERT_TRUE(StopAdmissionContract::requiredRankedTailGrainsForPasses(2048, 2, 2048, 0, 0, 32)
      > StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(2048, 2, 2048, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(4960,
      StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(2048, 2, 2048, 0, 0, 32));
  for (size_t capacity : {size_t(1), size_t(16), size_t(2048)}) {
    for (size_t quantum : {size_t(1), size_t(32)}) {
      for (size_t history : {size_t(0), size_t(1), size_t(2048)}) {
        for (size_t rows : {size_t(0), size_t(1), size_t(2048)}) {
          const auto ordinary = StopAdmissionContract::requiredRankedTailGrainsForPasses(
              capacity, capacity, capacity, history, rows, quantum);
          const auto prepared = StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(
              capacity, capacity, capacity, history, rows, quantum);
          TEST_ASSERT_TRUE(prepared <= ordinary);
          TEST_ASSERT_EQUAL_UINT64(ordinary -
              RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(capacity, history, quantum),
              prepared - RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(capacity, history, quantum));
        }
      }
    }
  }
  const size_t invalid[][6] = {
    {0,0,0,0,0,32}, {2049,1,1,0,0,32}, {16,17,0,0,0,32},
    {16,1,17,0,0,32}, {16,1,1,2049,0,32}, {16,1,1,0,2049,32}, {16,1,1,0,0,0}
  };
  for (const auto& v : invalid)
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(
        v[0],v[1],v[2],v[3],v[4],v[5]));
}

void test_maximum_frozen_capture_has_a_declared_stop_budget() {
  const StopAdmissionContract contract{
      PendingStopAdmission::kMaximumClosureGrains, 3072, 1, 32};
  TEST_ASSERT_TRUE(contract.canFinalize(PendingStopAdmission::kMaximumClosureGrains,
                                       RuntimePlaybackViewConfig::kCaptureStagingEvents));
}

static void prepareBridgeBase(RuntimePlaybackView& base, const std::vector<MidiEvent>& events) {
  TEST_ASSERT_TRUE(base.prewarm(RuntimePlaybackViewConfig::kEventsPerView));
  for (const MidiEvent& event : events) TEST_ASSERT_TRUE(base.storage.append(event));
  base.source = PlaybackViewSourceIdentity{1, 1, nullptr};
  base.coverage = PlaybackCoverageInterval{0, 768, 768};
  RuntimePlaybackOrderBuilder order;
  TEST_ASSERT_TRUE(order.begin(base, 768));
  while (order.step(7) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_TRUE(base.complete);
  base.published = true;
}

static void compileBridge(RuntimePlaybackView& base, FixedPlaybackBridgeStorage& bridge,
                           bool select = true) {
  bridge.coverage = base.coverage;
  bridge.source = PlaybackViewSourceIdentity{2, 2, nullptr};
  RuntimePlaybackBridgeBuild build;
  TEST_ASSERT_TRUE(build.begin(base, bridge));
  const MidiEvent* originalEvents = base.storage.events;
  const std::vector<uint16_t> originalOrder(base.storage.order,
                                           base.storage.order + base.storage.eventCount);
  size_t turns = 0;
  RuntimePlaybackOrderBuildResult result;
  do {
    TEST_ASSERT_FALSE(bridge.complete);
    result = build.step(1);
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
    TEST_ASSERT_TRUE(++turns < 200000);
    TEST_ASSERT_EQUAL_PTR(originalEvents, base.storage.events);
    for (size_t i = 0; i < originalOrder.size(); ++i)
      TEST_ASSERT_EQUAL_UINT16(originalOrder[i], base.storage.order[i]);
  } while (result == RuntimePlaybackOrderBuildResult::Pending);
  bridge.selected = select;
}

static void assertBridgeEqualsOracle(RuntimePlaybackView& base,
                                     FixedPlaybackBridgeStorage& bridge,
                                     const std::vector<MidiEvent>& expected) {
  RuntimePlaybackView oracle;
  prepareBridgeBase(oracle, expected);
  RuntimePlaybackBridgeCursor cursor;
  TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, 7));
  for (size_t i = 0; i < expected.size(); ++i) {
    const MidiEvent& wanted = oracle.storage.events[oracle.storage.order[i]];
    const MidiEvent* actual = cursor.peek();
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_UINT32(wanted.tick, actual->tick);
    TEST_ASSERT_EQUAL_UINT8(wanted.type, actual->type);
    TEST_ASSERT_EQUAL_UINT8(wanted.channel, actual->channel);
    TEST_ASSERT_EQUAL_UINT32(wanted.noteId, actual->noteId);
    if (wanted.isNoteOn() || wanted.isNoteOff()) {
      TEST_ASSERT_EQUAL_UINT8(wanted.data.noteData.note, actual->data.noteData.note);
      TEST_ASSERT_EQUAL_UINT8(wanted.data.noteData.velocity, actual->data.noteData.velocity);
    } else {
      TEST_ASSERT_EQUAL_UINT8(wanted.data.ccData.cc, actual->data.ccData.cc);
      TEST_ASSERT_EQUAL_UINT8(wanted.data.ccData.value, actual->data.ccData.value);
    }
    TEST_ASSERT_TRUE(cursor.advance());
    TEST_ASSERT_EQUAL_UINT32(7, cursor.emissionCursor().projectionCycle);
  }
  TEST_ASSERT_NULL(cursor.peek());
  TEST_ASSERT_FALSE(cursor.advance());
}

struct RankedBridgeFixture {
  LoopPlaybackRuntime runtime;
  RuntimePlaybackView base;
  FixedPlaybackBridgeStorage bridge;
  RuntimePlaybackBridgeBuild build;
  void prepare(const std::vector<MidiEvent>& events, const std::vector<EditPass>& rows,
               const PlaybackViewSourceIdentity* baseIdentity = nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    prepareBridgeBase(base, events);
    if (baseIdentity) base.source = *baseIdentity;
    TEST_ASSERT_TRUE(bridge.prewarm(2048));
    bridge.coverage = base.coverage; bridge.source = {2, 2, nullptr};
    RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
    auto& apply = runtime.frozenCompanionApply;
    TEST_ASSERT_TRUE(apply.beginBatch(work, base.storage.events, base.storage.eventCount,
                                      base.source, rows.data(), rows.size(), 768));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turns = 0; turns < 2000000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turns)
      result = apply.step(32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    auto& map = runtime.frozenCompanionBridgeMap;
    TEST_ASSERT_TRUE(map.begin(apply, base.storage.events, base.storage.eventCount, base.source));
    for (size_t turns = 0; turns < 10000; ++turns) {
      result = map.step(7);
      if (result != RuntimePlaybackOrderBuildResult::Pending) break;
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  }
  void compile(size_t quantum) {
    std::vector<uint16_t> order(base.storage.order, base.storage.order + base.storage.eventCount);
    std::vector<uint32_t> phases(base.storage.phases, base.storage.phases + base.storage.eventCount);
    TEST_ASSERT_TRUE(build.begin(base, bridge, &runtime.frozenCompanionBridgeMap));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turns = 0; turns < 200000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turns)
      result = build.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    for (size_t i = 0; i < order.size(); ++i) {
      TEST_ASSERT_EQUAL_UINT16(order[i], base.storage.order[i]);
      TEST_ASSERT_EQUAL_UINT32(phases[i], base.storage.phases[i]);
    }
    runtime.frozenCompanionBridgeMap.discard(); runtime.frozenCompanionApply.discard();
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, build.step(quantum));
    bridge.selected = true;
  }
};

static EditPass lengthRow(NoteId id, uint32_t end) {
  EditPass row; row.targetNoteId = id; row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length; row.endTick = end; return row;
}

struct CanonicalPrefixFixture {
  LoopPasses passes;
  uint32_t revision = 1, generation = 2, length = 768, epoch = 3;
  uint64_t storageGeneration = 0;
  LoopPlaybackRuntime runtime;
  PlaybackViewSourceIdentity source() const { return {revision, generation, &passes}; }
  RuntimeCanonicalPassInputs::CurrentSource live() const { return {revision, generation, length, storageGeneration}; }
  CanonicalPrefixFixture() {
    passes.recordPass.id = 1;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    LoopEventStore record;
    auto on = MidiEvent::NoteOn(10, 1, 60, 90); on.noteId = 1;
    TEST_ASSERT_TRUE(record.append(on));
    TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(90, 1, 60)));
    TEST_ASSERT_TRUE(record.detachChunksToCommittedChunkIds(passes.recordPass.committedChunkIds));
  }
  ~CanonicalPrefixFixture() {
    runtime.reset(false);
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
  }
  RuntimePlaybackOrderBuildResult step() { return runtime.stepCanonicalPrefix(passes, source(), live(), 1); }
  bool needsService() const { return runtime.canonicalPrefixNeedsService(passes, source(), length, storageGeneration); }
  void finish() {
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = step();
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_TRUE(runtime.canonicalPassInputs.ready());
    TEST_ASSERT_EQUAL_UINT32(2, runtime.canonicalPassInputs.eventCount());
    TEST_ASSERT_EQUAL_UINT32(10, runtime.canonicalPassInputs.data()[0].events[0].tick);
    TEST_ASSERT_FALSE(needsService());
  }
};

void test_continuous_prefix_reuses_completed_passes_after_appends() {
  CanonicalPrefixFixture f;
  f.passes.editPasses.push_back(lengthRow(1, 70));
  f.finish();
  const auto* record = f.runtime.canonicalPassInputs.data()[0].events;
  for (unsigned pass = 0; pass < 4; ++pass) {
    const auto prior = f.revision;
    OverdubPass added; added.id = pass + 2; added.mergeSequence = pass + 1;
    LoopEventStore events;
    auto on = MidiEvent::NoteOn(10 + pass, 1, 60, 90); on.noteId = pass + 2;
    TEST_ASSERT_TRUE(events.append(on));
    TEST_ASSERT_TRUE(events.append(MidiEvent::NoteOff(80 + pass, 1, 60)));
    TEST_ASSERT_TRUE(events.detachChunksToCommittedChunkIds(added.committedChunkIds));
    f.passes.overdubPasses.push_back(std::move(added)); ++f.revision;
    const auto kept = f.runtime.canonicalPassInputs.completedPassCount();
    TEST_ASSERT_TRUE(f.runtime.resumeCanonicalAfterAppend(f.passes, prior, f.source(), f.live()));
    TEST_ASSERT_EQUAL_UINT32(kept, f.runtime.canonicalPassInputs.completedPassCount());
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (unsigned turn = 0; turn < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = f.step();
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_PTR(record, f.runtime.canonicalPassInputs.data()[0].events);
    TEST_ASSERT_EQUAL_UINT32(70, record[1].tick);
    LoopPlaybackRuntime fresh; TEST_ASSERT_TRUE(fresh.prewarmViews());
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (unsigned turn = 0; turn < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = fresh.stepCanonicalPrefix(f.passes, f.source(), f.live(), 1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    const auto& reused = f.runtime.canonicalPassInputs;
    TEST_ASSERT_EQUAL_UINT32(fresh.canonicalPassInputs.passCount(), reused.passCount());
    for (size_t i = 0; i < reused.passCount(); ++i) {
      const auto& a = reused.data()[i]; const auto& b = fresh.canonicalPassInputs.data()[i];
      TEST_ASSERT_EQUAL_UINT32(b.count, a.count);
      for (size_t j = 0; j < a.count; ++j) {
        TEST_ASSERT_EQUAL_UINT32(b.events[j].tick, a.events[j].tick);
        TEST_ASSERT_EQUAL_UINT32(b.events[j].noteId, a.events[j].noteId);
        TEST_ASSERT_EQUAL(b.events[j].type, a.events[j].type);
        TEST_ASSERT_EQUAL_UINT8(b.events[j].data.noteData.note, a.events[j].data.noteData.note);
      }
    }
  }
  // A history mutation potentially affects every pass: do not reuse old results.
  const auto prior = f.revision; ++f.revision; ++f.storageGeneration;
  f.passes.editPasses[0].endTick = 55;
  TEST_ASSERT_FALSE(f.runtime.resumeCanonicalAfterAppend(f.passes, prior, f.source(), f.live()));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn = 0; turn < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn) result = f.step();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT32(55, f.runtime.canonicalPassInputs.data()[0].events[1].tick);
  f.runtime.reset(false);
  for (auto& pass : f.passes.overdubPasses) LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
}

void test_continuous_prefix_retains_transferred_tail() {
  CanonicalPrefixFixture f; f.finish();
  auto& inputs = f.runtime.canonicalPassInputs;
  const auto* record = inputs.data()[0].events;
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  capture.captureGeneration = 4;
  const auto cc = MidiEvent::ControlChange(123, 1, 7, 64);
  TEST_ASSERT_TRUE(capture.storage.append(cc)); TEST_ASSERT_TRUE(capture.freeze(4));
  RuntimeCanonicalPassInputs::FrozenSource frozen{f.revision, f.generation, f.epoch, f.length, capture};
  TEST_ASSERT_TRUE(inputs.appendFrozenOverdub(frozen));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = inputs.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  OverdubPass pass; pass.id = 2; pass.mergeSequence = 1;
  LoopEventStore committed; TEST_ASSERT_TRUE(committed.append(cc));
  TEST_ASSERT_TRUE(committed.detachChunksToCommittedChunkIds(pass.committedChunkIds));
  f.passes.overdubPasses.push_back(std::move(pass)); const auto prior = f.revision++;
  TEST_ASSERT_TRUE(f.runtime.resumeCanonicalAfterAppend(f.passes, prior, f.source(), f.live(), true));
  TEST_ASSERT_FALSE(inputs.captureAttached());
  TEST_ASSERT_EQUAL_UINT32(2, inputs.completedPassCount());
  // The former capture can now be reused without invalidating retained output.
  capture.reset();
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = f.step();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_PTR(record, inputs.data()[0].events);
  TEST_ASSERT_EQUAL_UINT32(3, inputs.eventCount());
  TEST_ASSERT_EQUAL_UINT32(123, inputs.data()[1].events[0].tick);
  f.runtime.reset(false);
  LoopEventStore::releaseChunkRefs(f.passes.overdubPasses[0].committedChunkIds);
}

void test_continuous_prefix_preserves_completed_work_when_append_interrupts_read() {
  CanonicalPrefixFixture f; f.finish();
  for (unsigned p = 0; p < 2; ++p) {
    OverdubPass pass; pass.id = p + 2; pass.mergeSequence = p + 1;
    LoopEventStore events;
    for (unsigned i = 0; i < 64; ++i) TEST_ASSERT_TRUE(events.append(MidiEvent::ControlChange(i, 1, 7, i)));
    TEST_ASSERT_TRUE(events.detachChunksToCommittedChunkIds(pass.committedChunkIds));
    f.passes.overdubPasses.push_back(std::move(pass)); const auto prior = f.revision++;
    TEST_ASSERT_TRUE(f.runtime.resumeCanonicalAfterAppend(f.passes, prior, f.source(), f.live()));
    TEST_ASSERT_EQUAL_UINT32(1, f.runtime.canonicalPassInputs.completedPassCount());
    if (!p) for (unsigned i = 0; i < 12; ++i) TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, f.step());
  }
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = f.step();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT32(130, f.runtime.canonicalPassInputs.eventCount());
  TEST_ASSERT_EQUAL_UINT32(3, f.runtime.canonicalPassInputs.passCount());
  f.runtime.reset(false);
  for (auto& pass : f.passes.overdubPasses) LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
}

void test_normalization_scratch_receipt_rejects_superseded_builder() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
  RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
  RuntimeCompanionPasses first, second;
  TEST_ASSERT_TRUE(first.prewarm(2048)); TEST_ASSERT_TRUE(second.prewarm(2048));
  const auto source = f.source();
  TEST_ASSERT_TRUE(first.begin(runtime.frozenCompanionApply, work, runtime.canonicalPassInputs, nullptr, 0, source, f.length));
  TEST_ASSERT_TRUE(second.begin(runtime.frozenCompanionApply, work, runtime.canonicalPassInputs, nullptr, 0, source, f.length));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, first.step(1));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = second.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_FALSE(first.normalizationMatches(runtime.canonicalPassInputs, source));
  TEST_ASSERT_TRUE(second.normalizationMatches(runtime.canonicalPassInputs, source));
  second.discard();
  TEST_ASSERT_FALSE(second.normalizationMatches(runtime.canonicalPassInputs, source));
}

void test_continuous_prefix_reuses_snapshot_identity_refresh() {
  CanonicalPrefixFixture f; f.finish();
  const auto count = f.runtime.canonicalPassInputs.completedPassCount();
  const PlaybackViewSourceIdentity refreshed{f.revision, f.generation, nullptr};
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending,
      f.runtime.stepCanonicalPrefix(f.passes, refreshed, f.live(), 1));
  TEST_ASSERT_EQUAL_UINT32(count, f.runtime.canonicalPassInputs.completedPassCount());
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn = 0; turn < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = f.runtime.stepCanonicalPrefix(f.passes, refreshed, f.live(), 1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT32(2, f.runtime.canonicalPassInputs.eventCount());
}

void test_live_canonical_base_prework_reuses_stop_worker_and_invalidates_source() {
  CanonicalPrefixFixture f;
  auto& runtime = f.runtime;
  auto& set = *runtime.viewSetIfPrewarmed();
  const auto current = set.lifecycle.current;
  const auto selected = set.bridgeLifecycle.selected;
  // A tiny live tail must not shrink either the event or pass horizon.
  TEST_ASSERT_TRUE(set.frozenCaptureStaging().storage.append(MidiEvent::ControlChange(1, 1, 7, 64)));
  TEST_ASSERT_EQUAL_UINT64(
      StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(2048, 12, 2048, 0, 0, 32),
      runtime.requiredStopRankedGrains(f.source(), 12, 0, 0, 32));
  for (unsigned revision = 0; revision < 2; ++revision) {
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn) {
      const bool prefixReady = runtime.canonicalPrefixPhase == LoopPlaybackRuntime::CanonicalPrefixPhase::Ready;
      result = runtime.stepCanonicalBase(f.passes, f.source(), f.live(),
          runtime.currentCycleCursor(turn % f.length, f.generation), 1);
      if (!prefixReady)
        TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CanonicalWindowPhase::Idle, runtime.canonicalWindowPhase);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    const auto* base = runtime.preparedCanonicalWindow(f.source());
    TEST_ASSERT_NOT_NULL(base);
    TEST_ASSERT_EQUAL_UINT32(2, base->storage.eventCount);
    const auto generation = runtime.canonicalPassInputs.generation();
    // Stop resumes the very same prepared base, without rebuilding or selecting it.
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,
        runtime.stepCanonicalBase(f.passes, f.source(), f.live(),
            runtime.currentCycleCursor(700, f.generation), 1));
    TEST_ASSERT_EQUAL_UINT64(generation, runtime.canonicalPassInputs.generation());
    TEST_ASSERT_EQUAL_PTR(base, runtime.preparedCanonicalWindow(f.source()));
    TEST_ASSERT_EQUAL_UINT8(current, set.lifecycle.current);
    TEST_ASSERT_EQUAL_UINT8(selected, set.bridgeLifecycle.selected);
    TEST_ASSERT_EQUAL_UINT64(
        StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(2048, 12, 2, 0, 0, 32),
        runtime.requiredStopRankedGrains(f.source(), 12, 0, 0, 32));
    if (revision == 1) {
      runtime.canonicalPassInputs.discard();
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
          runtime.stepCanonicalBase(f.passes, f.source(), f.live(),
              runtime.currentCycleCursor(700, f.generation), 1));
    }
    ++f.revision; ++f.storageGeneration;
    TEST_ASSERT_NULL(runtime.preparedCanonicalWindow(f.source()));
  }
}

static void prepareUnsortedFrozenTailCapture(LoopPlaybackRuntime& runtime) {
  auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  auto on = MidiEvent::NoteOn(96, 2, 64, 80); on.noteId = 2;
  TEST_ASSERT_TRUE(capture.storage.append(on));
  TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(0, 2, 7, 90)));
  TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::NoteOff(96, 2, 64)));
  TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(50, 2, 8, 80)));
  capture.storage.discardEvent(3);
  capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
}

static void finishRuntimeCanonicalCaptureOrder(LoopPlaybackRuntime& runtime) {
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(4));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCaptureCanonicalOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
}

void test_runtime_canonical_bridge_base_survives_live_ledger_refresh() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
  auto& set = *runtime.viewSetIfPrewarmed();
  auto* initialCurrent = &set.views[set.lifecycle.current];
  const EmissionCursor anchor{0,11,0,0,f.generation};
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = runtime.stepCanonicalWindow({0,f.length,f.length}, anchor, 1, true);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  const auto* base = runtime.preparedCanonicalWindow(f.source());
  TEST_ASSERT_EQUAL_PTR(&set.bridgeBases[0], base);
  TEST_ASSERT_EQUAL_PTR(initialCurrent, &set.views[set.lifecycle.current]);
  prepareUnsortedFrozenTailCapture(runtime); finishRuntimeCanonicalCaptureOrder(runtime);
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Shorten,1,1,60,90,10,70}};
  const RuntimeCanonicalPassInputs::FrozenSource live{f.revision,f.generation,f.epoch,f.length,set.frozenCaptureStaging()};
  const PlaybackViewSourceIdentity target{f.revision + 2, f.generation, nullptr};
  TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = runtime.stepFrozenCanonicalTail(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  runtime.ledger.noteOn(1,60,1,10,90);
  TEST_ASSERT_TRUE(runtime.beginFrozenCompanionBridge(f.source(),target,4,f.epoch,f.length,anchor));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = runtime.stepFrozenCaptureBridge(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(runtime.canSelectFrozenCaptureBridge(4,f.epoch,f.length,anchor));
  TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4,f.epoch,f.length,{0,90,0,0,f.generation}));
  const auto mapGeneration = runtime.frozenCompanionBridgeMap.generation();
  const auto bridgeGeneration = set.bridges[set.bridgeLifecycle.candidate].storageGeneration;
  const auto recordCount = set.bridges[set.bridgeLifecycle.candidate].recordCount;
  runtime.ledger.clear(); // Original note ended while preparation was running.
  const EmissionCursor later{0,120,0,0,f.generation};
  TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4,f.epoch,f.length,later));
  TEST_ASSERT_TRUE(runtime.refreshFrozenCompanionBridge(later));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn) {
    // Continuous unrelated note changes must not force another bridge build.
    runtime.ledger.noteOff(1,127);
    if (turn % 2) runtime.ledger.noteOn(1,127,127,120+turn,90);
    result = runtime.stepFrozenCaptureBridge(1);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT64(mapGeneration,runtime.frozenCompanionBridgeMap.generation());
  TEST_ASSERT_EQUAL_UINT64(bridgeGeneration,set.bridges[set.bridgeLifecycle.candidate].storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(recordCount,set.bridges[set.bridgeLifecycle.candidate].recordCount);
  TEST_ASSERT_TRUE(runtime.selectFrozenCaptureBridge(4,f.epoch,f.length,later));
  runtime.discardFrozenCaptureBridge(); runtime.discardCanonicalWindow(); runtime.discardFrozenCanonicalTail();
  const auto* selected = runtime.selectedFullLoopBridge(target,f.length);
  TEST_ASSERT_NOT_NULL(selected);
  TEST_ASSERT_EQUAL_PTR(base, runtime.fullLoopBridgeBase(*selected,f.length));
  bool yieldedAfterPreparingWindows = false;
  for (unsigned turn = 0; turn < 1000; ++turn) {
    if (!runtime.serviceBridgeWindow(120,192,1)) {
      yieldedAfterPreparingWindows = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(yieldedAfterPreparingWindows);
  TEST_ASSERT_LESS_THAN(set.views.size(), set.lifecycle.ready);
  TEST_ASSERT_LESS_THAN(set.views.size(), set.lifecycle.build);
  TEST_ASSERT_TRUE(set.views[set.lifecycle.ready].complete);
  TEST_ASSERT_TRUE(set.views[set.lifecycle.build].complete);
  const auto readyGeneration = set.views[set.lifecycle.ready].storageGeneration;
  const auto buildGeneration = set.views[set.lifecycle.build].storageGeneration;
  for (unsigned turn = 0; turn < 32; ++turn)
    TEST_ASSERT_FALSE(runtime.serviceBridgeWindow(120,192,1));
  TEST_ASSERT_EQUAL_UINT64(readyGeneration,set.views[set.lifecycle.ready].storageGeneration);
  TEST_ASSERT_EQUAL_UINT64(buildGeneration,set.views[set.lifecycle.build].storageGeneration);
  TEST_ASSERT_NOT_NULL(runtime.publishedBridgeWindow(target,120));
}

void test_runtime_canonical_window_prepares_spare_coverage_and_releases_tail_workers() {
  for (const auto coverage : {PlaybackCoverageInterval{0,192,768},
                              PlaybackCoverageInterval{650,200,768},
                              PlaybackCoverageInterval{192,192,768},
                              PlaybackCoverageInterval{384,192,768}}) {
    CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
    auto* set = runtime.viewSetIfPrewarmed();
    const MidiEvent audible[] = {MidiEvent::ControlChange(22, 1, 7, 99)};
    TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(audible, 1, f.source(), {0,768,768}));
    if (coverage.startPhase != 0) {
      // First-record handoff leaves no ready successor and a separate idle buffer.
      set->lifecycle.ready = RuntimePlaybackViewLifecycle::kNoView;
      set->lifecycle.idle = 1;
    }
    const auto currentIndex = set->lifecycle.current;
    const auto* currentEvents = set->views[currentIndex].storage.events;
    const EmissionCursor anchor{7, coverage.startPhase, 0, 0, f.generation};
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    unsigned fillTurns = 0;
    for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn) {
      const auto before = runtime.canonicalWindowPhase;
      result = runtime.stepCanonicalWindow(coverage, anchor, 1);
      if (before == LoopPlaybackRuntime::CanonicalWindowPhase::Fill) ++fillTurns;
      TEST_ASSERT_EQUAL_UINT8(currentIndex, set->lifecycle.current);
      TEST_ASSERT_EQUAL_PTR(currentEvents, set->views[currentIndex].storage.events);
      TEST_ASSERT_EQUAL_UINT32(1, set->views[currentIndex].storage.eventCount);
      TEST_ASSERT_EQUAL_UINT32(22, currentEvents[0].tick);
      if (result != RuntimePlaybackOrderBuildResult::Complete)
        TEST_ASSERT_NULL(runtime.preparedCanonicalWindow(f.source()));
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT32(2, fillTurns); // Includes rows outside coverage.
    const auto* window = runtime.preparedCanonicalWindow(f.source());
    TEST_ASSERT_NOT_NULL(window);
    TEST_ASSERT_EQUAL_PTR(window, &set->views[set->lifecycle.ready]);
    const size_t expectedCount = coverage.startPhase == 0 ? 2 : coverage.startPhase == 650 ? 1 : 0;
    TEST_ASSERT_EQUAL_UINT32(expectedCount, window->storage.eventCount);
    if (expectedCount) TEST_ASSERT_EQUAL_UINT16(0, window->passOrigins[0].event);
    TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
    TEST_ASSERT_TRUE(runtime.frozenCompanionApply.available());
    prepareUnsortedFrozenTailCapture(runtime); finishRuntimeCanonicalCaptureOrder(runtime);
    PendingNoteChangeVec changes;
    const RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch,
        f.length, set->frozenCaptureStaging()};
    const PlaybackViewSourceIdentity target{2, 3, &f.passes};
    TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = runtime.stepFrozenCanonicalTail(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_PTR(window, runtime.preparedCanonicalWindow(f.source()));
    TEST_ASSERT_EQUAL_UINT32(expectedCount, window->storage.eventCount);
    if (expectedCount) {
      TEST_ASSERT_EQUAL_UINT16(0, window->passOrigins[0].event);
      TEST_ASSERT_EQUAL_UINT32(10, window->storage.events[0].tick);
    }
    const EmissionCursor bridgeAnchor{7, coverage.startPhase, 0, 0, target.sourceGeneration};
    auto outsideAnchor = bridgeAnchor;
    outsideAnchor.phase = (coverage.startPhase + coverage.lengthTicks) % coverage.loopLengthTicks;
    TEST_ASSERT_FALSE(runtime.beginFrozenCompanionBridge(f.source(), target, 4, f.epoch, f.length, outsideAnchor));
    TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CaptureBridgePhase::Idle, runtime.captureBridgePhase);
    TEST_ASSERT_NULL(runtime.captureBridgeBase);
    TEST_ASSERT_TRUE(runtime.beginFrozenCompanionBridge(f.source(), target, 4, f.epoch, f.length, bridgeAnchor));
    TEST_ASSERT_EQUAL_PTR(window, runtime.frozenCaptureBridgeBase());
    // The older audible current is unrelated to this ready-window bridge.
    set->views[currentIndex].resetMetadata();
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = runtime.stepFrozenCaptureBridge(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    auto& bridge = set->bridges[set->bridgeLifecycle.candidate];
    TEST_ASSERT_TRUE(bridge.complete); TEST_ASSERT_FALSE(bridge.selected);
    // The candidate may use the immutable ready canonical window even though
    // the former audible-current view was retired above.
    TEST_ASSERT_TRUE(runtime.canSelectFrozenCaptureBridge(4, f.epoch, f.length, bridgeAnchor));
    TEST_ASSERT_TRUE(runtime.selectFrozenCaptureBridge(4, f.epoch, f.length, bridgeAnchor));
    TEST_ASSERT_TRUE(bridge.selected);
    // Exercise the selected stream against that canonical window.
    RuntimePlaybackBridgeCursor cursor;
    TEST_ASSERT_TRUE(cursor.selectAfterPhase(*window, bridge, 7));
    const uint32_t phases[] = {0,10,90,96,96};
    const size_t expectedEvents = coverage.startPhase == 0 ? 5 : coverage.startPhase == 650 ? 2 : 0;
    for (size_t i = 0; i < expectedEvents; ++i) {
      TEST_ASSERT_NOT_NULL(cursor.peek());
      TEST_ASSERT_EQUAL_UINT32(phases[i], cursor.peek()->tick);
      if (i == 3) TEST_ASSERT_TRUE(cursor.peek()->isNoteOff());
      if (i == 4) TEST_ASSERT_TRUE(cursor.peek()->isNoteOn());
      TEST_ASSERT_TRUE(cursor.advance());
    }
    TEST_ASSERT_NULL(cursor.peek());
    if (coverage.startPhase == 384) {
      // A published bridge takes ownership of its ready base. Cleanup must
      // release the preparation receipt without retiring that audible view,
      // so the next pass can build in the remaining slot.
      Loop loop; loop.loopLengthTicks = f.length; loop.openOverdubSession(384);
      TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
      PendingStopAdmission stop; stop.pending = true;
      stop.setPhase(PendingStopPhase::DeferredCleanup);
      // `ownsLiveSession` (DEC-068): these fixtures retire the only interval, so the job still
      // owns the live session and may end it.
      TEST_ASSERT_TRUE(TrackPreparedStopCommit::completeDeferredCleanup(loop, runtime, stop, stopCleanupPreview, true));
      TEST_ASSERT_TRUE(window->published);
      TEST_ASSERT_TRUE(cursor.selectAfterPhase(*window, bridge, 7));
      f.revision = target.playbackRevision; f.generation = target.sourceGeneration;
      f.finish();
      result = RuntimePlaybackOrderBuildResult::Pending;
      for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
        result = runtime.stepCanonicalWindow(coverage, bridgeAnchor, 1);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      TEST_ASSERT_NOT_EQUAL(window, runtime.preparedCanonicalWindow(f.source()));
      TEST_ASSERT_TRUE(cursor.selectAfterPhase(*window, bridge, 7));
      continue;
    }
    bridge.selected = false;
    // Retirement or same-identity buffer reuse invalidates completed indexes.
    auto& retired = set->views[runtime.canonicalWindowIndex];
    if (coverage.startPhase == 0) {
      bridge.selected = true;
      RuntimePlaybackBridgeCursor stale;
      TEST_ASSERT_TRUE(stale.selectAfterPhase(retired, bridge, 7));
      TEST_ASSERT_NOT_NULL(stale.peek());
      const auto generation = retired.storageGeneration;
      retired.resetMetadata();
      // Simulate rebuilding identical source/coverage/content in the same buffer.
      // clear() retains allocated bytes; restoring count exposes those same rows.
      retired.source = retired.passOriginSource = f.source();
      retired.coverage = coverage;
      retired.storage.eventCount = expectedCount;
      retired.sourceFilled = retired.passOriginsReady = retired.complete = retired.published = true;
      TEST_ASSERT_NOT_EQUAL(generation, retired.storageGeneration);
      TEST_ASSERT_NULL(stale.peek());
      TEST_ASSERT_FALSE(stale.advance());
      TEST_ASSERT_FALSE(cursor.selectAfterPhase(retired, bridge, 7));
      bridge.baseStorageGeneration = retired.storageGeneration;
      TEST_ASSERT_NULL(stale.peek()); // A newly compiled bridge cannot revive an old cursor.
      bridge.baseStorageGeneration = generation;
      bridge.selected = false;
    } else if (coverage.startPhase == 650) retired.published = false;
    else retired.passOriginsReady = false;
    TEST_ASSERT_NULL(runtime.frozenCaptureBridgeBase());
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureBridge(1));
  }
}

void test_runtime_canonical_window_waits_for_shared_builder_without_stealing_it() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
  auto* set = runtime.viewSetIfPrewarmed();
  auto& pending = set->views[set->lifecycle.build];
  const MidiEvent source[] = {MidiEvent::ControlChange(10, 1, 7, 99)};
  const PlaybackCoverageInterval coverage{0,192,768};
  const EmissionCursor anchor{0, 0, 0, 0, f.generation};
  LedgerBoundarySummary ledger;
  TEST_ASSERT_TRUE(runtime.viewBuild.begin(pending, source, 1, f.source(), coverage, anchor, ledger));
  for (unsigned turn = 0; turn < 3; ++turn) {
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepCanonicalWindow(coverage, anchor, 1));
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, runtime.viewBuild.nextGrain());
    TEST_ASSERT_EQUAL_UINT32(0, pending.storage.eventCount);
    TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
  }
  runtime.viewBuild.discard();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepCanonicalWindow(coverage, anchor, 1));
  TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CanonicalWindowPhase::Merge, runtime.canonicalWindowPhase);
  runtime.reset(false);
  TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
  TEST_ASSERT_TRUE(runtime.frozenCompanionApply.available());
  TEST_ASSERT_FALSE(runtime.viewBuild.active());
}

void test_runtime_canonical_window_source_change_cancels_only_pending_work() {
  for (unsigned stage = 0; stage < 4; ++stage) {
    CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
    const PlaybackCoverageInterval coverage{0,192,768};
    const EmissionCursor anchor{0, 0, 0, 0, f.generation};
    const auto desired = stage == 0 ? LoopPlaybackRuntime::CanonicalWindowPhase::Merge
        : stage == 1 ? LoopPlaybackRuntime::CanonicalWindowPhase::Fill
        : stage == 2 ? LoopPlaybackRuntime::CanonicalWindowPhase::Order
                     : LoopPlaybackRuntime::CanonicalWindowPhase::Ready;
    for (unsigned turn = 0; turn < 1000 && runtime.canonicalWindowPhase != desired; ++turn)
      TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
                           runtime.stepCanonicalWindow(coverage, anchor, 1));
    TEST_ASSERT_EQUAL(desired, runtime.canonicalWindowPhase);
    ++f.storageGeneration;
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepCanonicalWindow(coverage, anchor, 1));
    TEST_ASSERT_NULL(runtime.preparedCanonicalWindow(f.source()));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, f.step());
    TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CanonicalWindowPhase::Idle, runtime.canonicalWindowPhase);
    TEST_ASSERT_FALSE(runtime.viewBuild.active());
    TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
    f.finish();
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = runtime.stepCanonicalWindow(coverage, anchor, 1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  }
}

void test_frozen_tail_copies_prepared_canonical_indexes_before_audible_reuse() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
  prepareUnsortedFrozenTailCapture(runtime); finishRuntimeCanonicalCaptureOrder(runtime);
  auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  PendingNoteChangeVec changes;
  RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
  const PlaybackViewSourceIdentity target{2, 3, &f.passes};
  TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.borrowsCaptureOrder());
  for (size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepFrozenCanonicalTail(1));
    TEST_ASSERT_EQUAL(i != 2, runtime.canonicalPassInputs.borrowsCaptureOrder());
  }
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(f.length, 4));
  while (runtime.stepFrozenCaptureOrder(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_EQUAL_UINT16(2, capture.storage.order[1]); // Audible Off before On.
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCanonicalTail(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  const auto& tail = runtime.canonicalPassInputs.data()[1];
  TEST_ASSERT_EQUAL_UINT32(3, tail.count);
  TEST_ASSERT_EQUAL_UINT32(0, tail.events[0].tick);
  TEST_ASSERT_TRUE(tail.events[1].isNoteOn()); // Canonical raw-tick ties stay stable.
  TEST_ASSERT_TRUE(tail.events[2].isNoteOff());
  runtime.discardFrozenCaptureCanonicalOrder();
  TEST_ASSERT_TRUE(runtime.frozenCanonicalTailReady(target)); // Private copy is independent.
}

void test_frozen_tail_denies_shared_order_reuse_during_index_copy() {
  for (unsigned action = 0; action < 2; ++action) {
    CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
    prepareUnsortedFrozenTailCapture(runtime); finishRuntimeCanonicalCaptureOrder(runtime);
    auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    PendingNoteChangeVec changes;
    RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
    const PlaybackViewSourceIdentity target{2, 3, &f.passes};
    TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepFrozenCanonicalTail(1));
    if (action == 0) TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(f.length, 4));
    else {
      runtime.discardFrozenCaptureCanonicalOrder();
      finishRuntimeCanonicalCaptureOrder(runtime); // Same capture generation, new order storage generation.
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCanonicalTail(1));
    TEST_ASSERT_FALSE(runtime.frozenCanonicalTailReady(target));
    TEST_ASSERT_NULL(runtime.canonicalPassInputs.data());
    TEST_ASSERT_TRUE(capture.frozen); TEST_ASSERT_EQUAL_UINT32(4, capture.storage.eventCount);
  }
}

void test_frozen_tail_uses_private_sort_when_shared_order_is_audible() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
  prepareUnsortedFrozenTailCapture(runtime); finishRuntimeCanonicalCaptureOrder(runtime);
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(f.length, 4));
  while (runtime.stepFrozenCaptureOrder(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  PendingNoteChangeVec changes;
  RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
  const PlaybackViewSourceIdentity target{2, 3, &f.passes};
  TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
  TEST_ASSERT_FALSE(runtime.canonicalPassInputs.borrowsCaptureOrder());
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCanonicalTail(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.data()[1].events[1].isNoteOn());
  TEST_ASSERT_EQUAL_UINT16(2, capture.storage.order[1]);
  const auto reused = RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(2048, 0, 32);
  TEST_ASSERT_EQUAL_UINT64(196, reused);
  TEST_ASSERT_TRUE(reused < RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(2048, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(2048, 2049, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(2048, 0, 0));
}

void test_frozen_canonical_order_receipt_expires_after_audible_discard() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
  prepareUnsortedFrozenTailCapture(runtime); finishRuntimeCanonicalCaptureOrder(runtime);
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(f.length, 4));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCaptureOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  runtime.discardFrozenCaptureOrder();
  TEST_ASSERT_NOT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(4));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureCanonicalOrder(1));
  LoopEventStore destination;
  TEST_ASSERT_TRUE(destination.append(MidiEvent::ControlChange(77, 1, 7, 80)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
      runtime.stepFrozenCaptureCanonicalRewrite(destination, 4, 1));
  TEST_ASSERT_FALSE(runtime.frozenCaptureRewrite.active);
  TEST_ASSERT_EQUAL_UINT32(1, destination.size());
  auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  PendingNoteChangeVec changes;
  RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
  const PlaybackViewSourceIdentity target{2, 3, &f.passes};
  TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
  TEST_ASSERT_FALSE(runtime.canonicalPassInputs.borrowsCaptureOrder());
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCanonicalTail(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.data()[1].events[1].isNoteOn());
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.data()[1].events[2].isNoteOff());
  runtime.discardFrozenCaptureCanonicalOrder();
  finishRuntimeCanonicalCaptureOrder(runtime); // Explicit rebuild gets a new current receipt.
  TEST_ASSERT_EQUAL_UINT16(0, capture.storage.order[1]);
}

void test_frozen_audible_order_receipt_denies_shared_storage_invalidation() {
  for (bool complete : {false, true}) {
    CanonicalPrefixFixture f; auto& runtime = f.runtime;
    prepareUnsortedFrozenTailCapture(runtime);
    TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(f.length, 4));
    if (complete) {
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
        result = runtime.stepFrozenCaptureOrder(1);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      TEST_ASSERT_TRUE(runtime.frozenCaptureCandidateReady(4));
    }
    runtime.discardFrozenCaptureCanonicalOrder();
    TEST_ASSERT_FALSE(runtime.frozenCaptureCandidateReady(4));
    TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(f.length, 4));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureOrder(1));
    runtime.discardFrozenCaptureOrder();
    TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(f.length, 4));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepFrozenCaptureOrder(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_TRUE(runtime.frozenCaptureCandidateReady(4));
  }
}

void test_prepared_frozen_tail_capacity_and_all_filtered_bounds() {
  for (bool retain : {false, true}) {
    CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
    auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    for (size_t i = 0; i < 2048; ++i) {
      TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(2048 - i, 2, 7, 80)));
      if (!retain || i >= 2046) capture.storage.discardEvent(i);
    }
    capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
    TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(4));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1600 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepFrozenCaptureCanonicalOrder(32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
    RuntimeCanonicalPassInputs::PreparedCaptureOrder order{capture.storage.order,
        runtime.frozenCaptureCanonicalOrder.orderedEventCount(), runtime.frozenCaptureOrderStorageGeneration};
    TEST_ASSERT_TRUE(runtime.canonicalPassInputs.appendFrozenOverdub(live, &order));
    const auto bound = RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(2048, 0, 32);
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (uint64_t turn = 0; turn < bound && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = runtime.canonicalPassInputs.step(32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT32(retain ? 2048 : 2, runtime.canonicalPassInputs.eventCount());
    TEST_ASSERT_EQUAL_UINT32(retain ? 2 : 1, runtime.canonicalPassInputs.passCount());
    TEST_ASSERT_FALSE(runtime.canonicalPassInputs.borrowsCaptureOrder());
    if (retain) {
      const auto& tail = runtime.canonicalPassInputs.data()[1];
      TEST_ASSERT_EQUAL_UINT32(3, tail.events[0].tick);
      TEST_ASSERT_EQUAL_UINT32(2048, tail.events[2045].tick);
    }
  }
}

void test_frozen_canonical_tail_coordinates_children_and_reuses_owned_prefix() {
  for (bool preparedOrder : {false, true}) {
    CanonicalPrefixFixture f; f.finish();
    auto& runtime = f.runtime;
    const auto* prefix = runtime.canonicalPassInputs.data()[0].events;
    auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    capture.captureGeneration = 4;
    TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(100, 2, 7, 80)));
    PendingNoteChangeVec changes{{PendingNoteChangeKind::Shorten, 1, 1, 60, 90, 10, 50}};
    RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
    PlaybackViewSourceIdentity target{3, 3, &f.passes};
    TEST_ASSERT_FALSE(runtime.beginFrozenCanonicalTail(live, changes, target));
    TEST_ASSERT_TRUE(runtime.canonicalPassInputs.ready());
    TEST_ASSERT_TRUE(capture.freeze(4));
    if (preparedOrder) finishRuntimeCanonicalCaptureOrder(runtime);
    const auto aggregate = preparedOrder
        ? StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(3, 2, 2, 0, 1, 1)
        : StopAdmissionContract::requiredRankedTailGrainsForPasses(3, 2, 2, 0, 1, 1);
    PendingStopAdmission stop;
    const StopAdmissionContract contract{1, 3072, 1, 1};
    TEST_ASSERT_TRUE(stop.begin(4, 100, f.epoch, 0, 1, contract, aggregate));
    TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain()); // Tail setup is its own turn.
    TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
    TEST_ASSERT_EQUAL(preparedOrder, runtime.canonicalPassInputs.borrowsCaptureOrder());
    TEST_ASSERT_FALSE(runtime.beginFrozenCanonicalTail(live, changes, target));
    TEST_ASSERT_FALSE(f.needsService());
    TEST_ASSERT_FALSE(runtime.frozenCaptureCompanions.ready(capture));
    const auto bound = RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(3, 0, 1) +
        RuntimeCompanionPasses::requiredGrainsUpperBound(3, 2, 0, 1, 1) + 4;
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    uint64_t rankedTurns = 0;
    for (uint64_t turn = 0; turn < bound && result == RuntimePlaybackOrderBuildResult::Pending; ++turn) {
      ++rankedTurns;
      const auto phase = runtime.canonicalTailPhase;
      TEST_ASSERT_FALSE(runtime.frozenCanonicalTailReady(target));
      const auto credits = stop.remainingRankedPreparationGrains;
      result = runtime.stepReservedFrozenCanonicalTail(stop, 1);
      TEST_ASSERT_EQUAL_UINT16(credits - 1, stop.remainingRankedPreparationGrains);
      if (phase == LoopPlaybackRuntime::CanonicalTailPhase::Normalize) {
        TEST_ASSERT_FALSE(runtime.frozenCaptureCompanions.ready(capture));
        TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
      } else if (phase == LoopPlaybackRuntime::CanonicalTailPhase::Rows) {
        TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
      }
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_TRUE(runtime.frozenCanonicalTailReady(target));
    TEST_ASSERT_FALSE(runtime.frozenCanonicalTailReady(f.source()));
    TEST_ASSERT_EQUAL_PTR(prefix, runtime.canonicalPassInputs.data()[0].events);
    TEST_ASSERT_EQUAL_UINT32(3, runtime.frozenCompanionPasses.size());
    TEST_ASSERT_EQUAL_UINT32(50, runtime.frozenCompanionPasses.data()[1].tick);
    TEST_ASSERT_EQUAL_UINT32(100, runtime.frozenCompanionPasses.data()[2].tick);
    // Publish an unedited baseline with owned provenance, then use the normal
    // ranked bridge entry point on the coordinator's typed output.
    RuntimeCompanionPasses baseline; TEST_ASSERT_TRUE(baseline.prewarm(2048));
    RuntimeCompanionPasses::Pass basePass{prefix, 2};
    auto baseSource = f.source();
    RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
    TEST_ASSERT_TRUE(baseline.begin(runtime.frozenCompanionApply, work, &basePass, 1, nullptr, 0, baseSource, f.length));
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = baseline.step(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    auto& set = *runtime.viewSetIfPrewarmed();
    auto& base = set.views[set.lifecycle.current];
    // Exercise production's STOPPED typed publisher, including upgrade from a
    // flat view with the same identity (which must not hit the old cache path).
    TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(baseline.data(), baseline.size(), baseSource, {0,768,768}));
    TEST_ASSERT_FALSE(base.passOriginsReady);
    TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(baseline, baseSource, {0,768,768}));
    TEST_ASSERT_TRUE(base.passOriginsReady);
    TEST_ASSERT_TRUE(base.passOriginSource == baseSource);
    TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(baseline, baseSource, {0,768,768}));
    TEST_ASSERT_FALSE(runtime.publishStoppedOracleView(baseline, target, {0,768,768}));
    TEST_ASSERT_FALSE(runtime.publishStoppedOracleView(baseline, baseSource, {0,384,768}));
    TEST_ASSERT_TRUE(base.published); TEST_ASSERT_TRUE(base.passOriginsReady);
    baseline.discard(); // Published provenance has independent ownership.
    EmissionCursor anchor{}; anchor.effectiveSourceGeneration = target.sourceGeneration;
    TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain()); // Bridge setup is a separate turn.
    TEST_ASSERT_TRUE(runtime.beginFrozenCompanionBridge(baseSource, target, 4, f.epoch, f.length, anchor));
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) {
      ++rankedTurns;
      const auto credits = stop.remainingRankedPreparationGrains;
      result = runtime.stepReservedFrozenCaptureBridge(stop, 1);
      TEST_ASSERT_EQUAL_UINT16(credits - 1, stop.remainingRankedPreparationGrains);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT64(rankedTurns + 2, aggregate - stop.remainingRankedPreparationGrains);
    const auto credits = stop.remainingRankedPreparationGrains;
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, runtime.stepReservedFrozenCanonicalTail(stop, 1));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, runtime.stepReservedFrozenCaptureBridge(stop, 1));
    TEST_ASSERT_EQUAL_UINT16(credits, stop.remainingRankedPreparationGrains);
    TEST_ASSERT_TRUE(rankedTurns + 2 <= aggregate);
    TEST_ASSERT_TRUE(runtime.canSelectFrozenCaptureBridge(4, f.epoch, f.length, anchor));
    // Complete stop accounting and prepared audible readiness are independent.
    auto commitStop = stop;
    commitStop.setPhase(PendingStopPhase::AtomicTransfer);
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, anchor));
    while (commitStop.remainingOrderGrains) TEST_ASSERT_TRUE(commitStop.consumeOrderGrain());
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, anchor));
    while (commitStop.remainingRankedPreparationGrains) TEST_ASSERT_TRUE(commitStop.consumeRankedPreparationGrain());
    commitStop.setPhase(PendingStopPhase::PrepareTransfer);
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, anchor));
    commitStop.setPhase(PendingStopPhase::AtomicTransfer);
    TEST_ASSERT_TRUE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, anchor));
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, target, target, f.length, anchor));
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, baseSource, f.length, anchor));
    auto outside = anchor; outside.phase = f.length;
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, outside));
    runtime.ledger.noteOn(1, 60, 1, 10, 90);
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, anchor));
    runtime.ledger.clear();
    TEST_ASSERT_TRUE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, anchor));
    TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge, set.bridgeLifecycle.selected);
    TEST_ASSERT_EQUAL_UINT32(1, f.revision); // Preflight never commits or selects.
    commitStop.requestTransportStop();
    TEST_ASSERT_FALSE(runtime.canSelectReservedFrozenCaptureBridge(commitStop, baseSource, target, f.length, anchor));

    stop.requestTransportStop();
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepReservedFrozenCaptureBridge(stop, 1));
    TEST_ASSERT_EQUAL_UINT16(credits, stop.remainingRankedPreparationGrains);
    ++f.epoch;
    TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4, f.epoch, f.length, anchor));
    runtime.discardFrozenCaptureBridge();
    TEST_ASSERT_FALSE(runtime.frozenCanonicalTailReady(target));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCanonicalTail(1));
    runtime.discardFrozenCanonicalTail();
    TEST_ASSERT_TRUE(f.needsService());
    TEST_ASSERT_TRUE(capture.frozen); TEST_ASSERT_EQUAL_UINT32(1, capture.storage.eventCount);
    f.finish();
  }
}

void test_companion_history_reservation_survives_snapshot_cleanup() {
  Loop loop;
  loop.resetPassTimeline();
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4));
  const auto generation = loop.canonicalStorageGeneration;
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(4));
  loop.closeOverdubSession();
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(4));
  TEST_ASSERT_FALSE(loop.queueLoadedSnapshotPublish()); // Empty load cleanup.
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(4));
  TEST_ASSERT_EQUAL_UINT64(generation, loop.canonicalStorageGeneration);
  // A new take must reserve after its reset; stale generations stay rejected.
  loop.resetPassTimeline();
  TEST_ASSERT_FALSE(loop.preparedCompanionHistoryPrewarmed(4));
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(4));
}

/// Demand-shaped preparation work: the working row vector is sized to *this* interval's
/// actual demand, so the preparation's unit cost scales with the rows it produces, not
/// with the horizon it was granted. A stop that prepares one companion row over a
/// 512-row reserve must not spend ~511 trim units walking unused tail rows; it must reach
/// Ready inside a small constant envelope, and the materialized size must equal the
/// demand (canonical rows plus produced companion rows), not the reserve.

/// The companion reserve is a *row* reserve. A frozen change vector also carries the
/// interval's `Add` entries, which become the capture pass and never become companion
/// rows, so admission must bound the rows the set will produce - not the vector size.
/// Before this, a dense interval with a small transform set was denied its companion
/// path (and with a derived reserve, could have denied the stop) for rows it never
/// produced.
void test_capture_companions_reserve_bounds_eligible_rows() {
  CanonicalPrefixFixture f;
  prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();

  RuntimeCaptureCompanions rows;
  TEST_ASSERT_TRUE(rows.prewarm(4));
  PendingNoteChangeVec dense;
  for (unsigned i = 0; i < 8; ++i) {
    dense.push_back(PendingNoteChange{PendingNoteChangeKind::Add, static_cast<NoteId>(i + 1), 1, 60,
                                      100, static_cast<uint32_t>(i * 10), static_cast<uint32_t>(i * 10 + 5)});
  }
  dense.push_back(PendingNoteChange{PendingNoteChangeKind::Hide, 2, 1, 62, 80, 20, 60});
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(RuntimeCaptureCompanions::eligibleRowCount(dense)));
  TEST_ASSERT_TRUE(rows.begin(capture, 4, dense));
  while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {
  }
  TEST_ASSERT_TRUE(rows.ready(capture));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(rows.size()));

  // Five producible rows over a four-row reserve is a real denial, not a silent prefix.
  RuntimeCaptureCompanions small;
  TEST_ASSERT_TRUE(small.prewarm(4));
  PendingNoteChangeVec transforms;
  for (unsigned i = 0; i < 5; ++i) {
    transforms.push_back(PendingNoteChange{PendingNoteChangeKind::Hide, static_cast<NoteId>(i + 10), 1,
                                           62, 80, 20, 60});
  }
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(RuntimeCaptureCompanions::eligibleRowCount(transforms)));
  TEST_ASSERT_FALSE(small.begin(capture, 4, transforms));
  TEST_ASSERT_TRUE(small.idle());
}

/// The derived companion-row bound scales with the loop's canonical payloads and still
/// reaches the architectural ceiling, so a dense loop keeps the full envelope.
void test_companion_row_horizon_scales_with_content_up_to_the_ceiling() {
  Loop loop;
  const size_t emptyBound = loop.companionRowHorizonBound();
  TEST_ASSERT_TRUE(emptyBound >= RuntimePlaybackViewConfig::kMaxCaptureNoteReservations);
  TEST_ASSERT_TRUE(emptyBound <= RuntimePlaybackViewConfig::kRecordsPerBridge);

  loop.passes.editPasses.resize(1);
  for (unsigned i = 0; i < 300; ++i) {
    loop.passes.editPasses[0].addedEvents.push_back(MidiEvent::NoteOn(i, 1, 60, 100));
  }
  TEST_ASSERT_EQUAL_UINT32(300 + RuntimePlaybackViewConfig::kMaxCaptureNoteReservations,
                           static_cast<uint32_t>(loop.companionRowHorizonBound()));
  TEST_ASSERT_TRUE(loop.companionRowHorizonBound() < RuntimePlaybackViewConfig::kRecordsPerBridge);

  for (unsigned i = 300; i < RuntimePlaybackViewConfig::kRecordsPerBridge; ++i) {
    loop.passes.editPasses[0].addedEvents.push_back(MidiEvent::NoteOn(i, 1, 60, 100));
  }
  // Maximum envelope: a dense loop is still granted the full row ceiling.
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kRecordsPerBridge,
                           static_cast<uint32_t>(loop.companionRowHorizonBound()));
}

void test_companion_history_prep_units_scale_with_demand_not_reserve() {
  Loop loop;
  loop.nextPassId_ = 100;
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(512));
  TEST_ASSERT_EQUAL_UINT32(512, static_cast<uint32_t>(loop.preparedCompanionHistory_.companionCapacity));
  TEST_ASSERT_EQUAL_UINT32(512, static_cast<uint32_t>(loop.preparedCompanionHistory_.rows.capacity()));
  CanonicalPrefixFixture f;
  prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  auto& rows = f.runtime.frozenCaptureCompanions;
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 2, 1, 62, 80, 20, 60}};
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {
  }
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(rows.size()));
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
  // One produced companion row over a 512-row grant. The old horizon walk needed
  // 511 `Phase::Trim` units here; the demand-sized vector needs a handful.
  unsigned units = 0;
  while (!loop.preparedCompanionHistoryReady() && units < 64) {
    TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    ++units;
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.preparedCompanionHistoryReady(),
                           "preparation must reach Ready well inside the quantum budget");
  TEST_ASSERT_TRUE(units <= 32);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(loop.preparedCompanionHistory()->size()));
  // The grant is untouched: a later, larger interval still fits the same reserve.
  TEST_ASSERT_EQUAL_UINT32(512, static_cast<uint32_t>(loop.preparedCompanionHistory_.companionCapacity));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistory_.rows.capacity() >= 512);
}

void test_companion_history_binds_wrap_publication_before_begin() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    Loop loop; loop.nextPassId_ = 100;
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4));
    auto* reservedRows = loop.preparedCompanionHistory_.rows.data();
    auto* reservedIds = loop.preparedCompanionHistory_.snapshotIds.data();
    // Use the same canonical append as wrap companion sealing. It changes
    // storage generation after prewarm but before final-stop preparation.
    const auto wrapId = loop.saveNoteEditPass(kOverdubCompanionEditPassIndex,
        lengthRow(1, 70), EditPassType::Note, true, true);
    TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, wrapId);
    CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
    auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    auto& rows = f.runtime.frozenCaptureCompanions;
    PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 2, 1, 62, 80, 20, 60}};
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    if (mode == 2) {
      for (unsigned i = 0; i < 3; ++i)
        loop.saveNoteEditPass(kOverdubCompanionEditPassIndex,
            lengthRow(1, 60), EditPassType::Note, true, true);
      TEST_ASSERT_FALSE(loop.beginPreparedCompanionHistory(rows, capture));
      continue;
    }
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    if (mode == 1) {
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex,
          lengthRow(1, 60), EditPassType::Note, true, true);
      TEST_ASSERT_FALSE(loop.stepPreparedCompanionHistory(1));
      TEST_ASSERT_FALSE(loop.preparedCompanionHistoryReady());
      continue;
    }
    for (unsigned turn = 0; turn < 32 && !loop.preparedCompanionHistoryReady(); ++turn)
      TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady());
    TEST_ASSERT_EQUAL_PTR(reservedRows, loop.preparedCompanionHistory_.rows.data());
    TEST_ASSERT_EQUAL_PTR(reservedIds, loop.preparedCompanionHistory_.snapshotIds.data());
    TEST_ASSERT_EQUAL_UINT32(2, loop.preparedCompanionHistory()->size());
    TEST_ASSERT_EQUAL_UINT32(wrapId, loop.preparedCompanionHistory()->at(0).id);
    TEST_ASSERT_EQUAL_UINT32(70, loop.preparedCompanionHistory()->at(0).endTick);
    TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_EQUAL_UINT32(2, loop.passes.editPasses.size());
    TEST_ASSERT_TRUE(loop.beginRecyclePreparedCompanionHistory());
    for (unsigned turn = 0; turn < 32 && !loop.preparedCompanionHistoryPrewarmed(); ++turn)
      TEST_ASSERT_TRUE(loop.stepRecyclePreparedCompanionHistory(1));
    TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(1));
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    for (unsigned turn = 0; turn < 32 && !loop.preparedCompanionHistoryReady(); ++turn)
      TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_EQUAL_UINT32(3, loop.passes.editPasses.size());
  }
}

/// The prepared companion history is interval-qualified: while an admitted stop
/// job owns an in-flight preparation, no other interval may observe, advance,
/// commit, recycle or discard it. "Other interval" is the successor job (a
/// different token) and the live session (no token at all). The horizon is
/// receipt-free and owner-agnostic again once recycling ends, so the successor
/// can begin its own preparation.
void test_prepared_companion_history_is_job_qualified() {
  Loop loop; loop.nextPassId_ = 100;
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4));
  CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  auto& rows = f.runtime.frozenCaptureCompanions;
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 2, 1, 62, 80, 20, 60}};
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  constexpr uint64_t kJobA = 1;
  constexpr uint64_t kJobB = 2;
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture, nullptr, kJobA));
  // A successor interval and the live session cannot drive the preparation.
  TEST_ASSERT_FALSE(loop.stepPreparedCompanionHistory(1, kJobB));
  TEST_ASSERT_FALSE(loop.stepPreparedCompanionHistory(1));
  // A stale discard must not clear the owner's in-flight work.
  loop.discardPreparedCompanionHistory(kJobB);
  loop.discardPreparedCompanionHistory();
  size_t turns = 0;
  while (!loop.preparedCompanionHistoryReady(kJobA) && turns < 32) {
    TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1, kJobA)); ++turns;
  }
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady(kJobA));
  TEST_ASSERT_FALSE(loop.preparedCompanionHistoryReady(kJobB));
  TEST_ASSERT_FALSE(loop.preparedCompanionHistoryReady());
  TEST_ASSERT_NULL(loop.preparedCompanionHistory(kJobB));
  TEST_ASSERT_NOT_NULL(loop.preparedCompanionHistory(kJobA));
  TEST_ASSERT_NULL(loop.preparedCompanionHistoryIds(kJobB));
  TEST_ASSERT_NOT_NULL(loop.preparedCompanionHistoryIds(kJobA));
  // The receipt can only be consumed by the interval that produced it.
  TEST_ASSERT_FALSE(loop.commitPreparedCompanionHistoryAtomic(kJobB));
  TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic(kJobA));
  TEST_ASSERT_FALSE(loop.beginRecyclePreparedCompanionHistory(kJobB));
  TEST_ASSERT_FALSE(loop.beginRecyclePreparedCompanionHistory());
  TEST_ASSERT_TRUE(loop.beginRecyclePreparedCompanionHistory(kJobA));
  for (unsigned turn = 0; turn < 32 && !loop.preparedCompanionHistoryPrewarmed(1); ++turn) {
    TEST_ASSERT_FALSE(loop.stepRecyclePreparedCompanionHistory(1, kJobB));
    TEST_ASSERT_TRUE(loop.stepRecyclePreparedCompanionHistory(1, kJobA));
  }
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryRecycleComplete(kJobA));
  // Receipt ended: the successor's own preparation is admitted again.
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture, nullptr, kJobB));
  for (unsigned turn = 0; turn < 32 && !loop.preparedCompanionHistoryReady(kJobB); ++turn)
    TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1, kJobB));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady(kJobB));
  // ...and the previous owner no longer reaches it.
  TEST_ASSERT_FALSE(loop.stepPreparedCompanionHistory(1, kJobA));
  TEST_ASSERT_NULL(loop.preparedCompanionHistory(kJobA));
}

void test_companion_history_stages_metadata_payloads_and_ids_without_publication() {
  Loop loop; loop.nextPassId_ = 100;
  EditPass create; create.id = 7; create.actionType = EditActionType::Create;
  for (size_t i = 0; i < 3; ++i) create.addedEvents.push_back(MidiEvent::ControlChange(i, 1, 7, 80));
  loop.passes.editPasses.push_back(create);
  auto disabled = lengthRow(1, 70); disabled.id = 8; disabled.state = EditPassState::Disabled;
  loop.passes.editPasses.push_back(disabled);
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4));
  auto* canonicalRows = loop.passes.editPasses.data();
  auto* canonicalPayload = loop.passes.editPasses[0].addedEvents.data();
  const auto revision = loop.playbackRevision; const auto storageGeneration = loop.canonicalStorageGeneration;
  CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Shorten, 1, 1, 60, 90, 10, 50},
      {PendingNoteChangeKind::Hide, 2, 1, 62, 80, 20, 60}};
  auto& rows = f.runtime.frozenCaptureCompanions;
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
  TEST_ASSERT_FALSE(loop.beginPreparedCompanionHistory(rows, capture));
  size_t turns = 0;
  while (!loop.preparedCompanionHistoryReady() && turns < 32) {
    TEST_ASSERT_NULL(loop.preparedCompanionHistory());
    TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1)); ++turns;
    TEST_ASSERT_EQUAL_PTR(canonicalRows, loop.passes.editPasses.data());
    TEST_ASSERT_EQUAL_PTR(canonicalPayload, loop.passes.editPasses[0].addedEvents.data());
    TEST_ASSERT_EQUAL_UINT32(2, loop.passes.editPasses.size());
    TEST_ASSERT_EQUAL_UINT32(100, loop.nextPassId_);
    TEST_ASSERT_EQUAL_UINT32(revision, loop.playbackRevision);
    TEST_ASSERT_EQUAL_UINT64(storageGeneration, loop.canonicalStorageGeneration);
  }
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady());
  TEST_ASSERT_TRUE(turns <= Loop::requiredPreparedCompanionHistoryGrains(2, 3, 4, 1));
  const auto& prepared = *loop.preparedCompanionHistory();
  TEST_ASSERT_EQUAL_UINT32(4, prepared.size());
  TEST_ASSERT_EQUAL_UINT32(7, prepared[0].id); TEST_ASSERT_EQUAL_UINT32(3, prepared[0].addedEvents.size());
  TEST_ASSERT_NOT_EQUAL(canonicalPayload, prepared[0].addedEvents.data());
  TEST_ASSERT_EQUAL_UINT32(2, prepared[0].addedEvents[2].tick);
  TEST_ASSERT_EQUAL(EditPassState::Disabled, prepared[1].state);
  TEST_ASSERT_EQUAL_UINT32(100, prepared[2].id); TEST_ASSERT_EQUAL_UINT32(101, prepared[3].id);
  TEST_ASSERT_EQUAL_UINT8(kOverdubCompanionEditPassIndex, prepared[2].editPassIndex);
  TEST_ASSERT_EQUAL_UINT32(50, prepared[2].endTick); TEST_ASSERT_EQUAL(EditActionType::Delete, prepared[3].actionType);
  loop.discardPreparedCompanionHistory(); TEST_ASSERT_NULL(loop.preparedCompanionHistory());
  TEST_ASSERT_EQUAL_UINT32(2, loop.passes.editPasses.size());
}

void test_companion_history_atomic_swap_preserves_snapshot_and_undo_ids() {
  for (bool empty : {false, true}) {
    Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
    EditPass create; create.id = 7; create.actionType = EditActionType::Create;
    create.addedEvents.push_back(MidiEvent::ControlChange(10, 1, 7, 80));
    loop.passes.editPasses.push_back(create);
    TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::RecordStop, EditPassIdList{7}));
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(2));
    CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
    auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    PendingNoteChangeVec changes;
    if (!empty) changes.push_back({PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50});
    auto& rows = f.runtime.frozenCaptureCompanions;
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    TEST_ASSERT_FALSE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_NULL(loop.preparedCompanionHistoryIds());
    auto* oldHistory = loop.passes.editPasses.data();
    auto* oldPayload = loop.passes.editPasses[0].addedEvents.data();
    const auto revision = loop.playbackRevision; const auto generation = loop.canonicalStorageGeneration;
    const auto bound = Loop::requiredPreparedCompanionHistoryGrains(1, 1, 2, 1, 1);
    for (uint64_t i = 0; i < bound && !loop.preparedCompanionHistoryReady(); ++i) {
      TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
      TEST_ASSERT_EQUAL_UINT32(1, loop.pendingSnapshotCompanionIds()->size());
    }
    TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady());
    auto* newHistory = loop.preparedCompanionHistory()->data();
    auto* receipt = loop.preparedCompanionHistoryIds()->data();
    TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_FALSE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_FALSE(loop.stepPreparedCompanionHistory(1));
    rows.discard(); // Receipt and committed data no longer borrow the runtime owner.
    TEST_ASSERT_EQUAL_PTR(receipt, loop.preparedCompanionHistoryIds()->data());
    TEST_ASSERT_EQUAL_UINT32(empty ? 0 : 1, loop.preparedCompanionHistoryIds()->size());
    TEST_ASSERT_EQUAL_PTR(empty ? oldHistory : newHistory, loop.passes.editPasses.data());
    if (!empty) {
      TEST_ASSERT_EQUAL_PTR(oldHistory, loop.preparedCompanionHistory_.rows.data());
      TEST_ASSERT_EQUAL_PTR(oldPayload, loop.preparedCompanionHistory_.rows[0].addedEvents.data());
      TEST_ASSERT_EQUAL_UINT32(100, loop.preparedCompanionHistoryIds()->at(0));
      TEST_ASSERT_EQUAL_UINT32(100, loop.pendingSnapshotCompanionIds()->at(1));
    }
    TEST_ASSERT_EQUAL_UINT32(7, loop.pendingSnapshotCompanionIds()->at(0));
    TEST_ASSERT_EQUAL_UINT32(empty ? 1 : 2, loop.pendingSnapshotCompanionIds()->size());
    TEST_ASSERT_EQUAL_UINT32(revision + !empty, loop.playbackRevision);
    TEST_ASSERT_EQUAL_UINT64(generation + !empty, loop.canonicalStorageGeneration);
    TEST_ASSERT_EQUAL_UINT32(100 + !empty, loop.nextPassId_);
  }
}

void test_job_owned_capture_and_companions_commit_without_retiring_live_capture() {
  Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(1));
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1));
  loop.beginCapture(CapturePhase::Overdub);
  const auto liveEvent = MidiEvent::ControlChange(200, 1, 9, 99);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(liveEvent));
  Capture job; job.phase = CapturePhase::Overdub;
  const auto event = MidiEvent::ControlChange(10, 1, 7, 80);
  TEST_ASSERT_TRUE(job.store.append(event));
  TEST_ASSERT_EQUAL(SealOutcome::Ok,
      loop.stagePreparedCapturePassFromActiveAdmission(job, 100, CommitReason::OverdubStop));
  LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(event));
  staging.captureGeneration = 1; TEST_ASSERT_TRUE(staging.freeze(1));
  PendingNoteChangeVec changes;
  changes.push_back({PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50});
  auto& rows = runtime.frozenCaptureCompanions;
  TEST_ASSERT_TRUE(rows.begin(staging, 1, changes));
  while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, staging));
  while (!loop.preparedCompanionHistoryReady()) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
  TEST_ASSERT_TRUE(loop.canCommitPreparedCaptureAndCompanions());
  PreparedCapturePassTransfer transferred;
  TEST_ASSERT_TRUE(loop.commitPreparedCaptureAndCompanionsAtomic(transferred));
  TEST_ASSERT_FALSE(transferred.retiresLiveCapture);
  TEST_ASSERT_EQUAL(CapturePhase::Overdub, loop.capture.phase);
  TEST_ASSERT_EQUAL(1, loop.capture.store.size());
  TEST_ASSERT_EQUAL(200, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(1, loop.passes.editPasses.size());
}

void test_capture_and_companion_history_commit_together_or_leave_both_pending() {
  // Success with/without companion rows, then independent preflight failures.
  for (unsigned mode = 0; mode < 8; ++mode) {
    Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
    TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(1));
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1));
    loop.beginCapture(CapturePhase::Overdub);
    const auto event = MidiEvent::ControlChange(10, 1, 7, 80);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(event));
    TEST_ASSERT_EQUAL(SealOutcome::Ok,
        loop.stagePreparedCapturePassFromActiveAdmission(100, CommitReason::OverdubStop));
    auto* chunkStorage = loop.pendingCapturePass_.committedChunkIds.data();
    const auto chunkId = loop.pendingCapturePass_.committedChunkIds[0];
    LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
    auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    TEST_ASSERT_TRUE(capture.storage.append(event));
    capture.captureGeneration = 1; TEST_ASSERT_TRUE(capture.freeze(1));
    PendingNoteChangeVec changes;
    if (mode != 1) changes.push_back({PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50});
    auto& rows = runtime.frozenCaptureCompanions;
    TEST_ASSERT_TRUE(rows.begin(capture, 1, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    while (!loop.preparedCompanionHistoryReady()) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    TEST_ASSERT_TRUE(loop.canCommitPreparedCaptureAndCompanions());
    if (mode == 2) ++loop.pendingCapturePass_.id;
    if (mode == 3) rows.discard();
    if (mode == 4) TEST_ASSERT_TRUE(loop.capture.store.append(event));
    if (mode == 5) loop.capture.phase = CapturePhase::Record;
    if (mode == 6) {
      decltype(loop.passes.overdubPasses) empty;
      loop.passes.overdubPasses.swap(empty); // Remove the prewarmed transfer capacity.
    }
    if (mode == 7) ++loop.playbackRevision;
    const auto revision = loop.playbackRevision;
    const auto generation = loop.canonicalStorageGeneration;
    PreparedCapturePassTransfer transferred; transferred.id = 999;
    const bool committed = loop.commitPreparedCaptureAndCompanionsAtomic(transferred);
    TEST_ASSERT_EQUAL(mode < 2, committed);
    if (!committed) {
      TEST_ASSERT_TRUE(loop.hasPendingCapturePass_);
      TEST_ASSERT_EQUAL_PTR(chunkStorage, loop.pendingCapturePass_.committedChunkIds.data());
      TEST_ASSERT_TRUE(loop.passes.overdubPasses.empty());
      TEST_ASSERT_TRUE(loop.passes.editPasses.empty());
      TEST_ASSERT_TRUE(loop.pendingSnapshotCompanionIds()->empty());
      TEST_ASSERT_EQUAL_UINT32(999, transferred.id);
      TEST_ASSERT_EQUAL_UINT32(revision, loop.playbackRevision);
      TEST_ASSERT_EQUAL_UINT64(generation, loop.canonicalStorageGeneration);
      TEST_ASSERT_EQUAL_UINT32(101, loop.nextPassId_);
      continue;
    }
    TEST_ASSERT_FALSE(loop.hasPendingCapturePass_);
    TEST_ASSERT_FALSE(loop.canCommitPreparedCaptureAndCompanions());
    TEST_ASSERT_FALSE(loop.commitPreparedCaptureAndCompanionsAtomic(transferred));
    TEST_ASSERT_EQUAL_UINT32(100, transferred.id);
    TEST_ASSERT_EQUAL_PTR(chunkStorage, loop.passes.overdubPasses[0].committedChunkIds.data());
    TEST_ASSERT_EQUAL_UINT16(chunkId, loop.passes.overdubPasses[0].committedChunkIds[0]);
    TEST_ASSERT_EQUAL(CapturePhase::None, loop.capture.phase);
    TEST_ASSERT_TRUE(loop.capture.store.empty());
    TEST_ASSERT_EQUAL_UINT32(mode == 1 ? 0 : 1, loop.passes.editPasses.size());
    TEST_ASSERT_EQUAL_UINT32(revision + (mode == 1 ? 1 : 2), loop.playbackRevision);
    TEST_ASSERT_EQUAL_UINT64(generation + (mode != 1), loop.canonicalStorageGeneration);
    TEST_ASSERT_EQUAL_UINT32(mode == 1 ? 101 : 102, loop.nextPassId_);
    if (mode != 1) {
      TEST_ASSERT_EQUAL_UINT32(101, loop.passes.editPasses[0].id);
      TEST_ASSERT_EQUAL_UINT32(101, loop.pendingSnapshotCompanionIds()->at(0));
      TEST_ASSERT_EQUAL_UINT32(101, loop.preparedCompanionHistoryIds()->at(0));
    }
  }
}

void test_track_prepared_stop_publishes_matching_canonical_and_audible_sources() {
  for (unsigned variant = 0; variant < 11; ++variant) {
    const bool companions = variant != 0 && variant != 7;
    Loop loop; loop.loopId = 7; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
    LoopEventStore record;
    auto on = MidiEvent::NoteOn(10, 1, 60, 90); on.noteId = 1;
    const auto off = MidiEvent::NoteOff(90, 1, 60);
    TEST_ASSERT_TRUE(record.append(on)); TEST_ASSERT_TRUE(record.append(off));
    loop.passes.recordPass.id = 1;
    TEST_ASSERT_TRUE(record.detachChunksToCommittedChunkIds(loop.passes.recordPass.committedChunkIds));
    TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(1));
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1));
    uint32_t generation = 2;
    const PlaybackViewSourceIdentity base{loop.playbackRevision, generation, nullptr};
    const PlaybackViewSourceIdentity target{loop.playbackRevision + (companions ? 2u : 1u), generation, nullptr};
    RuntimeCanonicalPassInputs::CurrentSource current{
      loop.playbackRevision, generation, loop.loopLengthTicks, loop.canonicalStorageGeneration};
    LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
    const MidiEvent oracle[]{on, off};
    TEST_ASSERT_TRUE(runtime.publishStoppedCanonicalOracleView(loop.passes, base, current, oracle, 2, {0,768,768}));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepCanonicalPrefix(loop.passes, base, current, 32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    loop.beginCapture(CapturePhase::Overdub);
    const auto event = MidiEvent::ControlChange(100, 1, 7, 80);
    const auto second = MidiEvent::ControlChange(120, 1, 8, 90);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(variant == 3 ? second : event));
    if (variant != 4) TEST_ASSERT_TRUE(loop.appendCaptureEvent(variant == 3 ? event :
        (variant == 2 ? MidiEvent::ControlChange(120, 1, 8, 91) : second)));
    if (variant == 5) TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(140, 1, 9, 80)));
    auto& views = *runtime.viewSetIfPrewarmed(); auto& capture = views.frozenCaptureStaging();
    TEST_ASSERT_TRUE(capture.storage.append(event));
    TEST_ASSERT_TRUE(capture.storage.append(second));
    if (variant == 7) {
      for (size_t i = 2; i < 2046; ++i) {
        const auto extra = MidiEvent::ControlChange(120 + i, 1, 9, 80);
        TEST_ASSERT_TRUE(loop.appendCaptureEvent(extra));
        TEST_ASSERT_TRUE(capture.storage.append(extra));
      }
    }
    capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
    PendingStopAdmission stop;
    const auto canonicalCredits = Loop::requiredPreparedCompanionHistoryGrains(
        0, 0, 1, 32, 0, capture.storage.eventCount, 8) + 2; // Setup + conservative spare turn.
    const auto recycleCredits = Loop::requiredPreparedCompanionRecycleGrains(1, 0, companions ? 1 : 0, 0, 32) + 1;
    TEST_ASSERT_TRUE(stop.begin(4, 100, 0, 0, capture.storage.eventCount,
                               {1,3072,1,32}, 6240, canonicalCredits, recycleCredits));
    TEST_ASSERT_FALSE(stop.consumeHistoryRecycleGrain());
    RuntimeCanonicalPassInputs::FrozenSource frozen{
      loop.playbackRevision, generation, stop.transportCommandEpoch, loop.loopLengthTicks, capture};
    PendingNoteChangeVec changes;
    if (companions) changes.push_back({PendingNoteChangeKind::Shorten, 1, 1, 60, 90, 10, 50});
    if (!companions) changes.push_back({PendingNoteChangeKind::Add, 2, 1, 62, 90, 100, 120});
    auto provisionalTarget = target;
    if (!companions) ++provisionalTarget.playbackRevision;
    TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(frozen, changes, provisionalTarget));
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepReservedFrozenCanonicalTail(stop, 32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_TRUE(runtime.frozenCanonicalTailReady(target));
    // Verify the sealed bytes/order rather than trusting capture event count.
    TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.stagePreparedCapturePassFromActiveAdmission(100, CommitReason::OverdubStop));
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(runtime.frozenCaptureCompanions, capture,
                                                        variant == 6 ? nullptr : &runtime.canonicalPassInputs,
                                                        stop.intervalToken()));
    const auto proofBound = Loop::requiredPreparedCompanionHistoryGrains(0, 0, 1, 1, 0, capture.storage.eventCount,
        loop.pendingCapturePass_.committedChunkIds.size());
    bool valid = true;
    if (variant < 2) {
      TEST_ASSERT_TRUE(stop.consumeCanonicalPreparationGrain()); // Separate begin/setup turn.
      const auto credits = stop.remainingCanonicalPreparationGrains;
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
          TrackPreparedStopCommit::stepPreparation(loop, runtime, stop, 1));
      auto exhausted = stop; exhausted.remainingCanonicalPreparationGrains = 0;
      TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
          TrackPreparedStopCommit::stepPreparation(loop, runtime, exhausted, 32));
      TEST_ASSERT_EQUAL_UINT16(credits, stop.remainingCanonicalPreparationGrains);
    }
    for (uint64_t grain = 0; grain < proofBound && !loop.preparedCompanionHistoryReady(stop.intervalToken()) && valid; ++grain) {
      TEST_ASSERT_FALSE(loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs,
                                                              stop.intervalToken()));
      if (variant < 2) {
        const auto credits = stop.remainingCanonicalPreparationGrains;
        const auto ranked = stop.remainingRankedPreparationGrains;
        valid = TrackPreparedStopCommit::stepPreparation(loop, runtime, stop, 32) != RuntimePlaybackOrderBuildResult::Denied;
        TEST_ASSERT_EQUAL_UINT16(credits ? credits - 1 : 0, stop.remainingCanonicalPreparationGrains);
        TEST_ASSERT_EQUAL_UINT16(ranked, stop.remainingRankedPreparationGrains);
      } else valid = loop.stepPreparedCompanionHistory(1, stop.intervalToken());
      TEST_ASSERT_TRUE(loop.passes.overdubPasses.empty());
      TEST_ASSERT_TRUE(loop.passes.editPasses.empty());
    }
    if (variant >= 2 && variant <= 5) {
      TEST_ASSERT_FALSE(valid);
      TEST_ASSERT_FALSE(loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs,
                                                              stop.intervalToken()));
      TEST_ASSERT_TRUE(loop.hasPendingCapturePass());
      TEST_ASSERT_EQUAL_UINT32(base.playbackRevision, loop.playbackRevision);
      continue;
    }
    TEST_ASSERT_EQUAL(variant != 6, loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs,
                                                                        stop.intervalToken()));
    if (variant == 7) {
      TEST_ASSERT_TRUE(loop.pendingCapturePass_.committedChunkIds.size() > 1);
      TEST_ASSERT_EQUAL_UINT32(2046, loop.preparedCompanionHistory_.comparedEvents);
      continue; // Maximum full-loop horizon: two baseline events + 2046 captured.
    }
    EmissionCursor cursor{}; cursor.effectiveSourceGeneration = generation;
    TEST_ASSERT_TRUE(runtime.beginFrozenCompanionBridge(base, target, 4, 0, 768, cursor));
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepFrozenCaptureBridge(32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    while (stop.remainingOrderGrains) TEST_ASSERT_TRUE(stop.consumeOrderGrain());
    while (stop.remainingRankedPreparationGrains) TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain());
    stop.setPhase(PendingStopPhase::AtomicTransfer);
    TEST_ASSERT_FALSE(stop.canCommit(0, PendingStopDestination::Playing));
    const auto canonicalRemaining = stop.remainingCanonicalPreparationGrains;
    if (variant < 2) TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,
        TrackPreparedStopCommit::stepPreparation(loop, runtime, stop, 32));
    TEST_ASSERT_EQUAL_UINT16(canonicalRemaining, stop.remainingCanonicalPreparationGrains);
    while (stop.remainingCanonicalPreparationGrains) TEST_ASSERT_TRUE(stop.consumeCanonicalPreparationGrain());
    PreparedCapturePassTransfer transferred; transferred.id = 999;
    const auto candidate = views.bridgeLifecycle.candidate;
    // Equal numeric counters on a different owner cannot authorize this commit.
    uint32_t foreignGeneration = generation;
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, stop, foreignGeneration, cursor, transferred));
    auto noRecycle = stop; noRecycle.remainingHistoryRecycleGrains = 0;
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, noRecycle, generation, cursor, transferred));
    noRecycle = stop; noRecycle.historyRecycleQuantum = 0;
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, noRecycle, generation, cursor, transferred));
    auto outside = cursor; outside.phase = 768;
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::requiresBridgeRefresh(loop, runtime, stop, cursor));
    TEST_ASSERT_TRUE(TrackPreparedStopCommit::requiresBridgeRefresh(loop, runtime, stop, outside));
    auto stoppedRefresh = stop;
    stoppedRefresh.requestTransportStop();
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::requiresBridgeRefresh(loop, runtime, stoppedRefresh, outside));
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, stop, generation, outside, transferred));
    runtime.ledger.noteOn(1, 60, 1, 10, 90);
    TEST_ASSERT_EQUAL(companions, TrackPreparedStopCommit::requiresBridgeRefresh(loop, runtime, stop, cursor));
    if (companions)
      TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, stop, generation, cursor, transferred));
    runtime.ledger.clear();
    auto stopped = stop; stopped.requestTransportStop();
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::commit(loop, runtime, stopped, generation, cursor, transferred));
    const auto savedTarget = runtime.frozenCaptureSourceAdmission.targetSource;
    ++runtime.frozenCaptureSourceAdmission.targetSource.playbackRevision;
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, stop, generation, cursor, transferred));
    runtime.frozenCaptureSourceAdmission.targetSource = savedTarget;
    TEST_ASSERT_EQUAL_UINT32(999, transferred.id);
    TEST_ASSERT_EQUAL_UINT32(base.playbackRevision, loop.playbackRevision);
    TEST_ASSERT_TRUE(loop.hasPendingCapturePass());
    TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge, views.bridgeLifecycle.selected);
    if (variant == 6) {
      TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, stop, generation, cursor, transferred));
      TEST_ASSERT_TRUE(loop.hasPendingCapturePass());
      TEST_ASSERT_EQUAL_UINT32(base.playbackRevision, loop.playbackRevision);
      TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge, views.bridgeLifecycle.selected);
      continue;
    }
    GlobalUndoStack undoStack;
    TrackUndoPublication undoPublication;
    bool retiredRootReleased = false;
    if (variant == 10) {
      // Transport Stop can publish prepared history without any audible base.
      views.views[views.lifecycle.current].published = false;
      stop.requestTransportStop();
      TEST_ASSERT_FALSE(TrackPreparedStopCommit::commitStopped(loop, runtime, stop,
          generation, transferred)); // Frozen command still has the old epoch.
      TEST_ASSERT_TRUE(TrackPreparedStopCommit::retargetStoppedRecovery(runtime, stop));
      TEST_ASSERT_TRUE(TrackPreparedStopCommit::commitStopped(loop, runtime, stop,
          generation, transferred));
      TEST_ASSERT_FALSE(stop.permitsPlaybackEmission());
    } else if (variant >= 8) {
      undoStack.entries.reserve(2);
      UndoEntry old; old.id = 1;
      old.beforePublishedSnapshot = ImmutableLoopSnapshotRef(new LoopSnapshot(),
          [&retiredRootReleased](const LoopSnapshot* root) { delete root; retiredRootReleased = true; });
      undoStack.entries.push_back(std::move(old)); undoStack.nextEntryId = 2;
      PassIdList ids; ids.reserve(1);
      EditPassIdList companionIds; companionIds.reserve(1);
      Loop::SessionUndoCollection collection;
      TEST_ASSERT_TRUE(loop.beginCollectOverdubStopUndoPasses(collection, ids, companionIds,
          loop.pendingCapturePass().id, true, stop.intervalToken()));
      while (!loop.collectedOverdubSessionUndoPassesCurrent(collection))
        TEST_ASSERT_NOT_EQUAL(Loop::SessionUndoCollection::Phase::Denied,
            loop.stepCollectOverdubSessionUndoPasses(collection, 1));
      UndoEntry prepared;
      TEST_ASSERT_TRUE(TrackPreparedUndo::prepareOverdubSessionEntry(loop, collection, 0, {}, prepared));
      TEST_ASSERT_TRUE(undoPublication.prewarm(1));
      TEST_ASSERT_TRUE(undoPublication.begin(undoStack, prepared));
      while (!undoPublication.stepPrepare(1)) {}
      runtime.ledger.noteOn(1, 60, 1, 10, 90);
      TEST_ASSERT_FALSE(TrackPreparedStopCommit::commitWithUndo(loop, runtime, stop,
          generation, cursor, transferred, undoStack, undoPublication));
      TEST_ASSERT_FALSE(retiredRootReleased);
      TEST_ASSERT_FALSE(undoPublication.published());
      TEST_ASSERT_EQUAL(2u, undoStack.nextEntryId);
      TEST_ASSERT_TRUE(loop.hasPendingCapturePass());
      runtime.ledger.clear();
      if (variant == 9) {
        TEST_ASSERT_TRUE(stop.permitsPlaybackEmission());
        TEST_ASSERT_FALSE(TrackPreparedStopCommit::retargetStoppedRecovery(runtime, stop));
        stop.requestTransportStop();
        TEST_ASSERT_FALSE(stop.permitsPlaybackEmission());
        TEST_ASSERT_FALSE(runtime.canonicalPassInputs.ready());
        const auto payloadGeneration = runtime.canonicalPassInputs.generation();
        auto foreignStop = stop;
        TEST_ASSERT_FALSE(TrackPreparedStopCommit::retargetStoppedRecovery(runtime, foreignStop));
        ++loop.canonicalStorageGeneration;
        TEST_ASSERT_FALSE(TrackPreparedStopCommit::retargetStoppedRecovery(runtime, stop));
        --loop.canonicalStorageGeneration;
        TEST_ASSERT_TRUE(TrackPreparedStopCommit::retargetStoppedRecovery(runtime, stop));
        TEST_ASSERT_EQUAL(payloadGeneration, runtime.canonicalPassInputs.generation());
        TEST_ASSERT_TRUE(loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs,
                                                             stop.intervalToken()));
        TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4, 0, 768, cursor));
        TEST_ASSERT_FALSE(TrackPreparedStopCommit::commitWithUndo(loop, runtime, stop,
            generation, cursor, transferred, undoStack, undoPublication));
        TEST_ASSERT_TRUE(TrackPreparedStopCommit::commitStoppedWithUndo(loop, runtime, stop,
            generation, transferred, undoStack, undoPublication));
        TEST_ASSERT_EQUAL(RuntimePlaybackBridgeLifecycle::kNoBridge, views.bridgeLifecycle.selected);
        TEST_ASSERT_FALSE(stop.permitsPlaybackEmission());
      } else {
        TEST_ASSERT_TRUE(TrackPreparedStopCommit::commitWithUndo(loop, runtime, stop,
            generation, cursor, transferred, undoStack, undoPublication));
      }
      TEST_ASSERT_TRUE(undoPublication.published());
      TEST_ASSERT_EQUAL(2u, undoPublication.committedId());
      TEST_ASSERT_EQUAL(2u, undoStack.entries.back().id);
      TEST_ASSERT_EQUAL(100u, undoStack.entries.back().passId);
      TEST_ASSERT_FALSE(retiredRootReleased);
      TEST_ASSERT_TRUE(undoPublication.defer(TrackUndoPublication::Command::Undo, 0, loop.loopId));
      stop.requestTransportStop();
      runtime.ledger.noteOn(1, 61, 2, 20, 90);
      while (!undoPublication.stepReleaseRedo(1)) {}
      TEST_ASSERT_TRUE(retiredRootReleased); // Only after the joint commit was successful.
      TEST_ASSERT_TRUE(undoPublication.beginTrimAfterReclaim());
      while (!undoPublication.stepTrim(1)) {}
      TEST_ASSERT_TRUE(undoPublication.finish());
      TEST_ASSERT_EQUAL(TrackUndoPublication::Command::Undo, undoPublication.takePendingCommand().kind);
    } else {
      TEST_ASSERT_TRUE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, stop, generation, cursor, transferred));
    }
    TEST_ASSERT_EQUAL_UINT32(100, transferred.id);
    TEST_ASSERT_EQUAL_UINT32(target.playbackRevision, loop.playbackRevision);
    TEST_ASSERT_EQUAL_PTR(target.snapshotIdentity, loop.publishedSnapshotIdentity());
    TEST_ASSERT_EQUAL_UINT8(variant >= 9 ? RuntimePlaybackBridgeLifecycle::kNoBridge : candidate, views.bridgeLifecycle.selected);
    TEST_ASSERT_EQUAL(PendingStopPhase::DeferredCleanup, stop.phase);
    TEST_ASSERT_EQUAL(variant < 9, runtime.selectedFullLoopBridge(target, 768) != nullptr);
    TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4, 0, 768, cursor)); // Old owners invalidated.
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::advanceAtomicTransfer(loop, runtime, stop, generation, cursor, transferred));
    TEST_ASSERT_TRUE(runtime.canonicalPassInputs.captureAttached());
    TEST_ASSERT_EQUAL_UINT16(recycleCredits, stop.remainingHistoryRecycleGrains);
    TEST_ASSERT_EQUAL_UINT32(companions ? 1 : 0,
        loop.preparedCompanionHistoryIds(stop.intervalToken())->size());
    TEST_ASSERT_EQUAL_PTR(&loop, stop.historyRecycleOwner);
    TEST_ASSERT_FALSE(stop.historyRecycleStarted);
    Loop foreignLoop;
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::beginHistoryRecycle(foreignLoop, stop));
    auto wrongGeneration = stop; ++wrongGeneration.historyRecycleGeneration;
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::beginHistoryRecycle(loop, wrongGeneration));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, 32)); // Receipt still in use.
    TEST_ASSERT_EQUAL_UINT16(recycleCredits, stop.remainingHistoryRecycleGrains);
    TEST_ASSERT_NOT_NULL(loop.preparedCompanionHistoryIds(stop.intervalToken()));
    TEST_ASSERT_TRUE(TrackPreparedStopCommit::beginHistoryRecycle(loop, stop));
    TEST_ASSERT_NULL(loop.preparedCompanionHistoryIds(stop.intervalToken()));
    TEST_ASSERT_FALSE(TrackPreparedStopCommit::beginHistoryRecycle(loop, stop));
    const auto recycleRemaining = stop.remainingHistoryRecycleGrains;
    for (size_t wrongQ : {size_t(0), size_t(1), size_t(33)})
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
          TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, wrongQ));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
        TrackPreparedStopCommit::stepHistoryRecycle(foreignLoop, stop, 32));
    auto exhaustedRecycle = stop; exhaustedRecycle.remainingHistoryRecycleGrains = 0;
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, exhaustedRecycle, 32));
    auto wrongPhase = stop; wrongPhase.setPhase(PendingStopPhase::AtomicTransfer);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, wrongPhase, 32));
    TEST_ASSERT_EQUAL_UINT16(recycleRemaining, stop.remainingHistoryRecycleGrains);
    stop.requestTransportStop(); // Post-commit cleanup survives a new transport command.
    while (!loop.preparedCompanionHistoryPrewarmed() && stop.remainingHistoryRecycleGrains) {
      const auto remaining = stop.remainingHistoryRecycleGrains;
      TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
          TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, stop.historyRecycleQuantum));
      TEST_ASSERT_EQUAL_UINT16(remaining - 1, stop.remainingHistoryRecycleGrains);
    }
    TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed());
    // Worker completion may leave unused estimates; no padding turns are owed.
    TEST_ASSERT_TRUE(stop.consumeHistoryRecycleGrain());
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, 32)); // Free completed query.
    TEST_ASSERT_EQUAL_UINT32(target.playbackRevision, loop.playbackRevision);
    TEST_ASSERT_EQUAL(variant < 9, runtime.selectedFullLoopBridge(target, 768) != nullptr);
    ++loop.playbackRevision;
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, 32)); // Completion cannot outlive source.
    --loop.playbackRevision;
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, 32)); // Same source, replaced preparation.
    // Production cleanup must retire the attached tail, not just the bridge
    // compiler. Otherwise every prefix turn of the next nonempty stop denies.
    loop.openOverdubSession(100);
    TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
    stop.historyRecycleOwner = nullptr;
    const size_t retainedNormalizedPasses = runtime.frozenCompanionPasses.normalizedPassCount();
    TEST_ASSERT_TRUE(TrackPreparedStopCommit::completeDeferredCleanup(loop, runtime, stop, stopCleanupPreview, true));
    TEST_ASSERT_EQUAL_UINT32(retainedNormalizedPasses, runtime.canonicalPassInputs.completedPassCount());
    TEST_ASSERT_FALSE(runtime.canonicalPassInputs.captureAttached());
    TEST_ASSERT_EQUAL(variant < 9, runtime.selectedFullLoopBridge(target, 768) != nullptr);
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepCanonicalPrefix(loop.passes, target,
          {loop.playbackRevision, generation, loop.loopLengthTicks, loop.canonicalStorageGeneration}, 32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    // Compare adopted per-pass ordering and edits with full canonical replay.
    LoopPlaybackRuntime fresh; TEST_ASSERT_TRUE(fresh.prewarmViews());
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = fresh.stepCanonicalPrefix(loop.passes, target,
          {loop.playbackRevision, generation, loop.loopLengthTicks, loop.canonicalStorageGeneration}, 32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT32(fresh.canonicalPassInputs.passCount(), runtime.canonicalPassInputs.passCount());
    for (size_t p = 0; p < fresh.canonicalPassInputs.passCount(); ++p) {
      const auto& expected = fresh.canonicalPassInputs.data()[p];
      const auto& adopted = runtime.canonicalPassInputs.data()[p];
      TEST_ASSERT_EQUAL_UINT32(expected.count, adopted.count);
      for (size_t e = 0; e < expected.count; ++e) {
        TEST_ASSERT_EQUAL_UINT32(expected.events[e].tick, adopted.events[e].tick);
        TEST_ASSERT_EQUAL_UINT32(expected.events[e].noteId, adopted.events[e].noteId);
        TEST_ASSERT_EQUAL(expected.events[e].type, adopted.events[e].type);
        TEST_ASSERT_EQUAL_UINT8(expected.events[e].channel, adopted.events[e].channel);
        TEST_ASSERT_EQUAL_UINT8(expected.events[e].data.noteData.note, adopted.events[e].data.noteData.note);
        TEST_ASSERT_EQUAL_UINT8(expected.events[e].data.noteData.velocity, adopted.events[e].data.noteData.velocity);
      }
    }
    TEST_ASSERT_NULL(stop.historyRecycleOwner);
    TEST_ASSERT_FALSE(stop.historyRecycleStarted);
    TEST_ASSERT_EQUAL_UINT64(0, stop.historyRecycleGeneration);
  }
}

void test_bounded_stop_undo_collection_appends_final_receipt_with_legacy_semantics() {
  using Phase = Loop::SessionUndoCollection::Phase;
  for (PassId finalId : {PassId(0), PassId(1), PassId(2), PassId(3)}) {
    for (bool include : {false, true}) {
      Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
      loop.openOverdubSession(0);
      loop.pushOverdubSessionPass(1, {99,100}); loop.pushOverdubSessionPass(2, {100});
      TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1));
      CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
      auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
      auto& rows = f.runtime.frozenCaptureCompanions;
      PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50}};
      TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
      while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
      TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
      while (!loop.preparedCompanionHistoryReady()) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
      PassIdList expectedPasses, passIds; EditPassIdList expectedCompanions, companions;
      loop.collectOverdubSessionUndoPasses(expectedPasses, expectedCompanions);
      if (include && finalId != kInvalidPassId && (expectedPasses.empty() || expectedPasses.back() != finalId))
        expectedPasses.push_back(finalId);
      for (auto id : *loop.preparedCompanionHistoryIds()) expectedCompanions.push_back(id);
      passIds.reserve(3); companions.reserve(4);
      const auto* passStorage = passIds.data(); const auto* companionStorage = companions.data();
      Loop::SessionUndoCollection work;
      TEST_ASSERT_TRUE(loop.beginCollectOverdubStopUndoPasses(work, passIds, companions, finalId, include));
      const auto bound = Loop::requiredSessionStopUndoCollectionGrains(2, 3, 1, 1);
      uint64_t grains = 0;
      while (work.phase != Phase::Complete && grains < bound) {
        const auto before = passIds.size() + companions.size();
        TEST_ASSERT_NOT_EQUAL(Phase::Denied, loop.stepCollectOverdubSessionUndoPasses(work, 1));
        TEST_ASSERT_TRUE(passIds.size() + companions.size() - before <= 1);
        TEST_ASSERT_EQUAL_PTR(passStorage, passIds.data()); TEST_ASSERT_EQUAL_PTR(companionStorage, companions.data());
        ++grains;
      }
      TEST_ASSERT_EQUAL_UINT64(bound, grains);
      TEST_ASSERT_TRUE(loop.collectedOverdubSessionUndoPassesCurrent(work));
      TEST_ASSERT_TRUE(expectedPasses == passIds); TEST_ASSERT_TRUE(expectedCompanions == companions);
      TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic()); // Ready receipt becomes committed, unchanged.
      TEST_ASSERT_TRUE(loop.collectedOverdubSessionUndoPassesCurrent(work));
      UndoEntry entry;
      TEST_ASSERT_TRUE(TrackPreparedUndo::prepareOverdubSessionEntry(loop, work, 0, {}, entry));
      TEST_ASSERT_TRUE(expectedPasses == entry.passIds);
      TEST_ASSERT_TRUE(expectedCompanions == entry.editPassIds);
      TEST_ASSERT_NOT_NULL(loop.preparedCompanionHistoryIds());
      TEST_ASSERT_TRUE(loop.beginRecyclePreparedCompanionHistory());
      TEST_ASSERT_FALSE(loop.collectedOverdubSessionUndoPassesCurrent(work));
      TEST_ASSERT_EQUAL(Phase::Denied, loop.stepCollectOverdubSessionUndoPasses(work, 1));
      TEST_ASSERT_TRUE(expectedCompanions == entry.editPassIds); // Private UndoEntry owns the retired receipt IDs.
    }
  }
}

void test_bounded_stop_undo_collection_denies_missing_recycled_and_undersized_receipts() {
  using Phase = Loop::SessionUndoCollection::Phase;
  for (unsigned mode = 0; mode < 5; ++mode) {
    Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1));
    PassIdList passIds; EditPassIdList companions;
    passIds.reserve(mode == 2 ? 0 : 1); companions.reserve(mode == 3 ? 0 : 1);
    Loop::SessionUndoCollection work;
    TEST_ASSERT_FALSE(loop.beginCollectOverdubStopUndoPasses(work, passIds, companions, 7, true));
    CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
    auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    auto& rows = f.runtime.frozenCaptureCompanions;
    PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50}};
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    while (!loop.preparedCompanionHistoryReady()) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_TRUE(loop.beginCollectOverdubStopUndoPasses(work, passIds, companions, 7, true));
    TEST_ASSERT_EQUAL(Phase::FinalPass, loop.stepCollectOverdubSessionUndoPasses(work, 1));
    if (mode == 0) TEST_ASSERT_TRUE(loop.beginRecyclePreparedCompanionHistory());
    if (mode == 1) { loop.discardPreparedCompanionHistory(); TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1)); }
    if (mode == 4) ++loop.playbackRevision;
    TEST_ASSERT_EQUAL(Phase::Denied, loop.stepCollectOverdubSessionUndoPasses(work, 32));
    TEST_ASSERT_FALSE(loop.collectedOverdubSessionUndoPassesCurrent(work));
    TEST_ASSERT_EQUAL_UINT32(mode == 3 ? 1 : 0, passIds.size());
    TEST_ASSERT_TRUE(companions.empty());
    TEST_ASSERT_EQUAL_UINT32(mode == 3 ? 0 : 1, companions.capacity());
  }
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Loop::requiredSessionStopUndoCollectionGrains(0, 2048, 1, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Loop::requiredSessionStopUndoCollectionGrains(2049, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Loop::requiredSessionStopUndoCollectionGrains(0, 0, 0, 0));
}

void test_prepared_undo_entry_consumes_collected_ids_without_growth_or_snapshot_release() {
  using Phase = Loop::SessionUndoCollection::Phase;
  for (size_t count : {size_t(0), size_t(1), size_t(3)}) {
    for (bool retainSnapshot : {false, true}) {
      Loop loop; loop.loopId = 42;
      for (size_t i = 0; i < count; ++i)
        loop.pushOverdubSessionPass(i + 1, count > 1 ? EditPassIdList{100,100} : EditPassIdList{});
      PassIdList passIds; EditPassIdList companionIds;
      passIds.reserve(count); companionIds.reserve(count * 2);
      Loop::SessionUndoCollection collection;
      TEST_ASSERT_TRUE(loop.beginCollectOverdubSessionUndoPasses(collection, passIds, companionIds));
      while (collection.phase != Phase::Complete)
        TEST_ASSERT_NOT_EQUAL(Phase::Denied, loop.stepCollectOverdubSessionUndoPasses(collection, 1));
      const auto* passStorage = passIds.data(); const auto* companionStorage = companionIds.data();
      UndoEntry entry; entry.passIds.reserve(2); entry.editPassIds.reserve(2);
      const auto* retiredPassStorage = entry.passIds.data(); const auto* retiredCompanionStorage = entry.editPassIds.data();
      ImmutableLoopSnapshotRef before;
      if (retainSnapshot) before = std::make_shared<LoopSnapshot>();
      const auto references = before.use_count();
      TEST_ASSERT_EQUAL(count != 0, TrackPreparedUndo::prepareOverdubSessionEntry(loop, collection, 1, before, entry));
      if (!count) {
        TEST_ASSERT_EQUAL(Phase::Complete, collection.phase);
        TEST_ASSERT_EQUAL_PTR(retiredPassStorage, entry.passIds.data());
        TEST_ASSERT_EQUAL(references, before.use_count());
        continue;
      }
      TEST_ASSERT_EQUAL(UndoEntryKind::OverdubPassAdded, entry.kind);
      TEST_ASSERT_EQUAL_UINT8(1, entry.slotIndex); TEST_ASSERT_EQUAL_UINT32(42, entry.loopId);
      TEST_ASSERT_EQUAL_UINT32(count, entry.passId); TEST_ASSERT_EQUAL_UINT32(0, entry.id);
      TEST_ASSERT_EQUAL(EditPassType::Note, entry.editPassType);
      TEST_ASSERT_EQUAL_UINT8(kOverdubCompanionEditPassIndex, entry.editPassIndex);
      TEST_ASSERT_EQUAL_PTR(before.get(), entry.beforePublishedSnapshot.get());
      TEST_ASSERT_FALSE(entry.afterPublishedSnapshot);
      TEST_ASSERT_EQUAL(references + (retainSnapshot ? 1 : 0), before.use_count());
      TEST_ASSERT_EQUAL_UINT32(count > 1 ? count : 0, entry.passIds.size());
      TEST_ASSERT_EQUAL_UINT32(count > 1 ? count * 2 : 0, entry.editPassIds.size());
      if (count > 1) {
        TEST_ASSERT_EQUAL_PTR(passStorage, entry.passIds.data());
        TEST_ASSERT_EQUAL_PTR(retiredPassStorage, passIds.data());
        for (size_t i = 0; i < count; ++i) TEST_ASSERT_EQUAL_UINT32(i + 1, entry.passIds[i]);
        for (auto id : entry.editPassIds) TEST_ASSERT_EQUAL_UINT32(100, id);
      } else {
        TEST_ASSERT_EQUAL_PTR(retiredPassStorage, entry.passIds.data());
        TEST_ASSERT_EQUAL_PTR(passStorage, passIds.data()); // Single pass remains scalar in UndoEntry.
      }
      TEST_ASSERT_EQUAL_PTR(companionStorage, entry.editPassIds.data());
      TEST_ASSERT_EQUAL_PTR(retiredCompanionStorage, companionIds.data());
      TEST_ASSERT_FALSE(loop.collectedOverdubSessionUndoPassesCurrent(collection));
      UndoEntry duplicate;
      TEST_ASSERT_FALSE(TrackPreparedUndo::prepareOverdubSessionEntry(loop, collection, 1, before, duplicate));
      TEST_ASSERT_EQUAL(references + (retainSnapshot ? 1 : 0), before.use_count());
    }
  }
}

void test_prepared_undo_entry_denials_leave_private_outputs_untouched() {
  for (unsigned mode = 0; mode < 7; ++mode) {
    Loop loop; loop.pushOverdubSessionPass(1, {100});
    PassIdList passIds; passIds.reserve(1); EditPassIdList companionIds; companionIds.reserve(1);
    Loop::SessionUndoCollection collection;
    TEST_ASSERT_TRUE(loop.beginCollectOverdubSessionUndoPasses(collection, passIds, companionIds));
    while (collection.phase != Loop::SessionUndoCollection::Phase::Complete)
      (void)loop.stepCollectOverdubSessionUndoPasses(collection, 32);
    UndoEntry entry;
    if (mode == 0) loop.pushOverdubSessionPass(2, {});
    if (mode == 1) entry.id = 99;
    if (mode == 2) entry.passId = 99; // An unpublished single-pass entry is occupied too.
    if (mode == 3) entry.editPassIds.push_back(77);
    if (mode == 4) entry.beforePublishedSnapshot = std::make_shared<LoopSnapshot>();
    if (mode == 5) entry.afterPublishedSnapshot = std::make_shared<LoopSnapshot>();
    const auto* passStorage = passIds.data(); const auto* companionStorage = companionIds.data();
    const auto* retainedBefore = entry.beforePublishedSnapshot.get();
    const auto* retainedAfter = entry.afterPublishedSnapshot.get();
    TEST_ASSERT_FALSE(TrackPreparedUndo::prepareOverdubSessionEntry(loop, collection,
        mode == 6 ? Config::MAX_LOOPS_PER_TRACK : 0, {}, entry));
    TEST_ASSERT_EQUAL_UINT32(1, passIds.size()); TEST_ASSERT_EQUAL_UINT32(1, companionIds.size());
    TEST_ASSERT_EQUAL_PTR(passStorage, passIds.data()); TEST_ASSERT_EQUAL_PTR(companionStorage, companionIds.data());
    TEST_ASSERT_EQUAL_PTR(retainedBefore, entry.beforePublishedSnapshot.get());
    TEST_ASSERT_EQUAL_PTR(retainedAfter, entry.afterPublishedSnapshot.get());
    TEST_ASSERT_EQUAL_UINT32(mode == 1 ? 99 : 0, entry.id);
    TEST_ASSERT_EQUAL_UINT32(mode == 2 ? 99 : 0, entry.passId);
  }
}

void test_stop_recycle_credits_check_total_and_cannot_run_before_commit() {
  const StopAdmissionContract contract{1,3072,1,32};
  for (uint64_t invalid : {UINT64_MAX}) {
    PendingStopAdmission denied;
    TEST_ASSERT_FALSE(denied.begin(4, 100, 0, 0, 2, contract, 6240, 7, invalid));
    TEST_ASSERT_FALSE(denied.pending);
    TEST_ASSERT_EQUAL_UINT16(0, denied.historyRecycleQuantum);
    TEST_ASSERT_EQUAL_UINT16(0, denied.remainingHistoryRecycleGrains);
  }
  const uint64_t longService = uint64_t(UINT16_MAX) + 1234;
  PendingStopAdmission stop;
  TEST_ASSERT_TRUE(stop.begin(4, 100, 0, 1, 2, contract, 2, 3, longService));
  TEST_ASSERT_EQUAL_UINT64(longService, stop.remainingHistoryRecycleGrains);
  TEST_ASSERT_EQUAL_UINT16(32, stop.historyRecycleQuantum);
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  TEST_ASSERT_FALSE(stop.consumeHistoryRecycleGrain()); // Phase alone is insufficient.
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  while (stop.remainingOrderGrains) TEST_ASSERT_TRUE(stop.consumeOrderGrain());
  while (stop.remainingRankedPreparationGrains) TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain());
  while (stop.remainingCanonicalPreparationGrains) TEST_ASSERT_TRUE(stop.consumeCanonicalPreparationGrain());
  stop.setPhase(PendingStopPhase::AtomicTransfer);
  TEST_ASSERT_FALSE(stop.consumeHistoryRecycleGrain());
  TEST_ASSERT_TRUE(stop.canCommit(0, PendingStopDestination::Playing)); // Cleanup credits do not deadlock commit.
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  TEST_ASSERT_TRUE(stop.consumeHistoryRecycleGrain());
  stop.requestTransportStop();
  TEST_ASSERT_EQUAL_UINT64(longService - 1, stop.remainingHistoryRecycleGrains);
  TEST_ASSERT_TRUE(stop.consumeHistoryRecycleGrain());
  stop.remainingHistoryRecycleGrains = 0;
  TEST_ASSERT_TRUE(stop.consumeHistoryRecycleGrain());
  stop.reset();
  TEST_ASSERT_EQUAL_UINT16(0, stop.remainingHistoryRecycleGrains);
  TEST_ASSERT_EQUAL_UINT16(0, stop.historyRecycleQuantum);
  TEST_ASSERT_FALSE(stop.consumeHistoryRecycleGrain());
  TEST_ASSERT_TRUE(stop.begin(5, 200, 1, 0, 0, contract));
  TEST_ASSERT_EQUAL_UINT16(0, stop.historyRecycleQuantum); // Legacy caller opts out.
}

void test_stop_canonical_credits_are_separate_checked_and_preserved_on_transport_stop() {
  const StopAdmissionContract contract{1,3072,1,32};
  for (uint64_t invalid : {UINT64_MAX}) {
    PendingStopAdmission denied;
    TEST_ASSERT_FALSE(denied.begin(1, 100, 0, 0, 1, contract, 6240, invalid));
    TEST_ASSERT_FALSE(denied.pending);
    TEST_ASSERT_EQUAL_UINT16(0, denied.canonicalPreparationQuantum);
    TEST_ASSERT_EQUAL_UINT16(0, denied.remainingCanonicalPreparationGrains);
  }
  const uint64_t longService = uint64_t(UINT16_MAX) + 4321;
  PendingStopAdmission stop;
  TEST_ASSERT_TRUE(stop.begin(1, 100, 0, 1, 2, contract, 2, longService));
  TEST_ASSERT_EQUAL_UINT64(longService, stop.remainingCanonicalPreparationGrains);
  TEST_ASSERT_EQUAL_UINT16(32, stop.canonicalPreparationQuantum);
  TEST_ASSERT_FALSE(stop.consumeCanonicalPreparationGrain());
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  TEST_ASSERT_TRUE(stop.consumeCanonicalPreparationGrain());
  TEST_ASSERT_EQUAL_UINT64(2, stop.remainingRankedPreparationGrains);
  stop.requestTransportStop();
  TEST_ASSERT_EQUAL_UINT64(longService - 1, stop.remainingCanonicalPreparationGrains);
  TEST_ASSERT_FALSE(stop.canCommit(stop.transportCommandEpoch, PendingStopDestination::Playing));
  while (stop.remainingOrderGrains) TEST_ASSERT_TRUE(stop.consumeOrderGrain());
  while (stop.remainingRankedPreparationGrains) TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain());
  TEST_ASSERT_FALSE(stop.canCommit(stop.transportCommandEpoch, PendingStopDestination::Stopped));
  while (stop.remainingCanonicalPreparationGrains) TEST_ASSERT_TRUE(stop.consumeCanonicalPreparationGrain());
  TEST_ASSERT_TRUE(stop.canCommit(stop.transportCommandEpoch, PendingStopDestination::Stopped));
  stop.reset();
  TEST_ASSERT_EQUAL_UINT16(0, stop.canonicalPreparationQuantum);
  TEST_ASSERT_EQUAL_UINT16(0, stop.remainingCanonicalPreparationGrains);
}

void test_companion_history_recycles_both_buffers_without_growth_until_horizon_exhaustion() {
  Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
  EditPass create; create.id = 7; create.actionType = EditActionType::Create;
  for (size_t i = 0; i < 3; ++i) create.addedEvents.push_back(MidiEvent::ControlChange(i, 1, 7, 80));
  loop.passes.editPasses.push_back(create);
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(3));
  TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::RecordStop, EditPassIdList{7}));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(2));
  TEST_ASSERT_FALSE(loop.preparedCompanionHistoryPrewarmed(3));
  CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  auto& rows = f.runtime.frozenCaptureCompanions;
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50}};
  for (unsigned pass = 0; pass < 2; ++pass) {
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    while (!loop.preparedCompanionHistoryReady()) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_EQUAL_UINT32(100 + pass, loop.preparedCompanionHistoryIds()->at(0));
    auto& work = loop.preparedCompanionHistory_;
    auto* retiredRows = work.rows.data(); auto* retiredPayload = work.rows[0].addedEvents.data();
    auto* canonicalRows = loop.passes.editPasses.data();
    auto* canonicalPayload = loop.passes.editPasses[0].addedEvents.data();
    auto* retiredSnapshot = work.snapshotIds.data(); auto* receipt = work.companionIds.data();
    const auto revision = loop.playbackRevision; const auto generation = loop.canonicalStorageGeneration;
    const auto bound = Loop::requiredPreparedCompanionRecycleGrains(4, 3, 1, pass + 1, 1);
    TEST_ASSERT_TRUE(loop.beginRecyclePreparedCompanionHistory()); // Receipt consumed above.
    TEST_ASSERT_NULL(loop.preparedCompanionHistoryIds());
    TEST_ASSERT_FALSE(loop.beginRecyclePreparedCompanionHistory());
    TEST_ASSERT_FALSE(loop.stepRecyclePreparedCompanionHistory(0));
    for (uint64_t i = 0; i < bound && !loop.preparedCompanionHistoryPrewarmed(); ++i) {
      TEST_ASSERT_TRUE(loop.stepRecyclePreparedCompanionHistory(1));
      TEST_ASSERT_FALSE(loop.stepPreparedCompanionHistory(1)); // Never mix the two continuations.
      TEST_ASSERT_EQUAL_PTR(retiredRows, work.rows.data());
      TEST_ASSERT_EQUAL_PTR(retiredPayload, work.rows[0].addedEvents.data());
      TEST_ASSERT_EQUAL_PTR(retiredSnapshot, work.snapshotIds.data());
      TEST_ASSERT_EQUAL_PTR(receipt, work.companionIds.data());
      TEST_ASSERT_EQUAL_PTR(canonicalRows, loop.passes.editPasses.data());
      TEST_ASSERT_EQUAL_PTR(canonicalPayload, loop.passes.editPasses[0].addedEvents.data());
      TEST_ASSERT_EQUAL_UINT32(revision, loop.playbackRevision);
      TEST_ASSERT_EQUAL_UINT64(generation, loop.canonicalStorageGeneration);
    }
    TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed());
    TEST_ASSERT_EQUAL_UINT32(3, work.rows.size());
    TEST_ASSERT_TRUE(work.rows[0].addedEvents.empty());
    TEST_ASSERT_TRUE(work.snapshotIds.empty()); TEST_ASSERT_TRUE(work.companionIds.empty());
    TEST_ASSERT_EQUAL_UINT32(3, loop.passes.editPasses[0].addedEvents.size());
    TEST_ASSERT_EQUAL_UINT32(pass + 2, loop.pendingSnapshotCompanionIds()->size());
    TEST_ASSERT_EQUAL(pass == 0, loop.preparedCompanionHistoryPrewarmed(1));
    rows.discard();
  }
  TEST_ASSERT_EQUAL_UINT32(3, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT32(100, loop.passes.editPasses[1].id);
  TEST_ASSERT_EQUAL_UINT32(101, loop.passes.editPasses[2].id);
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_FALSE(loop.beginPreparedCompanionHistory(rows, capture));
  TEST_ASSERT_EQUAL_UINT32(102, loop.nextPassId_);
}

void test_companion_history_recycle_denies_source_changes_without_touching_canonical_rows() {
  for (unsigned stale = 0; stale < 4; ++stale) {
    Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(2));
    CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
    auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    auto& rows = f.runtime.frozenCaptureCompanions;
    PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50}};
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    PendingStopAdmission stop;
    TEST_ASSERT_TRUE(stop.begin(4, 100, 0, 0, 0, {1,3072,1,1}, 0, 0, 16));
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture, nullptr, stop.intervalToken()));
    while (!loop.preparedCompanionHistoryReady(stop.intervalToken()))
      TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1, stop.intervalToken()));
    TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic(stop.intervalToken()));
    auto* canonical = loop.passes.editPasses.data();
    while (stop.remainingOrderGrains) TEST_ASSERT_TRUE(stop.consumeOrderGrain());
    // Isolate the post-commit state; the joint fixture verifies this binding's publication.
    stop.historyRecycleOwner = &loop;
    stop.historyRecycleGeneration = loop.preparedCompanionHistoryGeneration();
    stop.setPhase(PendingStopPhase::DeferredCleanup);
    TEST_ASSERT_TRUE(TrackPreparedStopCommit::beginHistoryRecycle(loop, stop));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, 1));
    if (stale == 0) ++loop.playbackRevision;
    if (stale == 1) ++loop.canonicalStorageGeneration;
    if (stale == 2) ++loop.nextPassId_;
    if (stale == 3) (void)loop.queueLoadedSnapshotPublish(); // Clears/replaces the mailbox.
    const auto credits = stop.remainingHistoryRecycleGrains;
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
        TrackPreparedStopCommit::stepHistoryRecycle(loop, stop, 1));
    TEST_ASSERT_EQUAL_UINT16(credits - 1, stop.remainingHistoryRecycleGrains);
    TEST_ASSERT_FALSE(loop.preparedCompanionHistoryPrewarmed());
    TEST_ASSERT_FALSE(loop.beginPreparedCompanionHistory(rows, capture));
    TEST_ASSERT_EQUAL_PTR(canonical, loop.passes.editPasses.data());
    TEST_ASSERT_EQUAL_UINT32(1, loop.passes.editPasses.size());
    TEST_ASSERT_EQUAL_UINT32(100, loop.passes.editPasses[0].id);
  }
}

void test_companion_history_snapshot_changes_deny_publication() {
  for (bool replace : {false, true}) {
    Loop loop; loop.loopLengthTicks = 768; loop.nextPassId_ = 100;
    TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::RecordStop, EditPassIdList{7}));
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(2));
    CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
    auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50}};
    auto& rows = f.runtime.frozenCaptureCompanions;
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    while (!loop.preparedCompanionHistoryReady()) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    if (replace) TEST_ASSERT_FALSE(loop.queueLoadedSnapshotPublish()); // Empty canonical loop clears the box.
    TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::RecordStop, EditPassIdList{8}));
    TEST_ASSERT_FALSE(loop.commitPreparedCompanionHistoryAtomic());
    TEST_ASSERT_NULL(loop.preparedCompanionHistoryIds());
    TEST_ASSERT_TRUE(loop.passes.editPasses.empty());
    TEST_ASSERT_EQUAL_UINT32(100, loop.nextPassId_);
  }
}

void test_companion_history_max_payload_and_empty_companions_fit_grain_bound() {
  Loop loop; EditPass row; row.actionType = EditActionType::Create;
  row.addedEvents.resize(2048, MidiEvent::ControlChange(5, 1, 7, 80));
  loop.passes.editPasses.push_back(row);
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(2));
  CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  PendingNoteChangeVec changes; auto& rows = f.runtime.frozenCaptureCompanions;
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, rows.step(32));
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
  const auto bound = Loop::requiredPreparedCompanionHistoryGrains(1, 2048, 2, 32);
  for (uint64_t i = 0; i < bound && !loop.preparedCompanionHistoryReady(); ++i)
    TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(32));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady());
  TEST_ASSERT_EQUAL_UINT32(1, loop.preparedCompanionHistory()->size());
  TEST_ASSERT_EQUAL_UINT32(2048, loop.preparedCompanionHistory()->at(0).addedEvents.size());
  TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic());
  auto* retainedPayload = loop.preparedCompanionHistory_.rows[0].addedEvents.data();
  TEST_ASSERT_TRUE(loop.beginRecyclePreparedCompanionHistory());
  const auto recycleBound = Loop::requiredPreparedCompanionRecycleGrains(3, 2048, 0, 0, 32);
  for (uint64_t i = 0; i < recycleBound && !loop.preparedCompanionHistoryPrewarmed(); ++i)
    TEST_ASSERT_TRUE(loop.stepRecyclePreparedCompanionHistory(32));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(2));
  TEST_ASSERT_EQUAL_PTR(retainedPayload, loop.preparedCompanionHistory_.rows[0].addedEvents.data());
  TEST_ASSERT_TRUE(loop.preparedCompanionHistory_.rows[0].addedEvents.empty());
  TEST_ASSERT_EQUAL_UINT32(2048, loop.passes.editPasses[0].addedEvents.size());
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Loop::requiredPreparedCompanionHistoryGrains(2049, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Loop::requiredPreparedCompanionHistoryGrains(1, 2049, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Loop::requiredPreparedCompanionHistoryGrains(2048, 0, 1, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Loop::requiredPreparedCompanionHistoryGrains(1, 1, 1, 0));
  loop.passes.editPasses[0].addedEvents.push_back(MidiEvent::ControlChange(6, 1, 7, 80));
  TEST_ASSERT_FALSE(loop.prewarmPreparedCompanionHistory(2));
}

void test_companion_history_denies_stale_sources_capacity_and_id_overflow() {
  for (unsigned stale = 0; stale < 4; ++stale) {
    Loop loop; loop.nextPassId_ = 10; loop.passes.editPasses.push_back(lengthRow(1, 70));
    TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1));
    CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
    auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50}};
    auto& rows = f.runtime.frozenCaptureCompanions;
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
    TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
    if (stale == 0) ++loop.canonicalStorageGeneration;
    if (stale == 1) ++loop.playbackRevision;
    if (stale == 2) ++loop.nextPassId_;
    if (stale == 3) rows.discard();
    TEST_ASSERT_FALSE(loop.stepPreparedCompanionHistory(1));
    TEST_ASSERT_NULL(loop.preparedCompanionHistory());
    TEST_ASSERT_EQUAL_UINT32(1, loop.passes.editPasses.size());
  }
  Loop loop;
  TEST_ASSERT_FALSE(loop.prewarmPreparedCompanionHistory(2049));
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(0));
  CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50}};
  auto& rows = f.runtime.frozenCaptureCompanions;
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  while (rows.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_FALSE(loop.beginPreparedCompanionHistory(rows, capture));
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(1)); loop.nextPassId_ = UINT32_MAX;
  TEST_ASSERT_FALSE(loop.beginPreparedCompanionHistory(rows, capture));
  loop.nextPassId_ = 10; ++loop.canonicalStorageGeneration;
  // Unborrowed reserved storage binds at begin; in-flight versions are checked above.
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
}

void test_stopped_canonical_construction_matches_oracle_and_releases_workers() {
  for (bool history : {false, true}) {
    CanonicalPrefixFixture f; auto& runtime = f.runtime;
    if (history) f.passes.editPasses.push_back(lengthRow(1, 70));
    auto source = f.source();
    auto on = MidiEvent::NoteOn(10, 1, 60, 90); on.noteId = 1;
    MidiEvent oracle[] = {on, MidiEvent::NoteOff(history ? 70 : 90, 1, 60)};
    TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(oracle, 2, source, {0,768,768}));
    auto& set = *runtime.viewSetIfPrewarmed(); auto& current = set.views[set.lifecycle.current];
    oracle[0].data.noteData.velocity = 80;
    TEST_ASSERT_FALSE(runtime.publishStoppedCanonicalOracleView(f.passes, source, f.live(), oracle, 2, {0,768,768}));
    TEST_ASSERT_TRUE(current.published); TEST_ASSERT_FALSE(current.passOriginsReady);
    TEST_ASSERT_EQUAL_UINT8(90, current.storage.events[0].data.noteData.velocity);
    TEST_ASSERT_TRUE(runtime.canonicalPassInputs.idle()); TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
    TEST_ASSERT_TRUE(runtime.frozenCompanionApply.available());
    oracle[0].data.noteData.velocity = 90;
    TEST_ASSERT_TRUE(runtime.publishStoppedCanonicalOracleView(f.passes, source, f.live(), oracle, 2, {0,768,768}));
    TEST_ASSERT_TRUE(current.passOriginsReady); TEST_ASSERT_TRUE(current.passOriginSource == source);
    TEST_ASSERT_EQUAL_UINT32(history ? 70 : 90, current.storage.events[1].tick);
    TEST_ASSERT_EQUAL_UINT16(0, current.passOrigins[1].pass); TEST_ASSERT_EQUAL_UINT16(1, current.passOrigins[1].event);
    TEST_ASSERT_TRUE(runtime.canonicalPassInputs.idle()); TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle());
    TEST_ASSERT_TRUE(runtime.frozenCompanionApply.available());
    TEST_ASSERT_FALSE(runtime.publishStoppedCanonicalOracleView(f.passes, source, f.live(), oracle, 2, {0,384,768}));
    ++f.revision;
    TEST_ASSERT_FALSE(runtime.publishStoppedCanonicalOracleView(f.passes, source, f.live(), oracle, 2, {0,768,768}));
    TEST_ASSERT_TRUE(current.published); TEST_ASSERT_TRUE(current.passOriginsReady);
  }
}

void test_stopped_canonical_construction_preserves_owned_prefix_and_handles_empty() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime; auto source = f.source();
  const auto generation = runtime.canonicalPassInputs.generation();
  TEST_ASSERT_FALSE(runtime.publishStoppedCanonicalOracleView(f.passes, source, f.live(), nullptr, 0, {0,768,768}));
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.ready());
  TEST_ASSERT_EQUAL_UINT64(generation, runtime.canonicalPassInputs.generation());
  LoopPlaybackRuntime empty; TEST_ASSERT_TRUE(empty.prewarmViews()); LoopPasses passes;
  TEST_ASSERT_TRUE(empty.publishStoppedCanonicalOracleView(passes, source, f.live(), nullptr, 0, {0,768,768}));
  const auto* current = empty.publishedFullLoopView(source, f.length);
  TEST_ASSERT_NOT_NULL(current); TEST_ASSERT_TRUE(current->passOriginsReady);
  TEST_ASSERT_EQUAL_UINT32(0, current->storage.eventCount);
}

void test_canonical_window_build_retains_provenance_and_bounds_filtered_work() {
  for (auto coverage : {PlaybackCoverageInterval{192,192,768},
                         PlaybackCoverageInterval{650,200,768},
                         PlaybackCoverageInterval{450,20,768}}) {
    CanonicalPrefixFixture f; auto& runtime = f.runtime;
    MidiEventVec events;
    for (uint32_t tick : {10u,100u,200u,300u,400u,700u})
      events.push_back(MidiEvent::ControlChange(tick, 1, 7, 80));
    RuntimeCompanionPasses::Pass pass{events.data(), events.size()};
    const auto source = f.source();
    RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
    auto& baseline = runtime.frozenCompanionPasses;
    TEST_ASSERT_TRUE(baseline.begin(runtime.frozenCompanionApply, work, &pass, 1, nullptr, 0, source, 768));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = baseline.step(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    RuntimePlaybackView view;
    TEST_ASSERT_TRUE(view.prewarm(2)); // Whole source exceeds this window's capacity.
    RuntimePlaybackViewBuild build;
    TEST_ASSERT_TRUE(build.begin(view, baseline, source, coverage, {}, {}));
    for (size_t index = 0; index < events.size(); ++index) {
      TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, build.nextGrain());
      const auto before = view.storage.eventCount;
      build.stepFill(1);
      TEST_ASSERT_EQUAL_UINT32(before + (coverage.contains(events[index].tick) ? 1 : 0),
                              view.storage.eventCount);
    }
    TEST_ASSERT_TRUE(view.sourceFilled);
    TEST_ASSERT_TRUE(view.passOriginsReady);
    TEST_ASSERT_TRUE(view.canonicalRanksReady);
    TEST_ASSERT_FALSE(view.published);
    baseline.discard(); // The window owns every copied origin after fill.
    for (size_t turn = 0; turn < 100 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++turn)
      build.stepOrder(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
    TEST_ASSERT_TRUE(view.passOriginSource == source);
    for (size_t index = 0; index < view.storage.eventCount; ++index) {
      TEST_ASSERT_EQUAL_UINT16(0, view.passOrigins[index].pass);
      TEST_ASSERT_EQUAL_UINT16(view.passOrigins[index].event, view.canonicalRanks[index]);
      TEST_ASSERT_EQUAL_UINT32(events[view.passOrigins[index].event].tick, view.storage.events[index].tick);
      TEST_ASSERT_TRUE(coverage.contains(view.storage.events[index].tick));
    }
  }
}

void test_typed_stopped_oracle_owns_empty_and_capacity_provenance() {
  for (size_t count : {size_t(0), size_t(2048)}) {
    CanonicalPrefixFixture f; auto& runtime = f.runtime;
    std::vector<MidiEvent> events;
    for (size_t i = 0; i < count; ++i) events.push_back(MidiEvent::ControlChange(i % 768, 1, 7, 80));
    RuntimeCompanionPasses::Pass pass{events.data(), events.size()};
    auto source = f.source();
    RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
    auto& baseline = runtime.frozenCompanionPasses;
    TEST_ASSERT_TRUE(baseline.begin(runtime.frozenCompanionApply, work, &pass, 1, nullptr, 0, source, f.length));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = baseline.step(32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    auto& set = *runtime.viewSetIfPrewarmed();
    auto& current = set.views[set.lifecycle.current];
    auto* eventStorage = current.storage.events; auto* origins = current.passOrigins;
    TEST_ASSERT_TRUE(runtime.publishStoppedOracleView(baseline, source, {0,768,768}));
    baseline.discard(); events.clear();
    TEST_ASSERT_TRUE(current.published); TEST_ASSERT_TRUE(current.complete);
    TEST_ASSERT_TRUE(current.passOriginsReady); TEST_ASSERT_TRUE(current.passOriginSource == source);
    TEST_ASSERT_EQUAL_UINT32(count, current.storage.eventCount);
    TEST_ASSERT_EQUAL_PTR(eventStorage, current.storage.events);
    TEST_ASSERT_EQUAL_PTR(origins, current.passOrigins);
    if (count) {
      TEST_ASSERT_EQUAL_UINT16(0, current.passOrigins[count - 1].pass);
      TEST_ASSERT_EQUAL_UINT16(count - 1, current.passOrigins[count - 1].event);
      TEST_ASSERT_EQUAL_UINT32((count - 1) % 768, current.storage.events[count - 1].tick);
    }
    TEST_ASSERT_NOT_NULL(runtime.publishedFullLoopView(source, f.length));
    TEST_ASSERT_FALSE(runtime.publishStoppedOracleView(baseline, source, {0,768,768}));
    TEST_ASSERT_TRUE(current.published); TEST_ASSERT_TRUE(current.passOriginsReady);
  }
}

void test_reserved_tail_denies_invalid_identity_and_continues_past_estimate() {
  CanonicalPrefixFixture f; f.finish(); auto& runtime = f.runtime;
  prepareUnsortedFrozenTailCapture(runtime); finishRuntimeCanonicalCaptureOrder(runtime);
  auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  PendingNoteChangeVec changes;
  RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
  TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, {2, 3, &f.passes}));
  PendingStopAdmission stop;
  const StopAdmissionContract contract{1, 3072, 1, 1};
  TEST_ASSERT_TRUE(stop.begin(4, 100, f.epoch, 1, 4, contract, 1));
  const auto phase = runtime.canonicalTailPhase;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepReservedFrozenCanonicalTail(stop, 1));
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepReservedFrozenCanonicalTail(stop, 0));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepReservedFrozenCanonicalTail(stop, 2));
  stop.frozenGeneration = 5;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepReservedFrozenCanonicalTail(stop, 1));
  stop.frozenGeneration = 4;
  stop.requestTransportStop();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepReservedFrozenCanonicalTail(stop, 1));
  TEST_ASSERT_EQUAL_UINT16(1, stop.remainingRankedPreparationGrains);
  TEST_ASSERT_EQUAL(phase, runtime.canonicalTailPhase);
  stop.reset(); TEST_ASSERT_TRUE(stop.begin(4, 100, f.epoch, 0, 4, contract, 1));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepReservedFrozenCanonicalTail(stop, 1));
  TEST_ASSERT_EQUAL_UINT16(0, stop.remainingRankedPreparationGrains);
  TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepReservedFrozenCanonicalTail(stop, 1));
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.borrowsCaptureOrder()); // Only one of three indexes copied.
  TEST_ASSERT_TRUE(capture.frozen); TEST_ASSERT_EQUAL_UINT32(4, capture.storage.eventCount);
}

void test_frozen_canonical_tail_denies_source_changes_and_preserves_reused_workers() {
  for (unsigned changed = 0; changed < 4; ++changed) {
    CanonicalPrefixFixture f; f.finish();
    auto& runtime = f.runtime;
    auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
    PendingNoteChangeVec changes;
    RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
    PlaybackViewSourceIdentity target{2, 3, &f.passes};
    TEST_ASSERT_TRUE(runtime.beginFrozenCanonicalTail(live, changes, target));
    uint64_t reused = 0;
    if (changed == 0) ++f.storageGeneration;
    if (changed == 1) changes.push_back({PendingNoteChangeKind::Shorten, 1, 1, 60, 90, 10, 50});
    if (changed == 2) {
      runtime.frozenCaptureCompanions.discard();
      TEST_ASSERT_TRUE(runtime.frozenCaptureCompanions.begin(capture, 4, changes));
      reused = runtime.frozenCaptureCompanions.preparationGeneration();
    }
    if (changed == 3) {
      runtime.canonicalPassInputs.discard();
      RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
        runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
      TEST_ASSERT_TRUE(runtime.canonicalPassInputs.beginCurrent(runtime.frozenCompanionApply, work,
                                                               f.passes, runtime.canonicalPrefixSource, f.live()));
      reused = runtime.canonicalPassInputs.generation();
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCanonicalTail(1));
    TEST_ASSERT_FALSE(runtime.frozenCanonicalTailReady(target));
    runtime.discardFrozenCanonicalTail();
    if (changed == 2) TEST_ASSERT_EQUAL_UINT64(reused, runtime.frozenCaptureCompanions.preparationGeneration());
    if (changed == 3) TEST_ASSERT_EQUAL_UINT64(reused, runtime.canonicalPassInputs.generation());
    TEST_ASSERT_TRUE(capture.frozen);
  }
}

void test_canonical_prefix_global_turn_resumes_two_runtime_owners_without_stacking() {
  CanonicalPrefixFixture tracks[2];
  uint8_t cursor = 0;
  unsigned children = 0;
  const auto step = [&](uint8_t index) {
    if (!tracks[index].needsService()) return false;
    ++children;
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, tracks[index].step());
    return true;
  };
  using namespace MidiServiceDrain;
  TEST_ASSERT_TRUE(serviceCanonicalPrefixTurn(DeferredIdleOwner::VisualCache, 2, cursor,
                                              [](uint8_t) { return false; }, step));
  TEST_ASSERT_EQUAL_UINT32(1, children); TEST_ASSERT_EQUAL_UINT8(1, cursor);
  TEST_ASSERT_FALSE(tracks[0].runtime.canonicalPassInputs.idle());
  TEST_ASSERT_TRUE(tracks[1].runtime.canonicalPassInputs.idle());
  const auto firstGeneration = tracks[0].runtime.canonicalPassInputs.generation();
  TEST_ASSERT_FALSE(serviceCanonicalPrefixTurn(DeferredIdleOwner::VisualCache, 2, cursor,
                                               [](uint8_t i) { return i == 0; }, step));
  TEST_ASSERT_EQUAL_UINT32(1, children); TEST_ASSERT_EQUAL_UINT8(1, cursor);
  TEST_ASSERT_EQUAL_UINT64(firstGeneration, tracks[0].runtime.canonicalPassInputs.generation());
  TEST_ASSERT_TRUE(serviceCanonicalPrefixTurn(DeferredIdleOwner::VisualCache, 2, cursor,
                                              [](uint8_t) { return false; }, step));
  TEST_ASSERT_EQUAL_UINT32(2, children); TEST_ASSERT_EQUAL_UINT8(0, cursor);
  for (size_t turn = 0; turn < 1000; ++turn) {
    const auto before = children;
    const bool ran = serviceCanonicalPrefixTurn(DeferredIdleOwner::VisualCache, 2, cursor,
                                                 [](uint8_t) { return false; }, step);
    TEST_ASSERT_EQUAL_UINT32(ran ? 1 : 0, children - before);
    if (!ran) break;
  }
  for (auto& track : tracks) {
    TEST_ASSERT_TRUE(track.runtime.canonicalPassInputs.ready());
    TEST_ASSERT_EQUAL_UINT32(2, track.runtime.canonicalPassInputs.eventCount());
    TEST_ASSERT_FALSE(track.needsService());
  }
}

void test_canonical_prefix_live_changes_hide_results_and_restart_on_separate_turn() {
  for (unsigned field = 0; field < 3; ++field) {
    CanonicalPrefixFixture f;
    TEST_ASSERT_TRUE(f.needsService());
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
      f.runtime.stepCanonicalPrefix(f.passes, f.source(), f.live(), 0));
    TEST_ASSERT_TRUE(f.runtime.canonicalPassInputs.idle());
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, f.step());
    TEST_ASSERT_NULL(f.runtime.canonicalPassInputs.data()); // Setup does no input work.
    f.finish();
    if (field == 0) ++f.revision;
    else if (field == 1) ++f.generation;
    else ++f.length;
    TEST_ASSERT_NULL(f.runtime.canonicalPassInputs.data()); // Before any owner service.
    TEST_ASSERT_TRUE(f.needsService());
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, f.step());
    // DEC-177: a **revision-only** move is window identity, not history identity -- the committed history is unchanged,
    // so the build resumes it (inputs stay busy) instead of discarding and replaying it. Generation and loop-length
    // moves are history/geometry identity and still restart on the cleanup turn.
    if (field == 0) {
      TEST_ASSERT_FALSE_MESSAGE(f.runtime.canonicalPassInputs.idle(),
                                "a revision-only move must resume the certified history, not restart it");
    } else {
      TEST_ASSERT_TRUE(f.runtime.canonicalPassInputs.idle()); // Cleanup only.
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, f.step());
    TEST_ASSERT_FALSE(f.runtime.canonicalPassInputs.idle()); // Setup on a later turn.
    f.finish();
    f.runtime.reset(false);
    TEST_ASSERT_TRUE(f.runtime.canonicalPassInputs.idle());
    TEST_ASSERT_TRUE(f.needsService());
  }
}

void test_canonical_prefix_denial_latches_until_source_changes() {
  CanonicalPrefixFixture f;
  f.passes.editPasses.resize(RuntimePlaybackViewConfig::kRecordsPerBridge + 1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, f.step());
  const auto deniedGeneration = f.runtime.canonicalPassInputs.generation();
  TEST_ASSERT_FALSE(f.needsService());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, f.step());
  TEST_ASSERT_EQUAL_UINT64(deniedGeneration, f.runtime.canonicalPassInputs.generation());
  ++f.revision; f.passes.editPasses.clear();
  TEST_ASSERT_TRUE(f.needsService());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, f.step());
  TEST_ASSERT_TRUE(f.runtime.canonicalPassInputs.idle());
  f.finish();
}

void test_canonical_prefix_does_not_cancel_reused_normalizer() {
  CanonicalPrefixFixture f;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, f.step());
  auto& inputs = f.runtime.canonicalPassInputs;
  inputs.discard();
  RuntimeCompanionApply::Work work{f.runtime.frozenCompanionPairLookup, f.runtime.frozenCompanionDeleteLookup,
    f.runtime.frozenCompanionSpans, f.runtime.frozenCompanionReconstruction, f.runtime.frozenCompanionOverlaps};
  auto source = f.source();
  TEST_ASSERT_TRUE(inputs.beginCurrent(f.runtime.frozenCompanionApply, work, f.passes, source, f.live()));
  const auto reusedGeneration = inputs.generation();
  TEST_ASSERT_FALSE(f.needsService());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, f.step());
  TEST_ASSERT_EQUAL_UINT64(reusedGeneration, inputs.generation());
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = inputs.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  inputs.discard();
}

void test_canonical_prefix_handoff_preserves_frozen_tail_owner() {
  CanonicalPrefixFixture f; f.finish();
  auto& inputs = f.runtime.canonicalPassInputs;
  const auto* prefix = inputs.data()[0].events;
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  capture.captureGeneration = 4;
  TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(100, 1, 7, 80)));
  TEST_ASSERT_TRUE(capture.freeze(4));
  RuntimeCanonicalPassInputs::FrozenSource live{f.revision, f.generation, f.epoch, f.length, capture};
  TEST_ASSERT_TRUE(inputs.appendFrozenOverdub(live));
  const auto tailGeneration = inputs.generation();
  TEST_ASSERT_FALSE(f.needsService()); // Refuse even before the tail becomes ready.
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, f.step());
  TEST_ASSERT_EQUAL_UINT64(tailGeneration, inputs.generation());
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = inputs.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_PTR(prefix, inputs.data()[0].events);
  TEST_ASSERT_EQUAL_UINT32(3, inputs.eventCount());
  TEST_ASSERT_FALSE(f.needsService());
  ++f.revision;
  TEST_ASSERT_NULL(inputs.data());
}

void test_canonical_prefix_rejects_timeline_reset_with_reused_audible_identity() {
  for (bool complete : {false, true}) {
    Loop loop; loop.loopLengthTicks = 768;
    uint32_t generation = 2;
    LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
    loop.passes.recordPass.id = 1;
    LoopEventStore record;
    TEST_ASSERT_TRUE(record.append(MidiEvent::ControlChange(10, 1, 7, 80)));
    TEST_ASSERT_TRUE(record.detachChunksToCommittedChunkIds(loop.passes.recordPass.committedChunkIds));
    PlaybackViewSourceIdentity source{loop.playbackRevision, generation, nullptr};
    RuntimeCanonicalPassInputs::CurrentSource live{loop.playbackRevision, generation,
      loop.loopLengthTicks, loop.canonicalStorageGeneration};
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepCanonicalPrefix(loop.passes, source, live, 1));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepCanonicalPrefix(loop.passes, source, live, 1));
    if (complete) {
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
        result = runtime.stepCanonicalPrefix(loop.passes, source, live, 1);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    }
    const auto before = loop.canonicalStorageGeneration;
    loop.resetPassTimeline(); // Frees the borrowed chunks and restarts revision at zero.
    TEST_ASSERT_EQUAL_UINT32(source.playbackRevision, loop.playbackRevision);
    TEST_ASSERT_TRUE(loop.canonicalStorageGeneration > before);
    TEST_ASSERT_NULL(runtime.canonicalPassInputs.data());
    TEST_ASSERT_TRUE(runtime.canonicalPrefixNeedsService(loop.passes, source, loop.loopLengthTicks,
                                                       loop.canonicalStorageGeneration));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepCanonicalPrefix(loop.passes, source, live, 1));
    TEST_ASSERT_TRUE(runtime.canonicalPassInputs.idle());
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepCanonicalPrefix(loop.passes, source, live, 1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT32(0, runtime.canonicalPassInputs.eventCount());
    runtime.reset(false);
  }
}

void test_canonical_prefix_rejects_history_relocation_without_audible_revision_change() {
  Loop loop; loop.loopLengthTicks = 768;
  uint32_t generation = 2, epoch = 3;
  LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
  loop.passes.recordPass.id = 1;
  LoopEventStore record;
  auto on = MidiEvent::NoteOn(10, 1, 60, 90); on.noteId = 1;
  TEST_ASSERT_TRUE(record.append(on)); TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(90, 1, 60)));
  TEST_ASSERT_TRUE(record.detachChunksToCommittedChunkIds(loop.passes.recordPass.committedChunkIds));
  EditPass disabled; disabled.id = 2; disabled.state = EditPassState::Disabled;
  loop.passes.editPasses.push_back(disabled);
  auto length = lengthRow(1, 70); length.id = 3; loop.passes.editPasses.push_back(length);
  PlaybackViewSourceIdentity source{loop.playbackRevision, generation, nullptr};
  RuntimeCanonicalPassInputs::CurrentSource live{loop.playbackRevision, generation,
    loop.loopLengthTicks, loop.canonicalStorageGeneration};
  const auto finish = [&]() {
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
      result = runtime.stepCanonicalPrefix(loop.passes, source, live, 1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  };
  finish();
  TEST_ASSERT_EQUAL_UINT32(70, runtime.canonicalPassInputs.data()[0].events[1].tick);
  const auto before = loop.canonicalStorageGeneration;
  SlotPassReferences refs;
  TEST_ASSERT_EQUAL_UINT32(1, loop.reclaimUnreferencedDisabledEditPasses(refs));
  TEST_ASSERT_TRUE(loop.canonicalStorageGeneration > before);
  TEST_ASSERT_EQUAL_UINT32(source.playbackRevision, loop.playbackRevision);
  TEST_ASSERT_NULL(runtime.canonicalPassInputs.data());
  TEST_ASSERT_TRUE(runtime.canonicalPrefixNeedsService(loop.passes, source, loop.loopLengthTicks,
                                                     loop.canonicalStorageGeneration));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepCanonicalPrefix(loop.passes, source, live, 1));
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.idle());
  finish();
  TEST_ASSERT_EQUAL_UINT32(70, runtime.canonicalPassInputs.data()[0].events[1].tick);
  const auto unchanged = loop.canonicalStorageGeneration;
  TEST_ASSERT_EQUAL_UINT32(0, loop.reclaimUnreferencedDisabledEditPasses(refs));
  TEST_ASSERT_EQUAL_UINT64(unchanged, loop.canonicalStorageGeneration);
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.ready());
  // Deferred publication can append history without advancing audible revision.
  auto deferred = lengthRow(1, 60);
  TEST_ASSERT_TRUE(loop.saveNoteEditPass(0, deferred, EditPassType::Note, true, true) != kInvalidEditPassId);
  TEST_ASSERT_EQUAL_UINT32(source.playbackRevision, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.canonicalStorageGeneration > unchanged);
  TEST_ASSERT_NULL(runtime.canonicalPassInputs.data());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepCanonicalPrefix(loop.passes, source, live, 1));
  finish();
  TEST_ASSERT_EQUAL_UINT32(60, runtime.canonicalPassInputs.data()[0].events[1].tick);
  // The same guard survives transfer from prefix work to frozen-tail work.
  auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
  RuntimeCanonicalPassInputs::FrozenSource frozen{loop.playbackRevision, generation, epoch, loop.loopLengthTicks, capture};
  TEST_ASSERT_TRUE(runtime.canonicalPassInputs.appendFrozenOverdub(frozen));
  loop.resetPassTimeline();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.canonicalPassInputs.step(1));
  runtime.reset(false);
}

struct PerPassBridgeFixture {
  LoopPlaybackRuntime runtime;
  RuntimePlaybackView base;
  FixedPlaybackBridgeStorage bridge;
  std::vector<MidiEventVec> inputs;
  std::vector<RuntimeCompanionPasses::Pass> descriptors;
  std::vector<RuntimeCompanionPasses::Origin> baseOrigins;
  std::vector<MidiEvent> expected;
  PlaybackViewSourceIdentity preparedSource{2, 3, nullptr};
  PlaybackViewSourceIdentity baselineSource{1, 1, nullptr};
  void prepare(std::vector<MidiEventVec> sourceInputs, const EditPassVec& rows, size_t basePassCount, PlaybackCoverageInterval coverage = {0,768,768}) {
    inputs = std::move(sourceInputs);
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    MidiEventVec reference;
    for (size_t pass = 0; pass < inputs.size(); ++pass) {
      descriptors.push_back({inputs[pass].data(), inputs[pass].size()});
      auto layer = inputs[pass]; applyNoteEditPassSequence(layer, rows, 768);
      MidiEventVec merged;
      std::merge(reference.begin(), reference.end(), layer.begin(), layer.end(), std::back_inserter(merged),
                 [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
      reference = std::move(merged);
    }
    for (const auto& event : reference)
      if (coverage.contains(event.tick % coverage.loopLengthTicks)) expected.push_back(event);
    RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
    auto& preparation = runtime.frozenCompanionPasses;
    TEST_ASSERT_TRUE(preparation.begin(runtime.frozenCompanionApply, work, descriptors.data(), basePassCount,
                                        nullptr, 0, baselineSource, 768));
    auto baselineResult = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turn = 0; turn < 200000 && baselineResult == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      baselineResult = preparation.step(32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, baselineResult);
    TEST_ASSERT_TRUE(base.prewarm(2048));
    RuntimePlaybackViewFill fill;
    TEST_ASSERT_TRUE(fill.begin(base, preparation, baselineSource, coverage, {}, {},
                                RuntimePlaybackViewFillScope::PlaybackCoverage));
    auto fillResult = RuntimePlaybackViewFillResult::Pending;
    for (size_t turn = 0; turn < 4096 && fillResult == RuntimePlaybackViewFillResult::Pending; ++turn)
      fillResult = fill.step(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Complete, fillResult);
    TEST_ASSERT_TRUE(base.passOriginsReady); TEST_ASSERT_TRUE(base.passOriginSource == base.source);
    baseOrigins.assign(base.passOrigins, base.passOrigins+base.storage.eventCount);
    preparation.discard(); // View provenance must survive reuse of the working buffers.
    RuntimePlaybackOrderBuilder order; TEST_ASSERT_TRUE(order.begin(base, 768));
    while (order.step(32) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(base.complete); base.published = true;
    TEST_ASSERT_TRUE(bridge.prewarm(2048)); bridge.coverage = base.coverage; bridge.source = {2, 3, nullptr};
    TEST_ASSERT_TRUE(preparation.begin(runtime.frozenCompanionApply, work, descriptors.data(), descriptors.size(),
                                        rows.data(), rows.size(), preparedSource, 768));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turn = 0; turn < 2000000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = preparation.step(32);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  }
  void mapAndCompile(size_t quantum) {
    auto& map = runtime.frozenCompanionBridgeMap;
    auto& preparation = runtime.frozenCompanionPasses;
    TEST_ASSERT_TRUE(map.begin(preparation, base, preparedSource));
    const size_t b = base.storage.eventCount, a = preparation.size();
    const auto mapBound = RuntimeCompanionBridgeMap::requiredPerPassGrainsUpperBound(b, a, quantum);
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turn = 0; turn < mapBound && result == RuntimePlaybackOrderBuildResult::Pending; ++turn) {
      TEST_ASSERT_NULL(map.records());
      result = map.step(quantum);
      TEST_ASSERT_TRUE(map.lastStepInputCount() <= quantum);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    RuntimePlaybackBridgeBuild build;
    TEST_ASSERT_TRUE(build.begin(base, bridge, &map));
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turn = 0; turn < RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(b, map.size(), quantum) &&
         result == RuntimePlaybackOrderBuildResult::Pending; ++turn) result = build.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    map.discard(); preparation.discard(); baseOrigins.clear();
    bridge.selected = true;
    assertBridgeEqualsOracle(base, bridge, expected);
  }
};

void test_companion_bridge_maps_canonical_windows_without_full_loop_base() {
  for (auto coverage : {PlaybackCoverageInterval{192,192,768},
                         PlaybackCoverageInterval{650,200,768},
                         PlaybackCoverageInterval{450,20,768}}) {
    for (size_t quantum : {size_t(1),size_t(32)}) {
      PerPassBridgeFixture f;
      MidiEventVec base{MidiEvent::NoteOn(10,1,60,90), MidiEvent::NoteOff(100,1,60),
          MidiEvent::ControlChange(200,1,7,20), MidiEvent::ControlChange(700,1,7,70)};
      base[0].noteId = 1;
      MidiEventVec tail{MidiEvent::ControlChange(200,2,7,21), MidiEvent::ControlChange(300,2,7,30)};
      f.prepare({base,tail}, {}, 1, coverage);
      TEST_ASSERT_TRUE(f.base.coverage.lengthTicks < f.base.coverage.loopLengthTicks);
      f.mapAndCompile(quantum);
    }
  }
}


void test_canonical_window_bridge_denies_changed_coverage_and_external_closure() {
  for (bool mutateCoverage : {false,true}) {
    PerPassBridgeFixture f;
    f.prepare({{MidiEvent::ControlChange(200,1,7,20), MidiEvent::ControlChange(300,1,7,30)}},
              {}, 1, {192,192,768});
    auto& mapping = f.runtime.frozenCompanionBridgeMap;
    TEST_ASSERT_TRUE(mapping.begin(f.runtime.frozenCompanionPasses, f.base, f.preparedSource));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, mapping.step(1));
    if (mutateCoverage) {
      ++f.base.coverage.startPhase;
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, mapping.step(1));
      TEST_ASSERT_NULL(mapping.records());
    } else {
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t turn = 0; turn < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
        result = mapping.step(1);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      f.runtime.ledger.noteOn(1,60,1,10,90);
      f.bridge.ledgerBoundary.capture(f.runtime.ledger);
      RuntimePlaybackBridgeBuild build;
      TEST_ASSERT_FALSE(build.begin(f.base, f.bridge, &mapping));
      TEST_ASSERT_FALSE(f.bridge.complete); TEST_ASSERT_FALSE(f.bridge.selected);
    }
  }
}


void test_frozen_canonical_pipeline_bridge_matches_materialized_reference() {
  for (size_t sample = 0; sample < 25; ++sample) {
    const bool openLedger = sample == 24;
    LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
    LoopPasses passes; passes.recordPass.id = 1;
    auto on = MidiEvent::NoteOn(openLedger ? 700 : 10, 1, 60, 90); on.noteId = 1;
    auto duplicate = MidiEvent::NoteOn(80, 1, 62, 80); duplicate.noteId = 1;
    LoopEventStore record;
    auto originalOff = MidiEvent::NoteOff(openLedger ? 100 : 50, 1, 60);
    if (openLedger) originalOff.noteId = 1;
    TEST_ASSERT_TRUE(record.append(on)); TEST_ASSERT_TRUE(record.append(originalOff));
    if (!openLedger) { TEST_ASSERT_TRUE(record.append(duplicate)); TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(120, 1, 62))); }
    TEST_ASSERT_TRUE(record.detachChunksToCommittedChunkIds(passes.recordPass.committedChunkIds));
    if (!openLedger && sample % 3 == 0) {
      EditPass deletion; deletion.actionType = EditActionType::Delete; deletion.targetNoteId = 1;
      passes.editPasses.push_back(deletion);
    }
    if (sample % 2) {
      EditPass create; create.actionType = EditActionType::Create;
      auto added = MidiEvent::NoteOn(80, 1, 65, 70); added.noteId = 1;
      create.addedEvents = {added, MidiEvent::NoteOff(160, 1, 65), MidiEvent::ControlChange(80, 1, 7, 99)};
      passes.editPasses.push_back(create);
    }
    LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
    auto& capture = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    auto captureOn = MidiEvent::NoteOn(openLedger ? 300 : 700, 2, 64, 95); captureOn.noteId = openLedger ? 2 : 1;
    TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::NoteOff(openLedger ? 350 : 800, 2, 64)));
    TEST_ASSERT_TRUE(capture.storage.append(captureOn));
    TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(80, 2, 8, 66)));
    TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::NoteOff(80, 2, 66)));
    if (openLedger || sample % 5 == 0) capture.storage.discardEvent(3);
    capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
    PendingNoteChangeVec changes{{!openLedger && sample % 4 == 0 ? PendingNoteChangeKind::Hide : PendingNoteChangeKind::Shorten,
                                  1, 1, 60, 90, 10, openLedger ? 150u : (sample % 4 == 1 ? 35u : 100u)}};
    auto& rows = runtime.frozenCaptureCompanions;
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, rows.step(1));
    // Build the complete oracle from the actual committed representation.
    MidiEventVec sortedCapture;
    for (size_t i = 0; i < capture.storage.eventCount; ++i)
      if (capture.storage.eventRetained(i)) sortedCapture.push_back(capture.storage.events[i]);
    std::stable_sort(sortedCapture.begin(), sortedCapture.end(), [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    CommittedChunkIdList capturedRefs; LoopEventStore captured;
    for (const auto& event : sortedCapture) TEST_ASSERT_TRUE(captured.append(event));
    TEST_ASSERT_TRUE(captured.detachChunksToCommittedChunkIds(capturedRefs));
    passes.overdubPasses.emplace_back(); passes.overdubPasses.back().mergeSequence = 1;
    TEST_ASSERT_TRUE(LoopEventStore::tryCopyCommittedChunkIds(passes.overdubPasses.back().committedChunkIds, capturedRefs));
    const size_t historyCount = passes.editPasses.size();
    for (size_t i = 0; i < rows.size(); ++i) passes.editPasses.push_back(rows.row(i));
    SessionMidiEventVec oracle; passes.materializeToEventVector(oracle, 768);
    passes.editPasses.resize(historyCount); passes.overdubPasses.clear();
    RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
    auto& inputs = runtime.canonicalPassInputs;
    auto& prepared = runtime.frozenCompanionPasses;
    PlaybackViewSourceIdentity baselineSource{1, 2, &passes}, candidateSource{2, 3, &passes};
    const size_t quantum = 1 + sample % 7;
    const auto finishInputs = [&]() {
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = inputs.step(quantum);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    };
    const auto finishPrepared = [&]() {
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = prepared.step(quantum);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    };
    TEST_ASSERT_TRUE(inputs.begin(runtime.frozenCompanionApply, work, passes, baselineSource, 768)); finishInputs();
    TEST_ASSERT_TRUE(prepared.begin(runtime.frozenCompanionApply, work, inputs, nullptr, 0, baselineSource, 768)); finishPrepared();
    auto& set = *runtime.viewSetIfPrewarmed();
    auto& base = set.views[set.lifecycle.current];
    RuntimePlaybackViewFill fill; TEST_ASSERT_TRUE(fill.begin(base, prepared, baselineSource, {0,768,768}, {}, {}));
    auto fillResult = RuntimePlaybackViewFillResult::Pending;
    for (size_t i = 0; i < 10000 && fillResult == RuntimePlaybackViewFillResult::Pending; ++i) fillResult = fill.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Complete, fillResult);
    prepared.discard();
    if (sample < 5) inputs.discard();
    RuntimePlaybackOrderBuilder order; TEST_ASSERT_TRUE(order.begin(base, 768));
    while (order.step(quantum) == RuntimePlaybackOrderBuildResult::Pending) {}
    base.published = true;
    uint32_t revision = sample >= 5 ? 1 : 2, generation = sample >= 5 ? 2 : 3, epoch = 5, length = 768;
    RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
    if (sample >= 5) {
      const auto oldGeneration = inputs.generation();
      const auto* prefixEvents = inputs.data()[0].events;
      TEST_ASSERT_TRUE(inputs.appendFrozenOverdub(live));
      TEST_ASSERT_TRUE(inputs.generation() != oldGeneration); TEST_ASSERT_NULL(inputs.data());
      finishInputs(); TEST_ASSERT_EQUAL_PTR(prefixEvents, inputs.data()[0].events);
    } else {
      TEST_ASSERT_TRUE(inputs.beginFrozenOverdub(runtime.frozenCompanionApply, work, passes, candidateSource, live)); finishInputs();
    }
    TEST_ASSERT_TRUE(prepared.begin(runtime.frozenCompanionApply, work, inputs, rows, candidateSource, 768)); finishPrepared();
    auto& mapping = runtime.frozenCompanionBridgeMap;
    auto& bridge = set.bridges[set.bridgeLifecycle.candidate];
    if (sample >= 2) {
      EmissionCursor anchor{}; anchor.phase = openLedger ? 701 : 0;
      anchor.effectiveSourceGeneration = candidateSource.sourceGeneration;
      if (openLedger) runtime.ledger.noteOn(1, 60, 1, 700, 90);
      TEST_ASSERT_FALSE(runtime.beginFrozenCompanionBridge(base.source, candidateSource, 4, epoch + 1, 768, anchor));
      TEST_ASSERT_TRUE(runtime.beginFrozenCompanionBridge(base.source, candidateSource, 4, epoch, 768, anchor));
      TEST_ASSERT_FALSE(runtime.beginFrozenCompanionBridge(base.source, candidateSource, 4, epoch, 768, anchor));
      TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4, epoch, 768, anchor));
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
        result = runtime.stepFrozenCaptureBridge(quantum);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      TEST_ASSERT_TRUE(runtime.canSelectFrozenCaptureBridge(4, epoch, 768, anchor));
      TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4, epoch + 1, 768, anchor));
      TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(5, epoch, 768, anchor));
      if (sample == 2 || sample == 3 || sample == 4) {
        uint64_t reusedMapGeneration = 0;
        if (sample == 2) {
          ++epoch; // Invalidate after compilation, without another builder turn.
          TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4, epoch - 1, 768, anchor));
        } else if (sample == 3) {
          TEST_ASSERT_TRUE(mapping.begin(prepared, base, candidateSource));
          reusedMapGeneration = mapping.generation();
        } else rows.discard();
        TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(4, epoch, 768, anchor));
        TEST_ASSERT_FALSE(runtime.selectFrozenCaptureBridge(4, epoch, 768, anchor));
        runtime.discardFrozenCaptureBridge(); prepared.discard(); inputs.discard();
        if (sample == 3) {
          TEST_ASSERT_EQUAL_UINT64(reusedMapGeneration, mapping.generation()); // Do not cancel a reused mapper.
          mapping.discard();
        }
        if (sample == 4) { TEST_ASSERT_TRUE(rows.begin(capture, 4, changes)); rows.step(1); }
        TEST_ASSERT_TRUE(inputs.beginFrozenOverdub(runtime.frozenCompanionApply, work, passes, candidateSource, live)); finishInputs();
        TEST_ASSERT_TRUE(prepared.begin(runtime.frozenCompanionApply, work, inputs, rows, candidateSource, 768)); finishPrepared();
        TEST_ASSERT_TRUE(runtime.beginFrozenCompanionBridge(base.source, candidateSource, 4, epoch, 768, anchor));
        result = RuntimePlaybackOrderBuildResult::Pending;
        for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
          result = runtime.stepFrozenCaptureBridge(quantum);
        TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      }
      TEST_ASSERT_TRUE(runtime.selectFrozenCaptureBridge(4, epoch, 768, anchor));
      TEST_ASSERT_EQUAL_PTR(&bridge, runtime.selectedFullLoopBridge(candidateSource, 768));
      runtime.discardFrozenCaptureBridge(); // Selected storage survives preparation cleanup.
      TEST_ASSERT_EQUAL_PTR(&bridge, runtime.selectedFullLoopBridge(candidateSource, 768));
      // A subsequent overdub gets the alternate candidate while this selected
      // bridge stays readable until the next atomic publication.
      TEST_ASSERT_TRUE(runtime.beginFrozenCompanionBridge(base.source, candidateSource, 4, epoch, 768, anchor));
      TEST_ASSERT_EQUAL_PTR(&bridge, runtime.selectedFullLoopBridge(candidateSource, 768));
      runtime.discardFrozenCaptureBridge();
    } else {
    TEST_ASSERT_TRUE(mapping.begin(prepared, base, candidateSource));
    if (sample == 0) {
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, mapping.step(1));
      ++epoch;
      TEST_ASSERT_FALSE(mapping.ready(base.source)); TEST_ASSERT_NULL(mapping.records());
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, mapping.step(1));
      prepared.discard(); inputs.discard();
      TEST_ASSERT_TRUE(inputs.beginFrozenOverdub(runtime.frozenCompanionApply, work, passes, candidateSource, live)); finishInputs();
      TEST_ASSERT_TRUE(prepared.begin(runtime.frozenCompanionApply, work, inputs, rows, candidateSource, 768)); finishPrepared();
      TEST_ASSERT_TRUE(mapping.begin(prepared, base, candidateSource));
    }
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = mapping.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);

    bridge.coverage = base.coverage; bridge.source = candidateSource;
    if (openLedger) {
      ActiveNoteLedger ledger; ledger.noteOn(1, 60, 1, 700, 90);
      bridge.ledgerBoundary.capture(ledger); bridge.cursorAnchor.phase = 701;
    }
    RuntimePlaybackBridgeBuild build; TEST_ASSERT_TRUE(build.begin(base, bridge, &mapping));
    if (sample == 1) {
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, build.step(1));
      ++epoch;
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(1));
      TEST_ASSERT_FALSE(bridge.complete); TEST_ASSERT_FALSE(bridge.selected);
      mapping.discard(); prepared.discard(); inputs.discard(); bridge.resetMetadata();
      TEST_ASSERT_TRUE(inputs.beginFrozenOverdub(runtime.frozenCompanionApply, work, passes, candidateSource, live)); finishInputs();
      TEST_ASSERT_TRUE(prepared.begin(runtime.frozenCompanionApply, work, inputs, rows, candidateSource, 768)); finishPrepared();
      TEST_ASSERT_TRUE(mapping.begin(prepared, base, candidateSource));
      result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = mapping.step(quantum);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      bridge.coverage = base.coverage; bridge.source = candidateSource;
      TEST_ASSERT_TRUE(build.begin(base, bridge, &mapping));
    }
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = build.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    }
    // Compiled bridge owns all data needed after source preparation retires.
    mapping.discard(); prepared.discard(); inputs.discard(); rows.discard();
    bridge.selected = true;
    if (!openLedger) assertBridgeEqualsOracle(base, bridge, std::vector<MidiEvent>(oracle.begin(), oracle.end()));
    else {
      const auto wire = [](uint32_t cycle, const MidiEvent& event) -> uint64_t {
        const bool note = event.isNoteOn() || event.isNoteOff();
        return (uint64_t(cycle * 768 + event.tick % 768) << 32) |
          (uint64_t(event.type) << 24) | (uint64_t(event.channel) << 16) |
          (uint64_t(note ? event.data.noteData.note : event.data.ccData.cc) << 8) |
          (note ? event.data.noteData.velocity : event.data.ccData.value);
      };
      std::vector<uint64_t> actual, expected;
      RuntimePlaybackBridgeCursor cursor;
      for (uint32_t cycle = 0; cycle < 4; ++cycle) {
        TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, cycle, cycle == 0 ? 700 : UINT32_MAX));
        while (const auto* event = cursor.peek()) { actual.push_back(wire(cycle, *event)); TEST_ASSERT_TRUE(cursor.advance()); }
        if (cycle == 0) continue;
        expected.push_back(wire(cycle, MidiEvent::ControlChange(80, 2, 8, 66)));
        expected.push_back(wire(cycle, MidiEvent::NoteOff(cycle == 1 ? 100 : 150, 1, 60)));
        expected.push_back(wire(cycle, captureOn));
        expected.push_back(wire(cycle, MidiEvent::NoteOff(350, 2, 64)));
        expected.push_back(wire(cycle, on));
      }
      TEST_ASSERT_EQUAL(expected.size(), actual.size());
      for (size_t i = 0; i < expected.size(); ++i) TEST_ASSERT_EQUAL_UINT64(expected[i], actual[i]);
    }
    LoopEventStore::releaseChunkRefs(capturedRefs); LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
  }
}

void test_pass_origin_view_fill_rejects_stale_or_edited_sources_and_published_writes() {
  LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
  MidiEvent events[] = {MidiEvent::NoteOn(0,1,60,90), MidiEvent::NoteOff(100,1,60)};
  events[0].noteId = 1;
  RuntimeCompanionPasses::Pass pass{events, 2};
  PlaybackViewSourceIdentity source{1,2,events};
  RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
    runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
  auto& prepared = runtime.frozenCompanionPasses;
  auto prepare = [&](const EditPass* rows, size_t count) {
    TEST_ASSERT_TRUE(prepared.begin(runtime.frozenCompanionApply, work, &pass, 1, rows, count, source, 768));
    while (prepared.step(32) == RuntimePlaybackOrderBuildResult::Pending) {}
    TEST_ASSERT_TRUE(prepared.ready(source));
  };
  RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(4));
  RuntimePlaybackViewFill fill;
  prepare(nullptr, 0);
  TEST_ASSERT_FALSE(fill.begin(view, prepared, source, {0,384,768}, {}, {}));
  TEST_ASSERT_TRUE(fill.begin(view, prepared, source, {0,768,768}, {}, {}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Pending, fill.step(1));
  TEST_ASSERT_FALSE(view.sourceFilled); TEST_ASSERT_FALSE(view.passOriginsReady);
  prepared.discard();
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Denied, fill.step(1));
  TEST_ASSERT_FALSE(view.passOriginsReady);
  auto row = lengthRow(1, 200); prepare(&row, 1);
  TEST_ASSERT_FALSE(fill.begin(view, prepared, source, {0,768,768}, {}, {}));
  prepare(nullptr, 0);
  TEST_ASSERT_TRUE(fill.begin(view, prepared, source, {0,768,768}, {}, {}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Complete, fill.step(2));
  TEST_ASSERT_TRUE(view.passOriginsReady);
  RuntimePlaybackOrderBuilder order; TEST_ASSERT_TRUE(order.begin(view, 768));
  while (order.step(32) == RuntimePlaybackOrderBuildResult::Pending) {}
  view.published = true;
  TEST_ASSERT_FALSE(fill.begin(view, events, 2, source, {0,768,768}, {}, {}));
  TEST_ASSERT_TRUE(view.published); TEST_ASSERT_TRUE(view.passOriginsReady);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  auto& map = runtime.frozenCompanionBridgeMap;
  TEST_ASSERT_TRUE(map.begin(prepared, view, source)); map.discard();
  ++view.source.sourceGeneration;
  TEST_ASSERT_FALSE(map.begin(prepared, view, source));
}

void test_per_pass_bridge_preserves_capture_and_repeated_identity_scope() {
  auto on = MidiEvent::NoteOn(0, 1, 60, 90); on.noteId = 1;
  auto captured = MidiEvent::NoteOn(150, 1, 60, 90); captured.noteId = 2;
  {
    PerPassBridgeFixture fixture;
    fixture.prepare({{on, MidiEvent::NoteOff(10, 1, 60)}, {captured, MidiEvent::NoteOff(160, 1, 60)}},
                    EditPassVec{lengthRow(1, 250)}, 1);
    TEST_ASSERT_EQUAL_UINT32(4, fixture.expected.size()); fixture.mapAndCompile(1);
  }
  captured.noteId = 1;
  EditPass deletion = lengthRow(1, 0); deletion.actionType = EditActionType::Delete;
  {
    PerPassBridgeFixture fixture;
    fixture.prepare({{on, MidiEvent::NoteOff(10, 1, 60)}, {captured, MidiEvent::NoteOff(160, 1, 60)}},
                    EditPassVec{deletion}, 1);
    TEST_ASSERT_TRUE(fixture.expected.empty()); fixture.mapAndCompile(1);
  }
  {
    PerPassBridgeFixture fixture; fixture.prepare({{on}}, EditPassVec{lengthRow(1, 250)}, 0);
    TEST_ASSERT_EQUAL_UINT32(2, fixture.expected.size()); fixture.mapAndCompile(1);
  }
}

void test_per_pass_bridge_generated_capture_maps_match_canonical_reference() {
  std::mt19937 rng(0x706d6170);
  for (size_t test = 0; test < 180; ++test) {
    std::vector<MidiEventVec> inputs(1+rng()%4);
    for (auto& pass : inputs) {
      for (size_t i = 0, count = rng()%10; i < count; ++i) {
        auto event = i%3 == 0 ? MidiEvent::ControlChange((rng()%17)*48, 1, 7, rng()%128)
          : i%2 ? MidiEvent::NoteOn((rng()%17)*48, 1+rng()%2, 60+rng()%3, 90)
                : MidiEvent::NoteOff((rng()%17)*48, 1+rng()%2, 60+rng()%3);
        if (event.isNoteOn()) event.noteId = 1+rng()%4;
        pass.push_back(event);
      }
      std::stable_sort(pass.begin(), pass.end(), [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    }
    EditPassVec rows;
    for (size_t i = 0; i < 4; ++i) {
      auto row = lengthRow(1+rng()%4, (rng()%18)*48);
      if (rng()%3 == 0) row.actionType = EditActionType::Delete;
      rows.push_back(row);
    }
    const size_t basePassCount = inputs.size()-1;
    PerPassBridgeFixture fixture; fixture.prepare(std::move(inputs), rows, basePassCount); fixture.mapAndCompile(1+test%7);
  }
}

void test_per_pass_bridge_maximum_base_and_capture_result() {
  std::vector<MidiEventVec> inputs(2);
  for (size_t i = 0; i < 1024; ++i) {
    inputs[0].push_back(MidiEvent::ControlChange(i*2, 1, 7, i%128));
    inputs[1].push_back(MidiEvent::ControlChange(i*2+1, 2, 11, i%128));
  }
  PerPassBridgeFixture fixture; fixture.prepare(std::move(inputs), {}, 1);
  TEST_ASSERT_EQUAL_UINT32(2048, fixture.expected.size()); fixture.mapAndCompile(32);
}

void test_per_pass_origin_index_maximum_interleaved_base() {
  std::vector<MidiEventVec> inputs(4);
  for (size_t pass = 0; pass < inputs.size(); ++pass)
    for (size_t i = 0; i < 512; ++i)
      inputs[pass].push_back(MidiEvent::ControlChange(i*4+pass, 1+pass, 7, i%128));
  PerPassBridgeFixture fixture; fixture.prepare(std::move(inputs), {}, 4);
  TEST_ASSERT_EQUAL(2048, fixture.base.storage.eventCount);
  TEST_ASSERT_TRUE(RuntimeCompanionBridgeMap::requiredPerPassGrainsUpperBound(2048, 2048, 32) < 10000);
  fixture.mapAndCompile(1); // Origins interleave all four lanes; source order is not key order.
}

void test_per_pass_bridge_mapping_denies_invalid_provenance_and_reuse() {
  const auto cc = MidiEvent::ControlChange(0, 1, 7, 90);
  PerPassBridgeFixture fixture; fixture.prepare({{cc, cc}}, {}, 1);
  auto& map = fixture.runtime.frozenCompanionBridgeMap;
  auto& prepared = fixture.runtime.frozenCompanionPasses;
  auto begin = [&]() { return map.begin(prepared, fixture.base.storage.events, fixture.base.storage.eventCount,
      fixture.baseOrigins.data(), fixture.base.source, fixture.preparedSource); };
  auto finishDenied = [&]() {
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i=0; i<100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result=map.step(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result); TEST_ASSERT_NULL(map.records());
  };
  fixture.baseOrigins[1] = fixture.baseOrigins[0];
  TEST_ASSERT_TRUE(begin()); finishDenied();
  fixture.baseOrigins[1] = {0, 1}; fixture.baseOrigins[0] = {0, UINT16_MAX};
  TEST_ASSERT_TRUE(begin()); finishDenied();
  fixture.baseOrigins[0] = {0, 0}; ++fixture.base.storage.events[0].data.ccData.value;
  TEST_ASSERT_TRUE(begin()); finishDenied(); --fixture.base.storage.events[0].data.ccData.value;
  TEST_ASSERT_TRUE(begin()); TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, map.step(1));
  ++fixture.base.source.sourceGeneration; finishDenied();
  TEST_ASSERT_TRUE(begin()); TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, map.step(1));
  prepared.discard(); finishDenied();
}

void test_ranked_bridge_orders_unchanged_base_and_replacement_ties() {
  std::vector<MidiEvent> events{MidiEvent::NoteOn(0, 1, 60, 90), MidiEvent::NoteOff(300, 1, 60),
    MidiEvent::NoteOn(10, 1, 61, 90), MidiEvent::NoteOff(300, 1, 61),
    MidiEvent::NoteOn(20, 1, 62, 90), MidiEvent::NoteOff(350, 1, 62)};
  events[0].noteId = 1; events[2].noteId = 2; events[4].noteId = 3;
  // Move index 3 before index 1, then restore its tick. Both become unchanged
  // base rows again, but the sequential reference's tie order remains 3,1.
  std::vector<EditPass> rows{lengthRow(2, 200), lengthRow(3, 400), lengthRow(2, 300)};
  RankedBridgeFixture fixture; fixture.prepare(events, rows);
  const auto& apply = fixture.runtime.frozenCompanionApply;
  std::vector<MidiEvent> expected(apply.data(), apply.data() + apply.size());
  TEST_ASSERT_TRUE(fixture.runtime.frozenCompanionBridgeMap.baseRanks()[3] <
                   fixture.runtime.frozenCompanionBridgeMap.baseRanks()[1]);
  fixture.compile(1);
  assertBridgeEqualsOracle(fixture.base, fixture.bridge, expected);
}

void test_ranked_bridge_batches_match_sequential_reference() {
  std::mt19937 rng(0x72616e6b);
  for (size_t test = 0; test < 250; ++test) {
    std::vector<MidiEvent> events;
    for (size_t i = 0, count = rng() % 36; i < count; ++i) {
      const uint32_t tick = (rng() % 18) * 48;
      MidiEvent event = i % 5 == 0 ? MidiEvent::ControlChange(tick, 1, 7, rng() % 128)
                        : i % 3 ? MidiEvent::NoteOn(tick, 1 + rng() % 2, 60 + rng() % 3, 90)
                                : MidiEvent::NoteOff(tick, 1 + rng() % 2, 60 + rng() % 3);
      if (event.isNoteOn()) event.noteId = 1 + rng() % 7;
      events.push_back(event);
    }
    std::vector<EditPass> rows;
    MidiEventVec reference(events.begin(), events.end());
    for (size_t i = 0; i < 6; ++i) {
      auto row = lengthRow(1 + rng() % 7, (rng() % 19) * 48);
      if (rng() % 4 == 0) row.actionType = EditActionType::Delete;
      if (row.actionType == EditActionType::Delete) applyDeleteNoteById(reference, row.targetNoteId, 768, nullptr);
      else applyChangeLengthById(reference, row.targetNoteId, row.endTick, 768, nullptr);
      rows.push_back(row);
    }
    RankedBridgeFixture fixture; fixture.prepare(events, rows); fixture.compile(1 + test % 7);
    assertBridgeEqualsOracle(fixture.base, fixture.bridge, std::vector<MidiEvent>(reference.begin(), reference.end()));
  }
}

void test_ranked_bridge_retains_original_cross_wrap_off() {
  auto on = MidiEvent::NoteOn(700, 1, 60, 90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(100, 1, 60); off.noteId = 42;
  RankedBridgeFixture fixture; fixture.prepare({on, off}, {lengthRow(42, 150)});
  ActiveNoteLedger ledger; ledger.noteOn(1, 60, 42, 700, 90);
  fixture.bridge.ledgerBoundary.capture(ledger); fixture.bridge.cursorAnchor.phase = 701;
  TEST_ASSERT_NULL(fixture.bridge.heldClosure(0));
  fixture.compile(1);
  const auto* closure = fixture.bridge.heldClosure(0);
  TEST_ASSERT_NOT_NULL(closure);
  TEST_ASSERT_EQUAL_UINT64(868, closure->relativeTick);
  TEST_ASSERT_TRUE(RuntimeCompanionBridgeMap::inputEventsMatch(off, closure->originalOff));
  TEST_ASSERT_EQUAL_UINT32(42, fixture.bridge.ledgerBoundary.openNotes[0].noteId);
  TEST_ASSERT_NULL(fixture.bridge.heldClosure(1));
  std::vector<uint32_t> trace;
  RuntimePlaybackBridgeCursor cursor;
  for (uint32_t cycle = 0; cycle < 4; ++cycle) {
    TEST_ASSERT_TRUE(cursor.selectAfterPhase(fixture.base, fixture.bridge, cycle, cycle == 0 ? 700 : UINT32_MAX));
    while (const auto* event = cursor.peek()) {
      trace.push_back((cycle * 768 + event->tick % 768) * 2 + (event->isNoteOn() ? 1 : 0));
      TEST_ASSERT_TRUE(cursor.advance());
    }
  }
  const std::vector<uint32_t> expected{868 * 2, 1468 * 2 + 1, 1686 * 2, 2236 * 2 + 1, 2454 * 2, 3004 * 2 + 1};
  TEST_ASSERT_EQUAL_UINT32(expected.size(), trace.size());
  for (size_t i = 0; i < expected.size(); ++i) TEST_ASSERT_EQUAL_UINT32(expected[i], trace[i]);
  fixture.build.discard();
  fixture.base.resetMetadata();
  TEST_ASSERT_TRUE(fixture.base.storage.append(MidiEvent::ControlChange(1, 1, 7, 80)));
  TEST_ASSERT_TRUE(fixture.base.storage.append(MidiEvent::ControlChange(2, 1, 7, 81)));
  TEST_ASSERT_EQUAL_PTR(closure, fixture.bridge.heldClosure(0));
  TEST_ASSERT_TRUE(RuntimeCompanionBridgeMap::inputEventsMatch(off, closure->originalOff));
  TEST_ASSERT_EQUAL_UINT64(868, closure->relativeTick);
  fixture.bridge.resetMetadata();
  TEST_ASSERT_NULL(fixture.bridge.heldClosure(0));
}

void test_bridge_boundary_rebind_matches_fresh_compile_across_live_cycles() {
  auto on = MidiEvent::NoteOn(700,1,60,90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(100,1,60); off.noteId = 42;
  RankedBridgeFixture reused; reused.prepare({on,off},{lengthRow(42,150)});
  TEST_ASSERT_TRUE(reused.build.begin(reused.base,reused.bridge,&reused.runtime.frozenCompanionBridgeMap));
  auto initial = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn=0; turn<10000 && initial==RuntimePlaybackOrderBuildResult::Pending; ++turn)
    initial=reused.build.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,initial);
  const auto generation = reused.bridge.storageGeneration;
  const auto mapGeneration = reused.runtime.frozenCompanionBridgeMap.generation();
  for (unsigned cycle=0; cycle<6; ++cycle) {
    ActiveNoteLedger ledger;
    const uint32_t phase = cycle%2 ? 120 : 710;
    if (!(cycle%2)) ledger.noteOn(1,60,42,700,90);
    ledger.noteOn(1,127,127,phase,90); // Not affected by this edit.
    const EmissionCursor anchor{cycle,phase,0,0,reused.bridge.source.sourceGeneration};
    reused.bridge.selected = false;
    const size_t insertionCount = reused.bridge.insertions.eventCount;
    const size_t records = reused.bridge.recordCount;
    TEST_ASSERT_TRUE(reused.build.rebindBoundary(reused.base,reused.bridge,
        reused.runtime.frozenCompanionBridgeMap,ledger,anchor));
    const auto bound = RuntimePlaybackBridgeBuild::requiredBoundaryGrains(
        reused.base.storage.eventCount,insertionCount,records,2,1);
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (uint64_t turn=0; turn<bound && result==RuntimePlaybackOrderBuildResult::Pending; ++turn) {
      ledger.noteOff(1,127);
      if (turn%2) ledger.noteOn(1,127,127,phase+turn,90);
      result=reused.build.step(1);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,result);
    TEST_ASSERT_TRUE(reused.bridge.matchesAffectedLedger(ledger));
    TEST_ASSERT_EQUAL_UINT64(generation,reused.bridge.storageGeneration);
    TEST_ASSERT_EQUAL_UINT64(mapGeneration,reused.runtime.frozenCompanionBridgeMap.generation());
    TEST_ASSERT_EQUAL_UINT32(records,reused.bridge.recordCount);
    TEST_ASSERT_EQUAL_UINT32(insertionCount,reused.bridge.insertions.eventCount);
    RankedBridgeFixture fresh; fresh.prepare({on,off},{lengthRow(42,150)});
    fresh.bridge.ledgerBoundary.capture(ledger); fresh.bridge.cursorAnchor=anchor;
    fresh.compile(1); reused.bridge.selected=true;
    for (unsigned age=0; age<4; ++age) {
      RuntimePlaybackBridgeCursor left,right;
      const auto completed=age ? UINT32_MAX : phase-1;
      TEST_ASSERT_TRUE(left.selectAfterPhase(reused.base,reused.bridge,cycle+age,completed));
      TEST_ASSERT_TRUE(right.selectAfterPhase(fresh.base,fresh.bridge,cycle+age,completed));
      while (const auto* expected=right.peek()) {
        const auto* actual=left.peek(); TEST_ASSERT_NOT_NULL(actual);
        TEST_ASSERT_EQUAL_UINT32(expected->tick,actual->tick);
        TEST_ASSERT_EQUAL_UINT32(expected->noteId,actual->noteId);
        TEST_ASSERT_EQUAL(expected->isNoteOn(),actual->isNoteOn());
        TEST_ASSERT_TRUE(left.advance()); TEST_ASSERT_TRUE(right.advance());
      }
      TEST_ASSERT_NULL(left.peek());
    }
  }
}

void test_second_source_keeps_selected_original_off() {
  auto on = MidiEvent::NoteOn(700,1,60,90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(300,1,60); off.noteId = 42;
  RankedBridgeFixture original; original.prepare({on,off},{lengthRow(42,150)});
  ActiveNoteLedger ledger; ledger.noteOn(1,60,42,700,90);
  original.bridge.ledgerBoundary.capture(ledger); original.bridge.cursorAnchor.phase = 701;
  original.compile(1);
  auto firstRevisionOff = off; firstRevisionOff.tick = 918;
  RankedBridgeFixture next;
  next.prepare({on,firstRevisionOff},{lengthRow(42,50)},&original.bridge.source);
  next.bridge.source = {3,2,nullptr}; next.bridge.cursorAnchor = {0,701,0,0,2};
  next.bridge.ledgerBoundary.capture(ledger);
  const auto bound = RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(
      next.base.storage.eventCount,next.runtime.frozenCompanionBridgeMap.size(),1) +
      RuntimePlaybackBridgeBuild::requiredRetainedClosureGrains(1,1);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX,RuntimePlaybackBridgeBuild::requiredRetainedClosureGrains(129,1));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX,RuntimePlaybackBridgeBuild::requiredRetainedClosureGrains(1,0));
  TEST_ASSERT_TRUE(next.build.begin(next.base,next.bridge,&next.runtime.frozenCompanionBridgeMap,&original.bridge));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (uint64_t turn = 0; turn < bound && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = next.build.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,result);
  TEST_ASSERT_EQUAL_UINT64(1068,next.bridge.heldClosure(0)->relativeTick);
  const auto stableGeneration = next.bridge.storageGeneration;
  TEST_ASSERT_TRUE(next.build.rebindBoundary(next.base,next.bridge,
      next.runtime.frozenCompanionBridgeMap,ledger,{0,710,0,0,2},&original.bridge));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn=0; turn<1000 && result==RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result=next.build.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,result);
  TEST_ASSERT_EQUAL_UINT64(stableGeneration,next.bridge.storageGeneration);
  TEST_ASSERT_EQUAL_UINT64(1068,next.bridge.heldClosure(0)->relativeTick);
  original.bridge.resetMetadata(); // The selected successor now owns its obligation.
  next.bridge.selected = true;
  std::vector<uint32_t> actual;
  RuntimePlaybackBridgeCursor cursor;
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    TEST_ASSERT_TRUE(cursor.selectAfterPhase(next.base,next.bridge,cycle,cycle ? UINT32_MAX : 709));
    while (const auto* event = cursor.peek()) {
      actual.push_back((cycle * 768 + event->tick % 768) * 2 + event->isNoteOn());
      TEST_ASSERT_TRUE(cursor.advance());
    }
  }
  const std::vector<uint32_t> expected{1068*2,1468*2+1,1586*2,2236*2+1,2354*2,3004*2+1};
  TEST_ASSERT_EQUAL_UINT32(expected.size(),actual.size());
  for (size_t i = 0; i < actual.size(); ++i) TEST_ASSERT_EQUAL_UINT32(expected[i],actual[i]);
  // A carried original Off must not turn into a recurring insertion after it closes.
  ledger.clear(); next.bridge.selected=false;
  TEST_ASSERT_TRUE(next.build.rebindBoundary(next.base,next.bridge,
      next.runtime.frozenCompanionBridgeMap,ledger,{4,200,0,0,2}));
  result=RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn=0; turn<1000 && result==RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result=next.build.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete,result);
  next.bridge.selected=true;
  TEST_ASSERT_TRUE(cursor.selectAfterPhase(next.base,next.bridge,4,199));
  TEST_ASSERT_NOT_NULL(cursor.peek());
  TEST_ASSERT_TRUE(cursor.peek()->isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(700,cursor.peek()->tick);
  TEST_ASSERT_TRUE(cursor.advance()); TEST_ASSERT_NULL(cursor.peek());

}

void test_bridge_successor_inherits_wrap_closure_without_replaying_it() {
  auto on = MidiEvent::NoteOn(700, 1, 60, 90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(100, 1, 60); off.noteId = 42;
  RankedBridgeFixture predecessor; predecessor.prepare({on, off}, {lengthRow(42, 150)});
  ActiveNoteLedger ledger; ledger.noteOn(1, 60, 42, 700, 90);
  predecessor.bridge.ledgerBoundary.capture(ledger); predecessor.bridge.cursorAnchor.phase = 701;
  predecessor.compile(1);
  RuntimePlaybackView base;
  auto newOff = off; newOff.tick = 918; // Canonical wrapped Off, phase 150.
  prepareBridgeBase(base, {newOff});
  base.canonicalRanksReady = true; base.canonicalRanks[0] = predecessor.bridge.insertionRanks[0];
  base.source = predecessor.bridge.source;
  base.coverage = {0,192,768};
  FixedPlaybackBridgeStorage successor; TEST_ASSERT_TRUE(successor.prewarm(32));
  successor.source = base.source; successor.coverage = base.coverage;
  successor.cursorAnchor = {1,0,0,0,base.source.sourceGeneration};
  successor.ledgerBoundary.capture(ledger);
  RuntimePlaybackBridgeBuild build;
  auto savedAnchor = successor.cursorAnchor;
  successor.cursorAnchor.projectionCycle = 2;
  TEST_ASSERT_FALSE(build.beginSuccessor(base, successor, predecessor.bridge));
  successor.cursorAnchor = savedAnchor;
  TEST_ASSERT_TRUE(build.beginSuccessor(base, successor, predecessor.bridge));
  predecessor.base.resetMetadata(); // The inherited Off must come from owned bridge metadata.
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (uint64_t turn = 0; turn < RuntimePlaybackBridgeBuild::requiredSuccessorGrainsUpperBound(1, 1, 1) &&
       result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = build.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_NOT_NULL(successor.heldClosure(0));
  TEST_ASSERT_EQUAL_UINT64(100, successor.heldClosure(0)->relativeTick);
  TEST_ASSERT_TRUE(RuntimeCompanionBridgeMap::inputEventsMatch(off, successor.heldClosure(0)->originalOff));
  predecessor.bridge.resetMetadata(); // Compilation detached its borrowed predecessor.
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, build.step(1));
  successor.selected = true;
  RuntimePlaybackBridgeCursor cursor;
  for (uint32_t cycle = 1; cycle < 6; ++cycle) {
    TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, successor, cycle));
    TEST_ASSERT_NOT_NULL(cursor.peek());
    TEST_ASSERT_EQUAL_UINT32(cycle == 1 ? 100 : 918, cursor.peek()->tick);
    TEST_ASSERT_TRUE(cursor.advance());
    TEST_ASSERT_NULL(cursor.peek()); // No repeated original Off in steady cycles.
  }
}

void test_initial_window_inherits_off_beyond_coverage_then_rotates() {
  auto on = MidiEvent::NoteOn(700, 1, 60, 90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(300, 1, 60); off.noteId = 42;
  RankedBridgeFixture context; context.prepare({on, off}, {lengthRow(42, 350)});
  ActiveNoteLedger ledger; ledger.noteOn(1, 60, 42, 700, 90);
  context.bridge.ledgerBoundary.capture(ledger); context.bridge.cursorAnchor.phase = 701;
  context.compile(1); context.bridge.selected = false;
  RuntimePlaybackView windows[3];
  FixedPlaybackBridgeStorage bridges[3];
  const FixedPlaybackBridgeStorage* predecessor = &context.bridge;
  const auto rank = context.bridge.insertionRanks[0];
  for (unsigned i = 0; i < 3; ++i) {
    auto& base = windows[i]; auto& bridge = bridges[i];
    auto canonicalOff = off; canonicalOff.tick = 1118;
    prepareBridgeBase(base, i == 2 ? std::vector<MidiEvent>{canonicalOff} : std::vector<MidiEvent>{});
    base.source = context.bridge.source;
    base.coverage = {i == 0 ? 576u : (i - 1) * 192u, 192, 768};
    base.canonicalRanksReady = true; base.canonicalRanks[0] = rank;
    TEST_ASSERT_TRUE(bridge.prewarm(32));
    bridge.source = base.source; bridge.coverage = base.coverage;
    bridge.cursorAnchor = {i == 0 ? 0u : 1u, i == 0 ? 701u : base.coverage.startPhase,
                           0, 0, base.source.sourceGeneration};
    bridge.ledgerBoundary.capture(ledger);
    RuntimePlaybackBridgeBuild build;
    if (i == 0) {
      ++bridge.ledgerBoundary.openNotes[0].startTick;
      TEST_ASSERT_FALSE(build.beginInitialWindow(base, bridge, *predecessor));
      --bridge.ledgerBoundary.openNotes[0].startTick;
      TEST_ASSERT_TRUE(build.beginInitialWindow(base, bridge, *predecessor));
      ++context.bridge.storageGeneration;
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(1));
      --context.bridge.storageGeneration;
      build.discard();
    }
    TEST_ASSERT_TRUE(i == 0 ? build.beginInitialWindow(base, bridge, *predecessor)
                           : build.beginSuccessor(base, bridge, *predecessor));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (uint64_t turn = 0; turn < RuntimePlaybackBridgeBuild::requiredSuccessorGrainsUpperBound(
        base.storage.eventCount, 1, 1) && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = build.step(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    bridge.selected = true;
    TEST_ASSERT_NOT_NULL(bridge.heldClosure(0));
    TEST_ASSERT_EQUAL_UINT64(i == 0 ? 1068 : 300, bridge.heldClosure(0)->relativeTick);
    RuntimePlaybackBridgeCursor cursor;
    TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, i == 0 ? 0 : 1,
                                             i == 0 ? 700 : i == 1 ? UINT32_MAX : 191));
    if (i < 2) TEST_ASSERT_NULL(cursor.peek());
    else {
      TEST_ASSERT_TRUE(RuntimeCompanionBridgeMap::inputEventsMatch(off, *cursor.peek()));
      TEST_ASSERT_TRUE(cursor.advance()); TEST_ASSERT_NULL(cursor.peek());
      TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, 4));
      TEST_ASSERT_EQUAL_UINT32(1118, cursor.peek()->tick);
      TEST_ASSERT_TRUE(cursor.advance()); TEST_ASSERT_NULL(cursor.peek());
    }
    predecessor = &bridge;
  }
}

void test_runtime_bridge_windows_keep_closure_after_worker_cleanup() {
  LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& set = *runtime.viewSetIfPrewarmed();
  auto& base = set.bridgeBases[0]; auto& bridge = set.bridges[0];
  auto on = MidiEvent::NoteOn(700, 1, 60, 90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(100, 1, 60); off.noteId = 42;
  prepareBridgeBase(base, {on, off});
  bridge.source = {2, 2, nullptr}; bridge.coverage = base.coverage;
  bridge.cursorAnchor = {0, 701, 0, 0, 2};
  runtime.ledger.noteOn(1, 60, 42, 700, 90); bridge.ledgerBoundary.capture(runtime.ledger);
  auto changed = off; changed.tick = 150;
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 1, changed}));
  compileBridge(base, bridge, false);
  TEST_ASSERT_TRUE(runtime.selectPreparedFullLoopBridge(bridge.source, 768, bridge.cursorAnchor));
  auto* storage = base.storage.events;
  runtime.discardFrozenCaptureBridge(); runtime.discardCanonicalWindow(); runtime.discardFrozenCanonicalTail();
  TEST_ASSERT_EQUAL_PTR(storage, runtime.fullLoopBridgeBase(bridge, 768)->storage.events);
  std::vector<uint32_t> actual;
  unsigned directEvents = 0, windowEvents = 0;
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    for (unsigned phase = cycle ? 0 : 701; phase < 768; phase += 1) {
      auto* stream = runtime.prepareFullLoopBridgeStream(bridge.source, 768,
          cycle == 0 && phase == 701 ? 700 : phase ? phase - 1 : UINT32_MAX,
          cycle > 0 && phase == 0);
      TEST_ASSERT_NOT_NULL(stream);
      // A missed maintenance horizon must not delay, lose or replay the Off.
      if (!(cycle == 1 && phase < 210))
        for (unsigned turn = 0; turn < 32; ++turn) runtime.serviceBridgeWindow(phase, 192, 1);
      const auto* window = runtime.publishedBridgeWindow(bridge.source, phase);
      if (window) {
        for (size_t i = 0; i < window->storage.eventCount; ++i) {
          const auto index = window->storage.order[i];
          if (window->storage.phases[index] == phase) {
            actual.push_back((cycle * 768 + phase) * 2 + window->storage.events[index].isNoteOn());
            ++windowEvents;
          }
        }
      } else {
        while (const auto* event = stream->cursor.peek()) {
          if (event->tick % 768 > phase) break;
          actual.push_back((cycle * 768 + event->tick % 768) * 2 + event->isNoteOn());
          ++directEvents;
          TEST_ASSERT_TRUE(stream->cursor.advance());
        }
      }
      stream->acknowledgeThroughPhase(phase);
    }
  }
  TEST_ASSERT_TRUE(directEvents > 0); TEST_ASSERT_TRUE(windowEvents > 0);
  const std::vector<uint32_t> expected{868 * 2, 1468 * 2 + 1, 1686 * 2,
                                     2236 * 2 + 1, 2454 * 2, 3004 * 2 + 1};
  TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
  for (size_t i = 0; i < actual.size(); ++i) TEST_ASSERT_EQUAL_UINT32(expected[i], actual[i]);
  // Beginning the next source may rewrite only its alternate base/bridge.
  TEST_ASSERT_TRUE(set.beginCandidateBridge());
  prepareBridgeBase(set.bridgeBases[1], {MidiEvent::ControlChange(0, 1, 7, 80)});
  TEST_ASSERT_EQUAL_PTR(storage, runtime.fullLoopBridgeBase(bridge, 768)->storage.events);
}

void test_bridge_closure_crosses_two_successor_windows() {
  auto on = MidiEvent::NoteOn(700, 1, 60, 90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(300, 1, 60); off.noteId = 42;
  RankedBridgeFixture original; original.prepare({on, off}, {lengthRow(42, 350)});
  ActiveNoteLedger ledger; ledger.noteOn(1, 60, 42, 700, 90);
  original.bridge.ledgerBoundary.capture(ledger); original.bridge.cursorAnchor.phase = 701;
  original.compile(1);
  RuntimePlaybackView first, second;
  prepareBridgeBase(first, {});
  auto canonicalOff = off; canonicalOff.tick = 1118;
  prepareBridgeBase(second, {canonicalOff});
  FixedPlaybackBridgeStorage bridges[2];
  const FixedPlaybackBridgeStorage* predecessor = &original.bridge;
  for (size_t i = 0; i < 2; ++i) {
    auto& base = i ? second : first;
    base.canonicalRanksReady = true; base.canonicalRanks[0] = original.bridge.insertionRanks[0];
    base.source = original.bridge.source;
    base.coverage = {uint32_t(i * 192),192,768};
    auto& bridge = bridges[i]; TEST_ASSERT_TRUE(bridge.prewarm(32));
    bridge.source = base.source; bridge.coverage = base.coverage;
    bridge.cursorAnchor = {1,base.coverage.startPhase,0,0,base.source.sourceGeneration};
    bridge.ledgerBoundary.capture(ledger);
    RuntimePlaybackBridgeBuild build;
    TEST_ASSERT_TRUE(build.beginSuccessor(base, bridge, *predecessor));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    const size_t quantum = i ? 32 : 1;
    for (uint64_t turn = 0; turn < RuntimePlaybackBridgeBuild::requiredSuccessorGrainsUpperBound(
        base.storage.eventCount, 1, quantum) && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = build.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    bridge.selected = true;
    TEST_ASSERT_EQUAL_UINT64(300, bridge.heldClosure(0)->relativeTick);
    RuntimePlaybackBridgeCursor cursor;
    TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, 1, i ? 191 : UINT32_MAX));
    if (!i) TEST_ASSERT_NULL(cursor.peek());
    else {
      TEST_ASSERT_NOT_NULL(cursor.peek());
      TEST_ASSERT_TRUE(RuntimeCompanionBridgeMap::inputEventsMatch(off, *cursor.peek()));
      TEST_ASSERT_TRUE(cursor.advance()); TEST_ASSERT_NULL(cursor.peek());
      TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, 4));
      TEST_ASSERT_EQUAL_UINT32(1118, cursor.peek()->tick);
      TEST_ASSERT_TRUE(cursor.advance()); TEST_ASSERT_NULL(cursor.peek());
    }
    predecessor = &bridge;
  }
}

void test_bridge_successor_preserves_gate_when_revised_off_moves_before_original_closure() {
  auto on = MidiEvent::NoteOn(700, 1, 60, 90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(300, 1, 60); off.noteId = 42;
  RankedBridgeFixture original; original.prepare({on, off}, {lengthRow(42, 150)});
  ActiveNoteLedger ledger; ledger.noteOn(1, 60, 42, 700, 90);
  original.bridge.ledgerBoundary.capture(ledger); original.bridge.cursorAnchor.phase = 701;
  original.compile(1);
  RuntimePlaybackView base;
  auto changedOff = off; changedOff.tick = 918;
  prepareBridgeBase(base, {changedOff});
  base.source = original.bridge.source; base.coverage = {0,192,768};
  base.canonicalRanksReady = true; base.canonicalRanks[0] = original.bridge.insertionRanks[0];
  FixedPlaybackBridgeStorage next; TEST_ASSERT_TRUE(next.prewarm(32));
  next.source = base.source; next.coverage = base.coverage;
  next.cursorAnchor = {1,0,0,0,base.source.sourceGeneration};
  next.ledgerBoundary.capture(ledger);
  RuntimePlaybackBridgeBuild build; TEST_ASSERT_TRUE(build.beginSuccessor(base, next, original.bridge));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = build.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  next.selected = true;
  RuntimePlaybackBridgeCursor cursor;
  TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, next, 1));
  TEST_ASSERT_NULL(cursor.peek()); // Revised Off at 150 cannot preempt the original Off at 300.
  TEST_ASSERT_EQUAL_UINT64(300, next.heldClosure(0)->relativeTick);
  TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, next, 2));
  TEST_ASSERT_EQUAL_UINT32(918, cursor.peek()->tick);
  TEST_ASSERT_TRUE(cursor.advance()); TEST_ASSERT_NULL(cursor.peek());
}

void test_bridge_successor_rejects_retired_predecessor_and_changed_source() {
  auto on = MidiEvent::NoteOn(700, 1, 60, 90); on.noteId = 42;
  auto off = MidiEvent::NoteOff(100, 1, 60); off.noteId = 42;
  RankedBridgeFixture predecessor; predecessor.prepare({on, off}, {lengthRow(42, 150)});
  ActiveNoteLedger ledger; ledger.noteOn(1, 60, 42, 700, 90);
  predecessor.bridge.ledgerBoundary.capture(ledger); predecessor.bridge.cursorAnchor.phase = 701;
  predecessor.compile(1);
  RuntimePlaybackView base; prepareBridgeBase(base, {});
  base.canonicalRanksReady = true; base.canonicalRanks[0] = predecessor.bridge.insertionRanks[0];
  base.source = predecessor.bridge.source; base.coverage = {0,192,768};
  FixedPlaybackBridgeStorage successor; TEST_ASSERT_TRUE(successor.prewarm(32));
  successor.source = base.source; successor.coverage = base.coverage;
  successor.cursorAnchor = {1,0,0,0,base.source.sourceGeneration};
  successor.ledgerBoundary.capture(ledger);
  RuntimePlaybackBridgeBuild build;
  ++successor.source.playbackRevision;
  TEST_ASSERT_FALSE(build.beginSuccessor(base, successor, predecessor.bridge));
  successor.source = base.source;
  TEST_ASSERT_TRUE(build.beginSuccessor(base, successor, predecessor.bridge));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, build.step(1));
  predecessor.bridge.resetMetadata();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(1));
  TEST_ASSERT_FALSE(successor.complete); TEST_ASSERT_NULL(successor.heldClosure(0));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimePlaybackBridgeBuild::requiredSuccessorGrainsUpperBound(1,129,1));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimePlaybackBridgeBuild::requiredSuccessorGrainsUpperBound(1,1,0));
}

void test_bridge_owns_every_reserved_ledger_closure_and_retires_receipts() {
  RuntimePlaybackView base;
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(RuntimePlaybackViewConfig::kEventsPerView));
  ActiveNoteLedger ledger;
  std::vector<MidiEvent> events;
  for (size_t i = 0; i < ActiveNoteLedger::kMaxOpenNotes; ++i) {
    auto on = MidiEvent::NoteOn(0, 1, i, 90); on.noteId = i + 1;
    auto off = MidiEvent::NoteOff(96, 1, i); off.noteId = i + 1;
    events.push_back(on); events.push_back(off);
    ledger.noteOn(1, i, i + 1, 0, 90);
    off.tick = 120;
    TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, uint32_t(2*i + 1), off}));
  }
  prepareBridgeBase(base, events);
  bridge.ledgerBoundary.capture(ledger);
  bridge.cursorAnchor.phase = 1;
  compileBridge(base, bridge);
  TEST_ASSERT_EQUAL_UINT32(ActiveNoteLedger::kMaxOpenNotes, bridge.heldClosureCount);
  for (size_t i = 0; i < ActiveNoteLedger::kMaxOpenNotes; ++i) {
    const auto* closure = bridge.heldClosure(i);
    TEST_ASSERT_NOT_NULL(closure);
    TEST_ASSERT_EQUAL_UINT64(96, closure->relativeTick);
    TEST_ASSERT_TRUE(RuntimeCompanionBridgeMap::inputEventsMatch(events[2*i + 1], closure->originalOff));
  }
  // Retiring a bridge invalidates receipts by count; no payload walk is needed.
  bridge.resetMetadata();
  TEST_ASSERT_EQUAL_UINT32(0, bridge.heldClosureCount);
  TEST_ASSERT_NULL(bridge.heldClosure(0));
  TEST_ASSERT_NULL(bridge.heldClosure(ActiveNoteLedger::kMaxOpenNotes));
  bridge.ledgerBoundary.clear();
  compileBridge(base, bridge);
  TEST_ASSERT_NULL(bridge.heldClosure(0)); // Reuse cannot expose a previous open instance.
  bridge.resetMetadata(); bridge.coverage = base.coverage;
  bridge.ledgerBoundary.capture(ledger);
  RuntimePlaybackBridgeBuild build;
  TEST_ASSERT_TRUE(build.begin(base, bridge));
  --bridge.ledgerBoundary.count;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(1));
  TEST_ASSERT_FALSE(bridge.complete);
}

void test_ranked_bridge_denies_reused_mapping_before_completion() {
  auto on = MidiEvent::NoteOn(0, 1, 60, 90); on.noteId = 1;
  RankedBridgeFixture fixture; fixture.prepare({on, MidiEvent::NoteOff(100, 1, 60)}, {lengthRow(1, 200)});
  auto& map = fixture.runtime.frozenCompanionBridgeMap;
  TEST_ASSERT_TRUE(fixture.build.begin(fixture.base, fixture.bridge, &map));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, fixture.build.step(1));
  map.discard();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, fixture.build.step(1));
  TEST_ASSERT_FALSE(fixture.bridge.complete); TEST_ASSERT_FALSE(fixture.bridge.streamPrepared);
  fixture.bridge.resetMetadata(); fixture.bridge.coverage = fixture.base.coverage;
  TEST_ASSERT_FALSE(fixture.build.begin(fixture.base, fixture.bridge, &map));
}

void test_bridge_merge_matches_complete_oracle_with_insert_suppress_replace() {
  const std::vector<MidiEvent> events{MidiEvent::NoteOn(10, 1, 60, 90),
      MidiEvent::NoteOff(30, 1, 60), MidiEvent::ControlChange(30, 1, 7, 80),
      MidiEvent::NoteOn(50, 1, 62, 70), MidiEvent::NoteOff(70, 1, 62)};
  RuntimePlaybackView base;
  prepareBridgeBase(base, events);
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(16));
  const MidiEvent replacement = MidiEvent::ControlChange(15, 1, 7, 20);
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 2, replacement}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress, 3, {}}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress, 4, {}}));
  const MidiEvent on = MidiEvent::NoteOn(30, 1, 64, 99);
  const MidiEvent off = MidiEvent::NoteOff(768, 1, 64);
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, on}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, off}));
  compileBridge(base, bridge);
  assertBridgeEqualsOracle(base, bridge, {events[0], events[1], replacement, on, off});
  TEST_ASSERT_FALSE(bridge.append({PlaybackBridgeOperation::Insert, 0, on}));
}

void test_bridge_cursor_skips_completed_phase_but_replays_on_next_cycle() {
  RuntimePlaybackView base;
  prepareBridgeBase(base, {MidiEvent::NoteOn(0, 1, 60, 100), MidiEvent::NoteOff(25, 1, 60)});
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(4));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, MidiEvent::NoteOff(768, 1, 61)}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, MidiEvent::NoteOn(25, 1, 61, 80)}));
  compileBridge(base, bridge);
  RuntimePlaybackBridgeCursor cursor;
  TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, 2, 0));
  TEST_ASSERT_TRUE(cursor.peek()->isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(25, cursor.peek()->tick);
  TEST_ASSERT_TRUE(cursor.advance());
  TEST_ASSERT_TRUE(cursor.peek()->isNoteOn());
  TEST_ASSERT_TRUE(cursor.advance());
  TEST_ASSERT_EQUAL_UINT16(1, cursor.emissionCursor().samePhaseOrdinal);
  TEST_ASSERT_NULL(cursor.peek());
  TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, 3));
  TEST_ASSERT_EQUAL_UINT32(768, cursor.peek()->tick);
  TEST_ASSERT_TRUE(cursor.advance());
  TEST_ASSERT_EQUAL_UINT32(3, cursor.emissionCursor().projectionCycle);
  TEST_ASSERT_EQUAL_UINT32(0, cursor.peek()->tick);
}

void test_bridge_build_denies_stale_source_and_invalid_target() {
  MidiEvent on = MidiEvent::NoteOn(5, 1, 60, 100);
  on.noteId = 42;
  MidiEvent off = MidiEvent::NoteOff(50, 1, 60);
  off.noteId = 42;
  RuntimePlaybackView base;
  prepareBridgeBase(base, {on, off});
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(4));
  bridge.coverage = base.coverage;
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress, 1, {}}));
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 42, 5, 100);
  bridge.ledgerBoundary.capture(ledger);
  RuntimePlaybackBridgeBuild build;
  TEST_ASSERT_TRUE(build.begin(base, bridge));
  while (build.step(8) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_TRUE(bridge.complete);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.baseEventCount);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.nextCycleBaseEventCount);
  TEST_ASSERT_EQUAL_UINT32(42, base.storage.events[1].noteId);
  bridge.resetMetadata();
  bridge.coverage = base.coverage;
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress, 999, {}}));
  TEST_ASSERT_TRUE(build.begin(base, bridge));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(8));
  bridge.resetMetadata();
  bridge.coverage = base.coverage;
  TEST_ASSERT_TRUE(build.begin(base, bridge));
  ++base.source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(8));
  TEST_ASSERT_FALSE(bridge.complete);
}

void test_bridge_merge_differential_varied_sources_and_long_suppression_runs() {
  for (size_t count : {size_t(0), size_t(1), size_t(16), size_t(129)}) {
    RuntimePlaybackView base;
    std::vector<MidiEvent> events;
    for (size_t i = 0; i < count; ++i) {
      MidiEvent event = i % 2 == 0 ? MidiEvent::NoteOn((i * 37) % 769, 1, 60, 90)
                                   : MidiEvent::NoteOff((i * 37) % 769, 1, 60);
      event.noteId = static_cast<NoteId>(i / 2 + 1);
      events.push_back(event);
    }
    prepareBridgeBase(base, events);
    FixedPlaybackBridgeStorage bridge;
    TEST_ASSERT_TRUE(bridge.prewarm(512));
    std::vector<MidiEvent> expected;
    for (size_t i = 0; i < count; ++i) {
      if (i % 17 == 0) expected.push_back(events[i]);
      else TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress,
                                           static_cast<uint32_t>(i), {}}));
    }
    for (size_t i = 0; i < 19; ++i) {
      const MidiEvent event = MidiEvent::ControlChange((i * 59) % 768, 1, 7, i);
      TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, event}));
      expected.push_back(event);
    }
    compileBridge(base, bridge);
    assertBridgeEqualsOracle(base, bridge, expected);
  }
}

void test_bridge_replacement_preserves_equal_key_provenance() {
  const MidiEvent first = MidiEvent::ControlChange(20, 1, 1, 10);
  const MidiEvent middle = MidiEvent::ControlChange(20, 1, 2, 20);
  const MidiEvent last = MidiEvent::ControlChange(20, 1, 3, 30);
  RuntimePlaybackView base;
  prepareBridgeBase(base, {first, middle, last});
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(4));
  const MidiEvent newFirst = MidiEvent::ControlChange(20, 1, 1, 11);
  const MidiEvent newLast = MidiEvent::ControlChange(20, 1, 3, 31);
  // Deliberately reverse record order. Replacement keeps the original base
  // provenance when its audible sort key is unchanged.
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 2, newLast}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 0, newFirst}));
  compileBridge(base, bridge);
  assertBridgeEqualsOracle(base, bridge, {newFirst, middle, newLast});
}

void test_bridge_stream_uses_existing_midi_dedup_and_resumes_after_future_event() {
  RuntimePlaybackView base;
  prepareBridgeBase(base, {MidiEvent::NoteOff(10, 1, 60), MidiEvent::NoteOn(20, 1, 61, 100)});
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(4));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, MidiEvent::NoteOff(10, 1, 60)}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, MidiEvent::ControlChange(30, 1, 7, 80)}));
  compileBridge(base, bridge);
  RuntimePlaybackBridgeStream merged;
  TEST_ASSERT_TRUE(merged.selectAfterPhase(base, bridge, 0));
  uint16_t index = 0;
  ProjectionContext projection{};
  projection.loopLength = 768;
  struct Trace { std::vector<MidiEvent> sent; size_t ledgerOnly = 0; } trace;
  auto send = [](void* context, const MidiEvent& event, uint8_t) {
    static_cast<Trace*>(context)->sent.push_back(event);
  };
  auto ledgerOnly = [](void* context, const MidiEvent&, uint8_t) {
    ++static_cast<Trace*>(context)->ledgerOnly;
  };
  for (uint32_t tick = 0; tick <= 30; ++tick) {
    const PlaybackTickFrame frame{&projection, tick, tick == 0 ? UINT32_MAX : tick - 1, tick == 0};
    TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
        advancePlaybackCursor({&index, nullptr}, frame, PlaybackEmitPolicy::ActiveCommitted,
                              merged.stream(), send, &trace, 0, nullptr, nullptr, 1, ledgerOnly));
    TEST_ASSERT_EQUAL_UINT16(index, merged.position);
  }
  TEST_ASSERT_EQUAL_UINT32(3, trace.sent.size());
  TEST_ASSERT_EQUAL_UINT32(1, trace.ledgerOnly);
  TEST_ASSERT_EQUAL_UINT32(4, index);
  TEST_ASSERT_NULL(merged.cursor.peek());
}

void test_bridge_candidate_build_keeps_selected_stream_readable() {
  RuntimePlaybackView base;
  prepareBridgeBase(base, {MidiEvent::ControlChange(20, 1, 7, 10)});
  FixedPlaybackBridgeStorage selected;
  FixedPlaybackBridgeStorage candidate;
  TEST_ASSERT_TRUE(selected.prewarm(4));
  TEST_ASSERT_TRUE(candidate.prewarm(4));
  TEST_ASSERT_TRUE(selected.append({PlaybackBridgeOperation::Insert, 0, MidiEvent::ControlChange(30, 1, 7, 20)}));
  compileBridge(base, selected);
  TEST_ASSERT_TRUE(candidate.append({PlaybackBridgeOperation::Insert, 0, MidiEvent::ControlChange(40, 1, 7, 30)}));
  candidate.coverage = base.coverage;
  candidate.source = {3, 3, nullptr};
  RuntimePlaybackBridgeBuild build;
  TEST_ASSERT_TRUE(build.begin(base, candidate));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(0));
  RuntimePlaybackOrderBuildResult result;
  do {
    result = build.step(1);
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
    assertBridgeEqualsOracle(base, selected,
        {base.storage.events[0], MidiEvent::ControlChange(30, 1, 7, 20)});
  } while (result == RuntimePlaybackOrderBuildResult::Pending);
  TEST_ASSERT_TRUE(selected.selected);
  candidate.selected = true;
  assertBridgeEqualsOracle(base, candidate,
      {base.storage.events[0], MidiEvent::ControlChange(40, 1, 7, 30)});
}

void test_runtime_bridge_selection_requires_complete_source_coverage_and_live_ledger() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& set = *runtime.viewSetIfPrewarmed();
  auto& base = set.views[set.lifecycle.current];
  prepareBridgeBase(base, {MidiEvent::NoteOn(10, 1, 60, 100), MidiEvent::NoteOff(20, 1, 60)});
  auto& candidate = set.bridges[set.bridgeLifecycle.candidate];
  TEST_ASSERT_TRUE(candidate.append({PlaybackBridgeOperation::Insert, 0,
                                      MidiEvent::ControlChange(30, 1, 7, 80)}));
  compileBridge(base, candidate, false);
  const PlaybackViewSourceIdentity target = candidate.source;
  const EmissionCursor earliest{0, 0, 0, 0, target.sourceGeneration};
  candidate.streamPrepared = false;
  TEST_ASSERT_FALSE(runtime.selectPreparedFullLoopBridge(target, 768, earliest));
  candidate.streamPrepared = true;
  TEST_ASSERT_FALSE(runtime.selectPreparedFullLoopBridge({9, 9, nullptr}, 768, earliest));
  TEST_ASSERT_FALSE(runtime.selectPreparedFullLoopBridge(target, 1536, earliest));
  runtime.ledger.noteOn(1, 60, 42, 10, 100);
  TEST_ASSERT_FALSE(runtime.selectPreparedFullLoopBridge(target, 768, earliest));
  runtime.ledger.clear();
  ++base.storageGeneration;
  TEST_ASSERT_FALSE(runtime.selectPreparedFullLoopBridge(target, 768, earliest));
  --base.storageGeneration; // Restore the test's original, still-owned base.
  TEST_ASSERT_TRUE(runtime.selectPreparedFullLoopBridge(target, 768, earliest));
  TEST_ASSERT_EQUAL_PTR(&candidate, runtime.selectedFullLoopBridge(target, 768));
  TEST_ASSERT_NULL(runtime.publishedFullLoopView(target, 768));
  ++base.storageGeneration;
  TEST_ASSERT_NULL(runtime.selectedFullLoopBridge(target, 768));
  --base.storageGeneration;
  ++base.source.playbackRevision;
  TEST_ASSERT_NULL(runtime.selectedFullLoopBridge(target, 768));
}

void test_runtime_bridge_cursor_survives_ticks_and_drains_tail_before_wrap() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& set = *runtime.viewSetIfPrewarmed();
  auto& base = set.views[set.lifecycle.current];
  prepareBridgeBase(base, {MidiEvent::NoteOn(10, 1, 60, 100), MidiEvent::NoteOff(700, 1, 60)});
  auto& bridge = set.bridges[set.bridgeLifecycle.candidate];
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0,
                                 MidiEvent::NoteOn(720, 1, 62, 80)}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0,
                                 MidiEvent::NoteOff(768, 1, 62)}));
  compileBridge(base, bridge, false);
  const auto target = bridge.source;
  TEST_ASSERT_TRUE(runtime.selectPreparedFullLoopBridge(target, 768, {0, 601, 0, 0, 2}));
  auto* stream = runtime.prepareFullLoopBridgeStream(target, 768, 600);
  TEST_ASSERT_NOT_NULL(stream);
  std::vector<uint32_t> sent;
  auto send = [](void* ctx, const MidiEvent& event, uint8_t) {
    static_cast<std::vector<uint32_t>*>(ctx)->push_back(event.tick);
  };
  ProjectionContext projection{};
  projection.loopLength = 768;
  auto advance = [&](uint32_t previous, uint32_t tick, bool start) {
    TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
        advancePlaybackCursor({&stream->playbackIndex, nullptr},
            {&projection, tick, previous, start}, PlaybackEmitPolicy::LayeredSlot,
            stream->stream(), send, &sent, 0, nullptr, nullptr, 1));
  };
  advance(600, 705, false);
  TEST_ASSERT_EQUAL_UINT32(1, sent.size());
  TEST_ASSERT_EQUAL_UINT32(700, sent[0]);
  TEST_ASSERT_EQUAL_PTR(stream, runtime.prepareFullLoopBridgeStream(target, 768, 705));
  TEST_ASSERT_EQUAL_UINT16(1, stream->playbackIndex);
  // A jump from 705 to phase 10 must first consume 720 in the old cycle.
  advance(705, 767, false);
  stream = runtime.prepareFullLoopBridgeStream(target, 768, UINT32_MAX, true);
  TEST_ASSERT_NOT_NULL(stream);
  advance(705, 10, true);
  TEST_ASSERT_EQUAL_UINT32(4, sent.size());
  TEST_ASSERT_EQUAL_UINT32(720, sent[1]);
  TEST_ASSERT_EQUAL_UINT32(768, sent[2]);
  TEST_ASSERT_EQUAL_UINT32(10, sent[3]);
  TEST_ASSERT_EQUAL_UINT32(1, stream->cursor.emissionCursor().projectionCycle);
  runtime.reset(true);
  TEST_ASSERT_NULL(runtime.selectedFullLoopBridge(target, 768));
  TEST_ASSERT_NULL(runtime.bridgeStream.cursor.peek());
}

void test_bridge_keeps_open_note_closure_until_changed_pair_next_cycle() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& set = *runtime.viewSetIfPrewarmed();
  auto& base = set.views[set.lifecycle.current];
  MidiEvent on = MidiEvent::NoteOn(10, 1, 60, 90);
  MidiEvent off = MidiEvent::NoteOff(100, 1, 60);
  on.noteId = off.noteId = 42;
  prepareBridgeBase(base, {on, off});
  runtime.ledger.noteOn(1, 60, 42, 10, 90);
  auto& bridge = set.bridges[set.bridgeLifecycle.candidate];
  bridge.ledgerBoundary.capture(runtime.ledger);
  MidiEvent replacementOn = MidiEvent::NoteOn(20, 1, 60, 80);
  MidiEvent replacementOff = MidiEvent::NoteOff(30, 1, 60);
  replacementOn.noteId = replacementOff.noteId = 42;
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 0, replacementOn}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 1, replacementOff}));
  compileBridge(base, bridge, false);
  runtime.bridgeProjectionCycle = 5;
  TEST_ASSERT_FALSE(runtime.selectPreparedFullLoopBridge(bridge.source, 768, {5, 101, 0, 0, 2}));
  // Admission can happen later than construction. Its own cycle owns the
  // retained base Off; using the construction cycle would orphan this On.
  TEST_ASSERT_TRUE(runtime.selectPreparedFullLoopBridge(bridge.source, 768, {5, 16, 0, 0, 2}));
  auto* stream = runtime.prepareFullLoopBridgeStream(bridge.source, 768, 15);
  TEST_ASSERT_NOT_NULL(stream);
  TEST_ASSERT_EQUAL_UINT32(100, stream->cursor.peek()->tick);
  // Normal-stop bookkeeping runs while this original NoteOn is still held.
  // Exercise the production cleanup body, including a retry before the snapshot
  // receipt is available. Neither attempt may restart or silence the stream.
  Loop loop; loop.loopLengthTicks = 768;
  loop.openOverdubSession(15);
  PendingStopAdmission stop;
  stop.pending = true;
  stop.destination = PendingStopDestination::Playing;
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  const auto playbackIndex = stream->playbackIndex;
  TEST_ASSERT_FALSE(TrackPreparedStopCommit::completeDeferredCleanup(loop, runtime, stop, stopCleanupPreview, true));
  TEST_ASSERT_TRUE(stop.pending);
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_TRUE(runtime.ledger.isActive(1, 60));
  TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  stop.historyRecycleOwner = &loop;
  TEST_ASSERT_FALSE(TrackPreparedStopCommit::completeDeferredCleanup(loop, runtime, stop, stopCleanupPreview, true));
  stop.historyRecycleOwner = nullptr;
  TEST_ASSERT_TRUE(TrackPreparedStopCommit::completeDeferredCleanup(loop, runtime, stop, stopCleanupPreview, true));
  TEST_ASSERT_FALSE(stop.pending);
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  // The receipt ring gate is a free slot, not an unconsumed singleton: the
  // handoff queued exactly one receipt and remains open for a successor.
  TEST_ASSERT_TRUE(loop.hasPendingSessionExitReceipt());
  TEST_ASSERT_EQUAL(1, loop.sessionExitReceiptCount());
  TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());
  TEST_ASSERT_TRUE(runtime.ledger.isActive(1, 60));
  TEST_ASSERT_EQUAL_PTR(stream, runtime.prepareFullLoopBridgeStream(bridge.source, 768, 15));
  TEST_ASSERT_EQUAL_UINT16(playbackIndex, stream->playbackIndex);
  TEST_ASSERT_EQUAL_UINT32(100, stream->cursor.peek()->tick);
  TEST_ASSERT_FALSE(TrackPreparedStopCommit::completeDeferredCleanup(loop, runtime, stop, stopCleanupPreview, true));
  TEST_ASSERT_TRUE(runtime.ledger.applyPlaybackEvent(1, *stream->cursor.peek()));
  TEST_ASSERT_TRUE(stream->cursor.advance());
  TEST_ASSERT_FALSE(runtime.ledger.isActive(1, 60));
  TEST_ASSERT_NULL(stream->cursor.peek());
  stream = runtime.prepareFullLoopBridgeStream(bridge.source, 768, UINT32_MAX, true);
  TEST_ASSERT_EQUAL_UINT32(20, stream->cursor.peek()->tick);
  TEST_ASSERT_TRUE(runtime.ledger.applyPlaybackEvent(1, *stream->cursor.peek()));
  TEST_ASSERT_TRUE(stream->cursor.advance());
  TEST_ASSERT_EQUAL_UINT32(30, stream->cursor.peek()->tick);
  TEST_ASSERT_TRUE(runtime.ledger.applyPlaybackEvent(1, *stream->cursor.peek()));
  TEST_ASSERT_TRUE(stream->cursor.advance());
  TEST_ASSERT_NULL(stream->cursor.peek());
  TEST_ASSERT_FALSE(runtime.ledger.isActive(1, 60));
}

void test_bridge_retains_cross_wrap_closure_and_retires_it_once() {
  for (uint32_t initialCycle : {0u, 3u}) {
    LoopPlaybackRuntime runtime;
    runtime.bridgeProjectionCycle = initialCycle;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    auto& set = *runtime.viewSetIfPrewarmed();
    auto& base = set.views[set.lifecycle.current];
    MidiEvent on = MidiEvent::NoteOn(700, 1, 60, 90);
    MidiEvent off = MidiEvent::NoteOff(768, 1, 60);
    on.noteId = off.noteId = 42;
    prepareBridgeBase(base, {on, off});
    runtime.ledger.noteOn(1, 60, 42, 700, 90);
    auto& candidate = set.bridges[set.bridgeLifecycle.candidate];
    candidate.ledgerBoundary.capture(runtime.ledger);
    candidate.cursorAnchor = runtime.currentCycleCursor(701, 2);
    TEST_ASSERT_TRUE(candidate.append({PlaybackBridgeOperation::Suppress, 0, {}}));
    TEST_ASSERT_TRUE(candidate.append({PlaybackBridgeOperation::Suppress, 1, {}}));
    compileBridge(base, candidate, false);
    TEST_ASSERT_FALSE(runtime.selectPreparedFullLoopBridge(candidate.source, 768, runtime.currentCycleCursor(600, 2)));
    TEST_ASSERT_TRUE(runtime.selectPreparedFullLoopBridge(candidate.source, 768, runtime.currentCycleCursor(701, 2)));
    auto* stream = runtime.prepareFullLoopBridgeStream(candidate.source, 768, 700);
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_NULL(stream->cursor.peek());
    stream = runtime.prepareFullLoopBridgeStream(candidate.source, 768, UINT32_MAX, true);
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_UINT32(768, stream->cursor.peek()->tick);
    TEST_ASSERT_TRUE(runtime.ledger.applyPlaybackEvent(1, *stream->cursor.peek()));
    TEST_ASSERT_TRUE(stream->cursor.advance());
    TEST_ASSERT_NULL(stream->cursor.peek());
    TEST_ASSERT_FALSE(runtime.ledger.isActive(1, 60));
    stream = runtime.prepareFullLoopBridgeStream(candidate.source, 768, UINT32_MAX, true);
    TEST_ASSERT_NULL(stream->cursor.peek());
    stream = runtime.prepareFullLoopBridgeStream(candidate.source, 768, UINT32_MAX, true);
    TEST_ASSERT_NULL(stream->cursor.peek());
    TEST_ASSERT_EQUAL_UINT32(42, base.storage.events[1].noteId);
  }
}

void test_bridge_cross_wrap_replacement_trace_matches_safe_note_instances() {
  // A changed pair may start after the original closure in the first wrap,
  // or need the following On occurrence. Its own Off can cross another wrap.
  const uint32_t replacements[][2] = {{200, 300}, {50, 80}, {200, 50}, {50, 20}, {100, 100}};
  for (const auto& ticks : replacements) {
    RuntimePlaybackView base;
    MidiEvent on = MidiEvent::NoteOn(700, 1, 60, 90);
    MidiEvent off = MidiEvent::NoteOff(100, 1, 60);
    on.noteId = off.noteId = 42;
    prepareBridgeBase(base, {on, off});
    FixedPlaybackBridgeStorage bridge;
    TEST_ASSERT_TRUE(bridge.prewarm(4));
    ActiveNoteLedger ledger;
    ledger.noteOn(1, 60, 42, 700, 90);
    bridge.ledgerBoundary.capture(ledger);
    bridge.cursorAnchor.phase = 701;
    MidiEvent newOn = MidiEvent::NoteOn(ticks[0], 1, 62, 80);
    MidiEvent newOff = MidiEvent::NoteOff(ticks[1], 1, 62);
    newOn.noteId = newOff.noteId = 43; // Replacement provenance preserves the old closing path.
    TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 0, newOn}));
    TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 1, newOff}));
    compileBridge(base, bridge);
    std::vector<uint32_t> actual;
    std::vector<uint32_t> expected{768 + 100};
    uint32_t start = 768 + ticks[0];
    if (ticks[0] < 100) start += 768;
    for (uint32_t t = start; t < 5 * 768; t += 768) {
      expected.push_back(t * 2 + 1);
      const uint32_t end = t + (ticks[1] > ticks[0] ? ticks[1] - ticks[0] : 768 + ticks[1] - ticks[0]);
      if (end < 5 * 768) expected.push_back(end * 2);
    }
    expected[0] *= 2;
    std::sort(expected.begin(), expected.end());
    RuntimePlaybackBridgeCursor cursor;
    for (uint32_t cycle = 0; cycle < 5; ++cycle) {
      TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, cycle, cycle == 0 ? 700 : UINT32_MAX));
      while (const auto* event = cursor.peek()) {
        actual.push_back((cycle * 768 + event->tick % 768) * 2 + (event->isNoteOn() ? 1 : 0));
        if (event->isNoteOn()) TEST_ASSERT_FALSE(ledger.isActive(1, event->data.noteData.note));
        else TEST_ASSERT_TRUE(ledger.isActive(1, event->data.noteData.note));
        TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, *event));
        TEST_ASSERT_TRUE(cursor.advance());
      }
    }
    TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); ++i) TEST_ASSERT_EQUAL_UINT32(expected[i], actual[i]);
  }
}

void test_bridge_open_note_repeated_identity_pairs_keep_their_own_closures() {
  RuntimePlaybackView base;
  MidiEvent on = MidiEvent::NoteOn(700, 1, 60, 90);
  MidiEvent off = MidiEvent::NoteOff(100, 1, 60);
  on.noteId = off.noteId = 42;
  prepareBridgeBase(base, {on, off});
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(8));
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 42, 700, 90);
  bridge.ledgerBoundary.capture(ledger);
  bridge.cursorAnchor.phase = 701;
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress, 0, {}}));
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress, 1, {}}));
  for (uint32_t tick : {20u, 40u, 200u, 300u}) {
    auto event = tick == 20 || tick == 200 ? MidiEvent::NoteOn(tick, 1, 60, 90) : MidiEvent::NoteOff(tick, 1, 60);
    event.noteId = 42;
    TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Insert, 0, event}));
  }
  compileBridge(base, bridge);
  RuntimePlaybackBridgeCursor cursor;
  const std::vector<std::vector<uint32_t>> expected{{}, {100, 200, 300}, {20, 40, 200, 300}, {20, 40, 200, 300}};
  for (uint32_t cycle = 0; cycle < expected.size(); ++cycle) {
    TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, cycle, cycle == 0 ? 700 : UINT32_MAX));
    for (uint32_t tick : expected[cycle]) {
      TEST_ASSERT_NOT_NULL(cursor.peek());
      TEST_ASSERT_EQUAL_UINT32(tick, cursor.peek()->tick);
      TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, *cursor.peek()));
      TEST_ASSERT_TRUE(cursor.advance());
    }
    TEST_ASSERT_NULL(cursor.peek());
  }
  TEST_ASSERT_FALSE(ledger.isActive(1, 60));
}

void test_bridge_safe_boundary_differential_across_admission_and_pair_phases() {
  for (uint32_t admission : {0u, 15u, 701u}) {
    for (uint32_t closing : {0u, 100u, 767u}) {
      for (uint32_t start : {0u, 50u, 200u, 767u}) {
        for (uint32_t end : {0u, 20u, 300u, 767u}) {
          RuntimePlaybackView base;
          auto oldOn = MidiEvent::NoteOn((admission + 758) % 768, 1, 60, 90);
          auto oldOff = MidiEvent::NoteOff(closing, 1, 60);
          oldOn.noteId = oldOff.noteId = 42;
          prepareBridgeBase(base, {oldOn, oldOff});
          FixedPlaybackBridgeStorage bridge;
          TEST_ASSERT_TRUE(bridge.prewarm(2));
          ActiveNoteLedger ledger;
          ledger.noteOn(1, 60, 42, oldOn.tick, 90);
          bridge.ledgerBoundary.capture(ledger);
          bridge.cursorAnchor.phase = admission;
          auto on = MidiEvent::NoteOn(start, 1, 60, 80);
          auto off = MidiEvent::NoteOff(end, 1, 60);
          on.noteId = off.noteId = 42;
          TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 0, on}));
          TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Replace, 1, off}));
          compileBridge(base, bridge);
          const uint32_t closure = closing + (closing < admission ? 768 : 0);
          uint32_t firstOn = start;
          while (firstOn < closure) firstOn += 768;
          std::vector<uint32_t> expected{closure * 2};
          for (uint32_t t = firstOn; t < 5 * 768; t += 768) {
            expected.push_back(t * 2 + 1);
            const uint32_t offTime = t + (end > start ? end - start : 768 + end - start);
            if (offTime < 5 * 768) expected.push_back(offTime * 2);
          }
          std::sort(expected.begin(), expected.end());
          std::vector<uint32_t> actual;
          RuntimePlaybackBridgeCursor cursor;
          for (uint32_t cycle = 0; cycle < 5; ++cycle) {
            TEST_ASSERT_TRUE(cursor.selectAfterPhase(base, bridge, cycle,
                cycle == 0 && admission != 0 ? admission - 1 : UINT32_MAX));
            while (const auto* event = cursor.peek()) {
              actual.push_back((cycle * 768 + event->tick % 768) * 2 + (event->isNoteOn() ? 1 : 0));
              if (event->isNoteOn()) TEST_ASSERT_FALSE(ledger.isActive(1, 60));
              else TEST_ASSERT_TRUE(ledger.isActive(1, 60));
              TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, *event));
              TEST_ASSERT_TRUE(cursor.advance());
            }
          }
          TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
          for (size_t i = 0; i < expected.size(); ++i) TEST_ASSERT_EQUAL_UINT32(expected[i], actual[i]);
        }
      }
    }
  }
}

void test_bridge_missing_original_closure_cannot_bind_in_a_later_cycle() {
  RuntimePlaybackView base;
  auto on = MidiEvent::NoteOn(10, 1, 60, 90);
  on.noteId = 42;
  prepareBridgeBase(base, {on});
  FixedPlaybackBridgeStorage bridge;
  TEST_ASSERT_TRUE(bridge.prewarm(2));
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 42, 10, 90);
  bridge.ledgerBoundary.capture(ledger);
  TEST_ASSERT_TRUE(bridge.append({PlaybackBridgeOperation::Suppress, 0, {}}));
  compileBridge(base, bridge);
  TEST_ASSERT_FALSE(bridge.hasRetainedClosure);
  RuntimePlaybackBridgeCursor cursor;
  TEST_ASSERT_FALSE(cursor.selectAfterPhase(base, bridge, 0, 10));
  TEST_ASSERT_FALSE(cursor.selectAfterPhase(base, bridge, 1));
  TEST_ASSERT_FALSE(cursor.selectAfterPhase(base, bridge, 3));
}

void test_deferred_stop_owner_resumes_canonical_rewrite_and_audible_turns() {
  for (PendingStopPhase interrupted : {PendingStopPhase::CanonicalOrder,
                                      PendingStopPhase::CanonicalRewrite, PendingStopPhase::AudibleOrder}) {
    LoopPlaybackRuntime runtime;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    auto& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
    LoopEventStore capture;
    for (uint32_t tick : {900u, 4u, 800u, 30u}) {
      auto event = MidiEvent::ControlChange(tick, 1, 1, tick % 128);
      TEST_ASSERT_TRUE(staging.storage.append(event));
      TEST_ASSERT_TRUE(capture.append(event));
    }
    staging.captureGeneration = 71;
    TEST_ASSERT_TRUE(staging.freeze(71));
    PendingStopAdmission stop;
    const StopAdmissionContract contract{128, 3072, 1, 1};
    TEST_ASSERT_TRUE(stop.begin(71, 901, 12, 0, 4, contract));
    bool stopped = false;
    size_t turns = 0;
    while (stop.phase != PendingStopPhase::PrepareTransfer && turns++ < 200) {
      if (!stopped && stop.phase == interrupted) {
        stop.requestTransportStop();
        stopped = true;
      }
      if (stop.phase == PendingStopPhase::CanonicalOrder) {
        // Exactly the production owner protocol: begin/check on EVERY turn,
        // then consume one grain. The original active-build check stalled it.
        TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(71));
        const auto result = runtime.stepFrozenCaptureCanonicalOrder(1);
        TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
        TEST_ASSERT_TRUE(stop.consumeOrderGrain());
        if (result == RuntimePlaybackOrderBuildResult::Complete) stop.setPhase(PendingStopPhase::CanonicalRewrite);
      } else if (stop.phase == PendingStopPhase::CanonicalRewrite) {
        const auto result = runtime.stepFrozenCaptureCanonicalRewrite(capture, 71, 1);
        TEST_ASSERT_NOT_EQUAL(LoopEventStoreRewriteResult::Denied, result);
        TEST_ASSERT_TRUE(stop.consumeOrderGrain());
        if (result == LoopEventStoreRewriteResult::Complete) {
          TEST_ASSERT_EQUAL_UINT32(4, capture.at(0).tick);
          TEST_ASSERT_EQUAL_UINT32(900, capture.at(3).tick);
          stop.setPhase(PendingStopPhase::AudibleOrder);
        }
      } else {
        TEST_ASSERT_EQUAL(PendingStopPhase::AudibleOrder, stop.phase);
        TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 71));
        const auto result = runtime.stepFrozenCaptureOrder(1);
        TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
        TEST_ASSERT_TRUE(stop.consumeOrderGrain());
        if (result == RuntimePlaybackOrderBuildResult::Complete) stop.setPhase(PendingStopPhase::PrepareTransfer);
      }
    }
    TEST_ASSERT_LESS_THAN_UINT32(200, turns);
    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_TRUE(runtime.frozenCaptureCandidateReady(71));
    TEST_ASSERT_EQUAL_UINT32(4, capture.size());
    TEST_ASSERT_EQUAL_UINT32(4, staging.storage.eventCount);
    while (stop.remainingOrderGrains) TEST_ASSERT_TRUE(stop.consumeOrderGrain());
    TEST_ASSERT_FALSE(stop.canCommit(12, PendingStopDestination::Playing));
    TEST_ASSERT_TRUE(stop.canCommit(13, PendingStopDestination::Stopped));
  }
}

void test_frozen_order_binding_rejects_changed_generation_count_and_projection() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(800, 1, 1, 2)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(40, 1, 1, 3)));
  staging.captureGeneration = 81;
  TEST_ASSERT_TRUE(staging.freeze(81));
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 81));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepFrozenCaptureOrder(1));
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 81));
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(1536, 81));
  // Canonical capture order is a distinct owner. It may prepare while the
  // ordinary audible order is in flight for the same frozen capture.
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(81));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending,
                    runtime.stepFrozenCaptureCanonicalOrder(1));
  // Simulate an invalidated source between owner turns. Neither begin nor a
  // direct step may continue writing indexes for the old captured identity.
  staging.captureGeneration = staging.frozenGeneration = 82;
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(768, 82));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureOrder(1));
  TEST_ASSERT_NOT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(82));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied,
                    runtime.stepFrozenCaptureCanonicalOrder(1));
  runtime.discardFrozenCaptureCanonicalOrder();
  runtime.discardFrozenCaptureOrder();
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 82));
  while (!runtime.frozenCaptureOrderComplete()) (void)runtime.stepFrozenCaptureOrder(1);
  TEST_ASSERT_TRUE(runtime.frozenCaptureCandidateReady(82));
  staging.captureGeneration = staging.frozenGeneration = 83;
  TEST_ASSERT_FALSE(runtime.frozenCaptureCandidateReady(83));
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(768, 83));
  runtime.discardFrozenCaptureOrder();
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(83));
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(768, 83));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepFrozenCaptureCanonicalOrder(1));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(10, 1, 1, 4)));
  TEST_ASSERT_NOT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready, runtime.beginFrozenCaptureCanonicalOrder(83));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureCanonicalOrder(1));
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::ControlChange(99, 1, 1, 5)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, runtime.stepFrozenCaptureCanonicalRewrite(store, 83, 1));
  TEST_ASSERT_EQUAL_UINT32(99, store.at(0).tick);
  TEST_ASSERT_FALSE(runtime.reset(true)); // an independently frozen interval still owns its role
  TEST_ASSERT_TRUE(runtime.frozenCaptureCanonicalOrderBinding.bound);
  staging.reset(); // explicit fixture owner release, before the runtime may reset metadata
  TEST_ASSERT_TRUE(runtime.reset(true));
  TEST_ASSERT_FALSE(runtime.frozenCaptureOrderBinding.bound);
  TEST_ASSERT_FALSE(runtime.frozenCaptureCanonicalOrderBinding.bound);
}

/// RED (2026-10-27): the `CanonicalOrder` phase's **only** re-bind site must not refuse forever on a
/// binding that names another interval. Device shape this reproduces: a stop job pinned at
/// `PendingStopPhase::CanonicalOrder` with `closure=0`, reported as `stop_job_pending` on every later
/// press, with no build in flight -- and because `serviceAcceptedInteractionTurn` yields while a job is
/// pending, that one job refused every subsequent stop. Before this change the second `begin` below
/// returned false on every turn, by construction.
void test_a_stale_canonical_binding_for_another_interval_rebinds_instead_of_wedging() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOff(40, 1, 60, 0)));
  staging.captureGeneration = staging.frozenGeneration = 81;
  TEST_ASSERT_TRUE(staging.freeze(81));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready,
                    runtime.beginFrozenCaptureCanonicalOrder(81));
  auto first = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 100 && first == RuntimePlaybackOrderBuildResult::Pending; ++i)
    first = runtime.stepFrozenCaptureCanonicalOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, first);
  // The next interval freezes the same role. Nothing in flight describes these rows, so refusing
  // here is the wedge: the binding cannot re-bind from anywhere else.
  staging.captureGeneration = staging.frozenGeneration = 82;
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready,
                    runtime.beginFrozenCaptureCanonicalOrder(82));
  auto rebuilt = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 100 && rebuilt == RuntimePlaybackOrderBuildResult::Pending; ++i)
    rebuilt = runtime.stepFrozenCaptureCanonicalOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, rebuilt);
  // Stale-interval control: the rebuild describes the current rows only. The retired interval is
  // not resurrected by the re-bind, either as a candidate or as an ordered projection.
  TEST_ASSERT_EQUAL_UINT32(2, runtime.frozenCaptureCanonicalOrder.orderedEventCount());
  TEST_ASSERT_FALSE(runtime.frozenCaptureCandidateReady(81));
  TEST_ASSERT_FALSE(runtime.frozenCaptureCandidateReady(82)); // no audible order was built
}

/// Negative control: same binding, same interval, but the shared index-scratch epoch moved under it
/// (the audible build took it). That is a real in-flight invalidation, so the phase must stay refused
/// -- the self-heal above may never silently rebuild a projection it cannot reproduce.
void test_a_same_interval_binding_whose_storage_epoch_moved_stays_refused() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOff(40, 1, 60, 0)));
  staging.captureGeneration = staging.frozenGeneration = 81;
  TEST_ASSERT_TRUE(staging.freeze(81));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready,
                    runtime.beginFrozenCaptureCanonicalOrder(81));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (unsigned i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = runtime.stepFrozenCaptureCanonicalOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(100, 81)); // retires the shared epoch
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::StagingContentMoved,
                    runtime.beginFrozenCaptureCanonicalOrder(81));
  // Refused, and the binding was not thrown away behind a live decision.
  TEST_ASSERT_TRUE(runtime.frozenCaptureCanonicalOrderBinding.bound);
}

/// Negative control: a build owns the scratch. The binding here names *another* interval, i.e. exactly
/// the case that self-heals when idle -- so this pins that the self-heal never discards work in flight.
void test_a_build_in_flight_keeps_the_stale_binding_refused() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& staging = runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::NoteOff(40, 1, 60, 0)));
  staging.captureGeneration = staging.frozenGeneration = 81;
  TEST_ASSERT_TRUE(staging.freeze(81));
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::Ready,
                    runtime.beginFrozenCaptureCanonicalOrder(81));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending,
                    runtime.stepFrozenCaptureCanonicalOrder(1));
  staging.captureGeneration = staging.frozenGeneration = 82;
  TEST_ASSERT_EQUAL(FrozenCaptureCanonicalOrderBegin::BindingBusy,
                    runtime.beginFrozenCaptureCanonicalOrder(82));
  TEST_ASSERT_TRUE(runtime.frozenCaptureCanonicalOrderBinding.bound);
  TEST_ASSERT_EQUAL_UINT32(81, runtime.frozenCaptureCanonicalOrderBinding.generation);
}

/// DEC-165: a deferred stop holds the FIFO head and the ring holds eight, so a preparation phase that
/// cannot advance turns "cannot stop overdubbing" into "cannot stop at all" once the ring fills. These
/// pin the decision table, not a fabricated staging set: the boundaries are the only place a wrong
/// number can become either a permanent wedge or a lost companion representation.
void test_stop_preparation_bounds_fire_only_where_an_escape_keeps_the_content() {
  const auto phase = [](PendingStopPhase p) { return static_cast<uint8_t>(p); };
  const uint8_t base = phase(PendingStopPhase::CanonicalPrefix);
  // The base is bounded only while a successor interval captures: that is the measured device state,
  // and a bare count is unsafe (the 128-cycle fixtures declare 5e8 preparation grains and three of them
  // failed with a fixed 2,048-grain budget). Without a successor the old, unbounded treatment stands.
  TEST_ASSERT_FALSE(stopPreparationIsBounded(base, false, false));
  TEST_ASSERT_EQUAL(StopPreparationEscape::None,
                    stopPreparationEscape(base, UINT16_MAX, false, false));
  TEST_ASSERT_TRUE(stopPreparationIsBounded(base, false, true));
  // With a successor capturing, the healthy builds measured on device (68..471 grains) never fire ...
  TEST_ASSERT_EQUAL(StopPreparationEscape::None, stopPreparationEscape(base, 471, false, true));
  TEST_ASSERT_EQUAL(StopPreparationEscape::None,
                    stopPreparationEscape(base, kCanonicalBaseStallGrainLimit - 1, false, true));
  // ... and the stalls measured 9,370 / 9,607 grains without advancing, so they do.
  TEST_ASSERT_EQUAL(StopPreparationEscape::AbandonBase,
                    stopPreparationEscape(base, kCanonicalBaseStallGrainLimit, false, true));
  TEST_ASSERT_EQUAL(StopPreparationEscape::AbandonBase, stopPreparationEscape(base, 9607, false, true));
  // The companion family is bounded ONLY for a retargeted job (DEC-070, unchanged).
  const uint8_t tail = phase(PendingStopPhase::CanonicalTail);
  TEST_ASSERT_EQUAL(StopPreparationEscape::None, stopPreparationEscape(tail, 233, false, true));
  TEST_ASSERT_EQUAL(StopPreparationEscape::None,
                    stopPreparationEscape(phase(PendingStopPhase::CompanionBridge), UINT16_MAX,
                                          false, true));
  TEST_ASSERT_EQUAL(StopPreparationEscape::None,
                    stopPreparationEscape(tail, kRetargetedCompanionStallGrainLimit - 1, true, true));
  TEST_ASSERT_EQUAL(StopPreparationEscape::RawPassOnly,
                    stopPreparationEscape(tail, kRetargetedCompanionStallGrainLimit, true, true));
  // The full contract per phase: bounded only where the escape cannot cost rows.
  struct PhaseExpectation {
    PendingStopPhase phase;
    bool boundedWhenPlaying;
    bool boundedWhenRetargeted;
  };
  const PhaseExpectation expectations[] = {
      {PendingStopPhase::ReservedClosure, false, false},
      {PendingStopPhase::CanonicalPrefix, false, false},   // (playing: gated on the cause above)
      {PendingStopPhase::CanonicalOrder, false, false},
      {PendingStopPhase::CanonicalRewrite, false, false},
      {PendingStopPhase::AudibleOrder, false, false},
      {PendingStopPhase::CanonicalTail, false, true},
      {PendingStopPhase::PrepareTransfer, false, true},
      {PendingStopPhase::CompanionHistory, false, true},
      {PendingStopPhase::UndoCollection, false, true},
      {PendingStopPhase::CompanionBridge, false, true},
      {PendingStopPhase::AtomicTransfer, false, false},
      {PendingStopPhase::DeferredCleanup, false, false},
  };
  for (const auto& expected : expectations) {
    const uint8_t code = phase(expected.phase);
    TEST_ASSERT_EQUAL(expected.boundedWhenPlaying,
                      stopPreparationIsBounded(code, false, false));
    TEST_ASSERT_EQUAL(expected.boundedWhenRetargeted,
                      stopPreparationIsBounded(code, true, false));
    if (!expected.boundedWhenRetargeted)
      TEST_ASSERT_EQUAL(StopPreparationEscape::None,
                        stopPreparationEscape(code, UINT16_MAX, true, false));
  }
  // The cause-based partner: once the base was abandoned the companion tail can never begin, so the
  // stop continues deterministically -- for a PLAYING job only, and only in the companion phases.
  for (PendingStopPhase companion :
       {PendingStopPhase::CanonicalTail, PendingStopPhase::CompanionHistory,
        PendingStopPhase::CompanionBridge}) {
    const uint8_t code = phase(companion);
    TEST_ASSERT_TRUE(stopCompanionPathIsUnreachable(code, false, true));    // base abandoned
    TEST_ASSERT_FALSE(stopCompanionPathIsUnreachable(code, false, false));  // base ready: reachable
    TEST_ASSERT_FALSE(stopCompanionPathIsUnreachable(code, true, true));    // retargeted: DEC-070's
  }
  for (PendingStopPhase other :
       {PendingStopPhase::CanonicalPrefix, PendingStopPhase::CanonicalOrder,
        PendingStopPhase::CanonicalRewrite, PendingStopPhase::PrepareTransfer,
        PendingStopPhase::UndoCollection, PendingStopPhase::AtomicTransfer}) {
    TEST_ASSERT_FALSE(stopCompanionPathIsUnreachable(phase(other), false, true));
  }
}

void test_order_grains_count_filtered_inputs_and_finish_within_reserved_turns() {
  for (size_t count : {0u, 1u, 3u, 4u, 17u, 129u, 2048u}) {
    for (size_t quantum : {1u, 3u, 32u}) {
      for (bool filtered : {false, true}) {
        FixedPlaybackEventStorage storage;
        TEST_ASSERT_TRUE(storage.prewarm(2048));
        for (size_t i = 0; i < count; ++i) {
          TEST_ASSERT_TRUE(storage.append(MidiEvent::ControlChange(count - i, 1, 1, i % 128)));
          if (filtered) TEST_ASSERT_TRUE(storage.discardEvent(i));
        }
        RuntimePlaybackOrderBuilder order;
        TEST_ASSERT_TRUE(order.beginCanonicalCapture(storage));
        const size_t bound = RuntimePlaybackOrderBuilder::requiredGrains(count, quantum);
        size_t turns = 0;
        while (!order.complete() && turns <= bound) {
          TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, order.step(quantum));
          ++turns;
        }
        TEST_ASSERT_TRUE(order.complete());
        TEST_ASSERT_TRUE(turns <= bound);
        if (filtered) TEST_ASSERT_EQUAL_UINT32((count + quantum - 1) / quantum, turns);
        TEST_ASSERT_EQUAL_UINT32(filtered ? 0 : count, order.orderedEventCount());
      }
    }
  }
}

void test_capture_bridge_fill_preserves_frozen_events_and_companion_overlay() {
  RuntimePlaybackView base;
  auto oldOn = MidiEvent::NoteOn(10, 1, 60, 90);
  auto oldOff = MidiEvent::NoteOff(40, 1, 60);
  oldOn.noteId = oldOff.noteId = 1;
  prepareBridgeBase(base, {oldOn, oldOff});
  auto shortenedOff = MidiEvent::NoteOff(30, 1, 60);
  shortenedOff.noteId = 1;
  const PlaybackBridgeRecord companion{PlaybackBridgeOperation::Replace, 1, shortenedOff};
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  auto on = MidiEvent::NoteOn(20, 1, 62, 80);
  auto off = MidiEvent::NoteOff(25, 1, 62);
  on.noteId = off.noteId = 2;
  TEST_ASSERT_TRUE(staging.appendContinuationNoteOn(on));
  staging.captureGeneration = 91;
  TEST_ASSERT_TRUE(staging.freeze(91)); // Append-and-stop, with no intervening fill grain.
  FixedPlaybackBridgeStorage candidate;
  TEST_ASSERT_TRUE(candidate.prewarm(3));
  candidate.coverage = base.coverage;
  candidate.source = {2, 2, nullptr};
  RuntimeCaptureBridgeFill fill;
  TEST_ASSERT_FALSE(fill.begin(staging, 91, &companion, 1, candidate)); // Reserved Off still due.
  TEST_ASSERT_TRUE(staging.appendReservedNoteOff(off));
  TEST_ASSERT_TRUE(fill.begin(staging, 91, &companion, 1, candidate));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, fill.step(1));
  TEST_ASSERT_EQUAL_UINT32(1, candidate.recordCount);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, fill.step(1));
  TEST_ASSERT_EQUAL_UINT32(2, candidate.recordCount);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, fill.step(1));
  TEST_ASSERT_EQUAL_UINT32(3, candidate.recordCount);
  TEST_ASSERT_FALSE(candidate.complete); // Filling alone is never ready for publication.
  TEST_ASSERT_EQUAL_UINT32(2, staging.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(20, staging.storage.events[0].tick);
  TEST_ASSERT_EQUAL_UINT32(25, staging.storage.events[1].tick);
  compileBridge(base, candidate);
  assertBridgeEqualsOracle(base, candidate, {oldOn, shortenedOff, on, off});
}

void test_capture_bridge_fill_denials_preserve_capture_and_unpublished_candidate() {
  CaptureAdmissionStaging staging;
  TEST_ASSERT_TRUE(staging.prewarm());
  for (uint32_t tick : {10u, 20u, 30u, 40u}) {
    TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(tick, 1, 1, 5)));
    TEST_ASSERT_TRUE(staging.storage.discardEvent(staging.storage.eventCount - 1));
  }
  staging.captureGeneration = 92;
  TEST_ASSERT_TRUE(staging.freeze(92));
  FixedPlaybackBridgeStorage candidate;
  TEST_ASSERT_TRUE(candidate.prewarm(4));
  candidate.coverage = {0, 768, 768};
  const PlaybackBridgeRecord extra{};
  RuntimeCaptureBridgeFill fill;
  TEST_ASSERT_FALSE(fill.begin(staging, 92, &extra, 1, candidate)); // Full capacity proved before writes.
  TEST_ASSERT_EQUAL_UINT32(0, candidate.recordCount);
  TEST_ASSERT_FALSE(fill.begin(staging, 93, nullptr, 0, candidate));
  TEST_ASSERT_TRUE(fill.begin(staging, 92, nullptr, 0, candidate));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, fill.step(1)); // Filtered input still costs a turn.
  TEST_ASSERT_EQUAL_UINT32(0, candidate.recordCount);
  staging.captureGeneration = staging.frozenGeneration = 93;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, fill.step(1));
  TEST_ASSERT_EQUAL_UINT32(4, staging.storage.eventCount);
  TEST_ASSERT_FALSE(candidate.selected);
  fill.discard();
  TEST_ASSERT_TRUE(fill.begin(staging, 93, nullptr, 0, candidate));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, fill.step(4));
  TEST_ASSERT_EQUAL_UINT32(0, candidate.recordCount);
  fill.discard();
  candidate.selected = true;
  TEST_ASSERT_FALSE(fill.begin(staging, 93, nullptr, 0, candidate));
}

void test_runtime_frozen_bridge_preparation_binds_epoch_and_keeps_preflight_read_only() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& set = *runtime.viewSetIfPrewarmed();
  auto& base = set.views[set.lifecycle.current];
  prepareBridgeBase(base, {MidiEvent::ControlChange(10, 1, 1, 5)});
  auto& staging = set.frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(30, 1, 1, 7)));
  staging.captureGeneration = 101;
  TEST_ASSERT_TRUE(staging.freeze(101));
  const PlaybackViewSourceIdentity target{2, 2, nullptr};
  const EmissionCursor anchor{0, 1, 0, 0, 2};
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureBridge(base.source, target, 101, 7, 768, anchor, nullptr, 0));
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 101));
  while (!runtime.frozenCaptureOrderComplete()) (void)runtime.stepFrozenCaptureOrder(1);
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureBridge(base.source, target, 101, 7, 768, anchor, nullptr, 0));
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureBridge(base.source, target, 101, 7, 768, anchor, nullptr, 0));
  TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(101, 7, 768, anchor));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepFrozenCaptureBridge(1));
  TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CaptureBridgePhase::Compile, runtime.captureBridgePhase);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureBridge(0));
  TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CaptureBridgePhase::Compile, runtime.captureBridgePhase);
  size_t turns = 0;
  while (runtime.captureBridgePhase != LoopPlaybackRuntime::CaptureBridgePhase::Complete && turns++ < 100) {
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureBridge(1));
  }
  TEST_ASSERT_LESS_THAN_UINT32(100, turns);
  TEST_ASSERT_TRUE(runtime.canSelectFrozenCaptureBridge(101, 7, 768, anchor));
  TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(101, 8, 768, anchor));
  TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(102, 7, 768, anchor));
  TEST_ASSERT_FALSE(runtime.canSelectFrozenCaptureBridge(101, 7, 1536, anchor));
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge, set.bridgeLifecycle.selected);
  TEST_ASSERT_FALSE(set.bridges[set.bridgeLifecycle.candidate].selected);
  TEST_ASSERT_EQUAL_UINT32(1, staging.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, base.source.playbackRevision);
  TEST_ASSERT_FALSE(runtime.selectFrozenCaptureBridge(101, 8, 768, anchor));
  runtime.ledger.noteOn(1, 60, 42, 0, 90);
  TEST_ASSERT_FALSE(runtime.selectFrozenCaptureBridge(101, 7, 768, anchor));
  runtime.ledger.clear();
  TEST_ASSERT_TRUE(runtime.selectFrozenCaptureBridge(101, 7, 768, anchor));
  const uint8_t selected = set.bridgeLifecycle.selected;
  runtime.discardFrozenCaptureBridge();
  TEST_ASSERT_TRUE(set.bridges[selected].selected);
  TEST_ASSERT_NOT_NULL(runtime.selectedFullLoopBridge(target, 768));
  // A later capture prepares in the alternate buffer while the selected
  // bridge remains audible; it must not be rejected solely for being selected.
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureBridge(base.source, {3, 3, nullptr}, 101, 8, 768,
                                                   {0, 2, 0, 0, 3}, nullptr, 0));
  TEST_ASSERT_EQUAL_UINT8(selected, set.bridgeLifecycle.selected);
  TEST_ASSERT_TRUE(set.bridges[selected].selected);
  runtime.discardFrozenCaptureBridge();
}

void test_runtime_frozen_bridge_discards_stale_preparation_without_losing_capture() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  auto& set = *runtime.viewSetIfPrewarmed();
  auto& base = set.views[set.lifecycle.current];
  prepareBridgeBase(base, {MidiEvent::ControlChange(10, 1, 1, 5)});
  auto& staging = set.frozenCaptureStaging();
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(30, 1, 1, 7)));
  staging.captureGeneration = 102;
  TEST_ASSERT_TRUE(staging.freeze(102));
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureOrder(768, 102));
  while (!runtime.frozenCaptureOrderComplete()) (void)runtime.stepFrozenCaptureOrder(1);
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureBridge(base.source, {2, 2, nullptr}, 102, 9, 768,
                                                   {0, 1, 0, 0, 2}, nullptr, 0));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepFrozenCaptureBridge(1));
  staging.captureGeneration = staging.frozenGeneration = 103;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureBridge(1));
  TEST_ASSERT_FALSE(runtime.frozenCaptureSourceAdmission.ready);
  runtime.discardFrozenCaptureBridge();
  TEST_ASSERT_EQUAL_UINT32(1, staging.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(30, staging.storage.events[0].tick);
  TEST_ASSERT_EQUAL_UINT32(0, set.bridges[set.bridgeLifecycle.candidate].recordCount);
  TEST_ASSERT_TRUE(base.published);
  TEST_ASSERT_EQUAL_UINT32(10, base.storage.events[0].tick);
}

void test_frozen_companion_preparation_counts_add_rows_and_preserves_source() {
  CaptureAdmissionStaging capture;
  TEST_ASSERT_TRUE(capture.prewarm());
  capture.captureGeneration = 110;
  TEST_ASSERT_TRUE(capture.freeze(110));
  PendingNoteChangeVec changes{
      {PendingNoteChangeKind::Add, 3, 1, 64, 90, 20, 30},
      {PendingNoteChangeKind::Shorten, 1, 1, 60, 80, 700, 50},
      {PendingNoteChangeKind::Hide, 2, 1, 62, 70, 100, 200}};
  RuntimeCaptureCompanions prepared;
  TEST_ASSERT_TRUE(prepared.prewarm(3));
  const EditPass* backing = &prepared.row(0);
  TEST_ASSERT_FALSE(prepared.prewarm(4));
  TEST_ASSERT_TRUE(prepared.begin(capture, 110, changes));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, prepared.step(1));
  TEST_ASSERT_EQUAL_UINT32(0, prepared.size());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, prepared.step(1));
  TEST_ASSERT_EQUAL_UINT32(1, prepared.size());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, prepared.step(0));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, prepared.step(1));
  TEST_ASSERT_TRUE(prepared.ready(110));
  TEST_ASSERT_EQUAL_PTR(backing, &prepared.row(0));
  TEST_ASSERT_EQUAL_UINT32(3, changes.size());
  TEST_ASSERT_EQUAL_UINT32(2, prepared.size());
  TEST_ASSERT_EQUAL(EditActionType::Update, prepared.row(0).actionType);
  TEST_ASSERT_EQUAL(EditPropertyType::Length, prepared.row(0).propertyType);
  TEST_ASSERT_EQUAL_UINT32(700, prepared.row(0).startTick);
  TEST_ASSERT_EQUAL_UINT32(50, prepared.row(0).endTick);
  TEST_ASSERT_EQUAL_UINT32(kInvalidEditPassId, prepared.row(0).id);
  TEST_ASSERT_EQUAL_UINT8(kOverdubCompanionEditPassIndex, prepared.row(0).editPassIndex);
  TEST_ASSERT_TRUE(prepared.row(0).addedEvents.empty());
  TEST_ASSERT_EQUAL(EditActionType::Delete, prepared.row(1).actionType);
  TEST_ASSERT_EQUAL(EditPropertyType::None, prepared.row(1).propertyType);
  capture.captureGeneration = capture.frozenGeneration = 111;
  TEST_ASSERT_FALSE(prepared.ready(111));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, prepared.step(1));
  prepared.discard();
  TEST_ASSERT_EQUAL_PTR(backing, &prepared.row(0));
  TEST_ASSERT_TRUE(prepared.begin(capture, 111, changes));
  changes.push_back({PendingNoteChangeKind::Hide, 4, 1, 66, 90, 10, 20});
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, prepared.step(1));
  TEST_ASSERT_EQUAL_UINT32(4, changes.size());
  prepared.discard();
  // The reserve is a *row* reserve: the three producible transforms fit the three-row
  // grant, and the interval's `Add` entry never consumes a row. (`Add` still consumes
  // input quantum in `step`, which the pending/size assertions above cover.)
  TEST_ASSERT_TRUE(prepared.begin(capture, 111, changes));
  prepared.discard();
  changes.push_back({PendingNoteChangeKind::Hide, 5, 1, 67, 91, 11, 21});
  // A fourth producible row over a three-row grant is the real denial.
  TEST_ASSERT_FALSE(prepared.begin(capture, 111, changes));
}

void test_ranked_preparation_bounds_do_not_narrow_into_stop_credits() {
  const auto maximum = StopAdmissionContract::requiredRankedPreparationGrains(2048, 2048, 2048, 32);
  const auto fine = StopAdmissionContract::requiredRankedPreparationGrains(2048, 2048, 2048, 1);
  const auto reused = StopAdmissionContract::requiredRankedTailGrains(2048, 2048, 2048, 32);
  TEST_ASSERT_TRUE(reused < maximum); TEST_ASSERT_TRUE(reused > UINT32_MAX);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrains(0, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedTailGrains(2048, 0, 2049, 32));
  TEST_ASSERT_TRUE(maximum > UINT32_MAX); TEST_ASSERT_TRUE(fine >= maximum);
  TEST_ASSERT_TRUE(fine < UINT64_MAX);
  const StopAdmissionContract existing{PendingStopAdmission::kMaximumClosureGrains, 3072, 1, 32};
  TEST_ASSERT_TRUE(existing.isDeclared());
  TEST_ASSERT_TRUE(maximum > existing.maximumOrderGrains);
  TEST_ASSERT_TRUE(StopAdmissionContract::requiredRankedPreparationGrains(2048, 0, 0, 32) > UINT16_MAX);
  for (size_t capacity : {1u, 4u, 32u, 2048u}) {
    const auto a = StopAdmissionContract::requiredRankedPreparationGrains(capacity, 0, 0, 32);
    const auto b = StopAdmissionContract::requiredRankedPreparationGrains(capacity, 1, 0, 32);
    const auto c = StopAdmissionContract::requiredRankedPreparationGrains(capacity, 1, 1, 32);
    TEST_ASSERT_TRUE(a <= b); TEST_ASSERT_TRUE(b <= c);
    TEST_ASSERT_TRUE(c <= StopAdmissionContract::requiredRankedPreparationGrains(capacity, 1, 1, 1));
  }
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedPreparationGrains(0, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedPreparationGrains(2049, 0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedPreparationGrains(2048, 2049, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedPreparationGrains(2048, 0, 2049, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, StopAdmissionContract::requiredRankedPreparationGrains(2048, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(2049, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(0, 2049, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(0, 0, 0));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionBridgeMap::requiredPerPassGrainsUpperBound(2049, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionBridgeMap::requiredPerPassGrainsUpperBound(0, 2049, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionBridgeMap::requiredPerPassGrainsUpperBound(0, 0, 0));
  printf("Ranked preparation bound C=H=R=2048: Q32=%llu, Q1=%llu\n",
         static_cast<unsigned long long>(maximum), static_cast<unsigned long long>(fine));
}


void test_wide_playback_cursor_crosses_pool_extent_without_replay() {
  struct Source {
    mutable MidiEvent event;
    uint32_t consumed = 0, sent = 0;
    bool deny = true;
  } source;
  constexpr uint32_t count = uint32_t(LoopEventStoreConfig::POOL_CHUNK_COUNT) *
                              LoopEventStoreConfig::CHUNK_CAPACITY;
  static_assert(count > UINT16_MAX, "fixture must cross the narrow cursor extent");
  WidePlaybackEventStream stream{&source,
    [](const void*) -> size_t { return count; },
    [](const void* c, uint32_t index) {
      const auto& source = *static_cast<const Source*>(c);
      return index == source.consumed && !(source.deny && index == UINT16_MAX);
    },
    [](const void* c, uint32_t index) -> const MidiEvent& {
      const auto& source = *static_cast<const Source*>(c);
      source.event = MidiEvent::ControlChange(index, 1, 7, index % 128);
      source.event.noteId = index + 1;
      return source.event;
    },
    [](const MidiEvent& event, const ProjectionContext&) { return event.tick; },
    [](const void* c) { ++const_cast<Source*>(static_cast<const Source*>(c))->consumed; }};
  auto send = [](void* c, const MidiEvent& event, uint8_t) {
    auto& source = *static_cast<Source*>(c);
    TEST_ASSERT_EQUAL_UINT32(source.sent, event.tick);
    TEST_ASSERT_EQUAL_UINT32(source.sent + 1, event.noteId);
    ++source.sent;
  };
  ProjectionContext projection{}; projection.loopLength = count;
  uint32_t index = 0; bool dirty = false;
  PlaybackTickFrame frame{&projection, UINT16_MAX, 0, false};
  // Start at phase zero, then advance in two intervals across the 16-bit edge.
  PlaybackTickFrame start{&projection, 0, UINT32_MAX, true};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed, advancePlaybackCursor(
      {&index, &dirty}, start, PlaybackEmitPolicy::LayeredSlot, stream, send, &source,
      0, nullptr, nullptr, 1));
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::PlaybackOrderInvalid, advancePlaybackCursor(
      {&index, &dirty}, frame, PlaybackEmitPolicy::LayeredSlot, stream, send, &source,
      0, nullptr, nullptr, 1));
  TEST_ASSERT_TRUE(dirty);
  TEST_ASSERT_EQUAL_UINT32(UINT16_MAX, index);
  TEST_ASSERT_EQUAL_UINT32(index, source.sent);
  TEST_ASSERT_EQUAL_UINT32(index, source.consumed);
  source.deny = false; dirty = false;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed, advancePlaybackCursor(
      {&index, &dirty}, frame, PlaybackEmitPolicy::LayeredSlot, stream, send, &source,
      0, nullptr, nullptr, 1));
  TEST_ASSERT_EQUAL_UINT32(65536, index);
  frame.prevTickInLoop = UINT16_MAX; frame.tickInLoop = count - 1;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed, advancePlaybackCursor(
      {&index, &dirty}, frame, PlaybackEmitPolicy::LayeredSlot, stream, send, &source,
      0, nullptr, nullptr, 1));
  TEST_ASSERT_FALSE(dirty);
  TEST_ASSERT_EQUAL_UINT32(count, index);
  TEST_ASSERT_EQUAL_UINT32(count, source.sent);
  TEST_ASSERT_EQUAL_UINT32(count, source.consumed);
}

void test_shared_window_fill_filters_before_capacity_and_preserves_loop_end_order() {
  const MidiEvent events[] = {MidiEvent::NoteOn(32, 1, 60, 90),
    MidiEvent::NoteOff(1536, 1, 61), MidiEvent::NoteOn(800, 1, 62, 90),
    MidiEvent::ControlChange(1500, 1, 7, 80), MidiEvent::NoteOff(700, 1, 60)};
  RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(3));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, events, 5, {1, 1, nullptr}, {0, 768, 1536}, {}, {},
                              RuntimePlaybackViewFillScope::PlaybackCoverage));
  for (unsigned i = 0; i < 5; ++i) {
    TEST_ASSERT_FALSE(view.complete);
    const auto next = build.stepFill(1);
    TEST_ASSERT_EQUAL(i == 4 ? RuntimePlaybackViewBuildGrain::Order : RuntimePlaybackViewBuildGrain::Fill, next);
  }
  while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order) build.stepOrder(1);
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_EQUAL_UINT32(3, view.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(1536, view.storage.events[view.storage.order[0]].tick);
  TEST_ASSERT_EQUAL_UINT32(32, view.storage.events[view.storage.order[1]].tick);
  TEST_ASSERT_EQUAL_UINT32(700, view.storage.events[view.storage.order[2]].tick);
  TEST_ASSERT_FALSE(view.published);
}

void test_shared_window_fill_scans_wide_source_with_bounded_quantum() {
  std::vector<MidiEvent> source(65536, MidiEvent::ControlChange(800, 1, 7, 90));
  source.push_back(MidiEvent::NoteOn(0, 1, 60, 90));
  source.push_back(MidiEvent::NoteOff(1536, 1, 60));
  RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(2));
  RuntimePlaybackViewFill fill;
  TEST_ASSERT_TRUE(fill.begin(view, source.data(), source.size(), {1, 1, nullptr},
      {0, 768, 1536}, {}, {}, RuntimePlaybackViewFillScope::PlaybackCoverage));
  for (unsigned i = 0; i < 2048; ++i) {
    TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Pending, fill.step(32));
    TEST_ASSERT_EQUAL_UINT32(0, view.storage.eventCount);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Complete, fill.step(32));
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  TEST_ASSERT_TRUE(view.sourceFilled);
  TEST_ASSERT_EQUAL_UINT32(0, view.storage.events[0].tick);
  TEST_ASSERT_EQUAL_UINT32(1536, view.storage.events[1].tick);
}

void test_shared_window_fill_wraps_coverage_and_denies_matching_overflow() {
  const MidiEvent events[] = {MidiEvent::NoteOff(1536, 1, 60),
    MidiEvent::NoteOn(1500, 1, 60, 90), MidiEvent::ControlChange(32, 1, 7, 90),
    MidiEvent::NoteOn(800, 1, 61, 90)};
  RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(2));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, events, 4, {1, 1, nullptr}, {1400, 272, 1536}, {}, {},
                              RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepFill(4));
  TEST_ASSERT_FALSE(view.sourceFilled); TEST_ASSERT_FALSE(view.complete); TEST_ASSERT_FALSE(view.published);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  // The owner can discard the denied candidate and prepare a different window.
  build.discard();
  TEST_ASSERT_TRUE(build.begin(view, events, 4, {2, 2, nullptr}, {768, 768, 1536}, {}, {},
                              RuntimePlaybackViewFillScope::PlaybackCoverage));
  build.stepFill(4);
  while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order) build.stepOrder(1);
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
}

static void buildSharedTestBar(RuntimePlaybackView& view, uint32_t bar) {
  const MidiEvent event = MidiEvent::NoteOn(bar * 768 + 32, 1, 60, 90);
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, &event, 1, {1, 1, nullptr},
      {bar * 768, 768, 16 * 768}, {}, {}, RuntimePlaybackViewFillScope::PlaybackCoverage));
  build.stepFill(1);
  while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order) build.stepOrder(1);
  TEST_ASSERT_TRUE(view.complete);
}

void test_shared_lifecycle_retains_return_bar_while_successors_rotate() {
  RuntimePlaybackViewSet set; TEST_ASSERT_TRUE(set.prewarm());
  auto& life = set.lifecycle;
  TEST_ASSERT_FALSE(life.retainCurrent(set.views));
  buildSharedTestBar(set.views[0], 0);
  buildSharedTestBar(set.views[1], 1);
  const auto* firstStorage = set.views[0].storage.events;
  TEST_ASSERT_TRUE(life.retainCurrent(set.views));
  TEST_ASSERT_TRUE(set.handoffReadyCoverage());
  TEST_ASSERT_EQUAL_UINT8(0, life.retained);
  TEST_ASSERT_FALSE(life.retainCurrent(set.views));
  for (uint32_t bar = 2; bar <= 10; ++bar) {
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, life.build);
    TEST_ASSERT_NOT_EQUAL(life.retained, life.build);
    buildSharedTestBar(set.views[life.build], bar);
    TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady());
    TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, life.build);
    TEST_ASSERT_TRUE(set.handoffReadyCoverage());
    TEST_ASSERT_EQUAL_UINT32(bar * 768, set.views[life.current].coverage.startPhase);
    TEST_ASSERT_EQUAL_PTR(firstStorage, set.views[0].storage.events);
    TEST_ASSERT_EQUAL_UINT32(32, set.views[0].storage.events[0].tick);
    TEST_ASSERT_TRUE(set.views[0].published);
    TEST_ASSERT_TRUE(set.views[0].complete);
  }
  TEST_ASSERT_TRUE(life.handoffToRetained(set.views));
  TEST_ASSERT_EQUAL_UINT8(0, life.current);
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, life.retained);
  TEST_ASSERT_EQUAL_PTR(firstStorage, set.views[life.current].storage.events);
  TEST_ASSERT_FALSE(life.handoffToRetained(set.views));
  buildSharedTestBar(set.views[life.build], 1);
  TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady());
  TEST_ASSERT_TRUE(set.handoffReadyCoverage());
  TEST_ASSERT_EQUAL_UINT32(768, set.views[life.current].coverage.startPhase);
}

void test_shared_lifecycle_retains_return_when_build_already_complete() {
  RuntimePlaybackViewSet set; TEST_ASSERT_TRUE(set.prewarm());
  for (uint32_t bar = 0; bar < 3; ++bar) buildSharedTestBar(set.views[bar], bar);
  TEST_ASSERT_TRUE(set.lifecycle.retainCurrent(set.views));
  TEST_ASSERT_TRUE(set.handoffReadyCoverage());
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(2, set.lifecycle.build);
  TEST_ASSERT_FALSE(set.handoffReadyCoverage());
  TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady());
  TEST_ASSERT_TRUE(set.handoffReadyCoverage());
  TEST_ASSERT_EQUAL_UINT8(1, set.lifecycle.build);
  TEST_ASSERT_TRUE(set.views[0].complete);
  set.resetMetadata();
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set.lifecycle.retained);
}

void test_shared_runtime_coverage_owner_filters_source() {
  LoopPlaybackRuntime runtime; TEST_ASSERT_TRUE(runtime.prewarmViews());
  const MidiEvent source[] = {MidiEvent::NoteOn(32, 1, 60, 90), MidiEvent::NoteOn(800, 1, 61, 90)};
  const CoverageServiceContract contract{1, 0, 2, 2, 1, 1};
  TEST_ASSERT_TRUE(runtime.beginCoverageBuild(contract, 0, 768, source, 2, {1, 1, nullptr},
      {768, 768, 1536}, {}, {}));
  runtime.stepCoverageBuildFill(2);
  while (runtime.nextCoverageBuildGrain() == RuntimePlaybackViewBuildGrain::Order)
    runtime.stepCoverageBuildOrder(1);
  const auto* set = runtime.viewSetIfPrewarmed();
  const auto& view = set->views[set->lifecycle.build];
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_EQUAL_UINT32(1, view.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(800, view.storage.events[0].tick);
}

void test_shared_build_discard_detaches_pending_fill_and_order() {
  RuntimePlaybackView first, replacement;
  TEST_ASSERT_TRUE(first.prewarm(4)); TEST_ASSERT_TRUE(replacement.prewarm(4));
  const MidiEvent source[] = {MidiEvent::NoteOn(30, 1, 60, 90),
    MidiEvent::NoteOff(20, 1, 60), MidiEvent::NoteOn(10, 1, 61, 90)};
  for (bool cancelOrder : {false, true}) {
    RuntimePlaybackViewBuild build;
    TEST_ASSERT_TRUE(build.begin(first, source, 3, {1, 1, nullptr}, {0, 768, 768}, {}, {}));
    build.stepFill(cancelOrder ? 3 : 1);
    if (cancelOrder) build.stepOrder(1);
    const auto copied = first.storage.eventCount;
    build.discard();
    TEST_ASSERT_FALSE(first.sourceFilled); TEST_ASSERT_FALSE(first.complete);
    TEST_ASSERT_FALSE(build.active());
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepFill(1));
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepOrder(1));
    TEST_ASSERT_TRUE(build.begin(replacement, source, 3, {2, 2, nullptr}, {0, 768, 768}, {}, {}));
    build.stepFill(3);
    while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order) build.stepOrder(1);
    TEST_ASSERT_TRUE(replacement.complete);
    TEST_ASSERT_EQUAL_UINT32(copied, first.storage.eventCount);
    TEST_ASSERT_FALSE(first.complete);
    replacement.published = true;
    build.discard();
    TEST_ASSERT_TRUE(replacement.complete); // Published source remains readable.
    replacement.published = false;
  }
}

void test_first_record_bar_prepares_before_capture_and_survives_cache_reset() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT8(set->lifecycle.retained, set->lifecycle.current);
  TEST_ASSERT_TRUE(set->views[set->lifecycle.current].rawCoveragePrepared);
  TEST_ASSERT_TRUE(set->views[set->lifecycle.current].complete);
  runtime.reset(true);
  set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
  TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].rawCoveragePrepared);
  TEST_ASSERT_TRUE(runtime.bindFirstRecordingBar(1536));
  TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.current);
  // The handoff releases `retained` (the rolling rotation needs that buffer back)
  // and marks the bound window instead; `reset` preserves it by that flag so the
  // launch's playback-cache resets cannot clear the first take's only source.
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
  TEST_ASSERT_TRUE(set->views[set->lifecycle.current].firstRecordReturnWindow);
  TEST_ASSERT_FALSE(set->views[set->lifecycle.current].rawCoveragePrepared);
  TEST_ASSERT_EQUAL_UINT32(1536, set->views[set->lifecycle.current].coverage.loopLengthTicks);
  TEST_ASSERT_FALSE(runtime.bindFirstRecordingBar(768));
}

void test_first_record_preparation_failure_does_not_change_existing_runtime() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_FALSE(runtime.prepareFirstRecordingBar(768));
  TEST_ASSERT_EQUAL_STRING("no_prewarmed_view_set", runtime.firstRecordingBarDenialLabel());
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  TEST_ASSERT_FALSE(runtime.prepareFirstRecordingBar(768));
  TEST_ASSERT_EQUAL_STRING("return_window_held", runtime.firstRecordingBarDenialLabel());
}

// A take that ends without the record-stop handoff (transport Stop during
// RECORDING, empty/skipped commit) leaves the prepared raw bar-1 window held.
// The next take's preparation boundary owns releasing it, otherwise the
// prepared-first-bar path never exists for that take (`deadline-pacing-01`,
// `successor-reentry-13`, `bar-anchor-02/-06` all logged the denial after a
// `RECS,stopToStopped` empty take).
void test_first_record_return_window_releases_for_the_next_take() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(32, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60)));

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity source{loop.playbackRevision, 1, loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 16; ++i) runtime.serviceFirstRecordingBar(loop, source, 768, 1);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].rawCoveragePrepared);
  TEST_ASSERT_EQUAL_UINT16(2, set->views[set->lifecycle.retained].storage.eventCount);

  // The take ends here with no record-stop handoff. The cache reset keeps the
  // retained window (design: the retained raw bar survives playback-cache reset).
  runtime.reset(true);
  TEST_ASSERT_FALSE(runtime.prepareFirstRecordingBar(768));
  TEST_ASSERT_EQUAL_STRING("return_window_held", runtime.firstRecordingBarDenialLabel());

  TEST_ASSERT_TRUE(runtime.releaseFirstRecordingBarReturnWindow());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT8(set->lifecycle.retained, set->lifecycle.current);
  TEST_ASSERT_TRUE(set->views[set->lifecycle.current].rawCoveragePrepared);
  TEST_ASSERT_EQUAL_UINT16(0, set->views[set->lifecycle.current].storage.eventCount);
  TEST_ASSERT_EQUAL_STRING("none", runtime.firstRecordingBarDenialLabel());
}

// The record-stop handoff binds the prepared bar-1 window to the final loop
// geometry, and playback must select that bound window instead of holding empty
// until the first root publishes. The window is the only source the first take
// has: the live capture store it was filled from is gone once the take is sealed.
void test_first_record_bound_window_is_adopted_as_the_audible_source() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(32, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60)));

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity recordingSource{loop.playbackRevision, 1,
                                                   loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 16; ++i) runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  TEST_ASSERT_TRUE(runtime.bindFirstRecordingBar(768));

  // The launch identity is the post-stop one. It must not match the identity the
  // window was filled under, so no snapshot-backed source can be returned first.
  runtime.reset(true);
  const PlaybackViewSourceIdentity launchSource{loop.playbackRevision + 1, 2, nullptr};
  TEST_ASSERT_NULL(runtime.publishedFullLoopView(launchSource, 768));

  const RuntimePlaybackView* adopted =
      runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 768, 236);
  TEST_ASSERT_NOT_NULL(adopted);
  TEST_ASSERT_TRUE(adopted->published);
  TEST_ASSERT_TRUE(adopted->complete);
  TEST_ASSERT_FALSE(adopted->rawCoveragePrepared);
  TEST_ASSERT_EQUAL_UINT16(2, adopted->storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, adopted->coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(768, adopted->coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(768, adopted->coverage.loopLengthTicks);
  // Adoption is a one-time stamp: the ordinary published-view query serves it after.
  TEST_ASSERT_EQUAL_PTR(adopted, runtime.publishedFullLoopView(launchSource, 768));
  // A later root (new revision/generation) is not shadowed by the raw window; the
  // window cannot serve a longer loop than the one it was bound to; and it only
  // serves the launch while the playhead is inside its own coverage.
  TEST_ASSERT_NULL(runtime.publishedFullLoopView({loop.playbackRevision + 2, 3, loop.publishedSnapshotIdentity()}, 768));
  TEST_ASSERT_NULL(runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 1536, 236));
  TEST_ASSERT_NULL(runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 768, 768));
}

// A multi-bar take binds the same first-bar window. It serves the launch from the
// stop phase until the playhead leaves bar 1; the published root takes over inside
// that window (measured 0.45-0.94 s against a 2 s bar).
void test_first_record_first_bar_window_serves_a_multi_bar_launch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(32, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 62, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(500, 1, 62)));

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity recordingSource{loop.playbackRevision, 1,
                                                   loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 16; ++i) runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  // Two bars: the window keeps its bar-1 coverage and is bound to the loop length.
  TEST_ASSERT_TRUE(runtime.bindFirstRecordingBar(1536));
  runtime.reset(true);
  const PlaybackViewSourceIdentity launchSource{loop.playbackRevision + 1, 2, nullptr};

  // The launch phase is where the take stopped: the two notes below it were
  // already heard live, so the cursor starts at the first event after it instead
  // of replaying the window as a loop-start catch-up.
  loop.lastTickInLoop = 253;
  loop.nextEventIndex = 0;
  const RuntimePlaybackView* adopted =
      runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 1536, 253);
  TEST_ASSERT_NOT_NULL(adopted);
  TEST_ASSERT_EQUAL_UINT32(768, adopted->coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(1536, adopted->coverage.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT16(2, loop.nextEventIndex);
  TEST_ASSERT_EQUAL_UINT32(400, adopted->storage.events[adopted->storage.order[loop.nextEventIndex]].tick);
  // Past the window's coverage the caller falls back instead of emitting nothing
  // from a source that never held bar 2.
  TEST_ASSERT_NULL(runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 1536, 800));
}

// S4f.19: the deferred Record worker binds the retained bar-1 window to the job's own
// frozen source and final geometry. The bind must **transform** the raw bar, not merely
// clamp its coverage: overflow NoteOns are remapped into the loop, other overflow rows
// are dropped, held notes crossing the loop end are closed, and the rows are ordered
// canonically. A stale or unsealed range must leave the window unbound.
void test_deferred_record_return_window_binds_the_owned_frozen_source() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  // A sub-bar take (final length 500) whose raw bar also overflows: 600 is remapped to
  // 100 for the NoteOn, the CC at 700 is dropped, and the two notes left open are
  // closed at the loop end.
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(32, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(600, 1, 62, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(700, 1, 7, 1)));

  // The interval's frozen source: a sealed, generation-qualified range, exactly what
  // the deferred job owns.
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(32, 1, 60, 90)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(96, 1, 60)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
  TEST_ASSERT_TRUE(range.ready());

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity recordingSource{loop.playbackRevision, 1,
                                                   loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 32; ++i) runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  const PlaybackViewSourceIdentity launchSource{loop.playbackRevision + 1, 2, nullptr};

  // A stale generation must not bind: the retained window stays raw (unbound), so no
  // adopter can serve it.
  TEST_ASSERT_FALSE(runtime.bindDeferredRecordReturnWindow(range, range.generation() + 1, 500));
  TEST_ASSERT_NULL(runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 500, 0));

  // The qualified bind projects the raw bar onto the final 500-tick loop and makes the
  // window the interval's audible source.
  TEST_ASSERT_TRUE(runtime.bindDeferredRecordReturnWindow(range, range.generation(), 500));
  runtime.reset(true);
  const RuntimePlaybackView* adopted =
      runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 500, 0);
  TEST_ASSERT_NOT_NULL(adopted);
  TEST_ASSERT_TRUE(adopted->published);
  TEST_ASSERT_FALSE(adopted->rawCoveragePrepared);
  TEST_ASSERT_TRUE(adopted->firstRecordReturnWindow);
  // The coverage is the prepared bar intersected with the final loop.
  TEST_ASSERT_EQUAL_UINT32(0, adopted->coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(500, adopted->coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(500, adopted->coverage.loopLengthTicks);

  // Exact transformed content, in canonical order.
  size_t retained = 0;
  for (size_t i = 0; i < adopted->storage.eventCount; ++i) {
    if (adopted->storage.eventRetained(i)) ++retained;
  }
  TEST_ASSERT_EQUAL_UINT32(6, retained);
  const auto at = [&](size_t k) -> const MidiEvent& {
    return adopted->storage.events[adopted->storage.order[k]];
  };
  // 32 On(60), 96 Off(60), 100 On(62) [600 remapped], 400 On(64),
  // 499 Off(62) and 499 Off(64) [held-note closure at the loop end].
  TEST_ASSERT_EQUAL_UINT32(32, at(0).tick);
  TEST_ASSERT_TRUE(at(0).isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(60, at(0).data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(96, at(1).tick);
  TEST_ASSERT_TRUE(at(1).isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(100, at(2).tick);
  TEST_ASSERT_TRUE(at(2).isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(62, at(2).data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(400, at(3).tick);
  TEST_ASSERT_TRUE(at(3).isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(64, at(3).data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(499, at(4).tick);
  TEST_ASSERT_TRUE(at(4).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(62, at(4).data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(499, at(5).tick);
  TEST_ASSERT_TRUE(at(5).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(64, at(5).data.noteData.note);
  // The overflowing CC never survives projection.
  for (size_t k = 0; k < retained; ++k) {
    TEST_ASSERT_FALSE(at(k).type == midi::ControlChange);
    TEST_ASSERT_LESS_THAN_UINT32(500, at(k).tick);
  }

  // Idempotent: the role was handed off, so a second bind is a no-op refusal and the
  // adopted window is unchanged.
  TEST_ASSERT_FALSE(runtime.bindDeferredRecordReturnWindow(range, range.generation(), 500));
  TEST_ASSERT_EQUAL_PTR(adopted,
                        runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, 500, 0));
}

// A refusal must leave the window byte-identical: the worker retries the same raw
// window, so a partially projected (or partially closed) window is neither raw nor
// bindable. Fill the view role to capacity with held notes so the closure append cannot
// fit, then compare the complete window before and after the refusal.
void test_deferred_record_projection_refusal_leaves_the_window_untouched() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  // 1023 closed pairs plus two held Ons = 2048 rows, exactly the view capacity. Every
  // row must sit inside the raw bar-1 window (ticks < 768) and the ingress needs
  // non-decreasing ticks, so several events share a tick.
  constexpr uint32_t kPairs = 1023;
  for (uint32_t pair = 0; pair < kPairs; ++pair) {
    const uint8_t note = static_cast<uint8_t>(pair % 128);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(pair / 4, 1, note, 90)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(pair / 4, 1, note)));
  }
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(700, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(701, 2, 62, 90)));
  TEST_ASSERT_EQUAL_UINT32(2048, loop.capture.store.size());

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity recordingSource{loop.playbackRevision, 1,
                                                   loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 8192; ++i) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 32);
  }
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
  RuntimePlaybackView& window = set->views[set->lifecycle.retained];
  TEST_ASSERT_TRUE(window.complete);
  TEST_ASSERT_TRUE(window.rawCoveragePrepared);
  TEST_ASSERT_EQUAL_UINT32(2048, window.storage.eventCount);

  // Snapshot the complete window, then refuse: the closure appends cannot fit.
  using Row = std::array<uint32_t, 6>;  // tick, type, channel, note, retained, phase
  const auto snapshot = [&]() {
    std::vector<Row> rows;
    for (size_t i = 0; i < window.storage.eventCount; ++i) {
      const MidiEvent& event = window.storage.events[i];
      rows.push_back({event.tick, static_cast<uint32_t>(event.type), event.channel,
                      event.data.noteData.note,
                      window.storage.eventRetained(i) ? 1u : 0u, window.storage.phases[i]});
    }
    return rows;
  };
  const std::vector<Row> before = snapshot();
  const uint32_t coverageBefore = window.coverage.lengthTicks;
  TEST_ASSERT_FALSE(window.projectRawCoverageToLoopLength(200));
  const std::vector<Row> after = snapshot();
  TEST_ASSERT_EQUAL_UINT32(before.size(), after.size());
  for (size_t i = 0; i < before.size(); ++i) {
    for (size_t field = 0; field < before[i].size(); ++field) {
      TEST_ASSERT_EQUAL_UINT32(before[i][field], after[i][field]);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(coverageBefore, window.coverage.lengthTicks);
  TEST_ASSERT_TRUE(window.rawCoveragePrepared);
  TEST_ASSERT_TRUE(window.complete);

  // The refusal is repeatable and still leaves the window raw: the worker can retry the
  // same window while capacity is unavailable, and every refusal is a no-op.
  TEST_ASSERT_FALSE(window.projectRawCoverageToLoopLength(200));
  const std::vector<Row> afterRetry = snapshot();
  TEST_ASSERT_EQUAL_UINT32(before.size(), afterRetry.size());
  for (size_t i = 0; i < before.size(); ++i) {
    for (size_t field = 0; field < before[i].size(); ++field) {
      TEST_ASSERT_EQUAL_UINT32(before[i][field], afterRetry[i][field]);
    }
  }
  TEST_ASSERT_TRUE(window.rawCoveragePrepared);
}

// S4f.19 onward windows: the window containing the playhead is built from the owned
// frozen Record source through the existing fill/order owners and the RecordCoverage
// projection, into the `ready` role.
void test_frozen_record_source_builds_the_next_playback_window() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(100, 1, 60, 90)));   // bar 1
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(200, 1, 60)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(800, 1, 62, 91)));   // bar 2
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(900, 1, 62)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));

  const PlaybackViewSourceIdentity source{1, 2, nullptr};
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  // The record start's own preparation leaves the lifecycle with a free `ready` role,
  // exactly the state the onward service runs in.
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  auto* lifecycleSet = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, lifecycleSet->lifecycle.ready);
  // The playhead is in bar 2, so the bar-2 window is the one built.
  for (size_t grain = 0; grain < 4096 && !runtime.frozenRecordWindowReady(); ++grain) {
    (void)runtime.serviceFrozenRecordWindow(range, range.generation(), source, 1536, 800, 768, 32);
  }
  TEST_ASSERT_TRUE(runtime.frozenRecordWindowReady());
  auto* set = runtime.viewSetIfPrewarmed();
  const RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
  // The playhead at phase 800 is in the bar that starts at 768.
  TEST_ASSERT_EQUAL_UINT32(768, ready.coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(768, ready.coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(1536, ready.coverage.loopLengthTicks);
  // Exact projected bar-2 content: the two bar-2 rows, canonically ordered. Bar-1 rows
  // are outside the window.
  size_t retained = 0;
  for (size_t i = 0; i < ready.storage.eventCount; ++i) {
    if (ready.storage.eventRetained(i)) ++retained;
  }
  TEST_ASSERT_EQUAL_UINT32(2, retained);
  const MidiEvent& first = ready.storage.events[ready.storage.order[0]];
  const MidiEvent& second = ready.storage.events[ready.storage.order[1]];
  TEST_ASSERT_EQUAL_UINT32(800, first.tick);
  TEST_ASSERT_TRUE(first.isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(62, first.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(900, second.tick);
  TEST_ASSERT_TRUE(second.isNoteOff());
}

// A stale or unready frozen range refuses without creating a ready window.
void test_frozen_record_window_refuses_a_stale_range() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(800, 1, 62, 91)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity source{1, 2, nullptr};
  TEST_ASSERT_FALSE(runtime.serviceFrozenRecordWindow(range, range.generation() + 1, source,
                                                      1536, 800, 768, 32));
  TEST_ASSERT_FALSE(runtime.frozenRecordWindowReady());
  // A phase outside the loop also refuses.
  TEST_ASSERT_FALSE(runtime.serviceFrozenRecordWindow(range, range.generation(), source,
                                                      1536, 1536, 768, 32));
  TEST_ASSERT_FALSE(runtime.frozenRecordWindowReady());
  // An unsealed range refuses.
  LoopEventStore::CaptureRange unsealed;
  LoopEventStore spare;
  TEST_ASSERT_TRUE(spare.reserveCaptureTail());
  TEST_ASSERT_TRUE(unsealed.reserveTail());
  TEST_ASSERT_FALSE(runtime.serviceFrozenRecordWindow(unsealed, unsealed.generation(), source,
                                                      1536, 800, 768, 32));
  TEST_ASSERT_FALSE(runtime.frozenRecordWindowReady());

  // Stale source replacement: with the current source the same service builds the window,
  // so a stale refusal never blocks the replacement that follows it.
  for (size_t grain = 0; grain < 4096 && !runtime.frozenRecordWindowReady(); ++grain) {
    (void)runtime.serviceFrozenRecordWindow(range, range.generation(), source, 1536, 800, 768, 32);
  }
  TEST_ASSERT_TRUE(runtime.frozenRecordWindowReady());
  auto* set = runtime.viewSetIfPrewarmed();
  const RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
  TEST_ASSERT_TRUE(ready.complete);
  TEST_ASSERT_EQUAL_UINT32(768, ready.coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(1536, ready.coverage.loopLengthTicks);
  size_t retained = 0;
  for (size_t i = 0; i < ready.storage.eventCount; ++i) {
    if (ready.storage.eventRetained(i)) ++retained;
  }
  TEST_ASSERT_EQUAL_UINT32(1, retained);
}

// S4f.19 crossing notes: the projection closes only a lane that was open at the stop.
// A lane whose real Off is further inside the take stays open so the onward window ends
// it at its actual tick, instead of being cut at the loop end here.
void test_projection_closes_only_lanes_open_at_the_stop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEvent rows[] = {MidiEvent::NoteOn(700, 1, 60, 90),
                            MidiEvent::NoteOn(710, 2, 62, 91)};
  RuntimePlaybackView window;
  TEST_ASSERT_TRUE(window.prewarm(8));
  RuntimePlaybackViewFill fill;
  const PlaybackCoverageInterval coverage{0, 768, 0};
  TEST_ASSERT_TRUE(fill.begin(window, rows, 2, PlaybackViewSourceIdentity{1, 1, nullptr},
                              coverage, EmissionCursor{}, LedgerBoundarySummary{},
                              RuntimePlaybackViewFillScope::RawCoverage));
  while (fill.step(8) == RuntimePlaybackViewFillResult::Pending) {
  }
  // This fixture drives the fill owner directly, so the build/order owner's completion
  // flag is set here; the projection's precondition is `complete && rawCoveragePrepared`.
  window.complete = true;
  TEST_ASSERT_TRUE(window.rawCoveragePrepared);

  // Only the (ch1, note60) lane was open at the stop: lane 0*128+60 = 60.
  std::array<uint64_t, RuntimePlaybackView::kOpenLaneWords> lanes{};
  lanes[60 >> 6] |= static_cast<uint64_t>(1) << (60 & 63u);
  TEST_ASSERT_TRUE(window.projectRawCoverageToLoopLength(768, lanes.data()));

  size_t ons = 0;
  size_t offs = 0;
  for (size_t i = 0; i < window.storage.eventCount; ++i) {
    if (!window.storage.eventRetained(i)) continue;
    const MidiEvent& event = window.storage.events[i];
    if (event.isNoteOff()) {
      ++offs;
      // The open-at-stop lane closes at the loop end; the crossing lane does not.
      TEST_ASSERT_EQUAL_UINT8(60, event.data.noteData.note);
      TEST_ASSERT_EQUAL_UINT32(767, event.tick);
    } else {
      ++ons;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1, offs);
  TEST_ASSERT_EQUAL_UINT32(2, ons);

  // With no open-at-stop information at all, both lanes are treated as open at the stop
  // (the previous behaviour) -- the gate is what makes the distinction.
  RuntimePlaybackView both;
  TEST_ASSERT_TRUE(both.prewarm(8));
  RuntimePlaybackViewFill fillBoth;
  TEST_ASSERT_TRUE(fillBoth.begin(both, rows, 2, PlaybackViewSourceIdentity{1, 1, nullptr},
                                  coverage, EmissionCursor{}, LedgerBoundarySummary{},
                                  RuntimePlaybackViewFillScope::RawCoverage));
  while (fillBoth.step(8) == RuntimePlaybackViewFillResult::Pending) {
  }
  both.complete = true;
  TEST_ASSERT_TRUE(both.projectRawCoverageToLoopLength(768, nullptr));
  size_t bothOffs = 0;
  for (size_t i = 0; i < both.storage.eventCount; ++i) {
    if (both.storage.eventRetained(i) && both.storage.events[i].isNoteOff()) ++bothOffs;
  }
  TEST_ASSERT_EQUAL_UINT32(2, bothOffs);
}

void test_deferred_record_return_window_refuses_an_unsealed_range() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_FALSE(range.ready());
  TEST_ASSERT_FALSE(runtime.bindDeferredRecordReturnWindow(range, range.generation(), 1536));
  // The window is untouched and still raw, so nothing can adopt it.
  Loop loop;
  TEST_ASSERT_NULL(runtime.adoptFirstRecordingBarReturnWindow(
      loop, PlaybackViewSourceIdentity{1, 1, nullptr}, 1536, 0));
}

void test_deferred_record_interval_is_eligible_only_under_a_pending_owner() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(32, 1, 60, 90)));

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity recordingSource{loop.playbackRevision, 1,
                                                   loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 16; ++i) runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);

  // Ownership and audibility are separate: a pending owner owns the interval for its whole
  // life (eligibility), but an unbound raw window is not yet a servable view.
  PendingStopAdmission admission{};
  admission.pending = true;
  runtime.setCaptureStopOwner(admission, 1);
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_FALSE(runtime.hasServableDeferredRecordView());

  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(32, 1, 60, 90)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
  TEST_ASSERT_TRUE(runtime.bindDeferredRecordReturnWindow(range, range.generation(), 768));
  TEST_ASSERT_TRUE(runtime.hasServableDeferredRecordView());

  // Retiring the job ends the unpublished interval's eligibility and audibility; committed
  // content owns the slot from then on.
  admission.pending = false;
  TEST_ASSERT_FALSE(runtime.hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_FALSE(runtime.hasServableDeferredRecordView());
}

void test_first_record_return_window_release_covers_a_bound_window() {
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  TEST_ASSERT_TRUE(runtime.bindFirstRecordingBar(768));
  auto* set = runtime.viewSetIfPrewarmed();
  // The record-stop handoff releases `retained`; the bound window is identified by
  // its own flag and stays the current view.
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
  const uint8_t boundWindow = set->firstRecordReturnWindowIndex();
  TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, boundWindow);
  TEST_ASSERT_EQUAL_UINT8(set->lifecycle.current, boundWindow);

  // A new take on this slot is not audible yet, so its preparation boundary owns
  // releasing the previous take's bound window and establishing its own bar 1.
  TEST_ASSERT_TRUE(runtime.releaseFirstRecordingBarReturnWindow());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->views[set->lifecycle.current].rawCoveragePrepared);
  TEST_ASSERT_EQUAL_UINT16(0, set->views[set->lifecycle.current].storage.eventCount);
}

void test_first_record_window_refresh_reuses_capture_store_and_restarts_on_append() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(32, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(900, 1, 7, 80)));

  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(768));
  const PlaybackViewSourceIdentity source{loop.playbackRevision, 1, loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 16 && runtime.nextCoverageBuildGrain() != RuntimePlaybackViewBuildGrain::Denied; ++i) {
    runtime.serviceFirstRecordingBar(loop, source, 768, 1);
    if (runtime.nextCoverageBuildGrain() == RuntimePlaybackViewBuildGrain::Denied) break;
    if (runtime.viewSetIfPrewarmed()->views[runtime.viewSetIfPrewarmed()->lifecycle.current].storage.eventCount == 2) break;
  }
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT16(2, set->views[set->lifecycle.current].storage.eventCount);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(128, 1, 61, 90)));
  for (size_t i = 0; i < 32; ++i) runtime.serviceFirstRecordingBar(loop, source, 768, 1);
  set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT16(3, set->views[set->lifecycle.current].storage.eventCount);
}

void test_shared_raw_window_binds_final_geometry_without_modulo_or_reordering() {
  const MidiEvent source[] = {MidiEvent::NoteOn(32, 1, 60, 90),
    MidiEvent::NoteOff(32, 1, 61), MidiEvent::NoteOn(800, 1, 62, 90),
    MidiEvent::ControlChange(32, 1, 7, 90)};
  RuntimePlaybackView view, oracle;
  TEST_ASSERT_TRUE(view.prewarm(4)); TEST_ASSERT_TRUE(oracle.prewarm(4));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, source, 4, {1, 1, nullptr}, {0, 768, 0}, {}, {},
                              RuntimePlaybackViewFillScope::RawCoverage));
  TEST_ASSERT_FALSE(view.bindRawCoverage(1536));
  build.stepFill(4);
  while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order) build.stepOrder(1);
  TEST_ASSERT_TRUE(view.complete); TEST_ASSERT_TRUE(view.rawCoveragePrepared);
  TEST_ASSERT_FALSE(view.bindRawCoverage(767));
  const auto* events = view.storage.events;
  const std::vector<uint16_t> order(view.storage.order, view.storage.order + view.storage.eventCount);
  TEST_ASSERT_TRUE(view.bindRawCoverage(1536));
  TEST_ASSERT_FALSE(view.rawCoveragePrepared);
  TEST_ASSERT_FALSE(view.bindRawCoverage(2304));
  TEST_ASSERT_EQUAL_PTR(events, view.storage.events);
  RuntimePlaybackViewBuild reference;
  TEST_ASSERT_TRUE(reference.begin(oracle, source, 4, {1, 1, nullptr}, {0, 768, 1536}, {}, {},
                                  RuntimePlaybackViewFillScope::PlaybackCoverage));
  reference.stepFill(4);
  while (reference.nextGrain() == RuntimePlaybackViewBuildGrain::Order) reference.stepOrder(1);
  TEST_ASSERT_EQUAL_UINT32(3, view.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(oracle.storage.eventCount, view.storage.eventCount);
  for (size_t i = 0; i < order.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT16(order[i], view.storage.order[i]);
    TEST_ASSERT_EQUAL_UINT16(oracle.storage.order[i], view.storage.order[i]);
    TEST_ASSERT_EQUAL_UINT32(oracle.storage.phases[i], view.storage.phases[i]);
  }
}

void test_shared_raw_window_cannot_become_audible_before_binding() {
  RuntimePlaybackViewSet set; TEST_ASSERT_TRUE(set.prewarm());
  auto& life = set.lifecycle;
  auto& ready = set.views[life.ready];
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(ready, nullptr, 0, {1, 1, nullptr}, {0, 768, 0}, {}, {},
                              RuntimePlaybackViewFillScope::RawCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.stepFill(1));
  TEST_ASSERT_TRUE(ready.complete);
  TEST_ASSERT_FALSE(set.handoffReadyCoverage());
  TEST_ASSERT_TRUE(ready.bindRawCoverage(768));
  TEST_ASSERT_TRUE(set.handoffReadyCoverage());
  TEST_ASSERT_EQUAL_UINT8(1, life.current);
}

void test_shared_raw_preparation_rotates_then_returns_only_after_final_binding() {
  RuntimePlaybackViewSet set; TEST_ASSERT_TRUE(set.prewarm());
  auto prepare = [&](uint8_t index, uint32_t bar) {
    const MidiEvent event = MidiEvent::NoteOn(bar * 768 + 32, 1, 60, 90);
    RuntimePlaybackViewBuild build;
    TEST_ASSERT_TRUE(build.begin(set.views[index], &event, 1, {1, 1, nullptr},
        {bar * 768, 768, 0}, {}, {}, RuntimePlaybackViewFillScope::RawCoverage));
    build.stepFill(1);
    while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order) build.stepOrder(1);
    TEST_ASSERT_TRUE(set.views[index].complete);
  };
  auto& life = set.lifecycle;
  for (uint8_t i = 0; i < 3; ++i) prepare(i, i);
  TEST_ASSERT_TRUE(life.retainCurrent(set.views));
  const auto* first = set.views[0].storage.events;
  TEST_ASSERT_FALSE(set.handoffReadyCoverage());
  TEST_ASSERT_TRUE(set.handoffReadyCoverage(PlaybackViewHandoffMode::Preparation));
  // Private bar 2 needs its preparation owner's explicit publication.
  TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady(PlaybackViewHandoffMode::Preparation));
  for (uint32_t bar = 2; bar <= 7; ++bar) {
    TEST_ASSERT_FALSE(set.handoffReadyCoverage());
    TEST_ASSERT_TRUE(set.handoffReadyCoverage(PlaybackViewHandoffMode::Preparation));
    TEST_ASSERT_EQUAL_PTR(first, set.views[life.retained].storage.events);
    TEST_ASSERT_EQUAL_UINT32(32, first[0].tick);
    TEST_ASSERT_FALSE(life.handoffToRetained(set.views));
    if (bar < 7) {
      prepare(life.build, bar + 1);
      TEST_ASSERT_FALSE(set.publishCompletedBuildAsReady());
      TEST_ASSERT_TRUE(set.publishCompletedBuildAsReady(PlaybackViewHandoffMode::Preparation));
    }
  }
  TEST_ASSERT_TRUE(set.views[life.retained].bindRawCoverage(8 * 768));
  TEST_ASSERT_TRUE(life.handoffToRetained(set.views));
  TEST_ASSERT_EQUAL_UINT8(0, life.current);
  TEST_ASSERT_EQUAL_UINT32(8 * 768, set.views[life.current].coverage.loopLengthTicks);
  TEST_ASSERT_FALSE(set.views[life.current].rawCoveragePrepared);
}

/// The session-exit receipt is a ring, not a singleton: a successor session is
/// gated by a *free slot*, never by an unconsumed publisher turn, and the idle
/// publisher drains the oldest receipt first.
void test_session_exit_receipt_ring_is_fifo_and_not_serialized() {
  Loop loop;
  loop.loopLengthTicks = 768;
  TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  TEST_ASSERT_FALSE(loop.hasPendingSessionExitReceipt());
  TEST_ASSERT_EQUAL(0, loop.sessionExitReceiptCount());

  for (size_t i = 0; i < Loop::kSessionExitReceiptCapacity; ++i) {
    // Derive the reason per slot: a partially initialised array would hand off
    // `CommitReason{}` for the extra slots and be refused for the wrong reason.
    const CommitReason reason = (i % 2 == 0) ? CommitReason::OverdubStop
                                             : CommitReason::OverdubStopToStopped;
    loop.openOverdubSession(static_cast<uint32_t>(i));
    // The old singleton refused this once a receipt was pending.
    TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());
    TEST_ASSERT_TRUE(loop.handoffSessionExitSnapshot(reason));
    TEST_ASSERT_EQUAL(i + 1, loop.sessionExitReceiptCount());
    loop.closeOverdubSession();
  }

  // Capacity is a free-slot gate: the next handoff is refused explicitly and the
  // queued receipts stay intact.
  loop.openOverdubSession(0);
  TEST_ASSERT_FALSE(loop.canHandoffSessionExitSnapshot());
  TEST_ASSERT_FALSE(loop.handoffSessionExitSnapshot(CommitReason::OverdubStop));
  TEST_ASSERT_EQUAL(Loop::kSessionExitReceiptCapacity, loop.sessionExitReceiptCount());

  // FIFO order: index 0 is the oldest receipt handed off.
  for (size_t i = 0; i < Loop::kSessionExitReceiptCapacity; ++i) {
    const SessionExitSnapshotHandoff* receipt = loop.sessionExitReceiptAt(i);
    TEST_ASSERT_NOT_NULL(receipt);
    const CommitReason expectedReason = (i % 2 == 0) ? CommitReason::OverdubStop
                                                     : CommitReason::OverdubStopToStopped;
    TEST_ASSERT_EQUAL(static_cast<int>(expectedReason), static_cast<int>(receipt->reason));
  }
  TEST_ASSERT_NULL(loop.sessionExitReceiptAt(Loop::kSessionExitReceiptCapacity));
}

void test_frozen_record_source_is_windowed_quantized_and_immutable() {
  LoopEventStore producer;
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(frozen.reserveTail());
  const size_t count = RuntimePlaybackViewConfig::kRecordsPerBridge + 257;
  for (size_t i = 0; i < count; ++i)
    TEST_ASSERT_TRUE(producer.append(MidiEvent::ControlChange(500, 1, 1, 2)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(778, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(778, 1, 60)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::ControlChange(778, 1, 2, 3)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(10, 1, 61)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(frozen));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(10, 1, 70, 100)));
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(2));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, RuntimePlaybackViewSource::fromCaptureRange(frozen),
      PlaybackViewSourceIdentity{1, 1, nullptr}, PlaybackCoverageInterval{0, 20, 768},
      EmissionCursor{}, LedgerBoundarySummary{}, RuntimePlaybackViewFillScope::RecordCoverage));
  const auto chunks = LoopEventStore::usedChunkCount();
  const size_t grains = (count + 4 + 31) / 32;
  size_t served = 0;
  for (; served < 2 * grains && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++served) {
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepFill(32));
    TEST_ASSERT_EQUAL(chunks, LoopEventStore::usedChunkCount());
  }
  TEST_ASSERT_TRUE(served > 0 && served <= 2 * grains);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.nextGrain());
  TEST_ASSERT_EQUAL(2, view.storage.eventCount);
  TEST_ASSERT_EQUAL(10, view.storage.events[0].tick);
  TEST_ASSERT_TRUE(view.storage.events[0].isNoteOn());
  TEST_ASSERT_TRUE(view.storage.events[1].isNoteOff());
  MidiEvent original; bool end = false;
  TEST_ASSERT_TRUE(frozen.read(frozen.generation(), count / 256, count % 256, original, end));
  TEST_ASSERT_EQUAL(778, original.tick);
  // Once copied, ordering owns its rows and no longer borrows the capture.
  const size_t releaseGrains = frozen.chunkCount();
  for (size_t i = 0; i < releaseGrains; ++i)
    TEST_ASSERT_EQUAL(i + 1 == releaseGrains, frozen.releaseGrain(1));
  for (size_t i = 0; i < 32 && build.active(); ++i) build.stepOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_EQUAL(1, view.storage.order[0]);
  TEST_ASSERT_EQUAL(0, view.storage.order[1]);
}

void test_frozen_record_source_denies_expiry_and_window_overflow() {
  LoopEventStore producer;
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(frozen.reserveTail());
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(2));
  RuntimePlaybackViewFill fill;
  auto begin = [&]() {
    return fill.begin(view, RuntimePlaybackViewSource::fromCaptureRange(frozen),
        PlaybackViewSourceIdentity{}, PlaybackCoverageInterval{0, 768, 768},
        EmissionCursor{}, LedgerBoundarySummary{}, RuntimePlaybackViewFillScope::RecordCoverage);
  };
  TEST_ASSERT_FALSE(begin());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(1, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(2, 1, 61, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(3, 1, 62, 100)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(frozen));
  TEST_ASSERT_TRUE(begin());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Pending, fill.step(1)); // one block summary
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Pending, fill.step(2)); // two rows
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Denied, fill.step(1)); // third exceeds capacity
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_FALSE(view.sourceFilled);
  // Ensure the expiry assertion cannot pass merely due to window overflow.
  TEST_ASSERT_TRUE(begin());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Pending, fill.step(1));
  TEST_ASSERT_TRUE(frozen.releaseGrain(1));
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(3, 1, 62, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(4, 1, 63, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(5, 1, 64, 100)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(frozen)); // same extent, new generation
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Denied, fill.step(1));
  TEST_ASSERT_FALSE(view.sourceFilled);
  TEST_ASSERT_TRUE(frozen.releaseGrain(1));
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(producer.sealCaptureRange(frozen));
  TEST_ASSERT_TRUE(begin()); // sealed empty range is valid
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Complete, fill.step(1));
}

void test_frozen_record_source_rejects_pool_reset_and_failed_row_reads() {
  LoopEventStore producer;
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(1, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(frozen));
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(2));
  RuntimePlaybackViewFill fill;
  auto source = RuntimePlaybackViewSource::fromCaptureRange(frozen);
  // Fault injection at the fallible reader boundary: no row may publish.
  source.readEventAt = [](const void*, size_t, MidiEvent&) { return false; };
  TEST_ASSERT_TRUE(fill.begin(view, source, PlaybackViewSourceIdentity{},
      PlaybackCoverageInterval{0, 768, 768}, EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Denied, fill.step(1));
  TEST_ASSERT_EQUAL(0, view.storage.eventCount);
  TEST_ASSERT_FALSE(view.sourceFilled);
  source = RuntimePlaybackViewSource::fromCaptureRange(frozen);
  TEST_ASSERT_TRUE(fill.begin(view, source, PlaybackViewSourceIdentity{},
      PlaybackCoverageInterval{0, 768, 768}, EmissionCursor{}, LedgerBoundarySummary{}));
  LoopEventStore::resetPoolForTests();
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Denied, fill.step(1));
  TEST_ASSERT_FALSE(view.sourceFilled);
}

ImmutableLoopSnapshotRef bindingSnapshot(Loop& loop, unsigned count = 80) {
  auto parent = LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, 0, 1, 1);
  LoopSnapshotChangeSet changes;
  for (unsigned i = 0; i < count; ++i) {
    SnapshotEventChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next.loopId = loop.loopId;
    change.next.eventId = 0x10000u * (1 + (i % 5)) + (count - i);
    change.next.canonicalOrder = 0x10000u + (count - i) / 4;
    change.next.value = MidiEvent::ControlChange(i % 7 == 0 ? 900 : 200, 1, 7, i % 128);
    if (i == 2) change.next.value = MidiEvent::NoteOn(200, 1, 60, 100);
    if (i == 3) change.next.value = MidiEvent::NoteOff(200, 1, 60, 0);
    if (i == 4) change.next.value = MidiEvent::ControlChange(99, 1, 7, 4);
    if (i == 5) change.next.value = MidiEvent::ControlChange(1636, 1, 7, 5);
    changes.events.push_back(change);
  }
  LoopSnapshotConstruction construction(parent, changes);
  for (unsigned turn = 0; turn < 10000 && !construction.complete(); ++turn)
    TEST_ASSERT_TRUE(construction.step(64));
  TEST_ASSERT_TRUE(construction.complete());
  auto root = construction.freeze();
  TEST_ASSERT_NOT_NULL(root.get());
  loop.storePublishedSnapshotForTest(root, loop.playbackRevision);
  return root;
}

void retireBindingSource(LoopSnapshot::EventWindowCursor& source) {
  source.beginRelease();
  for (unsigned turn = 0; turn < 10000 && !source.idle(); ++turn) source.stepRelease(16);
  TEST_ASSERT_TRUE(source.idle());
}

void test_dense_snapshot_pipeline_measures_work_after_ping_pong_ordering() {
  constexpr size_t count = 2048;
  Loop loop;
  loop.loopLengthTicks = 512 * 768;
  auto parent = LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, 0, 1, 1);
  LoopSnapshotChangeSet changes;
  for (size_t i = 0; i < count; ++i) {
    SnapshotEventChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next.loopId = loop.loopId;
    change.next.eventId = i + 1;
    // Each bar's sorted canonical membership interleaves with the other bar.
    // Raw ticks are intentionally not canonical order, so both sorts do work.
    change.next.canonicalOrder = count - i;
    change.next.value = MidiEvent::ControlChange((i % 2) * 768 + (i * 137) % 768, 1, 7, i % 128);
    changes.events.push_back(change);
  }
  LoopSnapshotConstruction construction(parent, changes);
  for (unsigned i = 0; i < 20000 && !construction.complete(); ++i)
    TEST_ASSERT_TRUE(construction.step(64));
  TEST_ASSERT_TRUE(construction.complete());
  auto root = construction.freeze();
  TEST_ASSERT_NOT_NULL(root.get());
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmRollingViews());
  auto& request = runtime.pendingViewRequest_;
  request.active = true;
  request.sourceKind = PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
  request.source = {1, 1, root.get()};
  request.coverage = {0, 1536, loop.loopLengthTicks};
  request.loopLengthTicks = loop.loopLengthTicks;
  auto& source = runtime.coverageSource;
  auto* set = runtime.viewSetIfPrewarmed();
  auto& view = set->views[set->lifecycle.build];
  TEST_ASSERT_TRUE(runtime.beginCoverageQualification(*root));
  const auto token = source.traversalGeneration();
  size_t fills = 0, orders = 0;
  while (runtime.coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Counting && fills < 300) {
    const auto before = source.readUnits();
    TEST_ASSERT_TRUE(runtime.stepCoverageQualification(64));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(64, source.readUnits() - before);
    TEST_ASSERT_FALSE(view.complete);
    TEST_ASSERT_FALSE(view.published);
    TEST_ASSERT_FALSE(runtime.coverageBuildNeedsService());
    ++fills;
  }
  TEST_ASSERT_TRUE(source.complete());
  TEST_ASSERT_EQUAL_UINT32(2178, source.readUnits());
  TEST_ASSERT_EQUAL_UINT32(35, fills);
  TEST_ASSERT_EQUAL_UINT32(2178, source.qualifiedReleaseUnitBound());
  TEST_ASSERT_EQUAL_UINT32(count, view.storage.eventCount);
  TEST_ASSERT_TRUE(view.sourceFilled);
  CoverageServiceContract contract;
  contract.deriveSnapshotGrainsFrom(source.readUnits(), count, 64, 48);
  contract.requiredFillGrains = 0;
  TEST_ASSERT_TRUE(runtime.beginQualifiedCoverageBuild(contract, {}));
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(token, source.traversalGeneration(), "admission never rewinds qualified source");
  TEST_ASSERT_EQUAL_UINT32(0, runtime.viewBuildWork.fillGrains);
  while (runtime.coverageBuildNeedsService() && orders < 3000) {
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, runtime.nextCoverageBuildGrain());
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, runtime.stepCoverageBuildOrder(48));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2178, source.readUnits(), "ordering never rereads the qualified source");
    ++orders;
  }
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1153, orders, "two 555-turn sorts plus 43 rank turns");
  TEST_ASSERT_EQUAL_UINT32(1188, fills + orders);
  std::vector<size_t> expected(count);
  for (size_t i = 0; i < count; ++i) expected[i] = i;
  std::sort(expected.begin(), expected.end(), [&](size_t a, size_t b) {
    const auto& x = changes.events[a].next;
    const auto& y = changes.events[b].next;
    if (x.value.tick != y.value.tick) return x.value.tick < y.value.tick;
    return x.canonicalOrder < y.canonicalOrder;
  });
  for (size_t i = 0; i < count; ++i) {
    const auto& actual = view.storage.events[view.storage.order[i]];
    const auto& wanted = changes.events[expected[i]].next.value;
    TEST_ASSERT_EQUAL_UINT32(wanted.tick, actual.tick);
    TEST_ASSERT_EQUAL_UINT8(wanted.data.ccData.value, actual.data.ccData.value);
  }
  source.beginRelease();
  size_t retirementTurns = 0;
  while (!source.idle() && retirementTurns < 300) {
    source.stepRelease(64);
    ++retirementTurns;
  }
  TEST_ASSERT_TRUE(source.idle());
  TEST_ASSERT_EQUAL_UINT32(1, retirementTurns); // Root still owns the shared heads.
  // One qualification/fill pass, plus freeze/admit, orders and retirement.
  const size_t lifecycleTurns = 2 + fills + orders + retirementTurns;
  TEST_ASSERT_EQUAL_UINT32(1191, lifecycleTurns);
  TEST_ASSERT_TRUE_MESSAGE(8 * lifecycleTurns * 64 > 1536,
      "measured bounded work still exceeds continuous two-bar service supply");
}

void test_qualification_fill_is_private_and_cursor_rewind_denies_without_touching_current() {
  Loop loop;
  loop.loopLengthTicks = 512 * 768;
  auto root = bindingSnapshot(loop);
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmRollingViews());
  auto* set = runtime.viewSetIfPrewarmed();
  auto& current = set->views[set->lifecycle.current];
  TEST_ASSERT_TRUE(current.storage.append(MidiEvent::NoteOn(17, 1, 67, 99)));
  current.complete = current.published = true;
  auto& request = runtime.pendingViewRequest_;
  request.active = true;
  request.sourceKind = PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
  request.source = {loop.playbackRevision, 1, root.get()};
  request.coverage = {100, 1536, loop.loopLengthTicks};
  auto& candidate = set->views[set->lifecycle.build];
  TEST_ASSERT_TRUE(runtime.beginCoverageQualification(*root));
  TEST_ASSERT_TRUE(runtime.stepCoverageQualification(8));
  TEST_ASSERT_GREATER_THAN_UINT32(0, candidate.storage.eventCount);
  TEST_ASSERT_FALSE(candidate.sourceFilled);
  TEST_ASSERT_FALSE(candidate.complete);
  TEST_ASSERT_FALSE(candidate.published);
  TEST_ASSERT_FALSE(runtime.viewBuild.admitCoverageQualification());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, runtime.stepCoverageBuildOrder(48));
  TEST_ASSERT_TRUE(runtime.coverageSource.rewind());
  TEST_ASSERT_FALSE(runtime.stepCoverageQualification(8));
  TEST_ASSERT_TRUE(runtime.coverageSource.retiring());
  TEST_ASSERT_FALSE(candidate.sourceFilled);
  TEST_ASSERT_FALSE(candidate.complete);
  TEST_ASSERT_TRUE(current.complete && current.published);
  TEST_ASSERT_EQUAL_UINT32(17, current.storage.events[0].tick);
  TEST_ASSERT_EQUAL_UINT32(1, current.storage.eventCount);
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackViewPendingReason::SourceNotCurrent),
      static_cast<int>(runtime.pendingViewRequest_.reason));
  for (unsigned i = 0; i < 256 && runtime.ownsCoverageSource(); ++i)
    runtime.stepCoverageSourceRetirement(64);
  TEST_ASSERT_FALSE(runtime.ownsCoverageSource());
}

void test_snapshot_runtime_binding_matches_gather_and_preserves_canonical_ties() {
  for (size_t quantum : {size_t(1), size_t(7), size_t(64)}) {
    Loop loop;
    loop.loopLengthTicks = 300 * 768;
    auto root = bindingSnapshot(loop);
    SessionMidiEventVec flat;
    Loop::SnapshotWindowGatherPath path{};
    TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(flat, 100, 1536, &path));
    TEST_ASSERT_EQUAL(Loop::SnapshotWindowGatherPath::Snapshot, path);
    TEST_ASSERT_EQUAL_UINT32(78, flat.size());
    const PlaybackCoverageInterval coverage{100, 1536, loop.loopLengthTicks};
    const PlaybackViewSourceIdentity identity{loop.playbackRevision, 11, root.get()};
    RuntimePlaybackView reference;
    TEST_ASSERT_TRUE(reference.prewarm(128));
    RuntimePlaybackViewBuild referenceBuild;
    TEST_ASSERT_TRUE(referenceBuild.begin(reference, flat.data(), flat.size(), identity, coverage, {}, {}));
    for (unsigned turn = 0; turn < 10000 && referenceBuild.active(); ++turn) {
      if (referenceBuild.nextGrain() == RuntimePlaybackViewBuildGrain::Fill) referenceBuild.stepFill(quantum);
      else referenceBuild.stepOrder(quantum);
    }
    TEST_ASSERT_TRUE(reference.complete);

    LoopSnapshot::EventWindowCursor source;
    TEST_ASSERT_TRUE(source.begin(*root, 100, 1536));
    LoopPlaybackRuntime runtime;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    const auto bytes = runtime.viewSetIfPrewarmed()->fixedExternalBytes();
    CoverageServiceContract contract;
    contract.maximumPhaseAdvancePerTurn = 1;
    contract.leadTicks = 1;
    contract.deriveSnapshotGrainsFrom(LoopSnapshot::EventWindowCursor::kMaximumReadUnits, flat.size(), quantum, quantum);
    TEST_ASSERT_TRUE(runtime.beginCoverageBuild(contract, 200, 100, source, identity, coverage, {}, {}));
    const auto* set = runtime.viewSetIfPrewarmed();
    const auto& view = set->views[set->lifecycle.build];
    size_t fillTurns = 0;
    for (; fillTurns < 10000 && runtime.nextCoverageBuildGrain() == RuntimePlaybackViewBuildGrain::Fill; ++fillTurns) {
      const auto before = source.readUnits();
      TEST_ASSERT_FALSE(view.complete);
      runtime.stepCoverageBuildFill(quantum);
      TEST_ASSERT_TRUE(source.readUnits() - before <= quantum);
      TEST_ASSERT_FALSE(view.complete); // Not even the final fill grain publishes.
    }
    TEST_ASSERT_TRUE(source.complete());
    TEST_ASSERT_TRUE(view.sourceFilled);
    TEST_ASSERT_FALSE(view.passOriginsReady);
    TEST_ASSERT_FALSE(view.canonicalRanksReady);
    const size_t sourceUnits = source.readUnits();
    contract.deriveSnapshotGrainsFrom(sourceUnits, flat.size(), quantum, quantum);
    TEST_ASSERT_EQUAL_UINT32(contract.requiredFillGrains, fillTurns);
    // The order phase is independent of the root AND the retired query cursor.
    loop.storePublishedSnapshotForTest({}, 0);
    root.reset();
    retireBindingSource(source);
    size_t orderTurns = 0;
    for (; orderTurns < 10000 && runtime.coverageBuildNeedsService(); ++orderTurns) {
      TEST_ASSERT_FALSE(view.complete);
      runtime.stepCoverageBuildOrder(quantum);
    }
    TEST_ASSERT_TRUE(view.complete);
    TEST_ASSERT_TRUE(orderTurns <= contract.requiredOrderGrains);
    TEST_ASSERT_TRUE(view.canonicalRanksReady);
    TEST_ASSERT_FALSE(view.passOriginsReady);
    TEST_ASSERT_EQUAL_UINT32(flat.size(), view.storage.eventCount);
    TEST_ASSERT_EQUAL_UINT64(bytes, runtime.viewSetIfPrewarmed()->fixedExternalBytes());
    for (size_t i = 0; i < flat.size(); ++i) {
      const auto& actual = view.storage.events[view.storage.order[i]];
      const auto& expected = reference.storage.events[reference.storage.order[i]];
      TEST_ASSERT_EQUAL_UINT32(expected.tick, actual.tick);
      TEST_ASSERT_EQUAL(expected.type, actual.type);
      TEST_ASSERT_EQUAL(expected.channel, actual.channel);
      // CC and note payloads are the same two bytes in this fixture.
      TEST_ASSERT_EQUAL(expected.data.ccData.cc, actual.data.ccData.cc);
      TEST_ASSERT_EQUAL(expected.data.ccData.value, actual.data.ccData.value);
    }
  }
}

void test_snapshot_binding_denies_rebound_cursor_overflow_and_published_destination() {
  Loop loop;
  loop.loopLengthTicks = 300 * 768;
  auto root = bindingSnapshot(loop);
  LoopSnapshot::EventWindowCursor source;
  const PlaybackCoverageInterval coverage{100, 1536, loop.loopLengthTicks};
  const PlaybackViewSourceIdentity identity{loop.playbackRevision, 11, root.get()};
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(8));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(source.begin(*root, 100, 1536));
  view.published = true;
  TEST_ASSERT_FALSE(build.begin(view, source, identity, coverage, {}, {}));
  view.published = false;
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {}));
  // Rewind before the first grain leaves position zero; only the token catches it.
  TEST_ASSERT_TRUE(source.rewind());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepFill(1));
  TEST_ASSERT_FALSE(view.complete);
  build.discard();
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {}));
  for (unsigned turn = 0; turn < 1000 && build.active(); ++turn) build.stepFill(3);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.nextGrain());
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_FALSE(view.sourceFilled);
  TEST_ASSERT_EQUAL_UINT32(8, view.storage.eventCount); // Refuse, never publish a truncated result.
  build.discard();
  retireBindingSource(source);
  TEST_ASSERT_TRUE(source.begin(*root, 100, 1536));
  TEST_ASSERT_FALSE(build.begin(view, source, identity, {100, 1536, 400 * 768}, {}, {}));
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {}));
  source.beginRelease();
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepFill(1));
  build.discard();
  retireBindingSource(source);
  RuntimePlaybackOrderBuilder order;
  TEST_ASSERT_FALSE(order.begin(view.storage, loop.loopLengthTicks, nullptr, RuntimePlaybackOrderMode::SnapshotCanonical));
  TEST_ASSERT_FALSE(order.begin(view.storage, loop.loopLengthTicks, nullptr, RuntimePlaybackOrderMode::SnapshotPlayback));
}

void test_snapshot_binding_empty_window_completes_and_costs_saturate() {
  auto root = LoopSnapshot::createEmpty(1, 300 * 768, 0, 1, 1);
  LoopSnapshot::EventWindowCursor source;
  TEST_ASSERT_TRUE(source.begin(*root, 100, 1536));
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(8));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, source, {1, 1, root.get()}, {100, 1536, 300 * 768}, {}, {}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.stepFill(64));
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.stepOrder(64));
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_EQUAL_UINT32(0, view.storage.eventCount);
  retireBindingSource(source);
  CoverageServiceContract cost;
  cost.deriveSnapshotGrainsFrom(3, 0, 64, 48);
  TEST_ASSERT_EQUAL_UINT16(1, cost.requiredFillGrains);
  TEST_ASSERT_EQUAL_UINT16(1, cost.requiredOrderGrains);
  cost.deriveSnapshotGrainsFrom(SIZE_MAX, SIZE_MAX, 1, 1);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, cost.requiredFillGrains);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, cost.requiredOrderGrains);
  cost.deriveSnapshotGrainsFrom(0, 0, 0, 0);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, cost.requiredFillGrains);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, cost.requiredOrderGrains);
}

// ---- revision-manifest C5.1 (DEC-246 amendments 19-20): the consumer contract of a committed window ----
// The same committed passes, read once through the published root's `EventWindowCursor` and once through the
// RevisionManifest's `ManifestWindowCursor`, through the **real** build (fill -> canonical order -> ranks -> playback
// order) must give the consumer the same ordered tuple: event, playback phase, rank, and provenance class (committed,
// never the live-overlay origin). Shape: a cross-pass equal-tick tie, same-pitch duplicates across passes, a NoteOff and
// a NoteOn on one tick, the loop's last bar, and a coverage that starts mid-bar.
namespace c5 {
constexpr uint32_t kBar = Config::TICKS_PER_BAR;
constexpr uint32_t kLength = 4 * kBar;
using Cap = RevisionStore::Capacity<512, 32, 16, 128, 8, 8, 4>;

void appendPass(LoopEventStore& store, const std::vector<std::tuple<uint32_t, bool, uint8_t>>& rows, NoteId& nextId) {
  for (const auto& [tick, on, pitch] : rows) {
    if (on) {
      TEST_ASSERT_TRUE(NoteIdTestFixtures::storeAppendNoteOn(store, tick, 1, pitch, 100, nextId++));
    } else {
      TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick, 1, pitch, 0)));
    }
  }
}

void buildLoop(Loop& loop) {
  loop.loopLengthTicks = kLength;
  NoteId nextId = 7001;
  RecordPass record{};
  record.id = 1;
  LoopEventStore recordStore;
  appendPass(recordStore,
             {{0, true, 60}, {96, false, 60}, {96, true, 62}, {192, false, 62}, {768, true, 60}, {864, false, 60},
              {1500, true, 65}, {1700, false, 65}, {2300, true, 67}, {2500, false, 67}, {3000, true, 69}, {3070, false, 69}},
             nextId);
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(recordStore, record.committedChunkIds));
  loop.passes.recordPass = std::move(record);
  OverdubPass overdub{};
  overdub.id = 2;
  overdub.mergeSequence = 1;
  LoopEventStore overdubStore;
  // A same-tick NoteOn on another pitch (cross-pass tie), a same-pitch duplicate, and a note across the wrap region.
  appendPass(overdubStore,
             {{0, true, 64}, {96, false, 64}, {768, true, 60}, {800, false, 60}, {2304, true, 71}, {2400, false, 71}},
             nextId);
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(overdubStore, overdub.committedChunkIds));
  loop.passes.overdubPasses.push_back(std::move(overdub));
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
  for (unsigned turn = 0; turn < 4096 && loop.hasPendingSnapshotPublish(); ++turn) {
    (void)loop.stepPendingSnapshotPublish(64, true, 0);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(), "fixture precondition: a current root from the passes");
}

template <typename Begin>
void drive(RuntimePlaybackView& view, RuntimePlaybackViewBuild& build, Begin begin) {
  TEST_ASSERT_TRUE(view.prewarm(128));
  TEST_ASSERT_TRUE(begin());
  for (unsigned turn = 0; turn < 100000 && build.active(); ++turn) {
    if (build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill) build.stepFill(3);
    else build.stepOrder(3);
  }
  TEST_ASSERT_TRUE(view.complete);
}
}  // namespace c5

void test_c5_manifest_window_gives_the_consumer_the_snapshot_windows_tuple() {
  using namespace RevisionStore;
  Loop loop;
  c5::buildLoop(loop);
  static Store<c5::Cap> store;
  store.reset();
  TEST_ASSERT_EQUAL(ImportResult::Ok, importLoop(loop, store));
  const ManifestId m = store.current();
  static PassEvent staging[256];
  const PlaybackCoverageInterval coverages[] = {
      {0, 2 * c5::kBar, c5::kLength}, {2 * c5::kBar, 2 * c5::kBar, c5::kLength}, {3 * c5::kBar, c5::kBar, c5::kLength},
      {c5::kBar / 2, c5::kBar, c5::kLength}};
  for (const PlaybackCoverageInterval& coverage : coverages) {
    const auto root = loop.publishedSnapshot();
    TEST_ASSERT_NOT_NULL(root.get());
    RuntimePlaybackView snapshotView;
    RuntimePlaybackViewBuild snapshotBuild;
    LoopSnapshot::EventWindowCursor snapshotSource;
    TEST_ASSERT_TRUE(snapshotSource.begin(*root, coverage.startPhase, coverage.lengthTicks));
    const PlaybackViewSourceIdentity snapshotIdentity{loop.playbackRevision, 1, root.get()};
    c5::drive(snapshotView, snapshotBuild, [&] {
      return snapshotBuild.begin(snapshotView, snapshotSource, snapshotIdentity, coverage, {}, {});
    });
    retireBindingSource(snapshotSource);

    RuntimePlaybackView manifestView;
    RuntimePlaybackViewBuild manifestBuild;
    ManifestWindowCursor<c5::Cap> manifestSource(staging, 256);
    TEST_ASSERT_EQUAL(WindowResult::Pending,
                      manifestSource.begin(store, m, store.identityOf(m), store.manifest(m).revision, coverage.startPhase,
                                           coverage.lengthTicks, coverage.loopLengthTicks));
    const PlaybackViewSourceIdentity manifestIdentity{store.manifest(m).revision, 1, nullptr};
    c5::drive(manifestView, manifestBuild, [&] {
      return manifestBuild.begin(manifestView, manifestSource, manifestIdentity, coverage, {}, {});
    });
    TEST_ASSERT_EQUAL(WindowResult::Ready, manifestSource.result());

    char label[96];
    snprintf(label, sizeof(label), "coverage start=%u: snapshot rows=%u manifest rows=%u", coverage.startPhase,
             snapshotView.storage.eventCount, manifestView.storage.eventCount);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(snapshotView.storage.eventCount, manifestView.storage.eventCount, label);
    TEST_ASSERT_TRUE_MESSAGE(manifestView.storage.eventCount > 0, label);
    for (uint32_t i = 0; i < snapshotView.storage.eventCount; ++i) {
      const uint16_t a = snapshotView.storage.order[i];
      const uint16_t b = manifestView.storage.order[i];
      const MidiEvent& x = snapshotView.storage.events[a];
      const MidiEvent& y = manifestView.storage.events[b];
      snprintf(label, sizeof(label), "start=%u order %u: snapshot t=%u %u/%u manifest t=%u %u/%u", coverage.startPhase,
               i, x.tick, x.data.noteData.note, x.data.noteData.velocity, y.tick, y.data.noteData.note,
               y.data.noteData.velocity);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(x.tick, y.tick, label);
      TEST_ASSERT_EQUAL_MESSAGE(x.type, y.type, label);
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(x.channel, y.channel, label);
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(x.data.noteData.note, y.data.noteData.note, label);
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(x.data.noteData.velocity, y.data.noteData.velocity, label);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(snapshotView.storage.phases[a], manifestView.storage.phases[b], label);
      TEST_ASSERT_EQUAL_UINT16_MESSAGE(snapshotView.canonicalRanks[a], manifestView.canonicalRanks[b], label);
      // Provenance class: committed on both sides, never the live-overlay origin.
      TEST_ASSERT_NOT_EQUAL_MESSAGE(kLiveOverlayPassOrigin, snapshotView.passOrigins[a].pass, label);
      TEST_ASSERT_NOT_EQUAL_MESSAGE(kLiveOverlayPassOrigin, manifestView.passOrigins[b].pass, label);
    }
    TEST_ASSERT_EQUAL(snapshotView.canonicalRanksReady, manifestView.canonicalRanksReady);
    TEST_ASSERT_EQUAL(snapshotView.passOriginsReady, manifestView.passOriginsReady);
  }
}

// C5.2: the cursor's named answers. A manifest that moves before the read is SourceMoved, never a window of another
// identity; an owed index is IndexOwed, never a scan.
void test_c5_manifest_window_cursor_refuses_by_name() {
  using namespace RevisionStore;
  Loop loop;
  c5::buildLoop(loop);
  static Store<c5::Cap> store;
  store.reset();
  TEST_ASSERT_EQUAL(ImportResult::Ok, importLoop(loop, store));
  const ManifestId m = store.current();
  static PassEvent staging[256];
  ManifestWindowCursor<c5::Cap> cursor(staging, 256);
  TEST_ASSERT_EQUAL(WindowResult::SourceMoved,
                    cursor.begin(store, m, store.identityOf(m) + 1, store.manifest(m).revision, 0, c5::kBar, c5::kLength));
  TEST_ASSERT_TRUE(cursor.idle());
  TEST_ASSERT_EQUAL(WindowResult::ImportOwed,
                    cursor.begin(store, kNoManifest, 0, 0, 0, c5::kBar, c5::kLength));
  // The snapshot cursor's shapes only: a coverage that wraps the loop end or exceeds two bars is refused by name.
  TEST_ASSERT_EQUAL(WindowResult::CapacityDenied,
                    cursor.begin(store, m, store.identityOf(m), store.manifest(m).revision, 3 * c5::kBar, 2 * c5::kBar,
                                 c5::kLength));
  TEST_ASSERT_EQUAL(WindowResult::CapacityDenied,
                    cursor.begin(store, m, store.identityOf(m), store.manifest(m).revision, 0, 3 * c5::kBar, c5::kLength));
  TEST_ASSERT_FALSE(store.windowActive());
  // A window that cannot fit its rows is a named CapacityDenied and ends Inactive, never truncated.
  ManifestWindowCursor<c5::Cap> small(staging, 2);
  TEST_ASSERT_EQUAL(WindowResult::Pending,
                    small.begin(store, m, store.identityOf(m), store.manifest(m).revision, 0, 2 * c5::kBar, c5::kLength));
  LoopSnapshotEvent row{};
  CommittedWindowRowSource::ReadResult r = CommittedWindowRowSource::ReadResult::Progress;
  for (unsigned i = 0; i < 100 && (r == CommittedWindowRowSource::ReadResult::Progress ||
                                   r == CommittedWindowRowSource::ReadResult::Event); ++i) {
    r = small.step(row);
  }
  TEST_ASSERT_EQUAL(CommittedWindowRowSource::ReadResult::Inactive, r);
  TEST_ASSERT_EQUAL(WindowResult::CapacityDenied, small.result());
  TEST_ASSERT_FALSE(store.windowActive());  // the pin is released
}

// C5.3b buildGrantBound(E): the post-admission cost of a committed manifest window, pinned per stage at the lane's
// production quanta (qualification fill 64, order 48) and in exact units (quantum 1). The canonical order's key is the
// row's `canonicalOrder = origin << 16 | ordinal` (pass-major), so its fill pass sees ordered input only while the
// window's passes do not interleave in time; otherwise the bottom-up merge runs every level over all E entries, plus a
// copy-back when the level count is odd. Playback order's input is ordered by construction. Each window's final
// order, phases and ranks also equal the published root's own.
namespace c5cost {
using Rows = std::vector<std::tuple<uint32_t, bool, uint8_t>>;

/// A capture appends chronologically: each pass's rows in tick order (stable, so a fixture's equal-tick order stays).
Rows chronological(Rows rows) {
  std::stable_sort(rows.begin(), rows.end(),
                   [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
  return rows;
}

void buildLoop(Loop& loop, const Rows& recordRows, const Rows& overdubRows) {
  loop.loopLengthTicks = c5::kLength;
  NoteId nextId = 7001;
  RecordPass record{};
  record.id = 1;
  LoopEventStore recordStore;
  c5::appendPass(recordStore, chronological(recordRows), nextId);
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(recordStore, record.committedChunkIds));
  loop.passes.recordPass = std::move(record);
  if (!overdubRows.empty()) {
    OverdubPass overdub{};
    overdub.id = 2;
    overdub.mergeSequence = 1;
    LoopEventStore overdubStore;
    c5::appendPass(overdubStore, chronological(overdubRows), nextId);
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(overdubStore, overdub.committedChunkIds));
    loop.passes.overdubPasses.push_back(std::move(overdub));
  }
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
  for (unsigned turn = 0; turn < 4096 && loop.hasPendingSnapshotPublish(); ++turn) {
    (void)loop.stepPendingSnapshotPublish(64, true, 0);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(), "fixture precondition: a current root from the passes");
}

/// Notes `[on, on + 40)` at `first + k * spacing`, pitch 60 + k % 24.
void addNotes(Rows& rows, uint32_t first, uint32_t spacing, uint32_t count) {
  for (uint32_t k = 0; k < count; ++k) {
    const uint32_t on = first + k * spacing;
    const uint8_t pitch = static_cast<uint8_t>(60 + k % 24);
    rows.emplace_back(on, true, pitch);
    rows.emplace_back(on + 40, false, pitch);
  }
}

struct StageCost {
  uint32_t rows = 0;
  uint32_t readUnits = 0;
  uint32_t qualification = 0;   // stepCoverageQualification calls to Complete
  uint32_t canonicalRanks = 0;  // stepOrder calls until canonicalRanksReady (canonical order, then ranks)
  uint32_t playback = 0;        // stepOrder calls after it, to Complete
};

StageCost measure(RevisionStore::Store<c5::Cap>& store, RevisionStore::ManifestWindowCursor<c5::Cap>* source,
                  const PlaybackCoverageInterval& coverage, size_t fillQuantum, size_t orderQuantum,
                  RuntimePlaybackView& view, RuntimePlaybackViewBuild& build) {
  using namespace RevisionStore;
  const ManifestId m = store.current();
  TEST_ASSERT_EQUAL(WindowResult::Pending, source->begin(store, m, store.identityOf(m), store.manifest(m).revision,
                                                         coverage.startPhase, coverage.lengthTicks,
                                                         coverage.loopLengthTicks));
  TEST_ASSERT_TRUE(view.prewarm(128));
  const PlaybackViewSourceIdentity identity{store.manifest(m).revision, 1, nullptr};
  TEST_ASSERT_TRUE(build.beginCoverageQualification(view, *source, identity, coverage));
  StageCost cost{};
  RuntimePlaybackViewFillResult fill = RuntimePlaybackViewFillResult::Pending;
  while (fill == RuntimePlaybackViewFillResult::Pending && cost.qualification < 100000) {
    fill = build.stepCoverageQualification(fillQuantum);
    ++cost.qualification;
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillResult::Complete, fill);
  cost.rows = view.storage.eventCount;
  cost.readUnits = static_cast<uint32_t>(source->readUnits());
  TEST_ASSERT_TRUE(build.admitCoverageQualification());
  while (!view.canonicalRanksReady && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order &&
         cost.canonicalRanks < 100000) {
    (void)build.stepOrder(orderQuantum);
    ++cost.canonicalRanks;
  }
  TEST_ASSERT_TRUE(view.canonicalRanksReady);
  while (build.nextGrain() == RuntimePlaybackViewBuildGrain::Order && cost.playback < 100000) {
    (void)build.stepOrder(orderQuantum);
    ++cost.playback;
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_TRUE(view.complete);
  return cost;
}

/// The published root's window through the snapshot build: the parity oracle for order, phase and rank.
void expectSnapshotParity(Loop& loop, const PlaybackCoverageInterval& coverage, const RuntimePlaybackView& manifestView,
                          const char* name) {
  const auto root = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(root.get());
  RuntimePlaybackView snapshotView;
  RuntimePlaybackViewBuild snapshotBuild;
  LoopSnapshot::EventWindowCursor snapshotSource;
  TEST_ASSERT_TRUE(snapshotSource.begin(*root, coverage.startPhase, coverage.lengthTicks));
  const PlaybackViewSourceIdentity snapshotIdentity{loop.playbackRevision, 1, root.get()};
  c5::drive(snapshotView, snapshotBuild, [&] {
    return snapshotBuild.begin(snapshotView, snapshotSource, snapshotIdentity, coverage, {}, {});
  });
  retireBindingSource(snapshotSource);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(snapshotView.storage.eventCount, manifestView.storage.eventCount, name);
  for (uint32_t i = 0; i < snapshotView.storage.eventCount; ++i) {
    const uint16_t a = snapshotView.storage.order[i];
    const uint16_t b = manifestView.storage.order[i];
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(snapshotView.storage.events[a].tick, manifestView.storage.events[b].tick, name);
    TEST_ASSERT_EQUAL_MESSAGE(snapshotView.storage.events[a].type, manifestView.storage.events[b].type, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(snapshotView.storage.events[a].data.noteData.note,
                                    manifestView.storage.events[b].data.noteData.note, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(snapshotView.storage.phases[a], manifestView.storage.phases[b], name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(snapshotView.canonicalRanks[a], manifestView.canonicalRanks[b], name);
  }
}

uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }
// `Track::kPlaybackViewFillQuantum` / `kPlaybackViewOrderQuantum` (Track is incomplete in this suite).
constexpr size_t kFillQuantum = 64;
constexpr size_t kOrderQuantum = 48;

struct Case {
  const char* name;
  Rows record;
  Rows overdub;
  PlaybackCoverageInterval coverage;
  uint32_t expectedRows;
  uint32_t expectedCanonicalUnits;  // exact, quantum 1
};

void run(Case& c) {
  using namespace RevisionStore;
  Loop loop;
  buildLoop(loop, c.record, c.overdub);
  static Store<c5::Cap> store;
  store.reset();
  TEST_ASSERT_EQUAL(ImportResult::Ok, importLoop(loop, store));

  static PassEvent staging[256];
  ManifestWindowCursor<c5::Cap> unitSource(staging, 256);
  RuntimePlaybackView unitView;
  RuntimePlaybackViewBuild unitBuild;
  const StageCost units = measure(store, &unitSource, c.coverage, 1, 1, unitView, unitBuild);
  ManifestWindowCursor<c5::Cap> grantSource(staging, 256);
  RuntimePlaybackView grantView;
  RuntimePlaybackViewBuild grantBuild;
  const StageCost grants = measure(store, &grantSource, c.coverage, kFillQuantum, kOrderQuantum, grantView, grantBuild);
  const uint32_t E = units.rows;
  char label[200];
  snprintf(label, sizeof(label),
           "%s: E=%u read=%u canonical+ranks units=%u playback units=%u | grants q=%u c+r=%u p=%u", c.name, E,
           units.readUnits, units.canonicalRanks, units.playback, grants.qualification, grants.canonicalRanks,
           grants.playback);
  TEST_MESSAGE(label);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.expectedRows, E, label);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2 * E + 1, units.readUnits, label);
  // Exact units: canonical order as pinned, ranks E, playback order E (ordered by construction).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.expectedCanonicalUnits + E, units.canonicalRanks, label);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(E, units.playback, label);
  // Grants: each stage is ceil(units / quantum); a stage never shares a grant with the next.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(ceilDiv(2 * E + 1, 64), grants.qualification, label);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(ceilDiv(c.expectedCanonicalUnits, 48) + ceilDiv(E, 48), grants.canonicalRanks, label);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(ceilDiv(E, 48), grants.playback, label);
  // The canonical merge cost is the builder's own declared bound (fill + one write per level + odd-level copy-back).
  if (c.expectedCanonicalUnits != E)
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(RuntimePlaybackOrderBuilder::workEntriesUpperBound(E), c.expectedCanonicalUnits,
                                     label);
  expectSnapshotParity(loop, c.coverage, grantView, c.name);
  expectSnapshotParity(loop, c.coverage, unitView, c.name);
}
}  // namespace c5cost

void test_c5_manifest_build_grant_bound_pins_the_canonical_merge_branch() {
  using c5cost::Rows;
  const PlaybackCoverageInterval twoBars{0, 2 * c5::kBar, c5::kLength};
  // A: one pass, 46 notes + a NoteOn whose Off lies past the window: E = 93, canonical input ordered.
  c5cost::Case single{"single pass E=93", {}, {}, twoBars, 93, 93};
  c5cost::addNotes(single.record, 4, 32, 46);
  single.record.emplace_back(1500, true, 90);
  single.record.emplace_back(1600, false, 90);
  // B: the same 93 rows split over two passes that alternate in time: 7 merge levels (odd) -> 9E = 837 units.
  c5cost::Case interleaved{"interleaved passes E=93", {}, {}, twoBars, 93, 837};
  c5cost::addNotes(interleaved.record, 4, 64, 23);
  c5cost::addNotes(interleaved.overdub, 36, 64, 23);
  interleaved.record.emplace_back(1500, true, 90);
  interleaved.record.emplace_back(1600, false, 90);
  // C: copy-back parity, E = 64 interleaved: 6 levels (even), no copy-back -> 7E = 448 units.
  c5cost::Case even{"interleaved passes E=64", {}, {}, twoBars, 64, 448};
  c5cost::addNotes(even.record, 4, 96, 16);
  c5cost::addNotes(even.overdub, 52, 96, 16);
  // D: the only disorder is one equal-tick pair: the overdub's NoteOff at 2000 (its NoteOn before the window) and the
  // record's NoteOn at 2000. Store order puts the later pass's Off first; canonical (pass-major) wants it last.
  const PlaybackCoverageInterval midWindow{c5::kBar, 2 * c5::kBar, c5::kLength};
  c5cost::Case pair{"equal-tick cross-pass pair E=93", {}, {}, midWindow, 93, 837};
  c5cost::addNotes(pair.record, c5::kBar + 4, 26, 45);
  pair.record.emplace_back(2000, true, 91);
  pair.record.emplace_back(2100, false, 91);
  pair.overdub.emplace_back(700, true, 95);
  pair.overdub.emplace_back(2000, false, 95);
  for (c5cost::Case* c : {&single, &interleaved, &even, &pair}) c5cost::run(*c);
}

void test_capture_source_identity_distinguishes_same_size_revisions_and_other_origins() {
  const PlaybackViewSourceIdentity capture{0, 7, nullptr, PlaybackWindowOrigin::Capture, 41};
  const PlaybackViewSourceIdentity newer{0, 7, nullptr, PlaybackWindowOrigin::Capture, 42};
  const PlaybackViewSourceIdentity committed{0, 7, nullptr, PlaybackWindowOrigin::Committed};
  const PlaybackViewSourceIdentity preview{0, 7, nullptr, PlaybackWindowOrigin::Preview};
  TEST_ASSERT_FALSE(capture == newer);
  TEST_ASSERT_FALSE(capture == committed);
  TEST_ASSERT_FALSE(capture == preview);
  TEST_ASSERT_TRUE(capture == capture);
}

static void beginSmallLiveSource(Loop& loop, RuntimePlaybackView& view,
                                 RuntimePlaybackViewBuild& build) {
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(view.prewarm(8));
  const auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  TEST_ASSERT_TRUE(build.begin(view, source,
      {0, 7, nullptr, PlaybackWindowOrigin::Capture, source.captureContentToken},
      {0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR}, {}, {},
      RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.stepFill(8));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.stepOrder(8));
}

struct CapturePhaseStateTestAccess {
  static void exhaustOnNextWrite(CapturePhaseState& phase) { phase.generation_ = UINT64_MAX; }
};

void test_live_descriptor_rejects_raw_or_semantic_mutation_before_begin() {
  for (unsigned action = 0; action < 4; ++action) {
    Loop loop; loop.beginCapture(CapturePhase::Record);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
    if (action == 3)
      TEST_ASSERT_TRUE(loop.capture.store.append(MidiEvent::NoteOn(101, 1, 61, 100)));
    const auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
    if (action == 0) loop.capture.store.shiftAllTicks(5);
    if (action == 1) loop.capture.phase = CapturePhase::Record;
    if (action == 2) ++loop.startLoopTick;
    if (action == 3) {
      // Static API bypass, same row count and unchanged Loop content generation.
      LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(
          loop.capture.store.chunkIds().data(), loop.capture.store.chunkIds().size(),
          [](void*) { return NoteId(77); }, nullptr);
    }
    RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(4));
    RuntimePlaybackViewBuild build;
    const PlaybackViewSourceIdentity identity{0, 7, nullptr, PlaybackWindowOrigin::Capture,
                                              source.captureContentToken};
    const PlaybackCoverageInterval coverage{0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR};
    TEST_ASSERT_FALSE(build.begin(view, source, identity, coverage, {}, {}));
    const auto fresh = RuntimePlaybackViewSource::fromLiveCapture(loop);
    TEST_ASSERT_TRUE(build.begin(view, fresh, identity, coverage, {}, {}));
    (void)build.stepFill(4); (void)build.stepOrder(4);
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
    TEST_ASSERT_EQUAL_UINT32(action == 0 ? 105 : 100, view.storage.events[0].tick);
    build.discard(); loop.discardCapture();
  }
}

void test_capture_phase_identity_copy_assignment_and_exhaustion_are_nonrecycled() {
  Loop loop; RuntimePlaybackView view; RuntimePlaybackViewBuild build;
  beginSmallLiveSource(loop, view, build);
  CapturePhaseState other; other = CapturePhase::Record;
  const auto old = loop.capture.phase.generation();
  loop.capture.phase = other;
  const auto beforeSelf = loop.capture.phase.generation();
  loop.capture.phase.operator=(loop.capture.phase);
  TEST_ASSERT_NOT_EQUAL_UINT64(beforeSelf, loop.capture.phase.generation());
  TEST_ASSERT_NOT_EQUAL_UINT64(old, loop.capture.phase.generation());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.nextGrain());
  build.discard();
  CapturePhaseStateTestAccess::exhaustOnNextWrite(loop.capture.phase);
  loop.capture.phase = CapturePhase::Record;
  TEST_ASSERT_EQUAL_UINT64(0, loop.capture.phase.generation());
  TEST_ASSERT_NULL(RuntimePlaybackViewSource::fromLiveCapture(loop).context);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(300, 1, 7, 64)));
  loop.capture.phase = CapturePhase::None; loop.capture.phase = CapturePhase::Record;
  TEST_ASSERT_EQUAL_UINT64(0, loop.capture.phase.generation());
  TEST_ASSERT_NULL(RuntimePlaybackViewSource::fromLiveCapture(loop).context);
  TEST_ASSERT_EQUAL(3, loop.capture.store.size()); loop.discardCapture();
}

static unsigned liveOrdinalReads;
static const MidiEvent& observeLiveOrdinalRead(const void* context, size_t row) {
  ++liveOrdinalReads; return static_cast<const Loop*>(context)->capture.store.at(row);
}
void test_live_fill_charges_chunk_transition_and_never_calls_ordinal_reader() {
  Loop loop; loop.beginCapture(CapturePhase::Record);
  for (unsigned i = 0; i < 257; ++i)
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(i, 1, 7, 64)));
  auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  source.eventAt = observeLiveOrdinalRead; liveOrdinalReads = 0;
  RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(257));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, source,
      {0, 7, nullptr, PlaybackWindowOrigin::Capture, source.captureContentToken},
      {0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR}, {}, {}));
  unsigned grains = 0, advances = 0;
  while (grains < 260 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill) {
    const size_t before = view.storage.eventCount;
    (void)build.stepFill(1); ++grains;
    const size_t after = view.storage.eventCount;
    TEST_ASSERT_TRUE(after == before || after == before + 1);
    if (after == before) ++advances;
  }
  TEST_ASSERT_EQUAL(258, grains); TEST_ASSERT_EQUAL(1, advances);
  TEST_ASSERT_EQUAL(0, liveOrdinalReads); TEST_ASSERT_EQUAL(257, view.storage.eventCount);
  for (unsigned i = 0; i < 257; ++i) TEST_ASSERT_EQUAL_UINT32(i, view.storage.events[i].tick);
  for (unsigned i = 0; i < 200 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++i)
    (void)build.stepOrder(64);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_TRUE(build.ownsLiveCaptureBorrow());
  build.discard(); TEST_ASSERT_FALSE(build.ownsLiveCaptureBorrow()); loop.discardCapture();
}

void test_live_source_phase_reopen_and_same_phase_assignment_cannot_revive_receipt() {
  for (unsigned action = 0; action < 2; ++action) {
    Loop loop; RuntimePlaybackView view; RuntimePlaybackViewBuild build;
    beginSmallLiveSource(loop, view, build);
    const auto token = loop.captureContentGeneration();
    if (action == 0) loop.capture.phase = CapturePhase::None;
    loop.capture.phase = CapturePhase::Record;
    TEST_ASSERT_EQUAL_UINT64(token, loop.captureContentGeneration());
    TEST_ASSERT_EQUAL(2, loop.capture.store.size());
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.nextGrain());
    TEST_ASSERT_FALSE(view.published);
    build.discard(); loop.discardCapture();
  }
}

void test_live_source_projection_changes_refuse_same_count_completed_candidate() {
  for (unsigned field = 0; field < 3; ++field) {
    Loop loop; RuntimePlaybackView view; RuntimePlaybackViewBuild build;
    beginSmallLiveSource(loop, view, build);
    const auto token = loop.captureContentGeneration();
    if (field == 0) ++loop.startLoopTick;
    if (field == 1) ++loop.loopStartTick;
    if (field == 2) loop.loopLengthTicks = 2 * Config::TICKS_PER_BAR;
    TEST_ASSERT_EQUAL_UINT64(token, loop.captureContentGeneration());
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.nextGrain());
    TEST_ASSERT_EQUAL_UINT32(100, loop.capture.store.at(0).tick);
    build.discard(); loop.discardCapture();
  }
}

void test_live_source_direct_store_rewrite_refuses_without_loop_token_change() {
  Loop loop; RuntimePlaybackView view; RuntimePlaybackViewBuild build;
  beginSmallLiveSource(loop, view, build);
  const auto token = loop.captureContentGeneration();
  loop.capture.store.shiftAllTicks(5);
  TEST_ASSERT_EQUAL_UINT64(token, loop.captureContentGeneration());
  TEST_ASSERT_EQUAL_UINT32(105, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.nextGrain());
  build.discard(); loop.discardCapture();
}

void test_live_capture_borrow_denies_same_count_sort_and_rebuilds_privately() {
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(150, 1, 7, 64)));
  TEST_ASSERT_TRUE(loop.captureEventsSortDirty);
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(8));
  RuntimePlaybackViewBuild build;
  auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  TEST_ASSERT_EQUAL_UINT32(3, source.count);
  TEST_ASSERT_EQUAL_UINT64(loop.captureContentGeneration(), source.generationToken);
  const PlaybackCoverageInterval coverage{0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR};
  const PlaybackViewSourceIdentity identity{0, 7, nullptr, PlaybackWindowOrigin::Capture,
                                            source.generationToken};
  TEST_ASSERT_FALSE(build.begin(view, source,
      {0, 7, nullptr, PlaybackWindowOrigin::Committed}, coverage, {}, {},
      RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_FALSE(build.begin(view, source,
      {0, 7, nullptr, PlaybackWindowOrigin::Preview}, coverage, {}, {},
      RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_FALSE(build.begin(view, source,
      {0, 7, nullptr, PlaybackWindowOrigin::Capture, source.generationToken + 1},
      coverage, {}, {}, RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_FALSE(view.published);
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {},
                               RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, build.stepFill(1));
  const uint64_t oldToken = loop.captureContentGeneration();
  TEST_ASSERT_TRUE(loop.ensureCaptureEventsSorted());
  TEST_ASSERT_EQUAL_UINT32(3, loop.capture.store.size());
  TEST_ASSERT_NOT_EQUAL_UINT64(oldToken, loop.captureContentGeneration());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepFill(1));
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_FALSE(view.published);
  TEST_ASSERT_EQUAL_UINT32(3, loop.capture.store.size());
  build.discard();
  view.resetMetadata();
  source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  const PlaybackViewSourceIdentity fresh{0, 7, nullptr, PlaybackWindowOrigin::Capture,
                                         source.generationToken};
  TEST_ASSERT_TRUE(build.begin(view, source, fresh, coverage, {}, {},
                               RuntimePlaybackViewFillScope::PlaybackCoverage));
  for (unsigned grain = 0; grain < 8 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++grain)
    (void)build.stepFill(2);
  for (unsigned grain = 0; grain < 8 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++grain)
    (void)build.stepOrder(8);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_FALSE(view.published);
  TEST_ASSERT_FALSE(view.rawCoveragePrepared);
  TEST_ASSERT_TRUE(view.source == fresh);
  TEST_ASSERT_EQUAL_UINT32(3, view.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(100, view.storage.events[view.storage.order[0]].tick);
  TEST_ASSERT_TRUE(view.storage.events[view.storage.order[0]].isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(150, view.storage.events[view.storage.order[1]].tick);
  TEST_ASSERT_EQUAL(midi::ControlChange, view.storage.events[view.storage.order[1]].type);
  TEST_ASSERT_EQUAL_UINT32(200, view.storage.events[view.storage.order[2]].tick);
  TEST_ASSERT_TRUE(view.storage.events[view.storage.order[2]].isNoteOff());
  const uint64_t stable = loop.captureContentGeneration();
  TEST_ASSERT_FALSE(loop.ensureCaptureEventsSorted());
  TEST_ASSERT_EQUAL_UINT64(stable, loop.captureContentGeneration());
}

void test_frozen_range_descriptor_cannot_claim_live_capture_origin() {
  LoopEventStore producer;
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::ControlChange(100, 1, 7, 64)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(frozen));
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(4));
  RuntimePlaybackViewBuild build;
  const auto source = RuntimePlaybackViewSource::fromCaptureRange(frozen);
  const PlaybackViewSourceIdentity falseCapture{0, 7, nullptr, PlaybackWindowOrigin::Capture, 41};
  TEST_ASSERT_FALSE(build.begin(view, source, falseCapture,
      {0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR}, {}, {},
      RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_FALSE(view.published);
  TEST_ASSERT_TRUE(frozen.ready());
}

void test_live_capture_borrow_rechecks_after_fill_and_completion() {
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(150, 1, 7, 64)));
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(8));
  RuntimePlaybackViewBuild build;
  const auto coverage = PlaybackCoverageInterval{0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR};
  auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  auto identity = PlaybackViewSourceIdentity{0, 7, nullptr, PlaybackWindowOrigin::Capture,
                                              source.generationToken};
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {},
                               RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.stepFill(8));
  TEST_ASSERT_FALSE(view.complete);
  const uint64_t beforeSort = loop.captureContentGeneration();
  TEST_ASSERT_TRUE(loop.ensureCaptureEventsSorted());
  TEST_ASSERT_EQUAL_UINT32(3, loop.capture.store.size());
  TEST_ASSERT_NOT_EQUAL_UINT64(beforeSort, loop.captureContentGeneration());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepOrder(8));
  TEST_ASSERT_FALSE(view.complete);
  TEST_ASSERT_FALSE(view.published);
  build.discard();
  view.resetMetadata();
  source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  identity.captureContentGeneration = source.generationToken;
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {},
                               RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.stepFill(8));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.stepOrder(8));
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_FALSE(view.published);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(300, 1, 10, 65)));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.nextGrain());
  TEST_ASSERT_FALSE(view.published);
}

void test_frozen_record_capture_range_expires_during_fill_but_not_after_copy() {
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
  const auto coverage = PlaybackCoverageInterval{0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR};
  const PlaybackViewSourceIdentity identity{0, 7, nullptr, PlaybackWindowOrigin::Capture, 42};
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(4));
  RuntimePlaybackViewBuild build;
  auto source = RuntimePlaybackViewSource::fromFrozenRecordCapture(range, 42);
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {},
      RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, build.stepFill(1));
  TEST_ASSERT_TRUE(range.releaseGrain(1));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, build.stepFill(1));
  TEST_ASSERT_FALSE(view.complete || view.published);
  build.discard();
  TEST_ASSERT_NULL(RuntimePlaybackViewSource::fromFrozenRecordCapture(range, 42).context);
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
  view.resetMetadata();
  source = RuntimePlaybackViewSource::fromFrozenRecordCapture(range, 42);
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {},
      RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.stepFill(4));
  TEST_ASSERT_TRUE(range.releaseGrain(1));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.stepOrder(4));
  TEST_ASSERT_TRUE(view.complete && !view.published);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
}
void test_frozen_record_block_index_keeps_out_of_order_and_wrapped_rows() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 24 * bar;
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  for (uint32_t index = 0; index < 768; ++index) {
    const uint32_t tick = index == 600 ? bar + 18
        : index == 767 ? length + 9 : (index / 32) * bar + 40;
    const MidiEvent row = index == 600 || index == 767
        ? MidiEvent::NoteOn(tick, 1, index == 600 ? 61 : 62, 100)
        : MidiEvent::ControlChange(tick, 1, 7, static_cast<uint8_t>(index % 128));
    TEST_ASSERT_TRUE(producer.append(row));
  }
  TEST_ASSERT_TRUE(range.size() == 0); // A reserved descriptor is not a source.
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
  TEST_ASSERT_EQUAL_UINT32(768u, range.size());
  const auto source = RuntimePlaybackViewSource::fromFrozenRecordCapture(range, 42);
  const PlaybackViewSourceIdentity identity{0, 7, nullptr, PlaybackWindowOrigin::Capture, 42};
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(64));
  RuntimePlaybackViewBuild build;
  const auto serve = [&](uint32_t start) {
    view.resetMetadata();
    TEST_ASSERT_TRUE(build.begin(view, source, identity, {start, bar, length}, {}, {},
        RuntimePlaybackViewFillScope::RecordCoverage));
    for (unsigned turn = 0; turn < 6 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++turn)
      (void)build.stepFill(32);
    TEST_ASSERT_EQUAL_MESSAGE(RuntimePlaybackViewBuildGrain::Order, build.nextGrain(),
        "irrelevant 32-row blocks must be charged but not copied one row at a time");
    for (unsigned turn = 0; turn < 4 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++turn)
      (void)build.stepOrder(64);
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  };
  serve(bar);
  TEST_ASSERT_EQUAL_UINT32(33u, view.storage.eventCount);
  unsigned late = 0;
  for (size_t index = 0; index < view.storage.eventCount; ++index)
    if (view.storage.events[index].isNoteOn() && view.storage.events[index].tick == bar + 18) ++late;
  TEST_ASSERT_EQUAL_UINT32(1u, late);
  build.discard();
  serve(0);
  TEST_ASSERT_EQUAL_UINT32(33u, view.storage.eventCount);
  unsigned wrapped = 0;
  for (size_t index = 0; index < view.storage.eventCount; ++index)
    if (view.storage.events[index].isNoteOn() && view.storage.events[index].tick == 9) ++wrapped;
  TEST_ASSERT_EQUAL_UINT32(1u, wrapped);
  TEST_ASSERT_TRUE(range.ready());
}


void test_frozen_record_index_invalidates_before_tick_rewrite() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(bar + 12, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(bar + 32, 1, 60, 0)));
  producer.shiftAllTicks(bar); // Prior group membership names bar 1, not bar 2.
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(4));
  RuntimePlaybackViewBuild build;
  const PlaybackViewSourceIdentity identity{0, 7, nullptr, PlaybackWindowOrigin::Capture, 42};
  TEST_ASSERT_TRUE(build.begin(view, RuntimePlaybackViewSource::fromFrozenRecordCapture(range, 42),
      identity, {2 * bar, bar, 4 * bar}, {}, {}, RuntimePlaybackViewFillScope::RecordCoverage));
  for (unsigned turn = 0; turn < 8 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++turn)
    (void)build.stepFill(4);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, build.nextGrain());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.stepOrder(4));
  TEST_ASSERT_EQUAL_UINT32(2u, view.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(2 * bar + 12, view.storage.events[0].tick);
  TEST_ASSERT_EQUAL_UINT32(2 * bar + 32, view.storage.events[1].tick);
}

// A frozen Capture ready may coexist with the legacy Record return path, but
// that path cannot turn its content receipt into committed-root provenance.
void test_legacy_record_adopter_cannot_reidentify_frozen_capture_ready() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore producer;
  LoopEventStore::CaptureRange range;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(800, 1, 62, 100)));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOff(900, 1, 62, 0)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(Config::TICKS_PER_BAR));
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_TRUE(set->lifecycle.build < set->views.size());
  const PlaybackViewSourceIdentity capture{1, 2, nullptr, PlaybackWindowOrigin::Capture, 42};
  const PlaybackViewSourceIdentity committed{1, 2, nullptr};
  const PlaybackCoverageInterval coverage{768, 768, 1536};
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(set->views[set->lifecycle.build],
      RuntimePlaybackViewSource::fromFrozenRecordCapture(range, 42), capture,
      coverage, {}, {}, RuntimePlaybackViewFillScope::PlaybackCoverage));
  for (unsigned grain = 0; grain < 16 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++grain)
    (void)build.stepFill(4);
  for (unsigned grain = 0; grain < 16 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++grain)
    (void)build.stepOrder(4);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_TRUE(set->publishCompletedBuildAsReady(PlaybackViewHandoffMode::Preparation));
  const uint8_t readyRole = set->lifecycle.ready;
  const uint8_t currentRole = set->lifecycle.current;
  const uint64_t storage = set->views[readyRole].storageGeneration;
  TEST_ASSERT_TRUE(set->views[readyRole].source == capture);
  TEST_ASSERT_NULL(runtime.adoptFrozenRecordWindow(committed, 1536, 800));
  TEST_ASSERT_EQUAL_UINT8(currentRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT8(readyRole, set->lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT64(storage, set->views[readyRole].storageGeneration);
  TEST_ASSERT_TRUE(set->views[readyRole].source == capture);
  TEST_ASSERT_FALSE(set->views[readyRole].published);
  TEST_ASSERT_TRUE(range.ready());
  const PlaybackViewSourceIdentity wrongToken{1, 2, nullptr, PlaybackWindowOrigin::Capture, 43};
  TEST_ASSERT_NULL(runtime.adoptFrozenRecordWindow(wrongToken, 1536, 800));
  TEST_ASSERT_EQUAL_UINT8(readyRole, set->lifecycle.ready);
  TEST_ASSERT_TRUE(set->views[readyRole].source == capture);
  const RuntimePlaybackView* adopted = runtime.adoptFrozenRecordWindow(capture, 1536, 800);
  TEST_ASSERT_NOT_NULL(adopted);
  TEST_ASSERT_EQUAL_UINT8(readyRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(storage, adopted->storageGeneration);
  TEST_ASSERT_TRUE(adopted->source == capture);
  TEST_ASSERT_NULL(runtime.adoptFrozenRecordWindow(committed, 1536, 800));
  TEST_ASSERT_EQUAL_UINT8(readyRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(storage, set->views[readyRole].storageGeneration);
  TEST_ASSERT_TRUE(set->views[readyRole].source == capture);

  LoopSnapshot oldRoot;
  LoopSnapshot newRoot;
  const PlaybackViewSourceIdentity oldCommitted{1, 2, &oldRoot};
  const PlaybackViewSourceIdentity newCommitted{1, 2, &newRoot};
  LoopPlaybackRuntime legacy;
  TEST_ASSERT_TRUE(legacy.prewarmViews());
  TEST_ASSERT_TRUE(legacy.prepareFirstRecordingBar(Config::TICKS_PER_BAR));
  for (unsigned grain = 0; grain < 16 && !legacy.frozenRecordWindowReady(); ++grain)
    (void)legacy.serviceFrozenRecordWindow(range, range.generation(), oldCommitted, 1536, 800, 768, 4);
  TEST_ASSERT_TRUE(legacy.frozenRecordWindowReady());
  auto* legacySet = legacy.viewSetIfPrewarmed();
  const uint8_t legacyReadyRole = legacySet->lifecycle.ready;
  const uint64_t legacyStorage = legacySet->views[legacyReadyRole].storageGeneration;
  const RuntimePlaybackView* rebound = legacy.adoptFrozenRecordWindow(newCommitted, 1536, 800);
  TEST_ASSERT_NOT_NULL(rebound);
  TEST_ASSERT_EQUAL_UINT8(legacyReadyRole, legacySet->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(legacyStorage, rebound->storageGeneration);
  TEST_ASSERT_TRUE(rebound->source == newCommitted);
}

#include "pending_box_release.inc"
#define INVENTORY_ADOPTION_WORKER_HARNESS 1
#include "../test_note_edit_entry_boundary/inventory_adoption.inc"

#include "live_source_call.inc"
#include "destination_reuse_call.inc"
#include "acquisition_call.inc"
// Task 3.15e slice 1 — **one window carries the committed base and the live overdub overlay.**
//
// The migration's first slice is the *build contract*, not the audible route: a window that is filled and ordered from the
// committed base is continued, in the same build identity, from one append-stable live overlay source. Consequences this
// fixture pins:
//  - one coverage interval, one ordered index, one source identity (the overlay identity adds the live capture generation
//    to the base's revision/generation, so slice 2 can tell which capture the overlay belongs to);
//  - the base rows keep their committed pass provenance and the overlay rows carry `kLiveOverlayPassOrigin`, so one window
//    can be served while only committed rows acquire occupy/closure ledger ownership (`221334`);
//  - the equal-tick tie-break is the existing canonical class rule (NoteOff < NoteOn), not "base first, then overlay";
//  - appending another overlay note extends and re-orders the *same* window without duplicating any row from either half.
// Production emission is untouched: nothing selects this window yet, and the `ActiveCaptureOverdub` echo stays authoritative.
struct LiveOverlayWindowFixture {
  CanonicalPrefixFixture base;
  RuntimeCompanionApply::Work work;
  RuntimeCompanionPasses passes;
  Loop loop;
  RuntimePlaybackView view;
  RuntimePlaybackViewBuild build;
  /// The committed identity must outlive the passes/build: `RuntimeCompanionPasses::valid()` compares against the object
  /// the caller handed it, so a temporary here would dangle.
  PlaybackViewSourceIdentity committedIdentity;

  explicit LiveOverlayWindowFixture(size_t capacity) : work{
      base.runtime.frozenCompanionPairLookup, base.runtime.frozenCompanionDeleteLookup,
      base.runtime.frozenCompanionSpans, base.runtime.frozenCompanionReconstruction,
      base.runtime.frozenCompanionOverlaps} {
    // The committed base is On@10 note 60 plus its Off, moved to tick 40 by a committed length edit.
    base.passes.editPasses.push_back(lengthRow(1, 40));
    base.finish();
    committedIdentity = {base.revision, base.generation, &base.passes};
    TEST_ASSERT_TRUE(passes.prewarm(64));
    TEST_ASSERT_TRUE(passes.begin(base.runtime.frozenCompanionApply, work, base.runtime.canonicalPassInputs,
                                 nullptr, 0, committedIdentity, base.length));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t turn = 0; turn < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
      result = passes.step(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(passes.size()));
    TEST_ASSERT_TRUE(view.prewarm(capacity));
    TEST_ASSERT_TRUE(build.begin(view, passes, committedIdentity, coverage(), {}, {}));
  }
  ~LiveOverlayWindowFixture() {
    build.discard();
    view.release();
    passes.discard();
    base.runtime.reset(false);
    LoopEventStore::releaseChunkRefs(base.passes.recordPass.committedChunkIds);
  }
  PlaybackCoverageInterval coverage() const { return {0, base.length, base.length}; }
  bool drive(size_t turns) {
    for (size_t turn = 0; turn < turns; ++turn) {
      const RuntimePlaybackViewBuildGrain grain = build.nextGrain();
      if (grain == RuntimePlaybackViewBuildGrain::Complete) return true;
      if (grain == RuntimePlaybackViewBuildGrain::Denied) return false;
      const RuntimePlaybackViewBuildGrain stepped = grain == RuntimePlaybackViewBuildGrain::Fill
          ? build.stepFill(1) : build.stepOrder(1);
      if (stepped == RuntimePlaybackViewBuildGrain::Denied) return false;
    }
    return false;
  }
  void startOverdub() {
    loop.loopLengthTicks = base.length;
    loop.beginCapture(CapturePhase::Overdub);
  }
  PlaybackViewSourceIdentity overlayIdentity() const {
    return {base.revision, base.generation, nullptr, PlaybackWindowOrigin::Capture,
            loop.captureContentGeneration()};
  }
  bool attachOverlay() {
    const auto live = RuntimePlaybackViewSource::fromLiveCapture(loop, true);
    if (live.context == nullptr) return false;
    return build.beginLiveOverlayContinuation(live, overlayIdentity());
  }
  const MidiEvent& ordered(size_t index) const { return view.storage.events[view.storage.order[index]]; }
};

void test_3_15e_slice1_one_window_carries_the_committed_base_and_the_live_overlay() {
  LiveOverlayWindowFixture f(32);
  TEST_ASSERT_TRUE_MESSAGE(f.drive(2000), "the committed base must fill and order as one window");
  TEST_ASSERT_TRUE(f.view.complete);
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(f.view.storage.eventCount));

  f.startOverdub();
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOn(20, 1, 62, 100)));
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(30, 1, 62, 0)));
  // A wrap-held Off whose On belongs to the previous pass: legal live content, and it tests the equal-tick rule at tick 10.
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(10, 1, 64, 0)));
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(f.loop.capture.store.size()));

  TEST_ASSERT_TRUE_MESSAGE(f.attachOverlay(), "the continuation attaches to the completed base");
  TEST_ASSERT_TRUE(f.build.overlayContinuationActive());
  TEST_ASSERT_EQUAL_UINT64(f.loop.captureContentGeneration(), f.build.overlayCaptureGeneration());
  TEST_ASSERT_TRUE_MESSAGE(f.drive(2000), "the appended overlay half must fill and re-order");

  // (1) **One ordered sequence**, canonical: the overlay's Off@10 precedes the base's On@10 (class rule), then by tick.
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(f.view.storage.eventCount));
  {
    struct Want { uint32_t tick; uint8_t note; bool on; const char* why; };
    const Want want[] = {
        {10, 64, false, "the overlay's wrap-held Off sorts before the base's On at the same tick (class rule)"},
        {10, 60, true, "the base's On at the same tick"},
        {20, 62, true, "the overlay's On"},
        {30, 62, false, "the overlay's Off"},
        {40, 60, false, "the base's committed Off"},
    };
    for (size_t i = 0; i < 5; ++i) {
      const MidiEvent& event = f.ordered(i);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(want[i].tick, event.tick, want[i].why);
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i].note, event.data.noteData.note, want[i].why);
      TEST_ASSERT_EQUAL_MESSAGE(want[i].on, event.isNoteOn(), want[i].why);
    }
  }

  // (2) **Provenance per row**: the base rows keep their committed pass origin, the overlay rows carry the sentinel (with
  // their own ordinal), and the window claims no canonical committed rank for the mixed content.
  TEST_ASSERT_TRUE(f.view.passOriginsReady);
  TEST_ASSERT_FALSE(f.view.canonicalRanksReady);
  TEST_ASSERT_EQUAL_UINT16(0, f.view.passOrigins[0].pass);   // the baseline's own pass index, never the overlay sentinel
  TEST_ASSERT_EQUAL_UINT16(0, f.view.passOrigins[1].pass);
  for (size_t i = 2; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(kLiveOverlayPassOrigin, f.view.passOrigins[i].pass,
                                     "an overlay row must never look like a committed pass");
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(i), f.view.passOrigins[i].event);
    TEST_ASSERT_EQUAL_UINT16(kLiveOverlayCanonicalRank, f.view.canonicalRanks[i]);
  }

  // (3) **One identity**: the window's identity carries the base's revision/generation *and* the live capture generation.
  TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, f.view.source.origin);
  TEST_ASSERT_EQUAL_UINT32(f.base.revision, f.view.source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(f.base.generation, f.view.source.sourceGeneration);
  TEST_ASSERT_EQUAL_UINT64(f.loop.captureContentGeneration(), f.view.source.captureContentGeneration);
  TEST_ASSERT_NULL(f.view.source.snapshotIdentity);

  // (4) **Append continuation is monotonic**: one more overdub note extends the same window, every earlier row stays exactly
  // once (from both halves) and the new pair lands in canonical position.
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOn(50, 1, 66, 100)));
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(60, 1, 66, 0)));
  TEST_ASSERT_TRUE_MESSAGE(f.build.extendLiveCapture(f.overlayIdentity()), "an append extends the same build");
  TEST_ASSERT_TRUE_MESSAGE(f.drive(2000), "the extension must re-order the same window");
  TEST_ASSERT_EQUAL_UINT32(7, static_cast<uint32_t>(f.view.storage.eventCount));
  {
    const uint32_t wantTick[] = {10, 10, 20, 30, 40, 50, 60};
    const uint8_t wantNote[] = {64, 60, 62, 62, 60, 66, 66};
    const bool wantOn[] = {false, true, true, false, false, true, false};
    for (size_t i = 0; i < 7; ++i) {
      const MidiEvent& event = f.ordered(i);
      TEST_ASSERT_EQUAL_UINT32(wantTick[i], event.tick);
      TEST_ASSERT_EQUAL_UINT8(wantNote[i], event.data.noteData.note);
      TEST_ASSERT_EQUAL(wantOn[i], event.isNoteOn());
      for (size_t j = 0; j < i; ++j) {
        const MidiEvent& other = f.ordered(j);
        TEST_ASSERT_FALSE_MESSAGE(other.tick == event.tick && other.data.noteData.note == event.data.noteData.note &&
                                      other.isNoteOn() == event.isNoteOn(),
                                  "no row is duplicated by the extension");
      }
    }
  }
  // The committed rows still carry their own provenance and the appended rows the sentinel.
  TEST_ASSERT_EQUAL_UINT16(0, f.view.passOrigins[0].pass);
  TEST_ASSERT_EQUAL_UINT16(0, f.view.passOrigins[1].pass);
  TEST_ASSERT_EQUAL_UINT16(kLiveOverlayPassOrigin, f.view.passOrigins[5].pass);
  TEST_ASSERT_EQUAL_UINT16(kLiveOverlayPassOrigin, f.view.passOrigins[6].pass);
  TEST_ASSERT_EQUAL_UINT64(f.loop.captureContentGeneration(), f.build.overlayCaptureGeneration());
}

// A committed base copied from a raw array carries no per-row provenance, so the window could not distinguish a committed
// row from an overlay row. The continuation must refuse it by name rather than attach and let a default origin stand in for
// "committed pass 0" — which is the difference between one provenance table and a guess.
void test_3_15e_slice2b_a_base_without_provenance_cannot_take_an_overlay_half() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  MidiEvent baseRows[2] = {MidiEvent::NoteOn(10, 1, 60, 100), MidiEvent::NoteOff(90, 1, 60, 0)};
  const PlaybackCoverageInterval coverage{0, 768, 768};
  const PlaybackViewSourceIdentity baseIdentity{1, 2, nullptr};
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(32));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, baseRows, 2, baseIdentity, coverage, {}, {}));
  for (size_t turn = 0; turn < 2000; ++turn) {
    const RuntimePlaybackViewBuildGrain grain = build.nextGrain();
    if (grain == RuntimePlaybackViewBuildGrain::Complete) break;
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, grain);
    (void)(grain == RuntimePlaybackViewBuildGrain::Fill ? build.stepFill(1) : build.stepOrder(1));
  }
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_FALSE_MESSAGE(view.passOriginsReady, "fixture precondition: a raw copied base has no provenance table");

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(20, 1, 62, 100)));
  const auto live = RuntimePlaybackViewSource::fromLiveCapture(loop, true);
  const PlaybackViewSourceIdentity overlay{1, 2, nullptr, PlaybackWindowOrigin::Capture,
                                          loop.captureContentGeneration()};
  TEST_ASSERT_FALSE_MESSAGE(build.beginLiveOverlayContinuation(live, overlay),
                            "a base without provenance must not take an overlay half");
  TEST_ASSERT_EQUAL(RuntimePlaybackViewFillRefusal::ContinuationBaseWithoutProvenance,
                    build.continuationRefusal());
  TEST_ASSERT_FALSE(build.overlayContinuationActive());
  // The base window itself is still a valid committed window: the refusal is about the continuation, not the content.
  TEST_ASSERT_TRUE(view.complete);
  build.discard();
}

// Task 3.15e slice 2b-ii-b — **the borrow's collision rule is wired into the stopped oracle, not just named.**
//
// A retained overlay candidate owns the borrowed companion storage by identity. This fixture plants a real combined
// candidate (committed base from `RuntimeCompanionPasses` + a live overlay continuation) into the slot's build role and
// then runs the **production** oracle entry:
//  * no candidate           → the oracle borrows exactly as before (`Allowed`, publishes);
//  * retained and current   → `RefusedRetainedCandidate`; the candidate is untouched and nothing is published under it;
//  * retained and stale     → `RetireCandidateFirst`; the candidate is retired **inside** the call and the oracle then
//                             continues on its existing path and publishes — the bytes are never reinterpreted first.
void test_3_15e_slice2b_the_stopped_oracle_consults_the_overlay_candidate_and_retires_a_stale_one() {
  CanonicalPrefixFixture f;
  auto& runtime = f.runtime;
  const PlaybackCoverageInterval coverage{0, f.length, f.length};
  auto on = MidiEvent::NoteOn(10, 1, 60, 90);
  on.noteId = 1; // the fixture's record row carries it, and the oracle compares the full input identity
  MidiEvent oracle[] = {on, MidiEvent::NoteOff(90, 1, 60)};

  // (1) **No candidate**: the oracle's existing path is untouched, including the decision it reports.
  {
    OverdubOverlayLane::OracleRebuildDecision decision = OverdubOverlayLane::OracleRebuildDecision::RefusedRetainedCandidate;
    TEST_ASSERT_TRUE(runtime.publishStoppedCanonicalOracleView(f.passes, f.source(), f.live(), oracle, 2, coverage, {},
                                                                &decision));
    TEST_ASSERT_EQUAL(OverdubOverlayLane::OracleRebuildDecision::Allowed, decision);
    TEST_ASSERT_NULL(runtime.overdubOverlayCandidate());
  }

  // Plant the combined candidate the overlay owner turn would have produced: provenance-bearing base from the borrowed
  // companion instance, then the append-stable live half. The build role is where the lane keeps it.
  f.finish();
  Loop liveLoop;
  liveLoop.loopLengthTicks = f.length;
  liveLoop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(liveLoop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 62, 100)));
  TEST_ASSERT_TRUE(liveLoop.appendCaptureEvent(MidiEvent::NoteOff(260, 1, 62, 0)));
  auto& set = *runtime.viewSetIfPrewarmed();
  auto& candidate = set.views[set.lifecycle.build];
  RuntimeCompanionApply::Work work{runtime.frozenCompanionPairLookup, runtime.frozenCompanionDeleteLookup,
      runtime.frozenCompanionSpans, runtime.frozenCompanionReconstruction, runtime.frozenCompanionOverlaps};
  const PlaybackViewSourceIdentity base = f.source();
  TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.begin(runtime.frozenCompanionApply, work,
                                                       runtime.canonicalPassInputs, nullptr, 0, base, f.length));
  auto passesResult = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t turn = 0; turn < 4000 && passesResult == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    passesResult = runtime.frozenCompanionPasses.step(64);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, passesResult);
  TEST_ASSERT_TRUE(runtime.viewBuild.begin(candidate, runtime.frozenCompanionPasses, base, coverage, {}, {},
                                           RuntimePlaybackViewFillScope::PlaybackCoverage));
  const auto drive = [&]() {
    for (size_t turn = 0; turn < 8000; ++turn) {
      const auto grain = runtime.viewBuild.nextGrain();
      if (grain == RuntimePlaybackViewBuildGrain::Denied) return false;
      if (grain == RuntimePlaybackViewBuildGrain::Complete) return true;
      const auto stepped = grain == RuntimePlaybackViewBuildGrain::Fill
          ? runtime.viewBuild.stepFill(64) : runtime.viewBuild.stepOrder(48);
      if (stepped == RuntimePlaybackViewBuildGrain::Denied) return false;
    }
    return false;
  };
  TEST_ASSERT_TRUE_MESSAGE(drive(), "the borrowed base must fill and order");
  const auto live = RuntimePlaybackViewSource::fromLiveCapture(liveLoop, true);
  const PlaybackViewSourceIdentity overlay{base.playbackRevision, base.sourceGeneration, nullptr,
                                           PlaybackWindowOrigin::Capture, liveLoop.captureContentGeneration()};
  TEST_ASSERT_TRUE(runtime.viewBuild.beginLiveOverlayContinuation(live, overlay));
  runtime.frozenCompanionPasses.discard(); // the continuation nulls the baseline: the borrow is released here
  TEST_ASSERT_TRUE_MESSAGE(drive(), "the live overlay half must re-order the same window");
  TEST_ASSERT_TRUE(runtime.frozenCompanionPasses.idle()); // released as soon as the base was copied (slice contract)
  const RuntimePlaybackView* planted = runtime.overdubOverlayCandidate();
  TEST_ASSERT_NOT_NULL(planted);
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(planted->storage.eventCount));
  TEST_ASSERT_TRUE(planted->passOriginsReady);
  TEST_ASSERT_FALSE(planted->canonicalRanksReady);
  TEST_ASSERT_TRUE(planted->complete);

  // (2) **Retained and current**: the oracle is refused by name, and it does not touch the candidate.
  {
    const auto retention = OverdubOverlayLane::candidateRetention(planted, true, base,
                                                                  liveLoop.captureContentGeneration(), coverage);
    TEST_ASSERT_TRUE(retention.overlayCandidateRetained);
    TEST_ASSERT_TRUE_MESSAGE(retention.candidateBaseCurrent, "the committed base identity still answers the demand");
    TEST_ASSERT_TRUE_MESSAGE(retention.candidateIdentityCurrent, "a matching generation makes the candidate current");
    OverdubOverlayLane::OracleRebuildDecision decision = OverdubOverlayLane::OracleRebuildDecision::Allowed;
    TEST_ASSERT_FALSE(runtime.publishStoppedCanonicalOracleView(f.passes, f.source(), f.live(), oracle, 2, coverage,
                                                                retention, &decision));
    TEST_ASSERT_EQUAL_MESSAGE(OverdubOverlayLane::OracleRebuildDecision::RefusedRetainedCandidate, decision,
                              "a live candidate refuses the oracle for this turn");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(planted, runtime.overdubOverlayCandidate(),
                                  "a refused oracle must not overwrite the retained candidate");
    TEST_ASSERT_TRUE(planted->complete);
    TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(planted->storage.eventCount));
  }

  // The retention rule itself: the committed revision, the coverage or the demand itself moving makes the candidate
  // **stale** (retire it). The live capture **generation** is different: it advances on every accepted input row, so it
  // is the append frontier the continuation extends, not a retirement reason. The strict currency a claim compares still
  // reports not-current until the lane's next turn extends the window.
  {
    PlaybackViewSourceIdentity revisionDrift = base;
    ++revisionDrift.playbackRevision;
    const auto revisionMoved = OverdubOverlayLane::candidateRetention(
        planted, true, revisionDrift, liveLoop.captureContentGeneration(), coverage);
    TEST_ASSERT_FALSE(revisionMoved.candidateBaseCurrent);
    TEST_ASSERT_TRUE(OverdubOverlayLane::mustRetire(revisionMoved));
    PlaybackViewSourceIdentity snapshotDrift = base;
    snapshotDrift.snapshotIdentity = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x1234));
    TEST_ASSERT_FALSE_MESSAGE(OverdubOverlayLane::candidateRetention(planted, true, snapshotDrift,
                                                                     liveLoop.captureContentGeneration(),
                                                                     coverage).candidateBaseCurrent,
                              "a republication at the same revision is a different committed base");
    const auto generationDrift = OverdubOverlayLane::candidateRetention(
        planted, true, base, liveLoop.captureContentGeneration() + 1, coverage);
    TEST_ASSERT_TRUE_MESSAGE(generationDrift.candidateBaseCurrent,
                             "an appended row does not retire the candidate; the lane extends it");
    TEST_ASSERT_FALSE_MESSAGE(generationDrift.candidateIdentityCurrent,
                              "the strict claim currency is one append behind until the extension lands");
    TEST_ASSERT_FALSE(OverdubOverlayLane::mustRetire(generationDrift));
    const PlaybackCoverageInterval other{0, f.length - 1, f.length};
    TEST_ASSERT_FALSE_MESSAGE(OverdubOverlayLane::candidateRetention(planted, true, base,
                                                                     liveLoop.captureContentGeneration(),
                                                                     other).candidateBaseCurrent,
                              "committed-base coverage mismatch invalidates the retained candidate");
    TEST_ASSERT_FALSE_MESSAGE(OverdubOverlayLane::candidateRetention(planted, false, base,
                                                                     liveLoop.captureContentGeneration(),
                                                                     coverage).candidateBaseCurrent,
                              "a demand that ended leaves nothing for the candidate to answer");
    TEST_ASSERT_FALSE_MESSAGE(OverdubOverlayLane::candidateRetention(nullptr, true, base,
                                                                     liveLoop.captureContentGeneration(),
                                                                     coverage).overlayCandidateRetained,
                              "an empty build role is not a candidate");
  }

  // (3) **Retained and stale**: the oracle retires it inside the call, then continues on its existing path and
  // publishes. The stale bytes are never reinterpreted as oracle output before the retirement.
  {
    // The lead-in oracle call left its workers discarded; the fixture re-establishes the canonical prefix so the
    // oracle's own admission terms are satisfied (this is the state a stopped report finds).
    runtime.canonicalPassInputs.discard();
    runtime.canonicalPrefixPhase = LoopPlaybackRuntime::CanonicalPrefixPhase::Idle;
    // The demand ends at STOPPED, so the retained candidate's base no longer answers anything.
    const auto stale = OverdubOverlayLane::candidateRetention(planted, false, base,
                                                              liveLoop.captureContentGeneration(), coverage);
    TEST_ASSERT_TRUE(OverdubOverlayLane::mustRetire(stale));
    OverdubOverlayLane::OracleRebuildDecision decision = OverdubOverlayLane::OracleRebuildDecision::Allowed;
    TEST_ASSERT_TRUE_MESSAGE(runtime.publishStoppedCanonicalOracleView(f.passes, f.source(), f.live(), oracle, 2,
                                                                       coverage, stale, &decision),
                             "after retiring a stale candidate the oracle runs its existing path");
    TEST_ASSERT_EQUAL(OverdubOverlayLane::OracleRebuildDecision::RetireCandidateFirst, decision);
    TEST_ASSERT_NULL_MESSAGE(runtime.overdubOverlayCandidate(), "the stale candidate is retired, not left behind");
    TEST_ASSERT_FALSE(candidate.complete);
    TEST_ASSERT_FALSE(candidate.passOriginsReady);
    // And immediately afterwards the oracle sees no candidate at all: the decision returns to `Allowed`.
    OverdubOverlayLane::OracleRebuildDecision after = OverdubOverlayLane::OracleRebuildDecision::RefusedRetainedCandidate;
    (void)runtime.publishStoppedCanonicalOracleView(f.passes, f.source(), f.live(), oracle, 2, coverage, {}, &after);
    TEST_ASSERT_EQUAL(OverdubOverlayLane::OracleRebuildDecision::Allowed, after);
  }
  runtime.viewBuild.discard();
  LoopEventStore::releaseChunkRefs(liveLoop.passes.recordPass.committedChunkIds);
}

// Task 3.15e slice 3 — **the gating probe: a row's provenance is available to the emission callback at the moment it
// emits, with no second traversal.**
//
// The slice-3 packet's C5 (apply the occupy ledger only to committed-origin rows) needs the emission path to know a
// row's origin. `advancePlaybackCursor` sends the event only, and the stream's `consume` hook runs **after** send, so a
// "remember the last consumed row" trick would be one row late. This probe validates the alternative shape without touching
// production: the stream's `eventAt(ctx, cursor)` — called **before** the send for that row — records
// `view.passOrigins[view.storage.order[cursor]].pass` into a small context; the send callback reads it and gates its
// ledger action. It also counts the calls, so "one traversal" is measured rather than assumed. The production delta the
// packet proposes is exactly this store (a provenance pointer + an origin slot on `FixedPlaybackStreamCtx`), no new
// function and no extra pass.
void test_3_15e_slice3_probe_row_origin_is_visible_at_emission_time() {
  LiveOverlayWindowFixture f(32);
  TEST_ASSERT_TRUE(f.drive(2000));
  f.startOverdub();
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOn(20, 1, 62, 100)));
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(30, 1, 62, 0)));
  TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(10, 1, 64, 0)));
  TEST_ASSERT_TRUE(f.attachOverlay());
  TEST_ASSERT_TRUE(f.drive(2000));
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(f.view.storage.eventCount));

  struct ProbeCtx {
    const FixedPlaybackEventStorage* storage = nullptr;
    const RuntimePlaybackView* view = nullptr;
    uint16_t lastOrigin = UINT16_MAX;
    unsigned eventAtCalls = 0;
    unsigned sent = 0;
    unsigned committedSent = 0;
    unsigned overlaySent = 0;
    unsigned ledgeredCommitted = 0;
    unsigned ledgeredOverlay = 0;
  } probe;
  probe.storage = &f.view.storage;
  probe.view = &f.view;

  // Mirrors `fixedPlaybackStreamEventAt` plus the one store the packet proposes.
  const auto eventAt = [](const void* ctx, uint16_t cursor) -> const MidiEvent& {
    auto& p = *const_cast<ProbeCtx*>(static_cast<const ProbeCtx*>(ctx));
    ++p.eventAtCalls;
    const uint16_t row = p.storage->order[cursor];
    p.lastOrigin = p.view->passOrigins[row].pass;
    return p.storage->events[row];
  };
  const auto send = [](void* ctx, const MidiEvent&, uint8_t) {
    auto& p = *static_cast<ProbeCtx*>(ctx);
    // The origin recorded by `eventAt` must be **this** row's, proving the store happens before the emission callback.
    const uint16_t row = p.storage->order[p.sent];
    TEST_ASSERT_EQUAL_UINT16(p.view->passOrigins[row].pass, p.lastOrigin);
    if (p.lastOrigin == kLiveOverlayPassOrigin) {
      ++p.overlaySent;
      // The ledger is deliberately **not** applied to an overlay row (`221334`/`224719`).
    } else {
      ++p.committedSent;
      ++p.ledgeredCommitted;
    }
    ++p.sent;
  };
  const PlaybackEventStream stream{&probe, [](const void* ctx) {
                                      return static_cast<const ProbeCtx*>(ctx)->storage->eventCount;
                                    },
                                    nullptr, eventAt,
                                    [](const MidiEvent& event, const ProjectionContext&) { return event.tick; },
                                    nullptr};
  ProjectionContext projection{};
  projection.loopLength = f.base.length;
  uint16_t cursor = 0;
  bool dirty = false;
  const PlaybackTickFrame frame{&projection, f.base.length - 1, 0, false};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor({&cursor, &dirty}, frame, PlaybackEmitPolicy::ActiveCommitted, stream,
                                          send, &probe, 0, nullptr, nullptr, 1, nullptr));
  TEST_ASSERT_EQUAL_UINT32(5, cursor);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, probe.eventAtCalls,
                                   "one traversal: every row is visited exactly once");
  TEST_ASSERT_EQUAL_UINT32(5, probe.sent);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, probe.committedSent, "the committed base pair keeps its own origin");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, probe.overlaySent, "the live rows carry the overlay sentinel");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, probe.ledgeredCommitted, "committed rows are the ledger's only input");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, probe.ledgeredOverlay, "an overlay row is sounded once, never ledgered");
}

void test_3_15e_slice1_the_overlay_half_refuses_by_name_and_never_leaves_a_partial_window() {
  // (1) **Combined capacity** — base plus overlay must fit one window. The refusal is named and the window is cleared; no
  // overlay row is truncated and no committed row is evicted.
  {
    LiveOverlayWindowFixture f(4);
    TEST_ASSERT_TRUE(f.drive(2000));
    f.startOverdub();
    TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOn(20, 1, 62, 100)));
    TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(30, 1, 62, 0)));
    TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(10, 1, 64, 0)));
    TEST_ASSERT_TRUE(f.attachOverlay());
    TEST_ASSERT_FALSE_MESSAGE(f.drive(2000), "base 2 + overlay 3 cannot fit a 4-row window");
    TEST_ASSERT_EQUAL(RuntimePlaybackViewFillRefusal::CombinedCapacityExceeded, f.build.continuationRefusal());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, static_cast<uint32_t>(f.view.storage.eventCount),
                                     "a refused window must not be left partially filled");
    TEST_ASSERT_FALSE(f.view.complete);
    TEST_ASSERT_FALSE(f.view.passOriginsReady);
  }

  // (2) **A pass commit invalidates the live-overlay identity** — the continuation is refused as stale, the window does not
  // stay complete, and a fresh build re-derives from the committed source instead of carrying the old live rows.
  {
    LiveOverlayWindowFixture f(32);
    TEST_ASSERT_TRUE(f.drive(2000));
    f.startOverdub();
    TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOn(20, 1, 62, 100)));
    TEST_ASSERT_TRUE(f.loop.appendCaptureEvent(MidiEvent::NoteOff(30, 1, 62, 0)));
    TEST_ASSERT_TRUE(f.attachOverlay());
    TEST_ASSERT_TRUE(f.drive(2000));
    TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(f.view.storage.eventCount));

    // The wrap commit's own effect: the capture is re-based (new pass projection/generation) and the store restarts.
    f.loop.beginCapture(CapturePhase::Overdub);
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, f.build.nextGrain());
    TEST_ASSERT_EQUAL(RuntimePlaybackViewFillRefusal::ContinuationStale, f.build.continuationRefusal());
    TEST_ASSERT_FALSE_MESSAGE(f.view.complete, "a stale overlay half must not leave a complete-looking window");
    TEST_ASSERT_FALSE(f.build.extendLiveCapture(f.overlayIdentity()));

    // The next build derives from the now-committed source: the live rows are gone, the committed base is all there is.
    RuntimePlaybackViewBuild fresh;
    RuntimePlaybackView freshView;
    TEST_ASSERT_TRUE(freshView.prewarm(32));
    TEST_ASSERT_TRUE(fresh.begin(freshView, f.passes, f.committedIdentity, f.coverage(), {}, {}));
    bool complete = false;
    for (size_t turn = 0; turn < 2000 && !complete; ++turn) {
      const RuntimePlaybackViewBuildGrain grain = fresh.nextGrain();
      if (grain == RuntimePlaybackViewBuildGrain::Denied) break;
      if (grain == RuntimePlaybackViewBuildGrain::Complete) { complete = true; break; }
      (void)(grain == RuntimePlaybackViewBuildGrain::Fill ? fresh.stepFill(1) : fresh.stepOrder(1));
    }
    TEST_ASSERT_TRUE_MESSAGE(complete, "the committed source still builds after the overlay is invalidated");
    TEST_ASSERT_FALSE(fresh.overlayContinuationActive());
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(freshView.storage.eventCount));
    for (size_t i = 0; i < freshView.storage.eventCount; ++i)
      TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, freshView.passOrigins[i].pass,
                                       "re-derived content carries committed provenance only");
    fresh.discard();
  }
}

// P-A step 2 (DEC-161 — additive frozen staging roles): the overlap roles are an
// active-role lease, not a per-recorded-slot resident. A slot that recorded once must
// return the surplus roles when its last custody retires, and the next capture
// admission must re-admit them before ingress.
void test_surplus_staging_roles_release_only_after_custody_drains() {
  RuntimePlaybackViewSet set;
  TEST_ASSERT_TRUE(set.prewarm(true, true));
  TEST_ASSERT_TRUE(set.captureStagingsPrewarmed());
  TEST_ASSERT_EQUAL_UINT32(10, static_cast<uint32_t>(set.captureStagings.size()));
  // A frozen role still owed by an interval refuses the return, and the refusal
  // leaves every role allocated rather than half-released.
  TEST_ASSERT_TRUE(set.liveCaptureStaging().freeze(set.liveCaptureStaging().captureGeneration));
  TEST_ASSERT_FALSE(set.releaseSurplusCaptureStagings());
  TEST_ASSERT_TRUE(set.captureStagingsPrewarmed());
  set.liveCaptureStaging().reset();
  // Nothing owed: the four overlap roles return, the two baseline roles stay.
  TEST_ASSERT_TRUE(set.releaseSurplusCaptureStagings());
  TEST_ASSERT_FALSE_MESSAGE(set.releaseSurplusCaptureStagings(),
                            "a released slot must not report repeated releases");
  TEST_ASSERT_TRUE(set.liveCaptureStaging().storage.isPrewarmed());
  TEST_ASSERT_TRUE(set.frozenCaptureStaging().storage.isPrewarmed());
  for (size_t i = 2; i < set.captureStagings.size(); ++i)
    TEST_ASSERT_FALSE(set.captureStagings[i].storage.isPrewarmed());
  TEST_ASSERT_FALSE(set.captureStagingsPrewarmed());
  // The next capture admission re-admits every role, so ingress never runs on a
  // released slot.
  TEST_ASSERT_TRUE(set.prewarmCaptureStagings());
  TEST_ASSERT_TRUE(set.captureStagingsPrewarmed());
  TEST_ASSERT_TRUE(set.captureStagings[5].storage.isPrewarmed());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_record_direct_writers_refuse_executing_builder_before_role_mutation);
  RUN_TEST(test_bridge_cancelled_preparation_does_not_cleanup_selected_storage);
  RUN_TEST(test_surplus_staging_roles_release_only_after_custody_drains);
  RUN_TEST(test_bridge_service_revalidates_ready_role_after_unmasked_adoption);
  RUN_TEST(test_preparation_acquires_before_begin_and_busy_reset_cancels_both_lanes);
  RUN_TEST(test_preparation_cancellation_rejects_completed_destination_and_late_entry);
  RUN_TEST(test_record_direct_writers_and_lease_release_refuse_preparation_before_begin);
  RUN_TEST(test_live_build_cancellation_during_begin_is_not_lost);
  RUN_TEST(test_build_discard_preserves_repurposed_destination_generation);
  RUN_TEST(test_build_query_rejects_repurposed_destination_generation);
  RUN_TEST(test_live_build_cancellation_during_fill_keeps_executing_state_retained);
  RUN_TEST(test_live_build_runtime_reset_during_call_refuses_before_destructive_work);
  RUN_TEST(test_live_build_order_and_complete_query_cancel_without_publishing);
  RUN_TEST(test_live_build_repeated_in_call_discard_is_terminal);
  RUN_TEST(test_live_build_reentry_and_conflicting_preserve_requests_refuse);
  RUN_TEST(test_live_source_masks_restore_and_private_fill_yields_between_rows);
  RUN_TEST(test_live_source_row_transaction_defers_rewrite_then_refuses_stale_work);
  RUN_TEST(test_live_source_descriptor_and_currency_refuse_post_snapshot_mutation);
  RUN_TEST(test_private_delta_order_freezes_append_waves_without_prefix_replay);
  RUN_TEST(test_private_delta_order_wrap_off_on_and_index_write_quantum);
  RUN_TEST(test_private_delta_order_filters_and_refuses_non_private_or_stale_extent);
  RUN_TEST(test_private_delta_order_matrix_matches_independent_order_and_work_bound);
  RUN_TEST(test_adoption_prepared_real_ready_committed_recycle_owner_refuse);
  RUN_TEST(test_adoption_preserved_full_grant_future_worker_begin_commit);
  RUN_TEST(test_pending_box_admission_qualification_and_row_grains);
  RUN_TEST(test_pending_box_real_prewarm_retain_and_empty_release);
  RUN_TEST(test_pending_box_partial_construction_and_progress_batches);
  RUN_TEST(test_pending_box_both_invalid_batches_and_exclusive_root);
  RUN_TEST(test_pending_box_failed_child_and_shared_republish_parent);
  RUN_TEST(test_pending_box_live_capture_frontiers_remain_metadata);
  RUN_TEST(test_pending_box_applied_inventory_literal_rows_and_buffer);
  RUN_TEST(test_qualification_fill_is_private_and_cursor_rewind_denies_without_touching_current);
  RUN_TEST(test_dense_snapshot_pipeline_measures_work_after_ping_pong_ordering);
  RUN_TEST(test_snapshot_runtime_binding_matches_gather_and_preserves_canonical_ties);
  RUN_TEST(test_snapshot_binding_denies_rebound_cursor_overflow_and_published_destination);
  RUN_TEST(test_snapshot_binding_empty_window_completes_and_costs_saturate);
  RUN_TEST(test_c5_manifest_window_gives_the_consumer_the_snapshot_windows_tuple);
  RUN_TEST(test_c5_manifest_window_cursor_refuses_by_name);
  RUN_TEST(test_c5_manifest_build_grant_bound_pins_the_canonical_merge_branch);
  RUN_TEST(test_capture_source_identity_distinguishes_same_size_revisions_and_other_origins);
  RUN_TEST(test_live_descriptor_rejects_raw_or_semantic_mutation_before_begin);
  RUN_TEST(test_capture_phase_identity_copy_assignment_and_exhaustion_are_nonrecycled);
  RUN_TEST(test_live_fill_charges_chunk_transition_and_never_calls_ordinal_reader);
  RUN_TEST(test_live_source_phase_reopen_and_same_phase_assignment_cannot_revive_receipt);
  RUN_TEST(test_live_source_projection_changes_refuse_same_count_completed_candidate);
  RUN_TEST(test_live_source_direct_store_rewrite_refuses_without_loop_token_change);
  RUN_TEST(test_live_capture_borrow_denies_same_count_sort_and_rebuilds_privately);
  RUN_TEST(test_frozen_range_descriptor_cannot_claim_live_capture_origin);
  RUN_TEST(test_live_capture_borrow_rechecks_after_fill_and_completion);
  RUN_TEST(test_shared_raw_preparation_rotates_then_returns_only_after_final_binding);
  RUN_TEST(test_shared_raw_window_binds_final_geometry_without_modulo_or_reordering);
  RUN_TEST(test_shared_raw_window_cannot_become_audible_before_binding);
  RUN_TEST(test_shared_build_discard_detaches_pending_fill_and_order);
  RUN_TEST(test_shared_lifecycle_retains_return_bar_while_successors_rotate);
  RUN_TEST(test_shared_lifecycle_retains_return_when_build_already_complete);
  RUN_TEST(test_shared_runtime_coverage_owner_filters_source);
  RUN_TEST(test_shared_window_fill_filters_before_capacity_and_preserves_loop_end_order);
  RUN_TEST(test_shared_window_fill_scans_wide_source_with_bounded_quantum);
  RUN_TEST(test_shared_window_fill_wraps_coverage_and_denies_matching_overflow);
  RUN_TEST(test_wide_playback_cursor_crosses_pool_extent_without_replay);
  RUN_TEST(test_frozen_canonical_order_receipt_expires_after_audible_discard);
  RUN_TEST(test_frozen_audible_order_receipt_denies_shared_storage_invalidation);
  RUN_TEST(test_prepared_frozen_tail_capacity_and_all_filtered_bounds);
  RUN_TEST(test_runtime_canonical_bridge_base_survives_live_ledger_refresh);
  RUN_TEST(test_runtime_canonical_window_prepares_spare_coverage_and_releases_tail_workers);
  RUN_TEST(test_runtime_canonical_window_source_change_cancels_only_pending_work);
  RUN_TEST(test_runtime_canonical_window_waits_for_shared_builder_without_stealing_it);
  RUN_TEST(test_frozen_tail_copies_prepared_canonical_indexes_before_audible_reuse);
  RUN_TEST(test_frozen_tail_denies_shared_order_reuse_during_index_copy);
  RUN_TEST(test_frozen_tail_uses_private_sort_when_shared_order_is_audible);
  RUN_TEST(test_companion_history_stages_metadata_payloads_and_ids_without_publication);
  RUN_TEST(test_companion_history_reservation_survives_snapshot_cleanup);
  RUN_TEST(test_stop_preparation_bounds_fire_only_where_an_escape_keeps_the_content);
  RUN_TEST(test_capture_companions_reserve_bounds_eligible_rows);
  RUN_TEST(test_companion_row_horizon_scales_with_content_up_to_the_ceiling);
  RUN_TEST(test_companion_history_prep_units_scale_with_demand_not_reserve);
  RUN_TEST(test_companion_history_binds_wrap_publication_before_begin);
  RUN_TEST(test_companion_history_atomic_swap_preserves_snapshot_and_undo_ids);
  RUN_TEST(test_capture_and_companion_history_commit_together_or_leave_both_pending);
  RUN_TEST(test_job_owned_capture_and_companions_commit_without_retiring_live_capture);
  RUN_TEST(test_track_prepared_stop_publishes_matching_canonical_and_audible_sources);
  RUN_TEST(test_stop_canonical_credits_are_separate_checked_and_preserved_on_transport_stop);
  RUN_TEST(test_bounded_stop_undo_collection_appends_final_receipt_with_legacy_semantics);
  RUN_TEST(test_bounded_stop_undo_collection_denies_missing_recycled_and_undersized_receipts);
  RUN_TEST(test_prepared_undo_entry_consumes_collected_ids_without_growth_or_snapshot_release);
  RUN_TEST(test_prepared_undo_entry_denials_leave_private_outputs_untouched);
  RUN_TEST(test_stop_recycle_credits_check_total_and_cannot_run_before_commit);
  RUN_TEST(test_companion_history_recycles_both_buffers_without_growth_until_horizon_exhaustion);
  RUN_TEST(test_companion_history_recycle_denies_source_changes_without_touching_canonical_rows);
  RUN_TEST(test_companion_history_snapshot_changes_deny_publication);
  RUN_TEST(test_companion_history_max_payload_and_empty_companions_fit_grain_bound);
  RUN_TEST(test_companion_history_denies_stale_sources_capacity_and_id_overflow);
  RUN_TEST(test_stopped_canonical_construction_matches_oracle_and_releases_workers);
  RUN_TEST(test_stopped_canonical_construction_preserves_owned_prefix_and_handles_empty);
  RUN_TEST(test_canonical_window_build_retains_provenance_and_bounds_filtered_work);
  RUN_TEST(test_typed_stopped_oracle_owns_empty_and_capacity_provenance);
  RUN_TEST(test_reserved_tail_denies_invalid_identity_and_continues_past_estimate);
  RUN_TEST(test_frozen_canonical_tail_coordinates_children_and_reuses_owned_prefix);
  RUN_TEST(test_frozen_canonical_tail_denies_source_changes_and_preserves_reused_workers);
  RUN_TEST(test_canonical_prefix_global_turn_resumes_two_runtime_owners_without_stacking);
  RUN_TEST(test_canonical_prefix_rejects_timeline_reset_with_reused_audible_identity);
  RUN_TEST(test_canonical_prefix_rejects_history_relocation_without_audible_revision_change);
  RUN_TEST(test_canonical_prefix_live_changes_hide_results_and_restart_on_separate_turn);
  RUN_TEST(test_canonical_prefix_denial_latches_until_source_changes);
  RUN_TEST(test_canonical_prefix_does_not_cancel_reused_normalizer);
  RUN_TEST(test_canonical_prefix_handoff_preserves_frozen_tail_owner);
  RUN_TEST(test_ranked_preparation_bounds_do_not_narrow_into_stop_credits);
  RUN_TEST(test_frozen_companion_preparation_counts_add_rows_and_preserves_source);
  RUN_TEST(test_runtime_frozen_bridge_preparation_binds_epoch_and_keeps_preflight_read_only);
  RUN_TEST(test_runtime_frozen_bridge_discards_stale_preparation_without_losing_capture);
  RUN_TEST(test_order_grains_count_filtered_inputs_and_finish_within_reserved_turns);
  RUN_TEST(test_capture_bridge_fill_preserves_frozen_events_and_companion_overlay);
  RUN_TEST(test_capture_bridge_fill_denials_preserve_capture_and_unpublished_candidate);
  RUN_TEST(test_deferred_stop_owner_resumes_canonical_rewrite_and_audible_turns);
  RUN_TEST(test_frozen_order_binding_rejects_changed_generation_count_and_projection);
  RUN_TEST(test_a_stale_canonical_binding_for_another_interval_rebinds_instead_of_wedging);
  RUN_TEST(test_a_same_interval_binding_whose_storage_epoch_moved_stays_refused);
  RUN_TEST(test_a_build_in_flight_keeps_the_stale_binding_refused);
  RUN_TEST(test_bridge_safe_boundary_differential_across_admission_and_pair_phases);
  RUN_TEST(test_bridge_missing_original_closure_cannot_bind_in_a_later_cycle);
  RUN_TEST(test_bridge_keeps_open_note_closure_until_changed_pair_next_cycle);
  RUN_TEST(test_bridge_retains_cross_wrap_closure_and_retires_it_once);
  RUN_TEST(test_bridge_cross_wrap_replacement_trace_matches_safe_note_instances);
  RUN_TEST(test_bridge_open_note_repeated_identity_pairs_keep_their_own_closures);
  RUN_TEST(test_runtime_bridge_selection_requires_complete_source_coverage_and_live_ledger);
  RUN_TEST(test_runtime_bridge_cursor_survives_ticks_and_drains_tail_before_wrap);
  RUN_TEST(test_pass_origin_view_fill_rejects_stale_or_edited_sources_and_published_writes);
  RUN_TEST(test_companion_bridge_maps_canonical_windows_without_full_loop_base);
  RUN_TEST(test_canonical_window_bridge_denies_changed_coverage_and_external_closure);
  RUN_TEST(test_frozen_canonical_pipeline_bridge_matches_materialized_reference);
  RUN_TEST(test_per_pass_bridge_preserves_capture_and_repeated_identity_scope);
  RUN_TEST(test_per_pass_bridge_generated_capture_maps_match_canonical_reference);
  RUN_TEST(test_per_pass_bridge_maximum_base_and_capture_result);
  RUN_TEST(test_per_pass_origin_index_maximum_interleaved_base);
  RUN_TEST(test_per_pass_bridge_mapping_denies_invalid_provenance_and_reuse);
  RUN_TEST(test_ranked_bridge_orders_unchanged_base_and_replacement_ties);
  RUN_TEST(test_ranked_bridge_batches_match_sequential_reference);
  RUN_TEST(test_ranked_bridge_retains_original_cross_wrap_off);
  RUN_TEST(test_bridge_owns_every_reserved_ledger_closure_and_retires_receipts);
  RUN_TEST(test_bridge_boundary_rebind_matches_fresh_compile_across_live_cycles);
  RUN_TEST(test_second_source_keeps_selected_original_off);
  RUN_TEST(test_bridge_successor_inherits_wrap_closure_without_replaying_it);
  RUN_TEST(test_initial_window_inherits_off_beyond_coverage_then_rotates);
  RUN_TEST(test_runtime_bridge_windows_keep_closure_after_worker_cleanup);
  RUN_TEST(test_bridge_closure_crosses_two_successor_windows);
  RUN_TEST(test_bridge_successor_preserves_gate_when_revised_off_moves_before_original_closure);
  RUN_TEST(test_bridge_successor_rejects_retired_predecessor_and_changed_source);
  RUN_TEST(test_ranked_bridge_denies_reused_mapping_before_completion);
  RUN_TEST(test_bridge_merge_matches_complete_oracle_with_insert_suppress_replace);
  RUN_TEST(test_bridge_cursor_skips_completed_phase_but_replays_on_next_cycle);
  RUN_TEST(test_bridge_build_denies_stale_source_and_invalid_target);
  RUN_TEST(test_bridge_merge_differential_varied_sources_and_long_suppression_runs);
  RUN_TEST(test_bridge_replacement_preserves_equal_key_provenance);
  RUN_TEST(test_bridge_stream_uses_existing_midi_dedup_and_resumes_after_future_event);
  RUN_TEST(test_bridge_candidate_build_keeps_selected_stream_readable);
  RUN_TEST(test_playback_runtime_slot_is_stable_after_prewarm_touch);
  RUN_TEST(test_playback_order_alloc_is_stable_after_prewarm_touch);
  RUN_TEST(test_first_record_bar_prepares_before_capture_and_survives_cache_reset);
  RUN_TEST(test_first_record_preparation_failure_does_not_change_existing_runtime);
  RUN_TEST(test_first_record_return_window_releases_for_the_next_take);
  RUN_TEST(test_first_record_bound_window_is_adopted_as_the_audible_source);
  RUN_TEST(test_first_record_first_bar_window_serves_a_multi_bar_launch);
  RUN_TEST(test_deferred_record_return_window_binds_the_owned_frozen_source);
  RUN_TEST(test_deferred_record_projection_refusal_leaves_the_window_untouched);
  RUN_TEST(test_deferred_record_return_window_refuses_an_unsealed_range);
  RUN_TEST(test_frozen_record_source_builds_the_next_playback_window);
  RUN_TEST(test_legacy_record_adopter_cannot_reidentify_frozen_capture_ready);
  RUN_TEST(test_projection_closes_only_lanes_open_at_the_stop);
  RUN_TEST(test_frozen_record_window_refuses_a_stale_range);
  RUN_TEST(test_deferred_record_interval_is_eligible_only_under_a_pending_owner);
  RUN_TEST(test_first_record_return_window_release_covers_a_bound_window);
  RUN_TEST(test_first_record_window_refresh_reuses_capture_store_and_restarts_on_append);
  RUN_TEST(test_boot_playback_prewarm_does_not_allocate_capture_workers_and_upgrades_in_place);
  RUN_TEST(test_capture_staging_roles_exchange_without_sharing_physical_storage);
  RUN_TEST(test_staging_only_runtime_finishes_capture_orders_without_admitting_a_bridge);
  RUN_TEST(test_runtime_view_set_prewarm_has_fixed_capacity_and_no_growth);
  RUN_TEST(test_active_runtime_access_does_not_create_an_unprewarmed_view_set);
  RUN_TEST(test_stopped_oracle_publishes_a_fixed_current_view_without_growth);
  RUN_TEST(test_full_loop_view_selector_requires_exact_identity_and_coverage);
  RUN_TEST(test_runtime_view_coverage_wrap_and_ledger_summary_are_stable);
  RUN_TEST(test_capture_note_reservation_releases_its_slot_after_matching_off);
  RUN_TEST(test_capture_note_reservation_rejects_when_its_full_lifetime_will_not_fit);
  RUN_TEST(test_capture_reservation_can_precede_canonical_note_identity_consumption);
  RUN_TEST(test_capture_note_reservations_cover_every_open_note_slot);
  RUN_TEST(test_capture_staging_freeze_keeps_only_reserved_closures_available);
  RUN_TEST(test_frozen_capture_staging_consumes_only_the_reserved_terminal_noteoff);
  RUN_TEST(test_capture_staging_reserves_independent_events_before_freeze);
  RUN_TEST(test_capture_staging_reseeds_a_cross_wrap_note_with_a_fresh_closure_reservation);
  RUN_TEST(test_frozen_capture_filter_excludes_a_completed_short_pair);
  RUN_TEST(test_filtered_capture_rewrite_trims_rows_in_later_grains);
  RUN_TEST(test_canonical_copy_uses_reserved_output_at_maximum_staging_capacity);
  RUN_TEST(test_canonical_copy_denies_stale_generation_and_replaced_output);
  RUN_TEST(test_canonical_copy_of_filtered_pair_consumes_no_chunks);
  RUN_TEST(test_frozen_capture_order_uses_only_fixed_staging_storage);
  RUN_TEST(test_frozen_capture_order_waits_for_every_reserved_terminal_closure);
  RUN_TEST(test_frozen_capture_canonical_order_rewrites_store_in_fixed_grains);
  RUN_TEST(test_runtime_keeps_canonical_and_audible_frozen_orders_serial);
  RUN_TEST(test_runtime_rewrites_frozen_canonical_capture_in_one_declared_quantum);
  RUN_TEST(test_prepared_capture_canonicalization_prevents_legacy_sort_at_seal);
  RUN_TEST(test_ping_pong_order_preserves_permutation_and_private_source);
  RUN_TEST(test_ping_pong_order_measures_dense_work_and_resets_direction);
  RUN_TEST(test_ordered_inputs_finish_in_fill_grains_without_touching_merge_scratch);
  RUN_TEST(test_order_fill_checks_retained_adjacency_and_resets_sort_detection);
  RUN_TEST(test_order_fill_detects_rank_inversions_at_equal_tick);
  RUN_TEST(test_runtime_order_builder_yields_and_preserves_same_tick_off_before_on);
  RUN_TEST(test_runtime_order_builder_preserves_equal_phase_off_before_on_across_wrap);
  RUN_TEST(test_runtime_view_build_retains_empty_non_note_and_repeated_note_id_instances);
  RUN_TEST(test_runtime_view_fill_yields_and_denies_an_unreserved_source);
  RUN_TEST(test_view_build_keeps_fill_and_order_as_separate_resumable_grains);
  RUN_TEST(test_coverage_contract_accounts_for_denied_turns_and_phase_wrap);
  RUN_TEST(test_runtime_retains_only_a_coverage_proven_pending_build);
  RUN_TEST(test_three_view_rotation_keeps_retired_current_idle_until_build_completes);
  RUN_TEST(test_source_change_rotation_keeps_both_successive_pending_builds_distinct);
  RUN_TEST(test_paused_build_cannot_mutate_published_current_or_ready_view);
  RUN_TEST(test_second_bridge_candidate_does_not_mutate_selected_bridge);
  RUN_TEST(test_pending_stop_transport_stop_invalidates_playing_completion);
  RUN_TEST(test_pending_stop_rejects_an_undeclared_or_over_capacity_tail);
  RUN_TEST(test_pending_stop_orders_only_after_reserved_closures);
  RUN_TEST(test_stop_admission_contract_counts_fixed_order_work);
  RUN_TEST(test_stop_contract_rejects_saturated_counts_and_invalid_quantum);
  RUN_TEST(test_stop_reserves_ranked_preparation_separately_and_preserves_stop_precedence);
  RUN_TEST(test_stop_accepts_long_ranked_service_without_horizon_rejection);
  RUN_TEST(test_stop_completed_workers_release_large_unused_estimates_without_waiting);
  RUN_TEST(test_empty_stop_retires_skipped_tail_before_playing_or_transport_stop_commit);
  RUN_TEST(test_transport_stop_before_tail_retires_only_skipped_preparation);
  RUN_TEST(test_prepared_ranked_tail_bound_preserves_other_children_and_invalid_inputs);
  RUN_TEST(test_ranked_tail_bound_uses_declared_pass_and_base_horizons);
  RUN_TEST(test_live_canonical_base_prework_reuses_stop_worker_and_invalidates_source);
  RUN_TEST(test_continuous_prefix_reuses_completed_passes_after_appends);
  RUN_TEST(test_continuous_prefix_reuses_snapshot_identity_refresh);
  RUN_TEST(test_normalization_scratch_receipt_rejects_superseded_builder);
  RUN_TEST(test_continuous_prefix_retains_transferred_tail);
  RUN_TEST(test_continuous_prefix_preserves_completed_work_when_append_interrupts_read);
  RUN_TEST(test_maximum_frozen_capture_has_a_declared_stop_budget);
  RUN_TEST(test_session_exit_receipt_ring_is_fifo_and_not_serialized);
  RUN_TEST(test_prepared_companion_history_is_job_qualified);
  RUN_TEST(test_full_take_admissibility_follows_the_windowed_policy);
  RUN_TEST(test_whole_take_reservation_is_released_once_the_loop_is_known_long);
  RUN_TEST(test_release_refuses_while_a_bridge_is_selected);
  RUN_TEST(test_whole_take_release_reprewarm_recreates_side_arrays);
  RUN_TEST(test_windowed_only_slot_reserves_no_whole_take_representation);
  RUN_TEST(test_windowed_only_slot_refuses_the_whole_take_publishers);
  RUN_TEST(test_windowed_slot_upgrades_to_whole_take_at_a_cold_admission);
  RUN_TEST(test_frozen_record_capture_range_expires_during_fill_but_not_after_copy);
  RUN_TEST(test_frozen_record_block_index_keeps_out_of_order_and_wrapped_rows);
  RUN_TEST(test_frozen_record_index_invalidates_before_tick_rewrite);
  RUN_TEST(test_frozen_record_source_is_windowed_quantized_and_immutable);
  RUN_TEST(test_frozen_record_source_denies_expiry_and_window_overflow);
  RUN_TEST(test_frozen_record_source_rejects_pool_reset_and_failed_row_reads);
  RUN_TEST(test_3_15e_slice1_one_window_carries_the_committed_base_and_the_live_overlay);
  RUN_TEST(test_3_15e_slice1_the_overlay_half_refuses_by_name_and_never_leaves_a_partial_window);
  RUN_TEST(test_3_15e_slice2b_a_base_without_provenance_cannot_take_an_overlay_half);
  RUN_TEST(test_3_15e_slice2b_the_stopped_oracle_consults_the_overlay_candidate_and_retires_a_stale_one);
  RUN_TEST(test_3_15e_slice3_probe_row_origin_is_visible_at_emission_time);
  RUN_TEST(test_3_15e_slice3_prefix_preserving_extension_never_moves_a_published_prefix);
  return UNITY_END();
}
#endif // FROZEN_RECORD_READER_SURVIVAL_ORACLE
