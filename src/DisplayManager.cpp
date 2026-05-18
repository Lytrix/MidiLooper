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
#include "TickPhase.h"
#include "NoteEditManager.h"
#include "MidiHandler.h"
#include <map>
#include <string>
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>

DisplayManager displayManager;
namespace {
// Minimal gutter for longest line ("OVERD" = 30px) + 1px separator; content is right-aligned to display edge.
constexpr int SIDEBAR_WIDTH = 30;
constexpr int SIDEBAR_RIGHT_MARGIN = 1;
constexpr int SIDEBAR_SEPARATOR_BRIGHTNESS = 2;
constexpr int MODE_VALUE_BRIGHTNESS = 3; // match brightness of bottom-strip labels
constexpr int SIDEBAR_TEXT_BRIGHTNESS = 5;
constexpr int SIDEBAR_VALUE_BRIGHTNESS = 5; // match LEN / numeric field values in drawInfoField
constexpr int pianoRollRightX() { return DISPLAY_WIDTH - SIDEBAR_WIDTH - 1; }
constexpr int pianoRollWidth() { return pianoRollRightX() - DisplayManager::TRACK_MARGIN; }
}

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

bool DisplayManager::isLiveRecordingDisplay(const Track& track, uint8_t displaySlot) const {
    return !track.isJamming() && track.isRecording() && track.getRecordingFocusSlot() == displaySlot;
}

uint32_t DisplayManager::resolveDisplayLoopLength(const Track& track, uint8_t displaySlot, uint32_t currentTick) const {
    if (track.isJamming()) {
        return track.getLoopLength();
    }

    const uint32_t loopLength = track.getLoopLengthForSlot(displaySlot);
    if (loopLength > 0) {
        return loopLength;
    }

    if (!isLiveRecordingDisplay(track, displaySlot)) {
        return 0;
    }

    const Loop& loop = track.getLoop(displaySlot);
    if (currentTick <= loop.startLoopTick) {
        return 1;
    }
    return currentTick - loop.startLoopTick;
}

uint32_t DisplayManager::resolveDisplayTick(const Track& track, uint8_t displaySlot, uint32_t currentTick) const {
    if (!isLiveRecordingDisplay(track, displaySlot)) {
        return currentTick;
    }

    const Loop& loop = track.getLoop(displaySlot);
    if (currentTick <= loop.startLoopTick) {
        return 0;
    }
    return currentTick - loop.startLoopTick;
}

