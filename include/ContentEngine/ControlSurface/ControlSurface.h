//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The control surface on the engine. The gesture map is the DROID patch's: a change here is a change to
// `droid/midilooper_v1.ini` too.
//
// Input: the DROID control plane, MIDI channels 13–16 (`controlPlane`: never recorded, never thru). Buttons are notes
// on channel 16 (`MidiConfig`): record 36, track switch 37, transport 39, play / stop 40, loop slots 50–57, tracks
// 60–67; NOTE_EDIT (`NoteEditSurface`): edit mode 38 (short: the track buttons' select / mute mode; long: NOTE_EDIT on
// the shown slot ↔ LOOP_EDIT, from performance LOOP_EDIT when the slot is empty; held 1.5 s: leave; LOOP_EDIT's F4 is
// the coarse tempo, whole BPM, pitch bend on channel 15, and F3 the fine tempo, 0.1 BPM from its centre, CC 100 on
// channel 16; F2 the loop length on a scale of 25 notches, 1/16 bar to 128 bars, CC 101 on channel 16 (or pitch bend on
// channel 14), applied at rest as a play window (one undo step a LOOP_EDIT visit, DEC-257); F1 the loop start in 16ths,
// pitch bend on channel 16, applied at rest by rotating the content, counted from LOOP_EDIT's entry; every motor parked
// on entry, `loopWrite`), NOTELEN 35 (short: start ↔ end, double: delete / create, held: F4 is the velocity), F1 pitch
// bend channel 16, F2 pitch bend channel 14, F3 / F4 CC 2 / CC 3 channel 15; while editing the record button's double /
// triple are the session's undo / redo. The extended edit buttons 41–48 are read and ignored.
// * Presses (`PressDetector`): long when held ≥ the long time (on release); else a tap — a second tap within the double
//   window is a double (at once, or after the triple window when the button has a triple action), a third within the
//   triple window a triple; a lone tap is a short once the double window passes. Per-button debounce after a release. A
//   release without its press is dropped.
// * Gestures, on the selected track `t` and its selected slot (engine slot t × 8 + s), as `MidiButtonActions`:
//   - record 36: short = record / overdub toggle (armed: on a stopped transport it starts the transport and the take
//     with it, on a running one the arm is cancelled; a take ends — Record at the bar, overdub at once; an empty slot
//     arms; a playing track overdubs; a stopped one plays the slot); double = undo; triple = redo; long = clear;
//   - slot 50–57: short = the slot's toggle (selects it; during a take on another slot: the take ends, then the slot
//     records (empty) or plays (filled) at the loop end; an empty slot arms Record — at the playing slot's loop start,
//     or the bar on a running transport, or waiting for the transport — and a second press punches in on a playing
//     track, starts a stopped transport, or cancels the arm on a running one; the playing slot toggles the track's
//     mute; another filled slot plays from the loop end, or at once on the grid when the track is stopped — with the
//     transport stopped it is only selected, the next start plays it); double = launch: a filled slot plays at the next
//     launch step (the global launch quantisation, `TrackControl::setLaunchGrid`: 1/16 default, 1/8, 1/4, bar) — a
//     queued one sooner than its loop end; an empty slot, or one on a track taking, as the short press; the playing
//     slot keeps playing, a queued switch dropped (clear is record's long press; overdub its short); triple = redo;
//     long = get a loop for it (DEC-272, the get-a-loop picker: the picker over the working set's base stage, its
//     empty-loop presets included; without a browser, as before: on an empty slot an empty loop of `kNewLoopBars` bars,
//     then the length popup). In the picker NOTELEN, PLAY/STOP, REC and EDIT move the cursor (up, down, left, right);
//     in its confirmation REC and EDIT select YES / CANCEL;
//   - track switch 37: short = the next track; double = undo; triple = redo; long = the sets browser (opening and
//     cancelling it is a long hold; an open NOTE_EDIT or LOOP_EDIT is left first, its changes kept; without a library:
//     the mute toggle as before — mute stays on the track buttons' double press). While the browser is open record =
//     up, edit = down, track switch = its action, its double = the selected stage (or on `MOVE SET TO TRASH` the set)
//     to the trash at once, its triple = that back; a long press of any button closes the browser (a long press always
//     exits a window — the length popup too, kept);
//   - transport 39: short = start / stop (the internal clock); double = back to the loop start (stop, then start at the
//     next bar line). Under a running external clock (as Elektron / Squarp slaves) it is a local stop / start: every
//     track and take ends while the master's clock runs on, and the next press brings the tracks back (the ones
//     stopped, and every unmuted track's selected slot with content, as a Start) at the next bar line; nothing is sent
//     back to the master. Under a stopped external clock it does nothing (the master's Start starts). The double does
//     nothing under an external clock;
//   - play / stop 40: short = the selected track stops, or plays its selected slot;
//   - tracks 60–67: short = select; double = mute toggle; long = solo toggle;
//   - step buttons 0–15 and bar buttons 17–24 (performance only — not in an edit, the browser or a popup): on the
//     press, the selected track's playing slot restarts at that position at the next launch step
//     (`TrackControl::queueSeek`; a queued switch replaced) — a 16th of the bar playing, a bar of the 8-bar page
//     playing, modulo the loop. A stopped or taking track ignores them. In NOTE_EDIT the step buttons are the 16ths of
//     the bracket's bar (`NoteEditSurface`): short = select / cycle (an empty step: a 16th C4), double = delete, held
//     `kLockMs` = the parameter lock until the release (no gesture then); their LEDs show that bar's notes and the
//     bracket's step. In LOOP_EDIT a bar or a step held and another of its kind pressed, either way round, makes the
//     range between them the loop (a double press: that bar or 16th alone): its left end the new start (a rotation),
//     its length the bars / 16ths covered (odd lengths too) — places in the stored loop (the start is saved with the
//     state; bars of its 8-bar page playing, 16ths of its bar playing); the visit's one undo step, as F1 / F2. The loop
//     remembers the last two ranges of these buttons (A and B, saved with the state); NOTELEN's single press — in
//     performance and LOOP_EDIT — switches between them, its double press plays the whole stored loop, each a new undo
//     step keeping the edits made since. Stored bars and 16ths outside the loop that hold notes light dim.
// * The encoder (the Teensy's GPIO quadrature encoder and its push, pins 29 / 30 / 31, read by the firmware:
//   `encoderTurn` in detents, `encoderButton`), NOTE_EDIT only: push = the next option — pitch (first), move, length,
//   velocity; turn = the selected note's option by the turn, accelerated (move ×4 / ×8 / ×24 under 100 / 50 / 25 ms a
//   detent, length ×2 / ×4 / ×8, pitch and velocity ×2 / ×3 / ×4 under 100 / 75 / 50 ms). While a step button is held:
//   the turn edits its note (the lock begins at once), a push adds a 16th C4 on it, a double push deletes the selected
//   note; the step's release is then no press. The long push does nothing yet (a CC menu later).
// * Copy mode: a slot held `kCopyHoldMs` — or in NOTE_EDIT a bar button, or a step whose lock changed nothing — starts
//   it (the source's LED pulses, the screen shows the copy window); the source's release keeps it. A short press of
//   another slot / bar / step pastes there (`TrackControl::copySlot`, `NoteEditSurface::copyBar` / `copyNote`; one undo
//   step each; its LED flashes three times) and copy mode ends (the window closes after the copy; a refused paste keeps
//   it). It also closes `kCopyIdleMs` after the source's release with nothing pasted (a countdown, restarted by a track
//   switch). A double press of the source clears it — a slot, the bar's notes, or the 16th's notes (the source note's
//   step) — one undo step, it flashes, and the window closes. A long hold of another slot / bar / step (one with a
//   note) makes it the source; a long press of any other button closes copy mode at once. A bar or a step target: its
//   short press replaces the notes there, its double press merges with them (decided once the double window passes);
//   REC / EDIT's short press chooses replace / merge first, and the target's press then pastes at once. The copy holds
//   its notes as values: in a note's copy mode the bar buttons choose the bar the steps paste in, and a track or slot
//   switch re-opens NOTE_EDIT there, copy mode kept; a target past the loop's end lengthens the loop to the end of its
//   bar first (one undo step), then the paste follows once NOTE_EDIT has re-gathered. The track buttons select tracks
//   as ever, so a slot goes onto other tracks before its press; TRACK switch held, or the source slot held again, is
//   LOAD LOOP (its short press stays the next track) — the get-a-loop picker for the source slot. A long press of any
//   button, or the source pressed again, ends it; leaving NOTE_EDIT ends a bar's or a note's. A slot's long press is
//   copy mode. A play on a stopped transport starts the internal clock first, and the gesture then acts on the clock's
//   first tick (a play starts on that bar line, not one bar later); a Record only arms. Every transport start
//   (`transportStarted`, MIDI Start too) re-queues the armed Records on its first tick and starts every track that is
//   not muted and not playing on its selected slot, when that slot has content (a MIDI Start plays all tracks).
// * LEDs (channel 15 notes, the DROID patch's velocities), for the selected track's shown slot: 16th content 0–15 (64)
//   and position 16–31 (127) of the current bar, transport 39 (127), bars 40–47 (current 127, with notes 64, in the
//   loop 32), loop slots 50–57 (taking or armed 96, queued switch blinking 96 / 64, selected 127, playing muted 16,
//   playing 48, filled 32, empty selected 40), tracks 60–67 (selected 127, with content 32). `leds` sends only changes,
//   at most `max` a call.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/ControlSurface/PressDetector.h"
#include "ContentEngine/ControlSurface/SetBrowser.h"
#include "ContentEngine/ControlSurface/NoteEditSurface.h"
#include "ContentEngine/Timing/EngineClock.h"

