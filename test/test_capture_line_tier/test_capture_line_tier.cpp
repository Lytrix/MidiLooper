#include <cstdio>
#include <cstring>
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
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,123,DIAG,capture_prewarm,track=0,slot=0,full=1,ready=1,credits=8,free_chunks=400,heap_free=90000"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,124,DIAG,capture_prewarm,phase=after,psram_free=3000000,psram_pool=8380416,heap_free=340000,free_chunks=448"));
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
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,39819493,DIAG,disp_res_cmt,22386803,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,39819493,DIAG,lframe_reclaim,8200,513"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,35897060,DIAG,disp_win,us=26727786,gather=26368771,recon=44867,append=313376,"
      "filter=772,ev=2093,notes=1094,ws=0,wl=13824,cap=1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,35897060,DIAG,cer_range,us=34568381,collect=40,append=12000,sort=900,lists=106,"
      "evout=2087"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,35897060,DIAG,cer_cap,us=34568381,committed=12000,sort=34000000,copy=400000,"
      "filter=9000,merge=1200,capflat=98000,capwin=2087,evout=2087"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,35897060,DIAG,disp_full,us=12000,gather=11000,recon=800,tails=200,ev=2093,notes=1094,"
      "len=52224"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,35897060,DIAG,cer_win,us=26368771,span=120,copy=26000000,filter=300000,lists=4,"
      "chunks=900,nospan=0,outside=700,copied=200,evcopy=6000,evout=2093"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,6701480,DIAG,cer_heap,recon=334201,ihit=24,spill=0,sbytes=0,clk=0,iclk=0,ext=1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,6701481,DIAG,cer_recon,recon=339941,filt=1200,span=200000,proj=120000,itick=12,"
      "itickus=80"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,39804843,DIAG,loop_rem,load_frame,3981504,0,4,2,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,stop_phase,4,0,2,1,0,0,3,1,0,100,130,20"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,stop_grain,4,0,2,1,0,0,93,100,130,90,90,110,20,10,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,stop_state,4,0,6,7,93,100,93,90,110,20"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,stop_service,94,120,110,130,20"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,stop_emit,94,off,48,121,110,130,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,stop_trace_drop,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,late_event,off,10,42,250,200,240"));
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
  char overlapHoldWorst[192];
  const int overlapHoldWorstLen = snprintf(
      overlapHoldWorst, sizeof(overlapHoldWorst),
      "#CAP,%lu,DIAG,oh,n,%lu,e,%lu,i,%lu,o,%lu,k,%lu,x,%lu,s,%lu,u,%lu,t,%lu,"
      "add,%lu,shorten,%lu,hide,%lu",
      4294967295UL, 4294967295UL, 4294967295UL, 4294967295UL, 4294967295UL, 4294967295UL,
      4294967295UL, 4294967295UL, 4294967295UL, 4294967295UL, 4294967295UL, 4294967295UL,
      4294967295UL);
  TEST_ASSERT_TRUE(overlapHoldWorstLen > 0);
  TEST_ASSERT_TRUE(overlapHoldWorstLen < 192);
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(overlapHoldWorst));
  TEST_ASSERT_NOT_NULL(strstr(overlapHoldWorst, ",hide,4294967295"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,328705775,DIAG,timing_max,PlaybackBuildTime,74729"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701476,DIAG,late_on,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701477,DIAG,late_off,1200,2"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,late_clk,4000,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,extclk_arrival,1200,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,extclk_dispatch,6100,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,clk_batch,8,3"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,service_period,20833,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,usb_batch,12,1,8,5"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,6701478,DIAG,usb_bkt_size,10,2,0,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,usb_bkt_clk,3,1,0,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,usb_bkt_disp,0,6100,0,0,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,track_rebuild,4466,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,track_order,1200,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,track_send,800,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,track_slot,2100,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701478,DIAG,track_resid,400,0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,6701479,DIAG,late_event,on,5000,192"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine(
          "#CAP,6701480,DIAG,playback_build,compatibility,12345,0,384,9,3,1,2"));
  TEST_ASSERT_FALSE(
      CaptureLineTier::isTierALine(
          "#CAP,6701480,DIAG,playback_build,hold_empty,6,0,1536,1,6,0,1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,6701480,DIAG,playback_build,incremental,0,0,8448,4,4,0,1"));
  TEST_ASSERT_TRUE(
      CaptureLineTier::isTierALine("#CAP,57711324,DIAG,playback_order,2100,512"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,6701481,DIAG,snapshot_alloc,nodes=163,bytes=22000,slabs=1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,6701482,DIAG,snapshot_unit,kind=finish,ph=2,ld=1,p=1,q=1,a=0,"
      "if=1,hb=12000,ha=11800,psram=4294967295,n=1754,b=360000,s=12"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,6701483,DIAG,snapshot_slab_fail,site=slab_open,bytes=32768,nodes=1754,slabs=12"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,DIAG,boot_reset,crash=1"));
  // S1.3/S2b.3 post-restore census checkpoint: a one-shot boot marker the slope run pins the pool
  // figure to. It must be Tier-A, and it must be the timestamped form (`#CAP,<micros>,DIAG,...`),
  // which is also what makes `tagOf` read `DIAG,post_restore_census,...` rather than the micros field.
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,39819493,DIAG,post_restore_census,free_chunks=452,admission_complete=1,"
      "interactive_ready=1,undo_hydrated=1,transport_stopped=1,deferred_prewarm_idle=1,"
      "heap_free=77824"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,39819493,DIAG,post_restore_census,free_chunks=0,admission_complete=0"));
