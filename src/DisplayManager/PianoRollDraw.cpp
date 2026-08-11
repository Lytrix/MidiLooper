//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Piano roll drawing: grid, notes, overview strip, playhead, window geometry (Phase 3).

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "ClockManager.h"
#include "Globals.h"
#include "Loop.h"
#include "MidiButtonManager.h"
#include "MidiConfig.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "SSD1322_Config.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteUtils.h"
#include "Utils/SlotFocusDisplay.h"
#include <Arduino.h>
#include <algorithm>

using namespace DisplayManagerInternal;

namespace {

constexpr int kSidebarWidth = 30;

constexpr int kDetailedPianoRollRows = 32;
constexpr int kDetailedPianoRollY0 = 0;
constexpr int kDetailedPianoRollY1 = kDetailedPianoRollRows - 1;
constexpr int kOverviewGapRows = 1;
constexpr int kOverviewStripRows = 8;
constexpr int kOverviewStripY0 = kDetailedPianoRollY1 + kOverviewGapRows + 1;
constexpr int kOverviewStripY1 = kOverviewStripY0 + kOverviewStripRows - 1;

constexpr int pianoRollRightX() { return DISPLAY_WIDTH - kSidebarWidth - 1; }
constexpr int pianoRollWidth() { return pianoRollRightX() - DisplayManager::TRACK_MARGIN; }

bool isPlayStopButtonHeld() {
    return midiButtonManager.isButtonPressed(MidiConfig::ExtendedTransport::NOTE_PLAY_STOP,
                                             MidiConfig::Channels::SELECT);
}

float displayPlayheadPhase() { return clockManager.getDisplayTickPhase(); }

float resolveDisplayPlayheadInLoop(uint32_t playheadTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0.0f;
    }
    const float phase = displayPlayheadPhase();
    float tick = static_cast<float>(playheadTick % loopLength) + phase;
    const float loopLengthF = static_cast<float>(loopLength);
    if (tick >= loopLengthF) {
        tick -= loopLengthF;
    }
    return tick;
}

int mapPlayheadTickToScreenX(float tickInView, uint32_t viewLength) {
    if (viewLength == 0) {
        return DisplayManager::TRACK_MARGIN;
    }
    const float x =
        (tickInView / static_cast<float>(viewLength)) * static_cast<float>(pianoRollWidth());
    return DisplayManager::TRACK_MARGIN + static_cast<int>(x);
}

}  // namespace

namespace DisplayManagerInternal {

uint32_t resolveBracketDisplayTick(uint32_t loopStartTick, uint32_t loopLength) {
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        const EditorSelection& selection = editManager.getNoteEditSessionState().selection;
        if (editorSelectionHasNote(selection)) {
            return loopLength > 0 ? selection.selectedTick % loopLength : selection.selectedTick;
        }
        return loopLength > 0 ? editManager.getSelectedTick() % loopLength
                              : editManager.getSelectedTick();
    }
    return NoteEditDisplaySnapshot::displayStartTickFromStorage(editManager.getSelectedTick(),
                                                                loopStartTick, loopLength);
}

int resolveDrawHighlightIndex(const DisplayNoteVec& notes, const EditorSelection& selection,
                              uint32_t loopStartTick, uint32_t loopLength,
                              bool windowRelativeTicks, uint32_t windowStartTick,
                              uint32_t bracketDisplayTick) {
    if (!editorSelectionHasNote(selection) || loopLength == 0) {
        return -1;
    }
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    const bool lengthBracket = editManager.isLengthBracketEditActive();
    if (!windowRelativeTicks) {
        return NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(
            selection, notes, focus, loopStartTick, loopLength, lengthBracket);
    }
    uint32_t bracketInWindow = bracketDisplayTick;
    if (bracketDisplayTick >= windowStartTick) {
        bracketInWindow = bracketDisplayTick - windowStartTick;
    } else {
        bracketInWindow = bracketDisplayTick + loopLength - windowStartTick;
    }
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const DisplayNote& dn = notes[static_cast<size_t>(i)];
        if (dn.noteId != selection.primaryNote) {
            continue;
        }
        if (lengthBracket) {
            if (dn.endTick == bracketInWindow) {
                return i;
            }
        } else if (dn.startTick == bracketInWindow) {
            return i;
        }
    }
    if (focus.active && focus.movingNoteId == selection.primaryNote) {
        const uint32_t storageBracket =
            lengthBracket ? focus.last.endTick : focus.last.startTick;
        const uint32_t displayBracket =
            NoteEditDisplaySnapshot::displayStartTickFromStorage(storageBracket, loopStartTick,
                                                                 loopLength);
        if (int byBracket = filteredDisplayNoteIndexForNoteIdAndStart(
                notes, focus.movingNoteId, displayBracket, loopStartTick, loopLength);
            byBracket >= 0) {
            return byBracket;
        }
    }
    if (int byResolver = NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(
            selection, notes, focus, loopStartTick, loopLength, lengthBracket);
        byResolver >= 0) {
        return byResolver;
    }
    return -1;
}

}  // namespace DisplayManagerInternal