namespace ContentEngine {


template <typename Engine>
class ControlSurface {
 public:
  // Channel 16 button notes (MidiConfig) and their detector indices.
  static constexpr uint8_t kRecordNote = 36, kTrackSwitchNote = 37, kTransportNote = 39, kPlayStopNote = 40;
  static constexpr uint8_t kLengthNote = 35, kEditModeNote = 38;
  /// The GROOVE window's own note — the DROID patch's PLAY/STOP long press (`longpress`, note
  /// 43); its press toggles the window.
  static constexpr uint8_t kGrooveNote = 43;
  static constexpr uint32_t kVelocityHoldMs = 600;
  static constexpr uint32_t kNewLoopBars = 4;  // an empty loop's length (4; a global setting later)  // NOTELEN held this long: velocity mode (the long press)
  static constexpr uint8_t kSlotBase = 50, kTrackBase = 60, kLedChannel = 14;  // channel 15 (0-based 14)
  static constexpr uint8_t kBarStepBase = 17, kStepNotes = 25;  // 16ths 0–15, bars 17–24 (channel 16; 16 unused)
  /// The DROID's fader bank: Program Change on channel 16 (`_EDIT_NOTE_STATE`).
  static constexpr uint8_t kProgramChannel = 15;  // channel 16 (0-based 15)
  static constexpr uint8_t kLoopEditBank = 0, kNoteEditBank = 1, kGrooveBank = 2;  // the GROOVE window's
  static constexpr uint8_t kGrooveCc = 20;  // the GROOVE bank's F1–F4: CC 20–23 on channel 16
  static constexpr uint8_t kTempoChannel = 14;  // LOOP_EDIT's F4: pitch bend on channel 15; F3 / F4 triggers there
  static constexpr uint8_t kLengthCc = 101;      // LOOP_EDIT's F2: CC 101 on channel 16
  static constexpr uint8_t kFineCc = 100;        // LOOP_EDIT's F3: CC 100 on channel 16 (the fine tempo)
  static constexpr uint8_t kStartTrigger = 100, kLengthTrigger = 101;  // channel 16 notes: F1 / F2 `clear`
  static constexpr uint8_t kFineTrigger = 1, kCoarseTrigger = 2;       // channel 15 notes: F3 / F4 `clear`
  static constexpr uint8_t kGrooveTrigger = 102;  // channel 16 notes 102–105, the GROOVE bank's F1–F4 `clear`
  static constexpr uint32_t kCoarseMinBpm = 20, kCoarseMaxBpm = 300;
  static constexpr uint32_t kPopupMs = 3000, kPopupTapMs = 300;  // the popup hides; a single press is not a double
  static constexpr uint32_t kPopupCancelMs = 600;                 // a long press in the popup cancels it
  static constexpr uint32_t kLengthSettleMs = 300;
  enum Button : uint8_t {
    kRecord,
    kTrackSwitch,
    kTransport,
    kPlayStop,
    kLength,
    kEditMode,
    kSlot0,
    kTrack0 = kSlot0 + 8,
    kStep0 = kTrack0 + 8,  // NOTE_EDIT's step buttons (in performance they act on the press)
    kEncoder = kStep0 + 16,  // the GPIO encoder's push
    kButtons = kEncoder + 1
  };
  static constexpr uint32_t kLockMs = 400;  // a step held this long in NOTE_EDIT: the parameter lock
  static constexpr uint8_t kTracks = 8, kSlotsPerTrack = 8;

  void init(Engine& e, EngineClock& clock) {
    e_ = &e;
    clock_ = &clock;
    press_.init();
    using T = typename PressDetector<kButtons>::Timing;
    press_.configure(kRecord, T{300, 400, 600, 0, true});
    press_.configure(kTrackSwitch, T{300, 400, 600, 0, true});
    press_.configure(kTransport, T{300, 400, 600, 50, false});  // long opens the GROOVE window
    press_.configure(kPlayStop, T{400, 400, 600, 50, false});  // long (600 ms) opens the GROOVE window
    press_.configure(kLength, T{300, 400, 600, 0, false});
    press_.configure(kEditMode, T{300, 400, 600, 0, false});
    edit_.init(e);
    for (uint8_t s = 0; s < kSlotsPerTrack; ++s) press_.configure(kSlot0 + s, T{300, 400, 600, 0, true});
    for (uint8_t t = 0; t < kTracks; ++t) press_.configure(kTrack0 + t, T{300, 400, 600, 0, false});
    for (uint8_t k = 0; k < 16; ++k) press_.configure(kStep0 + k, T{300, 400, 600, 0, false});
    press_.configure(kEncoder, T{300, 400, 600, 0, false});
    encoderOption_ = NoteEditSurface<Engine>::Nudge::Pitch;
    encoderStep_ = 0xFF;
    encoderMs_ = 0;
    lockStep_ = 0xFF;
    barsHeld_ = 0;
    copy_ = Copy::None;
    copyHolding_ = false;
    flashLed_ = 0xFF;
    for (uint8_t& s : slot_) s = 0;
    for (uint8_t& v : sent_) v = 0xFF;
    bankSent_ = 0xFF;
    blinkMs_ = 0;
    loopEdit_ = loopPark_ = popup_ = popupHeld_ = false;
    groove_ = groovePending_ = false;
    grooveToHear_ = false;
    grooveHeardMs_ = 0;
    grooveChord_ = false;
    trackMute_ = false;
    grooveSlot_ = 0xFF;
    autoTried_ = 0;
    parkedStart_ = -1;
    parkedNotch_ = -1;
    popupTaps_ = 0;
    loopMotorCount_ = 0;
    for (uint32_t& w : loopWrittenMs_) w = 0;
    pendingMs_ = 0;
    coarseBpm_ = 120;
    pendingLoop_ = 0;
    startMs_ = pendingStart_ = 0;
    startPending_ = false;
    beyondIdentity_ = stepsIdentity_ = 0;  // the scans' caches: identities restart with the engine
    scan_ = Scan{};
  }
  /// The DROID control plane: channels 13–16 are the surface's, never recorded nor thru.
  static bool controlPlane(uint8_t status) { return (status & 0xF0) < 0xF0 && (status & 0x0F) >= 12; }

  /// A control-plane message at `ms`.
  void input(uint8_t status, uint8_t d1, uint8_t d2, uint32_t ms) {
    const uint8_t type = status & 0xF0, channel = status & 0x0F;
    if (groove_ && grooveFader(type, channel, d1, d2, ms)) return;  // the GROOVE window's faders
    if (type == 0xE0) {  // pitch bend: F1 select (channel 16), F2 coarse (channel 14); LOOP_EDIT: F4 tempo (channel 15)
      const int16_t pb = static_cast<int16_t>(static_cast<int32_t>(d1 | (d2 << 7)) - 8192);
      if (channel == 15 && loopEdit_) startFader(pb, ms);
      else if (channel == 14 && loopEdit_) coarseTempo(pb, ms);
      else if (channel == 13 && (loopEdit_ || popup_)) lengthFader(pb, ms);
      else if (channel == 15) edit_.select(pb, ms);
      else if (channel == 13) edit_.coarse(pb, ms);
      return;
    }
    if (type == 0xB0 && channel == 15 && d1 == kLengthCc && (loopEdit_ || popup_)) {  // LOOP_EDIT's F2: CC 101, bars − 1
      lengthAt(notchOfCc(d2), ms);
      return;
    }
    if (type == 0xB0 && channel == 15 && d1 == kFineCc && loopEdit_) {  // LOOP_EDIT's F3: CC 100, the fine tempo
      fineTempo(d2, ms);
      return;
    }
    if (type == 0xB0 && channel == 14) {  // F3 fine CC 2, F4 pitch CC 3 (channel 15)
      if (d1 == 2) {
        edit_.fine(d2, ms);
      } else if (d1 == 3 && edit_.editing() && press_.pressed(kLength)) {  // NOTELEN held: F4 is the velocity
        press_.consume(kLength);
        edit_.velocityBegin(ms, false);  // the hand is on F4: no motor
        edit_.velocity(d2, ms);
      } else if (d1 == 3) {
        edit_.pitch(d2, ms);
      }
      return;
    }
    if (channel != 15 || (type != 0x90 && type != 0x80)) return;
    // The DROID sends PLAY/STOP as a short-press trigger, nothing while it is held (DEC-289, GROOVE window): note 43
    // (the patch's PLAY/STOP long press) toggles the GROOVE window; so does PLAY/STOP pressed while NOTELEN is held
    // (any patch). Both take the press: no start / stop, no NOTELEN gesture.
    const bool on = type == 0x90 && d2 != 0;
    if (d1 == kGrooveNote) {
      if (on) toggleGroove();
      return;
    }
    if (d1 == kTransportNote && on && press_.pressed(kLength) && !edit_.editing()) {
      press_.consume(kLength);
      grooveChord_ = true;  // its release is no press either
      toggleGroove();
      return;
    }
    if (d1 == kTransportNote && !on && grooveChord_) {
      grooveChord_ = false;
      return;
    }
    uint8_t b;
    if (loopEdit_ && d1 < kStepNotes && d1 != kBarStepBase - 1) {  // LOOP_EDIT: a held one and another set the loop
      rangeInput(d1, type == 0x90 && d2 != 0, ms);
      return;
    }
    if (d1 >= kBarStepBase && d1 < kStepNotes && edit_.editing()) {  // NOTE_EDIT's bar buttons: copy mode only
      const uint8_t bar = static_cast<uint8_t>(d1 - kBarStepBase);
      const uint8_t bit = static_cast<uint8_t>(1u << bar);
      if (type == 0x90 && d2 != 0) {
        barsHeld_ = static_cast<uint8_t>(barsHeld_ | bit);
        barDownMs_[bar] = ms;
        if (copy_ == Copy::Note) {  // the steps paste in that bar — past the loop's end too (it grows to it)
          noteBar_ = edit_.barStart(bar);
          edit_.goToBar(bar);
          copyUntil_ = ms + kCopyIdleMs;
        }
      } else if (barsHeld_ & bit) {
        barsHeld_ = static_cast<uint8_t>(barsHeld_ & ~bit);
        exitBars_ = static_cast<uint8_t>(exitBars_ & ~bit);
        if (copyReleased(Copy::Bar, bar, ms)) return;  // the source's own release
        if (copy_ == Copy::Bar && ms - barDownMs_[bar] < kCopyHoldMs) targetPress(bar, ms);
      }
      return;
    }
    if (d1 < kStepNotes) {
      if (d1 >= 16 || !(edit_.editing() || press_.pressed(kStep0 + d1))) {  // performance: they act on the press
        if (type == 0x90 && d2 != 0) stepPress(d1);
        return;
      }
      b = static_cast<uint8_t>(kStep0 + d1);  // NOTE_EDIT: gestures
    } else {
      b = buttonOf(d1);
    }
    if (b == 0xFF) return;
    if (popup_ && b == popupButton_) {  // the length popup takes the button that opened it: taps
      popupPress(type == 0x90 && d2 != 0, ms);
      return;
    }
    if (copy_ != Copy::None && copyTarget(b)) {  // copy mode: a slot / step is a paste target, on its release
      if (type == 0x90 && d2 != 0) {
        press_.down(b, ms);
        press_.consume(b);  // no gesture of its own
      } else {
        const uint32_t held = press_.heldMs(b, ms);
        const bool pressed = press_.pressed(b);
        press_.up(b, ms);
        if (b == kStep0 + lockStep_) {  // the source step's lock ends with its release
          edit_.lockEnd(ms);
          lockStep_ = 0xFF;
        }
        if (!pressed || copyReleased(copy_, static_cast<uint8_t>(b - (copy_ == Copy::Slot ? kSlot0 : kStep0)), ms)) return;
        if (held < kCopyHoldMs) paste(b, ms);
      }
      return;
    }
    if (type == 0x90 && d2 != 0) {
      press_.down(b, ms);
    } else {
      if (b == kStep0 + lockStep_) {  // a held step: the lock ends, no gesture
        press_.consume(b);
        edit_.lockEnd(ms);
        lockStep_ = 0xFF;
      }
      press_.up(b, ms);
      // A long press — velocity mode, F4 not moved by hand — creates a note.
      if (b == kLength && edit_.velocityDone(ms)) edit_.create(ms);
    }
  }

