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

    if (DEBUG_BUTTONS) {
        Serial.println("ButtonManager constructor called.");
    }
}

void ButtonManager::setup(const std::vector<uint8_t>& pins) {
    buttons.resize(pins.size());
    pressTimes.resize(pins.size());

    for (size_t i = 0; i < pins.size(); ++i) {
        buttons[i].attach(pins[i], INPUT_PULLUP);
        buttons[i].interval(DEFAULT_DEBOUNCE_INTERVAL);
    }

    if (DEBUG_BUTTONS) {
        Serial.print("ButtonManager setup complete with ");
        Serial.print(pins.size());
        Serial.println(" buttons.");
    }
}

void ButtonManager::update() {
    static unsigned long bootTime = millis();

    if (millis() - bootTime < 500) {
        return;  // ignore button input for 500ms after boot
    }

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

            handleButton(i, action);
        }
    }
}

void ButtonManager::handleButton(uint8_t index, ButtonAction action) {
    
    auto& track = trackManager.getSelectedTrack();

    if (index == 0) {
        // Button A: Record / Overdub / Play / Clear
        switch (action) {
            case BUTTON_SHORT_PRESS:
                if (track.isEmpty()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Start Recording");
                    track.startRecording(clockManager.getCurrentTick());
                } else if (track.isRecording()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Switch to Overdub");
                    track.stopRecording(clockManager.getCurrentTick());
                    track.startPlaying(clockManager.getCurrentTick());
                    track.startOverdubbing(clockManager.getCurrentTick());
                    if (DEBUG_BUTTONS) track.printNoteEvents();
                } else if (track.isOverdubbing()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Stop Overdub");
                    track.stopOverdubbing(clockManager.getCurrentTick());
                    track.startPlaying(clockManager.getCurrentTick());
                } else if (track.isPlaying()) {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Live Overdub");
                    track.startOverdubbing(clockManager.getCurrentTick());
                } else {
                    if (DEBUG_BUTTONS) Serial.println("Button A: Toggle Play/Stop");
                    track.togglePlayStop();
                }
                break;

            case BUTTON_LONG_PRESS:
                if (!track.hasData()) {
                    logger.debug("Clear ignored — track is empty");
                return;
                }

                if (DEBUG_BUTTONS) Serial.println("Button A: Clear Track");
                track.clear();
                break;

            default:
                break;
        }
    } else if (index == 1) {
        // Button B: Track Switch / Mute
        switch (action) {
            case BUTTON_SHORT_PRESS: {
                uint8_t newIndex = (trackManager.getSelectedTrackIndex() + 1) % trackManager.getTrackCount();
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
                return;
                }
                track.toggleMuteTrack();
                if (DEBUG_BUTTONS) {
                    Serial.print("Button B: Toggled mute on track ");
                    Serial.println(trackManager.getSelectedTrackIndex());
                }
                break;

            default:
                break;
        }
    }
}
