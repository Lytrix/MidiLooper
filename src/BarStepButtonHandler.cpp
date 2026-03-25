//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "BarStepButtonHandler.h"
#include "Globals.h"
#include "MidiConfig.h"
#include "Logger.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "NoteEditManager.h"
#include "LoopEditManager.h"
#include "EditManager.h"
#include "TrackUndo.h"
#include "Utils/PressTiming.h"
#include <cstdarg>

extern NoteEditManager noteEditManager;
extern ClockManager clockManager;
extern TrackManager trackManager;

BarStepButtonHandler barStepButtonHandler;

BarStepButtonHandler::BarStepButtonHandler()
  : testLoggingEnabled(false),
    doubleTapWindow(PressTiming::DOUBLE_TAP_WINDOW),
    tripleTapWindow(PressTiming::TRIPLE_TAP_WINDOW),
    longPressTime(PressTiming::LONG_PRESS_TIME),
    holdTwoMinGap(PressTiming::HOLD_TWO_MIN_GAP_MS),
    pressed16thBits(0),
    pressedBarBits(0),
    lastBarNoteOnTime(0),
    last16thNoteOnTime(0),
    selectedBarIndex(0),
    isHoldTwoJam(false) {
  for (size_t i = 0; i < MAX_BUTTONS; i++) {
    buttonStates[i] = ButtonState{};
  }
}

void BarStepButtonHandler::setup() {
  testLog("BarStepButtonHandler setup - notes 0-15 (16th), 17-24 (bar), ch%d",
          MidiConfig::BarStepButton::CHANNEL);
}

void BarStepButtonHandler::update() {
  processPendingPresses();
}

uint8_t BarStepButtonHandler::pressed16thCount() const {
  uint8_t c = 0;
  for (uint8_t i = 0; i < 16; i++) {
    if (pressed16thBits & (1u << i)) c++;
  }
  return c;
}

uint8_t BarStepButtonHandler::pressedBarCount() const {
  uint8_t c = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (pressedBarBits & (1u << i)) c++;
  }
  return c;
}

uint8_t BarStepButtonHandler::getMinPressed16th() const {
  for (uint8_t i = 0; i < 16; i++) {
    if (pressed16thBits & (1u << i)) return i;
  }
  return 255;
}

uint8_t BarStepButtonHandler::getMaxPressed16th() const {
  for (int8_t i = 15; i >= 0; i--) {
    if (pressed16thBits & (1u << i)) return (uint8_t)i;
  }
  return 255;
}

uint8_t BarStepButtonHandler::getMinPressedBar() const {
  for (uint8_t i = 0; i < 8; i++) {
    if (pressedBarBits & (1u << i)) return i;
  }
  return 255;
}

uint8_t BarStepButtonHandler::getMaxPressedBar() const {
  for (int8_t i = 7; i >= 0; i--) {
    if (pressedBarBits & (1u << i)) return (uint8_t)i;
  }
  return 255;
}

uint32_t BarStepButtonHandler::getMinPressStartTimeForType(BarStepButtonType type) const {
  uint32_t minStart = 0xFFFFFFFFu;
  if (type == BarStepButtonType::SIXTEENTH) {
    for (uint8_t i = 0; i < 16; i++) {
      if (pressed16thBits & (1u << i)) {
        uint32_t t = buttonStates[i].pressStartTime;
        if (t < minStart) minStart = t;
      }
    }
  } else {
    for (uint8_t i = 0; i < 8; i++) {
      if (pressedBarBits & (1u << i)) {
        uint32_t t = buttonStates[16 + i].pressStartTime;
        if (t < minStart) minStart = t;
      }
    }
  }
  return minStart;
}

uint32_t BarStepButtonHandler::getMaxPressStartTimeForType(BarStepButtonType type) const {
  uint32_t maxStart = 0;
  if (type == BarStepButtonType::SIXTEENTH) {
    for (uint8_t i = 0; i < 16; i++) {
      if (pressed16thBits & (1u << i)) {
        uint32_t t = buttonStates[i].pressStartTime;
        if (t > maxStart) maxStart = t;
      }
    }
  } else {
    for (uint8_t i = 0; i < 8; i++) {
      if (pressedBarBits & (1u << i)) {
        uint32_t t = buttonStates[16 + i].pressStartTime;
        if (t > maxStart) maxStart = t;
      }
    }
  }
  return maxStart;
}

