//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

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
#include "../../src/Track/RuntimeCaptureStaging.cpp"
#include "../../src/Track/RuntimePlaybackOrderBuilder.cpp"
#include "../../src/Track/RuntimePlaybackBridge.cpp"
#include "../../src/Track/RuntimePlaybackBridgeRead.cpp"
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
#include "../../src/Track/TrackPreparedStopCommit.cpp"
#include "../../src/Track/TrackPreparedUndo.cpp"
#include "Loop.h"
#include "TrackPlaybackRuntime.h"
#include "Utils/MidiServiceDrain.h"
#include "Utils/DisplayWindowUtils.h"

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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(0));
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
  TEST_ASSERT_TRUE(runtime.preparedPairStorage.isPrewarmed());

  TEST_ASSERT_TRUE(runtime.prewarmViews(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                       /*fullTakeAdmissible=*/false));
  TEST_ASSERT_FALSE(set.wholeTakeViewsReserved());
  TEST_ASSERT_TRUE(set.fixedExternalBytes() < wholeTakeBytes);
  for (uint8_t i = 0; i < RuntimePlaybackViewConfig::kBridgeCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(0, set.bridges[i].capacity);
    TEST_ASSERT_EQUAL_UINT32(0, set.bridgeBases[i].storage.capacity);
  }
  TEST_ASSERT_FALSE(runtime.preparedPairStorage.isPrewarmed());
  TEST_ASSERT_FALSE(set.beginCandidateBridge());
  // The windowed views and both stagings survive the release.
  for (uint8_t i = 0; i < RuntimePlaybackViewConfig::kViewCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kEventsPerView,
                             set.views[i].storage.capacity);
  }
  TEST_ASSERT_TRUE(set.captureStagingsPrewarmed());
}

void test_release_refuses_while_a_bridge_is_selected() {
  // The selected bridge is the live authoritative representation; releasing its backing
  // would be a use-after-free, so the release must be refused until it is deselected.
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  set.bridgeLifecycle.selected = 0;
  TEST_ASSERT_FALSE(set.releaseWholeTakeViews());
  TEST_ASSERT_TRUE(set.wholeTakeViewsReserved());
  set.bridgeLifecycle.reset();
  TEST_ASSERT_TRUE(set.releaseWholeTakeViews());
  TEST_ASSERT_FALSE(set.wholeTakeViewsReserved());
}

