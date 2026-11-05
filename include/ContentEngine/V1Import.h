//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Reading a V1 slot file into the engine (fixed-memory-engine E9.7b; owner decisions 2026-11-05: exact current content
// only; pass-stream files skipped and reported). Read-only: V1 files are never written.
//
// File (`/MidiLooper/current/slots/loop_TT_SS.bin`): an optional 10-byte epoch header (epoch u32, schema u16, CRC u32,
// written field by field — measured on the owner's card; the persistence guide's "16 B" is wrong), the body, a 4-byte
// `SAVE` token. An LSV1 body: "LSV1", version u16 (1), flags u16, then sections — name length u8, name, payload length
// u32, payload. The `LoopSnapshot` section holds the slot's resolved current content: a root header (loopId, loop
// length, loop start, next note id, next event id: u32 each), the notes (u32 count; per note noteId u32, pitch u8,
// velocity u8, u16 pad, start u32, end u32 — sorted by noteId, not by start; no channel: the old firmware plays a slot
// on its track's channel, which `ClockOutput::setChannel` keeps), then the events (u32 count; per event eventId u32,
// order u32 and the firmware's raw `MidiEvent`, whose layout is a parameter: 20 bytes with the payload at 12 on the
// Teensy; note events among them are skipped — the notes list holds the notes). Everything after the events is skipped.
// * Windows (the notes are not in start order, and a slot can hold more notes than fit in memory): a counting pass
//   builds a histogram of note starts and kept event ticks over 64 spans of the loop; consecutive spans are grouped so a
//   group fits the collect area; one pass over the file per group collects its notes (then, per group, its events),
//   heap-sorts them by start (tick, then file order) and feeds `ResolvedImport` (notes and events in their own runs).
//   Each pass re-reads the file from the start (no seek): exact lengths only, the trailing token is never read.
// * Named outcomes: `NoFile`; `PassStream` (no LSV1 body: the older format, skipped and reported); `StartOffset` (a loop
//   start other than 0, from loop-start editing: skipped and reported rather than imported at an unconfirmed offset);
//   `EmptySnapshot` (no notes or kept events: skipped — its passes may hold content); `Dense` (one span holds more
//   notes or events than the collect area); `BadFormat`; `ImportRefused` (the importer refused — everything released);
//   `IoError`.
// No allocation; each grain reads at most `kReadBytes`; the collect area and the span histograms are the rest of the
// scratch.

#pragma once

#include <cstdint>

#include "ContentEngine/SetFile.h"

namespace ContentEngine {

struct MidiEventLayout {
  uint8_t bytes;      // sizeof(MidiEvent) where the file was written
  uint8_t payloadAt;  // offset of the payload union (note / cc / program / pitch bend ...)
};
constexpr MidiEventLayout kTeensyMidiEvent{20, 12};  // tick u32, type u8, channel u8, pad, noteId u32, union (8)

template <typename Card, typename Import>
class Lsv1Reader {
 public:
  enum class Status : uint8_t {
    Idle,
    Running,
    Done,
    NoFile,
    PassStream,
    StartOffset,
    BadFormat,
    ImportRefused,
    IoError,
    EmptySnapshot,
    Dense,
  };
  static constexpr uint32_t kReadBytes = 4096;
  static constexpr uint8_t kSpans = 64;

  /// Starts reading `path` into `slot` (which must be empty). `scratch` holds `chunk` bytes (> kReadBytes).
  template <typename Pool, typename Runs, typename Manifests, typename HistoryT, typename Res>
  Status start(Card& card, const char* path, Import& import, Pool& pool, Runs& runs, Manifests& manifests,
               HistoryT& history, Res& res, NoteIdSource& ids, uint8_t slot, uint8_t* scratch, uint32_t chunk,
               MidiEventLayout layout = kTeensyMidiEvent) {
    card_ = &card;
    path_ = path;
    import_ = &import;
    begin_ = [](void* self, uint32_t loop, uint32_t notes, uint32_t events) -> bool {
      Ctx& c = static_cast<Lsv1Reader*>(self)->ctx_;
      return static_cast<Lsv1Reader*>(self)->import_->start(*static_cast<Pool*>(c.pool), *static_cast<Runs*>(c.runs),
                                                            *static_cast<Manifests*>(c.manifests),
                                                            *static_cast<HistoryT*>(c.history), *static_cast<Res*>(c.res),
                                                            *c.ids, c.slot, loop, notes, events) == Import::Status::Running;
    };
    ctx_ = Ctx{&pool, &runs, &manifests, &history, &res, &ids, slot};
    scratch_ = scratch;
    // The scratch: the read area, the collect area, and the two span histograms at its end (they live across passes).
    noteHist_ = reinterpret_cast<uint16_t*>(scratch + chunk - 4u * kSpans);
    eventHist_ = noteHist_ + kSpans;
    collect_ = scratch + kReadBytes;
    collectBytes_ = chunk - kReadBytes - 4u * kSpans;
    layout_ = layout;
    mode_ = Mode::Count;
    for (uint8_t i = 0; i < kSpans; ++i) noteHist_[i] = eventHist_[i] = 0;
    notesDone_ = eventsDone_ = kept_ = 0;
    if (!card.exists(path)) return status_ = Status::NoFile;
    if (!card.openRead(path)) return status_ = Status::IoError;
    phase_ = Phase::Probe;
    return status_ = Status::Running;
  }

