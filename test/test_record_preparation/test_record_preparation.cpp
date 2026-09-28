// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
//
// S4f.18 — bounded Record preparation composition. Drives the real
// LoopEventStore private order/filter owners with the real wrap-repair and
// minimum-length policy cursors through RecordPreparationCursor, and compares
// the result against the legacy store helpers on the same rows.
#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/LoopStopFinalize.cpp"
#include "../../src/Utils/CaptureIncrementalSanity.cpp"
#include "../../src/Utils/RecordPreparation.cpp"

using RecordPreparation::DenyReason;
using RecordPreparation::Geometry;
using RecordPreparation::RecordPreparationCursor;
using RecordPreparation::Step;
using Row = std::tuple<uint32_t, uint8_t, uint8_t, uint8_t, uint32_t, uint8_t>;

static uint8_t rowClass(const MidiEvent& e) {
  return e.isNoteOff() ? 0 : (e.isNoteOn() ? 1 : 2);
}

static Row rowOf(const MidiEvent& e) {
  return {e.tick, rowClass(e), e.channel, e.data.noteData.note,
          static_cast<uint32_t>(e.noteId), e.data.noteData.velocity};
}

static std::vector<Row> orderOf(LoopEventStore& store) {
  std::vector<Row> rows;
  for (size_t i = 0; i < store.size(); ++i) {
    rows.push_back(rowOf(store.at(i)));
  }
  return rows;
}

static std::vector<Row> inventory(LoopEventStore& store) {
  auto rows = orderOf(store);
  std::sort(rows.begin(), rows.end());
  return rows;
}

static void expectSequence(const std::vector<Row>& expected, const std::vector<Row>& got) {
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expected.size()),
                           static_cast<uint32_t>(got.size()));
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(std::get<0>(expected[i]), std::get<0>(got[i]));
    TEST_ASSERT_EQUAL_UINT8(std::get<1>(expected[i]), std::get<1>(got[i]));
    TEST_ASSERT_EQUAL_UINT8(std::get<2>(expected[i]), std::get<2>(got[i]));
    TEST_ASSERT_EQUAL_UINT8(std::get<3>(expected[i]), std::get<3>(got[i]));
    TEST_ASSERT_EQUAL_UINT32(std::get<4>(expected[i]), std::get<4>(got[i]));
    TEST_ASSERT_EQUAL_UINT8(std::get<5>(expected[i]), std::get<5>(got[i]));
  }
}

/// Exact stored order: the canonical (tick, class) contract under test.
static void expectRows(const std::vector<Row>& expected, LoopEventStore& store) {
  expectSequence(expected, orderOf(store));
}

/// Order-insensitive comparison against the legacy helpers, whose synthetic
/// Offs are appended rather than re-sorted.
static void expectInventory(const std::vector<Row>& expected, LoopEventStore& store) {
  expectSequence(expected, inventory(store));
}

static MidiEvent on(uint32_t tick, uint8_t channel, uint8_t note, uint32_t id,
                    uint8_t velocity = 100) {
  auto e = MidiEvent::NoteOn(tick, channel, note, velocity);
  e.noteId = id;
  return e;
}

static MidiEvent off(uint32_t tick, uint8_t channel, uint8_t note, uint32_t id) {
  auto e = MidiEvent::NoteOff(tick, channel, note, 0);
  e.noteId = id;
  return e;
}

/// Prewarmed private prepared output: reserved directory, dense chunk ids and
/// rows that never enter persistence.
static void fill(LoopEventStore& store, const std::vector<MidiEvent>& rows) {
  TEST_ASSERT_TRUE(store.prewarmCaptureDirectory());
  LoopEventStore::enterEphemeralSeal();
  for (const auto& row : rows) {
    TEST_ASSERT_TRUE(store.append(row));
  }
  LoopEventStore::leaveEphemeralSeal();
}

struct RunResult {
  Step step = Step::Denied;
  size_t units = 0;
  size_t synthetic = 0;
  size_t pairs = 0;
  size_t assigned = 0;
  DenyReason deny = DenyReason::None;
};

/// Deterministic identity source, mirroring `Loop::allocateNoteId()`'s contract.
struct IdCounter {
  NoteId next = 1;
  size_t taken = 0;
};

