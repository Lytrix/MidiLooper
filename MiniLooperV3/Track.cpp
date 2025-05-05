#include "Globals.h"
#include "Track.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "Logger.h"
#include "TrackStateMachine.h"
#include <algorithm>  // for std::sort

// Track class implementation

Track::Track()
  : isPlayingBack(false),
    muted(false),
    trackState(TRACK_EMPTY),
    startLoopTick(0),
    loopLengthTicks(0),
    lastTickInLoop(0),
    pendingNotes(),
    midiEvents(),
    noteEvents()
 {}

const std::vector<MidiEvent>& Track::getEvents() const {
  return midiEvents;
}

const std::vector<NoteEvent>& Track::getNoteEvents() const {
  return noteEvents;
}

uint32_t Track::getStartLoopTick() const {
  return startLoopTick;
}

TrackState Track::getState() const {
  return trackState;
}

bool Track::isValidStateTransition(TrackState newState) const {
  return TrackStateMachine::isValidTransition(trackState, newState);
}

bool Track::setState(TrackState newState) {
  if (!TrackStateMachine::isValidTransition(trackState, newState)) {
    logger.log(CAT_STATE, LOG_WARNING, "Invalid state transition from %s to %s",
               TrackStateMachine::toString(trackState),
               TrackStateMachine::toString(newState));
    return false;
  }
  return transitionState(newState);
}

const char* Track::getStateName(TrackState state) {
  return TrackStateMachine::toString(state);
}

bool Track::transitionState(TrackState newState) {
  if (!TrackStateMachine::isValidTransition(trackState, newState)) {
    return false;
  }

  TrackState oldState = trackState;
  trackState = newState;

  logger.logStateTransition("Track", TrackStateMachine::toString(oldState), TrackStateMachine::toString(newState));
  return true;
}

void Track::startRecording(uint32_t currentTick) {
  if (!setState(TRACK_RECORDING)) {
    return;
  }
  // Clear out any old data
  midiEvents.clear();
  noteEvents.clear();         // for your piano-roll / display
  pendingNotes.clear();       // any hanging NoteOns
  nextEventIndex = 0;         // so playback will start from the top
  lastTickInLoop = 0;

  // Stamp the new start tick
  startLoopTick = currentTick;
  logger.logTrackEvent("Recording started", currentTick);
}

// -------------------------
// Helpers for stopRecording 
// -------------------------

static constexpr float  END_GRACE_FRACTION = 0.125f;  // 1/8 bar
static constexpr float  START_GRACE_FRACTION = 0.125f;

uint32_t Track::quantizeStart(uint32_t original) const {
    return (original / ticksPerBar) * ticksPerBar;
}

void Track::shiftMidiEvents(int32_t offset) {
    for (auto &evt : midiEvents) {
        evt.tick += offset;
    }
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](auto &a, auto &b){ return a.tick < b.tick; });
}

uint32_t Track::findLastEventTick() const {
    uint32_t last = 0;
    for (auto &evt : midiEvents) {
        last = std::max(last, evt.tick);
    }
    return last;
}

uint32_t Track::computeLoopLengthTicks(uint32_t lastTick) const {
    // How many full bars your last event has fully passed
    uint32_t fullBars = lastTick / ticksPerBar;
    // How far into the next bar your last event lands
    uint32_t rem      = lastTick % ticksPerBar;
    // A small “grace” (e.g. 1/8 bar) to allow tiny overshoots without padding
    uint32_t grace    = ticksPerBar / 8;

    // If you’ve only crept a little into the next bar, stay in the current one:
    if (rem <= grace) {
        // If nothing at all, ensure at least one bar:
        return (fullBars > 0 ? fullBars : 1) * ticksPerBar;
    }
    // Otherwise include the bar containing your last event:
    return (fullBars + 1) * ticksPerBar;
}

void Track::finalizePendingNotes(uint32_t offAbsTick) {
    // Temporarily pretend we're still recording so noteOff() will queue things
    TrackState prev = trackState;
    trackState = TRACK_RECORDING;

    // 1) Copy out the pending keys
    std::vector<std::pair<uint8_t,uint8_t>> toClose;
    toClose.reserve(pendingNotes.size());
    for (auto const &kv : pendingNotes) {
        toClose.push_back(kv.first);
    }

    // 2) Emit a noteOff() for each key (this will record the NoteEvent
    //    but no longer erase inside the map)
    for (auto const &key : toClose) {
        uint8_t note    = key.first;
        uint8_t channel = key.second;
        noteOff(channel, note, 0, offAbsTick);
    }

    // 3) Now safely clear all remaining pending notes
    pendingNotes.clear();

    // Restore the real state
    trackState = prev;
}


