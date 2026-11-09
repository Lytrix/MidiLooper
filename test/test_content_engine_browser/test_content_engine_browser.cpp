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
  gB.setWorking(2, 5);
}
void tearDown() {}

/// Owner 2026-11-09: `< CLOSE`, SAVE SET, a LOAD SET divider, then the sets; no return row (DEC-267, DEC-268).
void test_opening_selects_save_over_a_load_section_of_sets_newest_first() {
  gB.open();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(7, gB.rowCount());
  TEST_ASSERT_EQUAL(B::RowKind::Close, kind(0));
  TEST_ASSERT_EQUAL(B::RowKind::Save, kind(1));
  TEST_ASSERT_EQUAL(B::RowKind::Divider, kind(2));
  TEST_ASSERT_EQUAL(B::RowKind::Working, kind(3));
  TEST_ASSERT_EQUAL_UINT16(2, gB.row(3).set);
  TEST_ASSERT_EQUAL_UINT16(4, gB.row(3).record);  // T01, its newest
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(4).set);     // the newest other set first
  TEST_ASSERT_EQUAL_UINT16(7, gB.row(5).set);
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(5).record);  // SET 07's newest: T02
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());
}

/// `< CLOSE` closes (TRACK); the divider is passed both ways; HOLD TRACK on SAVE SET or CLOSE does nothing (DEC-268).
void test_close_closes_and_the_divider_is_passed() {
  gB.open();
  gB.down();
  TEST_ASSERT_EQUAL_UINT16(3, gB.selected());  // over LOAD SET to the working set
  gB.up();
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());
  gB.hold();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  gB.up();
  TEST_ASSERT_EQUAL(B::RowKind::Close, kind(gB.selected()));
  gB.hold();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  gB.press();
  TEST_ASSERT_FALSE(gB.isOpen());
  B::Request r;
  TEST_ASSERT_FALSE(gB.takeRequest(r));
}

void test_record_and_edit_stop_at_both_ends() {
  gB.open();
  gB.up();
  gB.up();
  TEST_ASSERT_EQUAL_UINT16(0, gB.selected());
  for (int i = 0; i < 10; ++i) gB.down();
  TEST_ASSERT_EQUAL_UINT16(6, gB.selected());  // TRASH, last
}

void test_a_set_opens_on_its_newest_milestone_and_loads_it() {
  gB.open();
  for (int i = 0; i < 3; ++i) gB.down();  // SET 07
  gB.press();
  TEST_ASSERT_EQUAL(B::View::Set, gB.view());
  TEST_ASSERT_EQUAL(B::RowKind::Back, kind(0));
  TEST_ASSERT_EQUAL(B::RowKind::Divider, kind(1));  // LOAD STAGE (owner 2026-11-09)
  TEST_ASSERT_EQUAL_UINT16(4, gB.rowCount());
  TEST_ASSERT_EQUAL_UINT16(2, gB.selected());
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(2).record);  // T02 first
  TEST_ASSERT_EQUAL_UINT16(1, gB.row(3).record);
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

void test_back_returns_to_the_set_row() {
  gB.open();
  for (int i = 0; i < 3; ++i) gB.down();
  gB.press();
  gB.up();  // over LOAD STAGE
  TEST_ASSERT_EQUAL_UINT16(0, gB.selected());
  gB.press();  // < SETS
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(5, gB.selected());
}

void test_the_popup_shows_next_ids_suggests_the_base_stage_and_saves_into_the_working_set() {
  gB.open();
  gB.press();  // SAVE
  TEST_ASSERT_EQUAL(B::View::Popup, gB.view());
  TEST_ASSERT_EQUAL(Stage::Take, gB.suggested());  // the base, T01
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Take) + 1, gB.popupSelected());  // 0 is AS NEW SET
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

