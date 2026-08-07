//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sidebar, track column, and bottom info strip drawing (Phase 4).

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "DeferredSaveDisplayStatus.h"
#include "Globals.h"
#include "MidiHandler.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "SSD1322_Config.h"
#include "StorageManager.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/SlotFocusDisplay.h"
#include <Arduino.h>
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace DisplayManagerInternal;

namespace {

const NoteUtils::DisplayNote* findProjectedPaintNoteForPrimary(
    const Track& track, NoteId primaryNote, NoteUtils::DisplayNote& out) {
  if (primaryNote == kInvalidNoteId || !editManager.isNoteEditActive()) {
    return nullptr;
  }
  const NoteUtils::DisplayNoteVec& paint = editManager.projectedNoteEditDisplayNotes(track);
  for (const NoteUtils::DisplayNote& dn : paint) {
    if (dn.noteId == primaryNote) {
      out = dn;
      return &out;
    }
  }
  return nullptr;
}

constexpr int SIDEBAR_WIDTH = 30;
constexpr int SIDEBAR_RIGHT_MARGIN = 1;
constexpr int SIDEBAR_SEPARATOR_BRIGHTNESS = 2;
constexpr int MODE_VALUE_BRIGHTNESS = 3;
constexpr int SIDEBAR_TEXT_BRIGHTNESS = 5;
constexpr int SIDEBAR_VALUE_BRIGHTNESS = 5;
constexpr int kSaveStatusDotY = 47;
constexpr int kSaveStatusDotSpacing = 2;
constexpr uint8_t kSaveStatusDimBrightness = 2;
constexpr uint8_t kSaveStatusActiveBrightness = 8;
constexpr uint8_t kSaveStatusCompletedBrightness = 15;
constexpr uint8_t kSaveStatusFailedBrightness = 6;
constexpr int kDetailedPianoRollRows = 32;
constexpr int kDetailedPianoRollY1 = kDetailedPianoRollRows - 1;
constexpr int kOverviewGapRows = 1;
constexpr int kOverviewStripRows = 8;
constexpr int kOverviewStripY0 = kDetailedPianoRollY1 + kOverviewGapRows + 1;
constexpr int kOverviewStripY1 = kOverviewStripY0 + kOverviewStripRows - 1;
constexpr int kPianoRollRegionBottomY = kOverviewStripY1;

char trackStateToLetter(TrackState state, bool muted) {
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

void ticksToBarsBeats16thTicks2Dec(uint32_t ticks, char* out, size_t outSize, bool leadingZeros = false) {
    uint32_t bar = ticks / Config::TICKS_PER_BAR + 1;
    uint32_t ticksInBar = ticks % Config::TICKS_PER_BAR;
    uint32_t beat = ticksInBar / Config::TICKS_PER_QUARTER_NOTE + 1;
    uint32_t ticksInBeat = ticksInBar % Config::TICKS_PER_QUARTER_NOTE;
    uint32_t sixteenthTicks = Config::TICKS_PER_QUARTER_NOTE / 4;
    uint32_t sixteenth = ticksInBeat / sixteenthTicks + 1;
    uint32_t ticksIn16th = ticksInBeat % sixteenthTicks;
    uint32_t ticks2dec = (ticksIn16th > 99) ? 99 : ticksIn16th;
    if (leadingZeros) {
        snprintf(out, outSize, "%02lu:%02lu:%02lu:%02lu", bar, beat, sixteenth, ticks2dec);
    } else {
        snprintf(out, outSize, "%lu:%lu:%lu:%lu", bar, beat, sixteenth, ticks2dec);
    }
}

void ticksToLaunchCountdownBarsBeats16thTicks2Dec(uint32_t remainingTicks, char* out,
                                                   size_t outSize) {
    const uint32_t bar = remainingTicks / Config::TICKS_PER_BAR;
    const uint32_t ticksInBar = remainingTicks % Config::TICKS_PER_BAR;
    const uint32_t beat = ticksInBar / Config::TICKS_PER_QUARTER_NOTE;
    const uint32_t ticksInBeat = ticksInBar % Config::TICKS_PER_QUARTER_NOTE;
    const uint32_t sixteenthTicks = Config::TICKS_PER_QUARTER_NOTE / 4;
    const uint32_t sixteenth = ticksInBeat / sixteenthTicks;
    const uint32_t ticksIn16th = ticksInBeat % sixteenthTicks;
    const uint32_t ticks2dec = (ticksIn16th > 99) ? 99 : ticksIn16th;
    if (remainingTicks == 0) {
        snprintf(out, outSize, "00:00:00:00");
        return;
    }
    snprintf(out, outSize, "-%02lu:%02lu:%02lu:%02lu", static_cast<unsigned long>(bar),
             static_cast<unsigned long>(beat), static_cast<unsigned long>(sixteenth),
             static_cast<unsigned long>(ticks2dec));
}

}  // namespace

void DisplayManager::drawInfoField(const char* label, const char* value, int x, int y, bool highlight, uint8_t defaultBrightness = 5) {
    int labelLen = strlen(label);
    int labelWidth = labelLen * 6;
    int colonWidth = 6;

    uint8_t brightness = highlight ? 15 : defaultBrightness;
    _display.gfx.select_font(&Font5x7Fixed);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), label, x, y, brightness / 3 + 2);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), ":", x+labelWidth, y, 4);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), value, x+labelWidth+colonWidth, y, brightness);
}

