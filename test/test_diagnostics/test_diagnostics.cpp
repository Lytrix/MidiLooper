#include <unity.h>

#include "Utils/Diagnostics.h"
#include "Utils/DiagnosticsEvents.h"
#include "Utils/DiagnosticsTypes.h"

void test_make_event_id_category_and_local() {
  const uint16_t id = Diagnostics::makeEventId(Diagnostics::Category::Edit, 5);
  TEST_ASSERT_EQUAL_UINT16(0x0305, id);
  TEST_ASSERT_EQUAL_UINT8(3, Diagnostics::eventCategory(id));
}

void test_edit_event_ids_unique_local_ids() {
  TEST_ASSERT_NOT_EQUAL(Diagnostics::Edit::NoteEditOpenEnter,
                        Diagnostics::Edit::AfterRematerializeEditView);
  TEST_ASSERT_EQUAL_UINT8(3, Diagnostics::eventCategory(Diagnostics::Edit::NoteEditOpenEnter));
}

void test_trace_record_size_stable() {
  TEST_ASSERT_GREATER_THAN(0, sizeof(Diagnostics::DiagTraceRecord));
  TEST_ASSERT_EQUAL_UINT8(1, Diagnostics::kTraceFormatVersion);
}

void test_architecture_counter_enum_layout() {
  TEST_ASSERT_EQUAL(0, static_cast<int>(Diagnostics::Counter::PlaybackWindowRebuild));
  TEST_ASSERT_EQUAL(1, static_cast<int>(Diagnostics::Counter::PlaybackDeferredReuse));
  TEST_ASSERT_EQUAL(2, static_cast<int>(Diagnostics::Counter::PlaybackFullMaterialize));
  TEST_ASSERT_EQUAL(3, static_cast<int>(Diagnostics::Counter::LegacyMidiEvents));
  TEST_ASSERT_EQUAL(4, static_cast<int>(Diagnostics::Counter::DisplayFullRebuild));
  TEST_ASSERT_EQUAL(5, static_cast<int>(Diagnostics::Counter::DisplayIncrementalUpdate));
  TEST_ASSERT_EQUAL(11, static_cast<int>(Diagnostics::Counter::Count));
}

void test_architecture_timing_enum_layout() {
  TEST_ASSERT_EQUAL(0, static_cast<int>(Diagnostics::Timing::PlaybackBuild));
  TEST_ASSERT_EQUAL(1, static_cast<int>(Diagnostics::Timing::DisplayBuild));
  TEST_ASSERT_EQUAL(2, static_cast<int>(Diagnostics::Timing::Count));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_make_event_id_category_and_local);
  RUN_TEST(test_edit_event_ids_unique_local_ids);
  RUN_TEST(test_trace_record_size_stable);
  RUN_TEST(test_architecture_counter_enum_layout);
  RUN_TEST(test_architecture_timing_enum_layout);
  return UNITY_END();
}
