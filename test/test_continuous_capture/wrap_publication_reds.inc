// Wrap-publication ownership REDs (owner ruling 2026-11-03; design input
// `openspec/changes/revision-manifest-prototype/evidence/2026-11-03-wrap-publication-ownership-map.md` section 6).
//
// The wrap Clock must only register a publication obligation for the interval it commits; the existing publish box
// builds that interval's snapshot changes later, from its immutable pass and its sealed companion rows. RED cases fail
// on today's eager wrap journal. GUARD cases hold today and pin the result the redesign must keep (the old journal as
// parity oracle). Registered only with `WRAP_PUBLICATION_RED=1`, like the S1.2 `WORKING_SET_RED` gate, so the default
// native gate stays green until the implementing slice lands.

#include <map>
#include <tuple>

// `Track::processSnapshotOwnerTurn` reads these two only for a loaded-pass hydration
// (`pendingSnapshotFromLoadedPasses`), which no case here builds; this harness does not link the storage session.
bool StorageManager::bootInteractiveReady() { OFF_PATH(); return true; }
bool StorageManager::isBootTitleLoadDrain() { OFF_PATH(); return false; }

namespace wrap_publication {

using NoteKey = std::tuple<uint8_t, uint32_t, uint32_t>;  // pitch, start, end
using RootNotes = std::map<NoteId, NoteKey>;

RootNotes rootNotes(const Loop& loop) {
  RootNotes notes;
  const ImmutableLoopSnapshotRef root = loop.publishedSnapshot();
  if (!root) return notes;
  const uint32_t bars = (root->loopLengthTicks() + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
  for (uint16_t pitch = 0; pitch < kLoopSnapshotPitchCount; ++pitch) {
    for (uint32_t bar = 0; bar < bars; ++bar) {
      const auto membership = root->noteMembership(static_cast<uint8_t>(pitch), bar);
      if (!membership) continue;
      forEachSnapshotMembershipReference(membership.get(), [&notes](const LoopSnapshotNoteRef& ref) {
        if (ref) notes[ref->noteId] = NoteKey{ref->pitch, ref->startTick, ref->endTick};
      });
    }
  }
  return notes;
}

size_t countPitch(const RootNotes& notes, uint8_t pitch) {
  size_t n = 0;
  for (const auto& entry : notes) n += std::get<0>(entry.second) == pitch ? 1u : 0u;
  return n;
}

const NoteKey* findPitchStart(const RootNotes& notes, uint8_t pitch, uint32_t start) {
  for (const auto& entry : notes) {
    if (std::get<0>(entry.second) == pitch && std::get<1>(entry.second) == start) return &entry.second;
  }
  return nullptr;
}

/// Worst single publication-worker turn, in snapshot-change translations (the owner's "maximum individual
/// publication-worker turn"; the device measures its time).
struct Drain {
  unsigned turns = 0;
  uint32_t maxTranslationsPerTurn = 0;
  bool converged = false;
};

/// Runs the production Snapshot owner turn (`Track::processSnapshotOwnerTurn`, which applies its own admission gate)
/// with the session still running -- no stop, no session exit -- until nothing is pending. At least `minTurns` turns
/// run, so an obligation the gate cannot see is given the chance to be rediscovered. `observe` runs after every turn.
template <typename Observe>
Drain drainWithSessionRunning(Track& track, Observe observe, unsigned minTurns = 64) {
  Loop& loop = track.getActiveLoop();
  Drain d;
  for (; d.turns < 4096; ++d.turns) {
    if (d.turns >= minTurns && !loop.hasPendingSnapshotPublish()) break;
    const uint32_t before = Loop::snapshotChangeTranslationsForTest();
    TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
    const uint32_t spent = Loop::snapshotChangeTranslationsForTest() - before;
    if (spent > d.maxTranslationsPerTurn) d.maxTranslationsPerTurn = spent;
    observe(loop);
  }
  d.converged = !loop.hasPendingSnapshotPublish();
  return d;
}
Drain drainWithSessionRunning(Track& track) {
  return drainWithSessionRunning(track, [](Loop&) {});
}

/// The interval every case starts from: a published root holding the source note (48 at 0-48), an overdub opened
/// at tick 0, a same-pitch overdub that shortens the source (a sealed Shorten companion at the wrap), and two
/// fresh Adds.
Track& openIntervalA() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  for (unsigned turn = 0; turn < 4096 && !loop.publishedSnapshotIsCurrent(); ++turn)
    (void)loop.stepPendingSnapshotPublish(16);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(), "the source root must be published first");
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(48, 24, 36);    // shortens the source note: a Shorten companion
  inputNote(60, 100, 200);  // fresh Add
  inputNote(62, 300, 400);  // fresh Add
  return track;
}

