// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
//
// S4f.18 — bounded Record source staging. Copies a real frozen CaptureRange into
// a prewarmed private output through the production per-row stop projection, one
// bounded unit at a time, and pins the denial and bound behaviour.
#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/RecordSourceStaging.cpp"

using RecordSourceStaging::Cursor;
using RecordSourceStaging::DenyReason;
using RecordSourceStaging::Step;
using Row = std::tuple<uint32_t, uint8_t, uint8_t, uint32_t>;

static Row rowOf(const MidiEvent& e) {
  return {e.tick, e.channel, e.data.noteData.note, static_cast<uint32_t>(e.noteId)};
}

static std::vector<Row> rowsOf(LoopEventStore& store) {
  std::vector<Row> rows;
  for (size_t i = 0; i < store.size(); ++i) {
    rows.push_back(rowOf(store.at(i)));
  }
  return rows;
}

static MidiEvent noteOn(uint32_t tick, uint8_t channel, uint8_t note, uint32_t id,
                        uint8_t velocity = 100) {
  auto e = MidiEvent::NoteOn(tick, channel, note, velocity);
  e.noteId = id;
  return e;
}

static MidiEvent noteOff(uint32_t tick, uint8_t channel, uint8_t note, uint32_t id) {
  auto e = MidiEvent::NoteOff(tick, channel, note, 0);
  e.noteId = id;
  return e;
}

static void buildSource(LoopEventStore& producer, LoopEventStore::CaptureRange& range,
                        const std::vector<MidiEvent>& rows) {
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(range.reserveTail());
  for (const auto& row : rows) {
    TEST_ASSERT_TRUE(producer.append(row));
  }
  TEST_ASSERT_TRUE(producer.sealCaptureRange(range));
}

static void prepareOutput(LoopEventStore& output, LoopEventStore::ChunkReservation& credits,
                          size_t chunks = 8) {
  TEST_ASSERT_TRUE(output.prewarmCaptureDirectory());
  TEST_ASSERT_TRUE(credits.reserveChunks(chunks));
}

struct Result {
  Step step = Step::Denied;
  size_t units = 0;
  size_t copied = 0;
  size_t projected = 0;
  size_t dropped = 0;
  DenyReason deny = DenyReason::None;
};

static Result run(Cursor& cursor, size_t quantum) {
  Result result;
  for (size_t turn = 0; turn < 20000; ++turn) {
    const Step step = cursor.step(quantum);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<uint32_t>(quantum),
                                     static_cast<uint32_t>(cursor.lastStepUnits()));
    result.units += cursor.lastStepUnits();
    if (step == Step::Pending) {
      continue;
    }
    result.step = step;
    result.copied = cursor.copiedRows();
    result.projected = cursor.projectedRows();
    result.dropped = cursor.droppedRows();
    result.deny = cursor.denyReason();
    return result;
  }
  TEST_FAIL_MESSAGE("source staging did not terminate");
  return result;
}

void setUp() { LoopEventStore::resetPoolForTests(); }
void tearDown() {}

void test_in_loop_rows_are_copied_in_order_without_projection() {
  LoopEventStore producer, output;
  LoopEventStore::CaptureRange range;
  LoopEventStore::ChunkReservation credits;
  buildSource(producer, range,
              {noteOn(0, 1, 60, 101), MidiEvent::ControlChange(20, 1, 7, 9),
               noteOff(60, 1, 60, 7), noteOn(99, 2, 62, 90)});
  prepareOutput(output, credits);
  Cursor cursor;
  TEST_ASSERT_TRUE(cursor.begin(range, output, credits, 100));
  const auto result = run(cursor, 3);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(4, result.copied);
  TEST_ASSERT_EQUAL_UINT32(0, result.projected);
  TEST_ASSERT_EQUAL_UINT32(0, result.dropped);
  const std::vector<Row> expected = {{0, 1, 60, 101}, {20, 1, 7, 0}, {60, 1, 60, 7},
                                     {99, 2, 62, 90}};
  TEST_ASSERT_TRUE(rowsOf(output) == expected);
  // The staged rows are private: nothing scheduled persistence for them.
  TEST_ASSERT_TRUE(output.chunkIds().size() > 0);
  for (uint16_t id : output.chunkIds()) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::NotScheduled),
                      static_cast<int>(PersistenceQueue::chunkState(id)));
  }
}

