#include "ButtonManager.h"
#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"

ButtonManager buttonManager;

ButtonManager::ButtonManager() {}

void ButtonManager::setup(const std::vector<uint8_t>& pins)
{
    buttons.resize(pins.size());
    pressTimes.resize(pins.size());

    for (size_t i = 0; i < pins.size(); ++i) {
        buttons[i].attach(pins[i], INPUT_PULLUP);
        buttons[i].interval(10);  // 10ms
    }
}

void ButtonManager::update()
{
    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].update();

        if (buttons[i].fell()) {
            pressTimes[i] = millis();
        }
        if (buttons[i].rose()) {
            uint32_t duration = millis() - pressTimes[i];
            if (duration >= LONG_PRESS_TIME) {
                handleButton(i, BUTTON_LONG_PRESS);
            } else {
                handleButton(i, BUTTON_SHORT_PRESS);
            }
        }
    }
}

void ButtonManager::handleButton(uint8_t index, ButtonAction action)
{
    if (index == 0) { 
        // --------- Button A: Record - Overdub - Play, Clear Track --------
        Track& track = trackManager.getTrack(selectedTrack);

        switch (action) {
            case BUTTON_SHORT_PRESS:
                if (!track.isRecording() && !track.isOverdubbing() && !track.isArmed() && !track.isPlaying()) {
                    Serial.println("Button A short press: Start Recording");
                    track.startRecording(clockManager.getCurrentTick());
                }
                else if (track.isRecording()) {
                    Serial.println("Button A short press: Start Overdubbing");
                    track.stopRecording(clockManager.getCurrentTick());
                    track.startPlaying();
                    track.startOverdubbing();

                    if (track.hasData()) {
                       Serial.println("Data recorded.");
                    }

                    Serial.print("Track recorded ");
                    Serial.print(track.getEventCount());
                    Serial.println(" events.");
                }
                else if (track.isOverdubbing()) {
                    Serial.println("Button A short press: Stop Overdubbing");
                    track.stopOverdubbing(clockManager.getCurrentTick());
                }
                else if (track.isPlaying()) {
                    Serial.println("Button A short press: Start live overdubbing");
                    track.startOverdubbing();
                }
                else {
                    Serial.println("Button A short press: Toggle Play/Stop");
                    track.togglePlayStop();
                }
                break;

            case BUTTON_LONG_PRESS:
                Serial.println("Button A long press: Erase Track");
                track.clear();
                break;

            default:
                break;
        }
    } else if (index == 1) {
    // --------- Button B: Track Switch, Mute/Unmute --------
    Track* track = nullptr; // set varibables outside of switch statement to safely use it as a pointer.
    switch (action) {
        case BUTTON_SHORT_PRESS:
            // Switch track
            selectedTrack = (selectedTrack + 1) % trackManager.getTrackCount();
            Serial.print("Button B short press: Switched to track ");
            Serial.println(selectedTrack);
            break;

        case BUTTON_LONG_PRESS: {
            track = &trackManager.getTrack(selectedTrack);
            track->toggleMuteTrack();
            Serial.print("Button B long press: Toggled mute on track ");
            Serial.println(selectedTrack);
            break;
        }

        default:
            break;
      }
    }
}
