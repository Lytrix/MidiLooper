// Real constructor/step/freeze producers; this friend only observes ownership.
struct LoopSnapshotConstructionTestAccess {
  static size_t noteCapacity(const LoopSnapshotConstruction& c) { return c.changes_.notes.capacity(); }
  static size_t eventCapacity(const LoopSnapshotConstruction& c) { return c.changes_.events.capacity(); }
  static const LoopSnapshotNoteBarApply* note(const LoopSnapshotConstruction& c) { return c.noteApply_.get(); }
  static const LoopSnapshotEventBarApply* event(const LoopSnapshotConstruction& c) { return c.eventApply_.get(); }
  static bool drained(const LoopSnapshotConstruction& c) {
    return !c.noteApply_ && !c.eventApply_ && !c.constructionSpanRoot_ &&
      !c.constructionEventRoot_ && !c.candidate_.noteMembership(60, 0) &&
      !c.candidate_.eventMembership(0) && !c.parent_ && !c.frozenCandidate_ &&
      !c.changes_.notes.capacity() && !c.changes_.events.capacity();
  }
};
using ConstructionRelease = LoopSnapshotConstruction::ReleaseResult;
using ConstructionCursor = LoopSnapshotConstruction::ReleaseCursor;
static_assert(!std::is_copy_constructible<ConstructionCursor>::value, "single-use");
static_assert(!std::is_move_constructible<ConstructionCursor>::value, "stable identity");
static_assert(!std::is_copy_assignable<ConstructionCursor>::value, "single-use");
static_assert(!std::is_move_assignable<ConstructionCursor>::value, "stable identity");

template <typename Reference>
static size_t constructionReleaseLiveEdges(
    const std::weak_ptr<SnapshotMembership<Reference>>& weak) {
  const auto node = weak.lock();
  if (!node) return 0;
  size_t edges = 0;
  for (const auto& edge : node->references) if (edge) ++edges;
  return edges;
}
template <typename Reference>
static void constructionReleaseCopyGrains(LoopSnapshotConstruction& c,
    ConstructionCursor& cursor,
    const std::weak_ptr<SnapshotMembership<Reference>>& head,
    const std::weak_ptr<SnapshotMembership<Reference>>& tail) {
  // Two real copied pages contain 16 retained scalar edges each. Weak locks
  // exist only during observations, never across a production release grant.
  TEST_ASSERT_EQUAL(16, constructionReleaseLiveEdges(head));
  TEST_ASSERT_EQUAL(16, constructionReleaseLiveEdges(tail));
  size_t removed = 0;
  for (size_t i = 0; i < 100 && !tail.expired(); ++i) {
    const size_t before = constructionReleaseLiveEdges(head) + constructionReleaseLiveEdges(tail);
    const bool headAlive = !head.expired(), tailAlive = !tail.expired();
    const auto units = cursor.completedUnitCount();
    c.stepRelease(cursor, 1);
    const size_t after = constructionReleaseLiveEdges(head) + constructionReleaseLiveEdges(tail);
    TEST_ASSERT_TRUE(after <= before && before - after <= 1);
    TEST_ASSERT_EQUAL(units + 1, cursor.completedUnitCount());
    TEST_ASSERT_TRUE((headAlive && head.expired()) + (tailAlive && tail.expired()) <= 1);
    removed += before - after;
  }
  TEST_ASSERT_TRUE(head.expired()); TEST_ASSERT_TRUE(tail.expired());
  TEST_ASSERT_EQUAL(32, removed);
}

