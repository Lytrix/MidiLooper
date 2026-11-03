// Slice 2 (owner ruling 2026-11-03): the three-view / two-span lifecycle, structurally.
//
// N-1 serves from `current`, N waits in `ready`, N+1 is prepared in `build` -- the existing three physical views and
// the one builder (`RuntimePlaybackViewBuild`), no fourth view, no new lifecycle. These cases drive only the existing
// `RuntimePlaybackViewLifecycle` transitions and the real builder; they do not touch DEC-147 admission or the rolling
// frame (`evidence/2026-11-03-dec147-successor-feasibility-envelope.md` section 10).

namespace two_span {

using Lifecycle = RuntimePlaybackViewLifecycle;
constexpr uint8_t kNone = Lifecycle::kNoView;
constexpr uint32_t kLoop = 4 * 768;
constexpr uint32_t kSpan = 2 * 768;

void assertRolesDistinct(const Lifecycle& lc) {
  const uint8_t roles[] = {lc.current, lc.ready, lc.build, lc.idle, lc.retained};
  for (size_t i = 0; i < 5; ++i)
    for (size_t j = i + 1; j < 5; ++j)
      if (roles[i] != kNone && roles[i] == roles[j] && !(i == 0 && j == 4))  // current may be the retained view
        TEST_FAIL_MESSAGE("two roles name the same physical view");
}

/// Two notes inside `[start, start + kSpan)` of a 4-bar loop.
struct SpanSource {
  MidiEvent events[4];
  explicit SpanSource(uint32_t start, uint8_t pitch) {
    events[0] = MidiEvent::NoteOn(start + 12, 1, pitch, 100);
    events[1] = MidiEvent::NoteOff(start + 96, 1, pitch, 0);
    events[2] = MidiEvent::NoteOn(start + 780, 1, static_cast<uint8_t>(pitch + 1), 100);
    events[3] = MidiEvent::NoteOff(start + 860, 1, static_cast<uint8_t>(pitch + 1), 0);
    for (uint8_t i = 0; i < 4; ++i) events[i].noteId = static_cast<NoteId>(100 + pitch + i / 2);
  }
};

PlaybackViewSourceIdentity identityFor(uint32_t revision) { return PlaybackViewSourceIdentity{revision, 7, nullptr}; }

/// Begin a real build of `src` over `[start, start + kSpan)` into `view`.
void beginBuild(RuntimePlaybackViewBuild& build, RuntimePlaybackView& view, const SpanSource& src, uint32_t start,
                uint32_t revision) {
  TEST_ASSERT_TRUE(build.begin(view, src.events, 4, identityFor(revision), PlaybackCoverageInterval{start, kSpan, kLoop},
                               EmissionCursor{}, LedgerBoundarySummary{}));
}

void finishBuild(RuntimePlaybackViewBuild& build) {
  for (unsigned grain = 0; grain < 64 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++grain)
    (void)build.stepFill(1);
  for (unsigned grain = 0; grain < 64 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++grain)
    (void)build.stepOrder(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
}

/// Reach `current` = N-1 (published), `ready` = N (complete, unpublished), with the third view free in `build` --
/// through the lifecycle's own transitions only.
void reachTwoSpanState(RuntimePlaybackViewSet& set, uint8_t& viewNm1, uint8_t& viewN) {
  Lifecycle& lc = set.lifecycle;
  lc.reset();
  {  // N-1 built, published as ready, handed off to current
    RuntimePlaybackViewBuild build;
    beginBuild(build, set.views[lc.build], SpanSource(0, 60), 0, 1);
    finishBuild(build);
    TEST_ASSERT_TRUE(lc.publishCompletedBuildAsReadySuccessor(set.views));
    TEST_ASSERT_TRUE(lc.handoff(set.views));
  }
  viewNm1 = lc.current;
  {  // N built and published as the ready successor while N-1 serves
    RuntimePlaybackViewBuild build;
    beginBuild(build, set.views[lc.build], SpanSource(kSpan, 70), kSpan, 1);
    finishBuild(build);
    TEST_ASSERT_TRUE(lc.publishCompletedBuildAsReadySuccessor(set.views));
  }
  viewN = lc.ready;
}

}  // namespace two_span

/// Slice 2, case 1+2+3: N-1 / N / N+1 coexist on the three views; N+1 is mid-build across the N-1 -> N handoff and
/// keeps its identity, coverage and fill progress; resumed, it equals a reference build made without a handoff; once
/// published it reuses the retired N-1 view as the next build, and nothing stale is ever published.
void test_two_span_three_views_carry_n_minus_1_n_and_n_plus_1_through_a_handoff() {
  using namespace two_span;
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  Lifecycle& lc = set.lifecycle;
  uint8_t viewNm1 = kNone, viewN = kNone;
  reachTwoSpanState(set, viewNm1, viewN);

  // Coexistence: three distinct physical views, three roles, no retained marker.
  TEST_ASSERT_NOT_EQUAL(kNone, lc.build);
  TEST_ASSERT_EQUAL(kNone, lc.retained);
  assertRolesDistinct(lc);
  TEST_ASSERT_TRUE(set.views[viewNm1].published && set.views[viewNm1].complete);
  TEST_ASSERT_TRUE(set.views[viewN].complete);
  TEST_ASSERT_FALSE_MESSAGE(set.views[viewN].published, "the ready successor was published before its handoff");

  // N+1 begins in `build` and is left mid-fill.
  const uint8_t viewNp1 = lc.build;
  const SpanSource nextSource(0, 80);
  RuntimePlaybackViewBuild build;
  beginBuild(build, set.views[viewNp1], nextSource, 0, 1);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Fill, build.nextGrain());
  (void)build.stepFill(1);
  TEST_ASSERT_FALSE(set.views[viewNp1].complete);
  const uint32_t filledBefore = set.views[viewNp1].storage.eventCount;
  const uint64_t storageBefore = set.views[viewNp1].storageGeneration;
  TEST_ASSERT_GREATER_THAN_UINT32(0, filledBefore);
  // One real successor at a time: the in-progress N+1 cannot displace the complete ready N.
  TEST_ASSERT_FALSE(lc.publishCompletedBuildAsReadySuccessor(set.views));

