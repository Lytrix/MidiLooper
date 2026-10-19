// Production Build/Fill/Order/runtime call-boundary regressions. The source query
// is a controlled interruption seam; no replacement cancellation state machine.
namespace LiveSourceCallFixture {
inline LoopPlaybackRuntime* runtime;
inline unsigned action;
inline unsigned calls;
inline bool resetAccepted;
inline RuntimePlaybackViewBuild* selected;
inline RuntimePlaybackView* candidate;
inline bool nestedRefused;
uint64_t interruptGeneration(const void* context) {
  const auto& loop = *static_cast<const Loop*>(context);
  const auto token = loop.captureActive() ? loop.captureContentGeneration() : 0;
  const unsigned requested = action;
  action = 0;
  if (requested) ++calls;
  if (requested == 1) runtime->cancelLiveCaptureBuildBorrows();
  if (requested == 2) resetAccepted = runtime->reset(false);
  if (requested == 3) {
    selected->discard(); selected->discard(); // idempotent while executing
  }
  if (requested == 4) {
    nestedRefused = selected->callInProgress() &&
        !selected->begin(*candidate, nullptr, 0, {},
            {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}) &&
        selected->stepFill(1) == RuntimePlaybackViewBuildGrain::Denied &&
        selected->stepOrder(1) == RuntimePlaybackViewBuildGrain::Denied &&
        selected->callInProgress();
    selected->discard();
  }
  if (requested == 5) { selected->discard(candidate); selected->discard(candidate); }
  if (requested == 6) {
    selected->discard(candidate); selected->discard(); selected->discard(candidate);
  }
  return token;
}
void exercise(unsigned builderIndex, unsigned stage, unsigned requested) {
  LoopEventStore::initPool();
  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100,1,60,100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200,1,60,0)));
  LoopPlaybackRuntime owner;
  TEST_ASSERT_TRUE(owner.prewarmViews());
  runtime = &owner;
  selected = builderIndex ? &owner.bridgeWindowBuild : &owner.viewBuild;
  auto* set = owner.viewSetIfPrewarmed();
  auto& published = set->views[0];
  TEST_ASSERT_TRUE(published.storage.append(MidiEvent::ControlChange(77,1,7,64)));
  published.complete = published.published = true;
  const auto publishedGeneration = published.storageGeneration;
  candidate = &set->views[1];
  owner.ledger.noteOn(1,61,2,20,90);
  auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  source.generation = interruptGeneration;
  action = stage == 0 ? requested : 0; calls = 0; resetAccepted = false;
  const bool began = selected->begin(*candidate, source,
      {0,7,nullptr,PlaybackWindowOrigin::Capture,source.captureContentToken},
      {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{});
  if (stage == 0) TEST_ASSERT_FALSE(began);
  else {
    TEST_ASSERT_TRUE(began);
    if (stage > 1) {
      TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order, selected->stepFill(8));
      if (stage == 3) {
        TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, selected->stepOrder(64));
      }
    }
    action = requested;
    const auto result = stage == 1 ? selected->stepFill(8)
        : stage == 2 ? selected->stepOrder(64) : selected->nextGrain();
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied, result);
  }
  TEST_ASSERT_EQUAL_UINT32(1,calls);
  if (requested == 4) TEST_ASSERT_TRUE(nestedRefused);
  if (requested == 5 && stage == 1) TEST_ASSERT_TRUE(candidate->sourceFilled);
  if (requested == 6) TEST_ASSERT_FALSE(candidate->sourceFilled);
  if (requested == 2 && stage < 3) TEST_ASSERT_FALSE(resetAccepted);
  TEST_ASSERT_FALSE(selected->ownsLiveCaptureBorrow());
  TEST_ASSERT_FALSE(candidate->published);
  TEST_ASSERT_TRUE(published.complete && published.published);
  TEST_ASSERT_EQUAL_UINT32(publishedGeneration,published.storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(1,published.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(77,published.storage.events[0].tick);
  TEST_ASSERT_TRUE(owner.ledger.isActive(1,61));
  TEST_ASSERT_EQUAL_UINT32(2,loop.capture.store.size());
  TEST_ASSERT_TRUE(loop.capture.store.at(0).isNoteOn());
  TEST_ASSERT_TRUE(loop.capture.store.at(1).isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(100,loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL_UINT32(200,loop.capture.store.at(1).tick);
  selected->discard();
  // Busy did not report a successful reset; an explicit caller retry can finish.
  TEST_ASSERT_TRUE(owner.reset(true));
  loop.discardCapture();
  runtime = nullptr; selected = nullptr; candidate = nullptr; action = 0;
}
}
void test_live_build_cancellation_during_begin_is_not_lost() {
  for (unsigned builder = 0; builder < 2; ++builder)
    LiveSourceCallFixture::exercise(builder,0,1);
}
void test_live_build_cancellation_during_fill_keeps_executing_state_retained() {
  for (unsigned builder = 0; builder < 2; ++builder)
    LiveSourceCallFixture::exercise(builder,1,1);
}
void test_live_build_runtime_reset_during_call_refuses_before_destructive_work() {
  for (unsigned builder = 0; builder < 2; ++builder)
    for (unsigned stage = 0; stage < 3; ++stage)
      LiveSourceCallFixture::exercise(builder,stage,2);
}
void test_live_build_order_and_complete_query_cancel_without_publishing() {
  for (unsigned builder = 0; builder < 2; ++builder)
    for (unsigned stage = 2; stage < 4; ++stage)
      LiveSourceCallFixture::exercise(builder,stage,1);
}
void test_live_build_repeated_in_call_discard_is_terminal() {
  for (unsigned builder = 0; builder < 2; ++builder)
    for (unsigned stage = 0; stage < 3; ++stage)
      LiveSourceCallFixture::exercise(builder,stage,3);
}
void test_live_build_reentry_and_conflicting_preserve_requests_refuse() {
  for (unsigned builder = 0; builder < 2; ++builder) {
    for (unsigned stage = 0; stage < 3; ++stage)
      LiveSourceCallFixture::exercise(builder,stage,4);
    LiveSourceCallFixture::exercise(builder,1,5);
    LiveSourceCallFixture::exercise(builder,1,6);
  }
}

namespace LiveSourceMaskTests {
inline RuntimePlaybackView* destination;
inline Loop* sourceLoop;
inline unsigned queryCount, interruptAt, interruptCount, interruptDepth;
inline bool allQueriesMasked;
inline std::vector<size_t> visibleCounts;
void observe() { visibleCounts.push_back(destination->storage.eventCount); }
void rewrite() {
  ++interruptCount;
  sourceLoop->capture.store.clear();
  (void)sourceLoop->appendCaptureEvent(MidiEvent::ControlChange(333,1,7,99));
}
uint64_t generation(const void* context) {
  const auto& loop = *static_cast<const Loop*>(context);
  allQueriesMasked = allQueriesMasked && PlaybackSourceMaskFixture::primask == 1;
  if (++queryCount == interruptAt) {
    interruptDepth = PlaybackSourceMaskFixture::depth;
    PlaybackSourceMaskFixture::request(rewrite);
  }
  return loop.captureContentGeneration();
}
void exercise(uint32_t incoming, bool interrupt) {
  PlaybackSourceMaskFixture::reset(incoming);
  LoopEventStore::initPool();
  Loop loop; loop.beginCapture(CapturePhase::Record);
  for (unsigned i = 0; i < 3; ++i)
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(100+i,1,7,64)));
  RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(8));
  RuntimePlaybackViewBuild build;
  destination = &view; sourceLoop = &loop;
  const auto sourceMasksBefore = PlaybackSourceMaskFixture::events.size();
  auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  TEST_ASSERT_TRUE(PlaybackSourceMaskFixture::events.size() > sourceMasksBefore);
  TEST_ASSERT_EQUAL_UINT32(incoming,PlaybackSourceMaskFixture::primask);
  source.generation = generation;
  queryCount = interruptAt = interruptCount = interruptDepth = 0; allQueriesMasked = true;
  TEST_ASSERT_TRUE(build.begin(view,source,
      {0,7,nullptr,PlaybackWindowOrigin::Capture,source.captureContentToken},
      {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}));
  queryCount = 0; interruptAt = interrupt ? 3 : 0; visibleCounts.clear();
  PlaybackSourceMaskFixture::observeUnmasked = observe;
  const auto result = build.stepFill(8);
  PlaybackSourceMaskFixture::observeUnmasked = nullptr;
  TEST_ASSERT_TRUE(allQueriesMasked);
  TEST_ASSERT_EQUAL_UINT32(incoming,PlaybackSourceMaskFixture::primask);
  TEST_ASSERT_EQUAL_UINT32(0,PlaybackSourceMaskFixture::depth);
  if (interrupt) {
    TEST_ASSERT_EQUAL_UINT32(1,interruptCount);
    TEST_ASSERT_TRUE(interruptDepth >= 2); // Row guard encloses nested currency guard.
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Denied,result);
    TEST_ASSERT_FALSE(build.ownsLiveCaptureBorrow());
    TEST_ASSERT_EQUAL_UINT32(1,loop.capture.store.size());
    TEST_ASSERT_EQUAL_UINT32(333,loop.capture.store.at(0).tick);
    TEST_ASSERT_FALSE(view.complete || view.published);
  } else {
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Order,result);
    TEST_ASSERT_EQUAL_UINT32(3,view.storage.eventCount);
    for (unsigned i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_UINT32(100+i,view.storage.events[i].tick);
    if (incoming == 0) {
      TEST_ASSERT_TRUE(std::find(visibleCounts.begin(),visibleCounts.end(),1) != visibleCounts.end());
      TEST_ASSERT_TRUE(std::find(visibleCounts.begin(),visibleCounts.end(),2) != visibleCounts.end());
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete,build.stepOrder(64));
  }
  TEST_ASSERT_FALSE(PlaybackSourceMaskFixture::events.empty());
  build.discard(); loop.discardCapture();
  TEST_ASSERT_EQUAL_UINT32(incoming,PlaybackSourceMaskFixture::primask);
  destination = nullptr; sourceLoop = nullptr; PlaybackSourceMaskFixture::reset();
}
}
void test_live_source_masks_restore_and_private_fill_yields_between_rows() {
  LiveSourceMaskTests::exercise(0,false);
  LiveSourceMaskTests::exercise(1,false);
}
void test_live_source_row_transaction_defers_rewrite_then_refuses_stale_work() {
  LiveSourceMaskTests::exercise(0,true);
}
void test_live_source_descriptor_and_currency_refuse_post_snapshot_mutation() {
  PlaybackSourceMaskFixture::reset();
  Loop loop; loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(100,1,7,64)));
  const auto source = RuntimePlaybackViewSource::fromLiveCapture(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(101,1,7,64)));
  RuntimePlaybackView view; TEST_ASSERT_TRUE(view.prewarm(4));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_FALSE(build.begin(view,source,
      {0,7,nullptr,PlaybackWindowOrigin::Capture,source.captureContentToken},
      {0,Config::TICKS_PER_BAR,Config::TICKS_PER_BAR},{},{}));
  TEST_ASSERT_FALSE(view.complete || view.published);
  build.discard(); loop.discardCapture();
}