/// AS NEW SET, at the popup's top (not selected): Sketch 01 of a set no milestone uses.
void test_as_new_set_saves_sketch_one_of_a_new_set() {
  gB.open();
  gB.press();
  for (int i = 0; i < 10; ++i) gB.up();
  TEST_ASSERT_EQUAL_UINT8(0, gB.popupSelected());
  TEST_ASSERT_EQUAL_UINT16(8, gB.newSetNumber());
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL(B::Action::Save, r.action);
  TEST_ASSERT_EQUAL_UINT16(8, r.set);
  TEST_ASSERT_EQUAL(Stage::Sketch, r.stage);
  TEST_ASSERT_EQUAL_UINT8(1, r.stageNumber);
}
void test_the_popup_runs_from_as_new_set_to_final() {
  gB.open();
  gB.press();
  for (int i = 0; i < 10; ++i) gB.down();
  TEST_ASSERT_EQUAL_UINT8(6, gB.popupSelected());  // FINAL, the last
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL(Stage::Final, r.stage);
  TEST_ASSERT_EQUAL_UINT16(2, r.set);
}

/// Slice 4 (proposal §3): one stage by one rule.
void test_the_suggestion_follows_the_evidence_since_the_base() {
  bool provisional = false;
  B::Activity a;
  gB.setActivity(a);
  TEST_ASSERT_EQUAL(Stage::Take, gB.suggest(false, provisional));  // nothing since T01: its stage
  a.explore = true;
  gB.setActivity(a);
  TEST_ASSERT_EQUAL(Stage::Exploration, gB.suggest(false, provisional));
  a = B::Activity{};
  a.build = true;
  gB.setActivity(a);
  TEST_ASSERT_EQUAL(Stage::Building, gB.suggest(false, provisional));
  TEST_ASSERT_FALSE(provisional);
  a.takesOnly = true;
  gB.setActivity(a);
  TEST_ASSERT_EQUAL(Stage::Take, gB.suggest(false, provisional));
  TEST_ASSERT_TRUE(provisional);  // a Take is only ever provisional
  a.explore = true;
  gB.setActivity(a);
  TEST_ASSERT_EQUAL(Stage::Take, gB.suggest(false, provisional));  // mixed: the base's stage, not provisional
  TEST_ASSERT_FALSE(provisional);
  a = B::Activity{};
  a.build = true;
  gB.setActivity(a);
  TEST_ASSERT_EQUAL(Stage::Take, gB.suggest(true, provisional));  // a new set: the base's stage
}
void test_arrangement_and_final_are_never_inferred_and_new_work_is_sketch() {
  publish(2, Stage::Arrangement);
  gB.setWorking(2, gSeq);
  B::Activity a;
  a.explore = true;
  gB.setActivity(a);
  bool provisional = false;
  TEST_ASSERT_EQUAL(Stage::Arrangement, gB.suggest(false, provisional));
  a = B::Activity{};
  a.build = true;
  gB.setActivity(a);
  TEST_ASSERT_EQUAL(Stage::Arrangement, gB.suggest(false, provisional));
  gB.setWorking(2, 0);  // nothing loaded or saved
  TEST_ASSERT_EQUAL(Stage::Sketch, gB.suggest(false, provisional));
  TEST_ASSERT_EQUAL(Stage::Sketch, gB.suggest(true, provisional));
  gB.open();
  gB.press();
  TEST_ASSERT_EQUAL(Stage::Sketch, gB.suggested());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Sketch) + 1, gB.popupSelected());  // the selection starts on it
}

void test_hold_closes_the_save_popup_and_asks_nothing() {
  gB.open();
  gB.press();
  gB.hold();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());
  B::Request r;
  TEST_ASSERT_FALSE(gB.takeRequest(r));
}

// ---- The trash (DEC-268) --------------------------------------------------------------------------------------------
/// A trash-side record as the library publishes it: its own sequence, the set, the target.
static uint32_t mark(uint8_t kind, uint16_t set, uint32_t target) {
  Record r{};
  r.sequence = ++gSeq;
  r.set = set;
  r.kind = kind;
  r.parent = target;
  TEST_ASSERT_TRUE(gB.add(r));
  return r.sequence;
}
/// TRACK on YES of the confirmation shown; the request it makes.
static B::Request confirmYes() {
  gB.up();
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  return r;
}