bool wrap(Track& track) {
  clockManager.assignCurrentTickSilently(767);
  return track.commitOverdubWrap();
}

/// The source note is shortened by A's companion: 48 starting at 0 no longer ends at 48.
bool sourceShortened(const RootNotes& notes) {
  const NoteKey* source = findPitchStart(notes, 48, 0);
  return source == nullptr || std::get<2>(*source) < 48;
}

}  // namespace wrap_publication

/// RED 1 -- Clock ownership. The wrap Clock commits the pass and seals its companions, and builds **no** snapshot
/// change: no journal row, EditPass row or pass event is translated inside `commitOverdubWrap`, and the box holds no
/// journal fragment for it. Today it translates every pending row into fragments on the Clock.
void test_wrap_red_clock_translates_no_snapshot_change() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  const uint32_t before = Loop::snapshotChangeTranslationsForTest();
  TEST_ASSERT_TRUE(wrap(track));
  const uint32_t inClock = Loop::snapshotChangeTranslationsForTest() - before;
  char message[160];
  std::snprintf(message, sizeof(message), "wrap Clock translated %lu snapshot changes, %u journal fragments queued",
                static_cast<unsigned long>(inClock), static_cast<unsigned>(loop.pendingSnapshotWrapJournalCount()));
  std::printf("wrap RED 1: %s\n", message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, inClock, message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, loop.pendingSnapshotWrapJournalCount(), message);
  TEST_ASSERT_TRUE_MESSAGE(loop.hasPendingSnapshotPublish(), "the wrap must leave a publication obligation owed");
}

/// GUARD 2 -- immutable input. Interval B starts recording before A publishes and mutates what B owns (a note that
/// overlaps A's note, a fresh note), without wrapping. A's publication must equal A published with B silent.
void test_wrap_guard_deferred_interval_ignores_successor_mutation() {
  using namespace wrap_publication;
  RootNotes reference;
  {
    Track& track = openIntervalA();
    TEST_ASSERT_TRUE(wrap(track));
    const Drain d = drainWithSessionRunning(track);
    TEST_ASSERT_TRUE(d.converged);
    reference = rootNotes(track.getActiveLoop());
  }
  setUp();
  Track& track = openIntervalA();
  TEST_ASSERT_TRUE(wrap(track));
  inputNote(60, 768 + 110, 768 + 150);  // B: overlaps A's note 60 (B's own Shorten intent)
  inputNote(70, 768 + 500, 768 + 600);  // B: fresh
  const Drain d = drainWithSessionRunning(track);
  TEST_ASSERT_TRUE(d.converged);
  const RootNotes observed = rootNotes(track.getActiveLoop());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(reference.size(), observed.size(), "B leaked into A's publication");
  TEST_ASSERT_TRUE_MESSAGE(reference == observed, "A's published notes differ once B recorded");
}

/// GUARD 3 -- semantic parity. A publishes its Adds and its Shorten; a note held across the wrap is not in A and is
/// published once, with one NoteId, when B (which owns its NoteOff) wraps.
void test_wrap_guard_publication_keeps_add_shorten_and_held_note() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  inputNote(64, 700, UINT32_MAX);  // held across the wrap
  TEST_ASSERT_TRUE(wrap(track));
  TEST_ASSERT_TRUE(drainWithSessionRunning(track).converged);
  RootNotes afterA = rootNotes(loop);
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(afterA, 60, 100), "A's Add 60 must publish");
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(afterA, 62, 300), "A's Add 62 must publish");
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(afterA, 48, 24), "A's same-pitch overdub must publish");
  TEST_ASSERT_TRUE_MESSAGE(sourceShortened(afterA), "A's Shorten companion must publish with A");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, countPitch(afterA, 64), "the held note is not A's");

  clockManager.assignCurrentTickSilently(768 + 50);
  midiHandler.handleMidiMessage(midi::NoteOff, 1, 64, 0, SOURCE_USB);
  clockManager.assignCurrentTickSilently(768 + 767);
  TEST_ASSERT_TRUE(track.commitOverdubWrap());
  TEST_ASSERT_TRUE(drainWithSessionRunning(track).converged);
  const RootNotes afterB = rootNotes(loop);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, countPitch(afterB, 64), "the held note publishes exactly once");
}