bool BarStepButtonHandler::isBarStepButtonNote(uint8_t channel, uint8_t note) const {
  if (channel != MidiConfig::BarStepButton::CHANNEL) return false;
  if (note >= MidiConfig::BarStepButton::SIXTEENTH_BASE &&
      note < MidiConfig::BarStepButton::SIXTEENTH_BASE + MidiConfig::BarStepButton::SIXTEENTH_COUNT) {
    return true;
  }
  if (note >= MidiConfig::BarStepButton::BAR_BASE &&
      note < MidiConfig::BarStepButton::BAR_BASE + MidiConfig::BarStepButton::BAR_COUNT) {
    return true;
  }
  return false;
}

BarStepButtonInfo BarStepButtonHandler::parseNote(uint8_t note) const {
  BarStepButtonInfo info = {BarStepButtonType::SIXTEENTH, 0, note};
  if (note >= MidiConfig::BarStepButton::SIXTEENTH_BASE &&
      note < MidiConfig::BarStepButton::SIXTEENTH_BASE + MidiConfig::BarStepButton::SIXTEENTH_COUNT) {
    info.type = BarStepButtonType::SIXTEENTH;
    info.stepIndex = note - MidiConfig::BarStepButton::SIXTEENTH_BASE;
  } else if (note >= MidiConfig::BarStepButton::BAR_BASE &&
             note < MidiConfig::BarStepButton::BAR_BASE + MidiConfig::BarStepButton::BAR_COUNT) {
    info.type = BarStepButtonType::BAR;
    info.stepIndex = note - MidiConfig::BarStepButton::BAR_BASE;
  }
  return info;
}

size_t BarStepButtonHandler::getButtonIndex(uint8_t note) const {
  if (note < 16) return note;
  if (note >= 17 && note <= 24) return 16 + (note - 17);
  return 0;
}

void BarStepButtonHandler::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
  if (!isBarStepButtonNote(channel, note)) return;

  testLog("BarStepButton: ch=%d note=%d %s vel=%d [TEST POINT: note received]",
          channel, note, isNoteOn ? "ON" : "OFF", velocity);

  if (isNoteOn && velocity > 0) {
    handleNoteOn(note, velocity);
  } else {
    handleNoteOff(note, velocity);
  }
}

void BarStepButtonHandler::handleNoteOn(uint8_t note, uint8_t velocity) {
  BarStepButtonInfo info = parseNote(note);
  size_t idx = getButtonIndex(note);
  if (idx >= MAX_BUTTONS) return;

  ButtonState& state = buttonStates[idx];
  if (!state.isPressed) {
    state.isPressed = true;
    state.consumedByHoldTwo = false;
    state.didImmediateSeek = false;
    state.pressStartTime = millis();
    if (info.type == BarStepButtonType::SIXTEENTH) {
      pressed16thBits |= (1u << info.stepIndex);
      last16thNoteOnTime = state.pressStartTime;
    } else {
      pressedBarBits |= (1u << info.stepIndex);
      lastBarNoteOnTime = state.pressStartTime;
    }
    testLog("BarStepButton: note %d (%s step %d) pressed, held: 16th=%d bar=%d [TEST POINT: press start]",
            note, info.type == BarStepButtonType::SIXTEENTH ? "16th" : "bar", info.stepIndex,
            pressed16thCount(), pressedBarCount());

    Track& trackRef = trackManager.getSelectedTrack();
    bool potentialHoldTwo = (info.type == BarStepButtonType::BAR && pressedBarCount() >= 2) ||
                            (info.type == BarStepButtonType::SIXTEENTH && pressed16thCount() >= 2);

    bool isLoopEdit = noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_LOOP_EDIT;
    if (isLoopEdit && trackRef.isJamPlaybackActive() && !potentialHoldTwo) {
      uint32_t loopLength = trackRef.getLoopLength();
      uint32_t loopStartTick = trackRef.getLoopStartTick();
      uint32_t jamStartTick = trackRef.getJamStartTick();
      uint32_t jamLength = trackRef.getJamLength();
      if (loopLength > 0 && jamLength > 0) {
        uint32_t displayPos = (info.type == BarStepButtonType::SIXTEENTH)
          ? (info.stepIndex * Config::TICKS_PER_16TH_STEP)
          : (info.stepIndex * Config::TICKS_PER_BAR);
        uint32_t tickInLoop = (loopStartTick + displayPos) % loopLength;
        tickInLoop = (tickInLoop / Config::TICKS_PER_16TH_STEP) * Config::TICKS_PER_16TH_STEP;
        uint32_t offsetInJam = (tickInLoop - jamStartTick + loopLength) % loopLength;
        if (offsetInJam < jamLength) {
          trackRef.setJamTick(offsetInJam);
          state.didImmediateSeek = true;
          testLog("BarStepButton: jam seek to tick %lu (quantized 16th) [TEST POINT: jam immediate seek]", offsetInJam);
        }
      }
    } else if (isLoopEdit && !trackRef.isJamPlaybackActive() && !potentialHoldTwo) {
      uint32_t loopLength = trackRef.getLoopLength();
      uint32_t loopStartTick = trackRef.getLoopStartTick();
      uint32_t startLoopTick = trackRef.getStartLoopTick();
      if (loopLength > 0) {
        uint32_t displayPos = (info.type == BarStepButtonType::SIXTEENTH)
          ? (info.stepIndex * Config::TICKS_PER_16TH_STEP)
          : (info.stepIndex * Config::TICKS_PER_BAR);
        uint32_t tickInLoop = (loopStartTick + displayPos) % loopLength;
        uint32_t seekTick = startLoopTick + tickInLoop;
        seekTick = (seekTick / Config::TICKS_PER_16TH_STEP) * Config::TICKS_PER_16TH_STEP;
        clockManager.setCurrentTick(seekTick);
        testLog("BarStepButton: immediate seek to tick %lu (quantized 16th) [TEST POINT: immediate seek]", seekTick);
      }
    }
  }
}