const std::vector<DisplayNote>& DisplayManager::resolveDisplayNotes(const Track& track, uint8_t displaySlot,
                                                                    uint32_t currentTick) {
    if (track.isJamming()) {
        return track.getCachedNotes();
    }

    if (!isLiveRecordingDisplay(track, displaySlot)) {
        return track.getCachedNotesForSlot(displaySlot);
    }

    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t liveLoopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    // Same cache as non-live paths: avoids duplicate full reconstructNotes in one frame
    // (drawPianoRoll + drawNoteInfo). During first record, liveLoopLength changes often → natural misses.
    return loop.getNoteCache().getNotes(loop.midiEvents, liveLoopLength);
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
    float dt = (static_cast<long>(currentMillis) - static_cast<long>(_lastPulseUpdate)) / 1000.0f;
    if (dt < 0.0f) dt = 0.0f;
    _pulsePhase += dt * PULSE_SPEED;
    if (_pulsePhase > 1.0f) _pulsePhase -= 1.0f;
    _lastPulseUpdate = currentMillis;
    
    // Font and layout
    _display.gfx.select_font(&Font5x7FixedMono);
    constexpr int x = 0; // left margin
    constexpr int char_height = 7; // Font5x7FixedMono is 7px high
    constexpr int trackCount = 8;
    constexpr int step = (DISPLAY_HEIGHT - char_height) / (trackCount - 1);


    for (uint8_t i = 0; i < trackCount; ++i) {
        char label[2] = {0};
        Track& rowTrack = trackManager.getTrack(i);
        const bool userMuted = rowTrack.isMuted();
        const bool soloHidden =
            trackManager.anyTrackSoloed() && !trackManager.isTrackSoloed(i) && (i != selectedTrack);
        const bool showMuteOverlay = userMuted || soloHidden;
        label[0] = trackStateToLetter(trackManager.getTrackState(i), showMuteOverlay);
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
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Midi Looper v0.4", 92, 32, 15);
    //_display.gfx.draw_text(_display.api.getFrameBuffer(), "v0.4", 92, 40, 8);
    Serial.println("Text drawn.");
    _display.api.display();
    Serial.println("DisplayManager: Text sent to display");
    delay(1500);
    clearDisplayBuffer();
}


int DisplayManager::tickToScreenX(uint32_t tick) {
    Track& track = trackManager.getSelectedTrack();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    const uint32_t loopLength = track.isJamming() ? track.getLoopLength()
                                                   : track.getLoopLengthForSlot(displaySlot);
    const uint32_t loopStartTick = track.isJamming() ? track.getLoopStartTick()
                                                     : track.getLoopStartTickForSlot(displaySlot);
    
    // Adjust tick to be relative to loop start point
    uint32_t relativeTick = (tick >= loopStartTick) ? (tick - loopStartTick) : (tick + loopLength - loopStartTick);
    relativeTick = relativeTick % loopLength; // Ensure wrapping
    
    return TRACK_MARGIN + map(relativeTick, 0, loopLength, 0, pianoRollWidth());
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
        int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, pianoRollWidth());
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), x, pianoRollY0, pianoRollY1, barBrightness);
    }
    // Beat lines
    bool showBeat = (lengthLoop <= 9 * ticksPerBar);
    if (showBeat) {
        for (uint32_t t = ticksPerBeat; t < lengthLoop; t += ticksPerBeat) {
            if (t % ticksPerBar == 0) continue;
            int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, pianoRollWidth());
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
            int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, pianoRollWidth());
            for (int y = pianoRollY0; y <= pianoRollY1; y += 4) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, y, sixteenthBrightness);
            }
        }
    }
}