int DisplayManager::tickToScreenX(uint32_t tick) {
    Track& track = trackManager.getSelectedTrack();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    const uint32_t loopLength = track.isJamming() ? track.getLoopLength()
                                                   : track.getLoopLengthForSlot(displaySlot);
    const uint32_t loopStartTick = resolveLoopOriginTick(track, displaySlot);

    uint32_t relativeTick = (tick >= loopStartTick) ? (tick - loopStartTick) : (tick + loopLength - loopStartTick);
    relativeTick = relativeTick % loopLength;

    return TRACK_MARGIN + map(relativeTick, 0, loopLength, 0, pianoRollWidth());
}

int DisplayManager::noteToScreenY(uint8_t note) {
    int minNote = 36;
    int maxNote = 84;
    return DISPLAY_HEIGHT - ((note - minNote) * DISPLAY_HEIGHT) / (maxNote - minNote + 1);
}

void DisplayManager::drawGridLines(uint32_t lengthLoop, int pianoRollY0, int pianoRollY1,
                                   uint32_t windowStartTick) {
    if (lengthLoop == 0) {
        return;
    }
    const int barBrightness = 3;
    const int beatBrightness = 2;
    const int sixteenthBrightness = 1;
    const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
    const uint32_t ticksPerBeat = Config::TICKS_PER_QUARTER_NOTE;
    const uint32_t ticksPerSixteenth = Config::TICKS_PER_QUARTER_NOTE / 4;
    const uint32_t windowEnd = windowStartTick + lengthLoop;
    const int rollWidth = pianoRollWidth();

    auto tickToColumn = [&](uint32_t absTick) -> int {
        if (absTick < windowStartTick || absTick >= windowEnd) {
            return -1;
        }
        const uint32_t relTick = absTick - windowStartTick;
        return TRACK_MARGIN + map(relTick, 0, lengthLoop, 0, rollWidth);
    };

    const uint32_t firstBarTick = (windowStartTick / ticksPerBar) * ticksPerBar;
    for (uint32_t absTick = firstBarTick; absTick < windowEnd; absTick += ticksPerBar) {
        if (absTick < windowStartTick) {
            continue;
        }
        const int x = tickToColumn(absTick);
        if (x >= 0) {
            _display.gfx.draw_vline(_display.api.getFrameBuffer(), x, pianoRollY0, pianoRollY1,
                                    barBrightness);
        }
    }

    const bool showBeat = (lengthLoop <= 9 * ticksPerBar);
    if (showBeat) {
        const uint32_t firstBeatTick =
            windowStartTick - (windowStartTick % ticksPerBeat);
        for (uint32_t absTick = firstBeatTick; absTick < windowEnd; absTick += ticksPerBeat) {
            if (absTick < windowStartTick || absTick % ticksPerBar == 0) {
                continue;
            }
            const int x = tickToColumn(absTick);
            if (x >= 0) {
                for (int y = pianoRollY0; y <= pianoRollY1; y += 2) {
                    _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, y, beatBrightness);
                }
            }
        }
    }

    const bool showSixteenth = (lengthLoop <= 5 * ticksPerBar);
    if (showSixteenth) {
        const uint32_t firstSixteenthTick =
            windowStartTick - (windowStartTick % ticksPerSixteenth);
        for (uint32_t absTick = firstSixteenthTick; absTick < windowEnd;
             absTick += ticksPerSixteenth) {
            if (absTick < windowStartTick || absTick % ticksPerBar == 0 ||
                absTick % ticksPerBeat == 0) {
                continue;
            }
            const int x = tickToColumn(absTick);
            if (x >= 0) {
                for (int y = pianoRollY0; y <= pianoRollY1; y += 4) {
                    _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, y, sixteenthBrightness);
                }
            }
        }
    }
}

