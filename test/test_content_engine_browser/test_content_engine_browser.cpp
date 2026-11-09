//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sets browser slice 3: the browser's lists, buttons, popup, requests and failures (proposal rules 1–7).

#include <unity.h>

#include "ContentEngine/Browser.h"

using namespace ContentEngine;
using namespace ContentEngine::library;
using B = Browser<64>;

static B gB;
static uint32_t gSeq = 0;

static void publish(uint16_t set, Stage stage) {
  Record r{};
  r.sequence = ++gSeq;
  r.set = set;
  r.stage = stage;
  r.stageNumber = gB.nextNumber(set, stage);
  r.wallTime = 1794175000 + gSeq;
  TEST_ASSERT_TRUE(gB.add(r));
}
static B::RowKind kind(uint16_t i) { return gB.row(i).kind; }

void setUp() {
  gB.clear();
  gSeq = 0;
  // SET 02: S01, B01, T01 (working); SET 07: T01, T02; SET 03: F01 — newest: SET 03, then SET 07.
  publish(2, Stage::Sketch);
  publish(7, Stage::Take);
  publish(2, Stage::Building);
  publish(7, Stage::Take);
  publish(2, Stage::Take);
  publish(3, Stage::Final);
  gB.setWorking(2, 5, false);
}
void tearDown() {}

void test_opening_selects_save_and_lists_actions_working_set_then_sets_newest_first() {
  gB.open();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(5, gB.rowCount());
  TEST_ASSERT_EQUAL(B::RowKind::Save, kind(0));
  TEST_ASSERT_EQUAL(B::RowKind::SaveNewSet, kind(1));
  TEST_ASSERT_EQUAL(B::RowKind::Working, kind(2));
  TEST_ASSERT_EQUAL_UINT16(2, gB.row(2).set);
  TEST_ASSERT_EQUAL_UINT16(4, gB.row(2).record);  // T01, its newest
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(3).set);     // the newest other set first
  TEST_ASSERT_EQUAL_UINT16(7, gB.row(4).set);
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(4).record);  // SET 07's newest: T02
  TEST_ASSERT_EQUAL_UINT16(0, gB.selected());
}

void test_with_a_checkpoint_return_sits_one_step_above_save() {
  gB.setWorking(2, 5, true);
  gB.open();
  TEST_ASSERT_EQUAL(B::RowKind::Return, kind(0));
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());  // still on save
  gB.up();
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL(B::Action::Return, r.action);
}

void test_record_and_edit_stop_at_both_ends() {
  gB.open();
  gB.up();
  TEST_ASSERT_EQUAL_UINT16(0, gB.selected());
  for (int i = 0; i < 10; ++i) gB.down();
  TEST_ASSERT_EQUAL_UINT16(4, gB.selected());
}

void test_a_set_opens_on_its_newest_milestone_and_loads_it() {
  gB.open();
  for (int i = 0; i < 4; ++i) gB.down();  // SET 07
  gB.press();
  TEST_ASSERT_EQUAL(B::View::Set, gB.view());
  TEST_ASSERT_EQUAL(B::RowKind::Back, kind(0));
  TEST_ASSERT_EQUAL_UINT16(3, gB.rowCount());
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(1).record);  // T02 first
  TEST_ASSERT_EQUAL_UINT16(1, gB.row(2).record);
  gB.down();
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL(B::Action::Load, r.action);
  TEST_ASSERT_EQUAL_UINT16(1, r.record);  // T01 of SET 07
  TEST_ASSERT_FALSE(gB.takeRequest(r));   // once
  gB.succeeded();
  TEST_ASSERT_FALSE(gB.isOpen());
}

void test_all_sets_returns_to_the_set_row() {
  gB.open();
  for (int i = 0; i < 4; ++i) gB.down();
  gB.press();
  gB.up();
  gB.press();  // < ALL SETS
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(4, gB.selected());
}

void test_the_popup_shows_next_ids_suggests_the_base_stage_and_saves_into_the_working_set() {
  gB.open();
  gB.press();  // + SAVE MILESTONE
  TEST_ASSERT_EQUAL(B::View::Popup, gB.view());
  TEST_ASSERT_EQUAL(Stage::Take, gB.suggested());  // the base, T01
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Take), gB.popupSelected());
  TEST_ASSERT_EQUAL_UINT8(2, gB.nextNumber(2, Stage::Take));
  TEST_ASSERT_EQUAL_UINT8(1, gB.nextNumber(2, Stage::Arrangement));
  gB.down();  // ARRANGEMENT
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL(B::Action::Save, r.action);
  TEST_ASSERT_EQUAL_UINT16(2, r.set);
  TEST_ASSERT_EQUAL(Stage::Arrangement, r.stage);
  TEST_ASSERT_EQUAL_UINT8(1, r.stageNumber);
}

