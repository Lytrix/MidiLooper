// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include <unity.h>
#include "../../src/LoopEventStore.cpp"

struct LoopEventStoreSourceTestAccess {
  static void storeToken(LoopEventStore& s, uint64_t n) { s.prefixGeneration_ = n; }
  static uint64_t aliasToken() { return LoopEventStore::aliasGeneration_; }
  static void aliasToken(uint64_t n) { LoopEventStore::aliasGeneration_ = n; }
  static uint64_t poolToken() { return LoopEventStore::poolLifetime_; }
  static void poolToken(uint64_t n) { LoopEventStore::poolLifetime_ = n; }
};
using Result = LoopEventStoreSourceResult;
static uint64_t savedAlias, savedPool;
void setUp() {
  LoopEventStore::resetPoolForTests();
  savedAlias = LoopEventStoreSourceTestAccess::aliasToken();
  savedPool = LoopEventStoreSourceTestAccess::poolToken();
}
void tearDown() {
  // Unity assertion aborts must not leak a terminal test token into later cases.
  LoopEventStoreSourceTestAccess::aliasToken(savedAlias);
  LoopEventStoreSourceTestAccess::poolToken(savedPool);
}
static void appendRows(LoopEventStore& s, size_t n) {
  for (size_t i = 0; i < n; ++i)
    TEST_ASSERT_TRUE(s.append(MidiEvent::NoteOn(i + 10, 1, 60, 100)));
}
static void denied(LoopEventStore& s, LoopEventStoreSourceCursor& c) {
  MidiEvent out = MidiEvent::NoteOn(999, 1, 99, 100);
  TEST_ASSERT_FALSE(s.extendSourceCursor(c));
  TEST_ASSERT_EQUAL(Result::Denied, s.stepSourceCursor(c, out));
  TEST_ASSERT_EQUAL_UINT32(999, out.tick);
  TEST_ASSERT_EQUAL(99, out.data.noteData.note);
}
void test_frozen_extent_extends_without_replaying_directory_prefix() {
  LoopEventStore s; TEST_ASSERT_TRUE(s.reserveCaptureTail()); appendRows(s, 257);
  LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  MidiEvent out; size_t rows = 0, chunks = 0;
  while (rows < 257) {
    const auto r = s.stepSourceCursor(c, out);
    if (r == Result::Chunk) { ++chunks; continue; }
    TEST_ASSERT_EQUAL(Result::Row, r);
    TEST_ASSERT_EQUAL_UINT32(rows + 10, out.tick); ++rows;
    if (rows == 128) TEST_ASSERT_TRUE(s.append(MidiEvent::NoteOff(3, 1, 60, 0)));
    TEST_ASSERT_EQUAL(257, c.extent());
  }
  TEST_ASSERT_EQUAL(1, chunks);
  TEST_ASSERT_EQUAL(Result::End, s.stepSourceCursor(c, out));
  TEST_ASSERT_TRUE(s.extendSourceCursor(c)); TEST_ASSERT_EQUAL(258, c.extent());
  TEST_ASSERT_EQUAL(Result::Row, s.stepSourceCursor(c, out));
  TEST_ASSERT_EQUAL_UINT32(3, out.tick); TEST_ASSERT_TRUE(out.isNoteOff());
  TEST_ASSERT_EQUAL(258, c.position());
  TEST_ASSERT_EQUAL(Result::End, s.stepSourceCursor(c, out)); s.clear();
}
void test_empty_extent_wrong_receiver_and_refused_append() {
  LoopEventStore s, other; LoopEventStoreSourceCursor c;
  TEST_ASSERT_TRUE(s.beginSourceCursor(c)); MidiEvent out;
  TEST_ASSERT_EQUAL(Result::End, s.stepSourceCursor(c, out));
  appendRows(s, 1); TEST_ASSERT_TRUE(s.extendSourceCursor(c));
  TEST_ASSERT_FALSE(s.append(MidiEvent::NoteOn(UINT32_MAX, 1, 61, 100)));
  other.clear(); TEST_ASSERT_EQUAL(Result::Row, s.stepSourceCursor(c, out));
  TEST_ASSERT_EQUAL_UINT32(10, out.tick);
  TEST_ASSERT_EQUAL(Result::Denied, other.stepSourceCursor(c, out));
  TEST_ASSERT_EQUAL(Result::End, s.stepSourceCursor(c, out)); s.clear();
}
void test_shift_shrink_regrowth_clear_and_projection_refuse_old_prefix() {
  for (unsigned action = 0; action < 5; ++action) {
    LoopEventStore s; appendRows(s, 3); LoopEventStoreSourceCursor c;
    TEST_ASSERT_TRUE(s.beginSourceCursor(c));
    if (action == 0) s.shiftAllTicks(5);
    if (action == 1) { s.dropEventsAtOrBeyondTick(11); appendRows(s, 2); }
    if (action == 2) { s.clear(); appendRows(s, 3); }
    if (action == 3) s.mapNoteOnsAtOrBeyondTickIntoLoop(8);
    if (action == 4) s.assignMissingNoteIdsToNoteOns([] { return NoteId(77); });
    denied(s, c); s.clear();
  }
}
void test_rewrite_invalidates_between_grains_without_breaking_worker() {
  LoopEventStore s; appendRows(s, 3);
  MidiEvent source[] = {MidiEvent::NoteOn(20, 1, 62, 100), MidiEvent::NoteOff(30, 1, 62, 0)};
  uint16_t order[] = {0, 1}; LoopEventStoreRewriteCursor worker;
  TEST_ASSERT_TRUE(s.beginRewriteFromOrderedSource(worker, 2));
  LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
      s.stepRewriteFromOrderedSource(worker, source, order, 2, 2, 1));
  denied(s, c); c.discard(); TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
      s.stepRewriteFromOrderedSource(worker, source, order, 2, 2, 1));
  denied(s, c);
  auto r = LoopEventStoreRewriteResult::Pending;
  for (unsigned i = 0; i < 20 && r == LoopEventStoreRewriteResult::Pending; ++i)
    r = s.stepRewriteFromOrderedSource(worker, source, order, 2, 2, 1);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, r);
  TEST_ASSERT_TRUE(s.ownsRewriteCursor(worker)); TEST_ASSERT_EQUAL(2, s.size());
  TEST_ASSERT_EQUAL_UINT32(20, s.at(0).tick); TEST_ASSERT_EQUAL_UINT32(30, s.at(1).tick); s.clear();
}
void test_private_filter_marks_ids_and_compaction_invalidate() {
  LoopEventStore s; appendRows(s, 3); LoopEventStoreFilterCursor worker;
  TEST_ASSERT_TRUE(s.beginPrivateFilter(worker));
  auto r = LoopEventStoreFilterResult::Pending;
  for (unsigned i = 0; i < 30 && r == LoopEventStoreFilterResult::Pending; ++i)
    r = s.stepPrivateFilter(worker, 1);
  TEST_ASSERT_EQUAL(LoopEventStoreFilterResult::InputReady, r);
  LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_TRUE(s.assignPrivateNoteId(worker, 2, NoteId(88))); denied(s, c);
  c.discard(); TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_TRUE(s.markPrivateFilterPair(worker, 0, 1)); denied(s, c);
  TEST_ASSERT_TRUE(s.finishPrivateFilter(worker));
  c.discard(); TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_EQUAL(LoopEventStoreFilterResult::Pending, s.stepPrivateFilter(worker, 1));
  denied(s, c);
  r = LoopEventStoreFilterResult::Pending;
  for (unsigned i = 0; i < 30 && r == LoopEventStoreFilterResult::Pending; ++i)
    r = s.stepPrivateFilter(worker, 1);
  TEST_ASSERT_EQUAL(LoopEventStoreFilterResult::Complete, r);
  TEST_ASSERT_EQUAL(1, s.size()); TEST_ASSERT_EQUAL_UINT32(12, s.at(0).tick);
  TEST_ASSERT_EQUAL(88, s.at(0).noteId); s.clear();
}
void test_private_order_invalidates_readers_acquired_after_admission() {
  LoopEventStore s; TEST_ASSERT_TRUE(s.append(MidiEvent::NoteOn(30, 1, 60, 100)));
  TEST_ASSERT_TRUE(s.append(MidiEvent::NoteOff(10, 1, 60, 0)));
  LoopEventStoreOrderCursor worker; TEST_ASSERT_TRUE(s.beginPrivateTickOrder(worker));
  auto r = LoopEventStoreRewriteResult::Pending;
  for (unsigned i = 0; i < 100 && r == LoopEventStoreRewriteResult::Pending; ++i) {
    LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
    r = s.stepPrivateTickOrder(worker, 1); denied(s, c);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, r);
  TEST_ASSERT_EQUAL_UINT32(10, s.at(0).tick); s.clear();
}
void test_seal_transfer_adopt_reset_and_retirement_refuse() {
  for (unsigned action = 0; action < 5; ++action) {
    LoopEventStore s, other; TEST_ASSERT_TRUE(s.reserveCaptureTail());
    if (action != 3) appendRows(s, 1);
    LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
    LoopEventStore::CaptureRange range; CaptureChunkIdList ids;
    if (action == 0) { TEST_ASSERT_TRUE(range.reserveTail()); TEST_ASSERT_TRUE(s.sealCaptureRange(range)); }
    if (action == 1) s.detachChunksTo(ids);
    if (action == 2) other.adoptAll(s);
    if (action == 3) TEST_ASSERT_TRUE(s.resetCaptureExtentPreservingReservedStorage());
    if (action == 4) s.releaseCaptureChunkGrain();
    denied(s, c); LoopEventStore::releaseChunkRefs(ids); s.clear(); other.clear();
  }
}
static NoteId supply(void*) { return NoteId(77); }
void test_static_id_alias_invalidates_all_only_on_actual_write() {
  LoopEventStore s, other; appendRows(s, 1); appendRows(other, 1);
  LoopEventStoreSourceCursor c, d; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_TRUE(other.beginSourceCursor(d));
  LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(s.chunkIds().data(), 1, supply, nullptr);
  denied(s, c); denied(other, d); TEST_ASSERT_EQUAL(77, s.at(0).noteId);
  c.discard(); TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(s.chunkIds().data(), 1, supply, nullptr);
  MidiEvent out; TEST_ASSERT_EQUAL(Result::Row, s.stepSourceCursor(c, out)); s.clear(); other.clear();
}
void test_pool_token_exhaustion_is_terminal_without_stopping_accepted_content() {
  const auto original = LoopEventStoreSourceTestAccess::poolToken();
  LoopEventStoreSourceTestAccess::poolToken(UINT64_MAX);
  LoopEventStore s; TEST_ASSERT_TRUE(s.reserveCaptureTail()); appendRows(s, 1);
  LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  LoopEventStore::resetPoolForTests(); denied(s, c); c.discard();
  LoopEventStore replacement; TEST_ASSERT_TRUE(replacement.append(MidiEvent::NoteOn(20, 1, 62, 100)));
  TEST_ASSERT_FALSE(replacement.beginSourceCursor(c));
  replacement.clear(); LoopEventStore::resetPoolForTests();
  TEST_ASSERT_EQUAL_UINT64(0, LoopEventStoreSourceTestAccess::poolToken());
  TEST_ASSERT_FALSE(replacement.beginSourceCursor(c));
  // No stale store is reused; restore only the test process's global fixture.
  s.clear(); LoopEventStoreSourceTestAccess::poolToken(original);
}
void test_pool_reset_and_terminal_tokens_never_resurrect() {
  LoopEventStore s; TEST_ASSERT_TRUE(s.reserveCaptureTail()); appendRows(s, 1);
  LoopEventStoreSourceCursor c;
  TEST_ASSERT_TRUE(s.beginSourceCursor(c)); LoopEventStore::resetPoolForTests();
  LoopEventStore next; appendRows(next, 1); denied(s, c); s.clear(); next.clear();
  LoopEventStoreSourceTestAccess::storeToken(next, UINT64_MAX);
  TEST_ASSERT_TRUE(next.beginSourceCursor(c = LoopEventStoreSourceCursor{}));
  next.clear(); denied(next, c); c.discard(); TEST_ASSERT_FALSE(next.beginSourceCursor(c));
  next.clear(); TEST_ASSERT_FALSE(next.beginSourceCursor(c));
  TEST_ASSERT_TRUE(next.append(MidiEvent::NoteOn(1, 1, 60, 100))); next.clear();
  const auto alias = LoopEventStoreSourceTestAccess::aliasToken();
  LoopEventStoreSourceTestAccess::aliasToken(UINT64_MAX);
  LoopEventStore t; appendRows(t, 1); TEST_ASSERT_TRUE(t.beginSourceCursor(c));
  LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(t.chunkIds().data(), 1, supply, nullptr);
  denied(t, c); c.discard(); TEST_ASSERT_FALSE(t.beginSourceCursor(c)); t.clear();
  LoopEventStoreSourceTestAccess::aliasToken(alias); // fixture isolation only
}
void test_discard_before_owner_destruction_needs_no_owner_query() {
  LoopEventStoreSourceCursor c;
  { LoopEventStore s; appendRows(s, 1); TEST_ASSERT_TRUE(s.beginSourceCursor(c)); c.discard(); s.clear(); }
  TEST_ASSERT_EQUAL(0, c.position()); TEST_ASSERT_EQUAL(0, c.extent()); c.discard();
}
void test_reserved_append_extends_at_full_tail_without_allocation_or_replay() {
  LoopEventStore s; TEST_ASSERT_TRUE(s.reserveCaptureTail()); appendRows(s, 255);
  LoopEventStore::ChunkReservation credits; TEST_ASSERT_TRUE(credits.reserveChunks(1));
  const auto physical = LoopEventStore::usedChunkCount();
  LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_TRUE(s.appendReserved(MidiEvent::NoteOff(2, 1, 60, 0), credits));
  MidiEvent out;
  for (unsigned i = 0; i < 255; ++i) {
    TEST_ASSERT_EQUAL(Result::Row, s.stepSourceCursor(c, out));
    TEST_ASSERT_EQUAL_UINT32(i + 10, out.tick);
  }
  TEST_ASSERT_EQUAL(Result::End, s.stepSourceCursor(c, out));
  TEST_ASSERT_TRUE(s.extendSourceCursor(c));
  TEST_ASSERT_EQUAL(Result::Row, s.stepSourceCursor(c, out)); TEST_ASSERT_EQUAL_UINT32(2, out.tick);
  TEST_ASSERT_TRUE(s.appendReserved(MidiEvent::NoteOn(1, 1, 64, 100), credits));
  TEST_ASSERT_TRUE(s.extendSourceCursor(c));
  TEST_ASSERT_EQUAL(Result::Chunk, s.stepSourceCursor(c, out));
  TEST_ASSERT_EQUAL(Result::Row, s.stepSourceCursor(c, out)); TEST_ASSERT_EQUAL_UINT32(1, out.tick);
  TEST_ASSERT_EQUAL(physical, LoopEventStore::usedChunkCount()); s.clear();
}
void test_static_reference_retirement_invalidates_without_losing_owned_rows() {
  LoopEventStore s; appendRows(s, 1); LoopEventStoreSourceCursor c;
  const uint16_t id = s.chunkIds().front();
  LoopEventStore::retainChunkReferenceForTests(id);
  CaptureChunkIdList refs; refs.push_back(id);
  TEST_ASSERT_TRUE(s.beginSourceCursor(c)); LoopEventStore::releaseChunkRefs(refs); refs.clear();
  denied(s, c); TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
  TEST_ASSERT_EQUAL_UINT32(10, s.at(0).tick);
  CommittedChunkIdList committed; committed.push_back(id);
  LoopEventStore::retainChunkReferenceForTests(id);
  c.discard(); TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  TEST_ASSERT_TRUE(LoopEventStore::releaseLastCommittedChunkRef(committed)); denied(s, c);
  TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id)); s.clear();
}
void test_tick_window_id_write_invalidates_callback_acquired_cursor() {
  LoopEventStore s; appendRows(s, 3); LoopEventStoreSourceCursor before, during;
  TEST_ASSERT_TRUE(s.beginSourceCursor(before));
  s.assignMissingNoteIdsToNoteOnsInTickWindow(1, 12, [&] {
    TEST_ASSERT_TRUE(s.beginSourceCursor(during)); return NoteId(99);
  });
  denied(s, before); denied(s, during);
  TEST_ASSERT_EQUAL(kInvalidNoteId, s.at(0).noteId);
  TEST_ASSERT_EQUAL(99, s.at(1).noteId); TEST_ASSERT_EQUAL(kInvalidNoteId, s.at(2).noteId); s.clear();
}
void test_committed_transfer_grains_invalidate_new_borrows_and_preserve_worker() {
  LoopEventStore s; TEST_ASSERT_TRUE(s.prewarmCaptureDirectory()); appendRows(s, 257);
  CommittedChunkIdList dest; TEST_ASSERT_TRUE(LoopEventStore::prewarmCommittedChunkIdCapacity(dest, 2));
  LoopEventStoreSourceCursor c; TEST_ASSERT_TRUE(s.beginSourceCursor(c));
  const uint64_t generation = 7; LoopEventStoreCommitCursor worker;
  TEST_ASSERT_TRUE(s.beginCommittedChunkTransfer(worker, dest, generation)); denied(s, c);
  auto r = LoopEventStoreRewriteResult::Pending;
  for (unsigned i = 0; i < 10 && r == LoopEventStoreRewriteResult::Pending; ++i) {
    c.discard(); TEST_ASSERT_TRUE(s.beginSourceCursor(c));
    r = s.stepCommittedChunkTransfer(worker, 1); denied(s, c);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, r); TEST_ASSERT_TRUE(s.empty());
  TEST_ASSERT_EQUAL(257, LoopEventStore::countEventsInChunkIds(dest));
  LoopEventStore::releaseChunkRefs(dest); dest.clear(); s.clear();
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_frozen_extent_extends_without_replaying_directory_prefix);
  RUN_TEST(test_empty_extent_wrong_receiver_and_refused_append);
  RUN_TEST(test_shift_shrink_regrowth_clear_and_projection_refuse_old_prefix);
  RUN_TEST(test_rewrite_invalidates_between_grains_without_breaking_worker);
  RUN_TEST(test_private_filter_marks_ids_and_compaction_invalidate);
  RUN_TEST(test_private_order_invalidates_readers_acquired_after_admission);
  RUN_TEST(test_seal_transfer_adopt_reset_and_retirement_refuse);
  RUN_TEST(test_static_id_alias_invalidates_all_only_on_actual_write);
  RUN_TEST(test_pool_token_exhaustion_is_terminal_without_stopping_accepted_content);
  RUN_TEST(test_pool_reset_and_terminal_tokens_never_resurrect);
  RUN_TEST(test_discard_before_owner_destruction_needs_no_owner_query);
  RUN_TEST(test_reserved_append_extends_at_full_tail_without_allocation_or_replay);
  RUN_TEST(test_static_reference_retirement_invalidates_without_losing_owned_rows);
  RUN_TEST(test_tick_window_id_write_invalidates_callback_acquired_cursor);
  RUN_TEST(test_committed_transfer_grains_invalidate_new_borrows_and_preserve_worker);
  return UNITY_END();
}