#if SESSION_CAPTURE_SNAPSHOT_IDLE
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,195718000,DIAG,snapshot_idle,us=79169,pending=1,root=0x0,root_rev=0,"
      "playback_rev=0,queued=3,applied=1,construction=4,exit=1,inflight=1,"
      "journals=0,fragment=0/0,parent_rev=0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,195718000,DIAG,snapshot_idle,child,start=79000,pass_change=1200,"
      "step=80,finish=40,publish=0,probe=1,enqueue=0,discard=0,emit=40"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,195718000,DIAG,snapshot_idle,inner,refill=200,event_scan=800,"
      "note_lookup=18000,change_build=400,construct=300,open=1"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,195718100,DIAG,snapshot_idle,reserve,start=195638831,enter=195718100,"
      "reserve_us=79269"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,195718200,DIAG,snapshot_idle,yield,remaining=0,margin=1000,admitted=0,"
      "cost=8000,crit=1,pending=1,root_rev=3"));
#else
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine(
      "#CAP,195718000,DIAG,snapshot_idle,us=79169,pending=1,root=0x0,root_rev=0,"
      "playback_rev=0,queued=3,applied=1,construction=4,exit=1,inflight=1,"
      "journals=0,fragment=0/0,parent_rev=0"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine(
      "#CAP,195718000,DIAG,snapshot_idle,child,start=79000,pass_change=1200,"
      "step=80,finish=40,publish=0,probe=1,enqueue=0,discard=0,emit=40"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine(
      "#CAP,195718000,DIAG,snapshot_idle,inner,refill=200,event_scan=800,"
      "note_lookup=18000,change_build=400,construct=300,open=1"));
#endif
}

void test_bridge_begin_denial_is_critical_capture_evidence() {
  TEST_ASSERT_TRUE(CaptureLineTier::isCriticalLine(
      "#CAP,500,ODUB,stop,bridge_begin,0,0,4294967295,4294967295,8192,1,denied"));
  TEST_ASSERT_FALSE(CaptureLineTier::isCriticalLine(
      "#CAP,500,ODUB,stop,bridge_base,0,0,4294967295,4294967295,1,1,present"));
}