/// HOLD TRACK on a set asks to move it to the trash, CANCEL selected; TRACK on CANCEL, or HOLD TRACK, changes nothing.
void test_hold_on_a_set_asks_to_trash_it_and_cancel_changes_nothing() {
  gB.open();
  for (int i = 0; i < 3; ++i) gB.down();  // SET 07
  gB.hold();
  TEST_ASSERT_EQUAL(B::View::Confirm, gB.view());
  TEST_ASSERT_EQUAL(B::Confirm::ToTrash, gB.confirmKind());
  TEST_ASSERT_EQUAL_UINT16(7, gB.confirmSet());
  TEST_ASSERT_EQUAL_UINT16(B::kNone, gB.confirmRecord());
  TEST_ASSERT_EQUAL_UINT8(1, gB.confirmSelected());  // CANCEL
  TEST_ASSERT_EQUAL(B::RowKind::Set, kind(gB.selected()));  // the list under it
  gB.press();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  B::Request r;
  TEST_ASSERT_FALSE(gB.takeRequest(r));
  gB.hold();
  gB.up();    // YES
  gB.hold();  // cancels all the same
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_FALSE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL_UINT16(5, gB.selected());
}

/// A set to the trash: gone from the sets (the nearest selected), one item in the trash with its stages together.
void test_a_trashed_set_leaves_the_sets_for_the_trash() {
  gB.open();
  for (int i = 0; i < 2; ++i) gB.down();  // SET 03
  gB.hold();
  const B::Request r = confirmYes();
  TEST_ASSERT_EQUAL(B::Action::Trash, r.action);
  TEST_ASSERT_EQUAL_UINT16(3, r.set);
  TEST_ASSERT_EQUAL_UINT16(B::kNone, r.record);
  TEST_ASSERT_EQUAL(B::View::Confirm, gB.view());  // until it is published
  mark(kTrash, 3, 0);
  gB.marked();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(6, gB.rowCount());
  TEST_ASSERT_EQUAL_UINT16(4, gB.selected());
  TEST_ASSERT_EQUAL_UINT16(7, gB.row(4).set);  // SET 07 moved up into its place
  TEST_ASSERT_TRUE(gB.hidden(5));              // F01
  TEST_ASSERT_EQUAL_UINT16(1, gB.itemCount());
  TEST_ASSERT_EQUAL_UINT16(6, gB.item(0));
  TEST_ASSERT_EQUAL_UINT16(1, gB.itemSize(6));
  TEST_ASSERT_EQUAL_UINT16(5, gB.itemNewest(6));
  TEST_ASSERT_EQUAL_UINT16(8, gB.newSetNumber());  // 03 is not reused, nor 07
}

/// A double TRACK opens the trash (`< SETS`, EMPTY TRASH, the RESTORE divider, the items newest first) and closes it.
void test_the_trash_lists_its_items_newest_first() {
  mark(kTrash, 7, gB.record(3).sequence);  // SET 07 T02
  mark(kTrash, 3, 0);                      // SET 03
  gB.open();
  gB.openTrash();
  TEST_ASSERT_EQUAL(B::View::Bin, gB.view());
  TEST_ASSERT_EQUAL_UINT16(5, gB.rowCount());
  TEST_ASSERT_EQUAL(B::RowKind::Back, kind(0));
  TEST_ASSERT_EQUAL(B::RowKind::Empty, kind(1));
  TEST_ASSERT_EQUAL(B::RowKind::Divider, kind(2));
  TEST_ASSERT_EQUAL(B::RowKind::Item, kind(3));
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(3).set);  // the newest item first
  TEST_ASSERT_EQUAL_UINT16(7, gB.row(4).set);
  TEST_ASSERT_EQUAL_UINT16(3, gB.selected());
  gB.hold();  // nothing to trash in the trash
  TEST_ASSERT_EQUAL(B::View::Bin, gB.view());
  gB.up();
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());  // over the divider
  gB.openTrash();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  gB.openTrash();
  gB.up();
  gB.up();
  gB.press();  // < SETS
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());
}

