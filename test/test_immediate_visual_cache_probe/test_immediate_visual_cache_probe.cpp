// P1 / 1.6 — visual-source-miss measurement only; no firmware substitutions.
// Borrow the real display/storage/Track closure and its physical I/O boundaries.
#define main borrowed_live_capture_display_main
#include "../test_live_capture_display_boundary/test_live_capture_display_boundary.cpp"
#undef main
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include <cstdlib>
#include <cstring>

namespace ImmediateVisualProbe {
constexpr uint32_t bars = 33;
constexpr uint32_t density = 8;
constexpr uint32_t overdubs = 16;
constexpr uint32_t barTicks = Config::TICKS_PER_BAR;
constexpr uint32_t length = bars * barTicks;
struct Expected { NoteId id; uint8_t pitch; uint32_t on, off; };
static std::vector<Expected> expected;

static void appendPair(LoopEventStore& store, NoteId id, uint8_t pitch,
                       uint32_t on, uint32_t off) {
  MidiEvent event = MidiEvent::NoteOn(on, 1, pitch, 100);
  event.noteId = id;
  TEST_ASSERT_TRUE(store.append(event));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(off, 1, pitch, 0)));
}
static EditPass lengthRow(NoteId id, uint32_t on, uint32_t off) {
  EditPass row{};
  row.id = 900;
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.state = EditPassState::Active;
  row.targetNoteId = id;
  row.startTick = on;
  row.endTick = off;
  return row;
}
// Explicit seeded inventory, independent of resolveDisplayNotes / merged MIDI pairing.
static Loop& seedBackground() {
  LoopContentResolution::deviceGateReset();
  LoopContentResolution::boundedDisplayCancel();
  LoopEventStore::initPool();
  Track& background = trackManager.getTrack(1);
  background.ensureLoopsAllocated();
  Loop& loop = background.getActiveLoop();
  expected.clear();
  LoopEventStore record;
  for (uint32_t b = 0; b < bars; ++b) {
    for (uint32_t n = 0; n < density; ++n) {
      const NoteId id = 1000 + b * density + n;
      const uint8_t pitch = 60 + n;
      const uint32_t on = b * barTicks + n * 64 + 12;
      const uint32_t off = on + (n == 0 ? 88 : 32);
      appendPair(record, id, pitch, on, off);
      if (id != 1001) expected.push_back({id, pitch, on, id == 1000 ? 40u : off});
    }
  }
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = length;
  for (uint32_t p = 0; p < overdubs; ++p) {
    LoopEventStore store;
    for (uint32_t b = 0; b < bars; ++b) {
      // Pass 0 overlaps the Record's pitch-60 note. A merged LIFO resolver
      // would change its endpoint; pass-local identity must survive.
      const uint32_t on = b * barTicks + 24 + p * 4;
      const uint32_t off = on + 20;
      const NoteId id = 10000 + p * bars + b;
      appendPair(store, id, 60, on, off);
      expected.push_back({id, 60, on, off});
    }
    OverdubPass pass{};
    pass.id = 100 + p;
    pass.mergeSequence = p + 1;
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, pass.committedChunkIds));
    loop.passes.overdubPasses.push_back(std::move(pass));
  }
  loop.passes.editPasses.push_back(lengthRow(1000, 12, 40));
  EditPass hide = lengthRow(1001, 76, 108);
  hide.id = 901;
  hide.actionType = EditActionType::Delete;
  loop.passes.editPasses.push_back(hide);
  loop.markDisplayCachesStale();
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  TEST_ASSERT_FALSE(loop.isPlaybackSessionExitPublicationGap());
  TEST_ASSERT_EQUAL_UINT32(17, loop.passes.capturePassCount());
  TEST_ASSERT_EQUAL_UINT32(791, expected.size());
  return loop;
}
static void assertInterval(const DisplayNoteVec& notes, uint32_t first, uint32_t count) {
  const uint32_t lo = first * barTicks, hi = (first + count) * barTicks;
  size_t total = 0;
  for (const Expected& e : expected) {
    if (e.on >= hi || e.off <= lo) continue;
    ++total;
    assertNote(notes, e.id, e.pitch, e.on, e.off);
    for (const auto& note : notes) if (note.noteId == e.id) {
      TEST_ASSERT_EQUAL_UINT8(100, note.velocity);
    }
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(total, notes.size(), "no extra, hidden or cross-pass-paired notes");
}
// ---------------------------------------------------------------------------------------------
// C5.2a/P-D producer driving. `turns` counts production turns, `maxUnits` the largest single-step
// touched-row count observed. Counts, not wall time: a step may never touch a content-sized number
// of rows.
static uint32_t producerTurns = 0;
static uint32_t producerMaxUnits = 0;
static uint32_t producerFirstPublishTurn = 0;

static void resetProducerCounters() {
  producerTurns = 0;
  producerMaxUnits = 0;
  producerFirstPublishTurn = 0;
}

/// One production turn on the derived-view lane: the bounded continuation steps once, then the
/// slice observes it. Asserts the declared per-turn unit bound on every step.
static bool productionTurn(Loop& loop, uint32_t b, bool append = true, size_t budget = 1) {
  const uint32_t revisionBefore = loop.visualCache.revision;
  (void)loop.stepVisualCacheSourceContinuation();
  const LoopContentResolution::BoundedDisplaySliceRecord last =
      LoopContentResolution::boundedDisplayLastSlice();
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(
      LoopContentResolution::kBoundedDisplayRowsPerSlice, last.workUnits);
  if (last.workUnits > producerMaxUnits) {
    producerMaxUnits = last.workUnits;
  }
  ++producerTurns;
  loop.rebuildVisualCacheIdleSlice(1, b, 0, append, true, budget);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Loop::VisualCacheIdleGatherSource::PassesResolve,
                                loop.lastVisualCacheIdleGatherSource(),
                                "above the declared cap the slice must never whole-pass resolve");
  TEST_ASSERT_FALSE(loop.lastVisualCacheIdleCopiedSnapshotNotes());
  const bool published = loop.visualCache.revision != revisionBefore;
  if (published && producerFirstPublishTurn == 0) {
    producerFirstPublishTurn = producerTurns;
  }
  return published;
}

