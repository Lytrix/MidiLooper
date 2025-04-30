#include "Globals.h"
#include "ButtonManager.h"
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
    // Get selected track from trackManager and set it to variable track
    auto& track = trackManager.getSelectedTrack();
    if (index == 0) { 
        // --------- Button A: Record - Overdub - Play, Clear Track --------
        switch (action) {
            case BUTTON_SHORT_PRESS:
                if (!track.isRecording() && !track.isOverdubbing() && !track.isArmed() && !track.isPlaying()) {
                    if(DEBUG_BUTTONS) Serial.println("Button A short press: Start Recording");
                    
                    track.startRecording(clockManager.getCurrentTick());
                }
                else if (track.isRecording()) {
                    if(DEBUG_BUTTONS) Serial.println("Button A short press: Start Overdubbing");
                    track.stopRecording(clockManager.getCurrentTick());
                    track.startPlaying();
                    track.startOverdubbing(clockManager.getCurrentTick());
                    if(DEBUG) trackManager.getSelectedTrack().printNoteEvents();
                    if (track.hasData()) {
                       Serial.println("Data recorded.");
                    }

                    if(DEBUG_BUTTONS) Serial.print("Track recorded ");
                    if(DEBUG) Serial.print(track.getMidiEventCount());
                    if(DEBUG) Serial.println(" Midi midiEvents.");
                    if(DEBUG) Serial.print(track.getNoteEventCount());
                    if(DEBUG) Serial.println(" Note midiEvents.");
                }
                else if (track.isOverdubbing()) {
                    if(DEBUG_BUTTONS) Serial.println("Button A short press: Stop Overdubbing");
                    track.stopOverdubbing(clockManager.getCurrentTick());
                }
                else if (track.isPlaying()) {
                    if(DEBUG_BUTTONS)  Serial.println("Button A short press: Start live overdubbing");
                    track.startOverdubbing(clockManager.getCurrentTick());
                }
                else {
                    if(DEBUG_BUTTONS) Serial.println("Button A short press: Toggle Play/Stop");
                    track.togglePlayStop();
                }
                break;

            case BUTTON_LONG_PRESS:
                if(DEBUG_BUTTONS) Serial.println("Button A long press: Erase Track");
                trackManager.getSelectedTrack().clear();
                break;

            default:
                break;
        }
    } else if (index == 1) {
    // --------- Button B: Track Switch, Mute/Unmute --------
    Track* track = nullptr; // set varibables outside of switch statement to safely use it as a pointer.
    switch (action) {
        case BUTTON_SHORT_PRESS: {
            // Switch track
            uint8_t newIndex = (trackManager.getSelectedTrackIndex() + 1) % trackManager.getTrackCount();
            trackManager.setSelectedTrack(newIndex);     
            if(DEBUG_BUTTONS) Serial.print("Button B short press: Switched to track ");
            if(DEBUG_BUTTONS) Serial.println(newIndex);
            }
            break;
        case BUTTON_LONG_PRESS: {
            track = &trackManager.getSelectedTrack();
            track->toggleMuteTrack();
            if(DEBUG_BUTTONS) Serial.print("Button B long press: Toggled mute on track ");
            if(DEBUG_BUTTONS) Serial.println(trackManager.getSelectedTrackIndex());
            }
            break;
        default:
            break;
      }
    }
}
