#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "StorageManager.h"
#include "Logger.h"
#include "TrackUndo.h"

ButtonManager buttonManager;

ButtonManager::ButtonManager() {
    buttons.clear();
    pressTimes.clear();
    lastTapTime.clear();
    pendingShortPress.clear();
    shortPressExpireTime.clear();

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

    if (DEBUG_BUTTONS) {
        Serial.print("ButtonManager setup complete with ");
        Serial.print(countPins);
        Serial.println(" buttons.");
    }
}

void ButtonManager::update() {
    // Ignore states when booting up, pullup change is else detected as button press
       // still ignore the first second of boot-up
    static uint32_t bootTime = millis();
    uint32_t now = millis();
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
                handleButton(i, BUTTON_LONG_PRESS);
            } else {
                // Delay decision for short press vs. double tap
                if (now - lastTapTime[i] <= DOUBLE_TAP_WINDOW) {
                    // Detected second tap in window → double
                    lastTapTime[i] = 0;
                    pendingShortPress[i] = false;
                    handleButton(i, BUTTON_DOUBLE_PRESS);
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
            handleButton(i, BUTTON_SHORT_PRESS);
        }
    }
}

void ButtonManager::handleButton(uint8_t index, ButtonAction action) {
    auto& track = trackManager.getSelectedTrack();
    uint8_t idx = trackManager.getSelectedTrackIndex();
    uint32_t now = clockManager.getCurrentTick();

    if (index == 0) {
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
                    StorageManager::saveState(looperState);
                    if (DEBUG_BUTTONS) Serial.println("Button A: Clear Track");
                }
                break;

            default:
                break;
        }
    }
    else if (index == 1) {
        // Button B: switch / mute unchanged
        switch (action) {
            case BUTTON_DOUBLE_PRESS:
                if (TrackUndo::canUndoClearTrack(track)) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Undo Clear Track");
                    TrackUndo::undoClearTrack(track);
                    StorageManager::saveState(looperState);
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
    }
}