/// Drives bounded production turns until `b`'s window publishes, retaining the previous frame.
static void slice(Loop& loop, uint32_t b, bool append = true, size_t budget = 1,
                  uint32_t maxTurns = 40000) {
  for (uint32_t turn = 0; turn < maxTurns; ++turn) {
    if (productionTurn(loop, b, append, budget)) {
      return;
    }
  }
  TEST_FAIL_MESSAGE("bounded continuation did not publish the window within its turn budget");
}

/// One producer grain without a slice turn: no publication can happen on this turn, so a retained
/// frame can be observed while the continuation is mid-flight.
static void producerGrainOnly(Loop& loop) {
  (void)loop.stepVisualCacheSourceContinuation();
  const LoopContentResolution::BoundedDisplaySliceRecord last =
      LoopContentResolution::boundedDisplayLastSlice();
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(
      LoopContentResolution::kBoundedDisplayRowsPerSlice, last.workUnits);
  if (last.workUnits > producerMaxUnits) {
    producerMaxUnits = last.workUnits;
  }
  ++producerTurns;
}
static void characterize(Loop& loop) {
  // This is a SEPARATE invocation of the exact production resolver with its
  // existing counter argument. The real idle call passes nullptr; these are
  // NOT instrumented counts from that call, nor total scan/pair/retire work.
  ResolutionCostCounters costs{};
  DisplayNoteVec full;
  LoopContentResolution::resolveDisplayNotes(loop.passes, length, full, &costs);
  assertInterval(full, 0, bars);
  TEST_ASSERT_EQUAL_UINT32(808, costs.resolutionOperations); // 791 output + 17 layers
  std::printf("VISUAL owner=track1 slot0 source=PassesResolve bars=33 passes=17 "
              "input_events=1584 full_notes=%zu separate_resolver_ops=%u "
              "idle_call_counter=UNAVAILABLE requested_bars=1 fallback_budget=1\n",
              full.size(), costs.resolutionOperations);
}
static void test_current_characterization_background_miss_exact_content() {
  Track& recording = beginRecord();
  const NoteId input = acceptPair(recording, 90, 4, 10);
  Loop& loop = seedBackground();
  characterize(loop);
  const uint32_t revision = loop.visualCache.revision;
  slice(loop, 0);
  assertInterval(loop.visualCache.notes, 0, 1);
  TEST_ASSERT_EQUAL_UINT32(revision + 1, loop.visualCache.revision);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT8(1, loop.visualCache.dirtyBars[1]);
  // Ordinary visual turns complete exact content; not a bounded-source producer proof.
  for (uint32_t b = 1; b < bars; ++b) slice(loop, b);
  assertInterval(loop.visualCache.notes, 0, bars);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(recording.isRecording());
  TEST_ASSERT_EQUAL_UINT32(2, recording.getActiveLoop().capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(input, recording.getActiveLoop().capture.store.at(0).noteId);
  const NoteId next = acceptPair(recording, 91, 20, 30);
  TEST_ASSERT_NOT_EQUAL(input, next);
  TEST_ASSERT_EQUAL_UINT32(next, recording.getActiveLoop().capture.store.at(2).noteId);
}
static void test_current_characterization_revision_supersession_between_slices() {
  Track& recording = beginRecord();
  const NoteId input = acceptPair(recording, 90, 4, 10);
  Loop& loop = seedBackground();
  slice(loop, 0, false); // exercises deferred wrap append, not a private resolver cursor
  // Disable a committed pass and replace an effective Length while slices remain dirty.
  loop.passes.overdubPasses[0].state = CapturePassState::Disabled;
  expected.erase(std::remove_if(expected.begin(), expected.end(), [](const Expected& e) {
    return e.id >= 10000 && e.id < 10000 + bars;
  }), expected.end());
  loop.passes.editPasses[0].endTick = 28;
  for (Expected& e : expected) if (e.id == 1000) e.off = 28;
  ++loop.playbackRevision; // controlled committed-source revision boundary
  loop.markDisplayCachesStale();
  for (uint32_t b = 0; b < bars; ++b) slice(loop, b);
  loop.applyDeferredVisualCacheWrapAppend();
  assertInterval(loop.visualCache.notes, 0, bars);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(2, recording.getActiveLoop().capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(input, recording.getActiveLoop().capture.store.at(0).noteId);
  TEST_ASSERT_TRUE(recording.isRecording());
}
static void test_desired_bound_source_miss_must_not_resolve_whole_passes() {
  beginRecord();
  Loop& loop = seedBackground();
  characterize(loop);
  resetProducerCounters();
  const uint32_t revision = loop.visualCache.revision;
  // P-D-1: ONE real-time slice on a bounded-budget miss. Above the declared cap it must not select
  // the whole-pass route; it defers by name and the previous frame stays authoritative.
  loop.rebuildVisualCacheIdleSlice(1, 0, 0, true, true, 1);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Loop::VisualCacheIdleGatherSource::PassesResolve,
                                loop.lastVisualCacheIdleGatherSource(),
                                "a source miss above the declared cap must not whole-pass resolve");
  TEST_ASSERT_EQUAL_MESSAGE(Loop::VisualCacheIdleGatherSource::BoundedContinuation,
                            loop.lastVisualCacheIdleGatherSource(),
                            "the bounded continuation is the progress for this key");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, loop.visualCache.notes.size(),
                                   "the previous (empty) frame is retained until exact replacement");
  TEST_ASSERT_EQUAL_UINT32(revision, loop.visualCache.revision);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  // The declared cap forces the producer: 1,584 committed rows > 512.
  TEST_ASSERT_TRUE(LoopContentResolution::boundedDisplayNeedsProducer(loop.passes));
  TEST_ASSERT_GREATER_THAN_UINT32(512,
      LoopContentResolution::boundedDisplayCountCommittedEvents(loop.passes, 512));
  // P-D-3: bounded production turns reach the exact first-bar inventory.
  slice(loop, 0);
  assertInterval(loop.visualCache.notes, 0, 1);
  TEST_ASSERT_EQUAL_UINT32(revision + 1, loop.visualCache.revision);
  TEST_ASSERT_EQUAL_MESSAGE(Loop::VisualCacheIdleGatherSource::BoundedContinuation,
                            loop.lastVisualCacheIdleGatherSource(),
                            "the publication names the bounded source");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(LoopContentResolution::kBoundedDisplayRowsPerSlice,
                                   producerMaxUnits);
}