void BarStepButtonHandler::handleNoteOff(uint8_t note, uint8_t velocity) {
  BarStepButtonInfo info = parseNote(note);
  size_t idx = getButtonIndex(note);
  if (idx >= MAX_BUTTONS) return;

  ButtonState& state = buttonStates[idx];
  if (!state.isPressed) return;

  uint32_t now = millis();
  uint32_t duration = (now >= state.pressStartTime)
    ? (now - state.pressStartTime)
    : (0xFFFFFFFFu - state.pressStartTime + 1 + now);

  // If this button was consumed by a prior HOLD_TWO, skip all action logic
  if (state.consumedByHoldTwo) {
    testLog("BarStepButton: note %d released, consumed by prior HOLD_TWO, ignoring [TEST POINT: consumed]", note);
    state.isPressed = false;
    state.consumedByHoldTwo = false;
    if (info.type == BarStepButtonType::SIXTEENTH) {
      pressed16thBits &= ~(1u << info.stepIndex);
    } else {
      pressedBarBits &= ~(1u << info.stepIndex);
    }
    state.lastTapTime = 0;
    state.pendingShortPress = false;
    state.pendingDoublePress = false;
    state.pendingTriplePress = false;
    return;
  }

  uint8_t n16 = pressed16thCount();
  uint8_t nBar = pressedBarCount();
  bool hadTwoBeforeRelease = (info.type == BarStepButtonType::SIXTEENTH && n16 >= 2) ||
                             (info.type == BarStepButtonType::BAR && nBar >= 2);

  uint8_t rangeStart = info.stepIndex, rangeEnd = info.stepIndex;
  bool firstHeldBeforeSecond = false;
  if (hadTwoBeforeRelease) {
    uint32_t minStart = getMinPressStartTimeForType(info.type);
    uint32_t maxStart = getMaxPressStartTimeForType(info.type);
    uint32_t gap = (maxStart >= minStart) ? (maxStart - minStart) : 0;
    uint32_t overlapDuration = (now >= maxStart) ? (now - maxStart) : 0;
    bool isSwap = (maxStart != state.pressStartTime) && (overlapDuration < SWAP_THRESHOLD_MS);
    bool firstHeldLongEnough = (now - minStart) >= longPressTime;
    bool overlapSufficient = (overlapDuration >= 200u);
    firstHeldBeforeSecond = !isSwap && (
        (gap >= holdTwoMinGap) ||
        (firstHeldLongEnough && overlapSufficient)
    );
    testLog("BarStepButton: hadTwo duration=%lums gap=%lums overlap=%lums isSwap=%d firstHeldBeforeSecond=%d [TEST POINT: gap check]",
            duration, gap, overlapDuration, isSwap ? 1 : 0, firstHeldBeforeSecond ? 1 : 0);
    if (firstHeldBeforeSecond) {
      if (info.type == BarStepButtonType::SIXTEENTH) {
        rangeStart = getMinPressed16th();
        rangeEnd = getMaxPressed16th();
        if (info.stepIndex < rangeStart) rangeStart = info.stepIndex;
        else if (info.stepIndex > rangeEnd) rangeEnd = info.stepIndex;
      } else {
        rangeStart = getMinPressedBar();
        rangeEnd = getMaxPressedBar();
        if (info.stepIndex < rangeStart) rangeStart = info.stepIndex;
        else if (info.stepIndex > rangeEnd) rangeEnd = info.stepIndex;
      }
    }
  }

  state.isPressed = false;
  if (info.type == BarStepButtonType::SIXTEENTH) {
    pressed16thBits &= ~(1u << info.stepIndex);
  } else {
    pressedBarBits &= ~(1u << info.stepIndex);
  }

  testLog("BarStepButton: note %d released, duration=%lums, hadTwoBeforeRelease=%d [TEST POINT: release]",
          note, duration, hadTwoBeforeRelease ? 1 : 0);

  if (hadTwoBeforeRelease && firstHeldBeforeSecond) {
    testLog("BarStepButton: HOLD_TWO_BUTTONS type=%s range %d-%d [TEST POINT: two-button hold]",
            info.type == BarStepButtonType::SIXTEENTH ? "16th" : "bar", (int)rangeStart, (int)rangeEnd);
    // Consume all remaining pressed buttons of the same type so they don't fire on release
    if (info.type == BarStepButtonType::SIXTEENTH) {
      for (uint8_t i = 0; i < 16; i++) {
        if (pressed16thBits & (1u << i)) {
          buttonStates[i].consumedByHoldTwo = true;
        }
      }
    } else {
      for (uint8_t i = 0; i < 8; i++) {
        if (pressedBarBits & (1u << i)) {
          buttonStates[16 + i].consumedByHoldTwo = true;
        }
      }
    }
    onPressDetected(info, BarStepPressType::HOLD_TWO_BUTTONS, rangeStart, rangeEnd);
    state.lastTapTime = 0;
    state.pendingShortPress = false;
    state.pendingDoublePress = false;
    state.pendingTriplePress = false;
    return;
  }

  if (duration >= longPressTime) {
    testLog("BarStepButton: LONG_PRESS / HOLD_ONE [TEST POINT: long press]");
    onPressDetected(info, BarStepPressType::HOLD_ONE_BUTTON);
    state.lastTapTime = 0;
    state.pendingShortPress = false;
    state.pendingDoublePress = false;
    state.pendingTriplePress = false;
    return;
  }

  if (state.pendingDoublePress && (now - state.secondTapTime) <= tripleTapWindow) {
    state.pendingDoublePress = false;
    state.pendingTriplePress = true;
    state.triplePressExpireTime = now + tripleTapWindow;
  } else if (state.pendingShortPress && (now - state.lastTapTime) <= doubleTapWindow) {
    state.pendingShortPress = false;
    state.pendingDoublePress = true;
    state.secondTapTime = now;
    state.doublePressExpireTime = now + tripleTapWindow;
  } else {
    state.pendingShortPress = true;
    state.lastTapTime = now;
    state.shortPressExpireTime = now + doubleTapWindow;
  }
}

