// Wrap-publication ownership REDs (owner ruling 2026-11-03; design input
// `openspec/changes/revision-manifest-prototype/evidence/2026-11-03-wrap-publication-ownership-map.md` section 6).
//
// The wrap Clock must only register a publication obligation for the interval it commits; the existing publish box
// builds that interval's snapshot changes later, from its immutable pass and its sealed companion rows. RED cases fail
// on the eager wrap journal (`822c0cee`, RED baseline); GUARD cases held there and pin the result the redesign keeps
// (the old journal as parity oracle). The implementing slice (DEC-248) turned the REDs green, so the former
// `WRAP_PUBLICATION_RED` gate is gone and every case runs in the default gate.

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

/// Root MIDI events: per NoteId (NoteOn, NoteOff) counts, and the non-note events.
struct RootEvents {
  std::map<NoteId, std::pair<unsigned, unsigned>> notes;
  std::vector<MidiEvent> nonNote;
};
RootEvents rootEvents(const Loop& loop) {
  RootEvents out;
  const ImmutableLoopSnapshotRef root = loop.publishedSnapshot();
  if (!root) return out;
  const uint32_t bars = (root->loopLengthTicks() + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
  for (uint32_t bar = 0; bar < bars; ++bar) {
    const auto membership = root->eventMembership(bar);
    if (!membership) continue;
    forEachSnapshotMembershipReference(membership.get(), [&out](const LoopSnapshotEventRef& ref) {
      if (!ref) return;
      const MidiEvent& e = ref->value;
      if (e.isNoteOn()) ++out.notes[e.noteId].first;
      else if (e.isNoteOff()) ++out.notes[e.noteId].second;
      else out.nonNote.push_back(e);
    });
  }
  return out;
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
/// private fragment copies of their content, and the drain applies them in order: B's Hide of A's note lands
/// after A's pass.
void test_wrap_red_two_rapid_wraps_queue_obligations_not_copies() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(wrap(track));
  inputNote(60, 768 + 90, 768 + 210);  // B covers A's note 60
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
  // B's covering note seals a Hide of A's note 60. Applied before A's pass it would find no target and A's note
  // would survive; its absence proves the record order (A's pass, then B's companion).
  TEST_ASSERT_NULL_MESSAGE(findPitchStart(notes, 60, 100), "B's Hide of A's note must apply after A's pass");
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(notes, 60, 90), "B's covering note must publish");
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


/// GUARD 7 -- Hide and non-note parity. An overdub that covers the source note seals a Hide (Delete companion); a
/// controller recorded in the interval is part of its pass. Both publish with A.
void test_wrap_guard_publication_keeps_hide_and_non_note_events() {
  using namespace wrap_publication;
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  for (unsigned turn = 0; turn < 4096 && !loop.publishedSnapshotIsCurrent(); ++turn)
    (void)loop.stepPendingSnapshotPublish(16);
  const RootNotes before = rootNotes(loop);
  TEST_ASSERT_EQUAL_UINT32(1u, before.size());
  const NoteId sourceId = before.begin()->first;
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  inputNote(48, 0, 60);  // covers the source note: Hide
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(300, 1, 7, 90)));
  TEST_ASSERT_TRUE(wrap(track));
  TEST_ASSERT_TRUE(drainWithSessionRunning(track).converged);
  const RootNotes notes = rootNotes(loop);
  TEST_ASSERT_TRUE_MESSAGE(notes.find(sourceId) == notes.end(), "A's Hide must remove the source note");
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(notes, 48, 0), "A's covering note must publish");
  const RootEvents events = rootEvents(loop);
  TEST_ASSERT_TRUE_MESSAGE(events.notes.find(sourceId) == events.notes.end(),
                           "A's Hide must remove the source note's events");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, events.nonNote.size(), "A's controller must publish once");
  TEST_ASSERT_EQUAL_UINT32(300u, events.nonNote[0].tick);
}