// P-D-2/P-D-3: precision of each step, no whole-pass operation, exact completion.
static void test_bounded_continuation_has_no_content_sized_step() {
  beginRecord();
  Loop& loop = seedBackground();
  resetProducerCounters();
  slice(loop, 0);
  // Every production step reported at most the declared row budget.
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(LoopContentResolution::kBoundedDisplayRowsPerSlice,
                                   producerMaxUnits);
  // A whole-pass sort/copy would finish in a handful of turns: 1,584 input rows at 8 rows per turn
  // cannot. This is a row count, not a wall-clock threshold.
  TEST_ASSERT_GREATER_THAN_UINT32(300, producerFirstPublishTurn);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(4000, producerFirstPublishTurn);
  const LoopContentResolution::BoundedDisplaySliceRecord last =
      LoopContentResolution::boundedDisplayLastSlice();
  TEST_ASSERT_EQUAL_STRING("ready", last.step);
  std::printf("CONTINUATION turns=%u max_units=%u first_publish_turn=%u rows=%u\n",
              producerTurns, producerMaxUnits, producerFirstPublishTurn,
              LoopContentResolution::boundedDisplayCountCommittedEvents(loop.passes, 0xFFFFFFFFu));
}

static void test_bounded_continuation_publishes_exact_completion() {
  Track& recording = beginRecord();
  const NoteId input = acceptPair(recording, 90, 4, 10);
  Loop& loop = seedBackground();
  resetProducerCounters();
  for (uint32_t b = 0; b < bars; ++b) {
    slice(loop, b);
    // Coherent at every observed frame: only exact per-window content is ever published.
    assertInterval(loop.visualCache.notes, 0, b + 1);
  }
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(LoopContentResolution::kBoundedDisplayRowsPerSlice,
                                   producerMaxUnits);
  // Exactly-once publication: a repeated turn after completion changes nothing.
  const uint32_t revision = loop.visualCache.revision;
  loop.rebuildVisualCacheIdleSlice(1, 0, 0, true, true, 1);
  TEST_ASSERT_EQUAL_UINT32(revision, loop.visualCache.revision);
  TEST_ASSERT_EQUAL_UINT32(791, loop.visualCache.notes.size());
  TEST_ASSERT_TRUE(recording.isRecording());
  TEST_ASSERT_EQUAL_UINT32(2, recording.getActiveLoop().capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(input, recording.getActiveLoop().capture.store.at(0).noteId);
}

// P-D-4: a revision change mid-continuation discards the partial candidate, publishes nothing
// obsolete, and keeps the qualified frame until the new content is exact.
static void test_bounded_continuation_supersession_never_publishes_obsolete() {
  beginRecord();
  Loop& loop = seedBackground();
  resetProducerCounters();
  // A qualified frame at the current revision.
  slice(loop, 0);
  assertInterval(loop.visualCache.notes, 0, 1);
  const DisplayNoteVec retained = loop.visualCache.notes;
  const uint32_t retainedRevision = loop.visualCache.revision;
  // Start a continuation for the SAME key and let it run mid-flight without a slice turn.
  producerGrainOnly(loop);
  for (uint32_t turn = 0; turn < 60; ++turn) {
    producerGrainOnly(loop);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedRevision, loop.visualCache.revision,
                                   "a mid-flight continuation cannot publish without its slice");
  // Supersede it mid-flight: new content identity and a new revision.
  loop.passes.overdubPasses[0].state = CapturePassState::Disabled;
  expected.erase(std::remove_if(expected.begin(), expected.end(), [](const Expected& e) {
    return e.id >= 10000 && e.id < 10000 + bars;
  }), expected.end());
  loop.passes.editPasses[0].endTick = 28;
  for (Expected& e : expected) if (e.id == 1000) e.off = 28;
  ++loop.playbackRevision;
  loop.markDisplayCachesStale();
  // The obsolete candidate cannot publish: the qualified frame survives the new key's progress.
  for (uint32_t turn = 0; turn < 60; ++turn) {
    producerGrainOnly(loop);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedRevision, loop.visualCache.revision,
                                     "a superseded continuation must not publish");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(retained.size(), loop.visualCache.notes.size(),
                                     "frame size must be the retained frame's");
    for (size_t i = 0; i < retained.size(); ++i) {
      TEST_ASSERT_EQUAL_UINT32(retained[i].noteId, loop.visualCache.notes[i].noteId);
      TEST_ASSERT_EQUAL_UINT32(retained[i].startTick, loop.visualCache.notes[i].startTick);
      TEST_ASSERT_EQUAL_UINT32(retained[i].endTick, loop.visualCache.notes[i].endTick);
    }
  }
  // Drain: the new revision's exact content only, never the disabled pass's rows.
  for (uint32_t b = 0; b < bars; ++b) slice(loop, b);
  assertInterval(loop.visualCache.notes, 0, bars);
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    TEST_ASSERT_TRUE_MESSAGE(note.noteId < 10000 || note.noteId >= 10000 + bars,
                             "disabled pass rows must not be published");
  }
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
}

