#include <unity.h>

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

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_make_event_id_category_and_local);
  RUN_TEST(test_edit_event_ids_unique_local_ids);
  RUN_TEST(test_trace_record_size_stable);
  return UNITY_END();
}