/// GUARD 8 -- repeated NoteId, resolved by the identity contract. The journal admits every Add row; the pass producer
/// admits a pass note only when the parent lacks its NoteId. They can differ only if a committed wrap pass carries a
/// NoteId the parent already has. Capture assigns every NoteOn a fresh `nextNoteId_`, so on the production path no
/// wrap pass does, and the two admission rules are equivalent there. This pins that contract (including a note held
/// across the wrap, the one case that carries a NoteId between passes).
void test_wrap_guard_wrap_pass_note_ids_are_fresh_against_the_parent() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  inputNote(64, 700, UINT32_MAX);  // held across the wrap
  const RootNotes parent = rootNotes(loop);
  TEST_ASSERT_TRUE(wrap(track));
  clockManager.assignCurrentTickSilently(768 + 50);
  midiHandler.handleMidiMessage(midi::NoteOff, 1, 64, 0, SOURCE_USB);
  inputNote(66, 768 + 200, 768 + 260);
  clockManager.assignCurrentTickSilently(768 + 767);
  TEST_ASSERT_TRUE(track.commitOverdubWrap());
  for (size_t record = 0; record < 2; ++record) {
    const PassId passId = loop.overdubSessionPassIdAt(record);
    TEST_ASSERT_NOT_EQUAL(kInvalidPassId, passId);
    const OverdubPass* pass = nullptr;
    for (const OverdubPass& p : loop.passes.overdubPasses) if (p.id == passId) pass = &p;
    TEST_ASSERT_NOT_NULL(pass);
    SessionMidiEventVec events;
    LoopEventStore::appendChunkRefEvents(pass->committedChunkIds, events);
    for (const MidiEvent& e : events) {
      if (!e.isNoteOn()) continue;
      TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, e.noteId);
      TEST_ASSERT_TRUE_MESSAGE(parent.find(e.noteId) == parent.end(), "a wrap pass reused a parent NoteId");
    }
  }
}

/// GUARD 9 -- delayed publication across the session exit (owner ruling 2026-11-03). A wraps; the publication owner
/// is delayed; the overdub stops, its deferred stop runs to completion and the session record is cleared; then
/// publication resumes. A's pass and A's companion must each be published exactly once, at a current revision.
void test_wrap_guard_publication_delayed_across_session_exit_is_exactly_once() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  const RootNotes source = rootNotes(loop);
  const NoteId sourceId = source.begin()->first;
  TEST_ASSERT_TRUE(wrap(track));
  clockManager.assignCurrentTickSilently(768 + 40);
  midiButtonActions.handleToggleRecord();  // stop: deferred, the session exits
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  for (unsigned turn = 0; turn < 4096 && track.hasPendingDeferredOverdubStop(); ++turn) {
    TrackCaptureStopTestAccess::snapshotOwnerTurn(track);  // only if the stop waits on publication
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(), "the deferred stop must retire");
  TEST_ASSERT_FALSE(track.isOverdubbing());
  for (unsigned turn = 0; turn < 4096 && loop.hasPendingSnapshotPublish(); ++turn)
    TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
  TEST_ASSERT_FALSE_MESSAGE(loop.hasPendingSnapshotPublish(), "publication must converge after the exit");
  const RootNotes notes = rootNotes(loop);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, countPitch(notes, 60), "A's Add 60 exactly once");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, countPitch(notes, 62), "A's Add 62 exactly once");
  TEST_ASSERT_TRUE_MESSAGE(sourceShortened(notes), "A's Shorten must be applied");
  const RootEvents events = rootEvents(loop);
  const auto sourceEvents = events.notes.find(sourceId);
  TEST_ASSERT_TRUE(sourceEvents != events.notes.end());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, sourceEvents->second.first, "the shortened source keeps one NoteOn");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, sourceEvents->second.second, "the shortened source keeps one NoteOff");
  for (const auto& entry : events.notes) {
    TEST_ASSERT_TRUE_MESSAGE(entry.second.first <= 1 && entry.second.second <= 1, "an event was applied twice");
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(), "the root must cover the committed revision");
}