void DisplayManager::drawAllNotes(const Track& track, uint8_t displaySlot, uint32_t currentTick,
                                  uint32_t lengthLoop, int minPitch, int maxPitch, int pianoRollY0,
                                  int pianoRollY1, bool windowRelativeTicks, uint32_t windowStartTick,
                                  const DisplayNoteVec& notes) {
    const uint32_t loopLength = track.isJamming() ? track.getLoopLength()
                                                  : resolveDisplayLoopLength(track, displaySlot, currentTick);
    const uint32_t jamStartTick = track.isJamming() ? track.getJamStartTick()
                                                    : resolveLoopOriginTick(track, displaySlot);
    int selectedIdx = -1;
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        const EditorSelection& selection = editManager.getNoteEditSessionState().selection;
        const uint32_t bracketDisplayTick =
            resolveBracketDisplayTick(jamStartTick, loopLength);
        selectedIdx = resolveDrawHighlightIndex(notes, selection, jamStartTick, loopLength,
                                                windowRelativeTicks, windowStartTick,
                                                bracketDisplayTick);
    } else {
        selectedIdx = editManager.getSelectedNoteIdx();
    }

    for (int i = 0; i < (int)notes.size(); i++) {
        const auto& n = notes[i];
        const bool indexHighlight = i == selectedIdx;
        int noteBrightness = indexHighlight ? HIGHLIGHT_COLOR : 7;

        uint32_t adjustedStartTick;
        uint32_t adjustedEndTick;
        if (windowRelativeTicks) {
            adjustedStartTick = n.startTick;
            adjustedEndTick = n.endTick;
        } else {
            adjustedStartTick = (n.startTick - jamStartTick + loopLength) % loopLength;
            adjustedEndTick = (n.endTick - jamStartTick + loopLength) % loopLength;
        }

        if (!windowRelativeTicks && adjustedStartTick >= lengthLoop && adjustedEndTick >= lengthLoop) {
            continue;
        }
        if (windowRelativeTicks && adjustedStartTick >= lengthLoop) {
            continue;
        }

        int y = map(n.note, minPitch, maxPitch, pianoRollY1, pianoRollY0);
        y = constrain(y, pianoRollY0, pianoRollY1);

        drawNoteBar(n, y, adjustedStartTick, adjustedEndTick, lengthLoop, noteBrightness);
    }
}

void DisplayManager::drawBracket(uint32_t selectedTick, uint32_t lengthLoop, int pianoRollY1) {
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        int bracketX = TRACK_MARGIN + map(selectedTick, 0, lengthLoop, 0, pianoRollWidth());
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), bracketX, 0, pianoRollY1, BRACKET_COLOR);
    }
}

void DisplayManager::drawNoteBar(const DisplayNote& e, int y, uint32_t s, uint32_t eTick, uint32_t lengthLoop, int noteBrightness) {
    bool isWrapped = (eTick < s) || (eTick > lengthLoop);

    if (!isWrapped && eTick >= s) {
        int x0 = TRACK_MARGIN + map(s, 0, lengthLoop, 0, pianoRollWidth());
        const uint32_t endExclusive = (lengthLoop > 0 && eTick >= lengthLoop - 1) ? lengthLoop
                                                                                  : (eTick + 1);
        int x1 = TRACK_MARGIN + map(endExclusive, 0, lengthLoop, 0, pianoRollWidth());
        if (x1 < x0) x1 = x0;
        _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, x1, y, noteBrightness);
    } else {
        const uint32_t wrappedEndInclusive = (lengthLoop == 0) ? 0 : (eTick % lengthLoop);
        const uint32_t wrappedEndExclusive =
            NoteUtils::wrapHeadExclusiveEndForDraw(wrappedEndInclusive, lengthLoop);

        int x0 = TRACK_MARGIN + map(s % lengthLoop, 0, lengthLoop, 0, pianoRollWidth());
        int xEnd = TRACK_MARGIN + map(lengthLoop, 0, lengthLoop, 0, pianoRollWidth());
        int x1 = TRACK_MARGIN + map(0, 0, lengthLoop, 0, pianoRollWidth());
        int x2 = TRACK_MARGIN + map(wrappedEndExclusive, 0, lengthLoop, 0, pianoRollWidth());

        if (s % lengthLoop < lengthLoop) {
            _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, xEnd, y, noteBrightness);
        }

        if (wrappedEndExclusive > 0) {
            _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x1, y, x2, y, noteBrightness);
        }
    }
}