/// TRASH, last in the sets (owner 2026-11-09: a second way in beside the double TRACK): TRACK opens the trash. A set
/// trashed from just above it leaves the nearest set selected, not TRASH.
void test_the_trash_row_opens_the_trash_and_is_not_the_nearest_set() {
  gB.open();
  for (int i = 0; i < 10; ++i) gB.down();
  TEST_ASSERT_EQUAL(B::RowKind::Trash, kind(gB.selected()));
  gB.hold();  // nothing to trash
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  gB.press();
  TEST_ASSERT_EQUAL(B::View::Bin, gB.view());
  gB.press();  // < SETS (the trash is empty)
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  for (int i = 0; i < 10; ++i) gB.down();
  gB.up();  // SET 07, the last set
  gB.hold();
  confirmYes();
  mark(kTrash, 7, 0);
  gB.marked();
  TEST_ASSERT_EQUAL(B::RowKind::Set, kind(gB.selected()));
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(gB.selected()).set);
}

/// TRACK on an item restores it at once; a stage restored brings its set back; the trash stays, the next item selected.
void test_restoring_a_stage_brings_its_set_back() {
  const uint32_t t = mark(kTrash, 3, gB.record(5).sequence);  // SET 03 F01, its only stage: the set is gone
  gB.open();
  TEST_ASSERT_EQUAL_UINT16(6, gB.rowCount());
  gB.openTrash();
  gB.press();
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  TEST_ASSERT_EQUAL(B::Action::Restore, r.action);
  TEST_ASSERT_EQUAL_UINT16(6, r.record);
  mark(kRestore, 3, t);
  gB.marked();
  TEST_ASSERT_EQUAL(B::View::Bin, gB.view());
  TEST_ASSERT_EQUAL_UINT16(0, gB.itemCount());
  TEST_ASSERT_EQUAL_UINT16(1, gB.selected());  // EMPTY TRASH, the nearest row left
  TEST_ASSERT_FALSE(gB.hidden(5));
  gB.openTrash();
  TEST_ASSERT_EQUAL_UINT16(7, gB.rowCount());
  TEST_ASSERT_EQUAL_UINT16(3, gB.row(4).set);
}

/// A stage trashed on its own, then its set: two items; restoring the set leaves the stage in the trash.
void test_a_set_item_takes_only_the_stages_still_shown() {
  const uint32_t stage = mark(kTrash, 7, gB.record(3).sequence);  // T02
  const uint32_t set = mark(kTrash, 7, 0);                         // T01, the rest
  TEST_ASSERT_EQUAL_UINT16(2, gB.itemCount());
  TEST_ASSERT_EQUAL_UINT16(1, gB.itemSize(7));
  TEST_ASSERT_EQUAL_UINT16(1, gB.itemSize(6));
  mark(kRestore, 7, set);
  TEST_ASSERT_FALSE(gB.hidden(1));  // T01 back
  TEST_ASSERT_TRUE(gB.hidden(3));   // T02 still in the trash
  TEST_ASSERT_EQUAL_UINT16(1, gB.itemCount());
  TEST_ASSERT_EQUAL_UINT16(1, gB.latestOf(7));
  mark(kRestore, 7, stage);
  TEST_ASSERT_EQUAL_UINT16(3, gB.latestOf(7));
  TEST_ASSERT_EQUAL_UINT16(0, gB.itemCount());
}

/// EMPTY TRASH asks again (CANCEL selected); YES removes every item for good — nothing to restore, numbers not reused.
void test_emptying_the_trash_asks_again_and_removes_for_good() {
  const uint32_t t = mark(kTrash, 3, 0);
  gB.open();
  gB.openTrash();
  gB.up();  // over the divider
  TEST_ASSERT_EQUAL(B::RowKind::Empty, kind(gB.selected()));
  gB.press();
  TEST_ASSERT_EQUAL(B::View::Confirm, gB.view());
  TEST_ASSERT_EQUAL(B::Confirm::Empty, gB.confirmKind());
  TEST_ASSERT_EQUAL_UINT8(1, gB.confirmSelected());
  const B::Request r = confirmYes();
  TEST_ASSERT_EQUAL(B::Action::Empty, r.action);
  mark(kEmptyTrash, 0, 0);
  gB.marked();
  TEST_ASSERT_EQUAL(B::View::Bin, gB.view());
  TEST_ASSERT_EQUAL_UINT16(0, gB.itemCount());
  mark(kRestore, 3, t);  // too late: nothing comes back
  TEST_ASSERT_TRUE(gB.hidden(5));
  TEST_ASSERT_EQUAL_UINT16(B::kNone, gB.latestOf(3));
  TEST_ASSERT_EQUAL_UINT8(2, gB.nextNumber(3, Stage::Final));
  gB.press();  // EMPTY TRASH with nothing in it: nothing asked
  TEST_ASSERT_EQUAL(B::View::Bin, gB.view());
}

