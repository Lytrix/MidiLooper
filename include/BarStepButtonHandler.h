//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file BarStepButtonHandler.h
 * @brief Handles bar and 16th-step button input (notes 0-15, 17-24 on channel 16).
 * Mode-specific actions for Loop Edit and Note Edit. Supports short, long, double, triple,
 * and two-button hold. Use with any MIDI controller that sends these note values.
 */
#ifndef BAR_STEP_BUTTON_HANDLER_H
#define BAR_STEP_BUTTON_HANDLER_H

#include <Arduino.h>
#include <cstdint>

// Forward declarations
class Track;

enum class BarStepPressType {
  SHORT_PRESS,
  LONG_PRESS,
  DOUBLE_PRESS,
  TRIPLE_PRESS,
  HOLD_ONE_BUTTON,
  HOLD_TWO_BUTTONS
};

enum class BarStepButtonType {
  SIXTEENTH,  // notes 0-15
  BAR         // notes 17-24
};

struct BarStepButtonInfo {
  BarStepButtonType type;
  uint8_t stepIndex;  // 0-15 for 16th, 0-7 for bar
  uint8_t note;
};

class BarStepButtonHandler {
public:
  BarStepButtonHandler();
  void setup();
  void update();
  void handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn);

  // Test hook: enable verbose logging (call from main.cpp setup for manual testing)
  void setTestLoggingEnabled(bool enabled) { testLoggingEnabled = enabled; }
  bool isTestLoggingEnabled() const { return testLoggingEnabled; }

  // Used by MidiHandler for routing - returns true if note is bar/step button
  bool isBarStepButtonNote(uint8_t channel, uint8_t note) const;

private:
  bool testLoggingEnabled;
  uint32_t doubleTapWindow;
  uint32_t tripleTapWindow;
  uint32_t longPressTime;
  uint32_t holdTwoMinGap;
  static constexpr uint32_t SWAP_THRESHOLD_MS = 50;

  // Pressed button tracking: bit N set = step N is pressed
  uint16_t pressed16thBits;  // bits 0-15 for 16th steps
  uint16_t pressedBarBits;   // bits 0-7 for bar steps
  uint32_t lastBarNoteOnTime;
  uint32_t last16thNoteOnTime;

  // Bar select / HOLD_TWO: uses Track jam state for display zoom
  uint8_t selectedBarIndex;
  bool isHoldTwoJam;
  void enterBarSelect(uint8_t barIndex);
  void exitBarSelect();
  void switchBarSelect(uint8_t barIndex);

  uint8_t pressed16thCount() const;
  uint8_t pressedBarCount() const;
  uint8_t getMinPressed16th() const;
  uint8_t getMaxPressed16th() const;
  uint8_t getMinPressedBar() const;
  uint8_t getMaxPressedBar() const;
  uint32_t getMinPressStartTimeForType(BarStepButtonType type) const;
  uint32_t getMaxPressStartTimeForType(BarStepButtonType type) const;

  // Per-note state for tap detection (note 0-15 or 17-24 maps to index 0-23)
  static constexpr size_t MAX_BUTTONS = 24;
  struct ButtonState {
    bool isPressed;
    bool consumedByHoldTwo;
    uint32_t pressStartTime;
    uint32_t lastTapTime;
    uint32_t secondTapTime;
    bool pendingShortPress;
    uint32_t shortPressExpireTime;
    bool pendingDoublePress;
    uint32_t doublePressExpireTime;
    bool pendingTriplePress;
    uint32_t triplePressExpireTime;
  };
  ButtonState buttonStates[MAX_BUTTONS];

  BarStepButtonInfo parseNote(uint8_t note) const;
  size_t getButtonIndex(uint8_t note) const;
  void handleNoteOn(uint8_t note, uint8_t velocity);
  void handleNoteOff(uint8_t note, uint8_t velocity);
  void processPendingPresses();
  void onPressDetected(const BarStepButtonInfo& info, BarStepPressType pressType,
                       uint8_t rangeStart = 0, uint8_t rangeEnd = 0);
  void executeLoopEditAction(const BarStepButtonInfo& info, BarStepPressType pressType,
                             uint8_t rangeStart, uint8_t rangeEnd);
  void executeNoteEditAction(const BarStepButtonInfo& info, BarStepPressType pressType,
                             uint8_t rangeStart, uint8_t rangeEnd);
  void testLog(const char* format, ...) const;
};

// Note: isBarStepButtonNote is public for MidiHandler routing

extern BarStepButtonHandler barStepButtonHandler;

#endif // BAR_STEP_BUTTON_HANDLER_H
