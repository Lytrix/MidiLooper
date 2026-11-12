//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The sets browser's state (DEC-263, the sets browser on the DROID). What the screen shows and what the four buttons
// do; the owner of the library and the set carries out what it asks (`takeRequest`) and answers `succeeded` or
// `failed`.
//
// * The index, in memory: every published milestone's entry, in publication order (`add`), and per set its newest
//   milestone. Sets are listed newest first, a set's milestones newest first.
// * Views (saving and loading split, no return row; the dividers name the intent). Top: `< CLOSE`, `SAVE SET`, a `LOAD
//   SET` divider (not selectable), then the working set and the other sets, newest first, and `TRASH` last (the second
//   way in). A set: `< SETS`, a `LOAD STAGE` divider, then its milestones newest first. The trash: `< SETS`, `EMPTY
//   TRASH`, a `RESTORE SET/STAGE` divider, then what is in it newest first. The save popup (`SAVE CURRENT`): `AS NEW
//   SET` (Sketch 01 of the next set number), then every stage with the id it would get, the suggested one selected;
//   beside it the working set's history from its base. A failure: what, why, `NOTHING CHANGED`, over the view it came
//   from.
// * Buttons (rules 2–4): RECORD up, EDIT down (stopping at both ends, passing the divider); TRACK does what the hint
//   says for the selected row; a double TRACK opens the trash (and from it, the sets). HOLD TRACK on a set or a stage
//   asks to move it to the trash (`YES` / `CANCEL`, CANCEL selected; TRACK confirms, HOLD TRACK cancels); it closes the
//   save popup and a failure, changing nothing. In the trash TRACK restores an item; `EMPTY TRASH` asks again before
//   removing everything in it for good. Opening selects `SAVE SET`.
// * The trash: trash, restore and empty-trash entries are `add`ed like milestones, in card order. A trash entry is an
//   item: the milestone it names, or every shown milestone of its set before it (its stages and history together).
//   Restoring an item shows its milestones again (and so their set); emptying removes every item for good. Entries keep
//   their place (a row's entry is its card index) and every milestone counts for numbering, so no id or set number is
//   used twice.
// * The suggestion (proposal §3): one stage by one rule. No base milestone: Sketch. A new set from a base: the base's
//   stage. Otherwise the base's stage, unless everything since it is of one kind (`Activity`): content changed only →
//   Building, or a provisional Take when every change was a recording begun while another track played; launches,
//   switches, stops, mutes and solos only → Exploration. Arrangement and Final are never inferred: a base of either
//   keeps its stage. Mixed evidence keeps the base's stage.
// * A long press always exits a window, and trash is destructive: the surface closes the browser on any long press;
//   `trashSelected` (a double TRACK) moves a stage, or with a set's last row `MOVE SET TO TRASH` the open set, to the
//   trash at once, and `undoTrash` (a triple TRACK) brings the last one back. The trash opens from its `TRASH` row.
//   `hold` and its confirmation remain the model's (not bound on the DROID).
// * Get a loop: a set's view has a `GET A LOOP` row; TRACK lists the set's stages (`FROM STAGE`), TRACK on one opens
//   the picker — the stage's slots as a grid (tracks as rows, slots as columns, from its mosaic) with a column of
//   empty-loop presets (1–16 bars) left of slot 1 — for a destination fixed on entry (the selected slot, or the slot
//   whose long press opened it: `openPickerFromSlot`, on the working set's base stage); for an empty destination the
//   cursor starts on the preset closest to the new-loop length (`setNewLoopBars`). Moves: NOTELEN up, PLAY/STOP down,
//   REC left, EDIT right (`pickUp` …). TRACK on a loop asks to audition it, once heard (or previewed silently) to USE
//   it; on a preset to USE an empty loop — over a filled destination after a confirmation. A move ends an audition;
//   HOLD TRACK leaves it (back to the stages). The preview the panel shows is the owner's: it reads the loop under the
//   cursor (`previewWanted`, a generation per cursor position) and answers with its generation.
// No allocation; the entry array is the caller's memory (PSRAM on the device).

#pragma once

#include <cstdint>
#include <cstring>

#include "ContentEngine/Storage/SetLibraryFormat.h"