/// A stage to the trash from inside its set: the set keeps its other stages, the next one selected.
void test_trashing_a_stage_keeps_the_set_and_selects_the_next_stage() {
  gB.open();
  for (int i = 0; i < 3; ++i) gB.down();
  gB.press();  // SET 07: T02, T01
  gB.hold();
  TEST_ASSERT_EQUAL_UINT16(3, gB.confirmRecord());  // T02
  const B::Request r = confirmYes();
  TEST_ASSERT_EQUAL_UINT16(3, r.record);
  mark(kTrash, 7, gB.record(3).sequence);
  gB.marked();
  TEST_ASSERT_EQUAL(B::View::Set, gB.view());
  TEST_ASSERT_EQUAL_UINT16(3, gB.rowCount());
  TEST_ASSERT_EQUAL_UINT16(2, gB.selected());
  TEST_ASSERT_EQUAL_UINT16(1, gB.row(2).record);  // T01
  TEST_ASSERT_EQUAL_UINT8(3, gB.nextNumber(7, Stage::Take));
}

/// The last stage of a set: the set goes, and the view goes back to the sets with the nearest one selected.
void test_trashing_the_last_stage_leaves_the_set_view() {
  gB.open();
  for (int i = 0; i < 2; ++i) gB.down();
  gB.press();  // SET 03: F01
  gB.hold();
  const B::Request r = confirmYes();
  mark(kTrash, 3, gB.record(r.record).sequence);
  gB.marked();
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT16(6, gB.rowCount());
  TEST_ASSERT_EQUAL_UINT16(4, gB.selected());
  TEST_ASSERT_EQUAL_UINT16(7, gB.row(4).set);
}

/// The loaded set (the working one): its row stays, empty, and the base is still known (what plays is untouched).
void test_trashing_the_working_set_keeps_its_row_and_base() {
  gB.open();
  gB.down();  // SET 02, the working set
  gB.hold();
  const B::Request r = confirmYes();
  TEST_ASSERT_EQUAL_UINT16(2, r.set);
  mark(kTrash, 2, 0);
  gB.marked();
  TEST_ASSERT_EQUAL(B::RowKind::Working, kind(3));
  TEST_ASSERT_EQUAL_UINT16(B::kNone, gB.row(3).record);
  TEST_ASSERT_EQUAL_UINT16(3, gB.selected());
  TEST_ASSERT_EQUAL_UINT16(4, gB.find(gB.baseSequence()));  // T01, in the trash but known
  bool provisional = false;
  gB.setActivity(B::Activity{});
  TEST_ASSERT_EQUAL(Stage::Take, gB.suggest(false, provisional));
  gB.hold();  // nothing left to trash there
  TEST_ASSERT_EQUAL(B::View::Top, gB.view());
  TEST_ASSERT_EQUAL_UINT8(2, gB.nextNumber(2, Stage::Take));  // a new save into it is numbered on
}

/// A failed trash: back to the list with what and why; TRACK asks for the same again.
void test_a_failed_trash_keeps_the_set_and_retries() {
  gB.open();
  for (int i = 0; i < 3; ++i) gB.down();
  gB.hold();
  confirmYes();
  gB.failed("TRASH FAILED", "CARD WRITE ERROR");
  TEST_ASSERT_EQUAL(B::View::Failed, gB.view());
  TEST_ASSERT_EQUAL(B::View::Top, gB.listView());
  TEST_ASSERT_EQUAL_UINT16(7, gB.rowCount());  // nothing hidden
  gB.press();
  B::Request again;
  TEST_ASSERT_TRUE(gB.takeRequest(again));
  TEST_ASSERT_EQUAL(B::Action::Trash, again.action);
  TEST_ASSERT_EQUAL_UINT16(7, again.set);
  TEST_ASSERT_EQUAL_UINT16(B::kNone, again.record);
}