void test_snapshot_idle_lines_fit_capture_text_budget() {
  // DebugSessionCapture kMaxCaptureTextBytes is 192; payload is 191 + NUL.
  // Parent uses observed-class maxima plus 10-digit micros. Child uses 6-digit
  // durations (above observed leftover) so nine fields still fit. Inner uses
  // the same 6-digit ceiling on a third line. Reserve is a fourth line paired at
  // the next `handleMidiInput()` enter (DEC-053); child stays inside 191 bytes.
  constexpr size_t kMaxCaptureTextPayload = 191;
  char parent[256];
  char child[256];
  char inner[256];
  const int parentLen = snprintf(
      parent, sizeof(parent),
      "#CAP,%lu,DIAG,snapshot_idle,us=%lu,pending=%u,root=0xffffffff,root_rev=%lu,"
      "playback_rev=%lu,queued=%lu,applied=%lu,construction=%lu,exit=%u,inflight=%u,"
      "journals=%lu,fragment=%u/%u,parent_rev=%lu",
      4294967295ul, 16252ul, 1u, 10ul, 10ul, 4ul, 4ul, 32ul, 1u, 1u, 18ul, 18u,
      18u, 10ul);
  const int childLen = snprintf(
      child, sizeof(child),
      "#CAP,%lu,DIAG,snapshot_idle,child,start=%lu,pass_change=%lu,step=%lu,"
      "finish=%lu,publish=%lu,probe=%lu,enqueue=%lu,discard=%lu,emit=%lu",
      4294967295ul, 999999ul, 999999ul, 999999ul, 999999ul, 999999ul, 999999ul,
      999999ul, 999999ul, 999999ul);
  const int innerLen = snprintf(
      inner, sizeof(inner),
      "#CAP,%lu,DIAG,snapshot_idle,inner,refill=%lu,event_scan=%lu,"
      "note_lookup=%lu,change_build=%lu,construct=%lu,open=%u",
      4294967295ul, 999999ul, 999999ul, 999999ul, 999999ul, 999999ul, 32u);
  char reserve[256];
  const int reserveLen = snprintf(
      reserve, sizeof(reserve),
      "#CAP,%lu,DIAG,snapshot_idle,reserve,start=%lu,enter=%lu,reserve_us=%lu",
      4294967295ul, 4294967295ul, 4294967295ul, 4294967295ul);
  TEST_ASSERT_GREATER_THAN(0, parentLen);
  TEST_ASSERT_GREATER_THAN(0, childLen);
  TEST_ASSERT_GREATER_THAN(0, innerLen);
  TEST_ASSERT_GREATER_THAN(0, reserveLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), parentLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), childLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), innerLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), reserveLen);
  char yieldLine[256];
  const int yieldLen = snprintf(
      yieldLine, sizeof(yieldLine),
      "#CAP,%lu,DIAG,snapshot_idle,yield,remaining=%lu,margin=%lu,admitted=%lu,"
      "cost=%lu,crit=%u,pending=%u,root_rev=%lu",
      4294967295ul, 4294967295ul, 1000ul, 10000ul, 8000ul, 1u, 1u, 10ul);
  TEST_ASSERT_GREATER_THAN(0, yieldLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), yieldLen);
}

void test_snapshot_unit_lines_fit_capture_text_budget() {
  constexpr size_t kMaxCaptureTextPayload = 191;
  char unit[256];
  char fail[256];
  char reset[64];
  const int unitLen = snprintf(
      unit, sizeof(unit),
      "#CAP,%lu,DIAG,snapshot_unit,kind=%s,ph=%u,ld=%u,p=%u,q=%lu,a=%lu,"
      "if=%u,hb=%lu,ha=%lu,psram=%lu,n=%lu,b=%lu,s=%lu",
      4294967295ul, "publish", 2u, 1u, 1u, 4294967295ul, 4294967295ul, 1u,
      4294967295ul, 4294967295ul, 4294967295ul, 4294967295ul, 4294967295ul,
      4294967295ul);
  const int failLen = snprintf(
      fail, sizeof(fail),
      "#CAP,%lu,DIAG,snapshot_slab_fail,site=%s,bytes=%lu,nodes=%lu,slabs=%lu",
      4294967295ul, "slab_allocate", 4294967295ul, 4294967295ul, 4294967295ul);
  const int resetLen = snprintf(reset, sizeof(reset), "#CAP,%lu,DIAG,boot_reset,crash=%u",
                                4294967295ul, 1u);
  TEST_ASSERT_GREATER_THAN(0, unitLen);
  TEST_ASSERT_GREATER_THAN(0, failLen);
  TEST_ASSERT_GREATER_THAN(0, resetLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), unitLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), failLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), resetLen);
}