void BarStepButtonHandler::processPendingPresses() {
  uint32_t now = millis();
  for (size_t i = 0; i < MAX_BUTTONS; i++) {
    ButtonState& state = buttonStates[i];
    if (state.pendingTriplePress && now >= state.triplePressExpireTime) {
      state.pendingTriplePress = false;
      uint8_t note = (i < 16) ? (uint8_t)i : (uint8_t)(17 + (i - 16));
      BarStepButtonInfo info = parseNote(note);
      testLog("BarStepButton: TRIPLE_PRESS [TEST POINT: triple]");
      onPressDetected(info, BarStepPressType::TRIPLE_PRESS);
    } else if (state.pendingDoublePress && now >= state.doublePressExpireTime) {
      state.pendingDoublePress = false;
      uint8_t note = (i < 16) ? (uint8_t)i : (uint8_t)(17 + (i - 16));
      BarStepButtonInfo info = parseNote(note);
      testLog("BarStepButton: DOUBLE_PRESS [TEST POINT: double]");
      onPressDetected(info, BarStepPressType::DOUBLE_PRESS);
    } else if (state.pendingShortPress && now >= state.shortPressExpireTime) {
      state.pendingShortPress = false;
      bool didSeek = state.didImmediateSeek;
      state.didImmediateSeek = false;
      uint8_t note = (i < 16) ? (uint8_t)i : (uint8_t)(17 + (i - 16));
      BarStepButtonInfo info = parseNote(note);
      testLog("BarStepButton: SHORT_PRESS [TEST POINT: short]");
      onPressDetected(info, BarStepPressType::SHORT_PRESS, 0, 0, didSeek);
    }
  }
}

