//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// NOTE_EDIT for the surface (fixed-memory-engine E9.5): what `EditManager` / `NoteEditFocus` call, on top of the edit
// writer (E4.5).
//
// * Selection (`request`): the notes the musician sees in a range — those starting in [from, to) within a pitch range,
//   as the piano roll shows them (every effective note: `Composer` with T = 48, the display cursor) — with
//   their **stored** geometry: the note's own NoteOn and NoteOff rows in the run that holds its current instance (an
//   edit run's replacement after earlier edits). Effective lengths are not stored: overlap results are derived at
//   composition (EDIT-1), so the edited note drives and the others adapt, as in the legacy NOTE_EDIT. A note hidden
//   (deleted or wholly occluded) is not selectable. Gathered in grains (`gatherStep`): edit index, composition up to the
//   range end, then one stored-row lookup at a time. Cycle 0 is exact for notes that start in the range: `begin` counts
//   notes held across the loop end as sounding at position 0 and the hide look-ahead reads across the loop end, so no
//   second cycle is composed. More than `MaxSelection` notes: `TooMany`, nothing selected.
// * Session: `enter` opens the edit writer on the gathered notes; `apply` / `undo` / `redo` are E: steps, each heard at
//   once (the writer's rebuild runs as a transition job; `takePublished` tells the root to make the slot's playing
//   tracks follow). Each step keeps the caller's focus token (the selection / focus to show), returned by undo / redo.
// * `leave` keeps the edits as one U: step; `abandon` returns to the session start.
// The gather uses the display cursor and scratch window (shared with the tile sweep, E9.6: one at a time). No
// allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/EditWriter.h"
#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/SpanWindow.h"

namespace ContentEngine {

template <typename Pool, typename Runs, typename Manifests, typename HistoryT, typename Composer, typename Window,
          typename Writer, uint16_t MaxSelection = Config::kEditSelection, uint8_t Steps = Config::kEditSteps>
class NoteEditSession {
 public:
  enum class Gather : uint8_t { Idle, Running, Done, Refused, TooMany };
  struct Query {
    uint8_t slot;
    uint32_t from, to;  // loop positions
    uint8_t pitchLo, pitchHi;
  };

  /// `notes` / `runs`: caller-placed storage for `MaxSelection` gathered notes and their runs.
  void init(Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Composer& composer, Window& scratch,
            Writer& writer, EditNote* notes, uint16_t* noteRuns, uint32_t windowTicks) {
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    history_ = &history;
    comp_ = &composer;
    win_ = &scratch;
    writer_ = &writer;
    notes_ = notes;
    noteRuns_ = noteRuns;
    window_ = windowTicks;
    lease_ = Lease{};
    status_ = Gather::Idle;
    phase_ = Phase::Idle;
    count_ = 0;
    slot_ = 0;
    building_ = false;
    published_ = false;
    for (auto& t : focus_) t = 0;
    forgotBase_ = 0;
  }

  // ---- Selection ------------------------------------------------------------------------------------------------------
  /// One selection owner (owner 2026-11-05): refused while a NOTE_EDIT session is open (leave or abandon it first).
  Gather request(const Query& q, uint32_t focusAtEntry) {
    if (writer_->editing()) return Gather::Refused;
    cancelGather();
    const ManifestId m = history_->current(q.slot);
    if (m == kNoManifest || manifests_->manifest(m).runCount == 0 || q.from >= q.to) return status_ = Gather::Refused;
    q_ = q;
    entryFocus_ = focusAtEntry;
    lease_.open(*manifests_, *runs_, *comp_, m);
    loop_ = lease_.loopLength();
    count_ = 0;
    phase_ = Phase::Index;
    bindStarted_ = false;
    return status_ = Gather::Running;
  }
  bool gatherPending() const { return status_ == Gather::Running; }
  /// One grain of the gather: at most `units` (index units, rows composed, or stored rows read).
  bool gatherStep(uint32_t units) {
    if (status_ != Gather::Running) return false;
    switch (phase_) {
      case Phase::Index: {
        const IndexResult r = lease_.prepare(units);
        if (r == IndexResult::CapacityExceeded) return finish(Gather::Refused);
        if (!lease_.prepared()) return true;
        if (!bindStarted_) comp_->bindStart(lease_.source(), lease_.count(), loop_, lease_.identity(), true);
        bindStarted_ = true;
        const auto b = comp_->bindMore(Composer::kRunsPerStep);  // in steps
        if (b == Composer::BindStep::Running) return true;
        bindStarted_ = false;
        if (b == Composer::BindStep::Failed) return finish(Gather::Refused);
        lease_.dropIndexLease();
        phase_ = Phase::Compose;
        return true;
      }
      case Phase::Compose:
        return composeGrain(units);
      case Phase::Lookup:
        return lookupGrain(units);
      default:
        return false;
    }
  }
  Gather status() const { return status_; }
  uint16_t count() const { return count_; }
  const EditNote& note(uint16_t i) const { return notes_[i]; }