// --- Helper: Draw all notes ---
void DisplayManager::drawAllNotes(const Track& track, uint8_t displaySlot, uint32_t currentTick, uint32_t /*startLoop*/, uint32_t lengthLoop, int minPitch, int maxPitch,
                                  const std::vector<DisplayNote>& notes) {
    const uint32_t loopLength = track.isJamming() ? track.getLoopLength()
                                                  : resolveDisplayLoopLength(track, displaySlot, currentTick);
    const uint32_t jamStartTick = track.isJamming() ? track.getJamStartTick() : track.getLoopStartTickForSlot(displaySlot);
    int selectedIdx = editManager.getSelectedNoteIdx();

    for (int i = 0; i < (int)notes.size(); i++) {
        const auto& n = notes[i];
        int noteBrightness = (i == selectedIdx) ? HIGHLIGHT_COLOR : 7;

        // Adjust note positions relative to jam start, using loopLength for wrapping
        uint32_t adjustedStartTick = (n.startTick - jamStartTick + loopLength) % loopLength;
        uint32_t adjustedEndTick = (n.endTick - jamStartTick + loopLength) % loopLength;

        // Skip notes entirely outside the jam window
        if (adjustedStartTick >= lengthLoop && adjustedEndTick >= lengthLoop) continue;

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
        int bracketX = TRACK_MARGIN + map(bracketTick, 0, lengthLoop, 0, pianoRollWidth());
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
        int x0 = TRACK_MARGIN + map(s, 0, lengthLoop, 0, pianoRollWidth());
        int x1 = TRACK_MARGIN + map(eTick, 0, lengthLoop, 0, pianoRollWidth());
        if (x1 < x0) x1 = x0;
        _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, x1, y, noteBrightness);
    } else {
        // Wrapped note: draw two segments
        uint32_t wrappedEndTick = eTick % lengthLoop;
        
        // Calculate screen positions
        int x0 = TRACK_MARGIN + map(s % lengthLoop, 0, lengthLoop, 0, pianoRollWidth());
        int xEnd = TRACK_MARGIN + map(lengthLoop, 0, lengthLoop, 0, pianoRollWidth());
        int x1 = TRACK_MARGIN + map(0, 0, lengthLoop, 0, pianoRollWidth());
        int x2 = TRACK_MARGIN + map(wrappedEndTick, 0, lengthLoop, 0, pianoRollWidth());
         
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
void DisplayManager::drawPianoRoll(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot, const std::vector<DisplayNote>& notes) {
    auto& track = selectedTrack;
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const uint32_t jamLength = track.isJamming() ? track.getJamLength()
                                                 : loopLength;
    const uint32_t jamStartTick = track.isJamming() ? track.getJamStartTick()
                                                    : track.getLoopStartTickForSlot(displaySlot);
    const uint32_t displayTick = resolveDisplayTick(track, displaySlot, currentTick);

    const int pianoRollY0 = 0;
    const int pianoRollY1 = 31;
    if (loopLength > 0) {
        // Same phase as playback: tickPhaseInLoop(transport, loop anchor, length) — not raw % length.
        const Loop& dispLoop = track.getLoop(displaySlot);
        uint32_t tickInLoop = tickPhaseInLoop(displayTick, dispLoop.startLoopTick, loopLength);
        uint32_t jamPos = (tickInLoop - jamStartTick + loopLength) % loopLength;

        // Compute min/max pitch for scaling
        int minPitch = 127;
        int maxPitch = 0;
        for (const auto& n : notes) {
            if (n.note < minPitch) minPitch = n.note;
            if (n.note > maxPitch) maxPitch = n.note;
        }
        if (minPitch > maxPitch) { minPitch = 60; maxPitch = 72; } // fallback

        drawGridLines(jamLength, pianoRollY0, pianoRollY1);
        drawAllNotes(track, displaySlot, currentTick, 0, jamLength, minPitch, maxPitch, notes);

        // Adjust bracket tick to be relative to jam start
        uint32_t bracketTick = editManager.getBracketTick();
        uint32_t relativeBracketTick = (bracketTick - jamStartTick + loopLength) % loopLength;
        if (relativeBracketTick < jamLength) {
            drawBracket(relativeBracketTick, jamLength, pianoRollY1);
        }

        // Draw playhead cursor if within jam window
        if (jamPos < jamLength) {
            int cx = TRACK_MARGIN + map(jamPos, 0, jamLength, 0, pianoRollWidth());
            _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, 0, 32, 3);
        }
    }
}

DisplayManager::SidebarMode DisplayManager::resolveSidebarMode(const Track& selectedTrack, uint8_t displaySlot) const {
    const SlotOpState slotState = selectedTrack.getSlotOpState(displaySlot);
    if (slotState == SlotOpState::SLOT_OP_RECORDING || selectedTrack.getState() == TRACK_RECORDING) {
        return SidebarMode::REC;
    }
    if (slotState == SlotOpState::SLOT_OP_OVERDUBBING || selectedTrack.getState() == TRACK_OVERDUBBING) {
        return SidebarMode::OVERD;
    }
    if (noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_LOOP_EDIT) {
        return SidebarMode::LOOP_EDIT;
    }
    if (noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_NOTE_EDIT) {
        return SidebarMode::NOTE_EDIT;
    }
    if (selectedTrack.getState() == TRACK_PLAYING) {
        return SidebarMode::PLAY;
    }
    if (selectedTrack.getState() == TRACK_STOPPED || selectedTrack.getState() == TRACK_STOPPED_RECORDING || selectedTrack.getState() == TRACK_ARMED) {
        return SidebarMode::STOP;
    }
    return SidebarMode::EMPTY_STATE;
}