  // ---- The encoder ----------------------------------------------------------------------------------------
  /// The encoder turned `detents` (signed) at `ms`: in NOTE_EDIT the selected note's option, accelerated.
  void encoderTurn(int32_t detents, uint32_t ms) {
    const uint32_t interval = ms - encoderMs_;
    encoderMs_ = ms;
    if (groove_ && detents != 0) {  // the GROOVE window's selected field
      grooveNudge(detents, ms);
      return;
    }
    if (detents == 0 || !edit_.editing()) return;
    const uint8_t k = heldStep();
    if (k != 0xFF && lockStep_ == 0xFF) {  // a held step: its note now, without waiting for the lock's hold
      lockStep_ = k;
      edit_.lockBegin(k, ms);
    }
    using N = typename NoteEditSurface<Engine>::Nudge;
    int32_t accel = 1;
    if (encoderOption_ == N::Move) accel = interval < 25 ? 24 : interval < 50 ? 8 : interval < 100 ? 4 : 1;
    else if (encoderOption_ == N::Length) accel = interval < 25 ? 8 : interval < 50 ? 4 : interval < 100 ? 2 : 1;
    else accel = interval < 50 ? 4 : interval < 75 ? 3 : interval < 100 ? 2 : 1;
    edit_.nudge(encoderOption_, detents * accel, ms);
  }
  /// The encoder's push pressed (`down`) or released, debounced by the firmware.
  void encoderButton(bool down, uint32_t ms) {
    if (groove_) {  // a push is the next field
      if (down) nextGrooveField(1);
      return;
    }
    if (down) {
      encoderStep_ = edit_.editing() ? heldStep() : 0xFF;  // a push with a step held acts on that step
      if (encoderStep_ != 0xFF) press_.consume(static_cast<uint8_t>(kStep0 + encoderStep_));
      press_.down(kEncoder, ms);
    } else {
      press_.up(kEncoder, ms);
    }
  }
  // ---- The GROOVE window -------------------------------------------------------------------------------
  // Fields: A's groove, A's amount, B's groove, B's amount (AUTO Q: TRK, the title bar's checkbox). REC / EDIT move
  // up / down as in the load / save window, the encoder's push down, its turn the value; the GROOVE fader bank (program 2):
  // F1 A's groove, F2 A's amount, F3 B's groove, F4 B's amount — CC 20–23 on channel 16.
  void nextGrooveField(int8_t d);
  /// What is heard: OFF (no groove), A or B — two grooves, both edited in the window.
  enum class Heard : uint8_t { Off, A, B };
  static Heard columnOf(uint8_t f);
  groove::Settings& columnSettings(Heard h) { return h == Heard::A ? grooveA_ : grooveSet_; }
  /// A value changed: only the column heard is rebuilt (an intermediate groove heard would defeat the A / B
  /// comparison) (once settled); the other is kept silently until PLAY/STOP or its cycle turn plays it — a
  /// turn already asked for is asked again with the new values.
  void grooveChanged(Heard col, uint32_t ms);
  /// AUTO Q (TRK in the window): the slot's, kept with what plays.
  void toggleAutoQ(uint32_t ms);
  void grooveNudge(int32_t d, uint32_t ms);
  /// The GROOVE bank's faders (CC 20–23 on channel 16): F1 A's groove, F2 A's amount, F3 B's groove, F4 B's amount.
  /// True: taken. The fields are in the same order (A's groove, A's amount, B's groove, B's amount).
  bool grooveFader(uint8_t type, uint8_t channel, uint8_t d1, uint8_t d2, uint32_t ms);
  /// A field's value on its fader (the inverse of `grooveFader`: a template's band centre; a file template,
  /// which no fader position names, at the bottom).
  static uint8_t grooveCc(uint8_t field, const groove::Settings& g);
  /// Each of F1–F4 to its field's value on opening — the
  /// value on its CC, then its `clear` trigger note (`droid/midilooper_v1.ini`, the GROOVE bank). Its travel is not a
  /// move for `kMotorSettleMs` (`landing`).
  void parkGrooveFaders(uint32_t ms);
  // ---- Compare -----------------------------------------------------------------------------------
  static constexpr uint32_t kBarTicks = NoteEditSurface<Engine>::kBar;
  /// The cycle's bars a turn: a global setting, no control yet.
  uint8_t cycleBars() const { return cycleBars_; }
  void setCycleBars(uint8_t b) { cycleBars_ = b == 0 ? 1 : b; }
  bool cycling() const { return cycling_; }
  /// What is heard now: cycling, A or B by the turn (A from its first bar line); else the last choice.
  Heard grooveHeard() const;
  const groove::Settings& grooveA() const { return grooveA_; }
  bool grooveAuto() const { return grooveAuto_; }
  /// The settings a choice builds: A's or B's (OFF: none) over the whole loop, with the slot's AUTO Q.
  groove::Settings settingsOf(Heard h) const;
  void playHeard(Heard h, uint32_t at = 0);
  void stopCycle();
  /// ADD (NOTELEN) in the window (as NOTELEN's ranges in performance: single A ↔ B, double FULL): single A ↔ B (from
  /// OFF: A; cycling: keeps the one playing), double OFF (and stops a cycle), hold the cycle on / off — A and B taking
  /// turns of `cycleBars` bars from the next bar line.
  void grooveCompare(Press p);
  /// The cycle's next turn built a bar ahead (time for a long loop's build) and switched in on its bar
  /// line.
  void serviceCycle();
  void serviceGroove(uint32_t ms);