  // ---- Session --------------------------------------------------------------------------------------------------------
  EditResult enter() {
    if (status_ != Gather::Done) return EditResult::DeniedSelection;
    const EditResult r = writer_->begin(q_.slot, notes_, count_);
    if (r != EditResult::Ok) return r;
    slot_ = q_.slot;
    forgotBase_ = writer_->stats().forgotSteps;
    focus_[index(0)] = entryFocus_;
    return r;
  }
  EditResult apply(const EditChange* changes, uint16_t n, uint32_t focus) {
    const EditResult r = writer_->apply(changes, n);
    if (r == EditResult::Ok) {
      focus_[index(writer_->depth())] = focus;
      stepTaken();
    }
    return r;
  }
  /// A new note (its noteId fresh: the caller's) as one E: step; `at` is its selection index (E9.8b-2).
  EditResult create(const EditNote& note, uint32_t focus, uint16_t& at) {
    const EditResult r = writer_->create(note, at);
    if (r == EditResult::Ok) {
      focus_[index(writer_->depth())] = focus;
      stepTaken();
    }
    return r;
  }
  /// Copy / paste: `n` changes and `m` new notes (`changes[n .. n + m)`) as one E: step (`EditWriter::applyAdding`).
  EditResult applyAdding(EditChange* changes, uint16_t n, uint16_t m, uint32_t focus, uint16_t& first) {
    const EditResult r = writer_->applyAdding(changes, n, m, first);
    if (r == EditResult::Ok) {
      focus_[index(writer_->depth())] = focus;
      stepTaken();
    }
    return r;
  }
  /// The E: level (an edit surface collapses a fader gesture into its one step while the level is unchanged).
  uint8_t depth() const { return writer_->depth(); }
  /// The session's notes as they are now (edits applied, created notes included): `size` of them.
  uint16_t size() const { return writer_->editing() ? writer_->count() : count_; }
  const EditNote& current(uint16_t i) const { return writer_->editing() ? writer_->note(i) : notes_[i]; }
  EditResult undo(uint32_t& focus) {
    const EditResult r = writer_->undo();
    if (r == EditResult::Ok) {
      focus = focus_[index(writer_->depth())];
      stepTaken();
    }
    return r;
  }
  EditResult redo(uint32_t& focus) {
    const EditResult r = writer_->redo();
    if (r == EditResult::Ok) {
      focus = focus_[index(writer_->depth())];
      stepTaken();
    }
    return r;
  }
  /// The selected note (selection index; < 0: none): the overlap results' causing note. Its results follow it live;
  /// `settle` first keeps the previous selected note's (owner 2026-11-07, DEC-030 sticky).
  void cause(int32_t index) {
    writer_->cause(index < 0 ? Writer::kNoCause : static_cast<uint16_t>(index));
    if (writer_->building()) building_ = true;
  }
  void settle() { writer_->settle(); }
  void leave() {  // the selected note's results stay as published: no settle needed
    writer_->leave();
    building_ = writer_->building();
    if (!building_) published_ = true;
  }
  void abandon() {
    writer_->abandon();
    building_ = false;
    published_ = true;  // the slot is back at its start
  }
  bool editing() const { return writer_->editing(); }
  uint8_t slot() const { return slot_; }

  /// The rebuild job (transition class: the edit is heard at once).
  bool rebuildPending() const { return writer_->building(); }
  bool rebuildStep(uint32_t units) {
    const bool more = writer_->step(units);
    if (!more && building_) {
      building_ = false;
      published_ = true;
    }
    return more;
  }
  /// The slot's state changed (a rebuild published, a session left or abandoned): its playing tracks follow.
  bool takePublished(uint8_t& slot) {
    if (!published_ || writer_->building()) return false;
    published_ = false;
    slot = slot_;
    return true;
  }

 private:
  /// An E: step was accepted: its rebuild publishes later, or (nothing edited any more) the slot is back at the
  /// session start now.
  void stepTaken() {
    if (writer_->building())
      building_ = true;
    else
      published_ = true;
  }

  enum class Phase : uint8_t { Idle, Index, Compose, Lookup };
  using Lease = SourceLease<Composer, Manifests, Runs>;