/// At boot the index is replayed in card order: a set's trash record takes only what was published before it.
void test_a_set_trash_takes_only_earlier_stages() {
  mark(kTrash, 7, 0);
  publish(7, Stage::Sketch);  // SET 07 started again
  TEST_ASSERT_EQUAL_UINT16(1, gB.milestonesIn(7));
  TEST_ASSERT_EQUAL_UINT8(3, gB.nextNumber(7, Stage::Take));
  gB.open();
  TEST_ASSERT_EQUAL_UINT16(7, gB.row(4).set);  // the newest other set again
}

void test_a_failure_keeps_the_screen_and_selection_and_track_tries_again() {
  gB.open();
  for (int i = 0; i < 3; ++i) gB.down();
  gB.press();
  gB.press();  // load SET 07 T02
  B::Request r;
  TEST_ASSERT_TRUE(gB.takeRequest(r));
  gB.failed("LOAD FAILED", "CARD READ ERROR");
  TEST_ASSERT_EQUAL(B::View::Failed, gB.view());
  TEST_ASSERT_EQUAL(B::View::Set, gB.listView());
  TEST_ASSERT_EQUAL_STRING("LOAD FAILED", gB.failWhat());
  TEST_ASSERT_EQUAL_UINT16(2, gB.selected());
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
  TEST_ASSERT_EQUAL_UINT16(10, gB.selected());  // over the divider
  TEST_ASSERT_EQUAL_UINT16(6, gB.first());      // five rows shown
  for (int i = 0; i < 10; ++i) gB.up();
  TEST_ASSERT_EQUAL_UINT16(0, gB.first());
}

/// Slice 5, walkthrough 5: T02, B04 from T02, T03 from B04, then A01 from T02 (loaded back and arranged). The line is
/// A01 and T02; B04 and T03 branched off, joining at T02.
void test_the_line_and_its_side_branch() {
  gB.clear();
  gSeq = 0;
  auto add = [](Stage st, uint8_t n, uint32_t parent) {
    Record r{};
    r.sequence = ++gSeq;
    r.set = 4;
    r.stage = st;
    r.stageNumber = n;
    r.parent = parent;
    TEST_ASSERT_TRUE(gB.add(r));
  };
  add(Stage::Take, 2, 0);         // 1
  add(Stage::Building, 4, 1);     // 2
  add(Stage::Take, 3, 2);         // 3
  add(Stage::Arrangement, 1, 1);  // 4
  gB.setWorking(4, 4);
  gB.open();
  gB.down();
  gB.press();  // the working set: A01, T03, B04, T02
  TEST_ASSERT_EQUAL(B::View::Set, gB.view());
  TEST_ASSERT_FALSE(gB.onLine(1));  // the LOAD STAGE divider
  TEST_ASSERT_TRUE(gB.onLine(2));   // A01
  TEST_ASSERT_FALSE(gB.onLine(3));  // T03
  TEST_ASSERT_FALSE(gB.onLine(4));  // B04
  TEST_ASSERT_TRUE(gB.onLine(5));   // T02
  TEST_ASSERT_TRUE(gB.hasSide());
  TEST_ASSERT_EQUAL_UINT16(5, gB.joinRow());
}
void test_a_straight_history_has_one_lane() {
  gB.open();
  gB.down();
  gB.press();  // SET 02: S01, B01, T01 without parents — the newest alone is "the line"
  for (uint16_t i = 1; i <= 3; ++i) publish(2, Stage::Sketch);
  gB.hold();
  gB.clear();
  gSeq = 0;
  Record r{};
  for (uint8_t k = 0; k < 3; ++k) {
    r.sequence = ++gSeq;
    r.set = 2;
    r.parent = k == 0 ? 0 : gSeq - 1;
    TEST_ASSERT_TRUE(gB.add(r));
  }
  gB.setWorking(2, 3);
  gB.open();
  gB.down();
  gB.press();
  TEST_ASSERT_FALSE(gB.hasSide());
  TEST_ASSERT_EQUAL_UINT16(0, gB.joinRow());
}