  // ---- Copy mode ------------------------------------------------------------------------------
  enum class Copy : uint8_t { None, Slot, Bar, Note };
  static constexpr uint32_t kCopyHoldMs = 600, kFlashMs = 100;  // the hold that starts it; a flash's half period
  static constexpr uint32_t kCopyIdleMs = 5000, kCopyDoubleMs = 300;  // the countdown; the source's double press
  /// The countdown left (ms; the whole while the source is still held), for the window's line.
  uint32_t copyMsLeft(uint32_t ms) const;
  Copy copyMode() const { return copy_; }
  /// A bar / step paste: chosen by the target's press (Ask), or by REC (Replace) / EDIT (Merge).
  enum class PasteMode : uint8_t { Ask, Replace, Merge };
  PasteMode pasteMode() const { return pasteMode_; }
  uint8_t copySourceSlot() const { return copySlot_; }  // Slot: the engine slot
  uint8_t copySourceBar() const { return copyButton_; }  // Bar: 0–7 of its page
  int16_t copySourceNote() const { return copyNote_; }  // Note: its index in the session
  /// The progress bar's frame (every note in the stored loop known; absolute): `scale` the stored loop's ticks
  /// (`frameOf`), `start` the current loop's start in it, `known` how far its notes reach from its start in whole bars
  /// (the loop past it is drawn as past the known length). False: no content.
  /// Groove: starts the slot's groove build per `s` (amount 0: its removal) over its stored loop, scope A / B its
  /// remembered ranges. Built in the background (`service`), heard from a bar line once ready; the settings of one
  /// visit are one undo step. The engine's result (Ok: started).
  /// `table` / `label`: a groove file's template; nullptr with `kFileTemplate`: the one stored with the state.
  typename Engine::Transport::Result applyGroove(uint8_t slot, const groove::Settings& s,
                                                 const groove::Table* table = nullptr, const char* label = nullptr,
                                                 uint8_t heard = 0);
  // ---- The GROOVE window (PLAY/STOP's long press) --------------------------------------------------------------------------
  /// Fields: template, amount, scope (FULL / A / B), AUTO. F1 amount, F2 template, F3 scope, F4 AUTO (the loop-edit
  /// fader bank); the encoder's turn the selected field, its push (or PLAY/STOP's press) the next; a long press of any
  /// button closes it (or note 43 / NOTELEN + PLAY/STOP again); it stays open otherwise. A change is applied
  /// once it rests `kLengthSettleMs` (built in the background, heard from the next beat); a visit's changes
  /// are one undo step.
  enum GrooveField : uint8_t { kFieldGrooveA = 0, kFieldAmountA, kFieldGrooveB, kFieldAmountB, kGrooveFields };
  static constexpr uint32_t kEditExitMs = 1500;  // EDIT held this long leaves the edit modes
  /// The track buttons' mode: select (false) or mute (true), toggled by EDIT's short press.
  bool trackMuteMode() const { return trackMute_; }
  bool grooveOpen() const { return groove_; }
  /// How far a change is from being heard, in percent (0xFF: nothing under way) — settling (0–25), building
  /// (25–75), then until the beat it switches on (75–100).
  uint8_t grooveProgress() const;
  uint8_t grooveField() const { return grooveField_; }
  /// The save dots' states: 0 none, 1 a change settling (dim), 2 building or waiting for its bar line /
  /// beat (one running), 3 heard in the last `kDotsDoneMs` (bright).
  static constexpr uint32_t kDotsDoneMs = 800;
  uint8_t grooveDots() const;
  /// How far the cycle's current turn is, in 1/1000 (0 before its first bar line).
  uint16_t cycleTurnPos() const;
  const groove::Settings& grooveSettings() const { return grooveSet_; }
  uint8_t grooveWindowSlot() const { return grooveWin_; }
  void openGroove(uint8_t slot);
  /// The columns stored with the slot: A, B and the column heard, when the heard one is still what
  /// the slot plays (else the slot's groove as A, as before, B kept). A column naming a file template not stored with
  /// the slot is straight.
  void restoreColumns(uint8_t slot, uint32_t packed);
  void toggleGroove();
  void closeGroove();
  void writeColumns(uint32_t replace);

  /// Ends the groove visit (the GROOVE window closed): the next setting is an undo step of its own.
  void leaveVisit();
  /// Is the slot's loop its whole stored loop (true without content)?
  bool isFull(uint8_t slot);
  bool loopFrame(uint8_t slot, uint32_t& scale, uint32_t& start, uint32_t& known);
  /// Diagnostics: full scans of a state's rows the LEDs made (one per new state; never per bar of playback).
  uint32_t rowScans() const { return rowScans_; }
  /// The encoder's option (pitch, move, length, velocity), for the screen.
  typename NoteEditSurface<Engine>::Nudge encoderOption() const { return encoderOption_; }

  /// Once per main-loop pass: the gestures due, at the last played tick.
  void service(uint32_t ms, uint32_t lastTick, uint32_t nowUs);
  const NoteEditSurface<Engine>& edit() const { return edit_; }
  /// LOOP_EDIT is open (the edit button's cycle).
  bool loopEdit() const { return loopEdit_; }
  /// LOOP_EDIT's F2 moved, not yet applied: the length it will take, in ticks (0: none).
  uint32_t pendingLoopTicks() const { return pendingLoop_; }
  /// The length popup is shown: its slot, and the length it shows (F2's pending one, else the slot's).
  bool popup() const { return popup_; }
  uint8_t popupSlot() const { return popupSlot_; }
  /// The popup's selected choice (0 FULL, 1 A, 2 B, 3–14 the lengths 1–8, 16–128 bars, 0xFF
  /// none).
  uint8_t popupChoice() const { return popupChoice_; }
  /// The remembered ranges the popup offers: bit 0 A, bit 1 B.
  uint8_t popupRanges();
  uint32_t popupLoop() const;
  /// LOOP_EDIT's loop start in the stored loop (F1's pending one while it settles).
  uint32_t loopStart();
  /// The next motor message, sent like the LEDs: NOTE_EDIT's, else LOOP_EDIT's (`loopWrite`).
  bool motor(uint32_t ms, typename NoteEditSurface<Engine>::MotorMsg& m);

  /// The transport started with `firstTick` as its first tick: armed Records start there (not a bar later).
  void transportStarted(uint32_t firstTick);

  /// The engine slot a track has selected (its slot button's), for the screen.
  uint8_t selectedSlot(uint8_t t) const { return static_cast<uint8_t>(t * kSlotsPerTrack + slot_[t]); }
  void selectSlot(uint8_t t, uint8_t s) { slot_[t] = s; }

  /// The sets browser: opened by a long track switch while the library is ready; nullptr: none.
  /// What the track's piano roll shows: a queued switch's slot at once — the playhead parked at its
  /// launch point until the switch (`shownQueued`) — else the playing slot, else the selected one.
  uint8_t shown(uint8_t t) { return shownSlot(t); }
  bool shownQueued(uint8_t t) { return control().switchQueued(t); }
  /// The loop position a queued switch starts at (a step or bar button's; 0: the loop start).
  uint32_t shownQueuedPosition(uint8_t t) { return control().queuedPosition(t); }
  void setBrowser(ProductionBrowser* b);
  bool browsing() const { return browser_ != nullptr && browser_->isOpen(); }

  /// The DROID's fader bank when it is to change (F2–F4 sent the loop-edit bank's messages in
  /// NOTE_EDIT — the DROID switches banks on a Program Change on channel 16, which the engine never sent): the
  /// note-edit bank while a NOTE_EDIT session is open (at the session's opening), else the loop-edit bank;
  /// the first call (boot) sends the current one, so the DROID follows a restart too. False: no change.
  bool bank(uint8_t& program);

  /// The LEDs wanted now, every note (diagnostics: `!ENGINE leds`).
  void ledsWanted(uint32_t lastTick, uint32_t ms, uint8_t* want);
  /// The LED changes (channel 15 notes), at most `max`: `send(note, velocity)`, velocity 0 = off.
  template <typename Send>
  uint8_t leds(uint32_t lastTick, uint32_t ms, Send send, uint8_t max);

 private:
  using X = typename Engine::Transport;
  static constexpr uint32_t kBar = 768, kSixteenth = kBar / 16;

  static uint8_t buttonOf(uint8_t note) {
    if (note == kRecordNote) return kRecord;
    if (note == kTrackSwitchNote) return kTrackSwitch;
    if (note == kTransportNote) return kTransport;
    if (note == kPlayStopNote) return kPlayStop;
    if (note == kLengthNote) return kLength;
    if (note == kEditModeNote) return kEditMode;
    if (note >= kSlotBase && note < kSlotBase + kSlotsPerTrack) return static_cast<uint8_t>(kSlot0 + note - kSlotBase);
    if (note >= kTrackBase && note < kTrackBase + kTracks) return static_cast<uint8_t>(kTrack0 + note - kTrackBase);
    return 0xFF;
  }

  auto& control() { return e_->control(); }
  auto& transport() { return e_->transport(); }
  uint32_t us() const { return clock_->usPerTick(); }
  /// No content: no state, or a cleared one (Clear is undoable: an empty state).
  bool empty(uint8_t slot) { return e_->transport().contentOf(slot) == kNoManifest; }
  uint8_t selected() { return control().selectedTrack(); }

  /// The transport button under an external clock — a local stop, or the tracks back at the next bar line.
  void externalTransport() {
    if (!clock_->running()) {  // a stopped master (DEC-276): start locally,
      clock_->startOnPulse();   // the next pulse as a bar line, as a Start
      control().transportStart(clock_->nextTick(), us_, us());
      tick_ = clock_->nextTick() - 1;
      transportStarted(clock_->nextTick());
      return;
    }
    auto& x = transport();
    bool active = x.writingTrack() != 0xFF;
    for (uint8_t t = 0; t < kTracks && !active; ++t)
      active = x.playing(t) || control().switchQueued(t) || control().recordQueued(t);
    if (active) {
      control().transportStop(tick_, us_, us());
      return;
    }
    control().resumeAtBar(tick_);
    for (uint8_t t = 0; t < kTracks; ++t) {  // as a Start: every unmuted track's selected slot with content
      const uint8_t slot = selectedSlot(t);
      if (control().switchQueued(t) || control().muted(t) || empty(slot)) continue;
      control().queueSwitch(t, slot, Engine::Control::Quantize::NextBar, tick_, false);
    }
  }
  void startClock() {
    if (clock_->running() || clock_->external()) return;  // an external clock runs the transport itself
    clock_->start(us_);
    control().transportStart(clock_->nextTick(), us_, us());
    tick_ = clock_->nextTick() - 1;  // the gestures that follow are due on the first tick
    transportStarted(clock_->nextTick());
  }
  void stopClock() {
    if (clock_->external()) return;
    clock_->stop();
    control().transportStop(tick_, us_, us());
  }
  /// The take on track `t` ends: a Record at the next bar, or at the bar line just passed when pressed at most half a
  /// bar after it; an overdub at once.
  void endTake(uint8_t t) {
    if (transport().mode() == X::Mode::Recording)
      control().queueStop(t, tick_);
    else
      control().stopNow(tick_, us_, us());
  }
  void play(uint8_t t, uint8_t slot, typename Engine::Control::Quantize q) {
    startClock();
    control().queueSwitch(t, slot, q, tick_);
  }

