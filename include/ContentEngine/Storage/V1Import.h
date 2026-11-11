//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Reading a V1 slot file into the engine: exact current content only; pass-stream files are imported with their note
// edits discarded.
// Read-only: V1 files are never written.
//
// File (`/MidiLooper/current/slots/loop_TT_SS.bin`): an optional 10-byte epoch header (epoch u32, schema u16, CRC u32,
// written field by field — as V1 cards hold it, not 16 B), the body, a 4-byte `SAVE` token. An LSV1 body: "LSV1",
// version u16 (1), flags u16, then sections — name length u8, name, payload length u32, payload. The `LoopSnapshot`
// section holds the slot's resolved current content: a root header (loopId, loop length, loop start, next note id, next
// event id: u32 each), the notes (u32 count; per note noteId u32, pitch u8, velocity u8, u16 pad, start u32, end u32 —
// sorted by noteId, not by start; no channel: the old firmware plays a slot on its track's channel, which
// `ClockOutput::setChannel` keeps), then the events (u32 count; per event eventId u32, order u32 and the firmware's raw
// `MidiEvent`, whose layout is a parameter: 20 bytes with the payload at 12 on the Teensy; note events among them are
// skipped — the notes list holds the notes). Everything after the events is skipped.
// * Windows (the notes are not in start order, and a slot can hold more notes than fit in memory): a counting pass
//   builds a histogram of note starts and kept event ticks over 64 spans of the loop; consecutive spans are grouped so
//   a group fits the collect area; one pass over the file per group collects its notes (then, per group, its events),
//   heap-sorts them by start (tick, then file order) and feeds `ResolvedImport` (notes and events in their own runs).
//   Each pass re-reads the file from the start (no seek): exact lengths only, the trailing token is never read.
// A pass-stream body (no LSV1 magic; the V1 firmware's `writeLegacyLoopPersisted`): loopId, startLoopTick, loop length,
// loop start, nextPassId, nextNoteId, nextMergeSequence, lastCommittedPassId, pass count (u32 each); per pass id u32,
// mergeSequence u32, state u8 (0 = Active), type u8 (0 = record, 1 = overdub), sealedAtTick u32, event count u32 and
// the raw `MidiEvent`s in capture order (ticks up to a bar past the loop end: wrapped); then nextPassId u32, "EPT3",
// the edit-pass count u32 and the edit passes (not read: discarded, reported by `editsDiscarded`). The epoch header is
// detected as the V1 firmware detects it (`fileStartsWithEpochHeader`: a first word other than the container version 6
// and a schema major of 0).
// * As the V1 firmware materialises it (`collectActiveCommittedChunkLists`): only Active passes; the record pass first,
//   then the overdubs by mergeSequence. Each pass with notes becomes its own run (`ResolvedImport::nextPass`), so later
//   passes occlude earlier ones at composition as a live overdub does. Kept events of all Active passes go to the event
//   run.
// * Pairing, cyclic (as V1 cards hold them: a pass's events are stored in tick order, so a note held across the loop
//   end has its NoteOff near the start of the pass and its NoteOn near the end): a NoteOff closes the last open note of
//   its channel and pitch (a NoteOn of velocity 0 is a NoteOff); a NoteOff with none open is kept, and at the pass end
//   those NoteOffs, in order, close the notes still open — the stream wrapped round. A NoteOn with no NoteOff anywhere
//   in its pass is held for the whole loop (its NoteOff one tick before its start) and counted (`heldWhole`) rather
//   than refusing the slot; NoteOffs left over are dropped.
// * A loop length of 0 with no passes (the card's placeholders for empty slots) is `EmptySnapshot`.
// * The windows run per pass: the counting pass builds one start histogram per Active pass, then each window re-reads
//   the file up to the end of its pass (pairing that pass's events again) and collects the notes starting in it.
// * Named outcomes: `NoFile`; `StartOffset` (a loop start other than 0, from loop-start editing: skipped and reported
//   rather than imported at an unconfirmed offset); `EmptySnapshot` (no notes or kept events: skipped — for LSV1 its
//   passes may hold content); `ManyPasses` (more Active passes than the importer's runs); `Dense` (one span holds more
//   notes or events than the collect area, or a pass more than `kOpen` notes open, or NoteOffs waiting, at once);
//   `BadFormat`; `ImportRefused` (the importer refused — everything released); `IoError`. `PassStream` and `Edited` are
//   not returned (kept for the outcome numbering).
// No allocation; each grain reads at most `kReadBytes`; the open-note table, the span histograms and the collect area
// are the rest of the scratch.