void BarStepButtonHandler::onPressDetected(const BarStepButtonInfo& info, BarStepPressType pressType,
                                           uint8_t rangeStart, uint8_t rangeEnd, bool didImmediateSeek) {
  bool isLoopEdit = noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_LOOP_EDIT;
  testLog("BarStepButton: onPressDetected type=%s step=%d pressType=%d mode=%s [TEST POINT: action dispatch]",
          info.type == BarStepButtonType::SIXTEENTH ? "16th" : "bar", info.stepIndex, (int)pressType,
          isLoopEdit ? "LOOP_EDIT" : "NOTE_EDIT");

  if (isLoopEdit) {
    executeLoopEditAction(info, pressType, rangeStart, rangeEnd, didImmediateSeek);
  } else {
    executeNoteEditAction(info, pressType, rangeStart, rangeEnd);
  }
}

void BarStepButtonHandler::enterBarSelect(uint8_t barIndex) {
  Track& track = trackManager.getSelectedTrack();
  selectedBarIndex = barIndex;
  isHoldTwoJam = false;

  uint32_t regionStartDisplay = barIndex * Config::TICKS_PER_BAR;
  uint32_t regionStartStorage = (track.getLoopStartTick() + regionStartDisplay) % track.getLoopLength();
  track.setJam(regionStartStorage, Config::TICKS_PER_BAR);
  track.setJamPlayback(true);
  testLog("BarStepButton: enterBarSelect bar=%d jamStart=%lu jamLen=%lu [TEST POINT: bar select enter]",
          barIndex, regionStartStorage, Config::TICKS_PER_BAR);
}

void BarStepButtonHandler::exitBarSelect() {
  Track& track = trackManager.getSelectedTrack();
  if (!track.isJamming()) return;
  track.clearJam();
  isHoldTwoJam = false;
  testLog("BarStepButton: exitBarSelect, jam cleared [TEST POINT: bar select exit]");
}

void BarStepButtonHandler::switchBarSelect(uint8_t barIndex) {
  Track& track = trackManager.getSelectedTrack();
  selectedBarIndex = barIndex;
  uint32_t regionStartDisplay = barIndex * Config::TICKS_PER_BAR;
  uint32_t regionStartStorage = (track.getLoopStartTick() + regionStartDisplay) % track.getLoopLength();
  track.setJam(regionStartStorage, Config::TICKS_PER_BAR);
  testLog("BarStepButton: switchBarSelect bar=%d jamStart=%lu [TEST POINT: bar select switch]",
          barIndex, regionStartStorage);
}