static LoopSnapshotChangeSet constructionReleaseChanges(size_t count) {
  LoopSnapshotChangeSet changes;
  for (size_t i = 0; i < count; ++i) {
    SnapshotNoteChange n{};
    n.kind = SnapshotChangeKind::Add;
    n.next.loopId = 1; n.next.noteId = static_cast<NoteId>(i + 1);
    n.next.pitch = 60; n.next.velocity = 100;
    n.next.startTick = 10; n.next.endTick = 100;
    changes.notes.push_back(n);
    SnapshotEventChange e{};
    e.kind = SnapshotChangeKind::Add;
    e.next = {1, static_cast<EventId>(i + 1), static_cast<uint32_t>(i),
              MidiEvent::ControlChange(10, 1, 7, static_cast<uint8_t>(i))};
    changes.events.push_back(e);
  }
  return changes;
}
static ImmutableLoopSnapshotRef constructionReleaseParent(size_t count) {
  auto empty = LoopSnapshot::createEmpty(1, Config::TICKS_PER_BAR, 0, 100, 100);
  auto changes = constructionReleaseChanges(count);
  LoopSnapshotConstruction c(empty, changes);
  for (size_t i = 0; i < 2000 && !c.complete(); ++i) TEST_ASSERT_TRUE(c.step(1));
  TEST_ASSERT_TRUE(c.complete());
  return c.freeze();
}
static void constructionReleaseDrain(LoopSnapshotConstruction& c, ConstructionCursor& cursor) {
  // 40 references: <=3 membership pages each; 128 pitch slots, 32 bar slots,
  // all workspace aliases/rows plus roots/shells fit below 4096 graph grains.
  for (size_t i = 0; i < 4096 && c.releaseResult(cursor) != ConstructionRelease::Complete; ++i) {
    const auto before = cursor.completedUnitCount();
    c.stepRelease(cursor, 1);
    TEST_ASSERT_EQUAL(before + 1, cursor.completedUnitCount());
  }
  TEST_ASSERT_TRUE(c.releaseResult(cursor) == ConstructionRelease::Complete);
  TEST_ASSERT_TRUE(LoopSnapshotConstructionTestAccess::drained(c));
  const auto units = cursor.completedUnitCount();
  TEST_ASSERT_TRUE(c.stepRelease(cursor, 10) == ConstructionRelease::Complete);
  TEST_ASSERT_TRUE(c.beginRelease(cursor) == ConstructionRelease::Complete);
  TEST_ASSERT_EQUAL(units, cursor.completedUnitCount());
  printf("construction-release drained units=%zu\n", units);
}
static void test_construction_release_acceptance_and_literal_rows() {
  auto parent = constructionReleaseParent(1);
  auto changes = constructionReleaseChanges(3);
  LoopSnapshotConstruction c(parent, changes);
  ConstructionCursor cursor;
  const auto capacity = LoopSnapshotConstructionTestAccess::noteCapacity(c);
  const auto eventCapacity = LoopSnapshotConstructionTestAccess::eventCapacity(c);
  const auto allocations = snapshotAllocNodeCount();
  TEST_ASSERT_TRUE(c.beginRelease(cursor) == ConstructionRelease::Progress);
  TEST_ASSERT_EQUAL(3, c.changes().notes.size());
  TEST_ASSERT_EQUAL(capacity, LoopSnapshotConstructionTestAccess::noteCapacity(c));
  TEST_ASSERT_EQUAL(eventCapacity, LoopSnapshotConstructionTestAccess::eventCapacity(c));
  TEST_ASSERT_EQUAL(allocations, snapshotAllocNodeCount());
  TEST_ASSERT_EQUAL(0, cursor.completedUnitCount());
  TEST_ASSERT_TRUE(c.stepRelease(cursor, 0) == ConstructionRelease::Progress);
  TEST_ASSERT_EQUAL(3, c.changes().notes.size());
  TEST_ASSERT_FALSE(c.complete()); TEST_ASSERT_FALSE(c.step(1)); TEST_ASSERT_FALSE(c.freeze());
  const auto length = c.candidate().loopLengthTicks();
  c.handoffLoopLengthTicks(length + 1);
  TEST_ASSERT_EQUAL(length, c.candidate().loopLengthTicks());
  // No workspaces: two phase grants, then exactly one literal row each.
  c.stepRelease(cursor, 1); c.stepRelease(cursor, 1);
  TEST_ASSERT_EQUAL(3, c.changes().notes.size());
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(2, c.changes().notes.size());
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(1, c.changes().notes.size());
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(0, c.changes().notes.size());
  TEST_ASSERT_EQUAL(capacity, LoopSnapshotConstructionTestAccess::noteCapacity(c));
  c.stepRelease(cursor, 1); // Empty note buffer retirement, not event row removal.
  TEST_ASSERT_EQUAL(0, LoopSnapshotConstructionTestAccess::noteCapacity(c));
  TEST_ASSERT_EQUAL(3, c.changes().events.size());
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(2, c.changes().events.size());
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(1, c.changes().events.size());
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(0, c.changes().events.size());
  TEST_ASSERT_EQUAL(eventCapacity, LoopSnapshotConstructionTestAccess::eventCapacity(c));
  constructionReleaseDrain(c, cursor);
}
static void test_construction_release_identity_and_single_use() {
  auto parent = constructionReleaseParent(1);
  auto changes = constructionReleaseChanges(3);
  LoopSnapshotConstruction a(parent, changes), b(parent, changes);
  ConstructionCursor cursor, overlap;
  TEST_ASSERT_TRUE(a.beginRelease(cursor) == ConstructionRelease::Progress);
  a.stepRelease(cursor, 1);
  const auto units = cursor.completedUnitCount();
  TEST_ASSERT_TRUE(a.beginRelease(cursor) == ConstructionRelease::Progress);
  TEST_ASSERT_TRUE(a.beginRelease(overlap) == ConstructionRelease::Rejected);
  TEST_ASSERT_TRUE(b.beginRelease(cursor) == ConstructionRelease::Rejected);
  TEST_ASSERT_TRUE(b.stepRelease(cursor, 1) == ConstructionRelease::Rejected);
  TEST_ASSERT_TRUE(b.releaseResult(cursor) == ConstructionRelease::Rejected);
  TEST_ASSERT_EQUAL(units, cursor.completedUnitCount());
  TEST_ASSERT_EQUAL(3, a.changes().notes.size());
  constructionReleaseDrain(a, cursor);
  TEST_ASSERT_TRUE(b.beginRelease(cursor) == ConstructionRelease::Rejected);
  TEST_ASSERT_TRUE(a.beginRelease(overlap) == ConstructionRelease::Rejected);
  TEST_ASSERT_TRUE(b.beginRelease(overlap) == ConstructionRelease::Progress);
  constructionReleaseDrain(b, overlap);
}
static void test_construction_release_partial_note_copy_and_failure() {
  auto parent = constructionReleaseParent(40);
  auto changes = constructionReleaseChanges(1);
  changes.notes[0].next.noteId = 80;
  changes.events.clear();
  auto c = std::make_unique<LoopSnapshotConstruction>(parent, changes);
  for (size_t i = 0; i < 20; ++i) {
    TEST_ASSERT_TRUE(c->step(1));
    auto* w = LoopSnapshotConstructionTestAccess::note(*c);
    if (w && w->membershipCopy.pageCount == 2 && w->membershipCopy.source) break;
  }
  auto* w = LoopSnapshotConstructionTestAccess::note(*c);
  TEST_ASSERT_NOT_NULL(w); TEST_ASSERT_EQUAL(2, w->membershipCopy.pageCount);
  TEST_ASSERT_TRUE(w->membershipCopy.source);
  std::weak_ptr<SnapshotMembership<LoopSnapshotNoteRef>> head = w->membershipCopy.head;
  std::weak_ptr<SnapshotMembership<LoopSnapshotNoteRef>> tail = w->membershipCopy.tail;
  c->failAfterUnitsForTest(c->completedUnitCount());
  TEST_ASSERT_FALSE(c->step(1)); TEST_ASSERT_TRUE(c->failed());
  ConstructionCursor cursor; c->beginRelease(cursor);
  TEST_ASSERT_FALSE(head.expired()); TEST_ASSERT_FALSE(tail.expired());
  c->stepRelease(cursor, 0); TEST_ASSERT_FALSE(head.expired());
  constructionReleaseCopyGrains(*c, cursor, head, tail);
  constructionReleaseDrain(*c, cursor);
  TEST_ASSERT_TRUE(head.expired()); TEST_ASSERT_TRUE(tail.expired());
  TEST_ASSERT_NOT_NULL(parent->findNote(1, 60).get());
  c.reset(); // drained shell, no deferred payload hidden in destructor
}
static void test_construction_release_partial_event_copy_last_owner() {
  auto parent = constructionReleaseParent(40);
  auto changes = constructionReleaseChanges(1);
  changes.notes.clear(); changes.events[0].next.eventId = 80;
  LoopSnapshotConstruction c(parent, changes);
  for (size_t i = 0; i < 20; ++i) {
    TEST_ASSERT_TRUE(c.step(1));
    auto* w = LoopSnapshotConstructionTestAccess::event(c);
    if (w && w->membershipCopy.pageCount == 2 && w->membershipCopy.source) break;
  }
  auto* w = LoopSnapshotConstructionTestAccess::event(c);
  TEST_ASSERT_NOT_NULL(w); TEST_ASSERT_EQUAL(2, w->membershipCopy.pageCount);
  TEST_ASSERT_TRUE(w->membershipCopy.source);
  std::weak_ptr<SnapshotMembership<LoopSnapshotEventRef>> head = w->membershipCopy.head;
  std::weak_ptr<SnapshotMembership<LoopSnapshotEventRef>> tail = w->membershipCopy.tail;
  std::weak_ptr<const SnapshotMembership<LoopSnapshotEventRef>> source = w->membershipCopy.source;
  std::weak_ptr<const LoopSnapshot> parentWeak = parent;
  parent.reset();
  ConstructionCursor cursor; c.beginRelease(cursor);
  TEST_ASSERT_FALSE(head.expired()); TEST_ASSERT_FALSE(source.expired());
  constructionReleaseCopyGrains(c, cursor, head, tail);
  constructionReleaseDrain(c, cursor);
  TEST_ASSERT_TRUE(head.expired()); TEST_ASSERT_TRUE(tail.expired());
  TEST_ASSERT_TRUE(source.expired()); TEST_ASSERT_TRUE(parentWeak.expired());
}
static void test_construction_release_idle_shared_frozen_and_parent() {
  auto parent = constructionReleaseParent(1);
  auto changes = constructionReleaseChanges(3);
  for (auto& n : changes.notes) n.next.noteId += 10;
  for (auto& e : changes.events) e.next.eventId += 10;
  LoopSnapshotConstruction c(parent, changes);
  for (size_t i = 0; i < 200 && !c.complete(); ++i) TEST_ASSERT_TRUE(c.step(1));
  TEST_ASSERT_TRUE(c.complete());
  TEST_ASSERT_NOT_NULL(LoopSnapshotConstructionTestAccess::note(c));
  TEST_ASSERT_NOT_NULL(LoopSnapshotConstructionTestAccess::event(c));
  auto frozen = c.freeze(); TEST_ASSERT_TRUE(frozen);
  auto membership = frozen->noteMembership(60, 0);
  const auto count = membership->count; membership.reset();
  const auto* noteWork = LoopSnapshotConstructionTestAccess::note(c);
  const auto* eventWork = LoopSnapshotConstructionTestAccess::event(c);
  const auto noteRows = noteWork->membershipEdits.size();
  const auto eventRows = eventWork->membershipEdits.size();
  const auto noteCapacity = noteWork->membershipEdits.capacity();
  const auto eventCapacity = eventWork->membershipEdits.capacity();
  TEST_ASSERT_TRUE(noteRows > 0 && eventRows > 0);
  ConstructionCursor cursor; c.beginRelease(cursor);
  c.stepRelease(cursor, 0);
  TEST_ASSERT_EQUAL(noteRows, noteWork->membershipEdits.size());
  TEST_ASSERT_EQUAL(eventRows, eventWork->membershipEdits.size());
  TEST_ASSERT_EQUAL(noteCapacity, noteWork->membershipEdits.capacity());
  TEST_ASSERT_EQUAL(eventCapacity, eventWork->membershipEdits.capacity());
  TEST_ASSERT_FALSE(c.complete()); TEST_ASSERT_FALSE(c.step(1)); TEST_ASSERT_FALSE(c.freeze());
  constructionReleaseDrain(c, cursor);
  TEST_ASSERT_EQUAL(count, frozen->noteMembership(60, 0)->count);
  TEST_ASSERT_NOT_NULL(frozen->findNote(11, 60).get());
  TEST_ASSERT_NOT_NULL(parent->findNote(1, 60).get());
  TEST_ASSERT_EQUAL(4, snapshotMembershipCount(frozen->eventMembership(0).get()));
  TEST_ASSERT_EQUAL(1, snapshotMembershipCount(parent->eventMembership(0).get()));
}
static void test_construction_release_exclusive_frozen_empty_missing_parent() {
  auto parent = constructionReleaseParent(3);
  LoopSnapshotChangeSet changes;
  auto c = std::make_unique<LoopSnapshotConstruction>(parent, changes);
  auto frozen = c->freeze();
  std::weak_ptr<const LoopSnapshot> weak = frozen;
  std::weak_ptr<const SnapshotMembership<LoopSnapshotNoteRef>> membership = frozen->noteMembership(60, 0);
  frozen.reset(); parent.reset();
  ConstructionCursor cursor; c->beginRelease(cursor);
  TEST_ASSERT_FALSE(weak.expired());
  constructionReleaseDrain(*c, cursor);
  TEST_ASSERT_TRUE(weak.expired()); TEST_ASSERT_TRUE(membership.expired()); c.reset();
  auto empty = LoopSnapshot::createEmpty(1, Config::TICKS_PER_BAR, 0, 1, 1);
  LoopSnapshotConstruction e(empty, changes), missing({}, changes);
  ConstructionCursor ec, mc;
  e.beginRelease(ec); constructionReleaseDrain(e, ec);
  TEST_ASSERT_TRUE(missing.failed());
  missing.beginRelease(mc); constructionReleaseDrain(missing, mc);
}

