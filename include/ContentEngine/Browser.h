//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The sets browser's state (sets browser slice 3; design: `docs/Plans/engine_sets_browser_proposal.md`, owner decisions
// 2026-11-08 / 09). What the screen shows and what the four buttons do; the owner of the library and the set carries
// out what it asks (`takeRequest`) and answers `succeeded` or `failed`.
//
// * The index, in memory: every published milestone's record, in publication order (`add`), and per set its newest
//   milestone. Sets are listed newest first, a set's milestones newest first.
// * Views (owner 2026-11-09: saving and loading split, no return row). Top: `CLOSE`, `SAVE`, a `LOAD` divider (not
//   selectable), then the working set and the other sets, newest first. A set: `< SETS`, then its milestones newest
//   first. The save popup (`SAVE CURRENT`): `AS NEW SET` (Sketch 01 of the next set number), then every stage with the
//   id it would get, the suggested one selected; beside it the working set's history from its base. A failure: what,
//   why, `NOTHING CHANGED`, over the view it came from.
// * Buttons (rules 2–4): RECORD up, EDIT down (stopping at both ends, passing the divider); TRACK does what the hint
//   says for the selected row; HOLD TRACK closes the popup or the browser, changing nothing. Opening selects `SAVE`.
// * The suggestion (slice 4, proposal §3): one stage by one rule. No base milestone: Sketch. A new set from a base: the
//   base's stage. Otherwise the base's stage, unless everything since it is of one kind (`Activity`): content changed
//   only → Building, or a provisional Take when every change was a recording begun while another track played;
//   launches, switches, stops, mutes and solos only → Exploration. Arrangement and Final are never inferred: a base of
//   either keeps its stage. Mixed evidence keeps the base's stage.
// No allocation; the record array is the caller's memory (PSRAM on the device).

#pragma once

#include <cstdint>
#include <cstring>

#include "ContentEngine/Library.h"

namespace ContentEngine {

template <uint16_t MaxRecords = library::kIndexRecords>
class Browser {
 public:
  enum class View : uint8_t { Closed, Top, Set, Popup, Failed };
  enum class RowKind : uint8_t { Close, Save, Divider, Working, Set, Back, Milestone };
  struct Row {
    RowKind kind;
    uint16_t set;     // Working, Set: the set
    uint16_t record;  // Set: its newest milestone; Milestone: the milestone (kNone: none)
  };
  enum class Action : uint8_t { None, Save, Load };
  struct Request {
    Action action = Action::None;
    uint16_t record = 0;  // Load
    uint16_t set = 0;     // Save: the set it goes into
    library::Stage stage = library::Stage::Sketch;
    uint8_t stageNumber = 0;
  };
  static constexpr uint16_t kNone = 0xFFFF;
  static constexpr uint8_t kStages = static_cast<uint8_t>(library::Stage::kCount);

  // ---- The index ---------------------------------------------------------------------------------------------------
  void clear() {
    count_ = sets_ = 0;
    view_ = View::Closed;
  }
  /// A published milestone, in publication order. False: the cache is full.
  bool add(const library::Record& r) {
    if (count_ >= MaxRecords) return false;
    records_[count_] = r;
    uint16_t i = 0;
    while (i < sets_ && setNumber_[i] != r.set) ++i;
    if (i == sets_) {
      setNumber_[sets_] = r.set;
      ++sets_;
    }
    setLatest_[i] = count_;  // publication order: the newest
    ++count_;
    return true;
  }
  uint16_t count() const { return count_; }
  const library::Record& record(uint16_t i) const { return records_[i]; }
  uint16_t setCount() const { return sets_; }
  /// The next stage number of `stage` in `set`.
  uint8_t nextNumber(uint16_t set, library::Stage stage) const {
    uint8_t high = 0;
    for (uint16_t i = 0; i < count_; ++i)
      if (records_[i].set == set && records_[i].stage == stage && records_[i].stageNumber > high)
        high = records_[i].stageNumber;
    return static_cast<uint8_t>(high + 1);
  }
  /// A set number no milestone uses: one past the highest.
  uint16_t newSetNumber() const {
    uint16_t high = 0;
    for (uint16_t i = 0; i < sets_; ++i)
      if (setNumber_[i] > high) high = setNumber_[i];
    return static_cast<uint16_t>(high + 1);
  }
  /// The cache index of the milestone published as `sequence` (kNone: none).
  uint16_t find(uint32_t sequence) const {
    for (uint16_t i = 0; i < count_; ++i)
      if (records_[i].sequence == sequence) return i;
    return kNone;
  }

