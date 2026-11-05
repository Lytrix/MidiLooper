//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The NOTE_EDIT edit writer (fixed-memory-engine E4.5; owner decision 2026-11-05: hear each edit, rewrite one run).
//
// * Session: `begin` admits a selection of at most `MaxSelection` notes of the slot's current manifest (their noteId
//   and geometry) and opens the History NOTE_EDIT session, which protects that manifest as the restore point.
// * Edits: `apply` changes the geometry of selected notes (tick, length, pitch, velocity, channel) or deletes them. It
//   is one E: step. The E: steps live in this writer's log (at most `MaxSteps`, the oldest forgotten when full), not in
//   History: each step records the values it replaced, and undo / redo swap them back.
// * Audible at once: every apply / undo / redo is new input. At acceptance it reserves the rows of a fresh edit run
//   (CE-A1: refused by name, nothing changed, only when content is full); the rebuild job (`step`) then writes the run
//   in grains — HideNote rows for every edited or deleted note, then the replacement notes in tick order (NoteOff
//   before NoteOn at a tick), keeping each note's noteId (DEC-245 lineage) — seals it, publishes base runs + edit run as
//   a new manifest, and makes it the session's one History state (the previous one is released to reclaim). With no
//   note edited the session state is removed and the slot plays its start. A newer action supersedes a running
//   rebuild (its reservation is forgiven, its run reclaimed).
// * `leave` keeps the current state as one U: step (after a running rebuild completes); `abandon` returns to the start.
// No allocation; `init()` writes every field.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/History.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/RowReservations.h"
#include "ContentEngine/RunTable.h"

namespace ContentEngine {

/// One selected note: its identity and geometry. `deleted` notes are hidden and not replaced.
struct EditNote {
  uint32_t noteId;
  uint32_t tick;    // NoteOn position in the loop
  uint32_t length;  // NoteOn to NoteOff, ticks (> 0)
  uint8_t pitch, velocity, channel;  // channel 0..15
  uint8_t deleted;
  bool operator==(const EditNote& o) const {
    return noteId == o.noteId && tick == o.tick && length == o.length && pitch == o.pitch && velocity == o.velocity &&
           channel == o.channel && deleted == o.deleted;
  }
  bool operator!=(const EditNote& o) const { return !(*this == o); }
};

/// One change of an E: step: selected note `index` takes `value` (its noteId is kept).
struct EditChange {
  uint16_t index;
  EditNote value;
};

enum class EditResult : uint8_t {
  Ok,
  NotEditing,
  Busy,             // a NOTE_EDIT session is already open, or the slot has another session
  EmptySlot,        // nothing to edit
  DeniedSelection,  // more than the selection holds, or a bad index / geometry
  DeniedRunFull,    // the base manifest has no room for an edit run
  DeniedRun,        // no run descriptor
  DeniedCapacity,   // the edit run's rows do not fit (content full)
  NothingToUndo,
  NothingToRedo,
};

template <typename Pool, typename Runs, typename Manifests, typename HistoryT,
          uint16_t MaxSelection = Config::kEditSelection, uint8_t MaxSteps = Config::kEditSteps,
          uint16_t MaxLogEntries = Config::kEditLogEntries>
class EditWriter {
 public:
  using Reservations = RowReservations<Pool, Runs>;
  static constexpr uint16_t kNoIndex = 0xFFFFu;
  static_assert(MaxLogEntries >= MaxSelection, "one step may change the whole selection");

  struct Stats {
    uint32_t rebuilds;     // edit runs published
    uint32_t superseded;   // rebuilds abandoned for a newer action
    uint32_t forgotSteps;  // oldest E: steps forgotten (log full)
  };

  void init(Pool& pool, Runs& runs, Manifests& manifests, Reservations& res, HistoryT& history) {
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    res_ = &res;
    history_ = &history;
    open_ = false;
    count_ = 0;
    stepHead_ = stepCount_ = cursor_ = 0;
    entryHead_ = entryCount_ = 0;
    clearBuild();
    leaving_ = false;
    stats_ = Stats{};
  }

