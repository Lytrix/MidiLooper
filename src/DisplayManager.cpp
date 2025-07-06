//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// DisplayManager.cpp
#include "DisplayManager.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"
#include "SSD1322_Config.h"
#include "TrackUndo.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "NoteEditManager.h"
#include <map>
#include <string>
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>

DisplayManager displayManager;

// Generic helper to draw a label:value field at (x, y) with optional highlight brightness
void DisplayManager::drawInfoField(const char* label, const char* value, int x, int y, bool highlight, uint8_t defaultBrightness = 5) {
    
    int labelLen = strlen(label);
    int labelWidth = labelLen * 6;
    int colonWidth = 6;

    uint8_t brightness = highlight ? 15 : defaultBrightness;
    // Always use mono font for info fields
    _display.gfx.select_font(&Font5x7Fixed);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), label, x, y, brightness / 3 + 2);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), ":", x+labelWidth, y, 4);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), value, x+labelWidth+colonWidth, y, brightness);
}

DisplayManager::DisplayManager() : _display() {
    // Don't initialize SPI here - it will be done in setup()
}

// Helper function to clear the display buffer
void DisplayManager::clearDisplayBuffer() {
    Serial.println("DisplayManager: Clearing display buffer");
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);
    Serial.println("DisplayManager: Display buffer cleared");
    Serial.println("DisplayManager: Displaying buffer");   
   _display.api.display();
    Serial.println("DisplayManager: Display buffer displayed");
}

// Helper: Map TrackState to status letter
static char trackStateToLetter(TrackState state, bool muted) {
    if (muted) return 'M';
    switch (state) {
        case TRACK_EMPTY:       return '-';
        case TRACK_RECORDING:   return 'R';
        case TRACK_PLAYING:     return 'P';
        case TRACK_OVERDUBBING: return 'O';
        case TRACK_STOPPED:     return 'S';
        case TRACK_ARMED:       return 'A';
        case TRACK_STOPPED_RECORDING: return 'r';
        default:                return '?';
    }
}

// Helper: Convert ticks to Bars:Beats:16th:Ticks string, with option to limit ticks to 2 decimals
static void ticksToBarsBeats16thTicks2Dec(uint32_t ticks, char* out, size_t outSize, bool leadingZeros = false) {
    uint32_t bar = ticks / Config::TICKS_PER_BAR + 1;
    uint32_t ticksInBar = ticks % Config::TICKS_PER_BAR;
    uint32_t beat = ticksInBar / Config::TICKS_PER_QUARTER_NOTE + 1;
    uint32_t ticksInBeat = ticksInBar % Config::TICKS_PER_QUARTER_NOTE;
    uint32_t sixteenthTicks = Config::TICKS_PER_QUARTER_NOTE / 4;
    uint32_t sixteenth = ticksInBeat / sixteenthTicks + 1;
    uint32_t ticksIn16th = ticksInBeat % sixteenthTicks;
    // Limit ticks to 2 decimals (max 99)
    uint32_t ticks2dec = (ticksIn16th > 99) ? 99 : ticksIn16th;
    if (leadingZeros) {
        snprintf(out, outSize, "%02lu:%02lu:%02lu:%02lu", bar, beat, sixteenth, ticks2dec);
    } else {
        snprintf(out, outSize, "%lu:%lu:%lu:%lu", bar, beat, sixteenth, ticks2dec);
    }
}

void DisplayManager::drawTrackStatus(uint8_t selectedTrack, uint32_t currentMillis) {
    // Update pulse phase for state of the selected track
    float dt = (now - _lastPulseUpdate) / 1000.0f;
    _pulsePhase += dt * PULSE_SPEED;
    if (_pulsePhase > 1.0f) _pulsePhase -= 1.0f;
    _lastPulseUpdate = now;
    
    // Font and layout
    _display.gfx.select_font(&Font5x7FixedMono);
    constexpr int x = 0; // left margin
    constexpr int char_height = 7; // Font5x7FixedMono is 7px high
    constexpr int trackCount = 8;
    constexpr int step = (DISPLAY_HEIGHT - char_height) / (trackCount - 1);


    for (uint8_t i = 0; i < trackCount; ++i) {
        char label[2] = {0};
        label[0] = trackStateToLetter(trackManager.getTrackState(i), !trackManager.isTrackAudible(i));
        int y = i * step + char_height;
        uint8_t brightness = 15;
        if (i == selectedTrack) {
            float phase = _pulsePhase;
            brightness = minPulse + (maxPulse - minPulse) * (0.5f + 0.5f * sinf(phase * 2 * 3.1415926f));
        } else {
            brightness = 8;
        }
        _display.gfx.draw_text(_display.api.getFrameBuffer(), label, x, y, brightness);
        // Draw track number next to state letter at 25% brightness
        char numStr[3];
        snprintf(numStr, sizeof(numStr), "%d", i + 1);
        uint8_t numBrightness = (i == selectedTrack) ? 15 : 4; // 100% if selected, else 25%
        _display.gfx.draw_text(_display.api.getFrameBuffer(), numStr, x + 10, y, numBrightness);
    }
}