  // ---- The working set (its owner keeps it current) --------------------------------------------------------------
  /// The set worked in and the milestone it descends from (sequence, 0: none).
  void setWorking(uint16_t set, uint32_t baseSequence) {
    workingSet_ = set;
    base_ = baseSequence;
  }
  uint16_t workingSet() const { return workingSet_; }
  uint32_t baseSequence() const { return base_; }

  /// What happened since the base milestone (or the boot): content changed (`build`), only through recordings begun
  /// while another track played (`takesOnly`), and whether launches / switches / stops / mutes / solos were asked for.
  struct Activity {
    bool build = false, takesOnly = false, explore = false;
  };
  void setActivity(const Activity& a) { activity_ = a; }
  /// The suggestion for a save into the working set (`newSet` false) or a new set; `provisional`: a Take inferred.
  library::Stage suggest(bool newSet, bool& provisional) const {
    provisional = false;
    const uint16_t b = find(base_);
    if (base_ == 0 || b == kNone) return library::Stage::Sketch;  // new work with no saved origin
    const library::Stage base = records_[b].stage;
    if (newSet || base == library::Stage::Arrangement || base == library::Stage::Final) return base;
    const Activity& a = activity_;
    if (a.build && a.explore) return base;  // mixed: the base's stage
    if (a.build) {
      provisional = a.takesOnly;
      return a.takesOnly ? library::Stage::Take : library::Stage::Building;
    }
    return a.explore ? library::Stage::Exploration : base;
  }

  // ---- Navigation --------------------------------------------------------------------------------------------------
  /// HOLD TRACK on the performance screen: the top level, `SAVE` selected.
  void open() {
    view_ = View::Top;
    request_ = Request{};
    selected_ = 1;
    first_ = 0;
    scroll();
  }
  void close() { view_ = View::Closed; }
  bool isOpen() const { return view_ != View::Closed; }
  View view() const { return view_; }
  /// The view a failure or the popup lies over.
  View listView() const { return list_; }

  /// RECORD: up.
  void up() {
    if (view_ == View::Popup) {
      if (stageSel_ > 0) --stageSel_;
      return;
    }
    if (view_ != View::Top && view_ != View::Set) return;
    if (selected_ > 0) --selected_;
    if (row(selected_).kind == RowKind::Divider && selected_ > 0) --selected_;  // the divider is passed
    scroll();
  }
  /// EDIT: down.
  void down() {
    if (view_ == View::Popup) {
      if (stageSel_ < kStages) ++stageSel_;  // 0 AS NEW SET, 1..6 the stages
      return;
    }
    if (view_ != View::Top && view_ != View::Set) return;
    if (selected_ + 1 < rowCount()) ++selected_;
    if (row(selected_).kind == RowKind::Divider) {
      if (selected_ + 1 < rowCount()) ++selected_;
      else --selected_;
    }
    scroll();
  }
  /// TRACK: what the hint says.
  void press() {
    switch (view_) {
      case View::Popup:  // AS NEW SET: Sketch 01 of a new set; else the stage, next in the working set
        request_.action = Action::Save;
        request_.set = stageSel_ == 0 ? newSetNumber() : workingSet_;
        request_.stage = stageSel_ == 0 ? library::Stage::Sketch : static_cast<library::Stage>(stageSel_ - 1);
        request_.stageNumber = nextNumber(request_.set, request_.stage);
        pending_ = request_;
        return;
      case View::Failed:  // try again
        view_ = list_;
        request_ = pending_;
        return;
      case View::Top:
      case View::Set:
        break;
      default:
        return;
    }
    const Row r = row(selected_);
    switch (r.kind) {
      case RowKind::Close:
        view_ = View::Closed;
        return;
      case RowKind::Save:
        openPopup();
        return;
      case RowKind::Divider:
        return;
      case RowKind::Working:
      case RowKind::Set:
        openSet(r.set);
        return;
      case RowKind::Back:
        view_ = View::Top;
        selected_ = setRowOf(openSet_);
        first_ = 0;
        scroll();
        return;
      case RowKind::Milestone:
        request_ = Request{};
        request_.action = Action::Load;
        request_.record = r.record;
        pending_ = request_;
        return;
    }
  }
  /// HOLD TRACK: the popup closes (back to its list), anything else closes the browser. Nothing changes.
  void hold() {
    if (view_ == View::Popup) {
      view_ = list_;
      return;
    }
    view_ = View::Closed;
  }
  /// What the owner is to do now (once).
  bool takeRequest(Request& out) {
    if (request_.action == Action::None) return false;
    out = request_;
    request_ = Request{};
    return true;
  }
  /// The request succeeded: the browser closes (the owner shows its one line).
  void succeeded() { view_ = View::Closed; }
  /// The request failed: the screen and the selection stay, with what and why; TRACK tries again. `changed`: the set is
  /// not as it was (a load that could not restore it) — the screen says so instead of NOTHING CHANGED.
  void failed(const char* what, const char* why, bool changed = false) {
    changed_ = changed;
    if (view_ == View::Popup) view_ = list_;
    list_ = view_ == View::Failed ? list_ : view_;
    copy(failWhat_, what);
    copy(failWhy_, why);
    view_ = View::Failed;
  }
  const char* failWhat() const { return failWhat_; }
  const char* failWhy() const { return failWhy_; }
  bool failChanged() const { return changed_; }