  /// Opens NOTE_EDIT on `slot` with `n` selected notes of its current manifest.
  EditResult begin(uint8_t slot, const EditNote* selection, uint16_t n) {
    if (open_) return EditResult::Busy;
    if (n > MaxSelection) return EditResult::DeniedSelection;
    const ManifestId base = history_->current(slot);
    if (base == kNoManifest) return EditResult::EmptySlot;
    if (manifests_->manifest(base).runCount >= Manifests::kMaxRuns) return EditResult::DeniedRunFull;
    const uint32_t loop = manifests_->manifest(base).loopLengthTicks;
    for (uint16_t i = 0; i < n; ++i)
      if (!valid(selection[i]) || selection[i].tick >= loop) return EditResult::DeniedSelection;
    if (!history_->beginSession(SessionKind::Edit, slot)) return EditResult::Busy;
    open_ = true;
    slot_ = slot;
    base_ = base;
    loop_ = loop;
    count_ = n;
    for (uint16_t i = 0; i < n; ++i) orig_[i] = cur_[i] = selection[i];
    stepHead_ = stepCount_ = cursor_ = 0;
    entryHead_ = entryCount_ = 0;
    clearBuild();
    leaving_ = false;
    return EditResult::Ok;
  }

  /// One E: step: `n` changes, applied together. Refused by name, nothing changed, when the edit run does not fit.
  EditResult apply(const EditChange* changes, uint16_t n) {
    if (!open_ || leaving_) return EditResult::NotEditing;
    if (n == 0 || n > MaxSelection) return EditResult::DeniedSelection;
    for (uint16_t k = 0; k < n; ++k)
      if (changes[k].index >= count_ || !valid(changes[k].value) || changes[k].value.tick >= loop_ ||
          changes[k].value.noteId != orig_[changes[k].index].noteId)
        return EditResult::DeniedSelection;
    for (uint16_t k = 0; k < n; ++k) swapIn(changes[k].index, changes[k].value, scratchOld_[k]);
    const EditResult r = rebuildAfterChange();
    if (r != EditResult::Ok) {  // undo the trial
      for (uint16_t k = n; k-- > 0;) cur_[changes[k].index] = scratchOld_[k];
      return r;
    }
    // Record the step: drop redo, forget the oldest steps while the log is full.
    stepCount_ = cursor_;
    entryCount_ = 0;
    for (uint8_t k = 0; k < stepCount_; ++k) entryCount_ = static_cast<uint16_t>(entryCount_ + steps_[(stepHead_ + k) % MaxSteps].count);
    while (stepCount_ == MaxSteps || entryCount_ + n > MaxLogEntries) forgetOldest();
    const uint16_t first = static_cast<uint16_t>((entryHead_ + entryCount_) % MaxLogEntries);
    for (uint16_t k = 0; k < n; ++k) log_[(first + k) % MaxLogEntries] = EditChange{changes[k].index, scratchOld_[k]};
    steps_[(stepHead_ + stepCount_) % MaxSteps] = Step{first, n};
    ++stepCount_;
    entryCount_ = static_cast<uint16_t>(entryCount_ + n);
    cursor_ = stepCount_;
    return EditResult::Ok;
  }

  EditResult undo() {
    if (!open_ || leaving_) return EditResult::NotEditing;
    if (cursor_ == 0) return EditResult::NothingToUndo;
    swapStep(cursor_ - 1, true);
    const EditResult r = rebuildAfterChange();
    if (r != EditResult::Ok) {
      swapStep(cursor_ - 1, false);
      return r;
    }
    --cursor_;
    return EditResult::Ok;
  }
  EditResult redo() {
    if (!open_ || leaving_) return EditResult::NotEditing;
    if (cursor_ == stepCount_) return EditResult::NothingToRedo;
    swapStep(cursor_, false);
    const EditResult r = rebuildAfterChange();
    if (r != EditResult::Ok) {
      swapStep(cursor_, true);
      return r;
    }
    ++cursor_;
    return EditResult::Ok;
  }

  /// Leave NOTE_EDIT keeping the edits: one U: step. Completes after a running rebuild (`step`).
  void leave() {
    if (!open_) return;
    leaving_ = true;
    if (!building()) finishLeave();
  }
  /// Leave NOTE_EDIT discarding the edits: the slot returns to its session start. Immediate.
  void abandon() {
    if (!open_) return;
    cancelBuild();
    history_->abandonSession(SessionKind::Edit);
    open_ = false;
    leaving_ = false;
  }

