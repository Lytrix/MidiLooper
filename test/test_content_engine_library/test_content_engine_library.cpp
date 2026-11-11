//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sets browser slice 1c-i (DEC-258): the milestone library's on-card format — the A / B index header and which one is
// current, index records, blob and checkpoint headers, every structure rejected when torn, the capacity refusals, and
// per-stage numbering within a set.

#include <unity.h>

#include <cstring>

#include "ContentEngine/Storage/SetLibraryFormat.h"

using namespace ContentEngine;
using namespace ContentEngine::library;

void setUp() {}
void tearDown() {}

void test_the_layout_fits_the_bound() {
  TEST_ASSERT_EQUAL_UINT32(4300, kMaxSetSectors);
  TEST_ASSERT_EQUAL_UINT32(514, kCheckpointBase);
  TEST_ASSERT_EQUAL_UINT32(514 + 2 * 4301, kLogBase);
  TEST_ASSERT_EQUAL_UINT32(kSectors, kLogBase + kLogSectors);
  TEST_ASSERT_TRUE(kLogSectors / kMaxSetSectors >= 485);
}

void test_the_newer_valid_index_header_is_current() {
  uint8_t a[kSector], b[kSector];
  uint8_t which = 9;
  std::memset(a, 0xFF, sizeof a);
  std::memset(b, 0, sizeof b);
  IndexHeader h = currentHeader(a, b, which);  // a blank file: empty, the first header to sector 0
  TEST_ASSERT_EQUAL_UINT16(0, h.count);
  TEST_ASSERT_EQUAL_UINT8(1, which);
  encode(IndexHeader{4, 3, 120, 9}, a);
  encode(IndexHeader{5, 4, 160, 10}, b);
  h = currentHeader(a, b, which);
  TEST_ASSERT_EQUAL_UINT32(5, h.sequence);
  TEST_ASSERT_EQUAL_UINT16(4, h.count);
  TEST_ASSERT_EQUAL_UINT32(160, h.appendSector);
  TEST_ASSERT_EQUAL_UINT8(1, which);
  b[13] ^= 1;  // the newer one torn: the older one stands
  h = currentHeader(a, b, which);
  TEST_ASSERT_EQUAL_UINT32(4, h.sequence);
  TEST_ASSERT_EQUAL_UINT8(0, which);
}

/// DEC-269: the working set rides after the header's CRC with its own: round-trips; a header without it (older
/// firmware) or with it damaged is still valid and names none.
void test_the_header_names_the_working_set_separately() {
  uint8_t s[kSector];
  IndexHeader h{7, 3, 120, 9};
  h.hasWorking = true;
  h.workingSet = 105;
  h.workingBase = 0;  // an empty working set: no base
  encode(h, s);
  IndexHeader back;
  TEST_ASSERT_TRUE(decode(s, back));
  TEST_ASSERT_TRUE(back.hasWorking);
  TEST_ASSERT_EQUAL_UINT16(105, back.workingSet);
  TEST_ASSERT_EQUAL_UINT32(0, back.workingBase);
  TEST_ASSERT_EQUAL_UINT16(3, back.count);
  s[29] ^= 1;  // damage in the working block only
  TEST_ASSERT_TRUE(decode(s, back));
  TEST_ASSERT_FALSE(back.hasWorking);
  TEST_ASSERT_EQUAL_UINT16(0, back.workingSet);
  TEST_ASSERT_EQUAL_UINT32(9, back.nextSequence);
  encode(IndexHeader{7, 3, 120, 9}, s);  // as older firmware writes it
  TEST_ASSERT_TRUE(decode(s, back));
  TEST_ASSERT_FALSE(back.hasWorking);
  for (uint32_t i = 24; i < 38; ++i) TEST_ASSERT_EQUAL_UINT8(0, s[i]);
}

void test_records_round_trip_and_reject_damage() {
  Record r;
  r.sequence = 17;
  r.set = 2;
  r.stage = Stage::Take;
  r.stageNumber = 4;
  r.parent = 12;
  r.wallTime = 1794175000;
  r.tempoTenths = 1205;
  r.loops = 23;
  r.shortestLoop = 768;
  r.longestLoop = 12288;
  r.logSector = 900;
  r.logSectors = 37;
  r.mosaic[3] = 0xE4;
  uint8_t b[kRecordBytes];
  encode(r, b);
  Record d;
  TEST_ASSERT_TRUE(decode(b, d));
  TEST_ASSERT_EQUAL_UINT32(17, d.sequence);
  TEST_ASSERT_EQUAL_UINT16(2, d.set);
  TEST_ASSERT_EQUAL(Stage::Take, d.stage);
  TEST_ASSERT_EQUAL_UINT8(4, d.stageNumber);
  TEST_ASSERT_EQUAL_UINT32(12, d.parent);
  TEST_ASSERT_EQUAL_UINT32(1794175000, d.wallTime);
  TEST_ASSERT_EQUAL_UINT16(1205, d.tempoTenths);
  TEST_ASSERT_EQUAL_UINT32(12288, d.longestLoop);
  TEST_ASSERT_EQUAL_UINT32(37, d.logSectors);
  TEST_ASSERT_EQUAL_HEX8(0xE4, d.mosaic[3]);
  b[20] ^= 4;
  TEST_ASSERT_FALSE(decode(b, d));
  std::memset(b, 0, sizeof b);  // an unwritten record
  TEST_ASSERT_FALSE(decode(b, d));
  TEST_ASSERT_EQUAL_UINT32(2, recordSector(0));
  TEST_ASSERT_EQUAL_UINT32(3, recordSector(8));
  TEST_ASSERT_EQUAL_UINT32(64, recordOffset(9));
}

