#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "Logger.h"

ButtonManager buttonManager;

// Debounce interval in milliseconds
static const uint16_t DEFAULT_DEBOUNCE_INTERVAL = 10;

ButtonManager::ButtonManager() {
    buttons.clear();
    pressTimes.clear();
    lastTapTime.clear();

    if (DEBUG_BUTTONS) {
        Serial.println("ButtonManager constructor called.");
    }
}

void ButtonManager::setup(const std::vector<uint8_t>& pins) {
    size_t countPins = pins.size();
    buttons.resize(countPins);
    pressTimes.resize(countPins);
    lastTapTime.resize(countPins, 0);

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
    static unsigned long bootTime = millis();
    if (millis() - bootTime < 500) return;  // ignore first 500ms

    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].update();

        if (buttons[i].fell()) {
            pressTimes[i] = millis();
        }
        if (buttons[i].rose()) {
            uint32_t duration = millis() - pressTimes[i];
            ButtonAction action = (duration >= LONG_PRESS_TIME)
                                  ? BUTTON_LONG_PRESS
                                  : BUTTON_SHORT_PRESS;

            // promote to DOUBLE_PRESS if it really is one:
            if (action == BUTTON_SHORT_PRESS && isDoubleTap(i)) {
                action = BUTTON_DOUBLE_PRESS;
            }

            handleButton(i, action);
        }
    }
}

/// Returns true if this SHORT_PRESS is the second tap within the window
bool ButtonManager::isDoubleTap(uint8_t idx) {
    uint32_t now = millis();
    logger.debug("Doubletap count:%d - %d", now, lastTapTime[idx]);
    // if the previous tap was within the window, it’s a double-tap:
    if (now - lastTapTime[idx] <= DOUBLE_TAP_WINDOW) {
        lastTapTime[idx] = 0;   // reset so a third tap doesn’t re-trigger
        return true;
    }
    // otherwise record this as the first tap:
    lastTapTime[idx] = now;
    return false;
}

void ButtonManager::handleButton(uint8_t index, ButtonAction action) {
    auto& track = trackManager.getSelectedTrack();
    uint32_t now = clockManager.getCurrentTick();

    if (index == 0) {
        switch (action) {
            case BUTTON_DOUBLE_PRESS:
                if (track.canUndoOverdub() && (track.isOverdubbing() || track.isPlaying())) {
                    track.isOverdubbing();
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