// The stopRecording 

void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  // 1) Quantize & shift as before
  uint32_t origStart = startLoopTick;
  uint32_t qStart    = quantizeStart(origStart);
  int32_t  offset    = int32_t(origStart) - int32_t(qStart);
  shiftMidiEvents(offset);
  startLoopTick      = qStart;

  // 2) Close out any still-held notes
  finalizePendingNotes(currentTick);

  // 3) Find the last event tick (including the forced NoteOffs)
  uint32_t lastTick = findLastEventTick();

  // 4) Compute loopLengthTicks with your grace logic
  loopLengthTicks = computeLoopLengthTicks(lastTick);

  logger.logTrackEvent("Recording stopped (with forced NoteOffs)",
                       currentTick,
                       "start=%lu length=%lu",
                       startLoopTick, loopLengthTicks);

  setState(TRACK_PLAYING);
}

void Track::startPlaying(uint32_t currentTick) {
  if (loopLengthTicks > 0) {
    if (!setState(TRACK_PLAYING)) {
      return;
    }
    startLoopTick = currentTick - ((currentTick - startLoopTick) % loopLengthTicks);
    logger.logTrackEvent("Playback started", currentTick);
  }
}

void Track::startOverdubbing(uint32_t currentTick) {
  if (!setState(TRACK_OVERDUBBING)) {
    return;
  }

  // Sore midiNotes and noteEvents in dedicated History buffer
  _midiHistory .push_back(midiEvents);
  _noteHistory .push_back(noteEvents);
  _hasNewEventsSinceSnapshot = false;            // no new events yet

  logger.logTrackEvent("Overdubbing started", currentTick);
}

void Track::stopOverdubbing(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_OVERDUBBING)) {
    return;
  }
  logger.logTrackEvent("Overdubbing stopped", currentTick);
}

void Track::stopPlaying() {
  // first kill all sounding notes
  sendAllNotesOff();

  // then transition to the stopped state
  setState(TRACK_STOPPED);
  logger.logTrackEvent("Playback stopped", clockManager.getCurrentTick());
}

void Track::togglePlayStop() {
  isPlaying() ? stopPlaying() : startPlaying(clockManager.getCurrentTick());
}

void Track::toggleMuteTrack() {
  muted = !muted;
}

bool Track::isMuted() const {
  return muted;
}

bool Track::hasData() const {
  return !midiEvents.empty();
}

size_t Track::getMidiEventCount() const {
  return midiEvents.size();
}

size_t Track::getNoteEventCount() const {
  return noteEvents.size();
}

// Clears the current track data and returns to the EMPTY state
void Track::clear() {
    if (trackState == TRACK_EMPTY) {
        logger.debug("Track already empty; ignoring clear");
        return;
    }

    // Remove all recorded events
    midiEvents.clear();
    noteEvents.clear();

    // Reset timing
    startLoopTick = 0;
    loopLengthTicks = 0;

    // Go back to "never recorded"
    setState(TRACK_EMPTY);

    // Log the clear action
    logger.logTrackEvent("Track cleared", clockManager.getCurrentTick());
}

bool Track::canUndoOverdub() const {
  // must have at least one snapshot, no new events since, and be playing or overdubbing
  return !_midiHistory.empty();
}

void Track::undoOverdub() {
  if (!canUndoOverdub()) {
    logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo overdub right now");
    return;
  }

  // 1. Restore the last-snapshot
  midiEvents = std::move(_midiHistory.back());
  noteEvents = std::move(_noteHistory.back());
  _midiHistory.pop_back();
  _noteHistory.pop_back();

  // // 2. Recompute your loop length
  // uint32_t lastTick      = findLastEventTick();          // your helper
  // loopLengthTicks        = computeLoopLengthTicks(lastTick);

  // 3. Reset the “new‐event” flag so you can undo again if you never record more
  _hasNewEventsSinceSnapshot = false;

  // 4. Log it (state stays PLAYING or OVERDUBBING as-is)
  logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
}