const char* DisplayManager::sidebarModeLabel(SidebarMode mode) const {
    switch (mode) {
        case SidebarMode::LOOP_EDIT:  return "LOOP EDIT";
        case SidebarMode::NOTE_EDIT:  return "NOTE EDIT";
        case SidebarMode::REC:        return "REC";
        case SidebarMode::OVERD:      return "OVERD";
        case SidebarMode::PLAY:       return "PLAY";
        case SidebarMode::STOP:       return "STOP";
        case SidebarMode::EMPTY_STATE:return "-";
        default:                      return "-";
    }
}

DisplayManager::MidiOutput DisplayManager::resolveMidiOutput() const {
    // Deterministic precedence: if DIN/Serial is enabled, display MID1; otherwise display USB1.
    if (midiHandler.isOutputSerialEnabled()) return MidiOutput::MID1;
    return MidiOutput::USB1;
}

const char* DisplayManager::midiOutputLabel(MidiOutput out) const {
    switch (out) {
        case MidiOutput::USB1: return "USB1";
        case MidiOutput::MID1: return "MID1";
        default:               return "USB1";
    }
}

void DisplayManager::drawSidebar(Track& selectedTrack, uint8_t displaySlot) {
    _display.gfx.select_font(&Font5x7FixedMono);
    const int sidebarX = DISPLAY_WIDTH - SIDEBAR_WIDTH;

    // Keep the separator tall enough to stay visually tied to the piano roll region.
    _display.gfx.draw_vline(_display.api.getFrameBuffer(), sidebarX - 1, 0, 39, SIDEBAR_SEPARATOR_BRIGHTNESS);

    static float displayedBpm = 0.0f;
    if (displayedBpm == 0.0f || fabsf(bpm - displayedBpm) >= 0.f) {
        displayedBpm = bpm;
    }

    const SidebarMode mode = resolveSidebarMode(selectedTrack, displaySlot);
    char modeTop[6] = "-";   // max 5 chars
    char modeBottom[6] = "-";// max 5 chars
    switch (mode) {
        case SidebarMode::LOOP_EDIT:  strcpy(modeTop, "LOOP"); strcpy(modeBottom, "EDIT"); break;
        case SidebarMode::NOTE_EDIT:  strcpy(modeTop, "NOTE"); strcpy(modeBottom, "EDIT"); break;
        case SidebarMode::REC:        strcpy(modeTop, "REC");  strcpy(modeBottom, "-");    break;
        case SidebarMode::OVERD:      strcpy(modeTop, "OVER");strcpy(modeBottom, "DUB");    break;
        case SidebarMode::PLAY:       strcpy(modeTop, "PLAY"); strcpy(modeBottom, "-");    break;
        case SidebarMode::STOP:       strcpy(modeTop, "STOP"); strcpy(modeBottom, "-");    break;
        case SidebarMode::EMPTY_STATE:strcpy(modeTop, "-");    strcpy(modeBottom, "-");     break;
        default:                      strcpy(modeTop, "-");    strcpy(modeBottom, "-");     break;
    }

    uint8_t undoCount = static_cast<uint8_t>(editManager.getDisplayUndoCount(selectedTrack));
    if (undoCount > 99) undoCount = 99;
    char undoValStr[4];
    if (undoCount == 0) {
        strcpy(undoValStr, "--");
    } else {
        snprintf(undoValStr, sizeof(undoValStr), "%02u", undoCount);
    }

    const int textRight = static_cast<int>(DISPLAY_WIDTH) - SIDEBAR_RIGHT_MARGIN;
    auto drawRight = [&](const char* txt, int y, uint8_t brightness) {
        const int w = static_cast<int>(strlen(txt)) * 6;
        const int x = textRight - w;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), txt, x, y, brightness);
    };

    // BPM: one decimal place. Use a real '.' + full glyph advance so "100.0" cannot read as "1000".
    const int bpmY = 7;
    const uint8_t bpmBright = SIDEBAR_TEXT_BRIGHTNESS;
    char wholeBuf[12];
    const int roundedTenths = static_cast<int>(displayedBpm * 10.0f + 0.5f);
    int whole = roundedTenths / 10;
    int tenth = roundedTenths % 10;
    if (tenth < 0) {
        tenth = 0;
        whole = 0;
    }
    snprintf(wholeBuf, sizeof(wholeBuf), "%d", whole);
    char fracStr[2] = { static_cast<char>('0' + tenth), '\0' };
    const int wWhole = static_cast<int>(strlen(wholeBuf)) * 6;
    constexpr int kGlyph = 6;
    const int wBpm = wWhole + kGlyph + kGlyph;
    const int bpmStartX = textRight - wBpm;
    _display.gfx.draw_text(_display.api.getFrameBuffer(), wholeBuf, bpmStartX, bpmY, bpmBright);
    const int dotX = bpmStartX + wWhole;
    const uint8_t dotBright = (bpmBright * 2) / 3;
    _display.gfx.draw_text(_display.api.getFrameBuffer(), ".", dotX, bpmY, dotBright);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), fracStr, dotX + kGlyph, bpmY, bpmBright);

    drawRight(modeTop, 17, MODE_VALUE_BRIGHTNESS);
    drawRight(modeBottom, 27, MODE_VALUE_BRIGHTNESS);

    // "U:" label dim like other sidebar labels; digits same brightness as LEN values.
    const int undoY = 37;
    const int wUndoVal = static_cast<int>(strlen(undoValStr)) * 6;
    const int wUcolon = 2 * 6; // "U" + ":"
    const int undoValX = textRight - wUndoVal;
    const int undoPrefixX = undoValX - wUcolon;
    char uGlyph[2] = "U";
    char colonGlyph[2] = ":";
    _display.gfx.draw_text(_display.api.getFrameBuffer(), uGlyph, undoPrefixX, undoY, MODE_VALUE_BRIGHTNESS);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), colonGlyph, undoPrefixX + 6, undoY, MODE_VALUE_BRIGHTNESS);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), undoValStr, undoValX, undoY, SIDEBAR_VALUE_BRIGHTNESS);
}

