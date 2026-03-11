//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "TrackUndo.h"
#include "Globals.h"
#include <Encoder.h>
#include "EditManager.h"

Encoder encoder(Buttons::ENCODER_PIN_A, Buttons::ENCODER_PIN_B);

ButtonManager buttonManager;

ButtonManager::ButtonManager() {
    buttons.clear();
    pressTimes.clear();
    lastTapTime.clear();
    pendingShortPress.clear();
    shortPressExpireTime.clear();
    // Initialize triple press tracking
    secondTapTime.clear();
    pendingDoublePress.clear();
    doublePressExpireTime.clear();
    encoderPosition = 0;
    lastEncoderPosition = 0;
}

void ButtonManager::setup(const std::vector<uint8_t>& pins) {
    size_t countPins = pins.size();
    buttons.resize(countPins);
    pressTimes.resize(countPins);
    lastTapTime.resize(countPins, 0);
    pendingShortPress.resize(countPins, false);
    shortPressExpireTime.resize(countPins, 0);
    // Initialize triple press tracking
    secondTapTime.resize(countPins, 0);
    pendingDoublePress.resize(countPins, false);
    doublePressExpireTime.resize(countPins, 0);
    encoderPosition = 0;
    lastEncoderPosition = 0;

    uint32_t t0 = millis();

    // seed pressTimes so that initial rises are harmless
    pressTimes.assign(countPins, t0);

    for (size_t i = 0; i < countPins; ++i) {
        buttons[i].attach(pins[i], INPUT_PULLUP);
        buttons[i].interval(DEFAULT_DEBOUNCE_INTERVAL);
        // prime the debouncer to clear any pending transitions
        buttons[i].update();
        buttons[i].update();
    }

    // encoder.write(0);
}

void ButtonManager::update() {
    // Ignore states when booting up, pullup change is else detected as button press
    static uint32_t bootTime = millis();
    uint32_t now = millis();

    // still ignore the first second of boot-up
    if (now - bootTime < 1000) {
        // make sure Bounce keeps its state up-to-date
        for (auto& b : buttons) b.update();
        return;
    }

    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].update();

        if (buttons[i].fell()) {
            pressTimes[i] = now;
        }

        if (buttons[i].rose()) {
            // never fire on a rise if we never saw a fall
            if (pressTimes[i] == 0) {
                pressTimes[i] = now;
                continue;
            }
            uint32_t duration = now - pressTimes[i];
            if (duration >= LONG_PRESS_TIME ) {
                // Long press - cancel any pending presses
                lastTapTime[i] = 0;
                pendingShortPress[i] = false;
                secondTapTime[i] = 0;
                pendingDoublePress[i] = false;
                handleButton(static_cast<ButtonId>(i), BUTTON_LONG_PRESS);
            } else {
                // Check for triple press first
                if (pendingDoublePress[i] && (now - secondTapTime[i] <= DOUBLE_TAP_WINDOW)) {
                    // Third tap within window - triple press
                    lastTapTime[i] = 0;
                    pendingShortPress[i] = false;
                    secondTapTime[i] = 0;
                    pendingDoublePress[i] = false;
                    handleButton(static_cast<ButtonId>(i), BUTTON_TRIPLE_PRESS);
                }
                // Check for double press
                else if (now - lastTapTime[i] <= DOUBLE_TAP_WINDOW) {
                    // Second tap within window - double press
                    lastTapTime[i] = 0;
                    pendingShortPress[i] = false;
                    secondTapTime[i] = now;
                    pendingDoublePress[i] = true;
                    doublePressExpireTime[i] = now + DOUBLE_TAP_WINDOW;
                } else {
                    // First tap
                    lastTapTime[i] = now;
                    pendingShortPress[i] = true;
                    shortPressExpireTime[i] = now + DOUBLE_TAP_WINDOW;
                }
            }
        }

        // Fire short press if timeout expired and no second tap arrived
        if (pendingShortPress[i] && now >= shortPressExpireTime[i]) {
            pendingShortPress[i] = false;
            handleButton(static_cast<ButtonId>(i), BUTTON_SHORT_PRESS);
        }
        
        // Fire double press if timeout expired and no third tap arrived
        if (pendingDoublePress[i] && now >= doublePressExpireTime[i]) {
            pendingDoublePress[i] = false;
            handleButton(static_cast<ButtonId>(i), BUTTON_DOUBLE_PRESS);
        }
    }

    // --- Encoder button hold/release logic ---
    static bool wasEncoderButtonHeld = false;
    static uint32_t encoderButtonHoldStart = 0;
    static bool pitchEditActive = false;
    const uint32_t ENCODER_HOLD_DELAY = 250; // ms
    bool encoderButtonHeld = false;
    if (buttons.size() > BUTTON_ENCODER) {
        encoderButtonHeld = buttons[BUTTON_ENCODER].read() == LOW; // LOW = pressed/held
    }
    if (encoderButtonHeld && !wasEncoderButtonHeld) {
        // Just started holding
        encoderButtonHoldStart = now;
    }
    if (encoderButtonHeld && (now - encoderButtonHoldStart >= ENCODER_HOLD_DELAY) &&
        (editManager.getCurrentState() == editManager.getNoteState() ||
         editManager.getCurrentState() == editManager.getStartNoteState())) {
        if (!pitchEditActive) {
            editManager.enterPitchEditMode(trackManager.getSelectedTrack());
            pitchEditActive = true;
        }
    }
    if (!encoderButtonHeld && wasEncoderButtonHeld) {
        // Just released
        if (editManager.getCurrentState() == editManager.getPitchNoteState()) {
            editManager.exitPitchEditMode(trackManager.getSelectedTrack());
        }
        encoderButtonHoldStart = 0;
        pitchEditActive = false;
    }
    wasEncoderButtonHeld = encoderButtonHeld;

    // --- Encoder handling ---
    static uint32_t lastEncoderTime = 0;
    long newEncoderPos = encoder.read() / 4; // adjust divisor for your encoder
    int rawDelta = newEncoderPos - encoderPosition;
    if (rawDelta != 0) {
        uint32_t now = millis();
        uint32_t interval = now - lastEncoderTime;
        lastEncoderTime = now;
        int accel = 1;
        if (editManager.getCurrentState() == editManager.getStartNoteState()) {
            if (interval < 25) accel = 24;
            else if (interval < 50) accel = 8;
            else if (interval < 100) accel = 4;
        } else {
            // Edit mode: slow down encoder
            if (interval < 50) accel = 4;
            else if (interval < 75) accel = 3;
            else if (interval < 100) accel = 2;
        }
        // else accel = 1;
        int finalDelta = rawDelta * accel;
        if (editManager.getCurrentState() != nullptr) {
            // In edit mode: encoder changes value
            editManager.onEncoderTurn(trackManager.getSelectedTrack(), finalDelta);
        }
        encoderPosition = newEncoderPos; // Only update after using the delta
    }
}