static void test_construction_release_previous_edges_before_rows() {
  auto parent = constructionReleaseParent(1);
  auto changes = constructionReleaseChanges(1);
  changes.notes[0].kind = SnapshotChangeKind::Remove;
  changes.notes[0].previous = parent->findNote(1, 60);
  changes.events[0].kind = SnapshotChangeKind::Remove;
  changes.events[0].previous = parent->eventMembership(0)->references[0];
  LoopSnapshotConstruction c(parent, changes);
  changes.notes.clear(); changes.events.clear(); // No fixture-held scalar aliases.
  ConstructionCursor cursor;
  TEST_ASSERT_TRUE(c.releaseResult(cursor) == ConstructionRelease::Idle);
  TEST_ASSERT_TRUE(c.stepRelease(cursor, 0) == ConstructionRelease::Rejected);
  c.beginRelease(cursor);
  c.stepRelease(cursor, 1); c.stepRelease(cursor, 1);
  c.stepRelease(cursor, 1); // Drain previous note edge, retain row.
  TEST_ASSERT_EQUAL(1, c.changes().notes.size());
  TEST_ASSERT_FALSE(c.changes().notes.back().previous);
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(0, c.changes().notes.size());
  c.stepRelease(cursor, 1); // Empty note buffer.
  c.stepRelease(cursor, 1); // Drain previous event edge, retain row.
  TEST_ASSERT_EQUAL(1, c.changes().events.size());
  TEST_ASSERT_FALSE(c.changes().events.back().previous);
  c.stepRelease(cursor, 1); TEST_ASSERT_EQUAL(0, c.changes().events.size());
  constructionReleaseDrain(c, cursor);
  TEST_ASSERT_NOT_NULL(parent->findNote(1, 60).get());
  TEST_ASSERT_EQUAL(1, snapshotMembershipCount(parent->eventMembership(0).get()));
}