  /// One grain of the rebuild job: at most `maxRows` rows written. Returns true while work remains.
  bool step(uint32_t maxRows) {
    if (!building()) return false;
    uint32_t budget = maxRows;
    while (budget > 0 && buildHide_ < buildHideCount_) {
      res_->writeOwed(buildRun_, hideRow(cur_[buildHides_[buildHide_++]].noteId));
      --budget;
    }
    if (buildHide_ < buildHideCount_) return true;
    if (!sorted_) {
      sortEvents();
      sorted_ = true;
      return true;  // sorting is this grain's work
    }
    while (budget > 0 && buildEvent_ < buildEventCount_) {
      res_->writeOwed(buildRun_, eventRow(events_[buildEvent_++]));
      --budget;
    }
    if (buildEvent_ < buildEventCount_) return true;
    publish();
    if (leaving_) finishLeave();
    return false;
  }

  bool editing() const { return open_; }
  bool building() const { return buildRun_ != kNoRun; }
  uint8_t depth() const { return cursor_; }  // the E: level
  uint8_t redoDepth() const { return static_cast<uint8_t>(stepCount_ - cursor_); }
  uint16_t selected() const { return count_; }
  const EditNote& note(uint16_t index) const { return cur_[index]; }
  const Stats& stats() const { return stats_; }

 private:
  struct Step {
    uint16_t first;  // log position, already mod MaxLogEntries
    uint16_t count;
  };

  static bool valid(const EditNote& e) { return e.length > 0 && e.pitch < 128 && e.velocity > 0 && e.velocity < 128 && e.channel < 16; }

  void swapIn(uint16_t index, const EditNote& value, EditNote& old) {
    old = cur_[index];
    cur_[index] = value;
  }
  /// Undo (newest change first) or redo (oldest first) of step `s`: swap each recorded value with the current one, so
  /// the log then holds what the other direction restores. A step may change one note twice.
  void swapStep(uint8_t s, bool undoing) {
    const Step& st = steps_[(stepHead_ + s) % MaxSteps];
    for (uint16_t j = 0; j < st.count; ++j) {
      const uint16_t k = undoing ? static_cast<uint16_t>(st.count - 1 - j) : j;
      EditChange& c = log_[(st.first + k) % MaxLogEntries];
      const EditNote t = cur_[c.index];
      cur_[c.index] = c.value;
      c.value = t;
    }
  }
  void forgetOldest() {
    const Step& st = steps_[stepHead_];
    entryHead_ = static_cast<uint16_t>((st.first + st.count) % MaxLogEntries);
    entryCount_ = static_cast<uint16_t>(entryCount_ - st.count);
    stepHead_ = static_cast<uint8_t>((stepHead_ + 1) % MaxSteps);
    --stepCount_;  // only called while recording a step, which sets the cursor after
    ++stats_.forgotSteps;
  }

  /// Admission of a changed selection: cancel a running rebuild, then reserve a fresh edit run for the new state (or,
  /// with nothing edited, return the slot to the session start at once).
  EditResult rebuildAfterChange() {
    uint16_t hides = 0, events = 0;
    for (uint16_t i = 0; i < count_; ++i) {
      if (cur_[i] == orig_[i]) continue;
      ++hides;
      if (!cur_[i].deleted) events = static_cast<uint16_t>(events + 2);
    }
    if (hides == 0) {
      cancelBuild();
      history_->setSessionState(SessionKind::Edit, kNoManifest);
      return EditResult::Ok;
    }
    const RunId run = runs_->allocate(slot_, RunKind::kEdit);
    if (run == kNoRun) return EditResult::DeniedRun;
    const Admission a = res_->reserve(run, static_cast<uint16_t>(hides + events));
    if (a != Admission::Ok) {
      runs_->seal(run);
      runs_->release(run);  // empty: reclaimed at once by the queue
      return a == Admission::DeniedRunFull ? EditResult::DeniedSelection : EditResult::DeniedCapacity;
    }
    cancelBuild();
    buildRun_ = run;
    buildHideCount_ = 0;
    buildEventCount_ = 0;
    for (uint16_t i = 0; i < count_; ++i) {
      if (cur_[i] == orig_[i]) continue;
      buildHides_[buildHideCount_++] = i;
      if (cur_[i].deleted) continue;
      events_[buildEventCount_++] = key(cur_[i].tick, true, i);
      events_[buildEventCount_++] = key(cur_[i].tick + cur_[i].length, false, i);
    }
    buildHide_ = buildEvent_ = 0;
    sorted_ = false;
    return EditResult::Ok;
  }