  // ---- Rows --------------------------------------------------------------------------------------------------------
  /// The rows of the list shown (the popup's and a failure's: the list under them).
  uint16_t rowCount() const {
    const View v = shownList();
    if (v == View::Set) return static_cast<uint16_t>(1 + members_);
    return static_cast<uint16_t>(4 + otherSets());
  }
  Row row(uint16_t i) const {
    if (shownList() == View::Set) {
      if (i == 0) return Row{RowKind::Back, openSet_, kNone};
      return Row{RowKind::Milestone, openSet_, member(static_cast<uint16_t>(i - 1))};
    }
    if (i == 0) return Row{RowKind::Close, workingSet_, kNone};
    if (i == 1) return Row{RowKind::Save, workingSet_, kNone};
    if (i == 2) return Row{RowKind::Divider, workingSet_, kNone};
    if (i == 3) return Row{RowKind::Working, workingSet_, latestOf(workingSet_)};
    const uint16_t s = otherSet(static_cast<uint16_t>(i - 4));
    return Row{RowKind::Set, setNumber_[s], setLatest_[s]};
  }
  uint16_t selected() const { return selected_; }
  /// The first row shown (the list scrolls with the selection).
  uint16_t first() const { return first_; }
  static constexpr uint8_t kVisible = 5;
  uint16_t openSetNumber() const { return openSet_; }
  /// The newest milestone of `set` (kNone: none).
  uint16_t latestOf(uint16_t set) const {
    for (uint16_t i = 0; i < sets_; ++i)
      if (setNumber_[i] == set) return setLatest_[i];
    return kNone;
  }
  /// How many milestones `set` has.
  uint16_t milestonesIn(uint16_t set) const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < count_; ++i) n = static_cast<uint16_t>(n + (records_[i].set == set ? 1 : 0));
    return n;
  }

  /// Slice 5: in the open set, whether row `i` (1: its newest milestone) lies on the newest milestone's line; whether
  /// any milestone branched off it; the row where the side joins the line (0: none).
  bool onLine(uint16_t i) const { return i >= 1 && i <= members_ && line_[i - 1]; }
  bool hasSide() const {
    for (uint16_t k = 0; k < members_; ++k)
      if (!line_[k]) return true;
    return false;
  }
  uint16_t joinRow() const { return join_ == kNone ? 0 : static_cast<uint16_t>(join_ + 1); }

  // ---- The save popup ----------------------------------------------------------------------------------------------
  uint16_t popupSet() const { return workingSet_; }
  /// 0: AS NEW SET; 1..6: the stages (Sketch .. Final).
  uint8_t popupSelected() const { return stageSel_; }
  /// The working set's milestones newest first (the popup's history): how many, which, and whether one lies on the
  /// base's line (the base and its parents; others branched off); the base's row (kNone: none).
  uint16_t historyCount() const { return members_; }
  uint16_t historyRecord(uint16_t k) const { return member_[k]; }
  bool historyOnLine(uint16_t k) const { return k < members_ && line_[k]; }
  uint16_t historyBase() const { return baseRow_; }
  library::Stage suggested() const { return suggested_; }
  /// The suggestion is a provisional Take (marked as such).
  bool suggestedProvisional() const { return provisional_; }

 private:
  static void copy(char* out, const char* s) {
    uint8_t i = 0;
    for (; s != nullptr && s[i] != 0 && i < kText - 1; ++i) out[i] = s[i];
    out[i] = 0;
  }
  View shownList() const { return view_ == View::Popup || view_ == View::Failed ? list_ : view_; }
  /// Sets other than the working one, newest milestone first.
  uint16_t otherSets() const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < sets_; ++i) n = static_cast<uint16_t>(n + (setNumber_[i] != workingSet_ ? 1 : 0));
    return n;
  }
  /// The `k`-th other set, newest first: the set slots ordered by their newest milestone, descending.
  uint16_t otherSet(uint16_t k) const {
    // Selection by rank: the set whose newest milestone has exactly `k` newer ones among the other sets.
    for (uint16_t i = 0; i < sets_; ++i) {
      if (setNumber_[i] == workingSet_) continue;
      uint16_t newer = 0;
      for (uint16_t j = 0; j < sets_; ++j)
        if (setNumber_[j] != workingSet_ && setLatest_[j] > setLatest_[i]) ++newer;
      if (newer == k) return i;
    }
    return 0;
  }
  uint16_t setRowOf(uint16_t set) const {
    const uint16_t n = rowCount();
    for (uint16_t i = 0; i < n; ++i) {
      const Row r = row(i);
      if ((r.kind == RowKind::Working || r.kind == RowKind::Set) && r.set == set) return i;
    }
    return 0;
  }
  void openSet(uint16_t set) {
    openSet_ = set;
    fillMembers(set);
    markLine();
    view_ = View::Set;
    selected_ = members_ != 0 ? 1 : 0;  // rule 5: its newest milestone
    first_ = 0;
    scroll();
  }
  uint16_t member(uint16_t k) const { return member_[k]; }
  /// Slice 5, the fork lanes: the open set's line — its newest milestone and that one's parents within the set — and the
  /// side of it (milestones that branched off; drawn on a lane of their own, joined where they grew from).
  void markLine(uint16_t from = 0) {
    for (uint16_t k = 0; k < members_; ++k) line_[k] = false;
    join_ = kNone;
    if (members_ == 0 || from >= members_) return;
    for (uint16_t k = from;;) {  // `from` (the newest, or the base), then each parent in the set
      line_[k] = true;
      const uint32_t parent = records_[member_[k]].parent;
      uint16_t next = kNone;
      for (uint16_t j = static_cast<uint16_t>(k + 1); parent != 0 && j < members_; ++j)
        if (records_[member_[j]].sequence == parent) next = j;
      if (next == kNone) break;
      k = next;
    }
    uint16_t oldestSide = kNone;
    for (uint16_t k = 0; k < members_; ++k)
      if (!line_[k]) oldestSide = k;
    if (oldestSide == kNone) return;
    for (uint16_t k = 0; k < members_; ++k)  // the side's root: where it grew from, on the line
      if (line_[k] && records_[member_[k]].sequence == records_[member_[oldestSide]].parent) join_ = k;
  }
  void fillMembers(uint16_t set) {
    members_ = 0;
    for (uint16_t i = count_; i-- > 0;)  // newest first
      if (records_[i].set == set) member_[members_++] = i;
  }
  /// The save popup over the top level: the working set's history with the base's line marked, the suggestion
  /// selected.
  void openPopup() {
    list_ = view_;
    fillMembers(workingSet_);
    baseRow_ = kNone;
    for (uint16_t k = 0; k < members_; ++k)
      if (records_[member_[k]].sequence == base_) baseRow_ = k;
    markLine(baseRow_ == kNone ? 0 : baseRow_);
    suggested_ = suggest(false, provisional_);
    stageSel_ = static_cast<uint8_t>(static_cast<uint8_t>(suggested_) + 1);
    view_ = View::Popup;
  }
  void scroll() {
    if (selected_ < first_) first_ = selected_;
    if (selected_ >= first_ + kVisible) first_ = static_cast<uint16_t>(selected_ - kVisible + 1);
  }

  static constexpr uint8_t kText = 24;
  library::Record records_[MaxRecords];
  uint16_t setNumber_[MaxRecords], setLatest_[MaxRecords], member_[MaxRecords];
  bool line_[MaxRecords] = {};
  uint16_t join_ = kNone, baseRow_ = kNone;
  uint16_t count_ = 0, sets_ = 0, members_ = 0;
  uint16_t workingSet_ = 1, openSet_ = 0;
  uint32_t base_ = 0;
  bool changed_ = false, provisional_ = false;
  Activity activity_{};
  View view_ = View::Closed, list_ = View::Top;
  uint16_t selected_ = 0, first_ = 0;
  uint8_t stageSel_ = 0;
  library::Stage suggested_ = library::Stage::Sketch;
  Request request_{}, pending_{};
  char failWhat_[kText] = {}, failWhy_[kText] = {};
};

using ProductionBrowser = Browser<>;

}  // namespace ContentEngine