void DisplayManager::drawTrackStatus(uint8_t selectedTrack, uint32_t currentMillis) {
    float dt = (static_cast<long>(currentMillis) - static_cast<long>(_lastPulseUpdate)) / 1000.0f;
    if (dt < 0.0f) dt = 0.0f;
    _pulsePhase += dt * PULSE_SPEED;
    if (_pulsePhase > 1.0f) _pulsePhase -= 1.0f;
    _lastPulseUpdate = currentMillis;

    _display.gfx.select_font(&Font5x7FixedMono);
    constexpr int x = 0;
    constexpr int char_height = 7;
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
        char numStr[3];
        snprintf(numStr, sizeof(numStr), "%d", i + 1);
        uint8_t numBrightness = (i == selectedTrack) ? 15 : 4;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), numStr, x + 10, y, numBrightness);
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
    if (editManager.getEditSessionType() == EditSessionType::Loop) {
        return SidebarMode::LOOP_EDIT;
    }
    if (editManager.getEditSessionType() == EditSessionType::Note) {
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

    _display.gfx.draw_vline(_display.api.getFrameBuffer(), sidebarX - 1, 0, kPianoRollRegionBottomY,
                            SIDEBAR_SEPARATOR_BRIGHTNESS);

    static float displayedBpm = 0.0f;
    if (displayedBpm == 0.0f || fabsf(bpm - displayedBpm) >= 0.f) {
        displayedBpm = bpm;
    }

    const SidebarMode mode = resolveSidebarMode(selectedTrack, displaySlot);
    char modeTop[6] = "-";
    char modeBottom[6] = "-";
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

    const Loop& undoLoop = trackManager.getSelectedLoop(selectedTrack);
    uint8_t undoCount = static_cast<uint8_t>(editManager.getDisplayUndoCount(selectedTrack, undoLoop));
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
    constexpr int kThinDotAdvance = 1;
    const int wBpm = wWhole + kThinDotAdvance + 6;
    const int bpmStartX = textRight - wBpm;
    _display.gfx.draw_text(_display.api.getFrameBuffer(), wholeBuf, bpmStartX, bpmY, bpmBright);
    const int dotX = bpmStartX + wWhole;
    _display.gfx.draw_pixel(_display.api.getFrameBuffer(), dotX - 1, bpmY - 1, bpmBright);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), fracStr, dotX + kThinDotAdvance, bpmY, bpmBright);

    drawRight(modeTop, 17, MODE_VALUE_BRIGHTNESS);
    drawRight(modeBottom, 27, MODE_VALUE_BRIGHTNESS);

    const int undoY = 37;
    const int wUndoVal = static_cast<int>(strlen(undoValStr)) * 6;
    const bool sessionUndo = editManager.isSessionUndoDisplayActive();
    const int wPrefix = 2 * 6;
    const int undoValX = textRight - wUndoVal;
    const int undoPrefixX = undoValX - wPrefix;
    char prefixGlyph[2] = {sessionUndo ? 'E' : 'U', '\0'};
    char colonGlyph[2] = ":";
    _display.gfx.draw_text(_display.api.getFrameBuffer(), prefixGlyph, undoPrefixX, undoY,
                           MODE_VALUE_BRIGHTNESS);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), colonGlyph, undoPrefixX + 6, undoY,
                           MODE_VALUE_BRIGHTNESS);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), undoValStr, undoValX, undoY,
                           SIDEBAR_VALUE_BRIGHTNESS);

    drawSaveStatusIndicator(millis(), textRight);
}

