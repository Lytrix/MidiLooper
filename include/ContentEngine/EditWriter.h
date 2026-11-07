//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The NOTE_EDIT edit writer (fixed-memory-engine E4.5; owner decision 2026-11-05: hear each edit, rewrite one run).
//
// * Session: `begin` admits a selection of at most `MaxSelection` notes of the slot's current manifest (their noteId
//   and geometry) and opens the History NOTE_EDIT session, which protects that manifest as the restore point.
// * Edits: `apply` changes the geometry of selected notes (tick, length, pitch, velocity, channel) or deletes them,
//   each note at most once per step. It is one E: step. The E: steps live in this writer's log (at most `MaxSteps`,
//   the oldest forgotten when full; entries in caller-placed storage), not in History: each step records the values
//   it replaced, and undo / redo swap them back.
// * Audible at once: every apply / undo / redo is new input. At acceptance it reserves the rows of a fresh edit run
//   (CE-A1: refused by name, nothing changed, only when content is full). Acceptance is O(changes of the action): the
//   edited / live note counts are kept incrementally. The rebuild job (`step`) then works in bounded units — gather
//   (an edited note's HideNote row), heapify and extract (one sift each), write (one replacement row; tick order,
//   NoteOff before NoteOn at a tick, noteIds kept, DEC-245 lineage) — seals the run, publishes base runs + edit run as
//   a new manifest, and makes it the session's one History state (the previous one is released to reclaim). With no
//   note edited the session state is removed and the slot plays its start. A newer action supersedes a running
//   rebuild (its reservation is forgiven, its run reclaimed).
// * Create (E9.8b-2, the legacy NOTELEN double press on an empty bracket): `create` adds a note to the session as one
//   that was deleted at its start, and one E: step brings it to life — undo returns it to deleted (nothing written; its
//   hide row hides nothing), redo brings it back. Its noteId is the caller's (fresh).
// * `leave` keeps the current state as one U: step (after a running rebuild completes); `abandon` returns to the start.
// * Overlap (owner 2026-11-07; legacy `resolveConstrainedGeometry` and DEC-030's sticky end of participation; EDIT-1:
//   an edit run is not an occluder, so NOTE_EDIT writes its own results): the causing note is the selected note A
//   (`cause`), once it has been edited and while it lives; it is never changed. Any other live note B of A's pitch
//   (channel does not matter; edited or not) that starts inside A is hidden; one that starts before A and sounds into
//   it ends at A's start; edges that only touch change nothing — on linear spans across the loop's wrap. While A stays
//   selected the results follow it live from B's state (moving A on, by position or pitch, brings B back); they are
//   written like edits (B's hide row, and a shortened B's replacement rows). `settle` (A deselected, or NOTE_EDIT left)
//   makes A's results B's own state, recorded in A's last E: step, so undoing that step brings B back. Acceptance
//   counts the rows: O(selection).
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
  static_assert(MaxLogEntries >= MaxSelection, "one step may change the whole selection");
  static_assert(MaxSelection <= 1024, "sort keys hold a 10-bit selection index");

  struct Stats {
    uint32_t rebuilds;     // edit runs published
    uint32_t superseded;   // rebuilds abandoned for a newer action
    uint32_t forgotSteps;  // oldest E: steps forgotten (log full)
  };

  /// `log`: storage for `MaxLogEntries` E: changes; the caller places it (PSRAM: it is touched only by the changed
  /// notes of an action).
  void init(Pool& pool, Runs& runs, Manifests& manifests, Reservations& res, HistoryT& history, EditChange* log) {
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    res_ = &res;
    history_ = &history;
    log_ = log;
    open_ = false;
    leaving_ = false;
    count_ = edited_ = live_ = 0;
    stepHead_ = stepCount_ = cursor_ = 0;
    entryHead_ = entryCount_ = 0;
    buildRun_ = kNoRun;
    phase_ = Phase::Idle;
    causer_ = kNoCause;
    stats_ = Stats{};
  }

  /// Opens NOTE_EDIT on `slot` with `n` selected notes of its current manifest. O(n).
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
    leaving_ = false;
    slot_ = slot;
    base_ = base;
    loop_ = loop;
    count_ = n;
    edited_ = live_ = 0;
    for (uint16_t i = 0; i < n; ++i) orig_[i] = cur_[i] = selection[i];
    stepHead_ = stepCount_ = cursor_ = 0;
    entryHead_ = entryCount_ = 0;
    buildRun_ = kNoRun;
    phase_ = Phase::Idle;
    causer_ = kNoCause;
    return EditResult::Ok;
  }

  /// One E: step: `n` changes, each note at most once. O(n). Refused by name, nothing changed, when the new edit run
  /// does not fit.
  EditResult apply(const EditChange* changes, uint16_t n) {
    if (!open_ || leaving_) return EditResult::NotEditing;
    if (n == 0 || n > MaxSelection) return EditResult::DeniedSelection;
    uint32_t seen[(MaxSelection + 31) / 32] = {};
    int32_t dEdited = 0, dLive = 0;
    for (uint16_t k = 0; k < n; ++k) {
      const EditChange& c = changes[k];
      if (c.index >= count_ || !valid(c.value) || c.value.tick >= loop_ || c.value.noteId != orig_[c.index].noteId ||
          ((seen[c.index >> 5] >> (c.index & 31)) & 1u))
        return EditResult::DeniedSelection;
      seen[c.index >> 5] |= 1u << (c.index & 31);
      delta(c.index, c.value, dEdited, dLive);
    }
    const EditResult r = admit(dEdited, dLive, overlapRows([&](uint16_t j) -> const EditNote& {
      if ((seen[j >> 5] >> (j & 31)) & 1u)
        for (uint16_t k = 0; k < n; ++k)
          if (changes[k].index == j) return changes[k].value;
      return cur_[j];
    }));
    if (r != EditResult::Ok) return r;
    // Record the step: drop redo, forget the oldest steps while the log is full, then swap the values in.
    stepCount_ = cursor_;
    entryCount_ = 0;
    for (uint8_t s = 0; s < stepCount_; ++s) entryCount_ = static_cast<uint16_t>(entryCount_ + steps_[(stepHead_ + s) % MaxSteps].count);
    while (stepCount_ == MaxSteps || entryCount_ + n > MaxLogEntries) forgetOldest();
    const uint16_t first = static_cast<uint16_t>((entryHead_ + entryCount_) % MaxLogEntries);
    for (uint16_t k = 0; k < n; ++k) {
      EditChange& slot = log_[(first + k) % MaxLogEntries];
      slot.index = changes[k].index;
      slot.value = cur_[changes[k].index];
      cur_[changes[k].index] = changes[k].value;
    }
    steps_[(stepHead_ + stepCount_) % MaxSteps] = Step{first, n};
    ++stepCount_;
    entryCount_ = static_cast<uint16_t>(entryCount_ + n);
    cursor_ = stepCount_;
    return EditResult::Ok;
  }

  /// A new note as one E: step (see the header); `index` is its selection index. DeniedSelection: the selection is
  /// full or the note invalid; nothing changes when the step is refused.
  EditResult create(const EditNote& note, uint16_t& index) {
    if (!open_ || leaving_) return EditResult::NotEditing;
    if (count_ == MaxSelection || !valid(note) || note.tick >= loop_) return EditResult::DeniedSelection;
    EditNote absent = note;
    absent.deleted = 1;
    orig_[count_] = cur_[count_] = absent;
    EditNote live = note;
    live.deleted = 0;
    const EditChange c{count_, live};
    ++count_;
    const EditResult r = apply(&c, 1);
    if (r != EditResult::Ok) {
      --count_;
      return r;
    }
    index = c.index;
    return r;
  }

  /// O(changes of the step).
  EditResult undo() {
    if (!open_ || leaving_) return EditResult::NotEditing;
    if (cursor_ == 0) return EditResult::NothingToUndo;
    const EditResult r = swapStep(static_cast<uint8_t>(cursor_ - 1));
    if (r == EditResult::Ok) --cursor_;
    return r;
  }
  EditResult redo() {
    if (!open_ || leaving_) return EditResult::NotEditing;
    if (cursor_ == stepCount_) return EditResult::NothingToRedo;
    const EditResult r = swapStep(cursor_);
    if (r == EditResult::Ok) ++cursor_;
    return r;
  }

  static constexpr uint16_t kNoCause = 0xFFFF;
  /// The selected note, the causing note of the overlap results (`kNoCause`: none). A different one rebuilds (its
  /// results replace the previous one's: `settle` first to keep those).
  void cause(uint16_t index) {
    if (index >= count_) index = kNoCause;
    if (index == causer_) return;
    causer_ = index;
    if (open_ && !leaving_ && edited_ != 0)
      admit(0, 0, overlapRows([&](uint16_t j) -> const EditNote& { return cur_[j]; }));
  }
  uint16_t causer() const { return causer_; }
  /// The causing note's results become the other notes' own state (sticky, DEC-030): recorded in the last E: step, so
  /// undoing it brings them back; redo is dropped. Nothing audible changes. O(selection).
  void settle() {
    if (!open_ || leaving_ || cursor_ == 0 || causer_ == kNoCause) return;
    auto at = [&](uint16_t j) -> const EditNote& { return cur_[j]; };
    uint16_t k = 0;
    for (uint16_t j = 0; j < count_; ++j)
      if (overlapLength(j, at) != cur_[j].length) ++k;
    if (k == 0) return;
    stepCount_ = cursor_;
    entryCount_ = 0;
    for (uint8_t s = 0; s < stepCount_; ++s) entryCount_ = static_cast<uint16_t>(entryCount_ + steps_[(stepHead_ + s) % MaxSteps].count);
    Step& last = steps_[(stepHead_ + stepCount_ - 1) % MaxSteps];
    if (last.count + k > MaxLogEntries) return;  // the step cannot hold them: the results stay live
    while (entryCount_ + k > MaxLogEntries) forgetOldest();
    int32_t dEdited = 0, dLive = 0;
    uint16_t at0 = static_cast<uint16_t>((entryHead_ + entryCount_) % MaxLogEntries);
    for (uint16_t j = 0; j < count_; ++j) {
      const uint32_t len = overlapLength(j, at);
      if (len == cur_[j].length) continue;
      EditNote v = cur_[j];
      if (len == 0)
        v.deleted = 1;
      else
        v.length = len;
      delta(j, v, dEdited, dLive);
      log_[at0] = EditChange{j, cur_[j]};
      at0 = static_cast<uint16_t>((at0 + 1) % MaxLogEntries);
      cur_[j] = v;
    }
    last.count = static_cast<uint16_t>(last.count + k);
    entryCount_ = static_cast<uint16_t>(entryCount_ + k);
    cursor_ = stepCount_;
    edited_ = static_cast<uint16_t>(static_cast<int32_t>(edited_) + dEdited);
    live_ = static_cast<uint16_t>(static_cast<int32_t>(live_) + dLive);
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

  /// One grain of the rebuild job: at most `units` units — a selected note gathered (its hide row written), a heap
  /// sift, or a replacement row written. Returns true while work remains.
  bool step(uint32_t units) {
    if (!building()) return false;
    while (units > 0) {
      switch (phase_) {
        case Phase::Gather:
          if (gather_ == count_) {
            heapEnd_ = events_;
            heapNext_ = static_cast<uint16_t>(events_ / 2);
            phase_ = Phase::Heapify;
            break;
          }
          gatherOne(gather_++);
          --units;
          break;
        case Phase::Heapify:
          if (heapNext_ == 0) {
            phase_ = Phase::Extract;
            break;
          }
          sift(--heapNext_, heapEnd_);
          --units;
          break;
        case Phase::Extract:
          if (heapEnd_ <= 1) {
            phase_ = Phase::Write;
            write_ = 0;
            break;
          }
          --heapEnd_;
          swapKeys(0, heapEnd_);
          sift(0, heapEnd_);
          --units;
          break;
        case Phase::Write:
          if (write_ == events_) {
            publish();
            if (leaving_) finishLeave();
            return false;
          }
          res_->writeOwed(buildRun_, eventRow(keys_[write_++]));
          --units;
          break;
        case Phase::Idle:
          return false;
      }
    }
    return true;
  }

  bool editing() const { return open_; }
  uint16_t count() const { return count_; }  // the selection, created notes included
  bool building() const { return buildRun_ != kNoRun; }
  uint8_t depth() const { return cursor_; }  // the E: level
  uint8_t redoDepth() const { return static_cast<uint8_t>(stepCount_ - cursor_); }
  uint16_t selected() const { return count_; }
  const EditNote& note(uint16_t index) const { return cur_[index]; }
  const Stats& stats() const { return stats_; }

 private:
  enum class Phase : uint8_t { Idle, Gather, Heapify, Extract, Write };
  struct Step {
    uint16_t first;  // log position, already mod MaxLogEntries
    uint16_t count;
  };

  static bool valid(const EditNote& e) {
    return e.length > 0 && e.length < (1u << 20) && e.pitch < 128 && e.velocity > 0 && e.velocity < 128 && e.channel < 16;
  }
  bool isEdited(uint16_t i, const EditNote& v) const { return v != orig_[i]; }
  /// The change of the edited / live counts if note `i` took `v`.
  void delta(uint16_t i, const EditNote& v, int32_t& dEdited, int32_t& dLive) const {
    const bool wasE = isEdited(i, cur_[i]), nowE = isEdited(i, v);
    dEdited += static_cast<int32_t>(nowE) - static_cast<int32_t>(wasE);
    dLive += static_cast<int32_t>(nowE && !v.deleted) - static_cast<int32_t>(wasE && !cur_[i].deleted);
  }

  /// Undo or redo of step `s`: admit the resulting state, then swap each recorded value with the current one (a step
  /// changes each note once, so the order does not matter).
  EditResult swapStep(uint8_t s) {
    const Step& st = steps_[(stepHead_ + s) % MaxSteps];
    int32_t dEdited = 0, dLive = 0;
    uint32_t seen[(MaxSelection + 31) / 32] = {};
    for (uint16_t k = 0; k < st.count; ++k) {
      const EditChange& c = log_[(st.first + k) % MaxLogEntries];
      delta(c.index, c.value, dEdited, dLive);
      seen[c.index >> 5] |= 1u << (c.index & 31);
    }
    const EditResult r = admit(dEdited, dLive, overlapRows([&](uint16_t j) -> const EditNote& {
      if ((seen[j >> 5] >> (j & 31)) & 1u)
        for (uint16_t k = 0; k < st.count; ++k) {
          const EditChange& c = log_[(st.first + k) % MaxLogEntries];
          if (c.index == j) return c.value;
        }
      return cur_[j];
    }));
    if (r != EditResult::Ok) return r;
    for (uint16_t k = 0; k < st.count; ++k) {
      EditChange& c = log_[(st.first + k) % MaxLogEntries];
      const EditNote t = cur_[c.index];
      cur_[c.index] = c.value;
      c.value = t;
    }
    return EditResult::Ok;
  }
  void forgetOldest() {
    const Step& st = steps_[stepHead_];
    entryHead_ = static_cast<uint16_t>((st.first + st.count) % MaxLogEntries);
    entryCount_ = static_cast<uint16_t>(entryCount_ - st.count);
    stepHead_ = static_cast<uint8_t>((stepHead_ + 1) % MaxSteps);
    --stepCount_;  // only called while recording a step, which sets the cursor after
    ++stats_.forgotSteps;
  }

  /// Admission of the state the action leads to (O(1)): with nothing edited the slot returns to the session start at
  /// once; otherwise a fresh edit run is reserved and its rebuild starts (a running one is superseded). Nothing
  /// changes when refused.
  EditResult admit(int32_t dEdited, int32_t dLive, uint32_t overlap) {
    const uint16_t edited = static_cast<uint16_t>(static_cast<int32_t>(edited_) + dEdited);
    const uint16_t live = static_cast<uint16_t>(static_cast<int32_t>(live_) + dLive);
    if (edited == 0) {
      cancelBuild();
      history_->setSessionState(SessionKind::Edit, kNoManifest);
      edited_ = live_ = 0;
      return EditResult::Ok;
    }
    const RunId run = runs_->allocate(slot_, RunKind::kEdit);
    if (run == kNoRun) return EditResult::DeniedRun;
    const Admission a = res_->reserve(run, edited + 2u * live + overlap);
    if (a != Admission::Ok) {
      runs_->seal(run);
      runs_->release(run);  // empty: reclaimed at once by the queue
      return a == Admission::DeniedRunFull ? EditResult::DeniedSelection : EditResult::DeniedCapacity;
    }
    cancelBuild();
    edited_ = edited;
    live_ = live;
    buildRun_ = run;
    phase_ = Phase::Gather;
    gather_ = 0;
    events_ = 0;
    return EditResult::Ok;
  }

  /// Gather one selected note: an edited note gets its hide row now and, unless deleted, two sort keys.
  void gatherOne(uint16_t i) {
    const EditNote& n = cur_[i];
    const bool edited = isEdited(i, n);
    const uint32_t len = overlapLength(i, [&](uint16_t j) -> const EditNote& { return cur_[j]; });
    if (!edited && len == n.length) return;  // as stored
    res_->writeOwed(buildRun_, hideRow(n.noteId));
    if (n.deleted || len == 0) return;
    keys_[events_++] = key(n.tick, true, i);
    keys_[events_++] = key(n.tick + len, false, i);
  }
  /// The length note `j` plays against the causing note as `at` gives them (see the header): 0 hidden, its own when
  /// it is not a target or nothing overlaps it. O(1).
  template <typename At>
  uint32_t overlapLength(uint16_t j, At at) const {
    const EditNote& b = at(j);
    if (causer_ == kNoCause || j == causer_ || b.deleted) return b.length;
    const EditNote& a = at(causer_);
    if (a.deleted || a.pitch != b.pitch || !isEdited(causer_, a)) return b.length;
    uint32_t len = b.length;
    const int64_t loop = loop_, a0 = a.tick, a1 = a0 + a.length;
    for (int64_t shift = -loop; shift <= loop; shift += loop) {  // linear spans across the wrap
      const int64_t b0 = static_cast<int64_t>(b.tick) + shift, b1 = b0 + b.length;
      if (a0 <= b0 && b0 < a1) return 0;  // starts inside A: hidden
      if (b0 < a0 && a0 < b1 && static_cast<uint32_t>(a0 - b0) < len) len = static_cast<uint32_t>(a0 - b0);
    }
    return len;
  }
  /// The rows the overlap results owe beyond the edited notes' own in the state `at` gives: an unedited note hidden
  /// 1, shortened 3 (an edited target only writes fewer of its own). O(selection).
  template <typename At>
  uint32_t overlapRows(At at) const {
    uint32_t rows = 0;
    for (uint16_t j = 0; j < count_; ++j) {
      const EditNote& b = at(j);
      if (isEdited(j, b)) continue;
      const uint32_t len = overlapLength(j, at);
      rows += len == 0 ? 1u : len < b.length ? 3u : 0u;
    }
    return rows;
  }
  /// Sort key: tick (21 bits), then NoteOff before NoteOn, then selection index (10 bits): deterministic.
  static uint32_t key(uint32_t tick, bool on, uint16_t index) {
    return (tick << 11) | (static_cast<uint32_t>(on ? 1 : 0) << 10) | index;
  }
  void swapKeys(uint16_t a, uint16_t b) {
    const uint32_t t = keys_[a];
    keys_[a] = keys_[b];
    keys_[b] = t;
  }
  /// Max-heap sift-down within [0, end): O(log n).
  void sift(uint16_t root, uint16_t end) {
    while (2u * root + 1u < end) {
      uint16_t child = static_cast<uint16_t>(2u * root + 1u);
      if (child + 1u < end && keys_[child] < keys_[child + 1]) ++child;
      if (!(keys_[root] < keys_[child])) return;
      swapKeys(root, child);
      root = child;
    }
  }

  static PassEvent hideRow(uint32_t noteId) {
    PassEvent e{};
    e.noteId = noteId;
    e.status = kHideNoteStatus;
    return e;
  }
  PassEvent eventRow(uint32_t k) const {
    const uint16_t i = static_cast<uint16_t>(k & 0x3FFu);
    const bool on = (k >> 10) & 1u;
    const EditNote& n = cur_[i];
    PassEvent e{};
    e.tick = k >> 11;
    e.noteId = n.noteId;
    e.status = static_cast<uint8_t>((on ? 0x90 : 0x80) | n.channel);
    e.data1 = n.pitch;
    e.data2 = on ? n.velocity : 0;
    return e;
  }

  void publish() {
    res_->forgive(buildRun_);  // rows reserved for an edited note the causing note hid or shortened
    runs_->seal(buildRun_);
    const auto& base = manifests_->manifest(base_);
    RunId list[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < base.runCount; ++i) list[i] = base.runs[i];
    list[base.runCount] = buildRun_;
    const ManifestId m = manifests_->create(slot_, base.revision + 1, base.loopLengthTicks, list,
                                            static_cast<uint8_t>(base.runCount + 1), *runs_);
    runs_->release(buildRun_);  // the manifest holds the run now
    buildRun_ = kNoRun;
    phase_ = Phase::Idle;
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
    phase_ = Phase::Idle;
    ++stats_.superseded;
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
  EditChange* log_ = nullptr;
  bool open_ = false, leaving_ = false;
  uint8_t slot_ = 0;
  ManifestId base_ = kNoManifest;
  uint32_t loop_ = 0;
  uint16_t count_ = 0;
  uint16_t edited_ = 0, live_ = 0;  // notes differing from the admitted selection, and those not deleted
  uint16_t causer_ = kNoCause;      // the selected note: the overlap results' causing note
  EditNote orig_[MaxSelection];
  EditNote cur_[MaxSelection];
  // E: log (entries in the caller's storage)
  Step steps_[MaxSteps];
  uint8_t stepHead_ = 0, stepCount_ = 0, cursor_ = 0;
  uint16_t entryHead_ = 0, entryCount_ = 0;
  // Rebuild
  RunId buildRun_ = kNoRun;
  Phase phase_ = Phase::Idle;
  uint16_t gather_ = 0, events_ = 0, heapNext_ = 0, heapEnd_ = 0, write_ = 0;
  uint32_t keys_[2 * MaxSelection];
  Stats stats_{};
};

}  // namespace ContentEngine