/// The save popup's history: the working set's milestones newest first, the line traced from the base (here B04, loaded
/// back: B04 and T02 on it; A01 and T03, newer, beside it).
void test_the_popup_history_follows_the_base_line() {
  gB.clear();
  gSeq = 0;
  auto add = [](Stage st, uint8_t n, uint32_t parent) {
    Record r{};
    r.sequence = ++gSeq;
    r.set = 4;
    r.stage = st;
    r.stageNumber = n;
    r.parent = parent;
    TEST_ASSERT_TRUE(gB.add(r));
  };
  add(Stage::Take, 2, 0);         // 1
  add(Stage::Building, 4, 1);     // 2
  add(Stage::Take, 3, 2);         // 3
  add(Stage::Arrangement, 1, 1);  // 4
  gB.setWorking(4, 2);            // working from B04
  gB.open();
  gB.press();
  TEST_ASSERT_EQUAL(B::View::Popup, gB.view());
  TEST_ASSERT_EQUAL_UINT16(4, gB.historyCount());
  TEST_ASSERT_EQUAL_UINT16(3, gB.historyRecord(0));  // A01 newest
  TEST_ASSERT_EQUAL_UINT16(2, gB.historyBase());     // B04's row
  TEST_ASSERT_FALSE(gB.historyOnLine(0));            // A01
  TEST_ASSERT_FALSE(gB.historyOnLine(1));            // T03
  TEST_ASSERT_TRUE(gB.historyOnLine(2));             // B04, the base
  TEST_ASSERT_TRUE(gB.historyOnLine(3));             // T02, its parent
  TEST_ASSERT_EQUAL(Stage::Building, gB.suggested());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_opening_selects_save_over_a_load_section_of_sets_newest_first);
  RUN_TEST(test_close_closes_and_the_divider_is_passed);
  RUN_TEST(test_record_and_edit_stop_at_both_ends);
  RUN_TEST(test_a_set_opens_on_its_newest_milestone_and_loads_it);
  RUN_TEST(test_back_returns_to_the_set_row);
  RUN_TEST(test_the_popup_shows_next_ids_suggests_the_base_stage_and_saves_into_the_working_set);
  RUN_TEST(test_as_new_set_saves_sketch_one_of_a_new_set);
  RUN_TEST(test_the_popup_runs_from_as_new_set_to_final);
  RUN_TEST(test_the_suggestion_follows_the_evidence_since_the_base);
  RUN_TEST(test_arrangement_and_final_are_never_inferred_and_new_work_is_sketch);
  RUN_TEST(test_hold_closes_the_save_popup_and_asks_nothing);
  RUN_TEST(test_hold_on_a_set_asks_to_trash_it_and_cancel_changes_nothing);
  RUN_TEST(test_a_trashed_set_leaves_the_sets_for_the_trash);
  RUN_TEST(test_the_trash_lists_its_items_newest_first);
  RUN_TEST(test_the_trash_row_opens_the_trash_and_is_not_the_nearest_set);
  RUN_TEST(test_restoring_a_stage_brings_its_set_back);
  RUN_TEST(test_a_set_item_takes_only_the_stages_still_shown);
  RUN_TEST(test_emptying_the_trash_asks_again_and_removes_for_good);
  RUN_TEST(test_trashing_a_stage_keeps_the_set_and_selects_the_next_stage);
  RUN_TEST(test_trashing_the_last_stage_leaves_the_set_view);
  RUN_TEST(test_trashing_the_working_set_keeps_its_row_and_base);
  RUN_TEST(test_a_failed_trash_keeps_the_set_and_retries);
  RUN_TEST(test_a_set_trash_takes_only_earlier_stages);
  RUN_TEST(test_a_failure_keeps_the_screen_and_selection_and_track_tries_again);
  RUN_TEST(test_a_failed_save_goes_back_to_its_list_and_retries_the_same_save);
  RUN_TEST(test_the_list_scrolls_with_the_selection);
  RUN_TEST(test_the_popup_history_follows_the_base_line);
  RUN_TEST(test_the_line_and_its_side_branch);
  RUN_TEST(test_a_straight_history_has_one_lane);
  return UNITY_END();
}