void DisplayManager::drawPersistenceStatusDots(int startX, int dotY,
                                             const DeferredSaveDisplayStatus& status) {
    if (status.phase == DeferredSaveDisplayPhase::Idle) {
        return;
    }

    constexpr int kDotCount = 4;
    for (int i = 0; i < kDotCount; ++i) {
        uint8_t brightness = 0;
        switch (status.phase) {
            case DeferredSaveDisplayPhase::Pending:
                brightness = kSaveStatusDimBrightness;
                break;
            case DeferredSaveDisplayPhase::InProgress:
                brightness = (static_cast<int>(status.rotateStep) == i) ? kSaveStatusActiveBrightness
                                                                        : kSaveStatusDimBrightness;
                break;
            case DeferredSaveDisplayPhase::Completed:
                brightness = kSaveStatusCompletedBrightness;
                break;
            case DeferredSaveDisplayPhase::Failed:
                brightness = kSaveStatusFailedBrightness;
                break;
            default:
                break;
        }
        const int x = startX + i * (1 + kSaveStatusDotSpacing);
        _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, dotY, brightness);
    }
}

void DisplayManager::drawSaveStatusIndicator(uint32_t nowMs, int textRight) {
    const DeferredSaveDisplayStatus status = StorageManager::getDeferredSaveDisplayStatus(nowMs);
    static DeferredSaveDisplayPhase lastReportedPhase = DeferredSaveDisplayPhase::Idle;
    if (status.phase != lastReportedPhase) {
        SC_SAVE(deferredSaveDisplayPhaseName(status.phase), status.rotateStep);
        lastReportedPhase = status.phase;
    }

    if (status.phase == DeferredSaveDisplayPhase::Idle) {
        return;
    }

    constexpr int kDotCount = 4;
    constexpr int kTotalWidth = kDotCount + (kDotCount - 1) * kSaveStatusDotSpacing;
    const int startX = textRight - kTotalWidth + 1;
    drawPersistenceStatusDots(startX, kSaveStatusDotY, status);
}

void DisplayManager::drawInfoArea(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot,
                                  uint32_t nowMs) {
    (void)nowMs;
    char posStr[24];
    char lenStr[8];
    char loopStr[12];
    char chnStr[4];
    char midiOutLabel[8];
    const uint32_t lengthLoop = selectedTrack.isJamming() ? selectedTrack.getLoopLength()
                                                          : selectedTrack.getLoopLengthForSlot(displaySlot);

    const bool transportActive =
        selectedTrack.isPlaying() || selectedTrack.isOverdubbing();
    const uint8_t playingSlot = selectedTrack.getActiveLoopIndex();
    const uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    const bool queuedLaunchCountdown =
        isPreviewPlayheadPending(displaySlot, playingSlot, transportActive) &&
        trackManager.hasPendingSlotSwitch(trackIdx);

    if (queuedLaunchCountdown) {
        const Loop& playingLoop = selectedTrack.getLoop(playingSlot);
        const uint32_t remainingTicks = ticksRemainingUntilLoopEndLaunch(
            currentTick, selectedTrack.getProjectionCycleStartTick(),
            playingLoop.loopLengthTicks, playingLoop.loopStartTick);
        ticksToLaunchCountdownBarsBeats16thTicks2Dec(remainingTicks, posStr, sizeof(posStr));
    } else if (lengthLoop > 0) {
        const uint32_t playheadInLoop =
            resolvePlayheadInLoop(selectedTrack, displaySlot, currentTick);
        ticksToBarsBeats16thTicks2Dec(playheadInLoop, posStr, sizeof(posStr), true);
    } else {
        ticksToBarsBeats16thTicks2Dec(currentTick, posStr, sizeof(posStr), true);
    }
    if (lengthLoop > 0 && Config::TICKS_PER_BAR > 0) {
        uint32_t bars = lengthLoop / Config::TICKS_PER_BAR;
        snprintf(lenStr, sizeof(lenStr), " %02lu", bars > 99 ? 99UL : bars);
    } else {
        snprintf(lenStr, sizeof(lenStr), " --");
    }
    const uint8_t trackNumber = trackManager.getSelectedTrackIndex() + 1;
    const uint8_t loopNumber = displaySlot + 1;
    snprintf(loopStr, sizeof(loopStr), "%u.%u", trackNumber, loopNumber);
    snprintf(chnStr, sizeof(chnStr), "%02u", selectedTrack.getMidiChannel());

    const MidiOutput midiOut = resolveMidiOutput();
    snprintf(midiOutLabel, sizeof(midiOutLabel), "%s", midiOutputLabel(midiOut));
    constexpr int kTimeCharW = 6;
    constexpr int kSignColumns = 1;
    const char* timeDigits = posStr;
    char signChar = ' ';
    if (posStr[0] == '-') {
        signChar = '-';
        timeDigits = posStr + 1;
    }
    int x = DisplayManager::TRACK_MARGIN;
    int y = DISPLAY_HEIGHT - 12;
    _display.gfx.select_font(&Font5x7FixedMono);
    if (signChar != ' ') {
        char signBuf[2] = {signChar, 0};
        _display.gfx.draw_text(_display.api.getFrameBuffer(), signBuf, x, y, 5);
    }
    const int digitX = x + kSignColumns * kTimeCharW;
    const int timeStrLen = static_cast<int>(strlen(timeDigits));
    for (int i = 0; i < timeStrLen; ++i) {
        char c[2] = {timeDigits[i], 0};
        uint8_t charBrightness = (c[0] == ':') ? 8/3 : 5;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), c, digitX + i * kTimeCharW, y,
                               charBrightness);
    }

    int infoX = digitX + timeStrLen * kTimeCharW + kTimeCharW;
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