/// GUARD 10 -- bounded worker turns. A dense interval (48 notes over the source, so a companion too) publishes in
/// full, and no single publication-owner turn translates more than one pass-producer batch
/// (`kSnapshotWrapJournalMaxChanges`). The device measures the same turns' wall time.
void test_wrap_guard_dense_interval_publishes_in_bounded_worker_turns() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  for (uint8_t i = 0; i < 48; ++i) inputNote(70 + (i % 24), 410 + i * 7, 414 + i * 7);
  TEST_ASSERT_TRUE(wrap(track));
  const Drain d = drainWithSessionRunning(track);
  std::printf("wrap GUARD 10: drain turns=%u worst turn=%lu translations\n", d.turns,
              static_cast<unsigned long>(d.maxTranslationsPerTurn));
  TEST_ASSERT_TRUE(d.converged);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(kSnapshotWrapJournalMaxChanges, d.maxTranslationsPerTurn,
                                           "one publication-owner turn exceeded one pass-producer batch");
  const RootNotes notes = rootNotes(loop);
  size_t dense = 0;
  for (const auto& entry : notes) dense += std::get<1>(entry.second) >= 410 ? 1u : 0u;
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(48u, dense, "every dense note must publish");
  TEST_ASSERT_TRUE(sourceShortened(notes));
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

/// Review finding 1 (2026-11-03) -- session undo against the record cursor. Wrap A and B, publish partially, undo B
/// (its pass and companions disabled), record C and wrap: C takes B's record index. Whatever publishes must never be
/// a root marked current that lacks C or still carries B, and publication must converge.
void test_wrap_guard_session_undo_never_publishes_a_wrong_current_root() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(wrap(track));
  inputNote(70, 768 + 500, 768 + 600);  // B
  clockManager.assignCurrentTickSilently(768 + 767);
  TEST_ASSERT_TRUE(track.commitOverdubWrap());
  for (unsigned turn = 0; turn < 3; ++turn) TrackCaptureStopTestAccess::snapshotOwnerTurn(track);  // partial
  TEST_ASSERT_TRUE(loop.undoOverdubSession());  // B disabled
  inputNote(72, 2 * 768 + 200, 2 * 768 + 260);  // C
  clockManager.assignCurrentTickSilently(2 * 768 + 767);
  TEST_ASSERT_TRUE(track.commitOverdubWrap());  // C takes B's record index
  bool wrongCurrent = false;
  const Drain d = drainWithSessionRunning(track, [&wrongCurrent](Loop& l) {
    if (!l.publishedSnapshotIsCurrent()) return;
    const RootNotes notes = rootNotes(l);
    if (findPitchStart(notes, 72, 200) == nullptr || findPitchStart(notes, 70, 500) != nullptr) wrongCurrent = true;
  });
  TEST_ASSERT_FALSE_MESSAGE(wrongCurrent, "a current root lacked C or still carried undone B");
  TEST_ASSERT_TRUE_MESSAGE(d.converged, "publication must converge after a session undo");
  const RootNotes notes = rootNotes(loop);
  TEST_ASSERT_NOT_NULL_MESSAGE(findPitchStart(notes, 72, 200), "C must publish");
  TEST_ASSERT_NULL_MESSAGE(findPitchStart(notes, 70, 500), "undone B must not be in the root");
  TEST_ASSERT_NOT_NULL(findPitchStart(notes, 60, 100));
}