// Draw info area
void DisplayManager::drawInfoArea(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot) {
    // 1. Current position (playhead) as musical time, with leading zeros and 2 decimals for ticks
    char posStr[24];
    char lenStr[8];
    char loopStr[12];
    char chnStr[4];
    char midiOutLabel[8];
    // Get length of loop (selected slot when not in jam overlay)
    const uint32_t lengthLoop = selectedTrack.isJamming() ? selectedTrack.getLoopLength()
                                                          : selectedTrack.getLoopLengthForSlot(displaySlot);
    
    ticksToBarsBeats16thTicks2Dec(currentTick, posStr, sizeof(posStr), true); // true = leading zeros
    if (lengthLoop > 0 && Config::TICKS_PER_BAR > 0) {
        uint32_t bars = lengthLoop / Config::TICKS_PER_BAR;
        snprintf(lenStr, sizeof(lenStr), " %02lu", bars > 99 ? 99UL : bars); // leading space for nicer LEN spacing
    } else {
        snprintf(lenStr, sizeof(lenStr), " --");
    }
    const uint8_t trackNumber = trackManager.getSelectedTrackIndex() + 1;
    const uint8_t loopNumber = displaySlot + 1;
    // LOOP: t.l = track index + loop slot (no trailing padding so MID1/LEN stay aligned with row below)
    snprintf(loopStr, sizeof(loopStr), "%u.%u", trackNumber, loopNumber);
    snprintf(chnStr, sizeof(chnStr), "%02u", selectedTrack.getMidiChannel());

    const MidiOutput midiOut = resolveMidiOutput();
    snprintf(midiOutLabel, sizeof(midiOutLabel), "%s", midiOutputLabel(midiOut));
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

    // Draw LOOP / MIDx / LEN fields (bottom info strip)
    int infoX = x + timeStrLen * 6 + 6; // after time string
    struct InfoField { const char* label; const char* value; bool highlight; };
    InfoField fields[] = {
        {"LOOP", loopStr, false},
        {midiOutLabel, chnStr, false},
        {"LEN", lenStr, false}
    };

    for (int i = 0; i < 3; ++i) {
        drawInfoField(fields[i].label, fields[i].value, infoX, y, fields[i].highlight, 5);
        infoX += strlen(fields[i].label) * 6 + 6 + strlen(fields[i].value) * 6 + 6;
    }
}