void BarStepButtonHandler::executeLoopEditAction(const BarStepButtonInfo& info, BarStepPressType pressType,
                                                 uint8_t rangeStart, uint8_t rangeEnd, bool didImmediateSeek) {
  Track& track = trackManager.getSelectedTrack();
  uint32_t loopLength = track.getLoopLength();
  uint32_t loopStartTick = track.getLoopStartTick();
  if (loopLength == 0) return;

  switch (pressType) {
    case BarStepPressType::SHORT_PRESS: {
      if (track.isJamPlaybackActive()) {
        if (info.type == BarStepButtonType::BAR) {
          uint32_t jamStartTick = track.getJamStartTick();
          uint32_t jamLength = track.getJamLength();
          uint32_t barStartTick = (loopStartTick + info.stepIndex * Config::TICKS_PER_BAR) % loopLength;
          uint32_t offsetInJam = (barStartTick - jamStartTick + loopLength) % loopLength;
          if (offsetInJam < jamLength) {
            if (!didImmediateSeek) {
              track.setJamTick(offsetInJam);
              testLog("BarStepButton: jam seek to bar %d offset=%lu [TEST POINT: jam seek bar]", info.stepIndex, offsetInJam);
            }
          } else {
            switchBarSelect(info.stepIndex);
          }
        } else {
          uint32_t seekPos = info.stepIndex * Config::TICKS_PER_16TH_STEP;
          if (seekPos < track.getJamLength() && !didImmediateSeek) {
            track.setJamTick(seekPos);
            testLog("BarStepButton: jam seek to 16th %d [TEST POINT: jam navigate 16th]", info.stepIndex);
          }
        }
      }
      break;
    }
    case BarStepPressType::HOLD_ONE_BUTTON: {
      if (info.type == BarStepButtonType::BAR) {
        if (track.isJamming() && info.stepIndex == selectedBarIndex) {
          track.setJamTick(0);
          testLog("BarStepButton: HOLD_ONE same bar, seek to start [TEST POINT: hold one seek]");
        } else if (track.isJamming()) {
          switchBarSelect(info.stepIndex);
        } else {
          enterBarSelect(info.stepIndex);
        }
      } else {
        // 16th HOLD_ONE: zoom into single 16th step (original behavior)
        uint32_t regionStartDisplay = info.stepIndex * Config::TICKS_PER_16TH_STEP;
        uint32_t regionLen = Config::TICKS_PER_16TH_STEP;
        uint32_t regionStartStorage = (loopStartTick + regionStartDisplay) % loopLength;
        TrackUndo::pushLoopStartSnapshot(track);
        track.setLoopStartTick(regionStartStorage);
        track.setLoopLength(regionLen);
        testLog("BarStepButton LoopEdit HOLD_ONE 16th: loop start=%lu len=%lu [TEST POINT: loop set]", regionStartStorage, regionLen);
      }
      break;
    }
    case BarStepPressType::HOLD_TWO_BUTTONS: {
      if (track.isJamming()) exitBarSelect();
      uint32_t rStartDisplay = (info.type == BarStepButtonType::SIXTEENTH)
        ? (rangeStart * Config::TICKS_PER_16TH_STEP)
        : (rangeStart * Config::TICKS_PER_BAR);
      uint32_t rEndDisplay = (info.type == BarStepButtonType::SIXTEENTH)
        ? ((rangeEnd + 1) * Config::TICKS_PER_16TH_STEP)
        : ((rangeEnd + 1) * Config::TICKS_PER_BAR);
      uint32_t rLen = rEndDisplay - rStartDisplay;
      uint32_t rStartStorage = (loopStartTick + rStartDisplay) % loopLength;
      track.setJam(rStartStorage, rLen);
      track.setJamPlayback(true);
      isHoldTwoJam = true;
      testLog("BarStepButton LoopEdit HOLD_TWO: jam start=%lu len=%lu [TEST POINT: loop range]", rStartStorage, rLen);
      break;
    }
    case BarStepPressType::DOUBLE_PRESS:
      if (track.isJamming()) {
        exitBarSelect();
        testLog("BarStepButton LoopEdit DOUBLE: exit jam [TEST POINT: double press exit]");
      }
      break;
    case BarStepPressType::TRIPLE_PRESS:
      TrackUndo::undoLoopStart(track);
      testLog("BarStepButton LoopEdit TRIPLE: undo/redo [TEST POINT: undo]");
      break;
    default:
      break;
  }
}

void BarStepButtonHandler::executeNoteEditAction(const BarStepButtonInfo& info, BarStepPressType pressType,
                                                 uint8_t rangeStart, uint8_t rangeEnd) {
  testLog("BarStepButton NoteEdit: pressType=%d [TODO: selector/bulk/add-remove]", (int)pressType);
  (void)info;
  (void)rangeStart;
  (void)rangeEnd;
}

void BarStepButtonHandler::testLog(const char* format, ...) const {
  if (!testLoggingEnabled) return;
  char buf[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  logger.log(CAT_BAR_STEP_BUTTON, LOG_INFO, "%s", buf);
}