  void browse(uint8_t b, Press p) {
    const uint8_t times = p == Press::Triple ? 3 : p == Press::Double ? 2 : 1;
    if (p == Press::Long) {  // a long press always exits the window, back to playing
      browser_->close();
      return;
    }
    if (b == kTrackSwitch) {  // once per gesture (a double press must not confirm, then act on the row under it)
      if (p == Press::Short) browser_->press();
      else if (p == Press::Double) browser_->trashSelected();  // destructive: a double, undone by a triple
      else if (p == Press::Triple) browser_->undoTrash();
      return;
    }
    if (browser_->picking()) {  // the get-a-loop picker: NOTELEN up, PLAY/STOP down, REC left, EDIT right
      if (browser_->view() != ProductionBrowser::View::Picker) {  // its confirmation: REC YES, EDIT CANCEL
        if (b == kRecord) browser_->up();
        else if (b == kEditMode) browser_->down();
        return;
      }
      for (uint8_t i = 0; i < times; ++i) {
        if (b == kLength) browser_->pickUp();
        else if (b == kPlayStop) browser_->pickDown();
        else if (b == kRecord) browser_->pickLeft();
        else if (b == kEditMode) browser_->pickRight();
      }
      return;
    }
    for (uint8_t i = 0; i < times; ++i) {
      if (b == kRecord && p != Press::Long) browser_->up();
      else if (b == kEditMode && p != Press::Long) browser_->down();
    }
  }
  void dispatch(uint8_t b, Press p) {
    const uint8_t t = selected();
    const uint8_t slot = selectedSlot(t);
    if ((copy_ == Copy::Bar || copy_ == Copy::Note) && b >= kSlot0 && b < kSlot0 + kSlotsPerTrack && p == Press::Short) {
      const uint8_t target = static_cast<uint8_t>(t * kSlotsPerTrack + (b - kSlot0));  // NOTE_EDIT on that slot
      if (!empty(target) && target != edit_.slot()) {
        slot_[t] = static_cast<uint8_t>(b - kSlot0);
        edit_.leave();
        enterEditOn(t, target);
      }
      copyUntil_ = nowMs_ + kCopyIdleMs;
      return;
    }
    if ((copy_ == Copy::Bar || copy_ == Copy::Note) && (b == kRecord || b == kEditMode) && p == Press::Short) {
      pasteMode_ = b == kRecord ? PasteMode::Replace : PasteMode::Merge;  // the next target pastes at once
      copyUntil_ = nowMs_ + kCopyIdleMs;
      return;
    }
    if (browsing() && (p == Press::Long || b == kRecord || b == kTrackSwitch || b == kEditMode ||
                       (browser_->picking() && (b == kLength || b == kPlayStop)))) {
      browse(b, p);  // a long press of any button closes it
      return;
    }
    if (groove_ && b == kLength) {  // ADD compares — single A ↔ B, double OFF, hold the cycle
      grooveCompare(p);
      return;
    }
    if (groove_ && p == Press::Long) {  // the GROOVE window: a long press of any other button applies
      closeGroove();
      return;
    }
    if (groove_ && (b == kPlayStop || b == kTransport)) return;  // PLAY/STOP: its hold (note 43) applies; no stop
    if (groove_ && (b == kRecord || b == kEditMode || b == kTrackSwitch)) {  // the load / save window's
      if (p == Press::Short && b == kTrackSwitch) toggleAutoQ(nowMs_);  // TRK: AUTO Q
      else if (p == Press::Short) nextGrooveField(b == kRecord ? -1 : 1);  // REC up, EDIT down
      return;
    }
    if (popup_ && p == Press::Long && b != popupButton_) {  // a long press of another button closes the popup, kept
      popup_ = false;
      visitState_ = 0;
      return;
    }
    switch (b) {
      case kRecord:
        if (p == Press::Double && edit_.editing()) edit_.undo(nowMs_);
        else if (p == Press::Triple && edit_.editing()) edit_.redo(nowMs_);
        else if (p == Press::Short) toggleRecord(t);
        else if (p == Press::Double) control().undo(slot, tick_, us_, us());
        else if (p == Press::Triple) control().redo(slot, tick_, us_, us());
        else control().clear(slot, tick_, us_, us());
        return;
      case kTrackSwitch:
        if (p == Press::Short) switchTrack(static_cast<uint8_t>((t + 1) % kTracks));
        else if (p == Press::Double) control().undo(slot, tick_, us_, us());
        else if (p == Press::Triple) control().redo(slot, tick_, us_, us());
        else if (browser_ != nullptr) {  // the sets browser, an open edit left first (rule 1)
          leaveEdit();
          browser_->open();
        } else {
          control().setMute(t, !control().muted(t), us_);
        }
        return;
      case kTransport:
        if (p == Press::Long) {  // the GROOVE window on the shown slot (the DROID's PLAY/STOP)
          openGroove(shownSlot(t));
          return;
        }
        if (clock_->external()) {
          if (p == Press::Short) externalTransport();
        } else if (p == Press::Short) {
          if (clock_->running()) stopClock();
          else startClock();
        } else if (p == Press::Double) {  // back to the loop start: the next bar line
          stopClock();
          startClock();
        }
        return;
      case kEditMode:  // short = the track buttons' select / mute mode, in every mode;
        // long (released before `kEditExitMs`) = into NOTE_EDIT, then NOTE_EDIT ↔ LOOP_EDIT; held `kEditExitMs` =
        // out of the edit modes (`service`, while held)
        if (p == Press::Short) trackMute_ = !trackMute_;
        else if (p == Press::Long) cycleEdit(t);
        return;
      case kLength:
        if (loopEdit_) {  // LOOP_EDIT: an empty slot gets an empty loop (short or long); long: the length popup
          if (p == Press::Short && !empty(shownSlot(t))) {  // the other range, as in performance (3)
            switchRange(shownSlot(t));
            return;
          }
          if (p == Press::Double) {  // the whole stored loop
            playFull(shownSlot(t));
            return;
          }
          if (p == Press::Short || p == Press::Long)
            control().createEmpty(shownSlot(t), kNewLoopBars * NoteEditSurface<Engine>::kBar);  // refused when filled
          if (p == Press::Long) openPopup(shownSlot(t), kLength, nowMs_);
          return;
        }
        if (!edit_.editing() && !edit_.entering() && (p == Press::Short || p == Press::Double)) {
          // performance: the other remembered range; double: the whole stored loop
          if (p == Press::Short) switchRange(shownSlot(t));
          else playFull(shownSlot(t));
          return;
        }
        if (p == Press::Short) edit_.lengthOrCreate(nowMs_);
        else if (p == Press::Double) edit_.deleteOrCreate(nowMs_);
        return;
      case kPlayStop:
        if (p == Press::Long) {  // the GROOVE window on the shown slot
          openGroove(shownSlot(t));
          return;
        }
        if (p != Press::Short) return;
        if (transport().playing(t)) control().stopTrack(t, tick_, us_, us());
        else if (!empty(slot)) play(t, slot, Engine::Control::Quantize::NextGrid);
        return;
      default:
        break;
    }
    if (b == kEncoder) {  // NOTE_EDIT only
      if (!edit_.editing()) return;
      const uint8_t k = encoderStep_;
      if (k != 0xFF && p == Press::Short) {
        edit_.stepAdd(k, nowMs_);
      } else if (k != 0xFF && p == Press::Double) {
        edit_.deleteSelected(nowMs_);
      } else if (k == 0xFF && p == Press::Short) {  // the next option: pitch, move, length, velocity
        using N = typename NoteEditSurface<Engine>::Nudge;
        encoderOption_ = static_cast<N>((static_cast<uint8_t>(encoderOption_) + 1) % 4);
      }
      return;  // long: a CC menu later
    }
    if (b >= kStep0 && b < kStep0 + 16) {  // NOTE_EDIT's step buttons
      const uint8_t k = static_cast<uint8_t>(b - kStep0);
      if (p == Press::Short) edit_.stepPress(k, nowMs_);
      else if (p == Press::Double) edit_.stepDelete(k, nowMs_);
      return;
    }
    if (b >= kSlot0 && b < kSlot0 + kSlotsPerTrack) {
      const uint8_t s = static_cast<uint8_t>(b - kSlot0);
      const uint8_t target = static_cast<uint8_t>(t * kSlotsPerTrack + s);
      if (p == Press::Short) {
        slotToggle(t, s);
      } else if (p == Press::Double) {  // launch at the next launch step (clear is record's long press)
        launch(t, s);
      } else if (p == Press::Triple) {
        slot_[t] = s;
        control().redo(target, tick_, us_, us());
      }  // long: copy mode takes the hold (`serviceCopy`)
      return;
    }
    if (b >= kTrack0 && b < kTrack0 + kTracks) {
      const uint8_t tt = static_cast<uint8_t>(b - kTrack0);
      if (p == Press::Short && trackMute_) control().setMute(tt, !control().muted(tt), us_);  // mute mode
      else if (p == Press::Short) switchTrack(tt);
      else if (p == Press::Double) control().setMute(tt, !control().muted(tt), us_);
      else if (p == Press::Long) control().setSolo(tt, !control().soloed(tt), us_);
    }
  }