  // The N-1 -> N handoff.
  TEST_ASSERT_TRUE(lc.handoff(set.views));
  TEST_ASSERT_EQUAL(viewN, lc.current);
  TEST_ASSERT_TRUE(set.views[viewN].published);
  TEST_ASSERT_EQUAL_MESSAGE(viewNp1, lc.build, "the in-progress N+1 lost its build role at the handoff");
  TEST_ASSERT_FALSE_MESSAGE(set.views[viewNm1].published, "the retired N-1 is still published");
  assertRolesDistinct(lc);
  // N+1's identity and progress are intact.
  TEST_ASSERT_TRUE(set.views[viewNp1].source == identityFor(1));
  TEST_ASSERT_EQUAL_UINT32(0, set.views[viewNp1].coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(kSpan, set.views[viewNp1].coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(filledBefore, set.views[viewNp1].storage.eventCount);
  TEST_ASSERT_EQUAL_UINT64(storageBefore, set.views[viewNp1].storageGeneration);

  // Resumed to completion it equals a reference build of the same source made without any handoff.
  finishBuild(build);
  LoopPlaybackRuntime reference;
  TEST_ASSERT_TRUE(reference.prewarmViews());
  RuntimePlaybackView& refView = reference.viewSetIfPrewarmed()->views[0];
  RuntimePlaybackViewBuild refBuild;
  beginBuild(refBuild, refView, nextSource, 0, 1);
  finishBuild(refBuild);
  const RuntimePlaybackView& got = set.views[viewNp1];
  TEST_ASSERT_EQUAL_UINT32(refView.storage.eventCount, got.storage.eventCount);
  for (uint32_t i = 0; i < got.storage.eventCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(refView.storage.order[i], got.storage.order[i]);
    const MidiEvent& a = refView.storage.events[refView.storage.order[i]];
    const MidiEvent& b = got.storage.events[got.storage.order[i]];
    TEST_ASSERT_EQUAL_UINT32(a.tick, b.tick);
    TEST_ASSERT_EQUAL_UINT8(a.data.noteData.note, b.data.noteData.note);
    TEST_ASSERT_EQUAL(a.isNoteOn(), b.isNoteOn());
  }

  // Published as the ready successor, it reuses the retired N-1 view as the next build -- reset, never published.
  TEST_ASSERT_TRUE(lc.publishCompletedBuildAsReadySuccessor(set.views));
  TEST_ASSERT_EQUAL(viewNp1, lc.ready);
  TEST_ASSERT_FALSE(set.views[viewNp1].published);
  TEST_ASSERT_EQUAL_MESSAGE(viewNm1, lc.build, "the retired predecessor was not reused as the next build");
  TEST_ASSERT_FALSE(set.views[viewNm1].complete);
  TEST_ASSERT_FALSE(set.views[viewNm1].published);
  assertRolesDistinct(lc);
}

/// Slice 2, guard: a legitimately retained Record return view keeps its storage. With `current`, `ready` and a
/// complete `retained` view occupying all three, there is no view for a second lookahead; the handoff and the next
/// publication must never reset or recycle the retained view, and its marker survives. Non-vacuous: the retained view
/// is asserted complete with real content before and after.
void test_two_span_never_steals_a_legitimately_retained_view() {
  using namespace two_span;
  LoopPlaybackRuntime runtime;
  TEST_ASSERT_TRUE(runtime.prewarmViews());
  RuntimePlaybackViewSet& set = *runtime.viewSetIfPrewarmed();
  Lifecycle& lc = set.lifecycle;
  uint8_t viewNm1 = kNone, viewN = kNone;
  reachTwoSpanState(set, viewNm1, viewN);
  // The third view becomes a complete retained return window (its own role, not current).
  const uint8_t retainedView = lc.build;
  {
    RuntimePlaybackViewBuild build;
    beginBuild(build, set.views[retainedView], SpanSource(0, 90), 0, 1);
    finishBuild(build);
  }
  lc.build = kNone;
  lc.retained = retainedView;
  assertRolesDistinct(lc);
  const uint32_t retainedEvents = set.views[retainedView].storage.eventCount;
  TEST_ASSERT_GREATER_THAN_UINT32(0, retainedEvents);
  TEST_ASSERT_TRUE(set.views[retainedView].complete);

  // No free view: the second lookahead has nowhere to build.
  TEST_ASSERT_EQUAL_MESSAGE(kNone, lc.build, "a second lookahead view exists while a valid retained view is held");
  TEST_ASSERT_FALSE(lc.releaseOrphanedRetained(set.views));  // complete: never an orphan

  TEST_ASSERT_TRUE(lc.handoff(set.views));
  TEST_ASSERT_EQUAL(retainedView, lc.retained);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(retainedView, lc.build, "the handoff recycled the retained view as a build");
  TEST_ASSERT_TRUE(set.views[retainedView].complete);
  TEST_ASSERT_EQUAL_UINT32(retainedEvents, set.views[retainedView].storage.eventCount);
  assertRolesDistinct(lc);
}

void registerTwoSpanLifecycleCases() {
  RUN_TEST(test_two_span_three_views_carry_n_minus_1_n_and_n_plus_1_through_a_handoff);
  RUN_TEST(test_two_span_never_steals_a_legitimately_retained_view);
}
