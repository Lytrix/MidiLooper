//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/Loop.cpp"
#include "../../src/TrackPlaybackRuntime.cpp"
#include "Loop.h"
#include "TrackPlaybackRuntime.h"

void test_playback_runtime_slot_is_stable_after_prewarm_touch() {
  TrackPlaybackRuntime runtime;
  LoopPlaybackRuntime& first = runtime.slot(0);
  LoopPlaybackRuntime& second = runtime.slot(0);
  TEST_ASSERT_EQUAL_PTR(&first, &second);
}

void test_playback_order_alloc_is_stable_after_prewarm_touch() {
  Loop loop;
  PlaybackOrderVec& first = loop.getPlaybackOrder();
  PlaybackOrderVec& second = loop.getPlaybackOrder();
  TEST_ASSERT_EQUAL_PTR(&first, &second);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_playback_runtime_slot_is_stable_after_prewarm_touch);
  RUN_TEST(test_playback_order_alloc_is_stable_after_prewarm_touch);
  return UNITY_END();
}