void Track::recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((isRecording() && !isPlaying()) || isOverdubbing()) {
    uint32_t tickRelative;

    // First pass: build the loop
    if (isRecording() && !isPlaying()) {
      tickRelative = currentTick - startLoopTick;

    // Overdub passes: wrap every hit back into the loop
    } else if (isOverdubbing()) {
      // defensive: loopLengthTicks was set when you stopped recording
      if (loopLengthTicks == 0) return;
      tickRelative = (currentTick - startLoopTick) % loopLengthTicks;

    } else {
      return;
    }

    // Prevent duplicate midiEvents at the same tick with same parameters
    for (const auto& e : midiEvents) {
      if (e.tick == tickRelative && e.type == type && e.channel == channel && e.data1 == data1 && e.data2 == data2) {
        return;  // Skip duplicate event
      }
    }

    logger.logMidiEvent(
      type == midi::NoteOn ? "NoteOn" : 
      type == midi::NoteOff ? "NoteOff" : 
      type == midi::ControlChange ? "ControlChange" : "Other",
      channel, data1, data2
    );

    midiEvents.push_back(MidiEvent{ tickRelative, type, channel, data1, data2 });
    // and anywhere you actually record a new event (e.g. in recordMidiEvents or noteOn/noteOff):
    _hasNewEventsSinceSnapshot = true;
    
     // **Keep events sorted by tick** so playback scanning never misses a wrapped-back note during overdubbing
    std::sort(midiEvents.begin(), midiEvents.end(),
            [](auto &a, auto &b){ return a.tick < b.tick; });

  }
}

void Track::playMidiEvents(uint32_t currentTick, bool isAudible) {
  if (!isAudible || muted || midiEvents.empty() || loopLengthTicks == 0)
    return;

  // Compute position in the loop
  uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

  // Reset at loop boundary
  if (tickInLoop < lastTickInLoop) {
    nextEventIndex = 0;
    logger.debug("Loop wrapped, resetting index");
    // optional: dump events again here
  }

  // Remember where we were
  uint32_t prevTickInLoop = lastTickInLoop;
  lastTickInLoop = tickInLoop;

  // Now send anything whose tick falls in (prevTickInLoop, tickInLoop]
  while (nextEventIndex < midiEvents.size()) {
    const MidiEvent &evt = midiEvents[nextEventIndex];
    uint32_t evTick = evt.tick % loopLengthTicks;

    logger.debug("Checking evt[%d] → evTick=%lu  prev=%lu…%lu",
                 nextEventIndex, evTick, prevTickInLoop, tickInLoop);

    // If this event just *now* should fire:
    if ( prevTickInLoop < evTick && evTick <= tickInLoop ) {
      sendMidiEvent(evt);
      nextEventIndex++;
    }
    // If it’s still coming later in this pass, bail out:
    else if (evTick > tickInLoop) {
      break;
    }
    // Otherwise we missed it entirely—skip it so we don't hang:
    else {
      nextEventIndex++;
    }
  }
}

void Track::sendMidiEvent(const MidiEvent& evt) {
  if (trackState != TRACK_PLAYING && trackState != TRACK_OVERDUBBING) return;

  isPlayingBack = true;  // Mark playback so noteOn/noteOff ignores it

  const char* typeStr;

  switch (evt.type) {
    case midi::NoteOn:
      typeStr = "NoteOn";
      break;
    case midi::NoteOff:
      typeStr = "NoteOff";
      break;
    case midi::ControlChange:
      typeStr = "ControlChange";
      break;
    case midi::ProgramChange:
      typeStr = "Program Change";
      break;
    default:
      typeStr = "Other";
      break;
  }

  logger.logMidiEvent(typeStr, evt.channel, evt.data1, evt.data2);

  switch (evt.type) {
    case midi::NoteOn:
      midiHandler.sendNoteOn(evt.channel, evt.data1, evt.data2);
      break;
    case midi::NoteOff:
      midiHandler.sendNoteOff(evt.channel, evt.data1, evt.data2);
      break;
    case midi::ControlChange:
      midiHandler.sendControlChange(evt.channel, evt.data1, evt.data2);
      break;
    case midi::PitchBend:
      midiHandler.sendPitchBend(evt.channel, (evt.data2 << 7) | evt.data1);
      break;
    case midi::AfterTouchChannel:
      midiHandler.sendAfterTouch(evt.channel, evt.data1);
      break;
    case midi::ProgramChange:
      midiHandler.sendProgramChange(evt.channel, evt.data1);
      break;
    case midi::Clock:
    case midi::Start:
    case midi::Stop:
    // Clock events might be handled by your ClockManager, so skip them here.
      break;
    case midi::InvalidType:
      logger.log(CAT_MIDI, LOG_WARNING, "Invalid Type: data1=%d, data2=%d", evt.data1, evt.data2);
      break;
    default:
      logger.log(CAT_MIDI, LOG_INFO, "Unhandled MIDI type: %d (ch=%d, d1=%d, d2=%d)",
                 evt.type, evt.channel, evt.data1, evt.data2);
      break;
  }

  isPlayingBack = false;  // Reset playback state
}