void DisplayManager::drawOverviewStrip(uint32_t fullLoopLength, uint32_t loopOriginTick,
                                       uint32_t windowStart, uint32_t windowLength,
                                       uint32_t playheadTick, int minPitch, int maxPitch,
                                       const DisplayNoteVec& notes, int y0, int y1) {
    if (fullLoopLength == 0 || y1 < y0) {
        return;
    }
    const int width = pianoRollWidth();
    constexpr uint8_t kOverviewRestInsideBrightness = 1;
    constexpr uint8_t kOverviewRestOutsideBrightness = 0;
    constexpr uint8_t kOverviewNoteInsideBrightness = 2;
    constexpr uint8_t kOverviewNoteOutsideBrightness = 1;
    constexpr uint8_t kOverviewWindowSideBrightness = 3;
    constexpr uint8_t kOverviewWindowEdgeBrightness = 2;

    const uint32_t windowEnd = windowStart + windowLength;
    const int boxX0 = TRACK_MARGIN + map(windowStart % fullLoopLength, 0, fullLoopLength, 0, width);
    const int boxX1 =
        TRACK_MARGIN + map(std::min(windowEnd, fullLoopLength), 0, fullLoopLength, 0, width);

    _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), TRACK_MARGIN, y0,
                                  TRACK_MARGIN + width, y1, kOverviewRestOutsideBrightness);
    _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), boxX0, y0, boxX1, y1,
                                  kOverviewRestInsideBrightness);

    auto drawNoteInsideWindow = [&](const DisplayNote& note, int y, uint32_t startTick,
                                    uint32_t endTick, int noteBrightness) {
        if (windowLength == 0) {
            return;
        }
        auto clipDraw = [&](uint32_t segStart, uint32_t segEnd) {
            if (segEnd <= segStart) {
                return;
            }
            const uint32_t insideStart = std::max(segStart, windowStart);
            const uint32_t insideEnd = std::min(segEnd, windowEnd);
            if (insideStart < insideEnd) {
                drawNoteBar(note, y, insideStart, insideEnd, fullLoopLength, noteBrightness);
            }
        };
        const bool isWrapped = (endTick < startTick) || (endTick > fullLoopLength);
        if (!isWrapped && endTick >= startTick) {
            clipDraw(startTick, endTick);
            return;
        }
        clipDraw(startTick % fullLoopLength, fullLoopLength);
        const uint32_t wrappedEndInclusive = endTick % fullLoopLength;
        const uint32_t wrappedEndExclusive = NoteUtils::wrapHeadExclusiveEndForDraw(
            wrappedEndInclusive, fullLoopLength);
        if (wrappedEndExclusive > 0) {
            clipDraw(0, wrappedEndExclusive);
        }
    };

    for (const DisplayNote& n : notes) {
        uint32_t startTick = (n.startTick >= loopOriginTick)
                                 ? (n.startTick - loopOriginTick)
                                 : (n.startTick + fullLoopLength - loopOriginTick);
        startTick %= fullLoopLength;
        uint32_t endTick = (n.endTick >= loopOriginTick) ? (n.endTick - loopOriginTick)
                                                         : (n.endTick + fullLoopLength - loopOriginTick);
        endTick %= fullLoopLength;
        if (startTick >= fullLoopLength && endTick >= fullLoopLength) {
            continue;
        }

        int y = map(n.note, minPitch, maxPitch, y1, y0);
        y = constrain(y, y0, y1);
        drawNoteBar(n, y, startTick, endTick, fullLoopLength, kOverviewNoteOutsideBrightness);
        drawNoteInsideWindow(n, y, startTick, endTick, kOverviewNoteInsideBrightness);
    }

    _display.gfx.draw_vline(_display.api.getFrameBuffer(), boxX0, y0, y1,
                            kOverviewWindowSideBrightness);
    _display.gfx.draw_vline(_display.api.getFrameBuffer(), boxX1, y0, y1,
                            kOverviewWindowSideBrightness);
    _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), boxX0, y0, boxX1, y0,
                                  kOverviewWindowEdgeBrightness);
    _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), boxX0, y1, boxX1, y1,
                                  kOverviewWindowEdgeBrightness);

    const float displayPlayhead = resolveDisplayPlayheadInLoop(playheadTick, fullLoopLength);
    const int playX = mapPlayheadTickToScreenX(displayPlayhead, fullLoopLength);
    _display.gfx.draw_vline(_display.api.getFrameBuffer(), playX, y0, y1, PLAYHEAD_COLOR);
}