// The display asks for a *different* interval every visual turn (the playhead advances one bar per
// turn) while the same bounded continuation is still in flight. What the cache publishes is
// measured, not assumed: the requested interval, the published frame and its interval bounds, the
// retained/fresh split, and whether the candidate was re-keyed (discarded) on each request.
static uint32_t publishedFirstBar(const DisplayNoteVec& notes) {
  uint32_t first = UINT32_MAX;
  for (const NoteUtils::DisplayNote& note : notes) {
    const uint32_t bar = visualBarForTick(note.startTick, Config::TICKS_PER_BAR);
    if (bar < first) first = bar;
  }
  return first;
}
static uint32_t publishedLastBar(const DisplayNoteVec& notes) {
  uint32_t last = 0;
  for (const NoteUtils::DisplayNote& note : notes) {
    const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
    const uint32_t bar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    if (bar > last) last = bar;
  }
  return last;
}
static LoopContentResolution::BoundedDisplayRequest boundedRequestFor(const Loop& loop) {
  LoopContentResolution::BoundedDisplayRequest request;
  request.owner = &loop;
  request.loopId = loop.loopId;
  request.loopLengthTicks = loop.loopLengthTicks;
  request.playbackRevision = loop.playbackRevision;
  return request;
}

static void test_moving_viewport_while_bounded_continuation_is_in_flight() {
  beginRecord();
  Loop& loop = seedBackground();
  resetProducerCounters();
  const uint32_t totalBars =
      (loop.loopLengthTicks + Config::TICKS_PER_BAR - 1u) / Config::TICKS_PER_BAR;
  TEST_ASSERT_EQUAL_UINT32(bars, totalBars);
  const LoopContentResolution::BoundedDisplayRequest request = boundedRequestFor(loop);
  TEST_ASSERT_TRUE(LoopContentResolution::boundedDisplayNeedsProducer(loop.passes));
  const uint32_t retainedRevision = loop.visualCache.revision;
  uint32_t turns = 0, requestedAdvances = 0, publishedTurns = 0, retainedTurns = 0, rekeys = 0,
           skippedTurns = 0;
  uint32_t firstPublishedTurn = 0, firstPublishedBar = UINT32_MAX, lastRequestedBar = UINT32_MAX;
  uint32_t flightTurns = 0, flightPublishedTurns = 0, flightRekeys = 0;
  bool sawProgress = false, inFlight = true;
  for (uint32_t turn = 0; turn < 4000; ++turn) {
    // The requested interval advances every turn: the priority bar *is* the viewport this turn.
    const uint32_t viewportBar = turn % totalBars;
    if (viewportBar != lastRequestedBar) {
      lastRequestedBar = viewportBar;
      ++requestedAdvances;
    }
    const uint32_t revisionBefore = loop.visualCache.revision;
    (void)loop.stepVisualCacheSourceContinuation();
    const LoopContentResolution::BoundedDisplaySliceRecord last =
        LoopContentResolution::boundedDisplayLastSlice();
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(LoopContentResolution::kBoundedDisplayRowsPerSlice,
                                     last.workUnits);
    if (std::strcmp(last.step, "order") == 0) {
      // The producer's key is (loop instance, loopId, loop length, playbackRevision, pass count,
      // row count): a request that reaches "order" again after progress discarded the candidate.
      if (sawProgress) {
        ++rekeys;
        if (inFlight) ++flightRekeys;
      }
    } else if (std::strcmp(last.step, "denied") == 0) {
      TEST_FAIL_MESSAGE("the continuation was denied its workspace, so this turn cannot be measured");
    } else {
      sawProgress = true;
    }
    const bool dirtyBefore = loop.visualCache.dirtyBars[viewportBar] != 0;
    loop.rebuildVisualCacheIdleSlice(1, viewportBar, 0, true, true, 1);
    ++turns;
    if (inFlight && !dirtyBefore) ++skippedTurns;
    if (loop.visualCache.revision != revisionBefore) {
      ++publishedTurns;
      if (inFlight) ++flightPublishedTurns;
      if (firstPublishedTurn == 0) {
        firstPublishedTurn = turns;
        firstPublishedBar = publishedFirstBar(loop.visualCache.notes);
      }
    } else {
      ++retainedTurns;
      if (inFlight) ++flightTurns;
    }
    if (LoopContentResolution::boundedDisplayReady(request, loop.passes)) {
      inFlight = false;
      break;
    }
  }
  TEST_ASSERT_FALSE_MESSAGE(
      inFlight, "the continuation never completed, so the moving viewport has no target");
  std::printf("MOVING_VIEWPORT turns=%u requested_advances=%u published_turns=%u retained_turns=%u "
              "rekeys=%u skipped=%u first_publish_turn=%u first_publish_pub_bar=%u "
              "flight_turns=%u flight_published=%u flight_rekeys=%u last_requested_bar=%u "
              "retained_revision=%u final_revision=%u notes=%u pub_bar_lo=%u pub_bar_hi=%u\n",
              turns, requestedAdvances, publishedTurns, retainedTurns, rekeys, skippedTurns,
              firstPublishedTurn, firstPublishedBar, flightTurns, flightPublishedTurns, flightRekeys,
              lastRequestedBar, retainedRevision, loop.visualCache.revision,
              static_cast<unsigned>(loop.visualCache.notes.size()),
              publishedFirstBar(loop.visualCache.notes), publishedLastBar(loop.visualCache.notes));
  // The requested interval moved on every turn, and the continuation's candidate was never discarded for it:
  // the producer's key carries (loop instance, loopId, loop length, playbackRevision, pass count, row count) and
  // no viewport, so "each request supersedes the candidate" is FALSIFIED by this counter (rekeys 0).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(turns, requestedAdvances,
                                   "the moving viewport must request a new interval every turn");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rekeys,
                                   "a moved viewport must not discard the bounded candidate");
  // What IS reproduced: publication is retained for the whole continuation -- one exact frame, on the completing
  // turn, for the bar the viewport asked for; every earlier turn keeps the previous frame.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, publishedTurns,
                                   "exactly one frame may publish across the whole continuation");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(turns - 1, retainedTurns,
                                   "every turn before completion must retain the previous frame");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(turns, firstPublishedTurn,
                                   "the single publication lands on the completing turn");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(lastRequestedBar, firstPublishedBar,
                                   "the published frame is the interval the viewport asked for, not a stale one");
  // Exactness is preserved: the one published frame is that bar's complete pass-local inventory, not a partial
  // window assembled from the retained frame.
  assertInterval(loop.visualCache.notes, firstPublishedBar, 1);
}