void test_windowed_only_slot_reserves_no_whole_take_representation() {
  // RC-MEM-2: a loop that can never publish a full-take view (every full-take publisher
  // requires an exactly full-loop coverage interval) must reserve only what the three rolling windows
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
  TEST_ASSERT_FALSE(runtime.preparedPairStorage.isPrewarmed());

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
  TEST_ASSERT_TRUE(wholeTake.preparedPairStorage.isPrewarmed());
  TEST_ASSERT_FALSE(runtime.preparedPairStorage.isPrewarmed());

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
  TEST_ASSERT_TRUE(runtime.preparedPairStorage.isPrewarmed());
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(61));
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(61));
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(62));
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(63));
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

  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(41));
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(52));
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
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Complete, emptyBuild.stepFill(1));
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
  while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Fill) {
    (void)build.stepFill(2);
  }
  while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) {
    (void)build.stepOrder(2);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Complete, build.nextGrain());
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
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Fill, build.nextGrain());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Fill, build.stepFill(1));
  TEST_ASSERT_EQUAL_UINT32(1, view.storage.eventCount);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Order, build.stepFill(1));
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  TEST_ASSERT_FALSE(view.complete);
  while (build.active()) {
    const RuntimePlaybackViewBuild::Grain result = build.stepOrder(1);
    TEST_ASSERT_TRUE(result == RuntimePlaybackViewBuild::Grain::Order ||
                     result == RuntimePlaybackViewBuild::Grain::Complete);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Complete, build.nextGrain());
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
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Denied, runtime.nextCoverageBuildGrain());

  const CoverageServiceContract admitted{8, 0, 1, 1, 1, 8};
  TEST_ASSERT_TRUE(runtime.beginCoverageBuild(admitted, 700, 100, source, 2,
                                              PlaybackViewSourceIdentity{1, 1, nullptr}, coverage,
                                              EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_FALSE(runtime.beginCoverageBuild(admitted, 700, 100, source, 2,
                                               PlaybackViewSourceIdentity{2, 2, nullptr}, coverage,
                                               EmissionCursor{}, LedgerBoundarySummary{}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Fill, runtime.nextCoverageBuildGrain());
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Order, runtime.stepCoverageBuildFill(2));
  while (runtime.nextCoverageBuildGrain() == RuntimePlaybackViewBuild::Grain::Order) {
    (void)runtime.stepCoverageBuildOrder(1);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Complete, runtime.nextCoverageBuildGrain());
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
  // Three entries: each order needs the initial fill plus two merge/copy-back
  // passes (15 operations, four 4-entry grains). Stop admission also owns a
  // one-grain rewrite and two chunk-trim turns, so canonical + rewrite +
  // audible needs eleven fixed grains.
  TEST_ASSERT_EQUAL_UINT32(11, contract.requiredOrderGrains(3));
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
  TEST_ASSERT_EQUAL_UINT16(6240, stop.remainingRankedPreparationGrains);
  TEST_ASSERT_EQUAL_UINT16(contract.requiredOrderGrains(2048), stop.remainingOrderGrains);
  TEST_ASSERT_FALSE(stop.consumeRankedPreparationGrain());
  TEST_ASSERT_TRUE(stop.consumeClosureGrain());
  while (stop.remainingOrderGrains) TEST_ASSERT_TRUE(stop.consumeOrderGrain());
  TEST_ASSERT_FALSE(stop.canCommit(7, PendingStopDestination::Playing));
  TEST_ASSERT_TRUE(stop.consumeRankedPreparationGrain());
  stop.requestTransportStop();
  TEST_ASSERT_EQUAL_UINT16(6239, stop.remainingRankedPreparationGrains);
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
      > StopAdmissionContract::kProvisionalRankedPreparationGrains);
  TEST_ASSERT_EQUAL_UINT64(6240,
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
  TEST_ASSERT_FALSE(first.mergedMatches(runtime.canonicalPassInputs, source));
  TEST_ASSERT_TRUE(second.mergedMatches(runtime.canonicalPassInputs, source));
  second.discard();
  TEST_ASSERT_FALSE(second.mergedMatches(runtime.canonicalPassInputs, source));
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(4));
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
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Fill, runtime.viewBuild.nextGrain());
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
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureCanonicalOrder(4));
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
    TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(4));
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
  TEST_ASSERT_FALSE(small.hasCaptureOwner());
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
    const size_t retainedNormalizedPasses = runtime.frozenCompanionPasses.mergedPassCount();
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
      TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Fill, build.nextGrain());
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
    for (size_t turn = 0; turn < 100 && build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order; ++turn)
      build.stepOrder(1);
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Complete, build.nextGrain());
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
    TEST_ASSERT_TRUE(f.runtime.canonicalPassInputs.idle()); // Cleanup only.
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
                                RuntimePlaybackViewFillCoverage::PlaybackCoverage));
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
        TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(71));
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(81));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending,
                    runtime.stepFrozenCaptureCanonicalOrder(1));
  // Simulate an invalidated source between owner turns. Neither begin nor a
  // direct step may continue writing indexes for the old captured identity.
  staging.captureGeneration = staging.frozenGeneration = 82;
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(768, 82));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureOrder(1));
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureCanonicalOrder(82));
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
  TEST_ASSERT_TRUE(runtime.beginFrozenCaptureCanonicalOrder(83));
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureOrder(768, 83));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, runtime.stepFrozenCaptureCanonicalOrder(1));
  TEST_ASSERT_TRUE(staging.storage.append(MidiEvent::ControlChange(10, 1, 1, 4)));
  TEST_ASSERT_FALSE(runtime.beginFrozenCaptureCanonicalOrder(83));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, runtime.stepFrozenCaptureCanonicalOrder(1));
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::ControlChange(99, 1, 1, 5)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, runtime.stepFrozenCaptureCanonicalRewrite(store, 83, 1));
  TEST_ASSERT_EQUAL_UINT32(99, store.at(0).tick);
  runtime.reset(true);
  TEST_ASSERT_FALSE(runtime.frozenCaptureOrderBinding.bound);
  TEST_ASSERT_FALSE(runtime.frozenCaptureCanonicalOrderBinding.bound);
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
                              RuntimePlaybackViewFillCoverage::PlaybackCoverage));
  for (unsigned i = 0; i < 5; ++i) {
    TEST_ASSERT_FALSE(view.complete);
    const auto next = build.stepFill(1);
    TEST_ASSERT_EQUAL(i == 4 ? RuntimePlaybackViewBuild::Grain::Order : RuntimePlaybackViewBuild::Grain::Fill, next);
  }
  while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) build.stepOrder(1);
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
      {0, 768, 1536}, {}, {}, RuntimePlaybackViewFillCoverage::PlaybackCoverage));
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
                              RuntimePlaybackViewFillCoverage::PlaybackCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Denied, build.stepFill(4));
  TEST_ASSERT_FALSE(view.sourceFilled); TEST_ASSERT_FALSE(view.complete); TEST_ASSERT_FALSE(view.published);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  // The owner can discard the denied candidate and prepare a different window.
  build.discard();
  TEST_ASSERT_TRUE(build.begin(view, events, 4, {2, 2, nullptr}, {768, 768, 1536}, {}, {},
                              RuntimePlaybackViewFillCoverage::PlaybackCoverage));
  build.stepFill(4);
  while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) build.stepOrder(1);
  TEST_ASSERT_TRUE(view.complete);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
}