void DisplayManager::setup() {
    Serial.println("DisplayManager: Setting up SSD1322 display...");

    // Initialize display
    _display.begin();

    // Set buffer size and clear display
    Serial.println("DisplayManager: Setting buffer size");
    _display.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    clearDisplayBuffer();

    // Now proceed with your drawing/demo code
    Serial.println("DisplayManager: Drawing startup text...");
    Serial.println("Selecting font...");
    _display.gfx.select_font(&Font5x7Fixed);
    Serial.println("Font selected.");
    Serial.println("Drawing text...");
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "MidiLooper", 50, 20, 15);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "v0.2", 80, 40, 8);
    Serial.println("Text drawn.");
    _display.api.display();
    Serial.println("DisplayManager: Text sent to display");
    delay(1000);
    clearDisplayBuffer();
}


int DisplayManager::tickToScreenX(uint32_t tick) {
    Track& track = trackManager.getSelectedTrack();
    uint32_t loopLength = track.getLoopLength();
    uint32_t loopStartTick = track.getLoopStartTick();
    
    // Adjust tick to be relative to loop start point
    uint32_t relativeTick = (tick >= loopStartTick) ? (tick - loopStartTick) : (tick + loopLength - loopStartTick);
    relativeTick = relativeTick % loopLength; // Ensure wrapping
    
    return TRACK_MARGIN + map(relativeTick, 0, loopLength, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
}

int DisplayManager::noteToScreenY(uint8_t note) {
    // Example: map MIDI note range to screen height
    int minNote = 36; // C2
    int maxNote = 84; // C6
    return DISPLAY_HEIGHT - ((note - minNote) * DISPLAY_HEIGHT) / (maxNote - minNote + 1);
}

// --- Helper: Draw grid lines (bars, beats, 16ths) ---
void DisplayManager::drawGridLines(uint32_t lengthLoop, int pianoRollY0, int pianoRollY1) {
    const int barBrightness = 3;      // 50%
    const int beatBrightness = 2;     // 25%
    const int sixteenthBrightness = 1;// 10%
    const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
    const uint32_t ticksPerBeat = Config::TICKS_PER_QUARTER_NOTE;
    const uint32_t ticksPerSixteenth = Config::TICKS_PER_QUARTER_NOTE / 4;
    // Bar lines
    for (uint32_t t = 0; t < lengthLoop; t += ticksPerBar) {
        int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), x, pianoRollY0, pianoRollY1, barBrightness);
    }
    // Beat lines
    bool showBeat = (lengthLoop <= 9 * ticksPerBar);
    if (showBeat) {
        for (uint32_t t = ticksPerBeat; t < lengthLoop; t += ticksPerBeat) {
            if (t % ticksPerBar == 0) continue;
            int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
            for (int y = pianoRollY0; y <= pianoRollY1; y += 2) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, y, beatBrightness);
            }
        }
    }
    // Sixteenth lines
    bool showSixteenth = (lengthLoop <= 5 * ticksPerBar);
    if (showSixteenth) {
        for (uint32_t t = ticksPerSixteenth; t < lengthLoop; t += ticksPerSixteenth) {
            if (t % ticksPerBar == 0 || t % ticksPerBeat == 0) continue;
            int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
            for (int y = pianoRollY0; y <= pianoRollY1; y += 4) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, y, sixteenthBrightness);
            }
        }
    }
}

// --- Helper: Draw all notes ---
void DisplayManager::drawAllNotes(const Track& track, uint32_t startLoop, uint32_t lengthLoop, int minPitch, int maxPitch) {
    const auto& notes = track.getCachedNotes();
    uint32_t loopStartTick = track.getLoopStartTick();
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    for (int i = 0; i < (int)notes.size(); i++) {
        const auto& n = notes[i];
        int noteBrightness = (i == selectedIdx) ? HIGHLIGHT_COLOR : 7;
        
        // Adjust note positions to be relative to loop start point
        uint32_t adjustedStartTick = (n.startTick >= loopStartTick) ? 
            (n.startTick - loopStartTick) : (n.startTick + lengthLoop - loopStartTick);
        adjustedStartTick = adjustedStartTick % lengthLoop;
        
        uint32_t adjustedEndTick = (n.endTick >= loopStartTick) ? 
            (n.endTick - loopStartTick) : (n.endTick + lengthLoop - loopStartTick);
        adjustedEndTick = adjustedEndTick % lengthLoop;
        
        int y = map(n.note, minPitch, maxPitch, 31, 0);
        y = constrain(y, 0, 31);
        
        drawNoteBar(n, y, adjustedStartTick, adjustedEndTick, lengthLoop, noteBrightness);
    }
}