  /// Event key: tick, then NoteOff before NoteOn, then selection index (deterministic).
  static uint64_t key(uint32_t tick, bool on, uint16_t index) {
    return (static_cast<uint64_t>(tick) << 17) | (static_cast<uint64_t>(on ? 1 : 0) << 16) | index;
  }
  void sortEvents() {  // heapsort, in place, O(E log E), E <= 2 x MaxSelection
    const uint16_t n = buildEventCount_;
    auto sift = [&](uint16_t root, uint16_t end) {
      while (2u * root + 1u < end) {
        uint16_t child = static_cast<uint16_t>(2u * root + 1u);
        if (child + 1u < end && events_[child] < events_[child + 1]) ++child;
        if (!(events_[root] < events_[child])) return;
        const uint64_t t = events_[root];
        events_[root] = events_[child];
        events_[child] = t;
        root = child;
      }
    };
    for (uint16_t i = n / 2; i-- > 0;) sift(i, n);
    for (uint16_t end = n; end > 1; --end) {
      const uint64_t t = events_[0];
      events_[0] = events_[end - 1];
      events_[end - 1] = t;
      sift(0, static_cast<uint16_t>(end - 1));
    }
  }

  static PassEvent hideRow(uint32_t noteId) {
    PassEvent e{};
    e.noteId = noteId;
    e.status = kHideNoteStatus;
    return e;
  }
  PassEvent eventRow(uint64_t k) const {
    const uint16_t i = static_cast<uint16_t>(k & 0xFFFFu);
    const bool on = (k >> 16) & 1u;
    const EditNote& n = cur_[i];
    PassEvent e{};
    e.tick = static_cast<uint32_t>(k >> 17);
    e.noteId = n.noteId;
    e.status = static_cast<uint8_t>((on ? 0x90 : 0x80) | n.channel);
    e.data1 = n.pitch;
    e.data2 = on ? n.velocity : 0;
    return e;
  }

  void publish() {
    runs_->seal(buildRun_);
    const auto& base = manifests_->manifest(base_);
    RunId list[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < base.runCount; ++i) list[i] = base.runs[i];
    list[base.runCount] = buildRun_;
    const ManifestId m = manifests_->create(slot_, base.revision + 1, base.loopLengthTicks, list,
                                            static_cast<uint8_t>(base.runCount + 1), *runs_);
    runs_->release(buildRun_);  // the manifest holds the run now
    buildRun_ = kNoRun;
    if (m == kNoManifest) return;  // unreachable inside W (manifest slots cover it); the slot keeps its last state
    history_->setSessionState(SessionKind::Edit, m);
    ++stats_.rebuilds;
  }
  void cancelBuild() {
    if (!building()) return;
    res_->forgive(buildRun_);
    runs_->seal(buildRun_);
    runs_->release(buildRun_);
    buildRun_ = kNoRun;
    ++stats_.superseded;
  }
  void clearBuild() {
    buildRun_ = kNoRun;
    buildHide_ = buildHideCount_ = buildEvent_ = buildEventCount_ = 0;
    sorted_ = false;
  }
  void finishLeave() {
    history_->endSession(SessionKind::Edit);
    open_ = false;
    leaving_ = false;
  }

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  Reservations* res_ = nullptr;
  HistoryT* history_ = nullptr;
  bool open_ = false, leaving_ = false, sorted_ = false;
  uint8_t slot_ = 0;
  ManifestId base_ = kNoManifest;
  uint32_t loop_ = 0;
  uint16_t count_ = 0;
  EditNote orig_[MaxSelection];
  EditNote cur_[MaxSelection];
  EditNote scratchOld_[MaxSelection];
  // E: log
  Step steps_[MaxSteps];
  uint8_t stepHead_ = 0, stepCount_ = 0, cursor_ = 0;
  EditChange log_[MaxLogEntries];
  uint16_t entryHead_ = 0, entryCount_ = 0;
  // Rebuild
  RunId buildRun_ = kNoRun;
  uint16_t buildHides_[MaxSelection];
  uint16_t buildHide_ = 0, buildHideCount_ = 0;
  uint64_t events_[2 * MaxSelection];
  uint16_t buildEvent_ = 0, buildEventCount_ = 0;
  Stats stats_{};
};

}  // namespace ContentEngine