static void test_construction_release_private_frozen_last_owner() {
  auto parent = LoopSnapshot::createEmpty(1, Config::TICKS_PER_BAR, 0, 100, 100);
  auto changes = constructionReleaseChanges(3);
  auto c = std::make_unique<LoopSnapshotConstruction>(parent, changes);
  for (size_t i = 0; i < 200 && !c->complete(); ++i) TEST_ASSERT_TRUE(c->step(1));
  TEST_ASSERT_TRUE(c->complete());
  auto frozen = c->freeze();
  std::weak_ptr<const LoopSnapshot> shell = frozen;
  std::weak_ptr<const LoopSnapshotNote> note = frozen->findNote(1, 60);
  std::weak_ptr<const LoopSnapshotEvent> event = frozen->eventMembership(0)->references[0];
  std::weak_ptr<const SnapshotMembership<LoopSnapshotNoteRef>> notePage = frozen->noteMembership(60, 0);
  std::weak_ptr<const SnapshotMembership<LoopSnapshotEventRef>> eventPage = frozen->eventMembership(0);
  frozen.reset(); parent.reset();
  ConstructionCursor cursor; c->beginRelease(cursor);
  TEST_ASSERT_FALSE(shell.expired()); TEST_ASSERT_FALSE(note.expired()); TEST_ASSERT_FALSE(event.expired());
  constructionReleaseDrain(*c, cursor);
  TEST_ASSERT_TRUE(shell.expired()); TEST_ASSERT_TRUE(note.expired()); TEST_ASSERT_TRUE(event.expired());
  TEST_ASSERT_TRUE(notePage.expired()); TEST_ASSERT_TRUE(eventPage.expired());
  c.reset();
}