#pragma once

#include <cstdint>

#include "ContentEngine/Storage/SlotFile.h"

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
    Edited,
    ManyPasses,
  };
  static constexpr uint32_t kReadBytes = 4096;
  static constexpr uint8_t kSpans = 64;
  static constexpr uint8_t kPasses = 16;  // Active passes of a pass stream (the importer's note runs)
  static constexpr uint32_t kOpen = 128;  // notes open at once within a pass

  /// Starts reading `path` into `slot` (which must be empty). `scratch` holds `chunk` bytes (> kReadBytes).
  template <typename Pool, typename Runs, typename Manifests, typename HistoryT, typename Res>
  Status start(Card& card, const char* path, Import& import, Pool& pool, Runs& runs, Manifests& manifests,
               HistoryT& history, Res& res, NoteIdSource& ids, uint8_t slot, uint8_t* scratch, uint32_t chunk,
               MidiEventLayout layout = kTeensyMidiEvent) {
    card_ = &card;
    path_ = path;
    import_ = &import;
    import.cancel();
    import.bind(pool, runs, manifests, history, res, ids);  // admitted (`begin`) once the rows are counted
    slot_ = slot;
    scratch_ = scratch;
    // The scratch: the read area, the collect area, then (at its end, living across the file passes) the open-note
    // table, the per-pass tables and the span histograms — one per Active pass, and the events' (see the accessors).
    noteHist_ = reinterpret_cast<uint16_t*>(scratch + chunk - kHistBytes);
    collectBytes_ = chunk - kReadBytes - kHistBytes - kPassBytes - 2 * kOpen * sizeof(Open);
    layout_ = layout;
    mode_ = Mode::Count;
    for (uint32_t i = 0; i < (kPasses + 1u) * kSpans; ++i) noteHist_[i] = 0;
    for (uint8_t a = 0; a < kPasses; ++a) passNotes()[a] = 0;
    notesDone_ = eventsDone_ = kept_ = 0;
    passDone_ = actives_ = 0;
    heldWhole_ = 0;
    edited_ = false;
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
      case Phase::PassHeader:
        return passHeader();
      case Phase::PassEvents:
        return passEvents();
      case Phase::Tail:
        return tail();
    }
    return status_;
  }
  Status status() const { return status_; }
  uint32_t notesRead() const { return notesDone_; }
  uint32_t eventsRead() const { return eventsDone_; }
  /// Notes of the last pass stream with no NoteOff in their pass, imported held for the whole loop.
  uint32_t heldWhole() const { return heldWhole_; }
  /// The last pass stream had note edits, which were discarded: its passes are imported as recorded.
  bool editsDiscarded() const { return edited_; }
  /// Why the import was refused (the importer's status), for the report.
  uint8_t importStatus() const { return static_cast<uint8_t>(import_->status()); }

 private:
  enum class Phase : uint8_t { Probe, Section, Skip, Root, Notes, EventCount, Events, PassHeader, PassEvents, Tail };
  enum class Mode : uint8_t { Count, NoteWindow, EventWindow };
  struct NoteRec {  // 16 B
    uint32_t start, end, order;
    uint8_t pitch, velocity, pad[2];
  };
  struct EventRec {  // 12 B
    uint32_t tick, order;
    uint8_t status, d1, d2, pad;
  };
  struct Open {  // 12 B: a NoteOn waiting for its NoteOff
    uint32_t start, order;
    uint8_t channel, pitch, velocity, pad;
  };
  static constexpr uint32_t kHistBytes = 2u * (kPasses + 1u) * kSpans;
  static constexpr uint32_t kPassBytes = 9u * kPasses;  // notes and merge key u32, order u8
  static constexpr uint32_t kPassHeader = 36;  // the 8 header words and the pass count
  static constexpr uint32_t kTailMarker = 0x45505433u;  // "EPT3"
  static constexpr uint32_t kContainerVersion = 6;     // a first word that is not an epoch

  // The scratch tables (persistence state is a fixed PSRAM line, so they are not members).
  uint16_t* eventHist() const { return noteHist_ + kPasses * kSpans; }
  uint32_t* passNotes() const { return reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(noteHist_) - kPassBytes); }
  uint32_t* mergeKeys() const { return passNotes() + kPasses; }
  uint8_t* passOrder() const { return reinterpret_cast<uint8_t*>(mergeKeys() + kPasses); }
  Open* openNotes() const { return reinterpret_cast<Open*>(reinterpret_cast<uint8_t*>(passNotes()) - kOpen * sizeof(Open)); }
  Open* waitingOffs() const { return openNotes() - kOpen; }  // NoteOffs with nothing open yet: their tick in `start`
  uint8_t* collectArea() const { return scratch_ + kReadBytes; }

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
    passDone_ = actives_ = 0;
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
    if (!read(4)) return end(Status::BadFormat);
    if (isLsv1(scratch_)) return version();
    // Maybe behind the 10-byte epoch header; else a pass stream (its first 14 body bytes already read).
    if (!card_->read(scratch_ + 4, 10)) return end(Status::BadFormat);
    if (isLsv1(scratch_ + 10)) return version();
    const bool epoch = u32(scratch_) != kContainerVersion && (u16(scratch_ + 4) >> 8) == 0;
    uint32_t have = 14;
    if (epoch) {
      for (uint8_t i = 0; i < 4; ++i) scratch_[i] = scratch_[10 + i];
      have = 4;
    }
    if (!card_->read(scratch_ + have, kPassHeader - have)) return end(Status::BadFormat);
    loop_ = u32(scratch_ + 8);
    if (loop_ == 0 && u32(scratch_ + 32) == 0) return end(Status::EmptySnapshot);  // an empty slot's placeholder
    if (loop_ == 0 || loop_ >= 0x80000000u) return end(Status::BadFormat);
    if (u32(scratch_ + 12) % loop_ != 0) return end(Status::StartOffset);
    passCount_ = u32(scratch_ + 32);
    spanTicks_ = (loop_ + kSpans - 1) / kSpans;
    phase_ = Phase::PassHeader;
    return status_;
  }
  Status version() {
    if (!read(4)) return end(Status::BadFormat);
    if (u16(scratch_) != 1) return end(Status::BadFormat);
    phase_ = Phase::Section;
    return status_;
  }
  static bool isLsv1(const uint8_t* b) { return b[0] == 'L' && b[1] == 'S' && b[2] == 'V' && b[3] == '1'; }

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
        NoteRec* buf = reinterpret_cast<NoteRec*>(collectArea());
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
        if (eventHist()[span] != 0xFFFF) ++eventHist()[span];
      } else if (mode_ == Mode::EventWindow && span >= from_ && span < to_) {
        EventRec* buf = reinterpret_cast<EventRec*>(collectArea());
        buf[collected_++] = EventRec{tick, eventsDone_ + k, status, d1, d2, 0};
      }
    }
    eventsDone_ += n;
    if (eventsDone_ < eventCount_) return status_;
    if (mode_ == Mode::Count) return counted();
    return feedEvents();
  }

  /// Counted (an LSV1 snapshot: one pass): reserve every row, then the first note window.
  Status counted() {
    if (noteCount_ == 0 && kept_ == 0) return end(Status::EmptySnapshot);
    passNotes()[0] = noteCount_;
    passOrder()[0] = 0;
    noted_ = 1;
    return admit();
  }
  /// The first pass's rows and the kept events reserved; the later passes reserve theirs at `nextPass`.
  Status admit() {
    if (import_->begin(slot_, loop_, noted_ == 0 ? 0 : passNotes()[passOrder()[0]], kept_) != Import::Status::Running)
      return end(Status::ImportRefused);
    at_ = 0;
    from_ = 0;
    return nextNoteWindow();
  }
  Status nextNoteWindow() {
    for (;;) {
      if (at_ == noted_) {
        from_ = 0;
        return nextEventWindow();
      }
      const uint16_t* hist = noteHist_ + passOrder()[at_] * kSpans;
      while (from_ < kSpans && hist[from_] == 0) ++from_;
      if (from_ < kSpans) break;
      if (++at_ < noted_ && import_->nextPass(passNotes()[passOrder()[at_]]) != Import::Status::Running)
        return end(Status::ImportRefused);
      from_ = 0;
    }
    pass_ = passOrder()[at_];
    if (!plan(noteHist_ + pass_ * kSpans, from_, collectBytes_ / sizeof(NoteRec), to_)) return end(Status::Dense);
    mode_ = Mode::NoteWindow;
    return again() ? status_ : end(Status::IoError);
  }
  Status nextEventWindow() {
    while (from_ < kSpans && eventHist()[from_] == 0) ++from_;
    if (from_ == kSpans) {
      card_->closeRead();
      return status_ = import_->finish() == Import::Status::Done ? Status::Done : Status::ImportRefused;
    }
    if (!plan(eventHist(), from_, collectBytes_ / sizeof(EventRec), to_)) return end(Status::Dense);
    mode_ = Mode::EventWindow;
    eventsDone_ = 0;
    return again() ? status_ : end(Status::IoError);
  }

  // ---- Pass stream ---------------------------------------------------------------------------------------------------
  Status passHeader() {
    if (passDone_ == passCount_) {
      if (mode_ == Mode::Count) {
        phase_ = Phase::Tail;
        return status_;
      }
      return feedEvents();  // an event window (a note window ends with its pass)
    }
    if (!read(18)) return end(Status::BadFormat);
    active_ = scratch_[8] == 0;
    if (active_) {
      if (actives_ == kPasses) return end(Status::ManyPasses);
      a_ = actives_++;
      if (mode_ == Mode::Count) {
        const uint32_t merge = u32(scratch_ + 4);
        mergeKeys()[a_] = scratch_[9] == 0 ? 0 : merge == 0xFFFFFFFFu ? merge : merge + 1;  // the record pass first
      }
    }
    left_ = u32(scratch_ + 14);
    evDone_ = 0;
    opens_ = offs_ = 0;
    phase_ = Phase::PassEvents;
    return status_;
  }
  Status passEvents() {
    const uint32_t bytes = layout_.bytes;
    uint32_t n = left_ < kReadBytes / bytes ? left_ : kReadBytes / bytes;
    if (n != 0 && !read(n * bytes)) return end(Status::BadFormat);
    const bool notesHere = active_ && (mode_ == Mode::Count || (mode_ == Mode::NoteWindow && a_ == pass_));
    const bool eventsHere = active_ && (mode_ == Mode::Count || mode_ == Mode::EventWindow);
    for (uint32_t k = 0; k < n && (notesHere || eventsHere); ++k) {
      const uint8_t* e = scratch_ + k * bytes;
      uint32_t tick = u32(e);
      if (tick >= loop_) {  // sealed past the loop end: plays wrapped
        if (tick - loop_ >= loop_) return end(Status::BadFormat);
        tick -= loop_;
      }
      const uint8_t type = e[4];
      if (type == 0x90 || type == 0x80) {
        if (notesHere && !pairNote(e, tick, evDone_ + k)) return status_;
        continue;
      }
      uint32_t raw;
      uint8_t status, d1, d2;
      if (!eventsHere || !decode(e, raw, status, d1, d2)) continue;
      const uint8_t span = spanOf(tick);
      if (mode_ == Mode::Count) {
        ++kept_;
        if (eventHist()[span] != 0xFFFF) ++eventHist()[span];
      } else if (span >= from_ && span < to_) {
        EventRec* buf = reinterpret_cast<EventRec*>(collectArea());
        buf[collected_++] = EventRec{tick, eventsDone_ + evDone_ + k, status, d1, d2, 0};
      }
    }
    left_ -= n;
    evDone_ += n;
    if (left_ != 0) return status_;
    if (notesHere) closeWrapped();
    if (mode_ == Mode::NoteWindow && active_ && a_ == pass_) return feedNotes();  // the rest of the file is not needed
    if (mode_ == Mode::EventWindow) eventsDone_ += evDone_;
    ++passDone_;
    phase_ = Phase::PassHeader;
    return status_;
  }
  /// A note event of the pass being paired. False: the reading ended (status set).
  bool pairNote(const uint8_t* e, uint32_t tick, uint32_t order) {
    const uint8_t channel = e[5], pitch = e[layout_.payloadAt], velocity = e[layout_.payloadAt + 1];
    if (pitch > 127 || velocity > 127) return fails(Status::BadFormat);
    if (e[4] == 0x90 && velocity != 0) {
      if (opens_ == kOpen) return fails(Status::Dense);
      openNotes()[opens_++] = Open{tick, order, channel, pitch, velocity, 0};
      if (mode_ == Mode::Count) {
        ++passNotes()[a_];
        uint16_t& h = noteHist_[a_ * kSpans + spanOf(tick)];
        if (h != 0xFFFF) ++h;
      }
      return true;
    }
    if (closeLast(channel, pitch, tick)) return true;
    if (offs_ == kOpen) return fails(Status::Dense);
    waitingOffs()[offs_++] = Open{tick, order, channel, pitch, 0, 0};  // closes a note open at the pass end
    return true;
  }
  /// Closes the last open note of (channel, pitch) at `end`. False: none open.
  bool closeLast(uint8_t channel, uint8_t pitch, uint32_t end) {
    for (uint32_t i = opens_; i-- > 0;) {
      if (openNotes()[i].channel != channel || openNotes()[i].pitch != pitch) continue;
      const Open o = openNotes()[i];
      for (uint32_t j = i + 1; j < opens_; ++j) openNotes()[j - 1] = openNotes()[j];
      --opens_;
      close(o, end);
      return true;
    }
    return false;
  }
  void close(const Open& o, uint32_t end) {
    const uint8_t span = spanOf(o.start);
    if (mode_ == Mode::NoteWindow && span >= from_ && span < to_) {
      NoteRec* buf = reinterpret_cast<NoteRec*>(collectArea());
      buf[collected_++] = NoteRec{o.start, end, o.order, o.pitch, o.velocity, {0, 0}};
    }
  }
  /// The pass end: the waiting NoteOffs close the notes still open (wrapped round); a note left open after that is
  /// held for the whole loop.
  void closeWrapped() {
    for (uint32_t k = 0; k < offs_; ++k) closeLast(waitingOffs()[k].channel, waitingOffs()[k].pitch, waitingOffs()[k].start);
    for (uint32_t i = 0; i < opens_; ++i) {
      const Open& o = openNotes()[i];
      close(o, o.start == 0 ? loop_ - 1 : o.start - 1);
      if (mode_ == Mode::Count) ++heldWhole_;
    }
    opens_ = offs_ = 0;
  }
  bool fails(Status s) {
    end(s);
    return false;
  }
  Status tail() {
    if (!read(12)) return end(Status::BadFormat);
    if (u32(scratch_ + 4) != kTailMarker) return end(Status::BadFormat);
    edited_ = u32(scratch_ + 8) != 0;  // the V1 note edits are discarded, the slot reported
    // The Active passes in materialisation order (insertion sort, ≤ 16), those with notes only.
    noted_ = 0;
    uint32_t notes = 0;
    for (uint8_t a = 0; a < actives_; ++a) {
      notes += passNotes()[a];
      if (passNotes()[a] == 0) continue;
      uint8_t i = noted_++;
      for (; i > 0 && mergeKeys()[passOrder()[i - 1]] > mergeKeys()[a]; --i) passOrder()[i] = passOrder()[i - 1];
      passOrder()[i] = a;
    }
    if (notes == 0 && kept_ == 0) return end(Status::EmptySnapshot);
    if (noted_ > Import::kMaxPassRuns) return end(Status::ManyPasses);
    return admit();
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
    NoteRec* buf = reinterpret_cast<NoteRec*>(collectArea());
    sortBy(buf, collected_, [](const NoteRec& r) { return r.start; });
    for (uint32_t i = 0; i < collected_; ++i)
      if (import_->addNote(buf[i].start, buf[i].end, buf[i].pitch, buf[i].velocity, 0) != Import::Status::Running)
        return end(Status::ImportRefused);
    from_ = to_;
    return nextNoteWindow();
  }
  Status feedEvents() {
    EventRec* buf = reinterpret_cast<EventRec*>(collectArea());
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
  uint8_t slot_ = 0;
  uint8_t* scratch_ = nullptr;
  uint32_t collectBytes_ = 0;
  MidiEventLayout layout_ = kTeensyMidiEvent;
  Phase phase_ = Phase::Probe;
  Mode mode_ = Mode::Count;
  Status status_ = Status::Idle;
  uint32_t left_ = 0, loop_ = 0, spanTicks_ = 1, noteCount_ = 0, eventCount_ = 0, notesDone_ = 0, eventsDone_ = 0;
  uint32_t kept_ = 0, collected_ = 0;
  uint8_t from_ = 0, to_ = 0;
  uint16_t* noteHist_ = nullptr;   // [kPasses][kSpans]: note starts per Active pass (an LSV1 snapshot: pass 0)
  // The pass stream's walk (one file pass) and its passes.
  // A pass stream reuses `left_` (the pass's events left) and `eventsDone_` (an event window's order base: the events
  // of the passes before).
  uint32_t passCount_ = 0, passDone_ = 0, evDone_ = 0, heldWhole_ = 0;
  uint16_t opens_ = 0, offs_ = 0;
  bool active_ = false, edited_ = false;
  uint8_t a_ = 0, actives_ = 0;
  uint8_t noted_ = 0, at_ = 0, pass_ = 0;
};

}  // namespace ContentEngine
