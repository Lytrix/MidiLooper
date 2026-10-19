// Deterministic interruptions through the actual runtime/build owners.
namespace AcquisitionCallFixture {
inline LoopPlaybackRuntime* owner;
inline bool attempted, released;
inline unsigned writer;
uint64_t directWriter(const void* context) {
  const auto& loop = *static_cast<const Loop*>(context);
  if (!attempted) {
    attempted = true;
    released = writer == 0 ? owner->releaseFirstRecordingBarReturnWindow()
                          : owner->bindFirstRecordingBar(Config::TICKS_PER_BAR);
  }
  return loop.captureContentGeneration();
}
}
void test_record_direct_writers_refuse_executing_builder_before_role_mutation() {
  using namespace AcquisitionCallFixture;
  for (unsigned lane = 0; lane < 2; ++lane) for (writer = 0; writer < 2; ++writer) {
    LoopEventStore::initPool();
    Loop loop;
    loop.beginCapture(CapturePhase::Record);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(10,1,60,100)));
    LoopPlaybackRuntime runtime;
    owner = &runtime;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(Config::TICKS_PER_BAR));
    auto* set = runtime.viewSetIfPrewarmed();
    const uint8_t retained = set->lifecycle.retained;
    const uint8_t build = set->lifecycle.build;
    const uint64_t retainedGeneration = set->views[retained].storageGeneration;
    runtime.viewBuild.discard(&set->views[retained]);
    auto& builder = lane ? runtime.bridgeWindowBuild : runtime.viewBuild;
    auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
    source.generation = directWriter;
    attempted = released = false;
    const bool began = builder.begin(set->views[build], source,
        {0,7,nullptr,PlaybackWindowOrigin::Capture,source.captureContentToken},
        {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{});
    TEST_ASSERT_TRUE(attempted);
    TEST_ASSERT_FALSE_MESSAGE(released, "direct Record writer must refuse an executing builder");
    TEST_ASSERT_TRUE(began);
    TEST_ASSERT_EQUAL_UINT8(retained,set->lifecycle.retained);
    TEST_ASSERT_EQUAL_UINT8(build,set->lifecycle.build);
    TEST_ASSERT_EQUAL_UINT64(retainedGeneration,set->views[retained].storageGeneration);
    TEST_ASSERT_TRUE(set->views[retained].complete && set->views[retained].rawCoveragePrepared);
    TEST_ASSERT_EQUAL_UINT32(1,loop.capture.store.size());
    builder.discard();
    loop.discardCapture();
  }
  owner = nullptr;
}

void test_preparation_acquires_before_begin_and_busy_reset_cancels_both_lanes() {
  using Turn = RuntimePlaybackViewBuild::PreparationTurn;
  for (unsigned lane = 0; lane < 2; ++lane) for (uint32_t incoming : {0u,1u}) {
    LoopPlaybackRuntime runtime;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    auto* set = runtime.viewSetIfPrewarmed();
    auto& builder = lane ? runtime.bridgeWindowBuild : runtime.viewBuild;
    auto& candidate = set->views[set->lifecycle.build];
    const uint64_t generation = candidate.storageGeneration;
    const uint8_t build = set->lifecycle.build;
    PlaybackSourceMaskFixture::reset(incoming);
    {
      Turn turn(builder);
      TEST_ASSERT_TRUE(static_cast<bool>(turn));
      TEST_ASSERT_TRUE(builder.preparationInProgress());
      TEST_ASSERT_EQUAL_UINT32(incoming,PlaybackSourceMaskFixture::primask);
      TEST_ASSERT_EQUAL_UINT32(0,PlaybackSourceMaskFixture::depth);
      Turn nested(builder);
      TEST_ASSERT_FALSE(static_cast<bool>(nested));
      TEST_ASSERT_FALSE(runtime.reset(false));
      TEST_ASSERT_TRUE(builder.preparationInProgress());
      TEST_ASSERT_TRUE(builder.preparationCancelled());
      TEST_ASSERT_FALSE(static_cast<bool>(turn));
      TEST_ASSERT_EQUAL_UINT8(build,set->lifecycle.build);
      TEST_ASSERT_EQUAL_UINT64(generation,candidate.storageGeneration);
      TEST_ASSERT_FALSE(builder.begin(candidate,nullptr,0,{},
          {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}));
      TEST_ASSERT_FALSE(candidate.complete || candidate.published);
      TEST_ASSERT_EQUAL_UINT32(incoming,PlaybackSourceMaskFixture::primask);
    }
    TEST_ASSERT_FALSE(builder.preparationInProgress());
    TEST_ASSERT_TRUE(runtime.reset(false)); // the real caller retry, not queued magic
    TEST_ASSERT_EQUAL_UINT32(incoming,PlaybackSourceMaskFixture::primask);
    PlaybackSourceMaskFixture::reset();
  }
}