void test_post_restore_census_line_fits_capture_text_budget() {
  // DebugSessionCapture kMaxCaptureTextBytes is 192; payload is 191 + NUL. Worst case is a 10-digit
  // micros field and both numeric fields at their type maxima.
  constexpr size_t kMaxCaptureTextPayload = 191;
  char census[256];
  const int censusLen = snprintf(
      census, sizeof(census),
      "#CAP,%lu,DIAG,post_restore_census,free_chunks=%u,admission_complete=1,"
      "interactive_ready=1,undo_hydrated=1,transport_stopped=1,deferred_prewarm_idle=1,"
      "heap_free=%lu",
      4294967295ul, 65535u, 4294967295ul);
  TEST_ASSERT_GREATER_THAN(0, censusLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), censusLen);
}

void test_library_observation_lines_fit_capture_text_budget() {
  // `restored-library-stability` 1.3. `DebugSessionCapture::kMaxCaptureTextBytes` is 192 (payload 191 +
  // NUL). The first wire form used full enum names, overran this, and was **silently truncated on
  // device**: the frame's trailing fields were lost and a completed observation read `complete=False`.
  // Worst case is a 10-digit micros field and every numeric field at its type maximum.
  constexpr size_t kMaxCaptureTextPayload = 191;
  char slot[256];
  const int slotLen = snprintf(
      slot, sizeof(slot),
      "#CAP,%lu,LIBOBS,S,t=%u,s=%u,st=%u,c=%c,p=%u,o=%u,q=%u,w=%u,m=%u,g=%lu,e=%lu,k=%u,a=%u,"
      "h=%lu,l=%lu,v=%lu,n=%u,i=%u,x=%u",
      4294967295ul, 255u, 255u, 7u, 'a', 3u, 2u, 5u, 5u, 4u, 4294967295ul, 4294967295ul, 1u, 65535u,
      4294967295ul, 4294967295ul, 4294967295ul, 1u, 1u, 1u);
  TEST_ASSERT_GREATER_THAN(0, slotLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), slotLen);

  char end[256];
  const int endLen = snprintf(
      end, sizeof(end),
      "#CAP,%lu,LIBOBS,E,a=%lu,b=%lu,t=%u,r=%u,s=%u,p=%u,i=%u,c=%u,m=%u,n=%u,e=%u,v=%u,x=%u",
      4294967295ul, 4294967295ul, 4294967295ul, 65535u, 65535u, 65535u, 65535u, 65535u, 65535u,
      65535u, 1u, 1u, 1u, 1u);
  TEST_ASSERT_GREATER_THAN(0, endLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), endLen);

  char begin[256];
  const int beginLen = snprintf(begin, sizeof(begin), "#CAP,%lu,LIBOBS,B,a=%lu,e=%lu,s=%u",
                                4294967295ul, 4294967295ul, 4294967295ul, 65535u);
  TEST_ASSERT_GREATER_THAN(0, beginLen);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kMaxCaptureTextPayload), beginLen);

  // The observation must survive the timing-critical flush budget: it is an explicitly requested
  // one-shot inventory, read back precisely in the stuck states a dense run produces.
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine("#CAP,500,LIBOBS,B,a=100,e=0,s=64"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,500,LIBOBS,S,t=0,s=0,st=2,c=n,p=3,o=2,q=5,w=0,m=1,g=0,e=0,k=1,a=1,h=4,l=1920,v=88,n=1,"
      "i=0,x=0"));
  TEST_ASSERT_TRUE(CaptureLineTier::isTierALine(
      "#CAP,500,LIBOBS,E,a=100,b=200,t=64,r=0,s=64,p=36,i=8,c=0,m=8,n=1,e=1,v=0,x=1"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,500,LIBOBSX,S,t=0"));
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

void test_full_ring_displacement_stays_non_fatal_for_tier_a() {
  // RC-MEM-7: Tier-A text displaced by newer Tier-A text is the ring's documented behaviour and
  // must NOT be reported as a session-invalidating overflow; only critical loss is fatal.
  TEST_ASSERT_TRUE(CaptureLineTier::evictionKindFor(/*headIsTierAText=*/false,
                                                    /*headIsCriticalText=*/false) ==
                   CaptureLineTier::EvictionKind::TimingShed);
  TEST_ASSERT_TRUE(CaptureLineTier::evictionKindFor(true, false) ==
                   CaptureLineTier::EvictionKind::TierAText);
  TEST_ASSERT_TRUE(CaptureLineTier::evictionKindFor(false, true) ==
                   CaptureLineTier::EvictionKind::CriticalText);
  // A critical head is critical even though it is also Tier-A.
  TEST_ASSERT_TRUE(CaptureLineTier::evictionKindFor(true, true) ==
                   CaptureLineTier::EvictionKind::CriticalText);
}

void test_tier_a_and_critical_are_only_displaced_by_their_own_kind() {
  // Ordinary diagnostics can never evict the evidence a timing verdict is computed from.
  TEST_ASSERT_FALSE(CaptureLineTier::incomingMayDisplace(false, false, /*headTierA=*/true, false));
  TEST_ASSERT_FALSE(CaptureLineTier::incomingMayDisplace(false, false, false, /*headCritical=*/true));
  TEST_ASSERT_FALSE(CaptureLineTier::incomingMayDisplace(true, false, false, true));
  // Tier-A may displace Tier-A, critical may displace critical, anything may displace plain
  // diagnostics.
  TEST_ASSERT_TRUE(CaptureLineTier::incomingMayDisplace(true, false, true, false));
  TEST_ASSERT_TRUE(CaptureLineTier::incomingMayDisplace(true, true, false, true));
  TEST_ASSERT_TRUE(CaptureLineTier::incomingMayDisplace(false, false, false, false));
  TEST_ASSERT_TRUE(CaptureLineTier::incomingMayDisplace(true, false, false, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tag_is_the_field_after_micros);
  RUN_TEST(test_timing_telemetry_lines_are_tier_a);
  RUN_TEST(test_bridge_begin_denial_is_critical_capture_evidence);
  RUN_TEST(test_snapshot_idle_lines_fit_capture_text_budget);
  RUN_TEST(test_snapshot_unit_lines_fit_capture_text_budget);
  RUN_TEST(test_post_restore_census_line_fits_capture_text_budget);
  RUN_TEST(test_library_observation_lines_fit_capture_text_budget);
  RUN_TEST(test_transport_and_persistence_lines_are_tier_a);
  RUN_TEST(test_note_and_display_traffic_is_not_tier_a);
  RUN_TEST(test_other_diag_subtags_are_not_tier_a);
  RUN_TEST(test_tag_parse_survives_a_wide_micros_field);
  RUN_TEST(test_tag_starts_with_matches_led_mo_diag);
  RUN_TEST(test_malformed_lines_are_not_tier_a);
  RUN_TEST(test_full_ring_displacement_stays_non_fatal_for_tier_a);
  RUN_TEST(test_tier_a_and_critical_are_only_displaced_by_their_own_kind);
  return UNITY_END();
}