  /// One grain.
  Status step() {
    if (status_ != Status::Running) return status_;
    switch (phase_) {
      case Phase::Probe:
        return probe();
      case Phase::Section:
        return section();
      case Phase::Skip:
        return skip();
      case Phase::Root:
        return root();
      case Phase::Notes:
        return notes();
      case Phase::EventCount:
        return eventCount();
      case Phase::Events:
        return events();
    }
    return status_;
  }
  Status status() const { return status_; }
  uint32_t notesRead() const { return notesDone_; }
  uint32_t eventsRead() const { return eventsDone_; }
  /// Why the import was refused (the importer's status), for the report.
  uint8_t importStatus() const { return static_cast<uint8_t>(import_->status()); }

 private:
  enum class Phase : uint8_t { Probe, Section, Skip, Root, Notes, EventCount, Events };
  enum class Mode : uint8_t { Count, NoteWindow, EventWindow };
  struct Ctx {
    void *pool, *runs, *manifests, *history, *res;
    NoteIdSource* ids;
    uint8_t slot;
  };
  struct NoteRec {  // 16 B
    uint32_t start, end, order;
    uint8_t pitch, velocity, pad[2];
  };
  struct EventRec {  // 12 B
    uint32_t tick, order;
    uint8_t status, d1, d2, pad;
  };

  static uint16_t u16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }
  static uint32_t u32(const uint8_t* b) { return u16(b) | (static_cast<uint32_t>(u16(b + 2)) << 16); }
  bool read(uint32_t n) { return card_->read(scratch_, n); }
  Status end(Status s) {
    card_->closeRead();
    if (s != Status::Done && s != Status::Running) import_->cancel();
    return status_ = s;
  }
  uint8_t spanOf(uint32_t tick) const { return static_cast<uint8_t>(tick / spanTicks_); }
  /// Reopens the file for the next pass.
  bool again() {
    card_->closeRead();
    if (!card_->openRead(path_)) return false;
    phase_ = Phase::Probe;
    notesDone_ = eventsDone_ = 0;
    collected_ = 0;
    return true;
  }
  /// The spans [from, to) of the next window: as many as fit the collect area (`cap` records). False: one span alone
  /// holds more.
  static bool plan(const uint16_t* hist, uint8_t from, uint32_t cap, uint8_t& to) {
    uint32_t sum = 0;
    to = from;
    while (to < kSpans && sum + hist[to] <= cap) sum += hist[to++];
    return to > from;
  }

  Status probe() {
    if (!read(4)) return end(Status::PassStream);
    if (!isLsv1()) {  // maybe behind the 10-byte epoch header
      if (!read(6)) return end(Status::PassStream);
      if (!read(4) || !isLsv1()) return end(Status::PassStream);
    }
    if (!read(4)) return end(Status::BadFormat);
    if (u16(scratch_) != 1) return end(Status::BadFormat);
    phase_ = Phase::Section;
    return status_;
  }
  bool isLsv1() const { return scratch_[0] == 'L' && scratch_[1] == 'S' && scratch_[2] == 'V' && scratch_[3] == '1'; }