namespace ContentEngine {

template <uint16_t MaxEntries = library::kIndexEntries>
class SetBrowser {
 public:
  enum class View : uint8_t { Closed, Top, Set, Bin, Popup, Confirm, Failed, Stages, Picker };
  enum class RowKind : uint8_t { Close, Save, Divider, Working, Set, Back, Milestone, Empty, Item, Trash, GetLoop, TrashSet };
  struct Row {
    RowKind kind;
    uint16_t set;     // Working, Set, Item: the set
    uint16_t entry;  // Set: its newest milestone; Milestone: the milestone (kNone: none); Item: the trash entry
  };
  /// The get-a-loop picker: Audition (entry, from, into), Use (the same: placed as one undo step), UseEmpty (bars,
  /// into), CancelAudition, LeavePicker (any audition ended, the preview dropped).
  enum class Action : uint8_t {
    None, Save, Load, Trash, Restore, Empty, Audition, Use, UseEmpty, CancelAudition, LeavePicker
  };
  /// What the confirmation asks: a set or stage to the trash, the trash emptied, or an empty loop over a
  /// filled destination.
  enum class Confirm : uint8_t { ToTrash, Empty, EmptyLoop };
  struct Request {
    Action action = Action::None;
    uint16_t entry = 0;  // Load; Trash: the stage (kNone: the whole set); Restore: the trash entry
    uint16_t set = 0;     // Save: the set it goes into; Trash, Restore: the set
    library::Stage stage = library::Stage::Sketch;
    uint8_t stageNumber = 0;
    uint8_t from = 0, into = 0, bars = 0;  // the source slot, the destination, an empty loop's bars
  };
  /// The preview of the loop under the cursor, as the owner reads it; the audition, as the owner runs it.
  enum class Preview : uint8_t { None, Reading, Ready, Failed, TooBig };
  enum class Audition : uint8_t { None, Pending, Sounding, Silent };
  static constexpr uint8_t kPresets = 5;
  static constexpr uint8_t kPresetBars[kPresets] = {1, 2, 4, 8, 16};

  static constexpr uint16_t kNone = 0xFFFF;
  static constexpr uint8_t kStages = static_cast<uint8_t>(library::Stage::kCount);