/// GUARD 4 -- publication identity. While A drains, no turn exposes A's notes without A's companion; after the
/// drain the root is current at the post-seal revision.
void test_wrap_guard_publication_is_atomic_and_current() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(wrap(track));
  const uint32_t sealedRevision = loop.playbackRevision;
  bool partial = false;
  const Drain d = drainWithSessionRunning(track, [&partial](Loop& l) {
    const RootNotes notes = rootNotes(l);
    if (findPitchStart(notes, 60, 100) != nullptr && !sourceShortened(notes)) partial = true;
  });
  std::printf("wrap GUARD 4: drain turns=%u worst turn=%lu translations\n", d.turns,
              static_cast<unsigned long>(d.maxTranslationsPerTurn));
  TEST_ASSERT_TRUE(d.converged);
  TEST_ASSERT_FALSE_MESSAGE(partial, "a root carried A's notes without A's companion");
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL_UINT32(sealedRevision, loop.publishedSnapshotRevision());
}

/// RED 5 -- two rapid wraps. A and B both wrap before any publication turn. The box holds their obligations, not
/// private fragment copies of their content, and the drain applies them in order: B's Shorten of A's note lands
/// after A's pass.
void test_wrap_red_two_rapid_wraps_queue_obligations_not_copies() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(wrap(track));
  inputNote(60, 768 + 110, 768 + 150);  // B shortens A's note 60
  inputNote(70, 768 + 500, 768 + 600);
  clockManager.assignCurrentTickSilently(768 + 767);
  TEST_ASSERT_TRUE(track.commitOverdubWrap());
  char message[128];
  std::snprintf(message, sizeof(message), "two wraps queued %u private journal fragments",
                static_cast<unsigned>(loop.pendingSnapshotWrapJournalCount()));
  std::printf("wrap RED 5: %s\n", message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, loop.pendingSnapshotWrapJournalCount(), message);
  const Drain d = drainWithSessionRunning(track);
  TEST_ASSERT_TRUE(d.converged);
  const RootNotes notes = rootNotes(loop);
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(notes, 70, 500), "B's Add must publish");
  const NoteKey* aNote = findPitchStart(notes, 60, 100);
  TEST_ASSERT_TRUE_MESSAGE(aNote == nullptr || std::get<2>(*aNote) <= 110,
                           "B's Shorten of A's note must apply after A's pass");
}

/// RED 6 -- capacity, lossless within the session. The obligation for A cannot be queued (allocation). With the
/// session still running -- no stop, no session exit -- the publication owner must rediscover A's pass **and** A's
/// companion from the inventory, in causal order, and apply each exactly once. Today the pass is rediscovered by the
/// live pass enqueue but its companion waits for session exit, so A's notes publish without A's Shorten.
void test_wrap_red_lost_obligation_is_rediscovered_with_its_companions_in_session() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  Loop::failNextWrapPublicationObligationForTest();
  TEST_ASSERT_TRUE(wrap(track));
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const Drain d = drainWithSessionRunning(track);
  std::printf("wrap RED 6: drain turns=%u converged=%d worst turn=%lu translations\n", d.turns, d.converged ? 1 : 0,
              static_cast<unsigned long>(d.maxTranslationsPerTurn));
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const RootNotes notes = rootNotes(loop);
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(notes, 60, 100), "A's pass must be rediscovered in session");
  TEST_ASSERT_TRUE_MESSAGE(sourceShortened(notes), "A's companion must be rediscovered in session, not at exit");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, countPitch(notes, 60), "no change may be applied twice");
}

void registerWrapPublicationReds() {
  if (getenv("WRAP_PUBLICATION_RED") == nullptr) return;
  RUN_TEST(test_wrap_red_clock_translates_no_snapshot_change);
  RUN_TEST(test_wrap_guard_deferred_interval_ignores_successor_mutation);
  RUN_TEST(test_wrap_guard_publication_keeps_add_shorten_and_held_note);
  RUN_TEST(test_wrap_guard_publication_is_atomic_and_current);
  RUN_TEST(test_wrap_red_two_rapid_wraps_queue_obligations_not_copies);
  RUN_TEST(test_wrap_red_lost_obligation_is_rediscovered_with_its_companions_in_session);
}