  /// A track select. NOTE_EDIT follows the track (it stayed open on the track left): the session is
  /// left — its edits one undo step on its slot, as leaving — and NOTE_EDIT opens on the new track's shown slot, or
  /// ends when that slot is empty.
  void switchTrack(uint8_t tt) {
    const bool follow = edit_.active() && tt != selected();
    const bool loopFollow = loopEdit_ && tt != selected();
    control().selectTrack(tt);
    if (loopFollow) enterLoopEdit();  // the new track's loop: faders parked, start counted from its content
    if (!follow) return;
    edit_.leave();
    enterEdit(tt);
  }
  /// The edit button's long press: from performance NOTE_EDIT, or LOOP_EDIT when
  /// the shown slot has no notes to edit; then NOTE ↔ LOOP (NOTE only on content; CC later). Held `kEditExitMs`: leave.
  void cycleEdit(uint8_t t);
  void enterLoopEdit();
  /// LOOP_EDIT's motor faders (`droid/midilooper_v1.ini`, the loop bank): a value, then its `clear` trigger note on
  /// and, after `kMotorGateMs`, off. F1 the loop start (pitch bend channel 16, note 100 channel 16), F2 the length
  /// (CC 101 channel 16, note 101 channel 16), F3 the fine tempo (CC 100 channel 16, note 1 channel 15), F4 the coarse
  /// tempo (pitch bend channel 15, note 2 channel 15). A fader's own messages are not moves for `kMotorSettleMs`.
  void loopWrite(uint8_t fader, uint8_t status, uint8_t d1, uint8_t d2, uint8_t trigStatus, uint8_t trigNote, uint32_t ms);
  void parkLoopFaders(uint32_t ms);
  void writeFine(uint8_t cc, uint32_t ms);
  bool landing(uint8_t fader, uint32_t ms) const { return ms - loopWrittenMs_[fader] < NoteEditSurface<Engine>::kMotorSettleMs; }
  /// LOOP_EDIT's F1 (pitch bend on channel 16): the loop start in the stored loop,
  /// over its 16ths (absolute). Applied at rest (`setWindow`), one undo step a visit; not while its motor lands.
  void startFader(int16_t pb, uint32_t ms);
  void settleStart(uint32_t ms);
  /// LOOP_EDIT's F2: the shown loop's length, 1–128 bars over the travel. Applied
  /// once the fader rests `kLengthSettleMs` (one undo step a move; the screen shows the pending length), not while its
  /// motor lands.
  void lengthFader(int16_t pb, uint32_t ms);
  /// F2's travel (the motor holds 25 notches, and 1–8 bars over 128 steps was niggly): 25 positions —
  /// 1/16, 1/8, 1/4, 1/2, 3/4 bar, 1–16 bars, 32, 64, 96, 128 bars, over CC 101's 0–127 (the nearest notch: a
  /// notch's value is `round(notch × 127 / 24)`). The notch number sent as is saturates at CC 127
  /// past notch 0, so the patch scales it back to 0–1 (or the fader runs continuous).
  static constexpr uint8_t kLengthScaleCount = 25;
  static uint32_t loopAt(uint8_t i);
  static uint8_t notchOfCc(uint8_t cc);
  static uint8_t ccOfNotch(uint8_t i) { return static_cast<uint8_t>((i * 127 + (kLengthScaleCount - 1) / 2) / (kLengthScaleCount - 1)); }
  /// The position that puts F2 on `loop` ticks (a length off the scale: the position below it).
  static uint8_t positionOf(uint32_t loop);
  /// F2 at position `pos` (CC 101 on channel 16, the DROID's loop bank).
  void lengthAt(uint8_t pos, uint32_t ms);
  void settleLength(uint32_t ms);
  /// LOOP_EDIT's step / bar buttons: the first held is the range's anchor; another pressed while it is held
  /// sets the range (`loopRange`). A double press loops
  /// that one bar or 16th.
  void rangeInput(uint8_t note, bool down, uint32_t ms);
  /// The range between buttons `a` and `b` (either way) as the shown loop: the left one's position
  /// its new start, the buttons covered its length. The buttons are absolute (DEC-283):
  /// bars of the stored loop's 8-bar page holding the playhead (stopped: the loop's
  /// start), 16ths of its bar. Refused when the start lies at or past the stored loop's end.
  void loopRange(bool bars, uint8_t a, uint8_t b);
  /// What a LOOP_EDIT change makes of the loop: a remembered range (the step / bar buttons), or
  /// nothing (F1, F2, the popup, NOTELEN).
  enum class Mark : uint8_t { Range, Keep };
  /// The loop `loop` ticks long from its start (F2, the popup).
  void resizeTo(uint8_t slot, uint32_t loop, Mark mark);
  /// The shown loop becomes `length` ticks from `start` in the stored loop, in this visit's undo step: the content back
  /// to the whole stored loop, rotated there to the start (lossless: rotated at the frame's length), then cut to the
  /// length; a loop reaching past the stored loop's end wraps to its start. A loop longer than the stored loop — or,
  /// `grow`, one reaching past its end — makes it longer (rotated back to its start first, so the stored notes keep
  /// their places). `mark`: a range of the buttons (`Range`: the newest remembered range, the one playing before it the
  /// other) or not; the remembered ranges are kept with every state.
  bool setWindow(uint8_t slot, uint32_t start, uint32_t length, Mark mark, bool grow = false);
  // ---- The remembered ranges (two custom loop lengths to switch between; a double press plays the whole loop) ----------------------------------------
  uint32_t loopTicks(uint8_t slot);
  /// NOTELEN's single press, in performance and LOOP_EDIT: the other remembered range — from B to A, from A to B, from
  /// anything else to B (or A, the only one); its own undo step, the edits made since kept.
  void switchRange(uint8_t slot);
  /// NOTELEN's double press, in performance and LOOP_EDIT: the whole stored loop (its own undo step; the ranges kept).
  void playFull(uint8_t slot);

  // ---- The length popup ---------------------------------------------------------------------------
  /// Opened by a long press on a slot (an empty one gets its empty loop first) or NOTELEN's in LOOP_EDIT, on `slot`;
  /// `button` (the one that opened it) is then the popup's (the tap bursts were annoying and had no way back): a single
  /// press steps to the next of its choices, a double press to the one before, cycling round at either end. The
  /// choices: FULL, the ranges A and B (each skipped while there is none), then 1–8, 16, 32, 64, 128 bars from the
  /// stored loop's start — trying a length keeps the ranges. F2 sets a length the same way. It hides `kPopupMs` after
  /// the last change; its changes are one undo step (a visit, as LOOP_EDIT's). F2's motor is parked on opening.
  void openPopup(uint8_t slot, uint8_t button, uint32_t ms);
  /// A press of the popup's button: its release is a tap (the opening long press fired at its own release); held
  /// `kPopupCancelMs` it cancels (an Atari dialog's Cancel) — the loop back at the length it had when
  /// the popup opened (its one undo step undone), the popup closed.
  void popupPress(bool down, uint32_t ms);
  void cancelPopup();
  void servicePopup(uint32_t ms);
  /// The popup's choices: FULL (the whole stored loop), the ranges A and B, then lengths in bars
  /// from the stored loop's start.
  static constexpr uint8_t kChoiceFull = 0, kChoiceA = 1, kChoiceB = 2, kChoiceCount = 15, kNoChoice = 0xFF;
  static uint16_t choiceBars(uint8_t i);
  /// The choice the slot's loop is now (kNoChoice: none of them).
  uint8_t choiceOf(uint8_t slot);
  /// The next choice after the popup's (`back`: the one before), a range skipped while there is none; from none, by the
  /// loop's length.
  uint8_t nextChoice(uint8_t slot, bool back);
  void applyChoice(uint8_t slot, uint8_t i);
  /// One undo step a LOOP_EDIT visit (the steps exploded): a later change replaces the state the
  /// visit's last one made, while that is still the slot's current. Leaving LOOP_EDIT or switching track ends it.
  void visited(uint8_t slot) { visitState_ = e_->manifests().manifest(transport().contentOf(slot)).identity; }
  /// The internal tempo in tenths of a BPM.
  uint32_t tempoTenths() const;
  /// LOOP_EDIT's tempo, coarse and fine as NOTE_EDIT's position (the whole-range fader was too
  /// shallow to set exactly): F4 sets whole BPM, `kCoarseMinBpm`–`kCoarseMaxBpm` over its travel, and puts F3 back at
  /// its centre; F3 adds (CC 100 − 64) tenths. Not on an external clock, nor while a fader's motor lands.
  static uint16_t bpmOfWire(uint32_t wire);
  static uint16_t wireOfBpm(uint32_t bpm);
  void coarseTempo(int16_t pb, uint32_t ms);
  void fineTempo(uint8_t cc, uint32_t ms);
  void leaveEdit() {
    edit_.leave();
    loopEdit_ = false;
  }
  /// The track's shown slot: a queued switch's slot (the preview, the edit focus), else the playing one, else
  /// the selected one.
  uint8_t shownSlot(uint8_t t) {
    auto& x = transport();
    if (control().switchQueued(t)) return control().queuedSwitchSlot(t);
    return x.playing(t) ? x.playingSlot(t) : selectedSlot(t);
  }
  /// NOTE_EDIT on the shown slot (the playing or selected one), the window at the playhead.
  void enterEdit(uint8_t t) { enterEditOn(t, shownSlot(t)); }
  /// NOTE_EDIT on track `t`'s `slot` (copy mode: the slot a paste goes to).
  void enterEditOn(uint8_t t, uint8_t slot) {
    auto& x = transport();
    const ManifestId m = x.contentOf(slot);
    if (m == kNoManifest || x.writingSlot() == slot) return;
    const uint32_t loop = e_->manifests().manifest(m).loopLengthTicks;
    const uint32_t pos = x.playing(t) && x.playingSlot(t) == slot ? (tick_ - x.origin(t)) % loop : 0;
    edit_.enter(slot, loop, pos, nowMs_);
  }