void Track::sendAllNotesOff() {
  // Control Change 123 = All Notes Off
  for (uint8_t ch = 0; ch < 16; ++ch) {
    midiHandler.sendControlChange(ch, 123, 0);
  }
  // also clear any half-open pending notes so they don't get forced later
  pendingNotes.clear();
  logger.logTrackEvent("All Notes Off sent", clockManager.getCurrentTick());
}

bool Track::isEmpty() const{
  return trackState == TRACK_EMPTY;
}

bool Track::isStopped() const {
  return trackState == TRACK_STOPPED;
}

bool Track::isArmed() const {
  return trackState == TRACK_ARMED;
}

bool Track::isRecording() const {
  return trackState == TRACK_RECORDING;
}

bool Track::isStoppedRecording() const {
  return trackState == TRACK_STOPPED_RECORDING;
}

bool Track::isOverdubbing() const {
  return trackState == TRACK_OVERDUBBING;
}

bool Track::isStoppedOverdubbing() const {
  return trackState == TRACK_STOPPED_OVERDUBBING;
}

bool Track::isPlaying() const {
  return trackState == TRACK_PLAYING;
}

uint32_t Track::getLength() const {
  return loopLengthTicks;
}

void Track::setLength(uint32_t ticks) {
  loopLengthTicks = ticks;
}

// Display functions
void Track::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered midiEvents

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    // Now store note & channel in the PendingNote struct
    pendingNotes[{note, channel}] = PendingNote{
      /* note: */        note,
      /* channel: */     channel,
      /* startNoteTick:*/ tick,
      /* velocity: */    velocity
    };
    recordMidiEvents(midi::NoteOn, channel, note, velocity, tick);
  }
}

void Track::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered midiEvents

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    auto key = std::make_pair(note, channel);
    auto it = pendingNotes.find(key);
    if (it != pendingNotes.end()) {
      recordMidiEvents(midi::NoteOff, channel, note, 0, tick);  // velocity 0 to mark end

      // Store final note event with previous startNoteTick from Note On information
     // Now pull startTick & velocity from the PendingNote
      NoteEvent noteEvent {
        /* .note = */           note,
        /* .velocity = */       it->second.velocity,
        /* .startNoteTick = */  it->second.startNoteTick,
        /* .endNoteTick = */    tick
      };
      noteEvents.push_back(noteEvent);

      // **Keep them sorted by start time to ensure proper rending on the LCD when overdubbing**
      std::sort(noteEvents.begin(), noteEvents.end(),
                [](auto &a, auto &b){ return a.startNoteTick < b.startNoteTick; });

      pendingNotes.erase(it);

     if (DEBUG_MIDI) {
        logger.log(CAT_TRACK, LOG_DEBUG, "Record NoteOff: %d @ %lu", note, tick);
      }
      if (DEBUG_NOTES) {
        logger.log(CAT_TRACK, LOG_DEBUG,
                   "NoteEvent start=%lu end=%lu",
                   noteEvent.startNoteTick,
                   noteEvent.endNoteTick);
      }
    } else {
      // Log unmatched NoteOffs
      logger.log(CAT_MIDI, LOG_WARNING,
                 "NoteOff for note %d on ch %d with no matching NoteOn",
                 note, channel);
    }
  }
}

void Track::printNoteEvents() const {
  Serial.println("---- NoteEvents ----");
  for (const auto& note : noteEvents) {
    Serial.print("Note: ");
    Serial.print(note.note);
    Serial.print("  Velocity: ");
    Serial.print(note.velocity);
    Serial.print("  StartTick: ");
    Serial.print(note.startNoteTick);
    Serial.print("  EndTick: ");
    Serial.println(note.endNoteTick);
  }
  Serial.println("---------------------");
}