void test_preparation_cancellation_rejects_completed_destination_and_late_entry() {
  using Turn = RuntimePlaybackViewBuild::PreparationTurn;
  for (unsigned lane = 0; lane < 2; ++lane) {
    LoopPlaybackRuntime runtime;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    auto* set = runtime.viewSetIfPrewarmed();
    auto& builder = lane ? runtime.bridgeWindowBuild : runtime.viewBuild;
    auto& candidate = set->views[set->lifecycle.build];
    {
      Turn turn(builder);
      TEST_ASSERT_TRUE(static_cast<bool>(turn));
      TEST_ASSERT_TRUE(builder.begin(candidate,nullptr,0,{},
          {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}));
      TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete,builder.stepFill(1));
      TEST_ASSERT_TRUE(builder.completedDestinationIs(candidate));
      builder.cancelPreparation();
      TEST_ASSERT_FALSE(static_cast<bool>(turn));
      TEST_ASSERT_FALSE(builder.completedDestinationIs(candidate));
      TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied,builder.nextGrain());
      TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied,builder.stepFill(1));
      TEST_ASSERT_FALSE(candidate.published);
    }
    builder.discard();
    Turn retry(builder);
    TEST_ASSERT_TRUE(static_cast<bool>(retry));
    TEST_ASSERT_TRUE(builder.begin(candidate,nullptr,0,{},
        {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}));
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete,builder.stepFill(1));
  }
}

void test_record_direct_writers_and_lease_release_refuse_preparation_before_begin() {
  using Turn = RuntimePlaybackViewBuild::PreparationTurn;
  for (unsigned lane = 0; lane < 2; ++lane) {
    LoopPlaybackRuntime runtime;
    TEST_ASSERT_TRUE(runtime.prewarmViews());
    TEST_ASSERT_TRUE(runtime.prepareFirstRecordingBar(Config::TICKS_PER_BAR));
    auto* set = runtime.viewSetIfPrewarmed();
    const auto retained = set->lifecycle.retained;
    const auto build = set->lifecycle.build;
    const auto generation = set->views[retained].storageGeneration;
    TEST_ASSERT_EQUAL(WorkingSetOutcome::Acquired,
        runtime.acquireWorkingSetLease(WorkingSetRole::PlaybackWindow));
    const auto lease = runtime.workingSetGeneration();
    {
      Turn turn(lane ? runtime.bridgeWindowBuild : runtime.viewBuild);
      TEST_ASSERT_TRUE(static_cast<bool>(turn));
      TEST_ASSERT_FALSE(runtime.releaseFirstRecordingBarReturnWindow());
      TEST_ASSERT_FALSE(runtime.bindFirstRecordingBar(Config::TICKS_PER_BAR));
      TEST_ASSERT_FALSE(runtime.releaseFrozenRecordWindow());
      TEST_ASSERT_EQUAL(WorkingSetOutcome::RefusedBusy,runtime.releaseAllWorkingSetLeases());
      TEST_ASSERT_EQUAL_UINT16(lease,runtime.workingSetGeneration());
      TEST_ASSERT_EQUAL_UINT8(retained,set->lifecycle.retained);
      TEST_ASSERT_EQUAL_UINT8(build,set->lifecycle.build);
      TEST_ASSERT_EQUAL_UINT64(generation,set->views[retained].storageGeneration);
      TEST_ASSERT_TRUE(set->views[retained].complete && set->views[retained].rawCoveragePrepared);
    }
    TEST_ASSERT_TRUE(runtime.releaseFirstRecordingBarReturnWindow());
  }
}