  void toggleRecord(uint8_t t) {
    if (control().recordQueued(t)) {  // armed: start the transport, else cancel
      if (!clock_->running()) startClock();
      else control().cancelQueued(t);
      return;
    }
    if (transport().writingTrack() == t) {
      endTake(t);
      return;
    }
    const uint8_t slot = selectedSlot(t);
    if (empty(slot)) {
      slotToggle(t, slot_[t]);
      return;
    }
    if (transport().playing(t)) {
      control().startOverdub(t, tick_);
      return;
    }
    play(t, slot, Engine::Control::Quantize::NextGrid);
  }

  /// A step (0–15) or bar (17–24) button pressed: the selected track's playing slot restarts there at the
  /// next launch step — a 16th of the bar playing, or a bar of the 8-bar page playing.
  void stepPress(uint8_t note) {
    if (note == kBarStepBase - 1 || edit_.editing() || edit_.entering() || loopEdit_ || popup_ || browsing()) return;
    const uint8_t t = selected();
    const auto& x = transport();
    if (!x.playing(t) || x.writingTrack() == t) return;
    const ManifestId m = x.contentOf(x.playingSlot(t));
    const uint32_t loop = m == kNoManifest ? 0 : x.loopLength(m);
    if (loop == 0) return;
    const uint32_t pos = (tick_ - x.origin(t)) % loop;
    constexpr uint32_t kPage = 8 * kBar;
    // The buttons are absolute everywhere (DEC-283): a place in the stored loop — the 16th of its bar playing, the bar
    // of its 8-bar page playing — taken into the loop; outside the loop nothing; past the stored loop's end modulo the
    // loop.
    beyondEnd(m);
    const uint32_t at = (frameStart_ + pos) % frame_;
    const uint32_t place = note < kBarStepBase ? at / kBar * kBar + note * kSixteenth
                                               : at / kPage * kPage + (note - kBarStepBase) * kBar;
    const uint32_t target = place >= frame_ ? place % loop : (place + frame_ - frameStart_) % frame_;  // past it: modulo the loop
    if (target >= loop) return;  // a stored place outside the loop: nothing
    control().queueSeek(t, target, tick_);
  }
  /// A slot's double press: the filled slot plays at the next launch step, its arm or queue replaced.
  void launch(uint8_t t, uint8_t s) {
    const uint8_t slot = static_cast<uint8_t>(t * kSlotsPerTrack + s);
    const auto& x = transport();
    if (empty(slot) || x.writingTrack() == t) {
      slotToggle(t, s);
      return;
    }
    slot_[t] = s;
    control().cancelQueued(t);
    if (x.playing(t) && x.playingSlot(t) == slot) return;  // the playing slot: it stays, another one's queue dropped
    play(t, slot, Engine::Control::Quantize::NextGrid);
  }
  void slotToggle(uint8_t t, uint8_t s) {
    const uint8_t slot = static_cast<uint8_t>(t * kSlotsPerTrack + s);
    slot_[t] = s;
    const auto& x = transport();
    if (x.writingTrack() == t && x.writingSlot() != slot) {  // a take on another slot ends first
      endTake(t);
      if (empty(slot)) control().queueRecord(t, slot, tick_);
      else control().queueSwitch(t, slot, Engine::Control::Quantize::LoopEnd, tick_);
      return;
    }
    const uint8_t armed = control().queuedRecordSlot(t);
    if (armed != 0xFF && armed != slot) control().cancelQueued(t);
    if (x.writingTrack() == t) {
      endTake(t);
      return;
    }
    if (empty(slot)) {
      if (control().queuedRecordSlot(t) != slot) {
        control().queueRecord(t, slot, tick_);  // armed: at the playing slot's loop start, the bar, or the transport
      } else if (x.playing(t)) {
        control().punchIn(t, tick_, us_, us());  // a second press on a playing track
      } else if (!clock_->running()) {
        startClock();  // a second press on a stopped transport: it starts, and the take with it
      } else {
        control().cancelQueued(t);
      }
      return;
    }
    if (x.playing(t)) {
      if (x.playingSlot(t) == slot && control().queuedSwitchSlot(t) == 0xFF)
        control().setMute(t, !control().muted(t), us_);
      else
        control().queueSwitch(t, slot, Engine::Control::Quantize::LoopEnd, tick_);
      return;
    }
    if (!clock_->running()) return;  // from stopped a press selects; the next start plays it
    play(t, slot, Engine::Control::Quantize::NextGrid);
  }

  // ---- LEDs -----------------------------------------------------------------------------------------------------
  void desired(uint32_t lastTick, uint32_t ms, uint8_t* want);