void ButtonManager::handleButton(ButtonId button, ButtonAction action) {
    auto& track = trackManager.getSelectedTrack();
    uint8_t idx = trackManager.getSelectedTrackIndex();
    uint32_t now = clockManager.getCurrentTick();

    switch (button) {
        case BUTTON_A:
            switch (action) {
                case BUTTON_DOUBLE_PRESS:
                    if (TrackUndo::canUndo(track)) {
                        TrackUndo::undoOverdub(track);
                    }
                    break;
                case BUTTON_TRIPLE_PRESS:
                    if (TrackUndo::canRedo(track)) {
                        TrackUndo::redoOverdub(track);
                    }
                    break;
                case BUTTON_SHORT_PRESS:
                    if (track.isEmpty()) {
                        looperState.requestStateTransition(LOOPER_RECORDING);
                        trackManager.startRecordingTrack(idx, now);
                    } else if (track.isRecording()) {
                        trackManager.stopRecordingTrack(idx);
                        track.startPlaying(now);
                    } else if (track.isOverdubbing()) {
                        track.startPlaying(now);
                    } else if (track.isPlaying()) {
                        trackManager.startOverdubbingTrack(idx);
                    } else {
                        track.togglePlayStop();
                    }
                    break;

                case BUTTON_LONG_PRESS:
                    if (!track.hasData()) {
                        logger.debug("Clear ignored — track is empty");
                    } else {
                        TrackUndo::pushClearTrackSnapshot(track);
                        track.clear();
                        StorageManager::saveState(looperState.getLooperState());
                    }
                    break;

                default:
                    break;
            }
            break;
        case BUTTON_B:
            switch (action) {
                case BUTTON_DOUBLE_PRESS:
                    if (TrackUndo::canUndoClearTrack(track)) {
                        TrackUndo::undoClearTrack(track);
                        StorageManager::saveState(looperState.getLooperState());
                    }
                    break;
                case BUTTON_TRIPLE_PRESS:
                    if (TrackUndo::canRedoClearTrack(track)) {
                        TrackUndo::redoClearTrack(track);
                        StorageManager::saveState(looperState.getLooperState());
                    }
                    break;
                case BUTTON_SHORT_PRESS: {
                    uint8_t newIndex = (trackManager.getSelectedTrackIndex() + 1)
                                       % trackManager.getTrackCount();
                    trackManager.setSelectedTrack(newIndex);
                    break;
                }
                case BUTTON_LONG_PRESS:
                    if (!track.hasData()) {
                        logger.debug("Mute ignored — track is empty");
                    } else {
                        track.toggleMuteTrack();
                    }
                    break;
                default:
                    break;
            }
            break;
        case BUTTON_ENCODER:
            switch (action) {
                case BUTTON_SHORT_PRESS:
                    if (editManager.getCurrentState() == nullptr) {
                        // Enter note edit mode
                        editManager.enterEditMode(editManager.getNoteState(), clockManager.getCurrentTick());
                    } else {
                        // Switch to next state (for now, just stay in note state)
                        editManager.switchToNextState(trackManager.getSelectedTrack());
                    }
                    break;
                case BUTTON_LONG_PRESS:
                    if (looperState.getEditContext() != EDIT_NONE) {
                        logger.debug("Exit edit mode");
                        editManager.exitEditMode(trackManager.getSelectedTrack());
                    }   
                    break;
                default:
                    break;
            }
            break;
    }
}