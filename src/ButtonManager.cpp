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
    encoderPosition = 0;
    lastEncoderPosition = 0;
    if (DEBUG_BUTTONS) {
        Serial.println("ButtonManager constructor called.");
    }
}

void ButtonManager::setup(const std::vector<uint8_t>& pins) {
    size_t countPins = pins.size();
    buttons.resize(countPins);
    pressTimes.resize(countPins);
    lastTapTime.resize(countPins, 0);
    pendingShortPress.resize(countPins, false);
    shortPressExpireTime.resize(countPins, 0);

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
    encoderPosition = 0;
    lastEncoderPosition = 0;

    if (DEBUG_BUTTONS) {
        Serial.print("ButtonManager setup complete with ");
        Serial.print(countPins);
        Serial.println(" buttons.");
    }
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
                handleButton(static_cast<ButtonId>(i), BUTTON_LONG_PRESS);
            } else {
                // Delay decision for short press vs. double tap
                if (now - lastTapTime[i] <= DOUBLE_TAP_WINDOW) {
                    // Detected second tap in window → double
                    lastTapTime[i] = 0;
                    pendingShortPress[i] = false;
                    handleButton(static_cast<ButtonId>(i), BUTTON_DOUBLE_PRESS);
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
    }

    // --- Encoder handling ---
    long newEncoderPos = encoder.read() / 4; // adjust divisor for your encoder
    if (newEncoderPos != encoderPosition) {
        lastEncoderPosition = encoderPosition;
        encoderPosition = newEncoderPos;
        if (looperState.getEditContext() != EDIT_NONE) {
            // In edit mode: encoder changes value (simulate value change)
            int delta = encoderPosition - lastEncoderPosition;
            editManager.moveBracket(delta, trackManager.getSelectedTrack(), Config::TICKS_PER_16TH_STEP);
            if (DEBUG_BUTTONS) {
                Serial.print("[EDIT] Encoder value change: ");
                Serial.println(delta);
            }
            // TODO: Apply value change to selected note/param/track
        } else 
            // Not in edit mode, print encoder position for debug
            if (DEBUG_BUTTONS) {
                Serial.print("Encoder position: ");
                Serial.println(encoderPosition);
            }
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
                        if (DEBUG_BUTTONS) Serial.println("Button A: Undo Overdub");
                        TrackUndo::undoOverdub(track);
                    }
                    break;
                case BUTTON_SHORT_PRESS:
                    if (track.isEmpty()) {
                        if (DEBUG_BUTTONS) Serial.println("Button A: Start Recording");
                        looperState.requestStateTransition(LOOPER_RECORDING);
                        trackManager.startRecordingTrack(idx, now);
                    } else if (track.isRecording()) {
                        if (DEBUG_BUTTONS) Serial.println("Button A: Stop Recording");
                        trackManager.stopRecordingTrack(idx);
                        track.startPlaying(now);
                    } else if (track.isOverdubbing()) {
                        if (DEBUG_BUTTONS) Serial.println("Button A: Stop Overdub");
                        track.startPlaying(now);
                    } else if (track.isPlaying()) {
                        if (DEBUG_BUTTONS) Serial.println("Button A: Live Overdub");
                        trackManager.startOverdubbingTrack(idx);
                    } else {
                        if (DEBUG_BUTTONS) Serial.println("Button A: Toggle Play/Stop");
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
                        if (DEBUG_BUTTONS) Serial.println("Button A: Clear Track");
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
                        if (DEBUG_BUTTONS) Serial.println("Button A: Undo Clear Track");
                        TrackUndo::undoClearTrack(track);
                        StorageManager::saveState(looperState.getLooperState());
                    } else {
                        if (DEBUG_BUTTONS) Serial.println("Nothing to undo for clear/mute.");
                    }                
                    break;
                case BUTTON_SHORT_PRESS: {
                    uint8_t newIndex = (trackManager.getSelectedTrackIndex() + 1)
                                       % trackManager.getTrackCount();
                    trackManager.setSelectedTrack(newIndex);
                    if (DEBUG_BUTTONS) {
                        Serial.print("Button B: Switched to track ");
                        Serial.println(newIndex);
                    }
                    break;
                }
                case BUTTON_LONG_PRESS:
                    if (!track.hasData()) {
                        logger.debug("Mute ignored — track is empty");
                    } else {
                        track.toggleMuteTrack();            
                        if (DEBUG_BUTTONS) {
                            Serial.print("Button B: Toggled mute on track ");
                            Serial.println(trackManager.getSelectedTrackIndex());
                        }
                    }
                    break;
                default:
                    break;
            }
            break;
        case BUTTON_ENCODER:
            switch (action) {
                case BUTTON_SHORT_PRESS:
                    if (looperState.getEditContext() == EDIT_NONE) {
                        looperState.enterEditMode(EDIT_NOTE);
                        editManager.enterEditMode(EDIT_NOTE, clockManager.getCurrentTick());
                        if (DEBUG_BUTTONS) Serial.println("Encoder Button: Enter Edit Mode (EDIT_NOTE)");
                    } else {
                        // Cycle through edit types
                        EditContext ctx = looperState.getEditContext();
                        int ctxInt = static_cast<int>(ctx);
                        int numContexts = 3; // EDIT_NOTE, EDIT_PARAM, EDIT_TRACK
                        ctxInt++;
                        if (ctxInt > numContexts) ctxInt = 1;
                        looperState.setEditContext(static_cast<EditContext>(ctxInt));
                        if (DEBUG_BUTTONS) {
                            Serial.print("Encoder Button: Edit context changed to: ");
                            switch (looperState.getEditContext()) {
                                case EDIT_NOTE: Serial.println("EDIT_NOTE"); break;
                                case EDIT_PARAM: Serial.println("EDIT_PARAM"); break;
                                case EDIT_TRACK: Serial.println("EDIT_TRACK"); break;
                                default: Serial.println("UNKNOWN"); break;
                            }
                        }
                    }
                    break;
                case BUTTON_LONG_PRESS:
                    if (looperState.getEditContext() != EDIT_NONE) {
                        logger.debug("Exit edit mode");
                        looperState.exitEditMode();
                        editManager.exitEditMode();
                    }   
                    break;
                default:
                    break;
            }
            break;
    }
}