/// Gate B RED (owner ruling 2026-11-03, device `captures/session_20261103_211405.log` and `_213204.log`): finite
/// publication work must converge. On both images the arena tail fell to 36,864 / 32,768 B during the overdub and
/// never recovered (`_sbrk` is a pure bump), and `SnapshotReserve` admits on the tail alone (floor 32 KiB + 8 KiB), so
/// every publication turn was refused for the rest of the run while the free list still held large blocks. Modeled here
/// with the measured tail and the device's measured largest free chunk (79.4 KB, `!HEAP_STATS` 2026-10-27).
void test_publication_red_converges_when_the_tail_is_low_but_memory_is_free() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(wrap(track));
  MemoryMonitor::setNativeTestFreeHeap(36864);
  MemoryMonitor::setNativeTestAllocatableCapacity(80u * 1024u);
  clockManager.assignCurrentTickSilently(768 + 40);
  midiButtonActions.handleToggleRecord();  // overdub stop: deferred, the session exits
  for (unsigned turn = 0; turn < 20000 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
    if (turn % 4 == 0) TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
  }
  for (unsigned turn = 0; turn < 4096 && loop.hasPendingSnapshotPublish(); ++turn)
    TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
  const bool pending = loop.hasPendingSnapshotPublish();
  const bool current = loop.publishedSnapshotIsCurrent();
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::resetNativeTestAllocatableCapacity();
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(), "the deferred stop must retire");
  TEST_ASSERT_FALSE_MESSAGE(pending, "finite publication work must converge without new input");
  TEST_ASSERT_TRUE_MESSAGE(current, "the converged root must be current");
}

/// Gate B guard: the reserve still protects a genuinely low heap. With no free-list holes (the largest block equals the
/// 36,864 B tail) no publication turn is admitted: nothing is published and the work stays owed, never dropped.
void test_publication_guard_genuinely_low_memory_still_denies_and_keeps_the_work_owed() {
  using namespace wrap_publication;
  Track& track = openIntervalA();
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(wrap(track));
  const uint32_t rootBefore = loop.publishedSnapshotRevision();
  MemoryMonitor::setNativeTestFreeHeap(36864);
  MemoryMonitor::setNativeTestAllocatableCapacity(36864);
  for (unsigned turn = 0; turn < 512; ++turn) TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
  const bool pending = loop.hasPendingSnapshotPublish();
  const uint32_t rootDuring = loop.publishedSnapshotRevision();
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::resetNativeTestAllocatableCapacity();
  TEST_ASSERT_TRUE_MESSAGE(pending, "denied work must stay owed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rootBefore, rootDuring, "nothing may publish below the reserve");
  const Drain d = drainWithSessionRunning(track);  // memory returns: the same owed work now converges
  TEST_ASSERT_TRUE(d.converged);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

void registerWrapPublicationReds() {
  RUN_TEST(test_wrap_red_clock_translates_no_snapshot_change);
  RUN_TEST(test_wrap_guard_deferred_interval_ignores_successor_mutation);
  RUN_TEST(test_wrap_guard_publication_keeps_add_shorten_and_held_note);
  RUN_TEST(test_wrap_guard_publication_is_atomic_and_current);
  RUN_TEST(test_wrap_red_two_rapid_wraps_queue_obligations_not_copies);
  RUN_TEST(test_wrap_red_lost_obligation_is_rediscovered_with_its_companions_in_session);
  RUN_TEST(test_wrap_guard_publication_keeps_hide_and_non_note_events);
  RUN_TEST(test_wrap_guard_wrap_pass_note_ids_are_fresh_against_the_parent);
  RUN_TEST(test_wrap_guard_publication_delayed_across_session_exit_is_exactly_once);
  RUN_TEST(test_wrap_guard_dense_interval_publishes_in_bounded_worker_turns);
  RUN_TEST(test_wrap_guard_session_undo_never_publishes_a_wrong_current_root);
  // Gate B (2026-11-03): GREEN since `SnapshotReserve` resolves a low tail by the read-only free-list walk.
  RUN_TEST(test_publication_red_converges_when_the_tail_is_low_but_memory_is_free);
  RUN_TEST(test_publication_guard_genuinely_low_memory_still_denies_and_keeps_the_work_owed);
}