  uint8_t index(uint8_t depth) const {
    return static_cast<uint8_t>((writer_->stats().forgotSteps - forgotBase_ + depth) % (Steps + 1u));
  }

  bool finish(Gather s) {
    if (phase_ == Phase::Compose || phase_ == Phase::Lookup) comp_->end();
    lease_.close();
    phase_ = Phase::Idle;
    status_ = s;
    if (s != Gather::Done) count_ = 0;
    return false;
  }
  void cancelGather() {
    if (status_ == Gather::Running) finish(Gather::Idle);
  }

  /// Windows from the loop start, collected up to the range end.
  bool composeGrain(uint32_t units) {
    Window& w = *win_;
    if (!comp_->windowOpen() && !comp_->openWindow(w, window_)) return startLookup();  // the loop ended
    if (!comp_->composeRows(w, units)) return true;
    for (uint16_t k = 0; k < w.count; ++k) {
      const SpanEntry& e = w.entries[k];
      if (e.terminal || (e.status & 0xF0) != 0x90 || e.data2 == 0) continue;
      if (e.tick < q_.from || e.tick >= q_.to || e.data1 < q_.pitchLo || e.data1 > q_.pitchHi) continue;
      if (count_ == MaxSelection) return finish(Gather::TooMany);
      notes_[count_] = EditNote{e.noteId, e.tick, 0, e.data1, e.data2, static_cast<uint8_t>(e.status & 0x0F), 0};
      noteRuns_[count_++] = runIdOf(e.runKey);
    }
    if (w.end >= q_.to || w.end >= loop_) return startLookup();
    return true;
  }
  bool startLookup() {
    comp_->end();
    phase_ = Phase::Lookup;
    look_ = 0;
    row_ = kNoRow;
    return true;
  }
  /// The stored NoteOff of gathered note `look_`: from its NoteOn row (binary search by tick) forward in its run.
  bool lookupGrain(uint32_t units) {
    while (units > 0) {
      if (look_ == count_) return finish(Gather::Done);
      EditNote& n = notes_[look_];
      const RunId r = noteRuns_[look_];
      const uint32_t rows = runs_->run(r).rowCount;
      if (row_ == kNoRow) {  // first row at the NoteOn's tick
        uint32_t lo = 0, hi = rows;
        while (lo < hi) {
          const uint32_t mid = (lo + hi) / 2;
          if (runs_->rowAt(r, mid, *pool_).tick < n.tick)
            lo = mid + 1;
          else
            hi = mid;
        }
        row_ = lo;
        onFound_ = false;
        --units;
        continue;
      }
      bool off = false;
      for (; units > 0 && row_ < rows; --units, ++row_) {
        const PassEvent& p = runs_->rowAt(r, row_, *pool_);
        if (p.noteId != n.noteId) continue;
        const uint8_t type = p.status & 0xF0;
        if (!onFound_ && type == 0x90 && p.data2 != 0) {
          onFound_ = true;
        } else if (onFound_ && (type == 0x80 || (type == 0x90 && p.data2 == 0))) {
          n.length = p.tick - n.tick;
          off = true;
          break;
        }
      }
      if (off && n.length != 0) {
        ++look_;
        row_ = kNoRow;
      } else if (off || row_ >= rows) {
        // No stored NoteOff (cannot happen for a sealed take), or one on the NoteOn's own tick (a press under a tick
        // long; owner 2026-11-06: the lookup found length 0 and never moved on, which hung entering NOTE_EDIT): not
        // selectable (an edited note is at least a tick long).
        notes_[look_] = notes_[count_ - 1];
        noteRuns_[look_] = noteRuns_[count_ - 1];
        --count_;
        row_ = kNoRow;
      }
    }
    return true;
  }

  static constexpr uint32_t kNoRow = 0xFFFFFFFFu;

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  Composer* comp_ = nullptr;
  bool bindStarted_ = false;  // a stepped bind under way (this gather's)
  Window* win_ = nullptr;
  Writer* writer_ = nullptr;
  EditNote* notes_ = nullptr;
  uint16_t* noteRuns_ = nullptr;
  uint32_t window_ = 0, loop_ = 0;
  Lease lease_;
  Query q_{};
  Gather status_ = Gather::Idle;
  Phase phase_ = Phase::Idle;
  uint16_t count_ = 0, look_ = 0;
  uint32_t row_ = kNoRow;
  bool onFound_ = false;
  uint8_t slot_ = 0;
  bool building_ = false, published_ = false;
  uint32_t entryFocus_ = 0;
  uint32_t focus_[Steps + 1];
  uint32_t forgotBase_ = 0;
};

}  // namespace ContentEngine