/// A helper for the composition oracle: drive the existing prepared-index owner to completion so
/// `tryResolvePreparedWindow` is a *qualified* provider for the loop's current generation. No new reader --
/// this is the same `deviceGate*` session the idle slice uses.
static void prepareQualifiedCommittedSource(const Loop& loop) {
  LoopContentResolution::deviceGateReset();
  LoopContentResolution::deviceGateBegin(loop.loopLengthTicks, 0, 0);
  for (unsigned turn = 0; turn < 200000; ++turn) {
    if (LoopContentResolution::deviceGateRunOneSlice(loop.passes, loop.loopLengthTicks) ==
        LoopContentResolution::DeviceGateSliceResult::Complete) {
      break;
    }
    if (!LoopContentResolution::deviceGateActive()) {
      break;
    }
  }
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
}

static bool noteInRangePresent(const DisplayNoteVec& notes, uint32_t lo, uint32_t hi, uint8_t pitch) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick >= lo && note.startTick < hi) {
      return true;
    }
  }
  return false;
}

static uint32_t countNotesAtPitchStart(const DisplayNoteVec& notes, uint8_t pitch, uint32_t startTick) {
  uint32_t count = 0;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) ++count;
  }
  return count;
}

// C5.6 — the composition contract on the handoff's geometry: a 40-bar take with a 16-bar window. The
// retained frame is the recording-era frame over bars 8..24 ([6144, 18432)); the request moves to bars
// 24..40 ([18432, 30720)), the clamped maximum, wholly outside that coverage. A retained frame is
// authoritative only over the coverage and generation it recorded, so the requested interval may be
// served only by a source independently qualified for it, and the two are never stitched together:
//   (a) the retained frame contributes ZERO rows to a frame for the requested interval;
//   (b) a request no source is qualified for stays empty rather than invented (the honest hole), and
//       the content key does not advance for the move alone;
//   (c) once a provider is qualified for the interval, its committed rows are drawn and the frame is
//       claimed for the interval it was served for;
//   (d) the fresh overdub row reaches the frame through the live-overlay origin exactly once;
//   (e) the viewport start and length equal the request on every turn.
static void test_request_outside_retained_coverage_is_served_by_a_qualified_source() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 40 * bar;                 // 30720
  constexpr uint32_t windowBars = 16;
  constexpr uint32_t windowLength = windowBars * bar;   // 12288
  constexpr uint32_t retainedStart = 8 * bar;           // bars 8..24
  constexpr uint32_t requestStart = 24 * bar;           // bars 24..40 = length - windowLength
  TEST_ASSERT_EQUAL_UINT32(length - windowLength, requestStart);
  Track& t = beginRecord();
  DisplayCaptureTestAccess::setDetailedWindowBars(0, windowBars);
  // bar 10: inside the *retained* coverage only. Its appearance in a frame for the request is a stitch (a).
  (void)acceptPair(t, 61, 10 * bar + 20, 10 * bar + 80);
  // bars 26, 30 and 34: committed content the requested interval must draw once a source qualifies (c).
  const NoteId committedA = acceptPair(t, 64, 26 * bar + 20, 26 * bar + 80);
  const NoteId committedB = acceptPair(t, 66, 30 * bar + 500, 30 * bar + 640);
  const NoteId committedC = acceptPair(t, 68, 34 * bar + 100, 34 * bar + 200);
  // bar 39: the take's own length anchor. A record stop binds its length to
  // min(quantized transport length, content length), so the last accepted event must sit inside the
  // final bar for the take to keep the 40 bars this geometry needs.
  const NoteId committedD = acceptPair(t, 72, 39 * bar + 300, 39 * bar + 380);
  growRecordGeometry(t, length);
  // The frame the handoff retains: the growing take's window ending at tick 24 bars -> coverage bars 8..24.
  submitFrame(24 * bar);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedStart, submittedFrame.source.windowStart,
                                   "the retained frame must be claimed for bars 8..24");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, submittedFrame.source.windowLength,
                                   "the retained frame must be claimed for a 16-bar interval");
  TEST_ASSERT_TRUE_MESSAGE(noteInRangePresent(submittedFrame.notes, retainedStart, requestStart, 61),
                           "fixture must retain the bar-10 row inside that coverage");
  completeFrame();
  t.stopRecording(length);
  TEST_ASSERT_EQUAL_UINT32(length, t.getActiveLoop().loopLengthTicks);
  for (unsigned turn = 0; turn < 4096 && !t.getActiveLoop().hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  Loop& loop = t.getActiveLoop();
  TEST_ASSERT_TRUE_MESSAGE(loop.isPlaybackSessionExitPublicationGap(),
                           "fixture must hold the content for a pending publication");
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  clockManager.assignCurrentTickSilently(length + 128);
  t.startOverdubbing(length + 128);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  // The fresh overdub row, inside the requested interval (bar 35).
  (void)acceptPair(t, 70, length + 32 * bar + 3000, length + 32 * bar + 3048);

  // Turn 1 — request bars 24..40 while nothing is independently qualified for it. The retained frame is
  // not authoritative there, so the interval stays empty; nothing is invented, and the content key keeps
  // the retained coverage (the move alone changes no content identity).
  submitFrame(length + 32 * bar);  // playhead 32 bars -> start 24 bars (the clamped maximum)
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(requestStart, DisplayCaptureTestAccess::requestedViewStart(),
                                   "(e) the request follows the playhead");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, DisplayCaptureTestAccess::requestedViewLength(),
                                   "(e) the requested length is the zoom");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(requestStart, DisplayCaptureTestAccess::drawnViewStart(),
                                   "(e) the drawn viewport must be the requested interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, DisplayCaptureTestAccess::drawnViewLength(),
                                   "(e) the drawn viewport length must be the requested length");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedStart, submittedFrame.source.windowStart,
                                   "(b) the content key must not advance for the move alone");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, submittedFrame.source.windowLength,
                                   "(b) the retained coverage length is unchanged");
  TEST_ASSERT_FALSE_MESSAGE(noteInRangePresent(submittedFrame.notes, requestStart, length, 64),
                            "(b) no committed row may be invented without a qualified source");
  TEST_ASSERT_FALSE_MESSAGE(noteInRangePresent(submittedFrame.notes, requestStart, length, 66),
                            "(b) no committed row may be invented without a qualified source");
  TEST_ASSERT_FALSE_MESSAGE(noteInRangePresent(submittedFrame.notes, requestStart, length, 68),
                            "(b) no committed row may be invented without a qualified source");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, submittedFrame.pianoRows,
                                   "(b) an interval without a qualified source stays empty");
  const uint32_t holeRows = submittedFrame.pianoRows;
  completeFrame();

  // Turn 2 — the recorded take's committed content is made a qualified provider for this generation.
  prepareQualifiedCommittedSource(loop);
  TEST_ASSERT_TRUE_MESSAGE(LoopContentResolution::preparedWindowReady(loop.playbackRevision),
                           "fixture must provide a qualified committed source for the current generation");
  submitFrame(length + 32 * bar);
  // (a)+(c): one generation, one base — the three committed rows of the requested interval are drawn.
  TEST_ASSERT_TRUE_MESSAGE(noteInRangePresent(submittedFrame.notes, requestStart, length, 64),
                           "(c) committed notes inside the request must be drawn");
  assertNote(submittedFrame.notes, committedA, 64, 26 * bar + 20, 26 * bar + 80);
  assertNote(submittedFrame.notes, committedB, 66, 30 * bar + 500, 30 * bar + 640);
  assertNote(submittedFrame.notes, committedC, 68, 34 * bar + 100, 34 * bar + 200);
  assertNote(submittedFrame.notes, committedD, 72, 39 * bar + 300, 39 * bar + 380);
  TEST_ASSERT_FALSE_MESSAGE(noteInRangePresent(submittedFrame.notes, 0, requestStart, 61),
                            "(a) the retained frame must contribute zero rows to the requested interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1, countNotesAtPitchStart(submittedFrame.notes, 70, 32 * bar + 3000),
      "(d) the fresh overdub row must reach the frame exactly once");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, submittedFrame.notes.size(),
                                   "(a) four committed rows and the live row, never a stitched frame");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, submittedFrame.pianoRows,
                                   "(c) every row of the served interval is drawn exactly once");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("prepared", DisplayCaptureTestAccess::qualifiedSourceName(loop),
                                   "(c) the frame names the source qualified for its interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(requestStart, submittedFrame.source.windowStart,
                                   "(c) the frame is claimed for the interval it was served for");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, submittedFrame.source.windowLength,
                                   "(c) the claimed length is the requested length");
  const uint32_t servedRows = submittedFrame.pianoRows;
  const uint32_t servedContent = static_cast<uint32_t>(submittedFrame.notes.size());
  completeFrame();
  std::printf("COMPOSITION retained=%u,%u request=%u,%u hole_rows=%u served_rows=%u "
              "served_content=%u provider=%s\n",
              retainedStart, windowLength, requestStart, windowLength, holeRows, servedRows,
              servedContent, DisplayCaptureTestAccess::qualifiedSourceName(loop));
}