void test_overflow_note_on_is_remapped_and_other_overflow_is_dropped() {
  LoopEventStore producer, output;
  LoopEventStore::CaptureRange range;
  LoopEventStore::ChunkReservation credits;
  buildSource(producer, range,
              {noteOn(150, 1, 60, 1),        // 150 -> 50
               noteOff(150, 1, 60, 2),       // dropped: not a NoteOn
               noteOn(100, 1, 61, 3),        // 100 -> 0
               MidiEvent::ControlChange(20, 1, 7, 4),
               noteOn(20, 2, 63, 5)});       // in loop
  prepareOutput(output, credits);
  Cursor cursor;
  TEST_ASSERT_TRUE(cursor.begin(range, output, credits, 100));
  const auto result = run(cursor, 2);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(4, result.copied);
  TEST_ASSERT_EQUAL_UINT32(2, result.projected);
  TEST_ASSERT_EQUAL_UINT32(1, result.dropped);
  const std::vector<Row> expected = {{50, 1, 60, 1}, {0, 1, 61, 3}, {20, 1, 7, 0},
                                     {20, 2, 63, 5}};
  TEST_ASSERT_TRUE(rowsOf(output) == expected);
}

void test_zero_length_copies_every_row_unchanged() {
  LoopEventStore producer, output;
  LoopEventStore::CaptureRange range;
  LoopEventStore::ChunkReservation credits;
  buildSource(producer, range, {noteOn(500, 1, 60, 5), noteOff(500, 1, 60, 6)});
  prepareOutput(output, credits);
  Cursor cursor;
  TEST_ASSERT_TRUE(cursor.begin(range, output, credits, 0));
  const auto result = run(cursor, 7);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL_UINT32(2, result.copied);
  TEST_ASSERT_EQUAL_UINT32(0, result.dropped);
  const std::vector<Row> expected = {{500, 1, 60, 5}, {500, 1, 60, 6}};
  TEST_ASSERT_TRUE(rowsOf(output) == expected);
}

void test_empty_source_reports_empty_and_all_dropped_reports_complete() {
  {
    LoopEventStore producer, output;
    LoopEventStore::CaptureRange range;
    LoopEventStore::ChunkReservation credits;
    buildSource(producer, range, {});
    prepareOutput(output, credits);
    Cursor cursor;
    TEST_ASSERT_TRUE(cursor.begin(range, output, credits, 100));
    const auto result = run(cursor, 7);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Empty), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(0, output.size());
  }
  {
    LoopEventStore producer, output;
    LoopEventStore::CaptureRange range;
    LoopEventStore::ChunkReservation credits;
    buildSource(producer, range, {MidiEvent::NoteOff(150, 1, 60, 0),
                                  MidiEvent::ControlChange(200, 1, 7, 1)});
    prepareOutput(output, credits);
    Cursor cursor;
    TEST_ASSERT_TRUE(cursor.begin(range, output, credits, 100));
    const auto result = run(cursor, 7);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(0, result.copied);
    TEST_ASSERT_EQUAL_UINT32(2, result.dropped);
    TEST_ASSERT_EQUAL_UINT32(0, output.size());
  }
}

void test_a_stale_source_generation_denies() {
  LoopEventStore producer, output;
  LoopEventStore::CaptureRange range;
  LoopEventStore::ChunkReservation credits;
  buildSource(producer, range, {MidiEvent::NoteOn(10, 1, 60, 100),
                                MidiEvent::NoteOn(20, 1, 61, 101)});
  prepareOutput(output, credits);
  Cursor cursor;
  TEST_ASSERT_TRUE(cursor.begin(range, output, credits, 100));
  TEST_ASSERT_TRUE(range.releaseGrain(range.chunkCount()));
  const auto result = run(cursor, 1);
  TEST_ASSERT_EQUAL(static_cast<int>(Step::Denied), static_cast<int>(result.step));
  TEST_ASSERT_EQUAL(static_cast<int>(DenyReason::SourceUnavailable),
                    static_cast<int>(result.deny));
}