bool DisplayManager::shouldAutoFollowDetailedWindow(const Track& track, uint32_t loopLength) const {
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (track.isJamming() || loopLength <= boundedThreshold) {
        return false;
    }
    if (isPlayStopButtonHeld()) {
        return true;
    }
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        return false;
    }
    return track.isRecording() || track.isPlaying() || track.isOverdubbing();
}

DetailedWindowContext DisplayManager::resolveDetailedWindow(const Track& track, uint8_t displaySlot,
                                                            uint32_t currentTick) const {
    DetailedWindowContext ctx;
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (track.isJamming() || loopLength <= boundedThreshold || displaySlot >= kDisplaySlotCount) {
        return ctx;
    }
    const uint8_t windowBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                 DisplayWindowUtils::kMaxDetailedWindowBars);
    ctx.active = true;
    ctx.window = DisplayWindowUtils::makeViewportInterval(
        detailedWindowStartTick_[displaySlot],
        static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR);
    if (static_cast<uint32_t>(ctx.window.end) > loopLength) {
        const uint32_t windowLength = static_cast<uint32_t>(ctx.window.length());
        const uint32_t windowStart =
            loopLength > windowLength ? loopLength - windowLength : 0;
        ctx.window = DisplayWindowUtils::makeViewportInterval(windowStart, windowLength);
    }
    return ctx;
}

void DisplayManager::centerDetailedWindowOnPlayhead(Track& track, uint8_t displaySlot,
                                                    uint32_t currentTick) {
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (track.isJamming() || loopLength <= boundedThreshold || displaySlot >= kDisplaySlotCount) {
        return;
    }
    const uint8_t windowBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                 DisplayWindowUtils::kMaxDetailedWindowBars);
    const uint32_t windowLength = static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR;
    const uint32_t playhead = resolvePlayheadInLoop(track, displaySlot, currentTick);
    detailedWindowStartTick_[displaySlot] =
        DisplayWindowUtils::resolveCenteredWindowStart(playhead, windowLength, loopLength);
}