  Status section() {
    if (!read(1)) return end(Status::BadFormat);  // the snapshot section ends the reading, so EOF here is malformed
    const uint8_t nameLength = scratch_[0];
    if (nameLength == 0 || nameLength > 64 || !read(nameLength + 4u)) return end(Status::BadFormat);
    static constexpr char kSnapshot[] = "LoopSnapshot";
    bool snapshot = nameLength == sizeof(kSnapshot) - 1;
    for (uint8_t i = 0; snapshot && i < nameLength; ++i) snapshot = scratch_[i] == static_cast<uint8_t>(kSnapshot[i]);
    left_ = u32(scratch_ + nameLength);
    phase_ = snapshot ? Phase::Root : Phase::Skip;
    return status_;
  }
  Status skip() {
    const uint32_t n = left_ < kReadBytes ? left_ : kReadBytes;
    if (n != 0 && !read(n)) return end(Status::BadFormat);
    left_ -= n;
    if (left_ == 0) phase_ = Phase::Section;
    return status_;
  }
  Status root() {
    if (!read(24)) return end(Status::BadFormat);
    loop_ = u32(scratch_ + 4);
    if (loop_ == 0) return end(Status::BadFormat);
    if (u32(scratch_ + 8) % loop_ != 0) return end(Status::StartOffset);
    noteCount_ = u32(scratch_ + 20);
    spanTicks_ = (loop_ + kSpans - 1) / kSpans;
    phase_ = Phase::Notes;
    return status_;
  }

  Status notes() {
    uint32_t n = noteCount_ - notesDone_;
    if (n > kReadBytes / 16) n = kReadBytes / 16;
    if (n != 0 && !read(n * 16)) return end(Status::BadFormat);
    for (uint32_t k = 0; k < n; ++k) {
      const uint8_t* r = scratch_ + k * 16;
      const uint32_t start = u32(r + 8);
      uint32_t endTick = u32(r + 12);
      if (endTick >= loop_) endTick -= loop_;  // stored unwrapped: held across the loop end
      if (start >= loop_ || endTick >= loop_ || r[5] == 0) return end(Status::BadFormat);
      const uint8_t span = spanOf(start);
      if (mode_ == Mode::Count) {
        if (noteHist_[span] != 0xFFFF) ++noteHist_[span];
      } else if (mode_ == Mode::NoteWindow && span >= from_ && span < to_) {
        NoteRec* buf = reinterpret_cast<NoteRec*>(collect_);
        buf[collected_++] = NoteRec{start, endTick, notesDone_ + k, r[4], r[5], {0, 0}};
      }
    }
    notesDone_ += n;
    if (notesDone_ < noteCount_) return status_;
    if (mode_ == Mode::NoteWindow) return feedNotes();
    phase_ = Phase::EventCount;
    return status_;
  }
  Status eventCount() {
    if (!read(4)) return end(Status::BadFormat);
    eventCount_ = u32(scratch_);
    phase_ = Phase::Events;
    return status_;
  }
  /// The event's row (status, data), or false when it is not kept (note events, system messages).
  bool decode(const uint8_t* e, uint32_t& tick, uint8_t& status, uint8_t& d1, uint8_t& d2) const {
    tick = u32(e);
    const uint8_t type = e[4], channel = e[5];
    const uint8_t* p = e + layout_.payloadAt;
    if (type < 0xA0 || type > 0xE0 || (type & 0x0F) != 0 || channel < 1 || channel > 16) return false;
    d1 = p[0];
    d2 = p[1];
    if (type == 0xC0 || type == 0xD0) d2 = 0;
    if (type == 0xE0) {  // stored as the logical value -8192..8191
      const uint16_t v = static_cast<uint16_t>(static_cast<int16_t>(u16(p)) + 8192);
      d1 = v & 0x7F;
      d2 = (v >> 7) & 0x7F;
    }
    status = static_cast<uint8_t>(type | ((channel - 1) & 0x0F));
    return true;
  }
  Status events() {
    const uint32_t record = 8u + layout_.bytes;
    uint32_t n = eventCount_ - eventsDone_;
    if (n > kReadBytes / record) n = kReadBytes / record;
    if (n != 0 && !read(n * record)) return end(Status::BadFormat);
    for (uint32_t k = 0; k < n; ++k) {
      uint32_t tick;
      uint8_t status, d1, d2;
      if (!decode(scratch_ + k * record + 8, tick, status, d1, d2)) continue;
      if (tick >= loop_) return end(Status::BadFormat);
      const uint8_t span = spanOf(tick);
      if (mode_ == Mode::Count) {
        ++kept_;
        if (eventHist_[span] != 0xFFFF) ++eventHist_[span];
      } else if (mode_ == Mode::EventWindow && span >= from_ && span < to_) {
        EventRec* buf = reinterpret_cast<EventRec*>(collect_);
        buf[collected_++] = EventRec{tick, eventsDone_ + k, status, d1, d2, 0};
      }
    }
    eventsDone_ += n;
    if (eventsDone_ < eventCount_) return status_;
    if (mode_ == Mode::Count) return counted();
    return feedEvents();
  }