void DisplayManager::drawNoteInfo(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot, const DisplayNoteVec& notes) {
    char startStr[24] = {0};
    const uint32_t lengthLoop = resolveDisplayLoopLength(selectedTrack, displaySlot, currentTick);
    const uint32_t loopStartTick = selectedTrack.isJamming() ? selectedTrack.getLoopStartTick()
                                                             : resolveLoopOriginTick(selectedTrack, displaySlot);
    uint8_t currentTrackIdx = trackManager.getSelectedTrackIndex();

    const DisplayNote* noteToShow = nullptr;
    uint32_t displayStartTick = 0;
    const EditorSelection& selection = editManager.getNoteEditSessionState().selection;
    int selectedIdx = -1;
    if (editManager.getEditSessionType() == EditSessionType::Note &&
        editorSelectionHasNote(selection)) {
        const uint32_t bracketDisplayTick =
            resolveBracketDisplayTick(loopStartTick, lengthLoop);
        selectedIdx = resolveDrawHighlightIndex(
            notes, selection, loopStartTick, lengthLoop, false, 0, bracketDisplayTick);
    } else {
        selectedIdx = editManager.getSelectedNoteIdx();
    }
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        noteToShow = &notes[static_cast<size_t>(selectedIdx)];
        const uint32_t storageBracketTick = editManager.isLengthBracketEditActive()
                                                ? noteToShow->endTick
                                                : noteToShow->startTick;
        displayStartTick = NoteEditDisplaySnapshot::displayStartTickFromStorage(
            storageBracketTick, loopStartTick, lengthLoop);
    }

    NoteUtils::DisplayNote projectedPrimarySpan{};
    if (editorSelectionHasNote(selection)) {
        if (const NoteUtils::DisplayNote* projected =
                findProjectedPaintNoteForPrimary(selectedTrack, selection.primaryNote,
                                                 projectedPrimarySpan)) {
            // Stage 8 / V5: sidebar LEN + DNTE use paint-cache participant span (same as grid).
            noteToShow = projected;
            const uint32_t storageBracketTick = editManager.isLengthBracketEditActive()
                                                    ? projected->endTick
                                                    : projected->startTick;
            displayStartTick = NoteEditDisplaySnapshot::displayStartTickFromStorage(
                storageBracketTick, loopStartTick, lengthLoop);
        }
    }

    DisplayNote focusDisplayFallback{};
    if (!noteToShow && editManager.isNoteEditActive()) {
        const NoteEditFocus& focus = editManager.getEditSession().focus;
        if (focus.active && editorSelectionHasNote(selection) &&
            focus.movingNoteId == selection.primaryNote) {
            focusDisplayFallback = {focus.movingNoteId, focus.last.pitch, focus.last.velocity,
                                    focus.last.startTick, focus.last.endTick};
            noteToShow = &focusDisplayFallback;
            const uint32_t storageBracketTick = editManager.isLengthBracketEditActive()
                                                    ? focus.last.endTick
                                                    : focus.last.startTick;
            displayStartTick = NoteEditDisplaySnapshot::displayStartTickFromStorage(
                storageBracketTick, loopStartTick, lengthLoop);
        }
    }

    if (!noteToShow && !notes.empty()) {
        if (editManager.getCurrentState() == nullptr) {
            const bool transportActive =
                selectedTrack.isPlaying() || selectedTrack.isOverdubbing();
            const uint8_t playingSlot = selectedTrack.getActiveLoopIndex();
            if (!isPreviewPlayheadPending(displaySlot, playingSlot, transportActive)) {
                const uint32_t relativeCurrentTick =
                    resolvePlayheadInLoop(selectedTrack, displaySlot, currentTick);

                for (const auto& n : notes) {
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
            }
            if (!noteToShow) {
                if (lastPlayedTrackIndex == currentTrackIdx && lastPlayedDisplayNote.note != 0) {
                    noteToShow = &lastPlayedDisplayNote;
                } else {
                    noteToShow = &notes.back();
                }
                displayStartTick = (noteToShow->startTick >= loopStartTick) ?
                    (noteToShow->startTick - loopStartTick) : (noteToShow->startTick + lengthLoop - loopStartTick);
                displayStartTick = displayStartTick % lengthLoop;
            }
        } else if (!(editManager.isNoteEditActive() && editManager.getEditSession().focus.active)) {
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
        uint8_t noteVal = noteToShow->note;
        const uint32_t endExclusive =
            (noteToShow->endTick == lengthLoop - 1) ? lengthLoop : noteToShow->endTick;
        uint32_t lenVal =
            NoteMovementUtils::calculateNoteLength(noteToShow->startTick, endExclusive, lengthLoop);
        uint8_t velVal = noteToShow->velocity;
        ticksToBarsBeats16thTicks2Dec(displayStartTick % lengthLoop, startStr, sizeof(startStr), true);
        validNote = (noteVal <= 127 && velVal <= 127 && lenVal < 10000);
        if (validNote) {
            snprintf(noteStr, sizeof(noteStr), "%3u", noteVal);
            snprintf(lenStr, sizeof(lenStr), "%3lu", lenVal);
            snprintf(velStr, sizeof(velStr), "%3u", velVal);
        }
#if defined(SESSION_CAPTURE)
        if (validNote &&
            editManager.getEditSessionType() == EditSessionType::Note &&
            selectedIdx >= 0) {
            static uint8_t capLastPitch = 255;
            static uint32_t capLastStorage = UINT32_MAX;
            static uint32_t capLastDisplay = UINT32_MAX;
            static uint32_t capLastLength = UINT32_MAX;
            const uint32_t storageStart = noteToShow->startTick;
            if (noteVal != capLastPitch || storageStart != capLastStorage ||
                displayStartTick != capLastDisplay || lenVal != capLastLength) {
                capLastPitch = noteVal;
                capLastStorage = storageStart;
                capLastDisplay = displayStartTick;
                capLastLength = lenVal;
                SC_DNTE(noteVal, storageStart, displayStartTick, lenVal, selectedIdx);
            }
        }
#endif
    }

    if (!(noteToShow && lengthLoop > 0)) {
        snprintf(startStr, sizeof(startStr), "--:--:--:--");
    }

    int x = DisplayManager::TRACK_MARGIN;
    int y = DISPLAY_HEIGHT;
    constexpr int kTimeCharW = 6;
    constexpr int kSignColumns = 1;
    bool isStartNote = (editManager.getEditSessionType() == EditSessionType::Note);
    _display.gfx.select_font(&Font5x7FixedMono);
    const int digitX = x + kSignColumns * kTimeCharW;
    const int timeStrLen = static_cast<int>(strlen(startStr));
    for (int i = 0; i < timeStrLen; ++i) {
        char c[2] = {startStr[i], 0};
        uint8_t charBrightness = (c[0] == ':') ? 8/3 : (isStartNote ? 15 : 5);
        _display.gfx.draw_text(_display.api.getFrameBuffer(), c, digitX + i * kTimeCharW, y,
                               charBrightness);
    }
    int infoX = digitX + timeStrLen * kTimeCharW + kTimeCharW;
    struct InfoField { const char* label; const char* value; bool highlight; };
    InfoField fields[] = {
        {"NOTE", noteStr, false},
        {"VEL", velStr, false},
        {"LEN", lenStr, false}
    };
    for (int i = 0; i < 3; ++i) {
        drawInfoField(fields[i].label, fields[i].value, infoX, y, fields[i].highlight, 5);
        infoX += strlen(fields[i].label) * 6 + 6 + strlen(fields[i].value) * 6 + 6;
    }
}
