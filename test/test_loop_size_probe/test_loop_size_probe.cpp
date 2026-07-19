#include <cstdio>
#include <unity.h>
#include "Globals.h"
#include "Loop.h"
void test_loop_size_probe() {
  std::printf("sizeof(Loop)=%zu\n", sizeof(Loop));
  std::printf("sizeof(LoopPasses)=%zu\n", sizeof(LoopPasses));
  std::printf("sizeof(RecordPass)=%zu\n", sizeof(RecordPass));
  std::printf("sizeof(OverdubPass)=%zu\n", sizeof(OverdubPass));
  std::printf("sizeof(EditPass)=%zu\n", sizeof(EditPass));
  std::printf("sizeof(Capture)=%zu\n", sizeof(Capture));
  std::printf("sizeof(LoopEventStore)=%zu\n", sizeof(LoopEventStore));
  std::printf("sizeof(VisualCache)=%zu\n", sizeof(VisualCache));
  std::printf("sizeof(CaptureChunkIdList)=%zu\n", sizeof(CaptureChunkIdList));
  std::printf("sizeof(CommittedChunkIdList)=%zu\n", sizeof(CommittedChunkIdList));
  std::printf("sizeof(CommittedOverdubPassVec)=%zu\n", sizeof(CommittedOverdubPassVec));
  std::printf("loop_pool_bytes=%zu\n", (size_t)Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK * sizeof(Loop));
  TEST_ASSERT_TRUE(sizeof(Loop) > 0);
}
int main(int, char**) { UNITY_BEGIN(); RUN_TEST(test_loop_size_probe); return UNITY_END(); }