void test_save_as_new_set_numbers_in_a_new_set_and_suggests_sketch() {
  gB.open();
  gB.down();
  gB.press();
  TEST_ASSERT_TRUE(gB.popupNewSet());
  TEST_ASSERT_EQUAL_UINT16(8, gB.popupSet());
  TEST_ASSERT_EQUAL(Stage::Sketch, gB.suggested());
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL_UINT16(8, r.set);
  TEST_ASSERT_EQUAL_UINT8(1, r.stageNumber);
}

void test_hold_closes_the_popup_then_the_browser_and_asks_nothing() {
  gB.open();
  gB.press();
  gB.hold();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  gB.hold();
  TEST_ASSERT_FALSE(gB.isOpen());
  B::Request r;
  TEST_ASSERT_FALSE(gB.takeRequest(r));
}

void test_a_failure_keeps_the_screen_and_selection_and_track_tries_again() {
  gB.open();
  for (int i = 0; i < 4; ++i) gB.down();
  gB.press();
  gB.press();  // load SET 07 T02
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  gB.failed("LOAD FAILED", "CARD READ ERROR");
  TEST_ASSERT_EQUAL(B::View::Failed, gB.view());
  TEST_ASSERT_EQUAL(B::View::Set, gB.listView());
  TEST_ASSERT_EQUAL_STRING("LOAD FAILED", gB.failWhat());
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());
  gB.press();  // try again
  TEST_ASSERT_EQUAL(B::View::Set, gB.view());
  B::Request again;
  TEST_ASSERT_TRUE(gB.takeRequest(again));
  TEST_ASSERT_EQUAL(B::Action::Load, again.action);
  TEST_ASSERT_EQUAL_UINT16(r.record, again.record);
  gB.failed("LOAD FAILED", "CARD READ ERROR");
  gB.hold();
  TEST_ASSERT_FALSE(gB.isOpen());
}

void test_a_failed_save_goes_back_to_its_list_and_retries_the_same_save() {
  gB.open();
  gB.press();
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  gB.failed("SAVE FAILED", "LIBRARY FULL");
  TEST_ASSERT_EQUAL(B::View::Top, gB.listView());
  gB.press();
  B::Request again;
  TEST_ASSERT_TRUE(gB.takeRequest(again));
  TEST_ASSERT_EQUAL(B::Action::Save, again.action);
  TEST_ASSERT_EQUAL(r.stage, again.stage);
}

void test_the_list_scrolls_with_the_selection() {
  for (uint16_t s = 10; s < 20; ++s) publish(s, Stage::Sketch);
  gB.open();
  for (int i = 0; i < 8; ++i) gB.down();
  TEST_ASSERT_EQUAL_UINT16(8, gB.selected());
  TEST_ASSERT_EQUAL_UINT16(3, gB.first());
  for (int i = 0; i < 8; ++i) gB.up();
  TEST_ASSERT_EQUAL_UINT16(0, gB.first());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_opening_selects_save_and_lists_actions_working_set_then_sets_newest_first);
  RUN_TEST(test_with_a_checkpoint_return_sits_one_step_above_save);
  RUN_TEST(test_record_and_edit_stop_at_both_ends);
  RUN_TEST(test_a_set_opens_on_its_newest_milestone_and_loads_it);
  RUN_TEST(test_all_sets_returns_to_the_set_row);
  RUN_TEST(test_the_popup_shows_next_ids_suggests_the_base_stage_and_saves_into_the_working_set);
  RUN_TEST(test_save_as_new_set_numbers_in_a_new_set_and_suggests_sketch);
  RUN_TEST(test_hold_closes_the_popup_then_the_browser_and_asks_nothing);
  RUN_TEST(test_a_failure_keeps_the_screen_and_selection_and_track_tries_again);
  RUN_TEST(test_a_failed_save_goes_back_to_its_list_and_retries_the_same_save);
  RUN_TEST(test_the_list_scrolls_with_the_selection);
  return UNITY_END();
}