void DisplayManager::drawPianoRoll(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot, const DisplayNoteVec& notes) {
    auto& track = selectedTrack;
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const uint32_t jamLength = track.isJamming() ? track.getJamLength()
                                                 : loopLength;
    const uint32_t jamStartTick = track.isJamming() ? track.getJamStartTick()
                                                    : resolveLoopOriginTick(track, displaySlot);
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    const bool useBoundedWindow =
        !track.isJamming() && loopLength > boundedThreshold && displaySlot < kDisplaySlotCount;
    uint32_t windowStart = 0;
    uint32_t windowLength = jamLength;
    uint8_t windowBars = DisplayWindowUtils::kMaxDetailedWindowBars;
    if (useBoundedWindow) {
        windowBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                       DisplayWindowUtils::kMaxDetailedWindowBars);
        windowLength = static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR;
        const uint32_t jamPos = resolvePlayheadInLoop(track, displaySlot, currentTick);
        if (shouldAutoFollowDetailedWindow(track, loopLength)) {
            detailedWindowStartTick_[displaySlot] =
                DisplayWindowUtils::resolveCenteredWindowStart(jamPos, windowLength, loopLength);
        }
        windowStart = detailedWindowStartTick_[displaySlot];
        if (windowStart + windowLength > loopLength) {
            windowStart = loopLength > windowLength ? loopLength - windowLength : 0;
            detailedWindowStartTick_[displaySlot] = windowStart;
        }
    }
    DisplayNoteVec windowFilteredNotes;
    const DisplayNoteVec* detailedNotes = &notes;
    if (useBoundedWindow) {
        windowFilteredNotes = DisplayWindowUtils::filterDisplayNotesToWindow(
            notes, windowStart, windowLength, loopLength);
        detailedNotes = &windowFilteredNotes;
    }
    const uint32_t detailedLength = useBoundedWindow ? windowLength : jamLength;
    const int pianoRollY0 = kDetailedPianoRollY0;
    const int pianoRollY1 = kDetailedPianoRollY1;
    if (loopLength > 0) {
        const uint32_t jamPos = resolvePlayheadInLoop(track, displaySlot, currentTick);
        const Loop& loop = track.getLoop(displaySlot);
        // Overview minimap spans the full loop. Use fully built visualCache when clean;
        // while dirty, `notes` carries the authoritative committed/capture span (not window
        // filtered). Stale partial visualCache must not paint the minimap (170314).
        const DisplayNoteVec& overviewDensityNotes =
            (!loop.visualCacheDirty && !loop.visualCache.notes.empty()) ? loop.visualCache.notes
                                                                         : notes;

        int minPitch = 127;
        int maxPitch = 0;
        for (const auto& n : notes) {
            if (n.note < minPitch) minPitch = n.note;
            if (n.note > maxPitch) maxPitch = n.note;
        }
        for (const auto& n : overviewDensityNotes) {
            if (n.note < minPitch) minPitch = n.note;
            if (n.note > maxPitch) maxPitch = n.note;
        }
        if (minPitch > maxPitch) {
            minPitch = 60;
            maxPitch = 72;
        } else if (minPitch == maxPitch) {
            maxPitch = minPitch + 1;
        }

        drawGridLines(detailedLength, pianoRollY0, pianoRollY1,
                      useBoundedWindow ? windowStart : 0);
        drawAllNotes(track, displaySlot, currentTick, detailedLength, minPitch, maxPitch, pianoRollY0,
                     pianoRollY1, useBoundedWindow, windowStart, *detailedNotes);

        uint32_t relativeBracketTick = resolveBracketDisplayTick(jamStartTick, loopLength);
        if (useBoundedWindow) {
            if (relativeBracketTick >= windowStart &&
                relativeBracketTick < windowStart + windowLength) {
                relativeBracketTick -= windowStart;
                drawBracket(relativeBracketTick, detailedLength, pianoRollY1);
            }
        } else if (relativeBracketTick < jamLength) {
            drawBracket(relativeBracketTick, detailedLength, pianoRollY1);
        }

        const bool previewPlayheadPending =
            isPreviewPlayheadPending(displaySlot, track.getActiveLoopIndex(),
                                     track.isPlaying() || track.isOverdubbing());
        const bool drawPlayhead =
            !previewPlayheadPending || previewPlayheadFlashVisible(millis());

        if (useBoundedWindow) {
            drawOverviewStrip(loopLength, jamStartTick, windowStart, windowLength, jamPos, minPitch,
                              maxPitch, overviewDensityNotes, kOverviewStripY0, kOverviewStripY1);
            if (drawPlayhead && jamPos >= windowStart && jamPos < windowStart + windowLength) {
                const float phase = previewPlayheadPending ? 0.0f : displayPlayheadPhase();
                const float relativePlayhead =
                    static_cast<float>(jamPos - windowStart) + phase;
                const int cx = mapPlayheadTickToScreenX(relativePlayhead, detailedLength);
                _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, pianoRollY0, pianoRollY1,
                                        PLAYHEAD_COLOR);
            }
        } else if (drawPlayhead && jamPos < jamLength) {
            const float displayPlayhead = resolveDisplayPlayheadInLoop(jamPos, jamLength);
            const int cx = mapPlayheadTickToScreenX(displayPlayhead, jamLength);
            _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, pianoRollY0, pianoRollY1,
                                    PLAYHEAD_COLOR);
        }
    }
}