void test_unprepared_output_and_missing_credits_deny() {
  {
    LoopEventStore producer, output;
    LoopEventStore::CaptureRange range;
    LoopEventStore::ChunkReservation credits;
    buildSource(producer, range, {MidiEvent::NoteOn(10, 1, 60, 100)});
    // No prewarmCaptureDirectory / no credits.
    Cursor cursor;
    TEST_ASSERT_FALSE(cursor.begin(range, output, credits, 100));
    TEST_ASSERT_EQUAL(static_cast<int>(DenyReason::OutputUnavailable),
                      static_cast<int>(cursor.denyReason()));
  }
  {
    LoopEventStore producer, output;
    LoopEventStore::CaptureRange range;
    LoopEventStore::ChunkReservation credits;
    std::vector<MidiEvent> rows;
    for (uint32_t i = 0; i < 300; ++i) {
      rows.push_back(MidiEvent::NoteOn(i, 1, 60, 100));
    }
    buildSource(producer, range, rows);
    prepareOutput(output, credits, 1);  // one chunk of credits for two chunks of rows
    Cursor cursor;
    TEST_ASSERT_TRUE(cursor.begin(range, output, credits, 1000));
    const auto result = run(cursor, 7);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Denied), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL(static_cast<int>(DenyReason::OutputUnavailable),
                      static_cast<int>(result.deny));
  }
}

void test_bounded_units_and_no_directory_growth() {
  std::vector<MidiEvent> rows;
  for (uint32_t i = 0; i < 600; ++i) {
    rows.push_back(MidiEvent::NoteOn(i, 1, static_cast<uint8_t>(i % 128), 100));
  }
  for (const size_t quantum : {size_t(1), size_t(7), size_t(64)}) {
    LoopEventStore::resetPoolForTests();
    LoopEventStore producer, output;
    LoopEventStore::CaptureRange range;
    LoopEventStore::ChunkReservation outCredits;
    buildSource(producer, range, rows);
    prepareOutput(output, outCredits);
    const auto capacity = output.chunkIds().capacity();
    const auto before = LoopEventStore::usedChunkCount();
    Cursor cursor;
    TEST_ASSERT_TRUE(cursor.begin(range, output, outCredits, 1000));
    const auto result = run(cursor, quantum);
    TEST_ASSERT_EQUAL(static_cast<int>(Step::Complete), static_cast<int>(result.step));
    TEST_ASSERT_EQUAL_UINT32(600, result.copied);
    TEST_ASSERT_EQUAL_UINT32(capacity, output.chunkIds().capacity());
    TEST_ASSERT_EQUAL_UINT32(before, LoopEventStore::usedChunkCount());
    // 600 rows fill real chunks; the ephemeral seal must keep every one private.
    TEST_ASSERT_TRUE(output.chunkIds().size() > 2);
    for (uint16_t id : output.chunkIds()) {
      TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::NotScheduled),
                        static_cast<int>(PersistenceQueue::chunkState(id)));
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_in_loop_rows_are_copied_in_order_without_projection);
  RUN_TEST(test_overflow_note_on_is_remapped_and_other_overflow_is_dropped);
  RUN_TEST(test_zero_length_copies_every_row_unchanged);
  RUN_TEST(test_empty_source_reports_empty_and_all_dropped_reports_complete);
  RUN_TEST(test_a_stale_source_generation_denies);
  RUN_TEST(test_unprepared_output_and_missing_credits_deny);
  RUN_TEST(test_bounded_units_and_no_directory_growth);
  return UNITY_END();
}