// --- Helper: Draw bracket ---
void DisplayManager::drawBracket(uint32_t bracketTick, uint32_t lengthLoop, int pianoRollY1) {
    // Draw bracket when in NOTE_EDIT mode (simplified since we use dedicated faders)
    if (noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_NOTE_EDIT) {
        // Use the bracketTick parameter passed to this function (already adjusted for loop start)
        const int pianoRollY1 = 31;

        // Convert bracketTick to screen X position
        int bracketX = TRACK_MARGIN + map(bracketTick, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        // Draw bracket (e.g., vertical line or rectangle)
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), bracketX, 0, pianoRollY1, BRACKET_COLOR);
    }
}

// --- Helper: Draw a single note bar ---
void DisplayManager::drawNoteBar(const DisplayNote& e, int y, uint32_t s, uint32_t eTick, uint32_t lengthLoop, int noteBrightness) {
    
    // Check if this is a wrapped note:
    // 1. endTick < startTick (classic wrap case)
    // 2. endTick > loopLength (note extends beyond loop boundary)
    // Note: endTick == loopLength is NOT wrapped - it's a normal note ending at loop boundary
    bool isWrapped = (eTick < s) || (eTick > lengthLoop);
    
    if (!isWrapped && eTick >= s) {
        // Normal note within loop boundary
        int x0 = TRACK_MARGIN + map(s, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        int x1 = TRACK_MARGIN + map(eTick, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        if (x1 < x0) x1 = x0;
        _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, x1, y, noteBrightness);
    } else {
        // Wrapped note: draw two segments
        uint32_t wrappedEndTick = eTick % lengthLoop;
        
        // Calculate screen positions
        int x0 = TRACK_MARGIN + map(s % lengthLoop, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        int xEnd = TRACK_MARGIN + map(lengthLoop, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        int x1 = TRACK_MARGIN + map(0, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        int x2 = TRACK_MARGIN + map(wrappedEndTick, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
         
        // Draw from start to end of loop (segment 1)
        if (s % lengthLoop < lengthLoop) {
            _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, xEnd, y, noteBrightness);
        }
        
        // Draw from 0 to wrapped endTick (segment 2)
        if (wrappedEndTick > 0) {
            _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x1, y, x2, y, noteBrightness);
        }
    }
}

// --- Draw piano roll using cached notes ---
void DisplayManager::drawPianoRoll(uint32_t currentTick, Track& selectedTrack) {
    auto& track = selectedTrack;
    uint32_t startLoop = 0; // Always start at bar 1 visually
    uint32_t lengthLoop = track.getLoopLength();
    uint32_t loopStartTick = track.getLoopStartTick();

    //setLastPlayedNote(nullptr); // Reset at the start
    const int pianoRollY0 = 0;
    const int pianoRollY1 = 31;
    if (lengthLoop > 0) {
        // Calculate loop position relative to the loop start point
        uint32_t loopPos;
        if (currentTick >= loopStartTick) {
            loopPos = (currentTick - loopStartTick) % lengthLoop;
        } else {
            // Handle the case where currentTick is before the loop start
            uint32_t offset = loopStartTick - currentTick;
            loopPos = (lengthLoop - (offset % lengthLoop)) % lengthLoop;
        }

        // Compute min/max pitch for scaling
        int minPitch = 127;
        int maxPitch = 0;
        const auto& notes = track.getCachedNotes();
        for (const auto& n : notes) {
            if (n.note < minPitch) minPitch = n.note;
            if (n.note > maxPitch) maxPitch = n.note;
        }
        if (minPitch > maxPitch) { minPitch = 60; maxPitch = 72; } // fallback

        drawGridLines(lengthLoop, pianoRollY0, pianoRollY1);
        drawAllNotes(track, startLoop, lengthLoop, minPitch, maxPitch); // includes selected note
        
        // Adjust bracket tick to be relative to loop start point
        uint32_t bracketTick = editManager.getBracketTick();
        uint32_t relativeBracketTick = (bracketTick >= loopStartTick) ? 
            (bracketTick - loopStartTick) : (bracketTick + lengthLoop - loopStartTick);
        relativeBracketTick = relativeBracketTick % lengthLoop;
        
        drawBracket(relativeBracketTick, lengthLoop, pianoRollY1);
      
        // Draw playhead cursor at relative position
        int cx = TRACK_MARGIN + map(loopPos, 0, lengthLoop, 0, DISPLAY_WIDTH - 1 - TRACK_MARGIN);
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, 0, 32, 3);
    }
}

// Draw info area
void DisplayManager::drawInfoArea(uint32_t currentTick, Track& selectedTrack) {
    // 1. Current position (playhead) as musical time, with leading zeros and 2 decimals for ticks
    char posStr[24];
    char loopLine[8];
    // Get length of loop
    uint32_t lengthLoop = selectedTrack.getLoopLength();
    
    ticksToBarsBeats16thTicks2Dec(currentTick, posStr, sizeof(posStr), true); // true = leading zeros
    if (lengthLoop > 0 && Config::TICKS_PER_BAR > 0) {
        uint32_t bars = lengthLoop / Config::TICKS_PER_BAR;
        snprintf(loopLine, sizeof(loopLine), "%lu", bars);
    } else {
        snprintf(loopLine, sizeof(loopLine), "-");
    }
    // Draw position string
    int x = DisplayManager::TRACK_MARGIN;
    int y = DISPLAY_HEIGHT - 12;
    _display.gfx.select_font(&Font5x7FixedMono);
    int timeStrLen = strlen(posStr);
    for (int i = 0; i < timeStrLen; ++i) {
        char c[2] = {posStr[i], 0};
        // Dim ":" with 8/3 brightness, rest is 5
        uint8_t charBrightness = (c[0] == ':') ? 8/3 : 5;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), c, x + i * 6, y, charBrightness);
    }


    // Draw LOOP field
    
    int loopX = x + 12 * 6; // after posStr (11 chars + 1 space)
    drawInfoField("LOOP", loopLine, loopX, y, false, 5);
    // Draw undo count right-aligned, max 99
    uint8_t undoCount = static_cast<uint8_t>(editManager.getDisplayUndoCount(selectedTrack));
    char undoStr[4];
    if (undoCount == 0) {
        snprintf(undoStr, sizeof(undoStr), "--");
    } else {
        if (undoCount > 99) undoCount = 99;
        snprintf(undoStr, sizeof(undoStr), "%02u", undoCount);
    }
    int undoX = DISPLAY_WIDTH - 4 * 6; // right-aligned, enough space for "U:99"
    drawInfoField("U", undoStr, undoX, y, false, 5);
}

