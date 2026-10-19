// Existing production maintenance completion seam: cancellation happens AFTER Build returns.
namespace TrackPublicationCallFixture {
inline LoopPlaybackRuntime* owner;
inline bool resetAccepted;
inline unsigned completions;
PlaybackDeadlineClock resetAtCompletion() {
  ++completions;
  resetAccepted = owner->reset(true);
  return nativePlaybackDeadlineClock;
}
}
void test_track_reset_after_build_completion_rejects_captured_result() {
  using namespace TrackPublicationCallFixture;
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, 300 * bar, 12, 14, 32, 12 * bar);
  owner = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const auto work = *owner->activeViewBuildWork();
  nativePlaybackDeadlineClock = {work.admittedTick + 1, work.clockEpoch};
  resetAccepted = false; completions = 0;
  Track::PlaybackViewTurnResult result = Track::PlaybackViewTurnResult::Idle;
  for (unsigned i = 0; i < 4096 && !completions; ++i)
    result = track.stepPlaybackViewMaintenance(12 * bar, nullptr, false,
        nativePlaybackDeadlineClock, resetAtCompletion);
  TEST_ASSERT_EQUAL_UINT32(1, completions);
  TEST_ASSERT_FALSE(resetAccepted); // Acquired preparation retains selection through judgement.
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewTurnResult::GrainDenied),
      static_cast<int>(result), "a cancelled completion cannot report BuildComplete");
  TEST_ASSERT_TRUE(owner->reset(true)); // explicit caller retry after turn unwind
  auto* set = owner->viewSetIfPrewarmed();
  for (const auto& view : set->views) TEST_ASSERT_FALSE(view.complete || view.published);
  TEST_ASSERT_FALSE(owner->completedPromisePendingJudgement);
  TEST_ASSERT_FALSE(owner->ownsCoverageSource());
  owner = nullptr;
}

void test_track_ready_adoption_does_not_promote_private_build() {
  auto& track = trackManager.getTrack(0);
  auto* owner = TrackCaptureStopTestAccess::prewarmLiveSourceRuntime(track, 0);
  TEST_ASSERT_NOT_NULL(owner);
  auto* set = owner->viewSetIfPrewarmed();
  const auto current = set->lifecycle.current;
  const auto ready = set->lifecycle.ready;
  const auto build = set->lifecycle.build;
  TEST_ASSERT_TRUE(current < set->views.size() && ready < set->views.size() && build < set->views.size());
  // Initial immutable current and already-qualified ready are fixture state.
  set->views[current].complete = set->views[current].published = true;
  set->views[ready].complete = true;
  const auto retiredGeneration = set->views[current].storageGeneration;
  // Private completion uses actual Build/Fill/Order, not a hand-set complete bit.
  const PlaybackCoverageInterval coverage{0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR};
  RuntimePlaybackViewBuild::PreparationTurn preparation(owner->viewBuild);
  TEST_ASSERT_TRUE(static_cast<bool>(preparation));
  TEST_ASSERT_TRUE(owner->viewBuild.begin(set->views[build], nullptr, 0, {}, coverage, {}, {}));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, owner->viewBuild.stepFill(1));
  const auto privateGeneration = set->views[build].storageGeneration;
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  TEST_ASSERT_EQUAL_UINT8(ready, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.ready,
      "adoption consumes ready; it cannot certify a complete private build");
  TEST_ASSERT_EQUAL_UINT8(build, set->lifecycle.build);
  TEST_ASSERT_EQUAL_UINT64(privateGeneration, set->views[build].storageGeneration);
  TEST_ASSERT_EQUAL_UINT64(retiredGeneration, set->views[current].storageGeneration);
  TEST_ASSERT_TRUE(set->views[build].complete);
  TEST_ASSERT_FALSE(track.adoptPreparedPlaybackWindow(0));
  TEST_ASSERT_TRUE(set->lifecycle.publishCompletedBuildAsReady(set->views));
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  TEST_ASSERT_EQUAL_UINT8(build, set->lifecycle.current);
}

