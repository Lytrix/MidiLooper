//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "GpioButtonManager.h"
#include "MidiButtonActions.h"
#include "NoteEditManager.h"
#include "EditManager.h"
#include "NoteEditSessionState.h"
#include "LooperState.h"
#include "DisplayManager.h"
#include "Logger.h"
#include <Encoder.h>

Encoder gpioEncoder(Buttons::ENCODER_PIN_A, Buttons::ENCODER_PIN_B);

GpioButtonManager gpioButtonManager;

GpioButtonManager::GpioButtonManager() {
    buttons.clear();
    pressTimes.clear();
    lastTapTime.clear();
    pendingShortPress.clear();
    shortPressExpireTime.clear();
    secondTapTime.clear();
    pendingDoublePress.clear();
    doublePressExpireTime.clear();
    encoderPosition = 0;
}

void GpioButtonManager::setup(const std::vector<uint8_t>& pins) {
    const size_t countPins = pins.size();
    buttons.resize(countPins);
    pressTimes.resize(countPins);
    lastTapTime.resize(countPins, 0);
    pendingShortPress.resize(countPins, false);
    shortPressExpireTime.resize(countPins, 0);
    secondTapTime.resize(countPins, 0);
    pendingDoublePress.resize(countPins, false);
    doublePressExpireTime.resize(countPins, 0);
    encoderPosition = 0;

    const uint32_t t0 = millis();
    pressTimes.assign(countPins, t0);

    for (size_t i = 0; i < countPins; ++i) {
        buttons[i].attach(pins[i], INPUT_PULLUP);
        buttons[i].interval(DEFAULT_DEBOUNCE_INTERVAL);
        buttons[i].update();
        buttons[i].update();
    }
}

void GpioButtonManager::update() {
    static uint32_t bootTime = millis();
    const uint32_t now = millis();

    if (now - bootTime < 1000) {
        for (auto& b : buttons) {
            b.update();
        }
        return;
    }

    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].update();

        if (buttons[i].fell()) {
            pressTimes[i] = now;
        }

        if (buttons[i].rose()) {
            if (pressTimes[i] == 0) {
                pressTimes[i] = now;
                continue;
            }
            const uint32_t duration = now - pressTimes[i];
            if (duration >= LONG_PRESS_TIME) {
                lastTapTime[i] = 0;
                pendingShortPress[i] = false;
                secondTapTime[i] = 0;
                pendingDoublePress[i] = false;
                handleButton(static_cast<ButtonId>(i), BUTTON_LONG_PRESS);
            } else if (pendingDoublePress[i] && (now - secondTapTime[i] <= DOUBLE_TAP_WINDOW)) {
                lastTapTime[i] = 0;
                pendingShortPress[i] = false;
                secondTapTime[i] = 0;
                pendingDoublePress[i] = false;
                handleButton(static_cast<ButtonId>(i), BUTTON_TRIPLE_PRESS);
            } else if (now - lastTapTime[i] <= DOUBLE_TAP_WINDOW) {
                lastTapTime[i] = 0;
                pendingShortPress[i] = false;
                secondTapTime[i] = now;
                pendingDoublePress[i] = true;
                doublePressExpireTime[i] = now + DOUBLE_TAP_WINDOW;
            } else {
                lastTapTime[i] = now;
                pendingShortPress[i] = true;
                shortPressExpireTime[i] = now + DOUBLE_TAP_WINDOW;
            }
        }

        if (pendingShortPress[i] && now >= shortPressExpireTime[i]) {
            pendingShortPress[i] = false;
            handleButton(static_cast<ButtonId>(i), BUTTON_SHORT_PRESS);
        }

        if (pendingDoublePress[i] && now >= doublePressExpireTime[i]) {
            pendingDoublePress[i] = false;
            handleButton(static_cast<ButtonId>(i), BUTTON_DOUBLE_PRESS);
        }
    }

    static bool wasEncoderButtonHeld = false;
    static uint32_t encoderButtonHoldStart = 0;
    static bool pitchEditActive = false;
    constexpr uint32_t kEncoderHoldDelay = 250;
    bool encoderButtonHeld = false;
    if (buttons.size() > BUTTON_ENCODER) {
        encoderButtonHeld = buttons[BUTTON_ENCODER].read() == LOW;
    }
    if (encoderButtonHeld && !wasEncoderButtonHeld) {
        encoderButtonHoldStart = now;
    }
    if (encoderButtonHeld && (now - encoderButtonHoldStart >= kEncoderHoldDelay) &&
        !looperState.isLoadSaveModeActive() &&
        (editManager.getNoteEditSessionState().kind == NoteEditKind::Select ||
         editManager.getNoteEditSessionState().kind == NoteEditKind::Move)) {
        if (!pitchEditActive) {
            editManager.applyGeometryKindFromControl(trackManager.getSelectedTrack(),
                                                     NoteEditKind::Pitch, false);
            editManager.syncNoteEditSessionStateToUi(trackManager.getSelectedTrack());
            pitchEditActive = true;
        }
    }
    if (!encoderButtonHeld && wasEncoderButtonHeld) {
        if (editManager.getNoteEditSessionState().kind == NoteEditKind::Pitch) {
            editManager.applySelectNav(trackManager.getSelectedTrack(),
                                       editManager.getSelectedNoteIdx(),
                                       editManager.getBracketTick(),
                                       editManager.getLastFader1SelectRef(),
                                       editManager.getSelectedNoteIdx() >= 0);
        }
        encoderButtonHoldStart = 0;
        pitchEditActive = false;
    }
    wasEncoderButtonHeld = encoderButtonHeld;

    const long newEncoderPos = gpioEncoder.read() / 4;
    const int rawDelta = static_cast<int>(newEncoderPos - encoderPosition);
    if (rawDelta != 0) {
        if (looperState.isLoadSaveModeActive()) {
            displayManager.adjustLoadSaveListSelection(rawDelta);
        } else {
            noteEditManager.processEncoderMovement(rawDelta);
        }
        encoderPosition = newEncoderPos;
    }
}