  Engine* e_ = nullptr;
  EngineClock* clock_ = nullptr;
  PressDetector<kButtons> press_;
  NoteEditSurface<Engine> edit_;
  uint8_t slot_[kTracks] = {};
  uint8_t lockStep_ = 0xFF;  // NOTE_EDIT: the step held as a parameter lock (0xFF: none)
  uint8_t barsHeld_ = 0;                      // NOTE_EDIT: the bar buttons held (bit per bar)
  uint16_t rangeSteps_ = 0, rangeBars_ = 0;   // LOOP_EDIT: the step / bar buttons held
  uint32_t beyondIdentity_ = 0;                // the state `beyondEnd` last scanned
  uint32_t rowScans_ = 0;                      // full scans of a state's rows for the LEDs (`rowScans`)
  uint64_t beyondBars_ = 0;                    // ... its bars with notes past the loop's end
  uint16_t beyondSteps_ = 0;                   // ... the 16ths of the bar holding its end
  uint32_t storedEnd_ = 0;                     // ... the end of its stored notes (ticks)
  uint32_t frame_ = 0, frameStart_ = 0;        // ... the stored loop's length and the loop's start in it
  uint32_t absEnd_ = 0;                        // ... how far its notes reach from the stored loop's start (whole bars)
  uint64_t absBars_ = 0;                       // ... the stored loop's bars holding a NoteOn
  uint32_t stepsIdentity_ = 0, stepsBar_ = 0;  // `absSteps`: the state and stored bar last scanned
  uint16_t steps_ = 0;                         // ... its 16ths holding a NoteOn
  // The stored loop's first 64 bars: each one's 16ths holding a NoteOn, built by `beyondEnd`'s scan, so the step LEDs
  // need no row scan when the playhead enters a bar (the LED stage over B_max at 300 BPM).
  uint16_t barSteps_[64] = {};
  // `beyondScan`'s state between passes: the state it scans, where it is, and the values it builds (published at its
  // end).
  struct Scan {
    uint32_t identity = 0, loop = 0, run = 0, row = 0, storedEnd = 0, frame = 0, frameStart = 0, absEnd = 0;
    uint64_t beyondBars = 0, absBars = 0;
    uint16_t beyondSteps = 0;
    uint16_t barSteps[64] = {};
    uint8_t phase = 0, played = 0;  // phase 0: none, 1: the stored reach, 2: the bars from the stored loop's start
    bool fit = false;
  };
  Scan scan_;
  // Rows a pass for the LEDs (the screen: half): 256 + 128 keep a pass near 200 µs at 30 overdub layers; a new
  // state's scan takes a few more passes (DEC-297, deep-overdub timing).
  static constexpr uint32_t kScanRows = 256;
  uint8_t stepAnchor_ = 0, barAnchor_ = 0;    // ... the first of them
  uint8_t rangeTapNote_ = 0xFF;                // ... the last one pressed alone, and when (a double: one bar / 16th)
  uint32_t rangeTapMs_ = 0;
  uint32_t barDownMs_[8] = {};                // ... since
  Copy copy_ = Copy::None;
  uint8_t copySlot_ = 0;       // Slot: the source's engine slot
  uint32_t copyBar_ = 0;       // Bar: the source bar's start
  int16_t copyNote_ = -1;      // Note: the source note's index
  uint8_t copyEditSlot_ = 0;   // Bar / Note: the slot edited when copied (its source clear and cancel only there)
  uint32_t noteBar_ = 0;       // Note: the bar the step buttons paste in (a bar button chooses it, past the loop too)
  struct Pending {
    bool active = false, merge = false;
    uint32_t at = 0;  // where: a bar's or a 16th's start
    uint8_t led = 0;
  };
  Pending pending_;            // a paste past the loop's end, after the loop grew and NOTE_EDIT re-gathered
  uint8_t copyButton_ = 0;     // the source's slot (0–7), bar (0–7) or step (0–15)
  bool copyHolding_ = false;   // the source is still held (its release keeps copy mode)
  uint8_t copyTrack_ = 0;      // the track selected last (a switch restarts the countdown)
  uint32_t copyUntil_ = 0;     // the countdown's end (from the source's release)
  uint8_t sourceTaps_ = 0;     // the source pressed again: 1, waiting for a second (a double clears it)
  uint8_t targetTaps_ = 0, targetButton_ = 0;  // a bar / step target pressed once: waiting for a second (merge)
  uint32_t targetMs_ = 0;
  PasteMode pasteMode_ = PasteMode::Ask;
  uint8_t exitButton_ = 0xFF;  // the button whose hold ended copy mode (none starts it until released)
  uint8_t exitBars_ = 0;       // ... a bar's
  uint32_t sourceTapMs_ = 0;
  uint8_t flashLed_ = 0xFF, flashTrack_ = 0;  // a paste's LED (channel 15 note) flashing three times, on that track
  uint32_t flashMs_ = 0;
  typename NoteEditSurface<Engine>::Nudge encoderOption_ = NoteEditSurface<Engine>::Nudge::Pitch;
  uint8_t encoderStep_ = 0xFF;  // the step held when the encoder was pushed (0xFF: none)
  uint32_t encoderMs_ = 0;      // the last turn (the acceleration's interval)
  /// Starts copy mode from a held source, ends it on a long press of any other button, a source lock that changed its
  /// note, or NOTE_EDIT left.
  void serviceCopy(uint32_t ms);
  /// Another slot / step held in copy mode: it becomes the source — copy mode starts again from it.
  /// False: the source itself (on its own track), or a step with no note (the hold then ends copy mode).
  bool reSource(uint8_t b, uint32_t ms);
  /// Copy mode on bar `bar` of the edited slot (its notes captured as values; too many: no copy mode).
  void beginBarCopy(uint8_t bar, uint32_t ms);
  void beginCopy(Copy c, uint8_t button, uint32_t ms);
  void endCopy();
  /// The source's press-detector button (slots and steps; 0xFF for a bar).
  uint8_t sourceButton() const;
  /// LOAD LOOP (copy mode on a slot: TRACK switch held, or the source held again): the get-a-loop picker for
  /// the source slot; copy mode ends.
  void loadLoop();
  /// A slot (slot copy) or a step (note copy) button: a paste target.
  bool copyTarget(uint8_t b) const;
  /// The release of `button` of kind `c`: the source's while still held (copy mode stays: true), or the source pressed
  /// again (copy mode ends: true). False: a target.
  bool copyReleased(Copy c, uint8_t button, uint32_t ms);
  /// A bar / step target pressed: pasted at once in the mode REC / EDIT chose; else a second press
  /// within the double window merges, none replaces (`serviceCopy`).
  void targetPress(uint8_t button, uint32_t ms);
  /// The paste onto bar / step `button`: replacing what is there, or merged with it. Refused: copy mode stays.
  void pasteTo(uint8_t button, bool merge, uint32_t ms);
  /// The paste waiting for NOTE_EDIT to re-gather after the loop grew (`serviceCopy`).
  void pendingPaste(uint32_t ms);
  /// A double press of the source: the slot cleared, or the bar's / the source note's 16th's notes.
  void clearSource(uint32_t ms);
  void paste(uint8_t b, uint32_t ms);
  void flash(uint8_t led, uint8_t track, uint32_t ms);
  /// Copy mode's LEDs over the rest: the source pulsing (a slot on its own track), a paste's target flashing three
  /// times.
  void copyLeds(uint32_t ms, uint8_t* want);
  /// The bars (and the 16ths of the bar holding the loop's end) with a NoteOn stored at or past the loop's end, for a
  /// resized state (one with a fit run): its content rows scanned once a state. A take's notes played past
  /// its bar line count too.
  // Stored notes outside the loop at the level an empty bar had (the lowest the DROID shows: 8 and 20 are not
  // visible), the loop's bars above it.
  static constexpr uint8_t kBeyondLed = 32, kLoopEmptyLed = 48, kLoopNotesLed = 96;
  /// The stored loop of state `m`: its notes past the loop's end, its frame and the bars and 16ths
  /// holding notes — scanned to the end now (gestures need the exact values).
  void beyondEnd(ManifestId m) { beyondScan(m, 0xFFFFFFFFu); }
  /// The same in steps of at most `budget` rows: a large state scanned in one main-loop pass puts the LED stage over
  /// B_max (DEC-297, deep-overdub timing). The LEDs and the screen advance it each pass and show the last state
  /// scanned until the new one is done; a scan restarts when its state changes. True: `m`'s values are in.
  bool beyondScan(ManifestId m, uint32_t budget);
  /// The first played run of state `d`: the groove pass when there is one (the runs before it are its
  /// sources, not played), else 0.
  template <typename D>
  uint8_t playedFrom(const D& d);
  /// The 16ths of stored bar `bar` (counted from the stored loop's start) holding a NoteOn, for state `m`.
  uint16_t absSteps(ManifestId m, uint32_t bar);
  /// LOOP_EDIT's bar and step LEDs: the stored loop's 8-bar page holding the playhead (stopped: the loop's start) and
  /// the 16ths of its bar — in the loop lit (notes brighter), outside it dark, its notes there dim; the playhead full.
  void loopEditLeds(ManifestId m, uint32_t loop, uint32_t pos, bool moving, uint8_t* want);
  /// The step button held in NOTE_EDIT (the first; 0xFF: none).
  uint8_t heldStep() const;
  uint8_t sent_[128] = {};
  uint8_t bankSent_ = 0xFF;  // the DROID fader bank last sent (0xFF: none yet)
  bool loopEdit_ = false;    // LOOP_EDIT: the edit button's other mode
  ProductionBrowser* browser_ = nullptr;  // the sets browser
  struct LoopMotor {
    typename NoteEditSurface<Engine>::MotorMsg m;
    uint32_t at;  // due
  };
  static constexpr uint8_t kLoopMotorQueue = 12;  // four faders, three messages each
  LoopMotor loopMotor_[kLoopMotorQueue];
  uint8_t loopMotorCount_ = 0;
  uint32_t loopWrittenMs_[4] = {};  // F1–F4: last motor write
  bool loopPark_ = false;           // LOOP_EDIT entered: every fader to its value
  int32_t parkedStart_ = -1;        // F1's parked 16th, F2's parked notch: their echo is no move (-1: moved since)
  int16_t parkedNotch_ = -1;
  uint16_t coarseBpm_ = 120;        // F4's whole BPM
  uint32_t pendingMs_ = 0;
  uint32_t visitState_ = 0;  // identity of the state this LOOP_EDIT visit's last change made (0: none)
  uint32_t pendingLoop_ = 0;  // LOOP_EDIT's F2 moved: the length it will take (ticks)
  bool popup_ = false, popupHeld_ = false;  // the length popup; its button is down
  uint8_t popupSlot_ = 0, popupButton_ = 0, popupTaps_ = 0;
  uint8_t popupChoice_ = 0xFF;  // the popup's selected choice
  uint8_t grooveSlot_ = 0xFF;   // the slot the groove job was last asked for
  bool groove_ = false, groovePending_ = false;  // the GROOVE window open; a change settling
  bool grooveChord_ = false;                     // PLAY/STOP pressed with NOTELEN held: its release taken too
  bool trackMute_ = false;                       // the track buttons mute (short press) instead of select
  uint8_t grooveWin_ = 0, grooveField_ = 0;      // its slot, its selected field
  groove::Settings grooveSet_;                   // B
  groove::Settings grooveA_;                     // A: the slot's groove when the window opened, then edited too
  groove::Table grooveATable_;                   // ... its file template, if one
  char grooveALabel_[21] = {};
  Heard heard_ = Heard::B;                       // the last choice (NOTELEN), outside a cycle
  bool cycling_ = false;                         // A and B taking turns
  bool grooveAuto_ = false;                      // AUTO Q, the slot's (the title bar's checkbox)
  uint8_t cycleBars_ = 2;                        // the cycle's bars a turn: global, 2 by default
  Heard pendingCol_ = Heard::B;                  // the one a settling change is to
  uint8_t faderSeen_ = 0;
  // The columns to store at closing ("save B with the slot"; `closeGroove`, `writeColumns`).
  bool columnsOwed_ = false;
  uint8_t columnsSlot_ = 0, columnsHeard_ = 0;
  uint32_t columnsA_ = 0, columnsB_ = 0;
  uint32_t openedA_ = 0, openedB_ = 0;
  uint8_t openedHeard_ = 0;                        // the GROOVE bank's faders reported since the window opened (bit f)
  uint32_t cycleStart_ = 0, cycleAsked_ = 0;     // the cycle's first bar line; the turn last asked for
  uint32_t grooveAt_ = 0;                        // a turn's bar line its build waits for (0: none) — asked within the
                                                 // beat before it, so the next beat is that bar line
  uint32_t groovePendingMs_ = 0, autoTried_ = 0;
  uint32_t grooveSwitchFrom_ = 0, grooveSwitchAt_ = 0;  // the last publish's tick and the beat it is heard on
  uint32_t grooveHeardMs_ = 0;                   // when the last one was first heard (the dots' bright)
  bool grooveToHear_ = false;                    // published, its switch tick not reached yet
  uint32_t popupUntil_ = 0, popupTapMs_ = 0, popupDownMs_ = 0;
  uint32_t startMs_ = 0, pendingStart_ = 0;
  bool startPending_ = false;
  uint32_t tick_ = 0, us_ = 0, nowMs_ = 0, blinkMs_ = 0;
};

}  // namespace ContentEngine

#include "ContentEngine/ControlSurface/ControlSurfaceGroove.h"
#include "ContentEngine/ControlSurface/ControlSurfaceLoopEdit.h"
#include "ContentEngine/ControlSurface/ControlSurfaceCopy.h"
#include "ContentEngine/ControlSurface/ControlSurfaceLeds.h"