// The split's oracle (`DIAG,disp_turn` on hardware): while a **content hold** is active — a pending session-exit
// publication — the interval the frame is *drawn* in must follow the playhead on every turn, while the *content*
// stays the retained frame's qualified interval and the content key is never advanced. The drawn rows are exactly
// the retained qualified rows that fall inside the requested viewport: nothing is gathered, mixed or re-claimed,
// so a note the viewport covers but the retained frame never qualified is honestly absent rather than invented.
static void test_hold_keeps_retained_content_but_draws_the_requested_viewport() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 24 * bar;  // the take's own length; a 16-bar window cannot cover it
  constexpr uint32_t windowLength = 16 * bar;
  const uint32_t retainedContentStart = length - 16 - windowLength;  // clamped end-of-take window
  Track& t = beginRecord();
  // Outside the retained interval but inside the first requested viewport: must stay absent (never invented).
  const NoteId outside = acceptPair(t, 59, 3 * bar + 20, 3 * bar + 80);
  (void)outside;
  const NoteId r1 = acceptPair(t, 62, 12 * bar + 20, 12 * bar + 80);
  const NoteId r2 = acceptPair(t, 65, 16 * bar + 20, 16 * bar + 80);
  const NoteId r3 = acceptPair(t, 67, 20 * bar + 20, 20 * bar + 80);
  const NoteId r4 = acceptPair(t, 69, 23 * bar + 500, 23 * bar + 640);
  growRecordGeometry(t, length);
  // The frame the hold will retain: the 16-bar window clamped to the end of the growing take.
  submitFrame(length - 16);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedContentStart, submittedFrame.source.windowStart,
                                   "fixture must retain the clamped end-of-take interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, submittedFrame.source.windowLength,
                                   "fixture must retain a 16-bar content interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, submittedFrame.notes.size(),
                                   "fixture must retain the four notes inside the end-of-take interval");
  completeFrame();
  t.stopRecording(length);
  TEST_ASSERT_EQUAL_UINT32(length, t.getActiveLoop().loopLengthTicks);
  for (unsigned turn = 0; turn < 4096 && !t.getActiveLoop().hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  Loop& loop = t.getActiveLoop();
  TEST_ASSERT_TRUE_MESSAGE(loop.isPlaybackSessionExitPublicationGap(),
                           "fixture must hold the content for a pending publication");
  clockManager.assignCurrentTickSilently(length + 128);
  t.startOverdubbing(length + 128);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  // A live overlay *inside* the first requested viewport: the hold must not mix it into the held content.
  const NoteId live = acceptPair(t, 70, length + 3000, length + 3048);
  (void)live;
  // Three turns with the playhead advancing, so the requested start advances 0 -> 2000 -> 4000
  // (start = clamp(playhead - window/2, 0, length - window)); the retained rows inside it are 1, 2, 3.
  const uint32_t inLoopTicks[3] = {5144u, 8144u, 10144u};
  const uint32_t expectedViewStarts[3] = {0u, 2000u, 4000u};
  const uint32_t expectedDrawnRows[3] = {1u, 2u, 3u};
  uint32_t previousViewStart = UINT32_MAX;
  for (unsigned turn = 0; turn < 3; ++turn) {
    const uint32_t inLoop = inLoopTicks[turn];
    submitFrame(length + inLoop);
    TEST_ASSERT_TRUE_MESSAGE(loop.isPlaybackSessionExitPublicationGap(),
                             "the content hold must still be active for this turn");
    // (a) the drawn viewport is the requested interval, and it advanced with the playhead.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, DisplayCaptureTestAccess::drawnViewSlot(),
                                    "(a) the viewport slot must be the painted slot");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        DisplayCaptureTestAccess::requestedViewStart(), DisplayCaptureTestAccess::drawnViewStart(),
        "(a) the drawn viewport must be the requested interval, not the retained one");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        DisplayCaptureTestAccess::requestedViewLength(), DisplayCaptureTestAccess::drawnViewLength(),
        "(a) the drawn viewport length must be the requested length");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedViewStarts[turn],
                                     DisplayCaptureTestAccess::drawnViewStart(),
                                     "(a) drawn viewport start for this playhead");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, DisplayCaptureTestAccess::drawnViewLength(),
                                     "(a) drawn viewport length");
    TEST_ASSERT_TRUE_MESSAGE(inLoop >= DisplayCaptureTestAccess::drawnViewStart() &&
                                 inLoop < DisplayCaptureTestAccess::drawnViewStart() + windowLength,
                             "(a) the drawn viewport must contain the playhead");
    TEST_ASSERT_TRUE_MESSAGE(previousViewStart == UINT32_MAX ||
                                 DisplayCaptureTestAccess::drawnViewStart() > previousViewStart,
                             "(a) the drawn viewport must advance on every turn of the hold");
    previousViewStart = DisplayCaptureTestAccess::drawnViewStart();
    // (c) no stale interval is claimed current: the content key never advances under the hold.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedContentStart, submittedFrame.source.windowStart,
                                     "(c) the content key must stay the retained interval");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(windowLength, submittedFrame.source.windowLength,
                                     "(c) the content key length must stay the retained length");
    // (b) content remains the retained qualified interval -- never advanced, never re-gathered, no overlay mixed.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, submittedFrame.notes.size(),
                                     "(b) content must stay the retained qualified rows");
    assertNote(submittedFrame.notes, r1, 62, 12 * bar + 20, 12 * bar + 80);
    assertNote(submittedFrame.notes, r2, 65, 16 * bar + 20, 16 * bar + 80);
    assertNote(submittedFrame.notes, r3, 67, 20 * bar + 20, 20 * bar + 80);
    assertNote(submittedFrame.notes, r4, 69, 23 * bar + 500, 23 * bar + 640);
    // (d) exactness of what is shown: the rasterised rows are the retained rows inside the requested viewport.
    // Drawn in the retained interval's coordinates instead, every turn would rasterise all four rows
    // (the retained interval contains all four notes); the requested viewport shows 1, 2, 3. That difference is
    // the behavioural witness that the frame is rasterised in the requested coordinates, not the retained ones.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedDrawnRows[turn], submittedFrame.pianoRows,
                                     "(d) drawn rows must be the retained rows inside the requested viewport");
    completeFrame();
  }
  std::printf("HOLD_SPLIT turns=3 view_starts=%u,%u,%u req_start=%u retained_key=%u,%u "
              "drawn_rows=%u,%u,%u content_notes=%u\n",
              expectedViewStarts[0], expectedViewStarts[1], expectedViewStarts[2],
              DisplayCaptureTestAccess::requestedViewStart(), retainedContentStart, windowLength,
              expectedDrawnRows[0], expectedDrawnRows[1], expectedDrawnRows[2],
              static_cast<unsigned>(submittedFrame.notes.size()));
}
} // namespace ImmediateVisualProbe
int main() {
  using namespace ImmediateVisualProbe;
  UNITY_BEGIN();
  const char* which = std::getenv("IMMEDIATE_VISUAL_CASE");
  if (!which || std::strcmp(which, "controls") == 0) {
    std::puts("CURRENT CHARACTERIZATION ONLY: green does not establish the new visual bound");
    RUN_TEST(test_current_characterization_background_miss_exact_content);
    RUN_TEST(test_current_characterization_revision_supersession_between_slices);
    // Existing coherent held-presentation oracles, not new source-miss continuation proof.
    RUN_TEST(test_busy_transfer_keeps_complete_frame_pending_and_receipts_separate);
    RUN_TEST(test_moving_window_gap_never_mixes_old_prefix_with_new_viewport_overlay);
  } else if (std::strcmp(which, "bound") == 0) {
    RUN_TEST(test_desired_bound_source_miss_must_not_resolve_whole_passes);
    RUN_TEST(test_bounded_continuation_has_no_content_sized_step);
    RUN_TEST(test_bounded_continuation_publishes_exact_completion);
    RUN_TEST(test_bounded_continuation_supersession_never_publishes_obsolete);
    RUN_TEST(test_moving_viewport_while_bounded_continuation_is_in_flight);
    // The split's oracle: drawn viewport follows the request while the content stays retained.
    RUN_TEST(test_hold_keeps_retained_content_but_draws_the_requested_viewport);
    // C5.6 composition: a request outside the retained coverage, served by a qualified source.
    RUN_TEST(test_request_outside_retained_coverage_is_served_by_a_qualified_source);
  } else {
    std::fprintf(stderr, "unknown IMMEDIATE_VISUAL_CASE=%s (controls|bound)\n", which);
    return 2;
  }
  return UNITY_END();
}
