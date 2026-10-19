// Reusing the same address must not let an old builder invalidate a replacement generation.
void test_build_discard_preserves_repurposed_destination_generation() {
  for (unsigned builder = 0; builder < 2; ++builder) for (bool published : {false,true}) {
    LoopPlaybackRuntime owner;
    TEST_ASSERT_TRUE(owner.prewarmViews());
    auto& work = builder ? owner.bridgeWindowBuild : owner.viewBuild;
    auto& candidate = owner.viewSetIfPrewarmed()->views[2];
    TEST_ASSERT_TRUE(work.begin(candidate, nullptr, 0, {},
        {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}));
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, work.stepFill(1));
    candidate.resetMetadata(); // Actual fixed-storage reuse: fresh non-recycled generation.
    candidate.complete = candidate.sourceFilled = true;
    candidate.published = published;
    TEST_ASSERT_FALSE(work.completedDestinationIs(candidate));
    const auto replacement = candidate.storageGeneration;
    work.discard();
    TEST_ASSERT_TRUE_MESSAGE(candidate.complete && candidate.sourceFilled,
        "stale cleanup cannot clear a replacement destination generation");
    TEST_ASSERT_EQUAL_UINT64(replacement, candidate.storageGeneration);
  }
}
void test_build_query_rejects_repurposed_destination_generation() {
  for (unsigned builder = 0; builder < 2; ++builder) for (bool published : {false,true}) {
    LoopPlaybackRuntime owner;
    TEST_ASSERT_TRUE(owner.prewarmViews());
    auto& work = builder ? owner.bridgeWindowBuild : owner.viewBuild;
    auto& candidate = owner.viewSetIfPrewarmed()->views[2];
    TEST_ASSERT_TRUE(work.begin(candidate, nullptr, 0, {},
        {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}));
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, work.stepFill(1));
    candidate.resetMetadata();
    candidate.complete = candidate.sourceFilled = true;
    candidate.published = published;
    TEST_ASSERT_FALSE(work.completedDestinationIs(candidate));
    TEST_ASSERT_EQUAL_MESSAGE(RuntimePlaybackViewBuildGrain::Denied, work.nextGrain(),
        "a complete bit at the same address does not transfer builder authority");
    TEST_ASSERT_TRUE(candidate.complete && candidate.sourceFilled);
    work.discard(&candidate);
  }
}
