#include <unity.h>

#include "Utils/CaptureLineTier.h"

void setUp() {}
void tearDown() {}

// The parse skipped two commas and landed a field past the tag, so every Tier-A prefix
// failed to match and the ring evicted protected lines (session_20260812_144323 lost all
// DIAG envelope windows between 32.8s and 304.1s).
void test_tag_is_the_field_after_micros() {
  TEST_ASSERT_EQUAL_STRING("DIAG,midi_gap,27834,1",
                           CaptureLineTier::tagOf("#CAP,6701437,DIAG,midi_gap,27834,1"));
  TEST_ASSERT_EQUAL_STRING("DIAG,msi,27834,1",
                           CaptureLineTier::tagOf("#CAP,6701437,DIAG,msi,27834,1"));
  TEST_ASSERT_EQUAL_STRING("ST,Track,STOPPED,ARMED",
                           CaptureLineTier::tagOf("#CAP,21524068,ST,Track,STOPPED,ARMED"));
}

void test_timing_telemetry_lines_are_tier_a() {
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701437,DIAG,midi_gap,27834,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701449,DIAG,midi_input,9,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701437,DIAG,msi,27834,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701449,DIAG,midisvc,9,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701454,DIAG,clk,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701459,DIAG,tracks,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701460,DIAG,usbdev,221000,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701461,DIAG,din,40,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701462,DIAG,hosttask,1200,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701463,DIAG,hostdrain,800,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701464,DIAG,usbread,40,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701465,DIAG,usbdisp,154000,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701466,DIAG,usbcap,12000,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701467,DIAG,usbthru,8000,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701468,DIAG,usbclk,90000,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701469,DIAG,usbnote,200,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701470,DIAG,usbcc,80,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701471,DIAG,usbtrans,40,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701472,DIAG,noteappend,300,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701473,DIAG,notechg,90000,2"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701474,DIAG,noterecon,9000,2"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701475,DIAG,notepair,110,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701465,DIAG,clockrate,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,39819493,DIAG,idle_maint,1200,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,39819493,DIAG,load_frame,3981504,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,39819493,DIAG,persist_save,80,0"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,39804843,DIAG,loop_rem,load_frame,3981504,0,4,2,0"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,39804843,DIAG,loop_rem,display_frame,64000,0,7,0,0"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,96725087,DIAG,loop_rem,idle_gather,60000,255,255,255,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,96725087,DIAG,loop_rem,idle_reconstruct,80000,255,255,255,0"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,96725087,DIAG,loop_rem,idle_append,50000,255,255,255,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,18121016,DIAG,stored_notes,track,4,slot,0,notes,1844,unique,1844,max_same_pitch,12"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,18121016,DIAG,overlap_hold,note_offs,3,empty_sets,1,max_ids,4,overflows,0,"
      "looked_up,2,max_examined,30,sum_examined,40,max_lookup_us,12,sum_lookup_us,18,"
      "add,3,shorten,1,hide,0"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,328705775,DIAG,timing_max,PlaybackBuildTime,74729"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701476,DIAG,late_on,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701477,DIAG,late_off,1200,2"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,late_clk,4000,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701479,DIAG,late_event,on,5000,192"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,6701480,DIAG,playback_build,12345,0,384,9"));
}

void test_transport_and_persistence_lines_are_tier_a() {
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,21524068,ST,Track,STOPPED,ARMED"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,22800000,PERS,queue,1,2,3,ok"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,22800000,RECS,stop,1,0,0,0,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,7800000,HDR,v1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,330913469,VCACHE,full,ev,1232,notes,299,first,21,last,37,total,77,dsz,77,dcnt,0,dirty,0"));
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

void test_tag_starts_with_matches_led_mo_diag() {
  TEST_ASSERT_TRUE(CaptureLineTier::tagStartsWith("#CAP,5388757,LED,1,40,127", "LED,"));
  TEST_ASSERT_TRUE(CaptureLineTier::tagStartsWith("#CAP,4683903,MO,176,7,123,0", "MO,"));
  TEST_ASSERT_TRUE(CaptureLineTier::tagStartsWith("#CAP,5416701,DIAG,midi_gap,28081,1", "DIAG,"));
  TEST_ASSERT_FALSE(CaptureLineTier::tagStartsWith("#CAP,5460688,VCACHE,stale_all,ev,-1", "LED,"));
  TEST_ASSERT_FALSE(CaptureLineTier::tagStartsWith("#CAP,BOOT,scan,t0", "DIAG,"));
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
  RUN_TEST(test_timing_telemetry_lines_are_tier_a);
  RUN_TEST(test_transport_and_persistence_lines_are_tier_a);
  RUN_TEST(test_note_and_display_traffic_is_not_tier_a);
  RUN_TEST(test_other_diag_subtags_are_not_tier_a);
  RUN_TEST(test_tag_parse_survives_a_wide_micros_field);
  RUN_TEST(test_tag_starts_with_matches_led_mo_diag);
  RUN_TEST(test_malformed_lines_are_not_tier_a);
  return UNITY_END();
}