static NoteId takeNextId(void* context) {
  auto* counter = static_cast<IdCounter*>(context);
  ++counter->taken;
  return counter->next++;
}

static RunResult runPrepared(LoopEventStore& store, const Geometry& geometry, size_t quantum,
                             bool withReservation, const RecordPreparation::NoteIdSource* ids = nullptr) {
  RecordPreparation::Workspace workspace;
  RecordPreparationCursor cursor;
  uint64_t generation = 1;
  LoopEventStore::ChunkReservation reservation;
  LoopEventStore::ChunkReservation* credits = nullptr;
  if (withReservation) {
    TEST_ASSERT_TRUE(reservation.reserveChunks(
        RecordPreparation::kMaxSyntheticOffs / LoopEventStoreConfig::CHUNK_CAPACITY + 1));
    credits = &reservation;
  }
  const RecordPreparation::NoteIdSource noIds{};
  TEST_ASSERT_TRUE(cursor.begin(store, workspace, generation, geometry, credits,
                                ids != nullptr ? *ids : noIds));
  RunResult result;
  for (size_t turn = 0; turn < 200000; ++turn) {
    const Step step = cursor.step(quantum);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<uint32_t>(quantum),
                                     static_cast<uint32_t>(cursor.lastStepUnits()));
    result.units += cursor.lastStepUnits();
    if (step == Step::Pending) {
      continue;
    }
    result.step = step;
    result.synthetic = cursor.syntheticOffs();
    result.pairs = cursor.pairsRemoved();
    result.assigned = cursor.assignedIds();
    result.deny = cursor.denyReason();
    cursor.discard();
    return result;
  }
  TEST_FAIL_MESSAGE("record preparation did not terminate");
  return result;
}

/// Legacy inline stop policy on the same rows: canonical sort, wrap repair, then
/// minimum-length removal.
static std::vector<Row> legacyReference(std::vector<MidiEvent> rows, uint32_t length,
                                        uint32_t close, uint32_t window, uint32_t minTicks,
                                        bool minEnabled) {
  std::sort(rows.begin(), rows.end(), LoopStopFinalize::noteEventLess);
  LoopEventStore store;
  LoopEventStore::enterEphemeralSeal();
  for (const auto& row : rows) {
    TEST_ASSERT_TRUE(store.append(row));
  }
  LoopEventStore::leaveEphemeralSeal();
  LoopStopFinalize::finalizeWrapWindowOnStore(store, length, close, window);
  CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(store, length, minTicks,
                                                                minEnabled);
  return inventory(store);
}

/// Legacy inline policy including canonical-order identity completion.
static std::vector<Row> legacyReferenceWithIds(std::vector<MidiEvent> rows, uint32_t length,
                                               uint32_t close, uint32_t window, uint32_t minTicks,
                                               bool minEnabled, NoteId* nextId) {
  std::sort(rows.begin(), rows.end(), LoopStopFinalize::noteEventLess);
  for (auto& row : rows) {
    if (row.isNoteOn() && row.noteId == kInvalidNoteId) {
      row.noteId = (*nextId)++;
    }
  }
  LoopEventStore store;
  LoopEventStore::enterEphemeralSeal();
  for (const auto& row : rows) {
    TEST_ASSERT_TRUE(store.append(row));
  }
  LoopEventStore::leaveEphemeralSeal();
  LoopStopFinalize::finalizeWrapWindowOnStore(store, length, close, window);
  CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(store, length, minTicks,
                                                                minEnabled);
  return inventory(store);
}

void setUp() { LoopEventStore::resetPoolForTests(); }
void tearDown() {}

void test_passthrough_preserves_canonical_content_and_order() {
  LoopEventStore store;
  fill(store, {on(0, 1, 60, 1), off(100, 1, 60, 1),
               MidiEvent::ControlChange(50, 1, 7, 99)});
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  const auto result = runPrepared(store, geometry, 7, true);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(0, result.synthetic);
  TEST_ASSERT_EQUAL_UINT32(0, result.pairs);
  expectRows({{0, 1, 1, 60, 1, 100}, {50, 2, 1, 7, 0, 99}, {100, 0, 1, 60, 1, 0}},
             store);
}