void GpioButtonManager::handleButton(ButtonId button, ButtonAction action) {
    if (looperState.isLoadSaveModeActive()) {
        switch (button) {
            case BUTTON_D:
                break;
            case BUTTON_ENCODER:
                switch (action) {
                    case BUTTON_SHORT_PRESS:
                        displayManager.handleLoadSaveOverlayPress(
                            DisplayManager::LoadSaveOverlayPressType::Short);
                        break;
                    case BUTTON_DOUBLE_PRESS:
                        displayManager.handleLoadSaveOverlayPress(
                            DisplayManager::LoadSaveOverlayPressType::Double);
                        break;
                    case BUTTON_LONG_PRESS:
                        displayManager.handleLoadSaveOverlayPress(
                            DisplayManager::LoadSaveOverlayPressType::Long);
                        break;
                    default:
                        break;
                }
                return;
            default:
                return;
        }
    }

    switch (button) {
        case BUTTON_A:
            switch (action) {
                case BUTTON_DOUBLE_PRESS:
                    midiButtonActions.handleUndo();
                    break;
                case BUTTON_TRIPLE_PRESS:
                    midiButtonActions.handleRedo();
                    break;
                case BUTTON_SHORT_PRESS:
                    midiButtonActions.handleToggleRecord();
                    break;
                case BUTTON_LONG_PRESS:
                    midiButtonActions.handleClearTrack();
                    break;
                default:
                    break;
            }
            break;
        case BUTTON_B:
            switch (action) {
                case BUTTON_DOUBLE_PRESS:
                    midiButtonActions.handleUndoClearTrack();
                    break;
                case BUTTON_TRIPLE_PRESS:
                    midiButtonActions.handleRedoClearTrack();
                    break;
                case BUTTON_SHORT_PRESS:
                    midiButtonActions.handleSelectTrack(
                        (trackManager.getSelectedTrackIndex() + 1) % trackManager.getTrackCount());
                    break;
                case BUTTON_LONG_PRESS:
                    midiButtonActions.handleMuteTrack(255);
                    break;
                default:
                    break;
            }
            break;
        case BUTTON_C:
            switch (action) {
                case BUTTON_SHORT_PRESS:
                    editManager.cycleEditSession(trackManager.getSelectedTrack());
                    break;
                default:
                    break;
            }
            break;
        case BUTTON_D:
            switch (action) {
                case BUTTON_SHORT_PRESS:
                    midiButtonActions.handleToggleTransport();
                    break;
                case BUTTON_DOUBLE_PRESS:
                    midiButtonActions.handleResetToLoopStart();
                    break;
                default:
                    break;
            }
            break;
        case BUTTON_ENCODER:
            switch (action) {
                case BUTTON_SHORT_PRESS:
                    midiButtonActions.handleCycleNoteEditType();
                    break;
                case BUTTON_LONG_PRESS:
                    midiButtonActions.handleExitEditMode();
                    break;
                default:
                    break;
            }
            break;
    }
}