  /// Counted: reserve every row, then the first note window.
  Status counted() {
    if (noteCount_ == 0 && kept_ == 0) return end(Status::EmptySnapshot);
    if (!begin_(this, loop_, noteCount_, kept_)) return end(Status::ImportRefused);
    from_ = 0;
    return nextNoteWindow();
  }
  Status nextNoteWindow() {
    while (from_ < kSpans && noteHist_[from_] == 0) ++from_;
    if (from_ == kSpans) {
      from_ = 0;
      return nextEventWindow();
    }
    if (!plan(noteHist_, from_, collectBytes_ / sizeof(NoteRec), to_)) return end(Status::Dense);
    mode_ = Mode::NoteWindow;
    return again() ? status_ : end(Status::IoError);
  }
  Status nextEventWindow() {
    while (from_ < kSpans && eventHist_[from_] == 0) ++from_;
    if (from_ == kSpans) {
      card_->closeRead();
      return status_ = import_->finish() == Import::Status::Done ? Status::Done : Status::ImportRefused;
    }
    if (!plan(eventHist_, from_, collectBytes_ / sizeof(EventRec), to_)) return end(Status::Dense);
    mode_ = Mode::EventWindow;
    return again() ? status_ : end(Status::IoError);
  }

  // A heap sort of the collected records by (key, file order): O(n log n), in place.
  template <typename Rec, typename Key>
  static void sortBy(Rec* a, uint32_t n, Key key) {
    auto less = [&](const Rec& x, const Rec& y) { return key(x) < key(y) || (key(x) == key(y) && x.order < y.order); };
    auto sift = [&](uint32_t root, uint32_t size) {
      for (;;) {
        uint32_t big = root;
        const uint32_t l = 2 * root + 1, r = l + 1;
        if (l < size && less(a[big], a[l])) big = l;
        if (r < size && less(a[big], a[r])) big = r;
        if (big == root) return;
        const Rec t = a[root];
        a[root] = a[big];
        a[big] = t;
        root = big;
      }
    };
    for (uint32_t i = n / 2; i-- > 0;) sift(i, n);
    for (uint32_t end = n; end-- > 1;) {
      const Rec t = a[0];
      a[0] = a[end];
      a[end] = t;
      sift(0, end);
    }
  }
  Status feedNotes() {
    NoteRec* buf = reinterpret_cast<NoteRec*>(collect_);
    sortBy(buf, collected_, [](const NoteRec& r) { return r.start; });
    for (uint32_t i = 0; i < collected_; ++i)
      if (import_->addNote(buf[i].start, buf[i].end, buf[i].pitch, buf[i].velocity, 0) != Import::Status::Running)
        return end(Status::ImportRefused);
    from_ = to_;
    return nextNoteWindow();
  }
  Status feedEvents() {
    EventRec* buf = reinterpret_cast<EventRec*>(collect_);
    sortBy(buf, collected_, [](const EventRec& r) { return r.tick; });
    for (uint32_t i = 0; i < collected_; ++i)
      if (import_->addEvent(buf[i].tick, buf[i].status, buf[i].d1, buf[i].d2) != Import::Status::Running)
        return end(Status::ImportRefused);
    from_ = to_;
    return nextEventWindow();
  }

  Card* card_ = nullptr;
  const char* path_ = nullptr;
  Import* import_ = nullptr;
  bool (*begin_)(void*, uint32_t, uint32_t, uint32_t) = nullptr;
  Ctx ctx_{};
  uint8_t* scratch_ = nullptr;
  uint8_t* collect_ = nullptr;
  uint32_t collectBytes_ = 0;
  MidiEventLayout layout_ = kTeensyMidiEvent;
  Phase phase_ = Phase::Probe;
  Mode mode_ = Mode::Count;
  Status status_ = Status::Idle;
  uint32_t left_ = 0, loop_ = 0, spanTicks_ = 1, noteCount_ = 0, eventCount_ = 0, notesDone_ = 0, eventsDone_ = 0;
  uint32_t kept_ = 0, collected_ = 0;
  uint8_t from_ = 0, to_ = 0;
  uint16_t* noteHist_ = nullptr;
  uint16_t* eventHist_ = nullptr;
};

}  // namespace ContentEngine
