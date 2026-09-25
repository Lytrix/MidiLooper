#include <unity.h>
#include <map>
#include "Utils/StoppedHeapDiagnostics.h"
using namespace StoppedHeapDiagnostics;
void setUp() {}
void tearDown() {}
void test_clock_quiet_including_wrap() {
  TEST_ASSERT_FALSE(recentClock(100, 0, false));
  TEST_ASSERT_TRUE(recentClock(100, 90, true));
  TEST_ASSERT_TRUE(recentClock(10, UINT32_MAX - 10, true));
  TEST_ASSERT_FALSE(recentClock(1000100, 100, true));
}
void test_refusal_never_collects() {
  for (int i=0;i<4;++i) {
    Gate g; if(i==0)g.transport=true; if(i==1)g.clock=true; if(i==2)g.tracks=true; if(i==3)g.storage=true;
    int calls=0;
    TEST_ASSERT_FALSE(collectIfStopped(g,[&]{++calls;}));
    TEST_ASSERT_EQUAL(0,calls);
  }
  int calls=0; TEST_ASSERT_TRUE(collectIfStopped(Gate{},[&]{++calls;})); TEST_ASSERT_EQUAL(1,calls);
}
void test_fragmented_external_space() {
  auto r=scanExternal(240,[](uint32_t at,uint32_t& payload)->uint32_t {
    if(at==60 || at==180) {payload=12;return 36;} return 0;
  });
  TEST_ASSERT_TRUE(r.complete); TEST_ASSERT_EQUAL(168,r.free);
  TEST_ASSERT_EQUAL(84,r.largestGap); TEST_ASSERT_EQUAL(60,r.largestRequest);
  TEST_ASSERT_EQUAL(24,r.payload); TEST_ASSERT_EQUAL(72,r.occupied);
  auto contiguous=scanExternal(240,[](uint32_t at,uint32_t& p)->uint32_t {if(at==0){p=48;return 72;}return 0;});
  TEST_ASSERT_EQUAL(r.free,contiguous.free); TEST_ASSERT_EQUAL(144,contiguous.largestRequest);
}
void test_invalid_external_span() {
  TEST_ASSERT_FALSE(scanExternal(120,[](uint32_t,uint32_t&)->uint32_t{return 132;}).complete);
  TEST_ASSERT_FALSE(scanExternal(121,[](uint32_t,uint32_t&)->uint32_t{return 0;}).complete);
}
void test_internal_free_list_and_invalid_link() {
  std::map<uintptr_t,uint32_t> words;
  const uintptr_t bins=0x1000, start=0x2000, end=0x2400;
  for(int i=0;i<127;++i){const auto head=bins+8+i*8;words[head+8]=head;words[head+12]=head;}
  words[bins+8]=0x2300; words[0x2304]=256|1;
  const auto head=bins+8; words[head+8]=0x2000;words[head+12]=0x2000;
  words[0x2008]=head;words[0x200c]=head;words[0x2004]=128|1;
  auto read=[&](uintptr_t a){TEST_ASSERT_TRUE(words.count(a));return words.at(a);};
  auto r=scanInternal(bins,start,end,1024,read);
  TEST_ASSERT_TRUE(r.complete); TEST_ASSERT_EQUAL(384,r.free); TEST_ASSERT_EQUAL(256,r.largest);
  TEST_ASSERT_EQUAL(256,r.top); TEST_ASSERT_EQUAL(2,r.blocks);
  words[0x200c]=0x9000;
  TEST_ASSERT_FALSE(scanInternal(bins,start,end,1024,read).complete);
  words[0x200c]=0x2000;
  TEST_ASSERT_FALSE(scanInternal(bins,start,end,1024,read).complete);
}
int main(){UNITY_BEGIN();RUN_TEST(test_clock_quiet_including_wrap);RUN_TEST(test_refusal_never_collects);RUN_TEST(test_fragmented_external_space);RUN_TEST(test_invalid_external_span);RUN_TEST(test_internal_free_list_and_invalid_link);return UNITY_END();}