namespace TrackPublicationCallFixture {
inline RuntimePlaybackViewSet* maskSet;
inline uint8_t maskReady;
inline unsigned deliveredInterrupts;
void observeAdopted() {
  ++deliveredInterrupts;
  TEST_ASSERT_EQUAL_UINT32(0, PlaybackSourceMaskFixture::primask);
  TEST_ASSERT_EQUAL_UINT8(maskReady, maskSet->lifecycle.current);
  TEST_ASSERT_TRUE(maskSet->views[maskReady].published);
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, maskSet->lifecycle.ready);
}
}
void test_track_adoption_metadata_restores_saved_mask() {
  using namespace TrackPublicationCallFixture;
  auto& track = trackManager.getTrack(0);
  auto* owner = TrackCaptureStopTestAccess::prewarmLiveSourceRuntime(track, 0);
  TEST_ASSERT_NOT_NULL(owner);
  maskSet = owner->viewSetIfPrewarmed();
  for (uint32_t incoming : {0u,1u}) {
    maskSet->resetMetadata();
    maskReady = maskSet->lifecycle.ready;
    maskSet->views[maskSet->lifecycle.current].complete = true;
    maskSet->views[maskReady].complete = true;
    PlaybackSourceMaskFixture::reset(incoming); deliveredInterrupts = 0;
    PlaybackSourceMaskFixture::pendingInterrupt = observeAdopted;
    TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
    TEST_ASSERT_FALSE(PlaybackSourceMaskFixture::events.empty());
    TEST_ASSERT_EQUAL_UINT32(incoming, PlaybackSourceMaskFixture::primask);
    TEST_ASSERT_EQUAL_UINT32(0, PlaybackSourceMaskFixture::depth);
    TEST_ASSERT_EQUAL_UINT32(incoming ? 0u : 1u, deliveredInterrupts);
    if (incoming) {
      TEST_ASSERT_NOT_NULL(PlaybackSourceMaskFixture::pendingInterrupt);
      PlaybackSourceMaskFixture::primask = 0;
      PlaybackSourceMaskFixture::pendingInterrupt = nullptr;
      observeAdopted();
    }
  }
  PlaybackSourceMaskFixture::reset(); maskSet = nullptr;
}

namespace TrackPublicationCallFixture {
inline LoopPlaybackRuntime* maskOwner;
void observePublished() {
  ++deliveredInterrupts;
  TEST_ASSERT_EQUAL_UINT32(0, PlaybackSourceMaskFixture::primask);
  TEST_ASSERT_EQUAL_UINT8(maskReady, maskSet->lifecycle.current);
  TEST_ASSERT_TRUE(maskSet->views[maskReady].complete && maskSet->views[maskReady].published);
  TEST_ASSERT_FALSE(maskOwner->viewBuild.completedDestinationIs(maskSet->views[maskReady]));
}
}
void test_track_publication_metadata_restores_saved_mask() {
  using namespace TrackPublicationCallFixture;
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  for (uint32_t incoming : {0u,1u}) {
    const uint8_t slot = static_cast<uint8_t>(incoming);
    prepareOwingWindowHolder(track, slot, 300 * bar, 12, 14, 32, 12 * bar);
    maskOwner = TrackCaptureStopTestAccess::runtimeIfAllocated(track, slot);
    maskSet = maskOwner->viewSetIfPrewarmed();
    maskReady = maskSet->lifecycle.build;
    for (unsigned i = 0; i < 4096 && maskOwner->coverageBuildNeedsService(); ++i) {
      if (maskOwner->nextCoverageBuildGrain() == RuntimePlaybackViewBuildGrain::Fill)
        maskOwner->stepCoverageBuildFill(64);
      else maskOwner->stepCoverageBuildOrder(64);
    }
    TEST_ASSERT_TRUE(maskOwner->viewBuild.completedDestinationIs(maskSet->views[maskReady]));
    PlaybackSourceMaskFixture::reset(incoming); deliveredInterrupts = 0;
    PlaybackSourceMaskFixture::pendingInterrupt = observePublished;
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::publishCompletedWindow(track, slot));
    TEST_ASSERT_FALSE(PlaybackSourceMaskFixture::events.empty());
    TEST_ASSERT_EQUAL_UINT32(incoming, PlaybackSourceMaskFixture::primask);
    TEST_ASSERT_EQUAL_UINT32(0, PlaybackSourceMaskFixture::depth);
    TEST_ASSERT_EQUAL_UINT32(incoming ? 0u : 1u, deliveredInterrupts);
    if (incoming) {
      TEST_ASSERT_NOT_NULL(PlaybackSourceMaskFixture::pendingInterrupt);
      PlaybackSourceMaskFixture::pendingInterrupt = nullptr;
      PlaybackSourceMaskFixture::primask = 0;
      observePublished();
    }
  }
  PlaybackSourceMaskFixture::reset(); maskOwner = nullptr; maskSet = nullptr;
}