// --- Draw note info using cached notes ---
void DisplayManager::drawNoteInfo(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot, const std::vector<DisplayNote>& notes) {
    char startStr[24] = {0};
    const uint32_t lengthLoop = resolveDisplayLoopLength(selectedTrack, displaySlot, currentTick);
    const uint32_t loopStartTick = selectedTrack.isJamming() ? selectedTrack.getLoopStartTick()
                                                             : selectedTrack.getLoopStartTickForSlot(displaySlot);
    const uint32_t displayTick = resolveDisplayTick(selectedTrack, displaySlot, currentTick);
    uint8_t currentTrackIdx = trackManager.getSelectedTrackIndex();

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
            // Phase within loop storage (matches playMidiEvents / tickPhaseInLoop), then offset by loop
            // bracket (loopStartTick) for the same coordinates as note s/e below.
            const Loop& dispLoop = selectedTrack.getLoop(displaySlot);
            uint32_t tickInLoopStorage = tickPhaseInLoop(displayTick, dispLoop.startLoopTick, lengthLoop);
            uint32_t relativeCurrentTick = (tickInLoopStorage - loopStartTick + lengthLoop) % lengthLoop;
            
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
                    lastPlayedDisplayNote = n;
                    lastPlayedTrackIndex = currentTrackIdx;
                    break;
                }
            }
            if (!noteToShow) {
                // No note playing: prefer last-played note (stickiness) over jumping to notes.back()
                if (lastPlayedTrackIndex == currentTrackIdx && lastPlayedDisplayNote.note != 0) {
                    noteToShow = &lastPlayedDisplayNote;
                } else {
                    noteToShow = &notes.back();
                }
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

    // Same width as leading-zero musical time (e.g. 01:01:01:00); keeps NOTE row layout when no note/slot data.
    if (!(noteToShow && lengthLoop > 0)) {
        snprintf(startStr, sizeof(startStr), "--:--:--:--");
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
    // Draw NOTE, VEL, LEN fields using drawInfoField (requested order)
    int infoX = x + timeStrLen * 6 + 6; // after time string
    struct InfoField { const char* label; const char* value; bool highlight; };
    InfoField fields[] = {
        // Keep NOTE brightness consistent even while editing (edit visuals are provided by the bracket/cursor)
        {"NOTE", noteStr, false},
        {"VEL", velStr, false},
        {"LEN", lenStr, false}
    };
    for (int i = 0; i < 3; ++i) {
        drawInfoField(fields[i].label, fields[i].value, infoX, y, fields[i].highlight, 5);
        infoX += strlen(fields[i].label) * 6 + 6 + strlen(fields[i].value) * 6 + 6; // label + colon + value + space
    }
}

void DisplayManager::update() {
    uint32_t currentTick = clockManager.getCurrentTick();
    Track& selTrack = trackManager.getSelectedTrack();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    uint32_t displayTick = selTrack.getEffectivePlaybackTick(currentTick);
    uint32_t now = millis();

    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);

    drawTrackStatus(trackManager.getSelectedTrackIndex(), now);
    const std::vector<DisplayNote>& frameNotes = resolveDisplayNotes(selTrack, displaySlot, displayTick);
    drawPianoRoll(displayTick, selTrack, displaySlot, frameNotes);
    drawSidebar(selTrack, displaySlot);
    drawInfoArea(displayTick, selTrack, displaySlot);
    drawNoteInfo(displayTick, selTrack, displaySlot, frameNotes);

   _display.api.display();
}