void test_blob_and_checkpoint_headers() {
  BlobHeader h;
  h.sequence = 17;
  h.noteIdHigh = 4000;
  h.tempoTenths = 1200;
  h.surface[0] = 3;
  h.slotBytes[0] = 1000;
  h.slotBytes[9] = 512;
  h.slotBytes[63] = 1;
  uint8_t s[kSector];
  encode(h, s);
  BlobHeader d;
  TEST_ASSERT_TRUE(decode(s, d));
  TEST_ASSERT_EQUAL_UINT32(1000, d.slotBytes[0]);
  TEST_ASSERT_EQUAL_UINT32(1, d.slotBytes[63]);
  TEST_ASSERT_EQUAL_UINT8(3, d.surface[0]);
  TEST_ASSERT_EQUAL_UINT32(1 + 2 + 1 + 1, blobSectors(d.slotBytes));  // header, 1000 B → 2, 512 → 1, 1 → 1
  s[100] ^= 1;
  TEST_ASSERT_FALSE(decode(s, d));
  uint8_t a[kSector], b[kSector];
  std::memset(a, 0, sizeof a);
  std::memset(b, 0, sizeof b);
  CheckpointHeader c;
  uint8_t region = 9;
  TEST_ASSERT_FALSE(currentCheckpoint(a, b, c, region));
  TEST_ASSERT_EQUAL_UINT8(1, region);  // the first checkpoint goes to region 0
  encode(CheckpointHeader{3, 1794170000, 12, 2, 20, 41}, a);
  TEST_ASSERT_TRUE(currentCheckpoint(a, b, c, region));
  TEST_ASSERT_EQUAL_UINT8(0, region);
  encode(CheckpointHeader{4, 1794175000, 17, 7, 30, 55}, b);  // the next one, into region 1
  TEST_ASSERT_TRUE(currentCheckpoint(a, b, c, region));
  TEST_ASSERT_EQUAL_UINT32(4, c.sequence);
  TEST_ASSERT_EQUAL_UINT8(1, region);
  b[10] ^= 1;  // torn before its header was whole: the previous checkpoint stands
  TEST_ASSERT_TRUE(currentCheckpoint(a, b, c, region));
  TEST_ASSERT_EQUAL_UINT32(3, c.sequence);
  TEST_ASSERT_EQUAL_UINT8(0, region);
  TEST_ASSERT_EQUAL_UINT32(kCheckpointBase + kCheckpointSectors, checkpointBase(1));
}

void test_a_save_that_does_not_fit_is_refused_by_name() {
  IndexHeader h;
  TEST_ASSERT_EQUAL(Fit::Ok, fits(h, 10));
  TEST_ASSERT_EQUAL(Fit::TooLarge, fits(h, kMaxSetSectors + 1));
  h.appendSector = kLogSectors - 9;
  TEST_ASSERT_EQUAL(Fit::LogFull, fits(h, 10));
  TEST_ASSERT_EQUAL(Fit::Ok, fits(h, 9));
  h.appendSector = 0;
  h.count = kIndexRecords;
  TEST_ASSERT_EQUAL(Fit::IndexFull, fits(h, 1));
}

void test_stage_numbers_count_per_stage_within_a_set() {
  Record r[5];
  r[0].set = 2, r[0].stage = Stage::Take, r[0].stageNumber = 1;
  r[1].set = 2, r[1].stage = Stage::Building, r[1].stageNumber = 4;
  r[2].set = 2, r[2].stage = Stage::Take, r[2].stageNumber = 3;
  r[3].set = 3, r[3].stage = Stage::Take, r[3].stageNumber = 9;  // another set
  r[4].set = 2, r[4].stage = Stage::Arrangement, r[4].stageNumber = 1;
  TEST_ASSERT_EQUAL_UINT8(4, nextStageNumber(r, 5, 2, Stage::Take));  // B04 used no Take number
  TEST_ASSERT_EQUAL_UINT8(5, nextStageNumber(r, 5, 2, Stage::Building));
  TEST_ASSERT_EQUAL_UINT8(1, nextStageNumber(r, 5, 2, Stage::Final));
  TEST_ASSERT_EQUAL_UINT8(1, nextStageNumber(r, 5, 4, Stage::Take));
  TEST_ASSERT_EQUAL('T', stageLetter(Stage::Take));
  TEST_ASSERT_EQUAL_STRING("ARRANGEMENT", stageName(Stage::Arrangement));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_layout_fits_the_bound);
  RUN_TEST(test_the_newer_valid_index_header_is_current);
  RUN_TEST(test_the_header_names_the_working_set_separately);
  RUN_TEST(test_records_round_trip_and_reject_damage);
  RUN_TEST(test_blob_and_checkpoint_headers);
  RUN_TEST(test_a_save_that_does_not_fit_is_refused_by_name);
  RUN_TEST(test_stage_numbers_count_per_stage_within_a_set);
  return UNITY_END();
}
