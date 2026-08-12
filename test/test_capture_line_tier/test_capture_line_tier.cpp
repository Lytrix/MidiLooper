#include <unity.h>

#include "Utils/CaptureLineTier.h"

void setUp() {}
void tearDown() {}

// The parse skipped two commas and landed a field past the tag, so every Tier-A prefix
// failed to match and the ring evicted protected lines (session_20260812_144323 lost all
// DIAG envelope windows between 32.8s and 304.1s).
void test_tag_is_the_field_after_micros() {
  TEST_ASSERT_EQUAL_STRING("DIAG,msi,27834,1",
                           CaptureLineTier::tagOf("#CAP,6701437,DIAG,msi,27834,1"));
  TEST_ASSERT_EQUAL_STRING("ST,Track,STOPPED,ARMED",
                           CaptureLineTier::tagOf("#CAP,21524068,ST,Track,STOPPED,ARMED"));
}

void test_envelope_lines_are_tier_a() {
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701437,DIAG,msi,27834,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701449,DIAG,midisvc,9,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701454,DIAG,clk,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701459,DIAG,tracks,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701465,DIAG,clockrate,0"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,328705775,DIAG,timing_max,PlaybackBuildTime,74729"));
}

void test_transport_and_persistence_lines_are_tier_a() {
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,21524068,ST,Track,STOPPED,ARMED"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,22800000,PERS,queue,1,2,3,ok"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,22800000,RECS,stop,1,0,0,0,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,7800000,HDR,v1"));
}

void test_note_and_display_traffic_is_not_tier_a() {
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,329644701,MI,U,128,4,12,64"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,329905674,MO,128,1,81,0"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,328670396,SEVT,F,48768,4,89"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,329537471,DFRAME,287,11683,18420"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,329501020,BPM,119.637,119.303"));
}

void test_other_diag_subtags_are_not_tier_a() {
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,100,DIAG,counter,appendDeny,4"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,100,DIAG,reclaim,1,2,3,4,5,ok,6"));
}

// The micros field grows over a long session; the tag offset must not be assumed fixed.
void test_tag_parse_survives_a_wide_micros_field() {
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,7,DIAG,clockrate,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,12345678901,DIAG,clockrate,48"));
}

void test_malformed_lines_are_not_tier_a() {
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine(nullptr));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine(""));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("[331.585] MIDI clock lost"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,6701437"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tag_is_the_field_after_micros);
  RUN_TEST(test_envelope_lines_are_tier_a);
  RUN_TEST(test_transport_and_persistence_lines_are_tier_a);
  RUN_TEST(test_note_and_display_traffic_is_not_tier_a);
  RUN_TEST(test_other_diag_subtags_are_not_tier_a);
  RUN_TEST(test_tag_parse_survives_a_wide_micros_field);
  RUN_TEST(test_malformed_lines_are_not_tier_a);
  return UNITY_END();
}