void test_unsorted_input_is_canonically_ordered() {
  LoopEventStore store;
  // Staged On-before-Off at the same tick: only a class-aware key reorders it.
  fill(store, {on(100, 1, 60, 1), off(100, 1, 61, 9), on(0, 1, 62, 2)});
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  const auto result = runPrepared(store, geometry, 3, true);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  // Equal-tick rows sort NoteOff before NoteOn.
  expectRows({{0, 1, 1, 62, 2, 100}, {100, 0, 1, 61, 9, 0}, {100, 1, 1, 60, 1, 100}},
             store);
}

void test_tail_repair_emits_and_reorders_synthetic_off() {
  LoopEventStore store;
  fill(store, {on(1500, 1, 60, 1), on(100, 1, 62, 2), off(200, 1, 62, 7)});
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  const auto result = runPrepared(store, geometry, 5, true);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(1, result.synthetic);
  TEST_ASSERT_EQUAL_UINT32(0, result.pairs);
  expectRows({{100, 1, 1, 62, 2, 100},
              {200, 0, 1, 62, 7, 0},
              {1500, 1, 1, 60, 1, 100},
              {1999, 0, 1, 60, 0, 0}},
             store);
}

void test_close_tick_branch_and_wrapped_pair() {
  {
    LoopEventStore store;
    fill(store, {on(1500, 1, 60, 1)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    geometry.openTailCloseTick = 1400;  // before the On: terminal Off moves to length-1
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(1, result.synthetic);
    expectRows({{1500, 1, 1, 60, 1, 100}, {1999, 0, 1, 60, 0, 0}}, store);
  }
  {
    LoopEventStore store;
    fill(store, {on(1400, 1, 60, 1)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    geometry.openTailCloseTick = 1500;  // after the On: terminal Off at the close tick
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(1, result.synthetic);
    expectRows({{1400, 1, 1, 60, 1, 100}, {1500, 0, 1, 60, 0, 0}}, store);
  }
  {
    LoopEventStore store;
    fill(store, {off(100, 1, 60, 9), on(1500, 1, 60, 1)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(0, result.synthetic);  // head Off wraps the tail On
    expectRows({{100, 0, 1, 60, 9, 0}, {1500, 1, 1, 60, 1, 100}}, store);
  }
}

void test_minimum_length_pair_removal_and_disable() {
  {
    LoopEventStore store;
    fill(store, {on(0, 1, 60, 1), off(20, 1, 60, 1), on(100, 2, 61, 2), off(300, 2, 61, 2)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    geometry.minLengthTicks = 48;
    geometry.minLengthRemoveEnabled = true;
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(1, result.pairs);
    expectRows({{100, 1, 2, 61, 2, 100}, {300, 0, 2, 61, 2, 0}}, store);
  }
  {
    LoopEventStore store;
    fill(store, {on(0, 1, 60, 1), off(20, 1, 60, 1)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    geometry.minLengthTicks = 48;
    geometry.minLengthRemoveEnabled = false;
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(0, result.pairs);
    expectRows({{0, 1, 1, 60, 1, 100}, {20, 0, 1, 60, 1, 0}}, store);
  }
}

void test_repair_and_minimum_compose_on_the_appended_stream() {
  {
    // The synthetic terminal Off completes a short pair, so the pair is removed.
    LoopEventStore store;
    fill(store, {on(1900, 1, 60, 1)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    geometry.openTailCloseTick = 100;
    geometry.minLengthTicks = 150;
    geometry.minLengthRemoveEnabled = true;
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(1, result.synthetic);
    TEST_ASSERT_EQUAL_UINT32(1, result.pairs);
    TEST_ASSERT_EQUAL_UINT32(0, store.size());
  }
  {
    // With a shorter threshold the same synthetic pair survives in order.
    LoopEventStore store;
    fill(store, {on(1900, 1, 60, 1)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    geometry.openTailCloseTick = 100;
    geometry.minLengthTicks = 10;
    geometry.minLengthRemoveEnabled = true;
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(1, result.synthetic);
    TEST_ASSERT_EQUAL_UINT32(0, result.pairs);
    expectRows({{1900, 1, 1, 60, 1, 100}, {1999, 0, 1, 60, 0, 0}}, store);
  }
  {
    // The reorder matters: the terminal Off lands before a later existing row.
    LoopEventStore store;
    fill(store, {on(1400, 1, 60, 1), MidiEvent::ControlChange(1800, 2, 7, 30)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    geometry.openTailCloseTick = 1500;
    const auto result = runPrepared(store, geometry, 4, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(1, result.synthetic);
    expectRows({{1400, 1, 1, 60, 1, 100},
                {1500, 0, 1, 60, 0, 0},
                {1800, 2, 2, 7, 0, 30}},
               store);
  }
}

void test_long_dense_take_beyond_overdub_staging() {
  std::vector<MidiEvent> rows;
  uint32_t id = 1;
  for (uint32_t pair = 0; pair < 1028; ++pair) {
    const uint32_t tick = pair * 48;
    rows.push_back(on(tick, 1, static_cast<uint8_t>(pair % 128), id, 100));
    rows.push_back(off(tick + 24, 1, static_cast<uint8_t>(pair % 128), id));
    ++id;
  }
  std::reverse(rows.begin(), rows.end());
  LoopEventStore store;
  fill(store, rows);
  TEST_ASSERT_EQUAL_UINT32(2056, store.size());
  Geometry geometry;
  geometry.loopLengthTicks = 49920;
  const auto result = runPrepared(store, geometry, 32, true);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(0, result.synthetic);
  TEST_ASSERT_EQUAL_UINT32(0, result.pairs);
  TEST_ASSERT_EQUAL_UINT32(2056, store.size());
  const auto got = inventory(store);
  for (size_t i = 0; i < got.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(i * 24), std::get<0>(got[i]));
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(i / 2 + 1), std::get<4>(got[i]));
  }
}

void test_empty_and_zero_length_terminals() {
  {
    LoopEventStore store;
    fill(store, {});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Empty), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(0, store.size());
  }
  {
    LoopEventStore store;
    fill(store, {on(1500, 1, 60, 1)});
    Geometry geometry;  // zero length skips wrap repair and minimum-length policy
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(0, result.synthetic);
    TEST_ASSERT_EQUAL_UINT32(1, store.size());
  }
}

void test_identity_incomplete_and_missing_reservation_deny() {
  {
    LoopEventStore store;
    fill(store, {on(1500, 1, 60, kInvalidNoteId)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    const auto result = runPrepared(store, geometry, 7, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Denied), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL(static_cast<int>(DenyReason::IdentityIncomplete),
                      static_cast<int>(result.deny));
  }
  {
    LoopEventStore store;
    fill(store, {on(1500, 1, 60, 1)});
    Geometry geometry;
    geometry.loopLengthTicks = 2000;
    const auto result = runPrepared(store, geometry, 7, false);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Denied), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL(static_cast<int>(DenyReason::ReservationMissing),
                      static_cast<int>(result.deny));
  }
}

void test_stale_preparation_generation_denies() {
  LoopEventStore store;
  fill(store, {on(0, 1, 60, 1), off(100, 1, 60, 1)});
  RecordPreparation::Workspace workspace;
  RecordPreparationCursor cursor;
  uint64_t generation = 1;
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  LoopEventStore::ChunkReservation reservation;
  TEST_ASSERT_TRUE(reservation.reserveChunks(2));
  TEST_ASSERT_TRUE(cursor.begin(store, workspace, generation, geometry, &reservation));
  generation = 2;  // the preparation role was replaced
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Denied), static_cast<int>(cursor.step(7)));
  TEST_ASSERT_EQUAL(static_cast<int>(DenyReason::OutputUnavailable),
                    static_cast<int>(cursor.denyReason()));
  cursor.discard();
}

void test_bounded_units_and_no_directory_growth() {
  std::vector<MidiEvent> rows;
  uint32_t id = 1;
  for (uint32_t pair = 0; pair < 400; ++pair) {
    const uint32_t tick = pair * 32;
    rows.push_back(on(tick, 1, static_cast<uint8_t>(pair % 128), id, 100));
    rows.push_back(off(tick + 16, 1, static_cast<uint8_t>(pair % 128), id));
    ++id;
  }
  rows.push_back(on(12000, 3, 70, id));  // needs a synthetic terminal Off
  std::reverse(rows.begin(), rows.end());
  for (const size_t quantum : {size_t(1), size_t(7), size_t(64)}) {
    LoopEventStore store;
    fill(store, rows);
    const auto capacity = store.chunkIds().capacity();
    const auto used = LoopEventStore::usedChunkCount();
    Geometry geometry;
    geometry.loopLengthTicks = 13000;
    geometry.minLengthTicks = 8;
    geometry.minLengthRemoveEnabled = true;
    const auto result = runPrepared(store, geometry, quantum, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(capacity, store.chunkIds().capacity());
    TEST_ASSERT_EQUAL_UINT32(used, LoopEventStore::usedChunkCount());
    TEST_ASSERT_TRUE(result.units > 0);
  }
}

void test_differential_against_legacy_store_policy() {
  uint32_t seed = 987654321u;
  auto next = [&]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  for (unsigned round = 0; round < 8; ++round) {
    const uint32_t length = 200 + next() % 2600;
    const uint32_t window = 1 + next() % 1000;
    const uint32_t minTicks = next() % 80;
    const bool minEnabled = (next() & 1u) != 0;
    const bool knownClose = (next() & 1u) != 0;
    const uint32_t close = knownClose ? next() % (length + 10) : UINT32_MAX;
    std::vector<MidiEvent> rows;
    uint32_t id = 1;
    for (unsigned i = 0; i < 300; ++i) {
      const uint32_t tick = next() % (length + 120);
      const uint8_t channel = static_cast<uint8_t>(1 + next() % 16);
      const uint8_t note = static_cast<uint8_t>(next() % 128);
      if (next() & 1u) {
        rows.push_back(on(tick, channel, note, id++, static_cast<uint8_t>(1 + next() % 127)));
      } else {
        rows.push_back(off(tick, channel, note, id++));
      }
    }
    LoopEventStore store;
    fill(store, rows);
    Geometry geometry;
    geometry.loopLengthTicks = length;
    geometry.openTailCloseTick = close;
    geometry.wrapWindow = window;
    geometry.minLengthTicks = minTicks;
    geometry.minLengthRemoveEnabled = minEnabled;
    const auto result = runPrepared(store, geometry, 13, true);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    const auto expected = legacyReference(rows, length, close, window, minTicks, minEnabled);
    expectInventory(expected, store);
  }
}

void test_missing_identities_are_completed_in_canonical_order() {
  LoopEventStore store;
  // Staged out of order: completion must follow the canonical order, not the append order.
  fill(store, {on(100, 1, 61, kInvalidNoteId), off(100, 1, 60, 9),
               on(0, 1, 62, kInvalidNoteId), MidiEvent::ControlChange(50, 1, 7, 3),
               on(200, 2, 70, kInvalidNoteId)});
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  IdCounter counter;
  const RecordPreparation::NoteIdSource source{&counter, takeNextId};
  const auto result = runPrepared(store, geometry, 3, true, &source);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(3, result.assigned);
  TEST_ASSERT_EQUAL_UINT32(3, counter.taken);
  // Canonical stream is On@0, CC@50, Off@100, On@100, On@200 -> ids 1, 2, 3 in that order.
  expectRows({{0, 1, 1, 62, 1, 100},
              {50, 2, 1, 7, 0, 3},
              {100, 0, 1, 60, 9, 0},
              {100, 1, 1, 61, 2, 100},
              {200, 1, 2, 70, 3, 100}},
             store);
}

void test_existing_identities_are_never_overwritten_and_the_source_stays_idle() {
  LoopEventStore store;
  fill(store, {on(0, 1, 60, 7), off(10, 1, 60, 7), on(20, 2, 61, 8)});
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  IdCounter counter;
  const RecordPreparation::NoteIdSource source{&counter, takeNextId};
  const auto result = runPrepared(store, geometry, 3, true, &source);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(0, result.assigned);
  TEST_ASSERT_EQUAL_UINT32(0, counter.taken);
  expectRows({{0, 1, 1, 60, 7, 100}, {10, 0, 1, 60, 7, 0}, {20, 1, 2, 61, 8, 100}}, store);
}

void test_an_invalid_source_id_denies_instead_of_assigning() {
  LoopEventStore store;
  fill(store, {on(0, 1, 60, kInvalidNoteId)});
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  IdCounter counter;
  counter.next = kInvalidNoteId;
  const RecordPreparation::NoteIdSource source{&counter, takeNextId};
  const auto result = runPrepared(store, geometry, 3, true, &source);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Denied), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL(static_cast<int>(DenyReason::IdentityIncomplete),
                    static_cast<int>(result.deny));
}

void test_legacy_differential_covers_completed_identities() {
  std::vector<MidiEvent> rows = {
      on(300, 1, 60, kInvalidNoteId), off(310, 1, 60, 0),
      on(0, 2, 61, kInvalidNoteId), off(20, 2, 61, 0),
      on(1900, 3, 62, kInvalidNoteId), MidiEvent::ControlChange(50, 1, 7, 5)};
  LoopEventStore store;
  fill(store, rows);
  Geometry geometry;
  geometry.loopLengthTicks = 2000;
  geometry.minLengthTicks = 30;
  geometry.minLengthRemoveEnabled = true;
  IdCounter counter;
  const RecordPreparation::NoteIdSource source{&counter, takeNextId};
  const auto result = runPrepared(store, geometry, 5, true, &source);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(3, result.assigned);
  NoteId referenceNext = 1;
  const auto expected = legacyReferenceWithIds(rows, 2000, UINT32_MAX, 768, 30, true,
                                               &referenceNext);
  expectInventory(expected, store);
}

void test_store_assign_private_note_id_is_fail_closed() {
  LoopEventStore store;
  fill(store, {on(0, 1, 60, kInvalidNoteId), off(5, 1, 60, 9),
               MidiEvent::ControlChange(9, 1, 7, 1)});
  LoopEventStoreFilterCursor cursor;
  TEST_ASSERT_TRUE(store.beginPrivateFilter(cursor));
  LoopEventStoreFilterResult result = LoopEventStoreFilterResult::Pending;
  while (result == LoopEventStoreFilterResult::Pending) {
    result = store.stepPrivateFilter(cursor, 7);
  }
  TEST_ASSERT_EQUAL(static_cast<int>(LoopEventStoreFilterResult::InputReady),
                    static_cast<int>(result));
  TEST_ASSERT_TRUE(store.assignPrivateNoteId(cursor, 0, 42));
  MidiEvent row{};
  TEST_ASSERT_TRUE(store.readPrivateFilterRow(cursor, 0, row));
  TEST_ASSERT_EQUAL_UINT32(42, row.noteId);
  TEST_ASSERT_FALSE(store.assignPrivateNoteId(cursor, 0, 43));   // identity already present
  TEST_ASSERT_FALSE(store.assignPrivateNoteId(cursor, 1, 43));   // NoteOff
  TEST_ASSERT_FALSE(store.assignPrivateNoteId(cursor, 2, 43));   // ControlChange
  TEST_ASSERT_FALSE(store.assignPrivateNoteId(cursor, 3, 43));   // no such row
  TEST_ASSERT_FALSE(store.assignPrivateNoteId(cursor, 0, kInvalidNoteId));
  TEST_ASSERT_TRUE(store.readPrivateFilterRow(cursor, 0, row));
  TEST_ASSERT_EQUAL_UINT32(42, row.noteId);
  cursor.discard();
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_passthrough_preserves_canonical_content_and_order);  RUN_TEST(test_unsorted_input_is_canonically_ordered);
  RUN_TEST(test_tail_repair_emits_and_reorders_synthetic_off);
  RUN_TEST(test_close_tick_branch_and_wrapped_pair);
  RUN_TEST(test_minimum_length_pair_removal_and_disable);
  RUN_TEST(test_repair_and_minimum_compose_on_the_appended_stream);
  RUN_TEST(test_long_dense_take_beyond_overdub_staging);
  RUN_TEST(test_empty_and_zero_length_terminals);
  RUN_TEST(test_identity_incomplete_and_missing_reservation_deny);
  RUN_TEST(test_missing_identities_are_completed_in_canonical_order);
  RUN_TEST(test_existing_identities_are_never_overwritten_and_the_source_stays_idle);
  RUN_TEST(test_an_invalid_source_id_denies_instead_of_assigning);
  RUN_TEST(test_legacy_differential_covers_completed_identities);
  RUN_TEST(test_store_assign_private_note_id_is_fail_closed);
  RUN_TEST(test_stale_preparation_generation_denies);
  RUN_TEST(test_bounded_units_and_no_directory_growth);
  RUN_TEST(test_differential_against_legacy_store_policy);
  return UNITY_END();
}