namespace AcquisitionBridgeFixture {
inline LoopPlaybackRuntime* runtime;
inline unsigned cut, seen, injected;
inline uint64_t generations[RuntimePlaybackViewConfig::kViewCount];
inline uint8_t buildIndex, contextIndex, current, ready, build;
inline bool resetAccepted;
void cancelAtBoundary() {
  if (!runtime->viewBuild.preparationInProgress() ||
      !runtime->bridgeWindowBuild.preparationInProgress() ||
      runtime->viewBuild.callInProgress() || runtime->bridgeWindowBuild.callInProgress()) return;
  if (++seen != cut) return;
  PlaybackSourceMaskFixture::observeUnmasked = nullptr;
  ++injected;
  auto* set = runtime->viewSetIfPrewarmed();
  for (unsigned i = 0; i < set->views.size(); ++i) generations[i] = set->views[i].storageGeneration;
  buildIndex = runtime->bridgeWindowBuildIndex;
  contextIndex = runtime->bridgeWindowContextIndex;
  current = set->lifecycle.current; ready = set->lifecycle.ready; build = set->lifecycle.build;
  resetAccepted = runtime->reset(false);
}
void prepare(LoopPlaybackRuntime& owner) {
  TEST_ASSERT_TRUE(owner.prewarmViews());
  auto* set = owner.viewSetIfPrewarmed();
  auto& base = set->bridgeBases[0]; auto& bridge = set->bridges[0];
  prepareBridgeBase(base,{MidiEvent::ControlChange(10,1,7,80),MidiEvent::ControlChange(20,1,8,90)});
  bridge.source = {2,2,nullptr}; bridge.coverage = base.coverage;
  bridge.cursorAnchor = {0,0,0,0,2};
  compileBridge(base,bridge,false);
  TEST_ASSERT_TRUE(owner.selectPreparedFullLoopBridge(bridge.source,768,bridge.cursorAnchor));
  for (unsigned i = 0; i < 16 && owner.bridgeWindowBuildIndex >= set->views.size(); ++i)
    owner.serviceBridgeWindow(0,192,1);
  TEST_ASSERT_TRUE(owner.bridgeWindowBuildIndex < set->views.size());
}
}
void test_bridge_cancelled_preparation_does_not_cleanup_selected_storage() {
  using namespace AcquisitionBridgeFixture;
  unsigned schedules = 0;
  for (cut = 1; cut <= 20; ++cut) {
    PlaybackSourceMaskFixture::reset();
    LoopPlaybackRuntime owner;
    prepare(owner); runtime = &owner;
    seen = injected = 0; resetAccepted = true;
    PlaybackSourceMaskFixture::observeUnmasked = cancelAtBoundary;
    (void)owner.serviceBridgeWindow(0,192,1);
    PlaybackSourceMaskFixture::observeUnmasked = nullptr;
    if (injected) {
      ++schedules;
      TEST_ASSERT_FALSE(resetAccepted);
      auto* set = owner.viewSetIfPrewarmed();
      for (unsigned i = 0; i < set->views.size(); ++i)
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(generations[i],set->views[i].storageGeneration,
            "cancelled bridge turn may not reset selected storage");
      TEST_ASSERT_EQUAL_UINT8(buildIndex,owner.bridgeWindowBuildIndex);
      TEST_ASSERT_EQUAL_UINT8(contextIndex,owner.bridgeWindowContextIndex);
      TEST_ASSERT_EQUAL_UINT8(current,set->lifecycle.current);
      TEST_ASSERT_EQUAL_UINT8(ready,set->lifecycle.ready);
      TEST_ASSERT_EQUAL_UINT8(build,set->lifecycle.build);
      TEST_ASSERT_TRUE(owner.reset(false));
    }
    runtime = nullptr;
  }
  TEST_ASSERT_TRUE_MESSAGE(schedules >= 2,"must reach multiple real unmasked bridge boundaries");
  PlaybackSourceMaskFixture::reset();
}

namespace AcquisitionBridgeFixture {
inline uint8_t adopted;
inline uint64_t adoptedGeneration;
void adoptAtBoundary() {
  if (!runtime->viewBuild.preparationInProgress() ||
      !runtime->bridgeWindowBuild.preparationInProgress() ||
      runtime->viewBuild.callInProgress() || runtime->bridgeWindowBuild.callInProgress()) return;
  if (++seen != cut) return;
  PlaybackSourceMaskFixture::observeUnmasked = nullptr;
  const auto* view = runtime->adoptFrozenRecordWindow({2,2,nullptr},768,192);
  if (!view) return;
  ++injected;
  auto* set = runtime->viewSetIfPrewarmed();
  adopted = set->lifecycle.current;
  adoptedGeneration = view->storageGeneration;
}
}
void test_bridge_service_revalidates_ready_role_after_unmasked_adoption() {
  using namespace AcquisitionBridgeFixture;
  unsigned schedules = 0;
  for (cut = 1; cut <= 16; ++cut) {
    PlaybackSourceMaskFixture::reset();
    LoopPlaybackRuntime owner; prepare(owner);
    auto* set = owner.viewSetIfPrewarmed();
    for (unsigned i = 0; i < 256; ++i) {
      const auto ready = set->lifecycle.ready;
      if (ready < set->views.size() && set->views[ready].complete &&
          set->views[ready].coverage.contains(192)) break;
      owner.serviceBridgeWindow(0,192,1);
    }
    TEST_ASSERT_TRUE(set->lifecycle.ready < set->views.size());
    TEST_ASSERT_TRUE(set->views[set->lifecycle.ready].complete);
    TEST_ASSERT_TRUE(set->views[set->lifecycle.ready].coverage.contains(192));
    runtime = &owner; seen = injected = 0;
    PlaybackSourceMaskFixture::observeUnmasked = adoptAtBoundary;
    (void)owner.serviceBridgeWindow(192,192,1);
    PlaybackSourceMaskFixture::observeUnmasked = nullptr;
    if (injected) {
      ++schedules;
      TEST_ASSERT_EQUAL_UINT8(adopted,set->lifecycle.current);
      const auto& current = set->views[adopted];
      TEST_ASSERT_EQUAL_UINT64(adoptedGeneration,current.storageGeneration);
      TEST_ASSERT_TRUE(current.complete && current.published);
      TEST_ASSERT_TRUE(current.coverage.contains(192));
      TEST_ASSERT_TRUE(set->lifecycle.ready == RuntimePlaybackViewLifecycle::kNoView ||
          set->lifecycle.ready < set->views.size());
      TEST_ASSERT_TRUE(set->lifecycle.build == RuntimePlaybackViewLifecycle::kNoView ||
          set->lifecycle.build < set->views.size());
    }
    runtime = nullptr;
  }
  TEST_ASSERT_TRUE_MESSAGE(schedules >= 2,"must adopt at multiple real unmasked service boundaries");
  PlaybackSourceMaskFixture::reset();
}
