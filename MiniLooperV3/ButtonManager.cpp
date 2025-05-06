#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "Logger.h"

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

    for (size_t i = 0; i < countPins; ++i) {
        buttons[i].attach(pins[i], INPUT_PULLUP);
        buttons[i].interval(DEFAULT_DEBOUNCE_INTERVAL);
    }

    if (DEBUG_BUTTONS) {
        Serial.print("ButtonManager setup complete with ");
        Serial.print(countPins);
        Serial.println(" buttons.");
    }
}

void ButtonManager::update() {
    // Ignore states when booting up, pullup change is else detected as button press
    static unsigned long bootTime = millis();
    if (millis() - bootTime < 1000) return;

    uint32_t now = millis();

    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].update();

        if (buttons[i].fell()) {
            pressTimes[i] = now;
        }

        if (buttons[i].rose()) {
            uint32_t duration = now - pressTimes[i];
            if (duration >= LONG_PRESS_TIME) {
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
    uint32_t now = clockManager.getCurrentTick();

    if (index == 0) {
        switch (action) {
            case BUTTON_DOUBLE_PRESS:
                if (track.canUndo()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Undo Overdub");
                    track.undoOverdub();
                }
                break;
            case BUTTON_SHORT_PRESS:
                if (track.isEmpty()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Start Recording");
                    track.startRecording(now);

                } else if (track.isRecording()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Switch to Overdub");
                    track.stopRecording(now);
                    track.startPlaying(now);

                } else if (track.isOverdubbing()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Stop Overdub");
                    track.startPlaying(now);

                } else if (track.isPlaying()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Live Overdub");
                    track.startOverdubbing(now);

                } else {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Toggle Play/Stop");
                    track.togglePlayStop();
                }
                break;

            case BUTTON_LONG_PRESS:
                if (!track.hasData()) {
                    logger.debug("Clear ignored — track is empty");
                } else {
                    track.clear();
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