// --- Draw note info using cached notes ---
void DisplayManager::drawNoteInfo(uint32_t currentTick, Track& selectedTrack) {
    char startStr[24] = {0};
    uint32_t lengthLoop = selectedTrack.getLoopLength();
    uint32_t loopStartTick = selectedTrack.getLoopStartTick();
    const auto& notes = selectedTrack.getCachedNotes();

    const DisplayNote* noteToShow = nullptr;
    uint32_t displayStartTick = 0;
    int selectedIdx = editManager.getSelectedNoteIdx();
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        noteToShow = &notes[selectedIdx];
        // Adjust display start tick to be relative to loop start point
        displayStartTick = (noteToShow->startTick >= loopStartTick) ? 
            (noteToShow->startTick - loopStartTick) : (noteToShow->startTick + lengthLoop - loopStartTick);
        displayStartTick = displayStartTick % lengthLoop;
        // Since we use dedicated faders, we don't need complex edit state checks
        // Just use the adjusted note's start tick
    }
    
    if (!noteToShow && !notes.empty()) {
        if (editManager.getCurrentState() == nullptr) {
            // Adjust current tick to be relative to loop start point for playback detection
            uint32_t relativeCurrentTick;
            if (currentTick >= loopStartTick) {
                relativeCurrentTick = (currentTick - loopStartTick) % lengthLoop;
            } else {
                uint32_t offset = loopStartTick - currentTick;
                relativeCurrentTick = (lengthLoop - (offset % lengthLoop)) % lengthLoop;
            }
            
            for (const auto& n : notes) {
                // Adjust note positions to be relative to loop start point
                uint32_t s = (n.startTick >= loopStartTick) ? 
                    (n.startTick - loopStartTick) : (n.startTick + lengthLoop - loopStartTick);
                s = s % lengthLoop;
                
                uint32_t e = (n.endTick >= loopStartTick) ? 
                    (n.endTick - loopStartTick) : (n.endTick + lengthLoop - loopStartTick);
                e = e % lengthLoop;
                
                bool isPlaying = (s <= e)
                    ? (relativeCurrentTick >= s && relativeCurrentTick < e)
                    : (relativeCurrentTick >= s || relativeCurrentTick < e);
                if (isPlaying) {
                    noteToShow = &n;
                    displayStartTick = s;
                    break;
                }
            }
            if (!noteToShow) {
                noteToShow = &notes.back();
                displayStartTick = (noteToShow->startTick >= loopStartTick) ? 
                    (noteToShow->startTick - loopStartTick) : (noteToShow->startTick + lengthLoop - loopStartTick);
                displayStartTick = displayStartTick % lengthLoop;
            }
        } else {
            noteToShow = &notes.back();
            displayStartTick = (noteToShow->startTick >= loopStartTick) ? 
                (noteToShow->startTick - loopStartTick) : (noteToShow->startTick + lengthLoop - loopStartTick);
            displayStartTick = displayStartTick % lengthLoop;
        }
    }

    char noteStr[4] = "---";
    char lenStr[6] = "---";
    char velStr[4] = "---";
    bool validNote = false;
    if (noteToShow && lengthLoop > 0) {
        ticksToBarsBeats16thTicks2Dec(displayStartTick % lengthLoop, startStr, sizeof(startStr), true);
        uint8_t noteVal = noteToShow->note;
        // Calculate note length, handling wrap-around case
        uint32_t lenVal;
        if (noteToShow->endTick >= noteToShow->startTick) {
            lenVal = noteToShow->endTick - noteToShow->startTick;
        } else {
            // Wrapped note: endTick < startTick
            lenVal = (lengthLoop - noteToShow->startTick) + noteToShow->endTick;
        }
        uint8_t velVal = noteToShow->velocity;
        validNote = (noteVal <= 127 && velVal <= 127 && lenVal < 10000);
        if (validNote) {
            snprintf(noteStr, sizeof(noteStr), "%3u", noteVal);
            snprintf(lenStr, sizeof(lenStr), "%3lu", lenVal);
            snprintf(velStr, sizeof(velStr), "%3u", velVal);
        }
    }
    
    int x = DisplayManager::TRACK_MARGIN;
    int y = DISPLAY_HEIGHT;
    // Draw the time string (ticksToBarsBeats16thTicks2Dec)
    // Highlight the time when in NOTE_EDIT mode (simplified since we use dedicated faders)
    bool isStartNote = (noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_NOTE_EDIT);
    _display.gfx.select_font(&Font5x7FixedMono);
    int timeStrLen = strlen(startStr);
    for (int i = 0; i < timeStrLen; ++i) {
        char c[2] = {startStr[i], 0};
        uint8_t charBrightness = (c[0] == ':') ? 8/3 : (isStartNote ? 15 : 5);
        _display.gfx.draw_text(_display.api.getFrameBuffer(), c, x + i * 6, y, charBrightness);
    }
    // Draw NOTE, LEN, VEL fields using drawInfoField
    int infoX = x + timeStrLen * 6 + 6; // after time string
    // Highlight NOTE field when in NOTE_EDIT mode (simplified since we use dedicated faders)
    bool inPitchEdit = (noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_NOTE_EDIT);
    struct InfoField { const char* label; const char* value; bool highlight; };
    InfoField fields[] = {
        {"NOTE", noteStr, inPitchEdit},
        {"LEN", lenStr, false},
        {"VEL", velStr, false}
    };
    for (int i = 0; i < 3; ++i) {
        drawInfoField(fields[i].label, fields[i].value, infoX, y, fields[i].highlight, 5);
        infoX += strlen(fields[i].label) * 6 + 6 + strlen(fields[i].value) * 6 + 6; // label + colon + value + space
    }
}

void DisplayManager::update() {
    // Get current global tick count for display timing
    uint32_t currentTick = clockManager.getCurrentTick();
    uint32_t now = millis();

    // Clear frame buffer
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);

    // Draw vertical track status on the left
    drawTrackStatus(trackManager.getSelectedTrackIndex(), now);
    
    // Draw piano roll
    drawPianoRoll(currentTick, trackManager.getSelectedTrack());

    // Draw info area
    drawInfoArea(currentTick, trackManager.getSelectedTrack());

    // Draw note info
    drawNoteInfo(currentTick, trackManager.getSelectedTrack());

    // Send buffer to display
   _display.api.display();
}