  // ---- The index ---------------------------------------------------------------------------------------------------
  void clear() {
    count_ = sets_ = items_ = 0;
    lastTrash_ = kNone;
    view_ = View::Closed;
  }
  /// A published entry, in publication order: a milestone, or a trash / restore / empty-trash entry.
  /// False: the cache is full.
  bool add(const library::Entry& r) {
    if (count_ >= MaxEntries) return false;
    const uint16_t at = count_++;
    entries_[at] = r;
    trashedBy_[at] = kNone;
    gone_[at] = false;
    switch (r.kind) {
      case library::kMilestone:
        addToSets(at);
        return true;
      case library::kTrash: {  // the milestone named, or the set's shown ones before it; an item that took none is spent
        bool took = false;
        for (uint16_t i = 0; i < at; ++i)
          if (shown(i) && entries_[i].set == r.set &&
              (r.parent == 0 ? entries_[i].sequence < r.sequence : entries_[i].sequence == r.parent)) {
            trashedBy_[i] = at;
            took = true;
          }
        gone_[at] = !took;
        break;
      }
      case library::kRestore:
        for (uint16_t j = 0; j < at; ++j)
          if (inTrash(j) && entries_[j].sequence == r.parent) {
            gone_[j] = true;
            for (uint16_t i = 0; i < at; ++i)
              if (trashedBy_[i] == j) trashedBy_[i] = kNone;
          }
        break;
      case library::kEmptyTrash:  // each item spent: its milestones stay tied to it, never shown again
        for (uint16_t j = 0; j < at; ++j)
          if (inTrash(j)) gone_[j] = true;
        break;
      default:
        break;
    }
    indexSets();
    return true;
  }
  /// Whether entry `i` is not a shown milestone (trash-side, in the trash or removed: not loadable from the browser).
  bool hidden(uint16_t i) const { return i >= count_ || !shown(i); }
  /// Whether trash entry `i` is an item still in the trash.
  bool inTrash(uint16_t i) const {
    return i < count_ && entries_[i].kind == library::kTrash && !gone_[i];
  }
  /// The trash: its items newest first (trash entries), and an item's milestones — how many, the newest.
  uint16_t itemCount() const { return items_; }
  uint16_t item(uint16_t k) const { return item_[k]; }
  uint16_t itemSize(uint16_t trash) const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < count_; ++i) n = static_cast<uint16_t>(n + (trashedBy_[i] == trash ? 1 : 0));
    return n;
  }
  uint16_t itemNewest(uint16_t trash) const {
    for (uint16_t i = count_; i-- > 0;)
      if (trashedBy_[i] == trash) return i;
    return kNone;
  }
  uint16_t count() const { return count_; }
  const library::Entry& entry(uint16_t i) const { return entries_[i]; }
  uint16_t setCount() const { return sets_; }
  /// The next stage number of `stage` in `set`.
  uint8_t nextNumber(uint16_t set, library::Stage stage) const {
    uint8_t high = 0;
    for (uint16_t i = 0; i < count_; ++i)
      if (entries_[i].set == set && entries_[i].stage == stage && entries_[i].stageNumber > high)
        high = entries_[i].stageNumber;
    return static_cast<uint8_t>(high + 1);
  }
  /// A set number no milestone uses, a deleted one included, nor the working set: one past the highest (numbers are
  /// never reused).
  uint16_t newSetNumber() const {
    uint16_t high = workingSet_;
    for (uint16_t i = 0; i < count_; ++i)
      if (entries_[i].set > high) high = entries_[i].set;
    return static_cast<uint16_t>(high + 1);
  }
  /// The working set a boot starts in, once the entries are added: the one the card's header names — kept
  /// with no shown milestone, its base only while that is shown in it — else the newest shown milestone's set, no base.
  struct Working {
    uint16_t set = 1;
    uint32_t base = 0;
  };
  Working bootWorking(const library::IndexHeader& h) const {
    Working w;
    if (h.hasWorking) {
      w.set = h.workingSet;
      const uint16_t b = h.workingBase != 0 ? find(h.workingBase) : kNone;
      if (b != kNone && shown(b) && entries_[b].set == w.set) w.base = h.workingBase;
      return w;
    }
    for (uint16_t i = count_; i-- > 0;)
      if (shown(i)) {
        w.set = entries_[i].set;
        break;
      }
    return w;
  }
  /// The cache index of the milestone published as `sequence` (kNone: none).
  uint16_t find(uint32_t sequence) const {
    for (uint16_t i = 0; i < count_; ++i)
      if (entries_[i].sequence == sequence) return i;
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
    const library::Stage base = entries_[b].stage;
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
  /// The list on screen: the view's own, or the one a popup or failure lies over.
  View shownView() const { return shownList(); }

  /// RECORD: up.
  void up() {
    if (view_ == View::Popup) {
      if (stageSel_ > 0) --stageSel_;
      return;
    }
    if (view_ == View::Confirm) {
      confirmSel_ = 0;  // YES
      return;
    }
    if (!listing()) return;
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
    if (view_ == View::Confirm) {
      confirmSel_ = 1;  // CANCEL
      return;
    }
    if (!listing()) return;
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
      case View::Confirm:  // YES: what it asks; CANCEL: back, nothing changed
        if (confirmSel_ == 1) {
          view_ = list_;
          return;
        }
        if (confirm_ == Confirm::EmptyLoop) {
          view_ = View::Picker;
          useEmpty();
          return;
        }
        request_ = Request{};
        request_.action = confirm_ == Confirm::Empty ? Action::Empty : Action::Trash;
        request_.set = confirmSet_;
        request_.entry = confirmEntry_;
        pending_ = request_;
        return;
      case View::Failed:  // try again
        view_ = list_;
        request_ = pending_;
        return;
      case View::Picker:
        pickPress();
        return;
      case View::Top:
      case View::Set:
      case View::Bin:
      case View::Stages:
        break;
      default:
        return;
    }
    const Row r = row(selected_);
    switch (r.kind) {
      case RowKind::GetLoop:
        openStages();
        return;
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
      case RowKind::Back: {
        if (view_ == View::Stages) {  // back to the set
          view_ = View::Set;
          selected_ = 1;
          first_ = 0;
          scroll();
          return;
        }
        const bool fromBin = view_ == View::Bin;
        view_ = View::Top;
        selected_ = fromBin ? 1 : setRowOf(openSet_);
        first_ = 0;
        scroll();
        return;
      }
      case RowKind::Empty:
        if (items_ != 0) openConfirm(Confirm::Empty, 0, kNone);
        return;
      case RowKind::Trash:
        openTrash();
        return;
      case RowKind::TrashSet:  // a double TRACK only (`trashSelected`)
        return;
      case RowKind::Item:  // restored at once: nothing is lost by it
        list_ = view_;
        request_ = Request{};
        request_.action = Action::Restore;
        request_.set = r.set;
        request_.entry = r.entry;
        pending_ = request_;
        return;
      case RowKind::Milestone:
        if (view_ == View::Stages) {  // SELECT STAGE: its loops
          openPicker(r.entry, false);
          return;
        }
        request_ = Request{};
        request_.action = Action::Load;
        request_.entry = r.entry;
        pending_ = request_;
        return;
    }
  }
  /// HOLD TRACK: on a set with stages or on a stage, asks to move it to the trash (CANCEL selected); a popup
  /// closes (back to its list), a failure closes the browser; on any other row, and in the trash, nothing. Nothing
  /// changes.
  void hold() {
    switch (view_) {
      case View::Popup:
      case View::Confirm:
        view_ = list_;
        return;
      case View::Picker:  // BACK: the audition ended, the preview dropped; the stages
        request_ = Request{};
        request_.action = Action::LeavePicker;
        audition_ = Audition::None;
        preview_ = Preview::None;
        openStages(pickEntry_);
        return;
      case View::Stages:
        view_ = View::Set;
        selected_ = 1;
        first_ = 0;
        scroll();
        return;
      case View::Failed:
        view_ = View::Closed;
        return;
      case View::Top:
      case View::Set:
        break;
      default:
        return;
    }
    const Row r = row(selected_);
    if (r.kind == RowKind::Milestone) openConfirm(Confirm::ToTrash, r.set, r.entry);
    else if ((r.kind == RowKind::Working || r.kind == RowKind::Set) && latestOf(r.set) != kNone)
      openConfirm(Confirm::ToTrash, r.set, kNone);
  }
  /// A double TRACK: in a set, the selected stage — or on `MOVE SET TO TRASH` the set — to the
  /// trash at once; elsewhere nothing.
  void trashSelected() {
    if (view_ != View::Set) return;
    const Row r = row(selected_);
    if (r.kind != RowKind::Milestone && r.kind != RowKind::TrashSet) return;
    if (r.kind == RowKind::TrashSet && latestOf(r.set) == kNone) return;  // no stages: nothing to move
    list_ = view_;
    request_ = Request{};
    request_.action = Action::Trash;
    request_.set = r.set;
    request_.entry = r.kind == RowKind::Milestone ? r.entry : kNone;
    pending_ = request_;
  }
  /// A triple TRACK: what the last trash moved comes back (while it is still in the trash).
  void undoTrash() {
    if (lastTrash_ == kNone || !inTrash(lastTrash_)) return;
    if (view_ != View::Top && view_ != View::Set && view_ != View::Bin) return;
    list_ = view_;
    request_ = Request{};
    request_.action = Action::Restore;
    request_.set = entries_[lastTrash_].set;
    request_.entry = lastTrash_;
    pending_ = request_;
    lastTrash_ = kNone;
  }
  /// The trash entry a triple TRACK would restore (kNone: none).
  uint16_t lastTrash() const { return lastTrash_ != kNone && inTrash(lastTrash_) ? lastTrash_ : kNone; }
  /// A double TRACK: the trash from the sets or a set (its newest item selected), the sets from the trash.
  void openTrash() {
    if (view_ == View::Bin) {
      view_ = View::Top;
      selected_ = 1;
    } else if (view_ == View::Top || view_ == View::Set) {
      view_ = View::Bin;
      selected_ = items_ != 0 ? kBinHead : 0;
    } else {
      return;
    }
    first_ = 0;
    scroll();
  }
  /// The confirmation: what it asks, its set and stage (kNone: the whole set); 0 YES, 1 CANCEL.
  Confirm confirmKind() const { return confirm_; }
  uint16_t confirmSet() const { return confirmSet_; }
  uint16_t confirmEntry() const { return confirmEntry_; }
  uint8_t confirmSelected() const { return confirmSel_; }
  /// A trash, restore or empty-trash asked for was published (its entry `add`ed): back to the list, the nearest row
  /// still there selected; a set left without stages leaves its view for the sets.
  void marked() {
    if (pending_.action == Action::Trash && count_ != 0 && entries_[count_ - 1].kind == library::kTrash)
      lastTrash_ = static_cast<uint16_t>(count_ - 1);  // a triple TRACK brings it back
    view_ = list_;
    if (view_ == View::Set) {
      fillMembers(openSet_);
      markLine();
      if (members_ == 0) {
        view_ = View::Top;
        selected_ = openSetRow_;
      }
    }
    const uint16_t n = rowCount();
    if (selected_ >= n) selected_ = static_cast<uint16_t>(n - 1);
    if (row(selected_).kind == RowKind::Divider) selected_ = static_cast<uint16_t>(selected_ + 1 < n ? selected_ + 1 : selected_ - 1);
    if (row(selected_).kind == RowKind::Trash) --selected_;  // the nearest set, not the way into the trash
    first_ = 0;
    scroll();
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
    if (view_ == View::Popup || view_ == View::Confirm) view_ = list_;
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
    if (v == View::Set) return static_cast<uint16_t>(kSetHead + members_ + 1);  // its stages, then MOVE SET TO TRASH
    if (v == View::Stages) return static_cast<uint16_t>(kStagesHead + members_);
    if (v == View::Bin) return static_cast<uint16_t>(kBinHead + items_);
    return static_cast<uint16_t>(kTopHead + otherSets() + 1);  // the sets, then TRASH
  }
  Row row(uint16_t i) const {
    if (shownList() == View::Bin) {
      if (i == 0) return Row{RowKind::Back, 0, kNone};
      if (i == 1) return Row{RowKind::Empty, 0, kNone};
      if (i == 2) return Row{RowKind::Divider, 0, kNone};
      const uint16_t t = item_[i - kBinHead];
      return Row{RowKind::Item, entries_[t].set, t};
    }
    if (shownList() == View::Set) {
      if (i == 0) return Row{RowKind::Back, openSet_, kNone};
      if (i == 1) return Row{RowKind::GetLoop, openSet_, kNone};
      if (i == 2) return Row{RowKind::Divider, openSet_, kNone};
      if (i == kSetHead + members_) return Row{RowKind::TrashSet, openSet_, kNone};
      return Row{RowKind::Milestone, openSet_, member(static_cast<uint16_t>(i - kSetHead))};
    }
    if (shownList() == View::Stages) {
      if (i == 0) return Row{RowKind::Back, openSet_, kNone};
      if (i == 1) return Row{RowKind::Divider, openSet_, kNone};
      return Row{RowKind::Milestone, openSet_, member(static_cast<uint16_t>(i - kStagesHead))};
    }
    if (i == 0) return Row{RowKind::Close, workingSet_, kNone};
    if (i == 1) return Row{RowKind::Save, workingSet_, kNone};
    if (i == 2) return Row{RowKind::Divider, workingSet_, kNone};
    if (i == 3) return Row{RowKind::Working, workingSet_, latestOf(workingSet_)};
    if (i == kTopHead + otherSets()) return Row{RowKind::Trash, 0, kNone};
    const uint16_t s = otherSet(static_cast<uint16_t>(i - kTopHead));
    return Row{RowKind::Set, setNumber_[s], setLatest_[s]};
  }
  uint16_t selected() const { return selected_; }
  /// The first row shown (the list scrolls with the selection).
  uint16_t first() const { return first_; }
  static constexpr uint8_t kVisible = 5;
  /// A set's rows before its milestones: `< SETS`, `GET A LOOP` and the `LOAD STAGE` divider.
  static constexpr uint16_t kSetHead = 3;
  /// The stages to get a loop from: `< BACK` and the `FROM STAGE` divider, then the set's milestones.
  static constexpr uint16_t kStagesHead = 2;
  /// The first milestone row of the list shown.
  uint16_t milestoneHead() const { return shownList() == View::Stages ? kStagesHead : kSetHead; }
  /// The top level's rows before the other sets: `< CLOSE`, `SAVE SET`, the `LOAD SET` divider, the working set.
  static constexpr uint16_t kTopHead = 4;
  /// The trash's rows before its items: `< SETS`, `EMPTY TRASH`, the `RESTORE` divider.
  static constexpr uint16_t kBinHead = 3;
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
    for (uint16_t i = 0; i < count_; ++i) n = static_cast<uint16_t>(n + (entries_[i].set == set && shown(i) ? 1 : 0));
    return n;
  }

  /// In the open set, whether row `i` (kSetHead: its newest milestone) lies on the newest milestone's line; whether
  /// any milestone branched off it; the row where the side joins the line (0: none).
  bool onLine(uint16_t i) const {
    const uint16_t h = milestoneHead();
    return i >= h && i < h + members_ && line_[i - h];
  }
  bool hasSide() const {
    for (uint16_t k = 0; k < members_; ++k)
      if (!line_[k]) return true;
    return false;
  }
  uint16_t joinRow() const { return join_ == kNone ? 0 : static_cast<uint16_t>(join_ + milestoneHead()); }

  // ---- Get a loop's picker ------------------------------------------------------------------------------
  /// The destination entry 1 uses (the selected slot) and whether it holds a loop; the owner keeps it current while
  /// the browser is closed, as the working set.
  void setPickDestination(uint8_t slot, bool filled) {
    if (view_ != View::Picker && view_ != View::Confirm) {
      pickDest_ = slot;
      pickDestFilled_ = filled;
    }
  }
  /// The new-loop length in bars (the surface's global setting): the picker starts on the preset closest to it.
  void setNewLoopBars(uint16_t bars) { newLoopBars_ = bars; }
  /// The preset closest to the new-loop length (a tie: the longer one).
  uint8_t startPreset() const {
    uint8_t best = 0;
    for (uint8_t i = 1; i < kPresets; ++i) {
      const int32_t d = static_cast<int32_t>(kPresetBars[i]) - newLoopBars_, db = static_cast<int32_t>(kPresetBars[best]) - newLoopBars_;
      if ((d < 0 ? -d : d) <= (db < 0 ? -db : db)) best = i;
    }
    return best;
  }
  /// Entry 2 (a slot's long press): the picker over the working set's base stage (else its newest; none: presets
  /// only) for `slot`.
  void openPickerFromSlot(uint8_t slot, bool filled) {
    pickDest_ = slot;
    pickDestFilled_ = filled;
    request_ = Request{};
    openSet_ = workingSet_;
    uint16_t src = find(base_);
    if (src == kNone || !shown(src) || entries_[src].set != workingSet_) src = latestOf(workingSet_);
    openPicker(src, true);
  }
  /// The cursor's moves: NOTELEN up, PLAY/STOP down, REC left, EDIT right. Each ends an audition and starts a preview
  /// generation.
  void pickUp() { pickMove(0, -1); }
  void pickDown() { pickMove(0, 1); }
  void pickLeft() { pickMove(-1, 0); }
  void pickRight() { pickMove(1, 0); }
  bool picking() const { return view_ == View::Picker || (view_ == View::Confirm && list_ == View::Picker); }
  uint16_t pickEntry() const { return pickEntry_; }
  uint8_t pickDestination() const { return pickDest_; }
  bool pickDestinationFilled() const { return pickDestFilled_; }
  /// The cursor: a preset (`pickPreset` 0..4, else kNoPreset) or the grid's track and slot.
  static constexpr uint8_t kNoPreset = 0xFF;
  uint8_t pickPreset() const { return pickPreset_; }
  uint8_t pickTrack() const { return pickTrack_; }
  uint8_t pickSlotIndex() const { return pickSlot_; }
  /// The source slot under the cursor (track × 8 + slot).
  uint8_t pickSource() const { return static_cast<uint8_t>(pickTrack_ * 8 + pickSlot_); }
  /// The stage's slot `slot` has a loop (its mosaic level, 0: empty).
  uint8_t pickLevel(uint8_t slot) const {
    if (pickEntry_ == kNone || slot >= 64) return 0;
    return static_cast<uint8_t>((entries_[pickEntry_].mosaic[slot / 4] >> (2 * (slot % 4))) & 3u);
  }
  /// The loop the owner should preview: the stage and slot under the cursor and its generation; false on a preset, an
  /// empty slot, or no picker.
  bool previewWanted(uint32_t& generation, uint16_t& entry, uint8_t& slot) const {
    if (!picking() || pickPreset_ != kNoPreset || pickLevel(pickSource()) == 0) return false;
    generation = generation_;
    entry = pickEntry_;
    slot = pickSource();
    return true;
  }
  uint32_t pickGeneration() const { return generation_; }
  /// The owner's preview of generation `generation` (an older one is ignored); its bars when Ready.
  void setPreview(uint32_t generation, Preview p, uint16_t bars = 0) {
    if (generation != generation_) return;
    preview_ = p;
    previewBars_ = bars;
  }
  Preview preview() const { return preview_; }
  uint16_t previewBars() const { return previewBars_; }
  /// The owner's audition: Sounding, or Silent (`why`: the owner's reason, shown) once it is ready; None when it ended.
  void setAudition(Audition a, uint8_t why = 0) {
    if (audition_ == Audition::None && a != Audition::None) return;  // ended meanwhile (a move): not this one
    audition_ = a;
    silentWhy_ = why;
  }
  Audition audition() const { return audition_; }
  uint8_t silentWhy() const { return silentWhy_; }
  /// The confirmation's empty loop: its bars.
  uint8_t confirmBars() const { return confirmBars_; }

  // ---- The save popup ----------------------------------------------------------------------------------------------
  uint16_t popupSet() const { return workingSet_; }
  /// 0: AS NEW SET; 1..6: the stages (Sketch .. Final).
  uint8_t popupSelected() const { return stageSel_; }
  /// The working set's milestones newest first (the popup's history): how many, which, and whether one lies on the
  /// base's line (the base and its parents; others branched off); the base's row (kNone: none).
  uint16_t historyCount() const { return members_; }
  uint16_t historyEntry(uint16_t k) const { return member_[k]; }
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
  View shownList() const {
    return view_ == View::Popup || view_ == View::Confirm || view_ == View::Failed ? list_ : view_;
  }
  bool listing() const {
    return view_ == View::Top || view_ == View::Set || view_ == View::Bin || view_ == View::Stages;
  }
  /// A milestone neither in the trash nor removed.
  bool shown(uint16_t i) const {
    return entries_[i].kind == library::kMilestone && trashedBy_[i] == kNone;
  }
  /// The sets with a shown milestone and each one's newest, and the trash's items newest first, from the entries.
  void indexSets() {
    sets_ = 0;
    items_ = 0;
    for (uint16_t i = 0; i < count_; ++i)
      if (shown(i)) addToSets(i);
    for (uint16_t i = count_; i-- > 0;)
      if (inTrash(i)) item_[items_++] = i;
  }
  void addToSets(uint16_t r) {
    uint16_t i = 0;
    while (i < sets_ && setNumber_[i] != entries_[r].set) ++i;
    if (i == sets_) {
      setNumber_[sets_] = entries_[r].set;
      ++sets_;
    }
    setLatest_[i] = r;  // publication order: the newest
  }
  void openConfirm(Confirm what, uint16_t set, uint16_t entry) {
    list_ = view_;
    confirm_ = what;
    confirmSet_ = set;
    confirmEntry_ = entry;
    confirmSel_ = 1;  // CANCEL
    view_ = View::Confirm;
  }
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
    openSetRow_ = selected_;
    openSet_ = set;
    fillMembers(set);
    markLine();
    view_ = View::Set;
    selected_ = members_ != 0 ? kSetHead : 0;  // rule 5: its newest milestone
    first_ = 0;
    scroll();
  }
  uint16_t member(uint16_t k) const { return member_[k]; }
  /// The fork lanes: the open set's line — its newest milestone and that one's parents within the set — and the
  /// side of it (milestones that branched off; drawn on a lane of their own, joined where they grew from).
  void markLine(uint16_t from = 0) {
    for (uint16_t k = 0; k < members_; ++k) line_[k] = false;
    join_ = kNone;
    if (members_ == 0 || from >= members_) return;
    for (uint16_t k = from;;) {  // `from` (the newest, or the base), then each parent in the set
      line_[k] = true;
      const uint32_t parent = entries_[member_[k]].parent;
      uint16_t next = kNone;
      for (uint16_t j = static_cast<uint16_t>(k + 1); parent != 0 && j < members_; ++j)
        if (entries_[member_[j]].sequence == parent) next = j;
      if (next == kNone) break;
      k = next;
    }
    uint16_t oldestSide = kNone;
    for (uint16_t k = 0; k < members_; ++k)
      if (!line_[k]) oldestSide = k;
    if (oldestSide == kNone) return;
    for (uint16_t k = 0; k < members_; ++k)  // the side's root: where it grew from, on the line
      if (line_[k] && entries_[member_[k]].sequence == entries_[member_[oldestSide]].parent) join_ = k;
  }
  void fillMembers(uint16_t set) {
    members_ = 0;
    for (uint16_t i = count_; i-- > 0;)  // newest first
      if (entries_[i].set == set && shown(i)) member_[members_++] = i;
  }
  /// The save popup over the top level: the working set's history with the base's line marked, the suggestion
  /// selected.
  void openPopup() {
    list_ = view_;
    fillMembers(workingSet_);
    baseRow_ = kNone;
    for (uint16_t k = 0; k < members_; ++k)
      if (entries_[member_[k]].sequence == base_) baseRow_ = k;
    markLine(baseRow_ == kNone ? 0 : baseRow_);
    suggested_ = suggest(false, provisional_);
    stageSel_ = static_cast<uint8_t>(static_cast<uint8_t>(suggested_) + 1);
    view_ = View::Popup;
  }
  /// The open set's stages to get a loop from, `select` (a milestone) selected, else its newest.
  void openStages(uint16_t select = kNone) {
    fillMembers(openSet_);
    markLine();
    view_ = View::Stages;
    selected_ = members_ != 0 ? kStagesHead : 0;
    for (uint16_t k = 0; k < members_; ++k)
      if (member_[k] == select) selected_ = static_cast<uint16_t>(kStagesHead + k);
    first_ = 0;
    scroll();
  }
  /// The picker over milestone `entry` (kNone: presets only) for the destination; the cursor on the 4-bar
  /// preset for an empty destination, else on the destination's own position.
  void openPicker(uint16_t entry, bool fromSlot) {
    pickEntry_ = entry;
    pickFromSlot_ = fromSlot;
    if (pickDestFilled_) {
      pickPreset_ = kNoPreset;
      pickTrack_ = static_cast<uint8_t>(pickDest_ / 8);
      pickSlot_ = static_cast<uint8_t>(pickDest_ % 8);
    } else {
      pickPreset_ = startPreset();
      pickTrack_ = 0;
      pickSlot_ = 0;
    }
    audition_ = Audition::None;
    preview_ = Preview::None;
    ++generation_;
    view_ = View::Picker;
  }
  void pickMove(int8_t dx, int8_t dy) {
    if (view_ != View::Picker) return;
    if (pickPreset_ != kNoPreset) {
      if (dy < 0 && pickPreset_ > 0) --pickPreset_;
      else if (dy > 0 && pickPreset_ + 1 < kPresets) ++pickPreset_;
      else if (dx > 0) {  // into the grid at the nearest track
        pickTrack_ = static_cast<uint8_t>((pickPreset_ * 8 * 2 + kPresets) / (2 * kPresets));  // round(8p / 5)
        if (pickTrack_ > 7) pickTrack_ = 7;
        pickSlot_ = 0;
        pickPreset_ = kNoPreset;
      } else {
        return;
      }
    } else if (dx < 0 && pickSlot_ == 0) {  // to the nearest preset
      pickPreset_ = static_cast<uint8_t>((pickTrack_ * kPresets * 2 + 8) / 16);  // round(5t / 8)
      if (pickPreset_ >= kPresets) pickPreset_ = kPresets - 1;
    } else if (dx < 0) {
      --pickSlot_;
    } else if (dx > 0 && pickSlot_ < 7) {
      ++pickSlot_;
    } else if (dy < 0 && pickTrack_ > 0) {
      --pickTrack_;
    } else if (dy > 0 && pickTrack_ < 7) {
      ++pickTrack_;
    } else {
      return;
    }
    ++generation_;  // a new position: a preview of its own; browsing never changes the sound
    preview_ = Preview::None;
    if (audition_ != Audition::None) {
      audition_ = Audition::None;
      request_ = Request{};
      request_.action = Action::CancelAudition;
    }
  }
  /// TRACK in the picker: a preset is used (asked again over a filled destination); a loop is auditioned, then used.
  void pickPress() {
    if (pickPreset_ != kNoPreset) {
      if (pickDestFilled_) {
        openConfirm(Confirm::EmptyLoop, 0, kNone);
        confirmBars_ = kPresetBars[pickPreset_];
        return;
      }
      useEmpty();
      return;
    }
    if (pickLevel(pickSource()) == 0) return;  // an empty slot of the stage: nothing to get
    if (audition_ == Audition::Pending) return;  // not heard yet
    request_ = Request{};
    request_.action = audition_ == Audition::None ? Action::Audition : Action::Use;
    request_.entry = pickEntry_;
    request_.from = pickSource();
    request_.into = pickDest_;
    pending_ = request_;
    if (audition_ == Audition::None) audition_ = Audition::Pending;
  }
  void useEmpty() {
    request_ = Request{};
    request_.action = Action::UseEmpty;
    request_.into = pickDest_;
    request_.bars = kPresetBars[pickPreset_ == kNoPreset ? startPreset() : pickPreset_];
    pending_ = request_;
  }
  void scroll() {
    if (selected_ < first_) first_ = selected_;
    if (selected_ >= first_ + kVisible) first_ = static_cast<uint16_t>(selected_ - kVisible + 1);
  }

  static constexpr uint8_t kText = 24;
  library::Entry entries_[MaxEntries];
  uint16_t setNumber_[MaxEntries], setLatest_[MaxEntries], member_[MaxEntries];
  bool line_[MaxEntries] = {};
  // A milestone's trash entry (kNone: none); a trash entry spent (its item restored, emptied, or empty).
  uint16_t trashedBy_[MaxEntries], item_[MaxEntries];
  bool gone_[MaxEntries] = {};
  uint16_t items_ = 0;
  uint16_t join_ = kNone, baseRow_ = kNone;
  uint16_t count_ = 0, sets_ = 0, members_ = 0;
  uint16_t workingSet_ = 1, openSet_ = 0, openSetRow_ = kTopHead;
  Confirm confirm_ = Confirm::ToTrash;
  uint16_t confirmSet_ = 0, confirmEntry_ = kNone;
  uint8_t confirmSel_ = 1;
  uint32_t base_ = 0;
  bool changed_ = false, provisional_ = false;
  Activity activity_{};
  View view_ = View::Closed, list_ = View::Top;
  uint16_t selected_ = 0, first_ = 0;
  uint8_t stageSel_ = 0;
  library::Stage suggested_ = library::Stage::Sketch;
  Request request_{}, pending_{};
  uint16_t lastTrash_ = kNone;  // the trash entry a triple TRACK restores
  // The picker — its stage (a milestone), destination, cursor (a preset, or a track and slot), the preview
  // generation and state, the audition.
  uint16_t pickEntry_ = kNone;
  uint8_t pickDest_ = 0, pickPreset_ = kNoPreset, pickTrack_ = 0, pickSlot_ = 0, confirmBars_ = 0, silentWhy_ = 0;
  bool pickDestFilled_ = false, pickFromSlot_ = false;
  uint32_t generation_ = 0;
  Preview preview_ = Preview::None;
  uint16_t previewBars_ = 0;
  Audition audition_ = Audition::None;
  uint16_t newLoopBars_ = 4;
  char failWhat_[kText] = {}, failWhy_[kText] = {};
};

using ProductionBrowser = SetBrowser<>;

}  // namespace ContentEngine