static void buildSharedTestBar(RuntimePlaybackView& view, uint32_t bar) {
  const MidiEvent event = MidiEvent::NoteOn(bar * 768 + 32, 1, 60, 90);
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_TRUE(build.begin(view, &event, 1, {1, 1, nullptr},
      {bar * 768, 768, 16 * 768}, {}, {}, RuntimePlaybackViewFillCoverage::PlaybackCoverage));
  build.stepFill(1);
  while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) build.stepOrder(1);
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
  TEST_ASSERT_EQUAL_UINT8(2, set.lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set.lifecycle.build);
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
  while (runtime.nextCoverageBuildGrain() == RuntimePlaybackViewBuild::Grain::Order)
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
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Denied, build.stepFill(1));
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Denied, build.stepOrder(1));
    TEST_ASSERT_TRUE(build.begin(replacement, source, 3, {2, 2, nullptr}, {0, 768, 768}, {}, {}));
    build.stepFill(3);
    while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) build.stepOrder(1);
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
  for (size_t i = 0; i < 16 && runtime.nextCoverageBuildGrain() != RuntimePlaybackViewBuild::Grain::Denied; ++i) {
    runtime.serviceFirstRecordingBar(loop, source, 768, 1);
    if (runtime.nextCoverageBuildGrain() == RuntimePlaybackViewBuild::Grain::Denied) break;
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
                              RuntimePlaybackViewFillCoverage::RawCoverage));
  TEST_ASSERT_FALSE(view.bindRawCoverage(1536));
  build.stepFill(4);
  while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) build.stepOrder(1);
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
                                  RuntimePlaybackViewFillCoverage::PlaybackCoverage));
  reference.stepFill(4);
  while (reference.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) reference.stepOrder(1);
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
                              RuntimePlaybackViewFillCoverage::RawCoverage));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuild::Grain::Complete, build.stepFill(1));
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
        {bar * 768, 768, 0}, {}, {}, RuntimePlaybackViewFillCoverage::RawCoverage));
    build.stepFill(1);
    while (build.nextGrain() == RuntimePlaybackViewBuild::Grain::Order) build.stepOrder(1);
    TEST_ASSERT_TRUE(set.views[index].complete);
  };
  auto& life = set.lifecycle;
  for (uint8_t i = 0; i < 3; ++i) prepare(i, i);
  TEST_ASSERT_TRUE(life.retainCurrent(set.views));
  const auto* first = set.views[0].storage.events;
  TEST_ASSERT_FALSE(set.handoffReadyCoverage());
  TEST_ASSERT_TRUE(set.handoffReadyCoverage(PlaybackViewHandoffMode::Preparation));
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

  const CommitReason reasons[Loop::kSessionExitReceiptCapacity] = {
      CommitReason::OverdubStop, CommitReason::OverdubStopToStopped,
      CommitReason::OverdubStop, CommitReason::OverdubStopToStopped};
  for (size_t i = 0; i < Loop::kSessionExitReceiptCapacity; ++i) {
    loop.openOverdubSession(static_cast<uint32_t>(i));
    // The old singleton refused this once a receipt was pending.
    TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());
    TEST_ASSERT_TRUE(loop.handoffSessionExitSnapshot(reasons[i]));
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
    TEST_ASSERT_EQUAL(static_cast<int>(reasons[i]), static_cast<int>(receipt->reason));
  }
  TEST_ASSERT_NULL(loop.sessionExitReceiptAt(Loop::kSessionExitReceiptCapacity));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
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
  RUN_TEST(test_windowed_only_slot_reserves_no_whole_take_representation);
  RUN_TEST(test_windowed_only_slot_refuses_the_whole_take_publishers);
  RUN_TEST(test_windowed_slot_upgrades_to_whole_take_at_a_cold_admission);
  return UNITY_END();
}
