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
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "MidiHandler.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteMovementWrap.h"
#include "Utils/NoteMovementUtils.h"
#include "TrackStateMachine.h"
#include "MidiButtonManager.h"
#include "MidiConfig.h"
#include "StorageManager.h"
#include "SetBrowserOverlayPolicy.h"
#include "SetRevisionCatalog.h"
#include "RtcTime.h"
#include "HitlDisplayBridge.h"

namespace {

bool shouldDeferFullDisplayVisualRebuild(const Loop& loop, uint32_t loopLength) {
    if (StorageManager::hasPendingLoopSlotRestore() ||
        StorageManager::hasPendingUndoSnapshotHydrate() || StorageManager::hasDeferredSaveWork()) {
        return true;
    }
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    return loopLength > boundedThreshold && loop.visualCacheDirty && loop.visualCache.notes.empty();
}

}  // namespace
#include "DeferredSaveDisplayStatus.h"
#include "LooperState.h"
#include "SavedSetCatalog.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>

#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define DISP_CAPTURE_MEM FLASHMEM
#else
#define DISP_CAPTURE_MEM
#endif

// Placed in DMAMEM (OCRAM/RAM2), not DTCM: DisplayManager is the largest RAM1
// variable object (~9.4 KB) and is only touched from the main loop, never the
// MIDI-clock ISR. A real constructor initializes it at startup, so it is safe in
// the .bss.dma (NOLOAD) region. This keeps RAM1 (DTCM) within its 3-bank budget.
DMAMEM DisplayManager displayManager;
namespace {
SessionMidiEventVec liveDisplayEventBuffer;

// Minimal gutter for longest line ("OVERD" = 30px) + 1px separator; content is right-aligned to display edge.
constexpr int SIDEBAR_WIDTH = 30;
constexpr int SIDEBAR_RIGHT_MARGIN = 1;
constexpr int SIDEBAR_SEPARATOR_BRIGHTNESS = 2;
constexpr int MODE_VALUE_BRIGHTNESS = 3; // match brightness of bottom-strip labels
constexpr int SIDEBAR_TEXT_BRIGHTNESS = 5;
constexpr int SIDEBAR_VALUE_BRIGHTNESS = 5; // match LEN / numeric field values in drawInfoField
constexpr int kSaveStatusDotY = 47;
constexpr int kSaveStatusDotSpacing = 2;
constexpr uint8_t kSaveStatusDimBrightness = 2;
constexpr uint8_t kSaveStatusActiveBrightness = 8;
constexpr uint8_t kSaveStatusCompletedBrightness = 15;
constexpr uint8_t kSaveStatusFailedBrightness = 6;
constexpr int kLoadSaveDetailLabelChars = 6;  // "Tracks" — fixed column for value alignment
constexpr int kLoadSaveDetailCharWidth = 6;
constexpr int kLoadSaveDetailColonWidth = 8;
constexpr uint8_t kLoadSaveDetailLabelBrightness = 3;  // matches drawInfoField label (5/3+2)
constexpr uint8_t kLoadSaveDetailColonBrightness = 4;
constexpr uint8_t kLoadSaveDetailValueBrightness = 5;
constexpr int kLoadSaveDetailRightMargin = 1;
constexpr int kLoadSaveDetailDateRightMargin = 0;
constexpr int kLoadSaveDetailBpmValueChars = 3;
constexpr int kLoadSaveDetailRightLabelChars = 4;  // "Bars"
constexpr int kLoadSaveLeftPadding = 2;
constexpr int kLoadSaveDetailLeftPadding = 6;  // px gap after centre divider column
constexpr int kLoadSaveTextLineStep = 9;       // 7px font + 1px descender + 1px below
constexpr int kLoadSaveDetailContentTopY = kLoadSaveTextLineStep;  // align with list rows; y=0 clipped
constexpr int kLoadSaveTrackRowGap = 1;
constexpr int kLoadSaveSlotHorizontalGap = 4;
constexpr int kLoadSaveDetailDateLineCount = 3;
constexpr int kLoadSaveDetailDateColumnChars = 5;  // "31 AUG", "13:30"
constexpr int kLoadSaveDetailDateColumnGap = 6;    // match kLoadSaveDetailLeftPadding
constexpr int kLoadSaveDetailUnsavedMarkerSize = 2;
constexpr int kLoadSaveDetailUnsavedMarkerGap = 2;
constexpr int kLoadSaveDetailFontHeight = 7;

void drawLoadSaveDetailUnsavedMarker(SSD1322& display, int labelX, int metricBottomY) {
    const int squareLeftX = labelX - kLoadSaveDetailUnsavedMarkerGap - kLoadSaveDetailUnsavedMarkerSize;
    // draw_text y is the font bottom row; center the marker on the cap height above it.
    const int textCenterY = metricBottomY - kLoadSaveDetailFontHeight / 2;
    const int squareTopY = textCenterY - kLoadSaveDetailUnsavedMarkerSize / 2;
    display.gfx.draw_rect_filled(display.api.getFrameBuffer(), squareLeftX, squareTopY,
                                 squareLeftX + kLoadSaveDetailUnsavedMarkerSize - 1,
                                 squareTopY + kLoadSaveDetailUnsavedMarkerSize - 1,
                                 kLoadSaveDetailValueBrightness);
}

constexpr int loadSaveDividerX() { return DISPLAY_WIDTH / 2; }

constexpr int loadSaveDetailContentX() {
    return loadSaveDividerX() + 1 + kLoadSaveDetailLeftPadding;
}

constexpr int loadSaveDetailLeftColonX(int detailX) {
    return detailX + kLoadSaveDetailLabelChars * kLoadSaveDetailCharWidth;
}

constexpr int loadSaveDetailRightColonX() {
    const int detailRightX = DISPLAY_WIDTH - kLoadSaveDetailRightMargin;
    const int valueX = detailRightX - kLoadSaveDetailBpmValueChars * kLoadSaveDetailCharWidth;
    return valueX - kLoadSaveDetailColonWidth;
}

constexpr int loadSaveDetailDateLineY(int lineIndex) {
    // Bottom line at DISPLAY_HEIGHT — same baseline as the main note info strip.
    return DISPLAY_HEIGHT -
           (kLoadSaveDetailDateLineCount - 1 - lineIndex) * kLoadSaveTextLineStep;
}

constexpr int loadSaveDetailDateTextLeftX() {
    const int detailRightX = DISPLAY_WIDTH - 1 - kLoadSaveDetailDateRightMargin;
    return detailRightX - kLoadSaveDetailDateColumnChars * kLoadSaveDetailCharWidth + 1;
}

constexpr int loadSaveDetailSlotBarWidth(int detailX) {
    const int slotBarRightX = loadSaveDetailDateTextLeftX() - kLoadSaveDetailDateColumnGap;
    return slotBarRightX - detailX;
}

constexpr int loadSaveBrowserMaxTextChars() {
    const int maxPixelX = loadSaveDividerX() - 1 - kLoadSaveDetailLeftPadding;
    const int availablePixels = maxPixelX - kLoadSaveLeftPadding + 1;
    return std::max(1, availablePixels / kLoadSaveDetailCharWidth);
}

void copyLoadSaveBrowserLabel(char* dest, size_t destSize, const char* source) {
    if (destSize == 0) {
        return;
    }
    const int maxChars = loadSaveBrowserMaxTextChars();
    if (source == nullptr) {
        dest[0] = '\0';
        return;
    }
    size_t copyLen = std::strlen(source);
    if (static_cast<int>(copyLen) > maxChars) {
        copyLen = static_cast<size_t>(maxChars);
    }
    std::memcpy(dest, source, copyLen);
    dest[copyLen] = '\0';
}
constexpr int kDetailedPianoRollRows = 32;
constexpr int kDetailedPianoRollY0 = 0;
constexpr int kDetailedPianoRollY1 = kDetailedPianoRollRows - 1;
constexpr int kOverviewGapRows = 1;
constexpr int kOverviewStripRows = 8;
constexpr int kOverviewStripY0 = kDetailedPianoRollY1 + kOverviewGapRows + 1;
constexpr int kOverviewStripY1 = kOverviewStripY0 + kOverviewStripRows - 1;
constexpr int kPianoRollRegionBottomY = kOverviewStripY1;
constexpr int pianoRollRightX() { return DISPLAY_WIDTH - SIDEBAR_WIDTH - 1; }
constexpr int pianoRollWidth() { return pianoRollRightX() - DisplayManager::TRACK_MARGIN; }

bool isPlayStopButtonHeld() {
    return midiButtonManager.isButtonPressed(MidiConfig::ExtendedTransport::NOTE_PLAY_STOP,
                                             MidiConfig::Channels::SELECT);
}

float displayPlayheadPhase() {
    return clockManager.getDisplayTickPhase();
}

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

uint32_t clampOpenNoteCloseTick(uint32_t closeTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    if (closeTick >= loopLength) {
        return loopLength - 1;
    }
    return closeTick;
}

std::vector<NoteUtils::OpenNoteOn> findCaptureOpenNoteOns(const Loop& loop) {
    if (loop.loopLengthTicks == 0 || loop.capture.store.empty()) {
        return {};
    }
    Loop& mutLoop = const_cast<Loop&>(loop);
    mutLoop.ensureCaptureEventsSorted();
    SessionMidiEventVec captureFlat;
    loop.capture.store.flatten(captureFlat);
    return NoteUtils::findOpenNoteOns(captureFlat, loop.loopLengthTicks);
}

template <typename Alloc>
bool findPreferredWrapHeadOffTick(const std::vector<MidiEvent, Alloc>& midiEvents,
                                  const NoteUtils::OpenNoteOn& open, uint32_t loopLength,
                                  uint32_t& headOffTickOut) {
    uint8_t channel = 0;
    bool channelKnown = false;
    for (const MidiEvent& evt : midiEvents) {
        if (evt.isNoteOn() && evt.data.noteData.note == open.note && evt.tick == open.tick) {
            channel = evt.channel;
            channelKnown = true;
            break;
        }
    }
    for (const MidiEvent& evt : midiEvents) {
        if (!evt.isNoteOff() || evt.data.noteData.note != open.note) {
            continue;
        }
        if (channelKnown && evt.channel != channel) {
            continue;
        }
        uint32_t headOffTick = evt.tick;
        if (headOffTick >= loopLength) {
            headOffTick %= loopLength;
        }
        if (headOffTick >= loopLength - 1) {
            continue;
        }
        if (NoteUtils::isPreferredWrapTailForHeadOff(open.tick, headOffTick, midiEvents, open.note,
                                                     channelKnown ? channel : evt.channel,
                                                     loopLength)) {
            headOffTickOut = headOffTick;
            return true;
        }
    }
    return false;
}

bool updateDisplayNoteEndFrom(DisplayNoteVec& notes, size_t regionStart, uint8_t pitch,
                              uint32_t startTick, uint32_t endTick) {
    for (size_t i = regionStart; i < notes.size(); ++i) {
        if (notes[i].note == pitch && notes[i].startTick == startTick) {
            notes[i].endTick = endTick;
            return true;
        }
    }
    return false;
}

void applyCapturePlayheadTails(const std::vector<NoteUtils::OpenNoteOn>& captureOpens,
                               const SessionMidiEventVec& captureEvents, uint32_t loopLength,
                               uint32_t closeTick, size_t captureRegionStart,
                               DisplayNoteVec& notes) {
    const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);
    const uint32_t wrapWindow =
        loopLength > Config::TICKS_PER_BAR ? Config::TICKS_PER_BAR : loopLength;
    const uint32_t headEnd = wrapWindow;

    for (const auto& open : captureOpens) {
        if (NoteUtils::isWrapHeldOpenNote(captureEvents, open, loopLength)) {
            uint32_t tailEnd = loopLength - 1;
            if (clampedCloseTick >= open.tick) {
                tailEnd = std::min(clampedCloseTick, loopLength - 1);
            }

            if (!updateDisplayNoteEndFrom(notes, captureRegionStart, open.note, open.tick, tailEnd)) {
                DisplayNote tailSeg;
                tailSeg.note = open.note;
                tailSeg.velocity = open.velocity;
                tailSeg.startTick = open.tick;
                tailSeg.endTick = tailEnd;
                notes.push_back(tailSeg);
            }

            uint32_t headEndTick = 0;
            if (findPreferredWrapHeadOffTick(captureEvents, open, loopLength, headEndTick) &&
                headEndTick > 0) {
                DisplayNote headSeg;
                headSeg.note = open.note;
                headSeg.velocity = open.velocity;
                headSeg.startTick = 0;
                headSeg.endTick = headEndTick;
                notes.push_back(headSeg);
            } else if (clampedCloseTick > 0 && clampedCloseTick < headEnd) {
                DisplayNote headSeg;
                headSeg.note = open.note;
                headSeg.velocity = open.velocity;
                headSeg.startTick = 0;
                headSeg.endTick = clampedCloseTick;
                notes.push_back(headSeg);
            }
            continue;
        }

        const uint32_t playheadEndTick = std::max(open.tick, clampedCloseTick);
        if (!updateDisplayNoteEndFrom(notes, captureRegionStart, open.note, open.tick,
                                      playheadEndTick)) {
            DisplayNote liveNote;
            liveNote.note = open.note;
            liveNote.velocity = open.velocity;
            liveNote.startTick = open.tick;
            liveNote.endTick = playheadEndTick;
            notes.push_back(liveNote);
        }
    }
}

template <typename Alloc>
void applyLiveOpenTails(const std::vector<NoteUtils::OpenNoteOn>& openNotes,
                        const std::vector<MidiEvent, Alloc>& midiEvents, uint32_t loopLength,
                        uint32_t closeTick, DisplayNoteVec& notes, bool extendHeldNotesToPlayhead) {
    const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);
    const uint32_t wrapWindow =
        loopLength > Config::TICKS_PER_BAR ? Config::TICKS_PER_BAR : loopLength;
    const uint32_t headEnd = wrapWindow;

    for (const auto& open : openNotes) {
        if (NoteUtils::isWrapHeldOpenNote(midiEvents, open, loopLength)) {
            // Held across loop wrap: tail segment at end of loop + head continuation from tick 0.
            uint32_t tailEnd = loopLength - 1;
            if (extendHeldNotesToPlayhead && clampedCloseTick >= open.tick) {
                tailEnd = std::min(clampedCloseTick, loopLength - 1);
            }

            bool foundTail = false;
            for (auto& note : notes) {
                if (note.note == open.note && note.startTick == open.tick) {
                    note.endTick = tailEnd;
                    foundTail = true;
                    break;
                }
            }
            if (!foundTail) {
                DisplayNote tailSeg;
                tailSeg.note = open.note;
                tailSeg.velocity = open.velocity;
                tailSeg.startTick = open.tick;
                tailSeg.endTick = tailEnd;
                notes.push_back(tailSeg);
            }

            if (extendHeldNotesToPlayhead) {
                if (clampedCloseTick > 0 && clampedCloseTick < headEnd) {
                    DisplayNote headSeg;
                    headSeg.note = open.note;
                    headSeg.velocity = open.velocity;
                    headSeg.startTick = 0;
                    headSeg.endTick = clampedCloseTick;
                    notes.push_back(headSeg);
                }
            } else {
                uint32_t headOffTick = 0;
                if (findPreferredWrapHeadOffTick(midiEvents, open, loopLength, headOffTick) &&
                    headOffTick > 0) {
                    DisplayNote headSeg;
                    headSeg.note = open.note;
                    headSeg.velocity = open.velocity;
                    headSeg.startTick = 0;
                    headSeg.endTick = headOffTick;
                    notes.push_back(headSeg);
                }
            }
            continue;
        }

        if (!extendHeldNotesToPlayhead) {
            continue;
        }

        // Record/overdub only: note-on without note-off yet grows to the live playhead.
        const uint32_t playheadEndTick = std::max(open.tick, clampedCloseTick);
        bool found = false;
        for (auto& note : notes) {
            if (note.note == open.note && note.startTick == open.tick) {
                note.endTick = playheadEndTick;
                found = true;
                break;
            }
        }
        if (!found) {
            DisplayNote liveNote;
            liveNote.note = open.note;
            liveNote.velocity = open.velocity;
            liveNote.startTick = open.tick;
            liveNote.endTick = playheadEndTick;
            notes.push_back(liveNote);
        }
    }
}

void applyRecordingPreviewOpenTails(DisplayNoteVec& notes, uint32_t loopLength,
                                    uint32_t closeTick) {
    const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);
    for (DisplayNote& note : notes) {
        if (note.endTick != note.startTick) {
            continue;
        }
        note.endTick = std::max(note.startTick, clampedCloseTick);
    }
}
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
    return !track.isJamming() && (track.isRecording() || track.isOverdubbing()) &&
           track.getRecordingFocusSlot() == displaySlot;
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

uint32_t DisplayManager::resolveLoopOriginTick(const Track& track, uint8_t displaySlot) const {
    if (track.isJamming()) {
        return track.getLoopStartTick();
    }
    return track.getLoopStartTickForSlot(displaySlot);
}

uint32_t DisplayManager::resolvePlayheadInLoop(const Track& track, uint8_t displaySlot,
                                               uint32_t currentTick) const {
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (loopLength == 0) {
        return 0;
    }

    const uint32_t displayTick = resolveDisplayTick(track, displaySlot, currentTick);
    if (isLiveRecordingDisplay(track, displaySlot)) {
        if (track.isRecording() && !track.isPlaying()) {
            // Growing capture length equals displayTick; modulo would always yield 0.
            if (loopLength == 0 || displayTick == 0) {
                return 0;
            }
            return std::min(displayTick, loopLength) - 1;
        }
        return displayTick % loopLength;
    }

    const Loop& dispLoop = track.getLoop(displaySlot);
    const uint32_t loopOrigin = resolveLoopOriginTick(track, displaySlot);
    const bool alignWithPlaybackCycle =
        displaySlot == track.getActiveLoopIndex() &&
        (track.isPlaying() || track.isOverdubbing());
    const uint32_t tickInLoopStorage =
        alignWithPlaybackCycle
            ? IntervalProjection::tickPhaseInProjectionCycle(
                  displayTick, track.getProjectionCycleStartTick(), loopLength)
            : tickPhaseInLoop(displayTick, dispLoop.startLoopTick, loopLength);
    return IntervalProjection::noteRelativeTick(tickInLoopStorage, loopOrigin, loopLength);
}

const DisplayNoteVec& DisplayManager::resolveDisplayNotes(const Track& track, uint8_t displaySlot,
                                                          uint32_t currentTick) {
    if (track.isJamming()) {
        const auto& cachedNotes = track.getCachedNotes();
        liveDisplayNotes.assign(cachedNotes.begin(), cachedNotes.end());
        return liveDisplayNotes;
    }

    if (isLiveRecordingDisplay(track, displaySlot)) {
        const Loop& loop = track.getLoop(displaySlot);
        const uint32_t liveLoopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
        if (liveLoopLength == 0) {
            invalidateLiveDisplayCache();
            liveDisplayNotes.clear();
            return liveDisplayNotes;
        }

        const TrackState liveTrackState = track.isRecording() ? TRACK_RECORDING : TRACK_OVERDUBBING;
        const size_t eventCount =
            (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size()
                                                        : loop.displayEventCountHint();
        const bool cacheCold = liveDisplayCacheEventCount == static_cast<size_t>(-1);
        const bool contextChanged = displaySlot != liveDisplayCacheSlot ||
                                    liveTrackState != liveDisplayCacheTrackState;
        const bool eventsShrunk = !cacheCold && eventCount < liveDisplayCacheEventCount;
        const bool eventsAdded = !cacheCold && eventCount > liveDisplayCacheEventCount;
        const bool loopLengthChanged = !cacheCold && liveLoopLength != liveDisplayCacheLoopLength;
        const bool captureRevisionChanged =
            !cacheCold && loop.captureDisplayRevision != liveDisplayCacheCaptureRevision;
        size_t committedDisplayEnd = 0;

        auto rebuildLiveDisplayNotes = [&]() {
            if (track.isOverdubbing()) {
                if (loop.visualCache.notes.empty() || loop.visualCacheDirty) {
                    const_cast<Loop&>(loop).ensureVisualCacheBuilt();
                }
                liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
                committedDisplayEnd = liveDisplayNotes.size();
                SessionMidiEventVec captureFlat;
                Loop& mutLoop = const_cast<Loop&>(loop);
                mutLoop.ensureCaptureEventsSorted();
                loop.capture.store.flatten(captureFlat);
                if (!captureFlat.empty()) {
                    NoteUtils::DisplayNoteVec captureDisplayNotes =
                        NoteUtils::reconstructDisplayNotes(captureFlat, liveLoopLength, false);
                    liveDisplayNotes.insert(liveDisplayNotes.end(), captureDisplayNotes.begin(),
                                            captureDisplayNotes.end());
                }
                return;
            }

            liveDisplayNotes.assign(loop.capturePreview.notes.begin(),
                                    loop.capturePreview.notes.end());
            committedDisplayEnd = 0;
            if (liveDisplayNotes.empty() && track.isRecording() && !loop.capture.store.empty()) {
                SessionMidiEventVec captureFlat;
                Loop& mutLoop = const_cast<Loop&>(loop);
                mutLoop.ensureCaptureEventsSorted();
                loop.capture.store.flatten(captureFlat);
                if (!captureFlat.empty()) {
                    const NoteUtils::DisplayNoteVec reconstructed =
                        NoteUtils::reconstructDisplayNotes(captureFlat, liveLoopLength, false);
                    liveDisplayNotes.assign(reconstructed.begin(), reconstructed.end());
                }
            }
        };

        const bool needsFullLiveRebuild = cacheCold || contextChanged || eventsShrunk || eventsAdded ||
                                          loopLengthChanged || captureRevisionChanged;

        if (needsFullLiveRebuild) {
            if (track.isOverdubbing()) {
                if (loop.visualCache.notes.empty() && !loop.isPassesMaterializedStoreFresh()) {
                    loop.mergeMaterializedPassesWithCapture(liveDisplayEventBuffer);
                }
            } else {
                liveDisplayEventBuffer.clear();
            }
            rebuildLiveDisplayNotes();
            if (track.isOverdubbing()) {
                liveDisplayCacheOpenNotes =
                    NoteUtils::findOpenNoteOns(liveDisplayEventBuffer, liveLoopLength);
            } else {
                liveDisplayCacheOpenNotes.clear();
            }
            liveDisplayCacheSlot = displaySlot;
            liveDisplayCacheTrackState = liveTrackState;
            liveDisplayCacheLoopLength = liveLoopLength;
            liveDisplayCacheEventCount = eventCount;
            liveDisplayCacheCaptureRevision = loop.captureDisplayRevision;
        } else {
            liveDisplayCacheLoopLength = liveLoopLength;
            if (track.isRecording() || track.isOverdubbing()) {
                rebuildLiveDisplayNotes();
            }
        }

        if (track.isRecording() || track.isOverdubbing()) {
            const uint32_t playheadCloseTick = resolvePlayheadInLoop(track, displaySlot, currentTick);
            if (track.isOverdubbing()) {
                SessionMidiEventVec captureEvents;
                const std::vector<NoteUtils::OpenNoteOn> captureOpens = findCaptureOpenNoteOns(loop);
                if (!captureOpens.empty()) {
                    Loop& mutLoop = const_cast<Loop&>(loop);
                    mutLoop.ensureCaptureEventsSorted();
                    loop.capture.store.flatten(captureEvents);
                    applyCapturePlayheadTails(captureOpens, captureEvents, liveLoopLength,
                                              playheadCloseTick, committedDisplayEnd, liveDisplayNotes);
                }
            } else {
                liveDisplayCacheOpenNotes.clear();
                applyRecordingPreviewOpenTails(liveDisplayNotes, liveLoopLength, playheadCloseTick);
            }
        }

        return liveDisplayNotes;
    }

    // NOTE_EDIT: session store (editAware) is the live edit buffer; filter for Hidden / inner overlap.
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
        if (editManager.isNoteEditActive() && loopLength > 0) {
            const NoteEditFocus& focus = editManager.getEditSession().focus;
            const uint32_t previewRevision = editManager.sessionPreviewRevision();
            const size_t overlapCount = focus.overlapNotes.size();
            const bool cacheHit = displaySlot == noteEditDisplayCacheSlot_ &&
                                  previewRevision == noteEditDisplayCachePreviewRevision_ &&
                                  loopLength == noteEditDisplayCacheLoopLength_ &&
                                  overlapCount == noteEditDisplayCacheOverlapCount_ &&
                                  !liveDisplayNotes.empty();
            if (!cacheHit) {
                liveDisplayNotes = filterSelectableDisplayNotes(track.editAwareMidiEvents(), focus,
                                                                track.getMidiChannel(), loopLength);
                noteEditDisplayCacheSlot_ = displaySlot;
                noteEditDisplayCachePreviewRevision_ = previewRevision;
                noteEditDisplayCacheLoopLength_ = loopLength;
                noteEditDisplayCacheOverlapCount_ = overlapCount;
            }
        } else if (loopLength > 0) {
            const auto& cachedNotes = track.getCachedNotes();
            liveDisplayNotes.assign(cachedNotes.begin(), cachedNotes.end());
        }
        if (liveDisplayNotes.empty() && loopLength > 0) {
            const Loop& loop = track.getLoop(displaySlot);
            if (loop.hasPublishedEvents() || loop.captureActive()) {
                Loop& mutLoop = const_cast<Loop&>(loop);
                mutLoop.ensureVisualCacheBuilt();
                liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
            }
        }
        return liveDisplayNotes;
    }

    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (loopLength == 0 || (!loop.hasPublishedEvents() && !loop.captureActive())) {
        liveDisplayNotes.clear();
        return liveDisplayNotes;
    }

    if (editManager.isNoteEditActive()) {
        invalidateLiveDisplayCache();
        const auto& cachedNotes = track.getCachedNotes();
        liveDisplayNotes.assign(cachedNotes.begin(), cachedNotes.end());
        return liveDisplayNotes;
    }

    Loop& mutLoop = const_cast<Loop&>(loop);
    const bool deferVisualRebuild =
        (track.isPlaying() || track.isStoppedRecording()) && !track.isOverdubbing() &&
        !(track.isRecording() && !track.isPlaying());
    const bool deferFullVisualRebuild = shouldDeferFullDisplayVisualRebuild(loop, loopLength);
    if (!deferVisualRebuild && !deferFullVisualRebuild) {
        mutLoop.ensureVisualCacheBuilt();
    }
    const bool needsLiveMergeForDisplay =
        loop.captureActive() || track.isRecording() || track.isOverdubbing();
    if (!needsLiveMergeForDisplay) {
        // Stale-while-revalidate: when PLAYING defers rebuild, show last visual cache until idle
        // maintenance refreshes it — only return empty when cache was never built.
        if (!loop.visualCacheDirty || !loop.visualCache.notes.empty()) {
            liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
            return liveDisplayNotes;
        }
        // Phase C: long loops — provisional window notes from chunk merge (no materialize).
        const uint32_t boundedThreshold =
            DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
        if (loopLength > boundedThreshold) {
            const uint8_t windowBars = DisplayWindowUtils::kMaxDetailedWindowBars;
            const uint32_t windowLength = static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR;
            const uint32_t playhead = resolvePlayheadInLoop(track, displaySlot, currentTick);
            const uint32_t windowStart =
                DisplayWindowUtils::resolveCenteredWindowStart(playhead, windowLength, loopLength);
            liveDisplayEventBuffer.clear();
            mutLoop.mergeActiveCapturePasses(liveDisplayEventBuffer);
            SessionMidiEventVec windowEvents;
            DisplayWindowUtils::filterMidiEventsToWindow(liveDisplayEventBuffer, windowEvents,
                                                         windowStart, windowLength, loopLength);
            if (!windowEvents.empty()) {
                const NoteUtils::DisplayNoteVec provisional =
                    NoteUtils::reconstructDisplayNotes(windowEvents, loopLength, false);
                liveDisplayNotes.assign(provisional.begin(), provisional.end());
            }
        }
        return liveDisplayNotes;
    }

    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (loopLength > boundedThreshold) {
        const uint8_t windowBars = DisplayWindowUtils::kMaxDetailedWindowBars;
        const uint32_t windowLength = static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR;
        const uint32_t playhead = resolvePlayheadInLoop(track, displaySlot, currentTick);
        const uint32_t windowStart =
            DisplayWindowUtils::resolveCenteredWindowStart(playhead, windowLength, loopLength);
        const bool mergeStale = liveMergePlaybackRevision_ != loop.playbackRevision ||
                                liveMergeCaptureRevision_ != loop.captureDisplayRevision;
        if (mergeStale || liveDisplayEventBuffer.empty()) {
            liveDisplayEventBuffer.clear();
            mutLoop.mergeMaterializedPassesWithCapture(liveDisplayEventBuffer);
            liveMergePlaybackRevision_ = loop.playbackRevision;
            liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        }
        SessionMidiEventVec windowEvents;
        DisplayWindowUtils::filterMidiEventsToWindow(liveDisplayEventBuffer, windowEvents, windowStart,
                                                     windowLength, loopLength);
        if (!windowEvents.empty()) {
            const NoteUtils::DisplayNoteVec provisional =
                NoteUtils::reconstructDisplayNotes(windowEvents, loopLength, false);
            liveDisplayNotes.assign(provisional.begin(), provisional.end());
        }
        if (!liveDisplayEventBuffer.empty()) {
            const std::vector<NoteUtils::OpenNoteOn> openNotes =
                NoteUtils::findOpenNoteOns(liveDisplayEventBuffer, loopLength);
            if (!openNotes.empty()) {
                const uint32_t playheadCloseTick =
                    resolvePlayheadInLoop(track, displaySlot, currentTick);
                applyLiveOpenTails(openNotes, liveDisplayEventBuffer, loopLength, playheadCloseTick,
                                   liveDisplayNotes, track.isRecording() || track.isOverdubbing());
            }
        }
        return liveDisplayNotes;
    }

    if (loop.visualCache.notes.empty() && !loop.isPassesMaterializedStoreFresh()) {
        const bool mergeStale = liveMergePlaybackRevision_ != loop.playbackRevision ||
                                liveMergeCaptureRevision_ != loop.captureDisplayRevision;
        if (mergeStale || liveDisplayEventBuffer.empty()) {
            mutLoop.mergeMaterializedPassesWithCapture(liveDisplayEventBuffer);
            liveMergePlaybackRevision_ = loop.playbackRevision;
            liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        }
    }

    // Prefer visualCache (passes.materializeToEventVector) over reconstructing the full
    // materialized view again — after long overdub stop heap can be too low for a second
    // RAM2-heavy reconstruct while deferred save is still running.
    if (!loop.visualCache.notes.empty()) {
        liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
    } else if (!liveDisplayEventBuffer.empty()) {
        const NoteUtils::DisplayNoteVec reconstructed =
            NoteUtils::reconstructDisplayNotes(liveDisplayEventBuffer, loopLength, false);
        liveDisplayNotes.assign(reconstructed.begin(), reconstructed.end());
    } else {
        liveDisplayNotes.clear();
    }

    if (!liveDisplayEventBuffer.empty()) {
        const std::vector<NoteUtils::OpenNoteOn> openNotes =
            NoteUtils::findOpenNoteOns(liveDisplayEventBuffer, loopLength);
        if (!openNotes.empty()) {
            const uint32_t playheadCloseTick =
                resolvePlayheadInLoop(track, displaySlot, currentTick);
            applyLiveOpenTails(openNotes, liveDisplayEventBuffer, loopLength, playheadCloseTick,
                               liveDisplayNotes, false);
        }
    }
    return liveDisplayNotes;
}

DISP_CAPTURE_MEM void DisplayManager::emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot,
                                                uint32_t currentTick,
                                                const DisplayNoteVec& frameNotes) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLen = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const size_t bufferEvents =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size() : frameNotes.size();
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (!track.isJamming() && loopLen > boundedThreshold && displaySlot < kDisplaySlotCount) {
        const uint8_t windowBars =
            std::min<uint8_t>(detailedWindowBars_[displaySlot],
                              DisplayWindowUtils::kMaxDetailedWindowBars);
        const uint32_t windowStart = detailedWindowStartTick_[displaySlot];
        const uint32_t windowLength = static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR;
        const DisplayNoteVec windowNotes = DisplayWindowUtils::filterDisplayNotesToWindow(
            frameNotes, windowStart, windowLength, loopLen);
        SC_DISP_WINDOW(displaySlot, TrackStateMachine::toString(track.getState()), loopLen,
                       bufferEvents, loop.visualCache.notes.size(), frameNotes.size(),
                       bufferEvents, loop.hasPublishedEvents() ? 1 : 0, windowStart, windowBars,
                       windowNotes.size());
        return;
    }

    SC_DISP(displaySlot, TrackStateMachine::toString(track.getState()), loopLen,
            bufferEvents, loop.visualCache.notes.size(), frameNotes.size(),
            bufferEvents, loop.hasPublishedEvents() ? 1 : 0);
}

DISP_CAPTURE_MEM void DisplayManager::emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot,
                                                uint32_t currentTick) {
    const DisplayNoteVec& frameNotes = resolveDisplayNotes(track, displaySlot, currentTick);
    emitDisplayCaptureSnapshot(track, displaySlot, currentTick, frameNotes);
}

DISP_CAPTURE_MEM void DisplayManager::maybeEmitDisplayCaptureOnChange(const Track& track, uint8_t displaySlot,
                                                     uint32_t currentTick,
                                                     const DisplayNoteVec& frameNotes) {
    static size_t lastFrameNotes = static_cast<size_t>(-1);
    static uint8_t lastSlot = 255;
    static TrackState lastState = NUM_TRACK_STATES;
    static uint32_t lastLoopLen = 0;
    static size_t lastTakeEvents = static_cast<size_t>(-1);

    const Loop& loop = track.getLoop(displaySlot);
    const TrackState state = track.getState();
    const uint32_t loopLen = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const size_t frameNoteCount = frameNotes.size();
    const size_t takeEvents =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size() : frameNoteCount;

    const bool changed = frameNoteCount != lastFrameNotes || displaySlot != lastSlot ||
                         state != lastState || loopLen != lastLoopLen ||
                         takeEvents != lastTakeEvents;
    const bool regression =
        loopLen > 0 && loop.hasPublishedEvents() && frameNoteCount == 0 && takeEvents > 0;

    if (!changed && !regression) {
        return;
    }

    if (regression) {
        static uint32_t lastRegressionCaptureMs = 0;
        const uint32_t nowMs = millis();
        if (nowMs - lastRegressionCaptureMs < 500U) {
            return;
        }
        lastRegressionCaptureMs = nowMs;
    }

    lastFrameNotes = frameNoteCount;
    lastSlot = displaySlot;
    lastState = state;
    lastLoopLen = loopLen;
    lastTakeEvents = takeEvents;
    emitDisplayCaptureSnapshot(track, displaySlot, currentTick, frameNotes);
}

void DisplayManager::invalidateForSlotChange(uint8_t trackIndex, uint8_t previousSlot,
                                             uint8_t newSlot) {
    if (trackIndex >= trackManager.getTrackCount()) {
        return;
    }
    invalidateLiveDisplayCache();
    Track& track = trackManager.getTrack(trackIndex);
    const uint8_t activeSlot = track.getActiveLoopIndex();
    const bool playbackActive = track.isPlaying();

    auto invalidateSlotDisplay = [&](uint8_t slot) {
        if (slot >= Config::MAX_LOOPS_PER_TRACK) {
            return;
        }
        Loop& loop = track.getLoop(slot);
        if (slot == newSlot || (playbackActive && slot == activeSlot)) {
            loop.invalidateDisplayCaches();
        } else {
            loop.markDisplayCachesStale();
        }
    };

    invalidateSlotDisplay(previousSlot);
    invalidateSlotDisplay(newSlot);

    if (editManager.isNoteEditActive()) {
        track.invalidateCaches();
    } else if (editManager.isLoopEditSession() || !playbackActive) {
        track.invalidateCaches();
    }

    if (playbackActive && trackIndex == trackManager.getSelectedTrackIndex()) {
        centerDetailedWindowOnPlayhead(track, newSlot, clockManager.getCurrentTick());
    }
}

void DisplayManager::invalidateLiveDisplayCache() {
    liveDisplayCacheEventCount = static_cast<size_t>(-1);
    liveDisplayCacheCaptureRevision = 0;
    liveDisplayCacheLoopLength = 0;
    liveDisplayCacheSlot = 255;
    liveDisplayCacheTrackState = NUM_TRACK_STATES;
    liveDisplayCacheOpenNotes.clear();
    liveMergePlaybackRevision_ = UINT32_MAX;
    liveMergeCaptureRevision_ = 0;
    invalidateNoteEditDisplayCache();
}

void DisplayManager::invalidateNoteEditDisplayCache() {
    noteEditDisplayCacheSlot_ = 255;
    noteEditDisplayCachePreviewRevision_ = UINT32_MAX;
    noteEditDisplayCacheLoopLength_ = 0;
    noteEditDisplayCacheOverlapCount_ = static_cast<size_t>(-1);
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

void DisplayManager::drawBootStatusMessage(const char* text) {
    if (text == nullptr) {
        return;
    }
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);
    _display.gfx.select_font(&Font5x7Fixed);
    const int textWidth = static_cast<int>(std::strlen(text)) * 6;
    const int x = std::max(0, (DISPLAY_WIDTH - textWidth) / 2);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), text, x, 32, 15);
    _display.api.display();
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

void DisplayManager::beginBootOled() {
    if (bootOledInitialized_) {
        return;
    }
    Serial.println("DisplayManager: Setting up SSD1322 display...");
    _display.begin();
    Serial.println("DisplayManager: Setting buffer size");
    _display.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    clearDisplayBuffer();
    bootOledInitialized_ = true;
}

void DisplayManager::finishBootSetup() {
    if (!bootOledInitialized_) {
        beginBootOled();
    }
    Serial.println("DisplayManager: Drawing startup text...");
    Serial.println("Selecting font...");
    _display.gfx.select_font(&Font5x7Fixed);
    Serial.println("Font selected.");
    Serial.println("Drawing text...");
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Midi Looper v0.4", 92, 32, 15);
    _display.api.display();
    Serial.println("DisplayManager: Text sent to display");
#if !defined(SESSION_CAPTURE)
    delay(1500);
#endif
    clearDisplayBuffer();
}

void DisplayManager::setup() {
    beginBootOled();
    finishBootSetup();
}


int DisplayManager::tickToScreenX(uint32_t tick) {
    Track& track = trackManager.getSelectedTrack();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    const uint32_t loopLength = track.isJamming() ? track.getLoopLength()
                                                   : track.getLoopLengthForSlot(displaySlot);
    const uint32_t loopStartTick = resolveLoopOriginTick(track, displaySlot);

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
void DisplayManager::drawGridLines(uint32_t lengthLoop, int pianoRollY0, int pianoRollY1,
                                   uint32_t windowStartTick) {
    if (lengthLoop == 0) {
        return;
    }
    const int barBrightness = 3;       // 50%
    const int beatBrightness = 2;      // 25%
    const int sixteenthBrightness = 1; // 10%
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

// --- Helper: Draw all notes ---
namespace {

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
    if (windowRelativeTicks) {
        uint32_t bracketInWindow = bracketDisplayTick;
        if (bracketDisplayTick >= windowStartTick) {
            bracketInWindow = bracketDisplayTick - windowStartTick;
        } else {
            bracketInWindow = bracketDisplayTick + loopLength - windowStartTick;
        }
        const bool lengthBracket = editManager.isLengthBracketEditActive();
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
        return -1;
    }
    return NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
        selection, notes, loopStartTick, loopLength, editManager.isLengthBracketEditActive());
}

}  // namespace

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
        int noteBrightness = (i == selectedIdx) ? HIGHLIGHT_COLOR : 7;

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

// --- Helper: Draw bracket ---
void DisplayManager::drawBracket(uint32_t selectedTick, uint32_t lengthLoop, int pianoRollY1) {
    // Draw bracket when in NOTE_EDIT mode (simplified since we use dedicated faders)
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        int bracketX = TRACK_MARGIN + map(selectedTick, 0, lengthLoop, 0, pianoRollWidth());
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
        const uint32_t wrappedEndTick = endTick % fullLoopLength;
        if (wrappedEndTick > 0) {
            clipDraw(0, wrappedEndTick);
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

// --- Draw piano roll using cached notes ---
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

        // Pitch range from all loop notes so overdub/capture pitches rescale the roll height.
        int minPitch = 127;
        int maxPitch = 0;
        for (const auto& n : notes) {
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

        // Bracket tick is projected-interval space (UIP selectedTick), not storage tick.
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

        if (useBoundedWindow) {
            drawOverviewStrip(loopLength, jamStartTick, windowStart, windowLength, jamPos, minPitch,
                              maxPitch, notes, kOverviewStripY0, kOverviewStripY1);
            if (jamPos >= windowStart && jamPos < windowStart + windowLength) {
                const float phase = displayPlayheadPhase();
                const float relativePlayhead =
                    static_cast<float>(jamPos - windowStart) + phase;
                const int cx = mapPlayheadTickToScreenX(relativePlayhead, detailedLength);
                _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, pianoRollY0, pianoRollY1,
                                        PLAYHEAD_COLOR);
            }
        } else if (jamPos < jamLength) {
            const float displayPlayhead = resolveDisplayPlayheadInLoop(jamPos, jamLength);
            const int cx = mapPlayheadTickToScreenX(displayPlayhead, jamLength);
            _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, pianoRollY0, pianoRollY1,
                                    PLAYHEAD_COLOR);
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
    _display.gfx.draw_vline(_display.api.getFrameBuffer(), sidebarX - 1, 0, kPianoRollRegionBottomY,
                            SIDEBAR_SEPARATOR_BRIGHTNESS);

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

    uint8_t undoCount = static_cast<uint8_t>(
        editManager.getDisplayUndoCount(selectedTrack, selectedTrack.getLoop(displaySlot)));
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

    // BPM: one decimal place, but draw the '.' as a single pixel so width ~= 4 chars (vs 5 for full ".").
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
    // One-pixel decimal: align with the descender row of digits (Font5x7 mono top-left at bpmY).
    _display.gfx.draw_pixel(_display.api.getFrameBuffer(), dotX - 1, bpmY - 1, bpmBright);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), fracStr, dotX + kThinDotAdvance, bpmY, bpmBright);

    drawRight(modeTop, 17, MODE_VALUE_BRIGHTNESS);
    drawRight(modeBottom, 27, MODE_VALUE_BRIGHTNESS);

    // "U:" label dim like other sidebar labels; digits same brightness as LEN values.
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

void DisplayManager::drawLoadSaveRowLoadStatusDots(int labelLeftX, int labelCharCount, int rowY,
                                                   uint32_t nowMs, uint16_t setId,
                                                   uint16_t revisionId) {
    if (setId == 0) {
        return;
    }
    if (StorageManager::getRevisionLoadDisplayTargetSetId() != setId) {
        return;
    }
    if (revisionId != 0 &&
        StorageManager::getRevisionLoadDisplayTargetRevisionId() != revisionId) {
        return;
    }
    const DeferredSaveDisplayStatus loadStatus =
        StorageManager::getDeferredLoadDisplayStatus(nowMs);
    if (loadStatus.phase == DeferredSaveDisplayPhase::Idle) {
        return;
    }
    constexpr int kDotGapAfterLabel = 2;
    const int dotsStartX =
        labelLeftX + labelCharCount * kLoadSaveDetailCharWidth + kDotGapAfterLabel;
    const int dotY = rowY - 4;
    drawPersistenceStatusDots(dotsStartX, dotY, loadStatus);
}

void DisplayManager::refreshLoadSaveListCache() {
    if (!StorageManager::isOverlayCatalogReadAllowed()) {
        return;
    }
    loadSaveListCount_ = StorageManager::listSetRevisionBrowserEntries(loadSaveListEntries_,
                                                                       kLoadSaveListCapacity);
    loadSaveListCacheValid_ = true;
}

void DisplayManager::invalidateLoadSaveRevisionListCache() {
    loadSaveRevisionListCacheValid_ = false;
    loadSaveRevisionListCount_ = 0;
    loadSaveRevisionListSetId_ = 0;
}

void DisplayManager::refreshLoadSaveRevisionHistoryCache(uint16_t setId, bool forceCatalogRead) {
    if (!forceCatalogRead && !StorageManager::isOverlayCatalogReadAllowed()) {
        return;
    }
    loadSaveRevisionListSetId_ = setId;
    loadSaveRevisionListCount_ = StorageManager::listSetRevisionHistoryEntries(
        setId, loadSaveRevisionListEntries_, kLoadSaveListCapacity);
    loadSaveRevisionListCacheValid_ = true;
}

void DisplayManager::ensureLoadSaveRevisionListCache(uint16_t setId, bool forceCatalogRead) {
    if (setId == 0) {
        return;
    }
    if (forceCatalogRead || !loadSaveRevisionListCacheValid_ ||
        loadSaveRevisionListSetId_ != setId) {
        refreshLoadSaveRevisionHistoryCache(setId, forceCatalogRead);
    }
}

uint16_t DisplayManager::resolveFocusedRootSetId() const {
    const size_t savedIndex =
        SetBrowserOverlayPolicy::rootSetFolderListIndex(loadSaveListSelection_);
    if (savedIndex >= loadSaveListCount_) {
        return 0;
    }
    uint16_t setId = 0;
    if (!SetRevisionCatalog::parseSetIdFromFolderName(loadSaveListEntries_[savedIndex].folderName,
                                                      setId)) {
        return 0;
    }
    return setId;
}

uint16_t DisplayManager::resolveFocusedRevisionId() const {
    if (!loadSaveRevisionListCacheValid_ ||
        loadSaveListSelection_ >= loadSaveRevisionListCount_) {
        return 0;
    }
    return loadSaveRevisionListEntries_[loadSaveListSelection_].revisionId;
}

void DisplayManager::invalidateLoadSaveDetailCache() {
    loadSaveDetailCacheValid_ = false;
}

bool DisplayManager::resolveLoadSaveWorkspaceDetail(LoadSaveWorkspaceDetailParams& out) {
    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    const SetBrowserOverlayPolicy::Mode policyMode =
        static_cast<SetBrowserOverlayPolicy::Mode>(overlayMode);

    LoadSaveDetailCacheKey key{};
    key.mode = policyMode;
    key.listSelection = loadSaveListSelection_;
    key.drilledSetId = StorageManager::getSetBrowserOverlayDrilledSetId();
    if (policyMode == SetBrowserOverlayPolicy::Mode::Root) {
        const size_t savedIndex =
            SetBrowserOverlayPolicy::rootSetFolderListIndex(loadSaveListSelection_);
        if (savedIndex < loadSaveListCount_) {
            std::snprintf(key.setFolderName, sizeof(key.setFolderName), "%s",
                          loadSaveListEntries_[savedIndex].folderName);
        }
    }

    if (loadSaveDetailCacheValid_ && loadSaveDetailCacheKey_.mode == key.mode &&
        loadSaveDetailCacheKey_.listSelection == key.listSelection &&
        loadSaveDetailCacheKey_.drilledSetId == key.drilledSetId &&
        std::strcmp(loadSaveDetailCacheKey_.setFolderName, key.setFolderName) == 0) {
        out = loadSaveDetailCache_;
        return loadSaveDetailCacheOk_;
    }

    const bool overlayCatalogReadAllowed = StorageManager::isOverlayCatalogReadAllowed();
    if (!overlayCatalogReadAllowed && loadSaveDetailCacheValid_) {
        out = loadSaveDetailCache_;
        return loadSaveDetailCacheOk_;
    }
    if (!overlayCatalogReadAllowed) {
        out = {};
        return false;
    }

    loadSaveDetailCacheKey_ = key;
    loadSaveDetailCache_ = {};
    bool detailOk = false;

    if (policyMode == SetBrowserOverlayPolicy::Mode::RevisionHistory) {
        const uint16_t setId = key.drilledSetId;
        const uint16_t revisionId = resolveFocusedRevisionId();
        loadSaveDetailCache_.setId = setId;
        loadSaveDetailCache_.revisionId = revisionId;
        if (setId != 0 && revisionId != 0) {
            detailOk = StorageManager::readSetRevisionHistoryBrowserMetadata(
                setId, revisionId, loadSaveDetailCache_.metrics,
                loadSaveDetailCache_.timestampUnix);
        }
    } else if (policyMode == SetBrowserOverlayPolicy::Mode::Root) {
        const bool isSaveRow = SetBrowserOverlayPolicy::isRootSaveRow(loadSaveListSelection_);
        const bool isCurrentRow =
            SetBrowserOverlayPolicy::isRootCurrentRow(loadSaveListSelection_);
        if (isSaveRow || isCurrentRow) {
            detailOk = StorageManager::readCurrentSetBrowserMetadata(loadSaveDetailCache_.metrics);
            loadSaveDetailCache_.timestampUnix = loadSaveDetailCache_.metrics.createdAtUnix;
            loadSaveDetailCache_.setId = StorageManager::getCurrentWorkspaceDerivedSetId();
            loadSaveDetailCache_.revisionId =
                StorageManager::getCurrentWorkspaceDerivedRevisionId();
            if (isCurrentRow && StorageManager::isCurrentWorkspaceDirty()) {
                loadSaveDetailCache_.markRevisionUnsaved = true;
            }
        } else if (key.setFolderName[0] != '\0') {
            detailOk = StorageManager::readSetRevisionCatalogBrowserMetadata(
                key.setFolderName, loadSaveDetailCache_.metrics, loadSaveDetailCache_.setId,
                loadSaveDetailCache_.revisionId, loadSaveDetailCache_.timestampUnix);
        }
    }

    loadSaveDetailCacheValid_ = true;
    loadSaveDetailCacheOk_ = detailOk;
    out = loadSaveDetailCache_;
    return detailOk;
}

void DisplayManager::handleLoadSaveOverlayPress(LoadSaveOverlayPressType pressType) {
    if (!looperState.isLoadSaveModeActive()) {
        return;
    }

    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode == StorageManager::SetBrowserOverlayMode::MinimalLoading) {
        return;
    }

    if (overlayMode == StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        if (pressType == LoadSaveOverlayPressType::Short) {
            confirmLoadSaveFocusedRow();
        } else if (pressType == LoadSaveOverlayPressType::Long) {
            StorageManager::cancelRevisionLoadRequest();
            looperState.exitLoadSaveMode();
        }
        return;
    }

    if (overlayMode == StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        const uint16_t setId = StorageManager::getSetBrowserOverlayDrilledSetId();
        switch (pressType) {
            case LoadSaveOverlayPressType::Short: {
                ensureLoadSaveRevisionListCache(setId, true);
                const uint16_t revisionId = resolveFocusedRevisionId();
                if (revisionId != 0) {
                    StorageManager::requestLoadRevision(setId, revisionId);
                }
                break;
            }
            case LoadSaveOverlayPressType::Long: {
                uint8_t parentSelection = 0;
                uint8_t parentScroll = 0;
                if (StorageManager::navigateSetBrowserOverlayBack(parentSelection,
                                                                  parentScroll)) {
                    loadSaveListSelection_ = parentSelection;
                    loadSaveListScrollOffset_ = parentScroll;
                    invalidateLoadSaveRevisionListCache();
                    invalidateLoadSaveDetailCache();
                }
                break;
            }
            default:
                break;
        }
        return;
    }

    if (overlayMode != StorageManager::SetBrowserOverlayMode::Root) {
        return;
    }

    const bool isSaveRow = SetBrowserOverlayPolicy::isRootSaveRow(loadSaveListSelection_);
    const uint16_t focusedSetId = resolveFocusedRootSetId();
    const bool isSetRow = focusedSetId != 0;

    switch (pressType) {
        case LoadSaveOverlayPressType::Short:
            if (isSaveRow) {
                confirmLoadSaveFocusedRow();
            } else if (isSetRow) {
                StorageManager::requestLoadLatestRevisionForSet(focusedSetId);
            }
            break;
        case LoadSaveOverlayPressType::Double:
            if (isSetRow) {
                StorageManager::toggleSetRevisionCatalogFavorite(focusedSetId);
            }
            break;
        case LoadSaveOverlayPressType::Long:
            if (isSetRow) {
                StorageManager::openSetBrowserRevisionHistory(focusedSetId, loadSaveListSelection_,
                                                              loadSaveListScrollOffset_);
                loadSaveRevisionListCacheValid_ = false;
                invalidateLoadSaveDetailCache();
                loadSaveListSelection_ = 0;
                loadSaveListScrollOffset_ = 0;
                ensureLoadSaveRevisionListCache(focusedSetId, true);
#if defined(SESSION_CAPTURE)
                SC_OVERLAY_SEL(static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                               loadSaveListSelection_);
#endif
            } else {
                looperState.exitLoadSaveMode();
            }
            break;
    }
}

void DisplayManager::adjustLoadSaveListSelection(int delta) {
    if (delta == 0) {
        return;
    }
    if (StorageManager::getSetBrowserOverlayMode() ==
        StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        StorageManager::adjustRevisionLoadDirtyPromptSelection(delta);
        return;
    }
    if (StorageManager::getSetBrowserOverlayMode() ==
        StorageManager::SetBrowserOverlayMode::MinimalLoading) {
        return;
    }
    if (SetBrowserOverlayPolicy::isDrillMode(StorageManager::getSetBrowserOverlayMode())) {
        adjustLoadSaveListSelectionInDrillMode(delta);
        return;
    }
    const size_t totalRows =
        SetBrowserOverlayPolicy::rootWorkspaceListRowCount(loadSaveListCount_);
    if (totalRows == 0) {
        loadSaveListSelection_ = 0;
        return;
    }
    int next = static_cast<int>(loadSaveListSelection_) + delta;
    if (next < 0) {
        next = 0;
    } else if (static_cast<size_t>(next) >= totalRows) {
        next = static_cast<int>(totalRows) - 1;
    }
    loadSaveListSelection_ = static_cast<uint8_t>(next);
    invalidateLoadSaveDetailCache();

    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;
    if (loadSaveListSelection_ < loadSaveListScrollOffset_) {
        loadSaveListScrollOffset_ = loadSaveListSelection_;
    } else if (loadSaveListSelection_ >= loadSaveListScrollOffset_ + kListVisibleRows) {
        loadSaveListScrollOffset_ =
            loadSaveListSelection_ - static_cast<uint8_t>(kListVisibleRows - 1);
    }
#if defined(SESSION_CAPTURE)
    SC_OVERLAY_SEL(0, loadSaveListSelection_);
#endif
}

void DisplayManager::adjustLoadSaveListSelectionInDrillMode(int delta) {
    if (delta == 0) {
        return;
    }
    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode != StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        return;
    }

    const uint16_t setId = StorageManager::getSetBrowserOverlayDrilledSetId();
    if (!loadSaveRevisionListCacheValid_ || loadSaveRevisionListSetId_ != setId) {
        refreshLoadSaveRevisionHistoryCache(setId);
    }

    const size_t totalRows = loadSaveRevisionListCount_;
    if (totalRows == 0) {
        loadSaveListSelection_ = 0;
        return;
    }
    int next = static_cast<int>(loadSaveListSelection_) + delta;
    if (next < 0) {
        next = 0;
    } else if (static_cast<size_t>(next) >= totalRows) {
        next = static_cast<int>(totalRows) - 1;
    }
    loadSaveListSelection_ = static_cast<uint8_t>(next);
    invalidateLoadSaveDetailCache();

    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;
    if (loadSaveListSelection_ < loadSaveListScrollOffset_) {
        loadSaveListScrollOffset_ = loadSaveListSelection_;
    } else if (loadSaveListSelection_ >= loadSaveListScrollOffset_ + kListVisibleRows) {
        loadSaveListScrollOffset_ =
            loadSaveListSelection_ - static_cast<uint8_t>(kListVisibleRows - 1);
    }
#if defined(SESSION_CAPTURE)
    SC_OVERLAY_SEL(static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                   loadSaveListSelection_);
#endif
}

void DisplayManager::confirmLoadSaveFocusedRow() {
    if (!looperState.isLoadSaveModeActive()) {
        return;
    }

    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode == StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        const uint8_t selection = StorageManager::getRevisionLoadDirtyPromptSelection();
#if defined(SESSION_CAPTURE)
        SC_OVERLAY_CONFIRM(1, selection);
#endif
        switch (selection) {
            case 0:
                StorageManager::confirmRevisionLoadAfterCommit();
                break;
            case 1:
                StorageManager::confirmRevisionLoadDiscardWorkspace();
                break;
            default:
                StorageManager::cancelRevisionLoadRequest();
                break;
        }
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        const uint16_t setId = StorageManager::getSetBrowserOverlayDrilledSetId();
        ensureLoadSaveRevisionListCache(setId, true);
        const uint16_t revisionId = resolveFocusedRevisionId();
        if (revisionId != 0) {
#if defined(SESSION_CAPTURE)
            SC_OVERLAY_CONFIRM(
                static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                loadSaveListSelection_);
#endif
            StorageManager::requestLoadRevision(setId, revisionId);
        } else {
#if defined(SESSION_CAPTURE)
            SC_PERSIST("rev_overlay_load_skip", 0, setId, loadSaveListSelection_,
                       loadSaveRevisionListCount_ > 0 ? "bad_selection" : "empty_list");
#endif
        }
        return;
    }
    if (overlayMode != StorageManager::SetBrowserOverlayMode::Root) {
        return;
    }

    if (SetBrowserOverlayPolicy::isRootSaveRow(loadSaveListSelection_)) {
#if defined(SESSION_CAPTURE)
        SC_OVERLAY_CONFIRM(0, loadSaveListSelection_);
#endif
        StorageManager::beginOverlaySaveRowCommit();
        looperState.exitLoadSaveMode();
#if defined(SESSION_CAPTURE)
        SC_PERSIST("rev_overlay_save", 0, 0, 0, "queued_exit");
#endif
        return;
    }

    const uint16_t setId = resolveFocusedRootSetId();
    if (setId != 0) {
#if defined(SESSION_CAPTURE)
        SC_OVERLAY_CONFIRM(0, loadSaveListSelection_);
#endif
        StorageManager::requestLoadLatestRevisionForSet(setId);
    }
}

#if defined(SESSION_CAPTURE)
void DisplayManager::openRevisionHistoryFromHitl(uint16_t setId) {
    if (!looperState.isLoadSaveModeActive() || setId == 0) {
        return;
    }
    StorageManager::openSetBrowserRevisionHistory(setId, loadSaveListSelection_,
                                                  loadSaveListScrollOffset_);
    invalidateLoadSaveRevisionListCache();
    loadSaveListSelection_ = 0;
    loadSaveListScrollOffset_ = 0;
    SC_OVERLAY_SEL(static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                   loadSaveListSelection_);
}

void DisplayManager::navigateLoadSaveOverlayBackFromHitl() {
    if (!looperState.isLoadSaveModeActive()) {
        return;
    }
    handleLoadSaveOverlayPress(LoadSaveOverlayPressType::Long);
}
#endif

void DisplayManager::drawLoadSaveTrackFilledBar(int x, int y, int barWidth, int barHeight,
                                                uint8_t filledSlots, uint8_t maxSlots,
                                                uint8_t brightness) {
    constexpr int kSegments = Config::MAX_LOOPS_PER_TRACK;
    if (maxSlots == 0 || barWidth < kSegments || barHeight < 1) {
        return;
    }
    const int totalGaps = kSegments - 1;
    const int fullSegmentWidth =
        (barWidth - totalGaps * kLoadSaveSlotHorizontalGap) / kSegments;
    const int segmentWidth = std::max(1, fullSegmentWidth / 2);
    const int segmentPitch = segmentWidth + kLoadSaveSlotHorizontalGap;
    for (int segment = 0; segment < kSegments; ++segment) {
        const int segmentX = x + segment * segmentPitch;
        if (segmentX + segmentWidth > x + barWidth) {
            break;
        }
        const bool filled = static_cast<uint32_t>(filledSlots) * kSegments >
                            static_cast<uint32_t>(segment) * maxSlots;
        const uint8_t segmentBrightness = filled ? brightness : 1;
        for (int py = 0; py < barHeight; ++py) {
            for (int px = 0; px < segmentWidth; ++px) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(), segmentX + px, y + py,
                                        segmentBrightness);
            }
        }
    }
}

void DisplayManager::drawLoadSaveDetailMetricAtColon(int colonX, int y, const char* label,
                                                     const char* value, int labelCharCount,
                                                     bool showUnsavedMarker) {
    const int labelX = colonX - labelCharCount * kLoadSaveDetailCharWidth;
    const int valueX = colonX + kLoadSaveDetailColonWidth;
    _display.gfx.select_font(&Font5x7FixedMono);
    if (showUnsavedMarker) {
        drawLoadSaveDetailUnsavedMarker(_display, labelX, y);
    }
    _display.gfx.draw_text(_display.api.getFrameBuffer(), label, labelX, y,
                           kLoadSaveDetailLabelBrightness);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), ":", colonX, y,
                           kLoadSaveDetailColonBrightness);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), value, valueX, y,
                           kLoadSaveDetailValueBrightness);
}

void DisplayManager::drawLoadSaveDetailMetricLeft(int detailX, int y, const char* label,
                                                    const char* value) {
    drawLoadSaveDetailMetricAtColon(loadSaveDetailLeftColonX(detailX), y, label, value,
                                    kLoadSaveDetailLabelChars);
}

void DisplayManager::drawLoadSaveDetailMetricRight(int y, const char* label, const char* value,
                                                   int labelCharCount, bool showUnsavedMarker) {
    drawLoadSaveDetailMetricAtColon(loadSaveDetailRightColonX(), y, label, value, labelCharCount,
                                    showUnsavedMarker);
}

void DisplayManager::drawLoadSaveDetailDateColumn(uint32_t timestampUnix) {
    RtcTime::LoadSaveDetailDateParts parts{};
    RtcTime::formatLoadSaveDetailDateParts(timestampUnix, parts);
    const char* lines[kLoadSaveDetailDateLineCount] = {parts.year, parts.monthDay,
                                                       parts.timeOfDay};
    const int detailRightX = DISPLAY_WIDTH - 1 - kLoadSaveDetailDateRightMargin;
    _display.gfx.select_font(&Font5x7FixedMono);
    for (int line = 0; line < kLoadSaveDetailDateLineCount; ++line) {
        const int y = loadSaveDetailDateLineY(line);
        const int textWidth =
            static_cast<int>(std::strlen(lines[line])) * kLoadSaveDetailCharWidth;
        const int textX = detailRightX - textWidth + 1;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), lines[line], textX, y,
                               kLoadSaveDetailValueBrightness);
    }
}

void DisplayManager::drawLoadSaveWorkspaceDetail(int detailX,
                                                 const LoadSaveWorkspaceDetailParams& params) {
    _display.gfx.select_font(&Font5x7FixedMono);
    int y = kLoadSaveDetailContentTopY;

    char valueStr[12];
    if (params.setId != 0) {
        std::snprintf(valueStr, sizeof(valueStr), "%2u", static_cast<unsigned>(params.setId));
    } else {
        std::snprintf(valueStr, sizeof(valueStr), "--");
    }
    drawLoadSaveDetailMetricLeft(detailX, y, "Set", valueStr);

    if (params.revisionId != 0) {
        std::snprintf(valueStr, sizeof(valueStr), "%3u", static_cast<unsigned>(params.revisionId));
    } else {
        std::snprintf(valueStr, sizeof(valueStr), "%3s", "--");
    }
    drawLoadSaveDetailMetricRight(y, "Rev", valueStr, 3, params.markRevisionUnsaved);
    y += kLoadSaveTextLineStep;

    std::snprintf(valueStr, sizeof(valueStr), "%2u", params.metrics.trackCount);
    drawLoadSaveDetailMetricLeft(detailX, y, "Tracks", valueStr);
    std::snprintf(valueStr, sizeof(valueStr), "%3u",
                  static_cast<unsigned>(std::min(bpm + 0.5f, 999.0f)));
    drawLoadSaveDetailMetricRight(y, "BPM", valueStr, 3);
    y += kLoadSaveTextLineStep;

    std::snprintf(valueStr, sizeof(valueStr), "%2u", params.metrics.filledSlotCount);
    drawLoadSaveDetailMetricLeft(detailX, y, "Loops ", valueStr);
    std::snprintf(valueStr, sizeof(valueStr), "%3u",
                  static_cast<unsigned>(params.metrics.masterLoopBars));
    drawLoadSaveDetailMetricRight(y, "Bars", valueStr, kLoadSaveDetailRightLabelChars);
    y += kLoadSaveTextLineStep;

    const int detailBarWidth = loadSaveDetailSlotBarWidth(detailX);
    const int slotAreaTop = y;
    const int slotAreaBottom = DISPLAY_HEIGHT;
    const int slotAreaHeight = slotAreaBottom - slotAreaTop;
    constexpr uint8_t kTrackRows = Config::NUM_TRACKS;
    const int baseRowHeight =
        slotAreaHeight > 0 && kTrackRows > 0
            ? (slotAreaHeight - static_cast<int>(kTrackRows - 1) * kLoadSaveTrackRowGap) /
                  static_cast<int>(kTrackRows)
            : 1;
    const int rowHeightRemainder =
        slotAreaHeight - static_cast<int>(kTrackRows) * baseRowHeight -
        static_cast<int>(kTrackRows - 1) * kLoadSaveTrackRowGap;

    int rowY = slotAreaTop;
    for (uint8_t trackIndex = 0; trackIndex < kTrackRows; ++trackIndex) {
        const int rowHeight =
            baseRowHeight + (static_cast<int>(trackIndex) < rowHeightRemainder ? 1 : 0);
        if (detailBarWidth > 0 && rowHeight > 0 && rowY < slotAreaBottom) {
            const int drawHeight = std::min(rowHeight, slotAreaBottom - rowY);
            drawLoadSaveTrackFilledBar(detailX, rowY, detailBarWidth, drawHeight,
                                       params.metrics.perTrackFilledSlots[trackIndex],
                                       Config::MAX_LOOPS_PER_TRACK, 8);
        }
        rowY += rowHeight;
        if (trackIndex + 1 < kTrackRows) {
            rowY += kLoadSaveTrackRowGap;
        }
    }

    drawLoadSaveDetailDateColumn(params.timestampUnix);
}

void DisplayManager::drawAutoSaveBeforeLoadToast(int detailX, uint32_t nowMs) {
    if (autoSaveBeforeLoadToastText_[0] == '\0' ||
        nowMs >= autoSaveBeforeLoadToastExpiresAtMs_) {
        return;
    }
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), autoSaveBeforeLoadToastText_, detailX,
                           DISPLAY_HEIGHT - kLoadSaveTextLineStep, 15);
}

void DisplayManager::drawLoadSaveDirtyPromptView(uint32_t nowMs) {
    constexpr int kLeftMargin = 2;
    constexpr int kRowsStartY = 8;
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Save Current?", kLeftMargin, 0, 15);

    static const char* kRows[] = {"Yes", "No", "Cancel"};
    const uint8_t selection = StorageManager::getRevisionLoadDirtyPromptSelection();
    for (uint8_t row = 0; row < RevisionLoadPolicy::kDirtyPromptRowCount; ++row) {
        const int rowY = kRowsStartY + static_cast<int>(row) * 8;
        const uint8_t brightness = row == selection ? 15 : 5;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), kRows[row], kLeftMargin, rowY,
                               brightness);
    }

    const DeferredSaveDisplayStatus saveStatus =
        StorageManager::getDeferredSaveDisplayStatus(nowMs);
    if (saveStatus.phase != DeferredSaveDisplayPhase::Idle) {
        _display.gfx.draw_text(_display.api.getFrameBuffer(), "Saving", kLeftMargin, 32, 5);
        drawPersistenceStatusDots(kLeftMargin + 6 * 6 + 2, 32 - 4, saveStatus);
    }
    drawSaveStatusIndicator(nowMs, DISPLAY_WIDTH - 4);
}

void DisplayManager::drawLoadSaveMinimalLoadingView(uint32_t nowMs) {
    constexpr int kLeftMargin = 2;
    _display.gfx.select_font(&Font5x7FixedMono);
    if (StorageManager::hasRevisionCommitWork()) {
        _display.gfx.draw_text(_display.api.getFrameBuffer(), "Saving", kLeftMargin, 0, 15);
        const DeferredSaveDisplayStatus saveStatus =
            StorageManager::getDeferredSaveDisplayStatus(nowMs);
        drawPersistenceStatusDots(kLeftMargin + 6 * 6 + 2, kSaveStatusDotY, saveStatus);
    } else {
        const uint16_t setId = StorageManager::getRevisionLoadDisplayTargetSetId();
        if (setId != 0) {
            char label[16];
            std::snprintf(label, sizeof(label), "S%04u", static_cast<unsigned>(setId));
            _display.gfx.draw_text(_display.api.getFrameBuffer(), label, kLeftMargin, 0, 15);
            drawLoadSaveRowLoadStatusDots(kLeftMargin, static_cast<int>(std::strlen(label)), 0,
                                          nowMs, setId, 0);
        } else {
            _display.gfx.draw_text(_display.api.getFrameBuffer(), "Loading...", kLeftMargin, 0, 15);
        }
    }
    drawSaveStatusIndicator(nowMs, DISPLAY_WIDTH - 4);
}

void DisplayManager::drawLoadSaveRevisionHistoryView(uint32_t nowMs, uint16_t setId) {
    ensureLoadSaveRevisionListCache(setId);

    const int kLoadSaveDividerX = loadSaveDividerX();
    const int kLoadSaveDetailX = loadSaveDetailContentX();
    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;

    char header[16];
    std::snprintf(header, sizeof(header), "S%04u", static_cast<unsigned>(setId));
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), header, kLoadSaveLeftPadding, 0, 15);

    for (int row = 0; row < DISPLAY_HEIGHT; ++row) {
        _display.gfx.draw_pixel(_display.api.getFrameBuffer(), kLoadSaveDividerX, row, 2);
    }

    for (int visibleRow = 0; visibleRow < kListVisibleRows; ++visibleRow) {
        const size_t listIndex = static_cast<size_t>(loadSaveListScrollOffset_) +
                                 static_cast<size_t>(visibleRow);
        if (listIndex >= loadSaveRevisionListCount_) {
            break;
        }
        const int rowY = kListRowsStartY + visibleRow * kLoadSaveTextLineStep;
        const bool selected = listIndex == loadSaveListSelection_;
        const uint8_t brightness = selected ? 15 : 5;
        char revisionLabel[8];
        std::snprintf(revisionLabel, sizeof(revisionLabel), "v%04u",
                      static_cast<unsigned>(loadSaveRevisionListEntries_[listIndex].revisionId));
        _display.gfx.draw_text(_display.api.getFrameBuffer(), revisionLabel, kLoadSaveLeftPadding,
                               rowY, brightness);
        drawLoadSaveRowLoadStatusDots(
            kLoadSaveLeftPadding, static_cast<int>(std::strlen(revisionLabel)), rowY, nowMs, setId,
            loadSaveRevisionListEntries_[listIndex].revisionId);
    }

    LoadSaveWorkspaceDetailParams detailParams{};
    if (resolveLoadSaveWorkspaceDetail(detailParams)) {
        drawLoadSaveWorkspaceDetail(kLoadSaveDetailX, detailParams);
    }
}

void DisplayManager::drawLoadSaveLoopPickView(uint16_t setId) {
    constexpr int kLeftMargin = 2;
    char header[16];
    std::snprintf(header, sizeof(header), "S%04u", static_cast<unsigned>(setId));
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), header, kLeftMargin, 0, 15);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Loops", kLeftMargin, 8, 5);
}

void DisplayManager::drawLoadSaveView(uint32_t nowMs) {
    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode == StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        drawLoadSaveDirtyPromptView(nowMs);
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::MinimalLoading) {
        drawLoadSaveMinimalLoadingView(nowMs);
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        drawLoadSaveRevisionHistoryView(nowMs, StorageManager::getSetBrowserOverlayDrilledSetId());
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::LoopPick) {
        drawLoadSaveLoopPickView(StorageManager::getSetBrowserOverlayDrilledSetId());
        return;
    }

    constexpr int kLoadSaveDividerX = loadSaveDividerX();
    const int kLoadSaveDetailX = loadSaveDetailContentX();
    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;

    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Sets", kLoadSaveLeftPadding, 0, 5);

    for (int row = 0; row < DISPLAY_HEIGHT; ++row) {
        _display.gfx.draw_pixel(_display.api.getFrameBuffer(), kLoadSaveDividerX, row, 2);
    }

    const size_t totalRows =
        SetBrowserOverlayPolicy::rootWorkspaceListRowCount(loadSaveListCount_);
    for (int visibleRow = 0; visibleRow < kListVisibleRows; ++visibleRow) {
        const size_t listIndex = static_cast<size_t>(loadSaveListScrollOffset_) +
                                 static_cast<size_t>(visibleRow);
        if (listIndex >= totalRows) {
            break;
        }
        const int rowY = kListRowsStartY + visibleRow * kLoadSaveTextLineStep;
        const bool selected = listIndex == loadSaveListSelection_;
        const uint8_t brightness = selected ? 15 : 5;

        if (SetBrowserOverlayPolicy::isRootSaveRow(static_cast<uint8_t>(listIndex))) {
            _display.gfx.draw_text(_display.api.getFrameBuffer(), "SAVE", kLoadSaveLeftPadding,
                                   rowY, brightness);
            continue;
        }
        if (SetBrowserOverlayPolicy::isRootCurrentRow(static_cast<uint8_t>(listIndex))) {
            _display.gfx.draw_text(_display.api.getFrameBuffer(), "CURRENT", kLoadSaveLeftPadding,
                                   rowY, brightness);
            continue;
        }

        const size_t savedIndex =
            SetBrowserOverlayPolicy::rootSetFolderListIndex(static_cast<uint8_t>(listIndex));
        if (savedIndex < loadSaveListCount_) {
            char browserLabel[24];
            copyLoadSaveBrowserLabel(browserLabel, sizeof(browserLabel),
                                     loadSaveListEntries_[savedIndex].folderName);
            if (loadSaveListEntries_[savedIndex].favorite != 0) {
                const size_t labelLen = std::strlen(browserLabel);
                if (labelLen + 1 < sizeof(browserLabel)) {
                    browserLabel[labelLen] = '*';
                    browserLabel[labelLen + 1] = '\0';
                }
            }
            _display.gfx.draw_text(_display.api.getFrameBuffer(), browserLabel,
                                   kLoadSaveLeftPadding, rowY, brightness);
            uint16_t rowSetId = 0;
            if (SetRevisionCatalog::parseSetIdFromFolderName(
                    loadSaveListEntries_[savedIndex].folderName, rowSetId)) {
                drawLoadSaveRowLoadStatusDots(kLoadSaveLeftPadding,
                                              static_cast<int>(std::strlen(browserLabel)), rowY,
                                              nowMs, rowSetId, 0);
            }
        }
    }

    LoadSaveWorkspaceDetailParams detailParams{};
    drawAutoSaveBeforeLoadToast(kLoadSaveDetailX, nowMs);
    if (resolveLoadSaveWorkspaceDetail(detailParams)) {
        drawLoadSaveWorkspaceDetail(kLoadSaveDetailX, detailParams);
    }
    drawSaveStatusIndicator(nowMs, DISPLAY_WIDTH - 4);
}

void DisplayManager::refreshAutoSaveBeforeLoadToast(uint32_t nowMs) {
    char savedFolder[16];
    if (!StorageManager::consumeAutoSaveBeforeLoadFolder(savedFolder, sizeof(savedFolder))) {
        return;
    }
    const int written =
        std::snprintf(autoSaveBeforeLoadToastText_, sizeof(autoSaveBeforeLoadToastText_),
                      "Saved %s", savedFolder);
    if (written <= 0 ||
        static_cast<size_t>(written) >= sizeof(autoSaveBeforeLoadToastText_)) {
        autoSaveBeforeLoadToastText_[0] = '\0';
        autoSaveBeforeLoadToastExpiresAtMs_ = 0;
        return;
    }
    autoSaveBeforeLoadToastExpiresAtMs_ = nowMs + 1500;
}

// Draw info area
void DisplayManager::drawInfoArea(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot,
                                  uint32_t nowMs) {
    (void)nowMs;
    // 1. Current position (playhead) as musical time, with leading zeros and 2 decimals for ticks
    char posStr[24];
    char lenStr[8];
    char loopStr[12];
    char chnStr[4];
    char midiOutLabel[8];
    // Get length of loop (selected slot when not in jam overlay)
    const uint32_t lengthLoop = selectedTrack.isJamming() ? selectedTrack.getLoopLength()
                                                          : selectedTrack.getLoopLengthForSlot(displaySlot);
    
    const uint32_t playheadInLoop = resolvePlayheadInLoop(selectedTrack, displaySlot, currentTick);
    if (lengthLoop > 0) {
        ticksToBarsBeats16thTicks2Dec(playheadInLoop, posStr, sizeof(posStr), true);
    } else {
        ticksToBarsBeats16thTicks2Dec(currentTick, posStr, sizeof(posStr), true);
    }
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
        selectedIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
            selection, notes, loopStartTick, lengthLoop,
            editManager.isLengthBracketEditActive());
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
            const uint32_t relativeCurrentTick =
                resolvePlayheadInLoop(selectedTrack, displaySlot, currentTick);
            
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
        uint32_t lenVal =
            NoteMovementUtils::calculateNoteLength(noteToShow->startTick, noteToShow->endTick,
                                                 lengthLoop);
        uint8_t velVal = noteToShow->velocity;
        if (editManager.isNoteEditActive()) {
            const NoteEditFocus& focus = editManager.getEditSession().focus;
            if (focus.active && editorSelectionHasNote(selection) &&
                focus.movingNoteId == selection.primaryNote) {
                noteVal = focus.last.pitch;
                velVal = focus.last.velocity;
                lenVal = NoteMovementUtils::calculateNoteLength(focus.last.startTick,
                                                               focus.last.endTick, lengthLoop);
                const uint32_t storageBracketTick = editManager.isLengthBracketEditActive()
                                                        ? focus.last.endTick
                                                        : focus.last.startTick;
                displayStartTick = NoteEditDisplaySnapshot::displayStartTickFromStorage(
                    storageBracketTick, loopStartTick, lengthLoop);
            }
        }
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
            const uint32_t storageStart = noteToShow->startTick;
            if (noteVal != capLastPitch || storageStart != capLastStorage ||
                displayStartTick != capLastDisplay) {
                capLastPitch = noteVal;
                capLastStorage = storageStart;
                capLastDisplay = displayStartTick;
                SC_DNTE(noteVal, storageStart, displayStartTick, lenVal, selectedIdx);
            }
        }
#endif
    }

    // Same width as leading-zero musical time (e.g. 01:01:01:00); keeps NOTE row layout when no note/slot data.
    if (!(noteToShow && lengthLoop > 0)) {
        snprintf(startStr, sizeof(startStr), "--:--:--:--");
    }

    int x = DisplayManager::TRACK_MARGIN;
    int y = DISPLAY_HEIGHT;
    // Draw the time string (ticksToBarsBeats16thTicks2Dec)
    // Highlight the time when in NOTE_EDIT mode (simplified since we use dedicated faders)
    bool isStartNote = (editManager.getEditSessionType() == EditSessionType::Note);
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

void DisplayManager::requestNoteInfoRefresh(Track& track) {
    (void)track.getCachedNotes();
}

void DisplayManager::applyWorkspaceDisplayRefreshPending(uint32_t currentTick) {
    if (!workspaceDisplayRefreshPending_) {
        return;
    }
    workspaceDisplayRefreshPending_ = false;
    invalidateLiveDisplayCache();
    // Do not mark every track/slot visual cache stale here — that forced a full
    // rematerialize on the next frame under ~64 KB internal heap and hung the OLED
    // after the first good piano-roll paint (grid-only freeze).
    Track& selectedTrack = trackManager.getSelectedTrack();
    editManager.rematerializeNoteEditSessionAfterWorkspaceReload(selectedTrack);
    trackManager.forceLedUpdate(currentTick);
}

void DisplayManager::update() {
    const uint32_t telemetryStartUs = micros();
    uint32_t currentTick = clockManager.getCurrentTick();
    const bool loadSaveActive = looperState.isLoadSaveModeActive();
    if (!loadSaveActive && StorageManager::consumeRevisionLoadDisplayRefreshPending()) {
        workspaceDisplayRefreshPending_ = true;
    }
    Track& selTrack = trackManager.getSelectedTrack();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    uint32_t displayTick = selTrack.getEffectivePlaybackTick(currentTick);
    uint32_t now = millis();
    refreshAutoSaveBeforeLoadToast(now);

    if (loadSaveActive && !loadSaveModeWasActive_) {
        const bool preserveNavigation = SetBrowserOverlayPolicy::shouldPreserveOverlayNavigationOnEnter(
            StorageManager::getSetBrowserOverlayPersistencePhase());
        if (!preserveNavigation) {
            StorageManager::resetSetBrowserOverlayNavigation();
            loadSaveListSelection_ = 0;
            loadSaveListScrollOffset_ = 0;
            invalidateLoadSaveRevisionListCache();
        }
        if (!loadSaveListCacheValid_ && StorageManager::isOverlayCatalogReadAllowed()) {
            refreshLoadSaveListCache();
        }
        invalidateLoadSaveDetailCache();
    } else if (!loadSaveActive && loadSaveModeWasActive_) {
        if (StorageManager::isRevisionLoadHeldForWorkspaceDirty()) {
            StorageManager::cancelRevisionLoadRequest();
        }
        StorageManager::resetSetBrowserOverlayNavigation();
        invalidateLoadSaveRevisionListCache();
        invalidateLoadSaveDetailCache();
    }
    loadSaveModeWasActive_ = loadSaveActive;

    applyWorkspaceDisplayRefreshPending(currentTick);

    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);

    if (loadSaveActive) {
        drawLoadSaveView(now);
        _display.api.display();
        HotPathTelemetry::recordDisplayUpdate(micros() - telemetryStartUs);
        return;
    }

    drawTrackStatus(trackManager.getSelectedTrackIndex(), now);
    const DisplayNoteVec& frameNotes = resolveDisplayNotes(selTrack, displaySlot, displayTick);
#if defined(SESSION_CAPTURE)
    maybeEmitDisplayCaptureOnChange(selTrack, displaySlot, displayTick, frameNotes);
#endif
    drawPianoRoll(displayTick, selTrack, displaySlot, frameNotes);
    drawSidebar(selTrack, displaySlot);
    drawInfoArea(displayTick, selTrack, displaySlot, now);
    drawNoteInfo(displayTick, selTrack, displaySlot, frameNotes);

   _display.api.display();
#if defined(SESSION_CAPTURE)
    {
        static uint32_t dframeCounter = 0;
        if (++dframeCounter % 30U == 0U) {
            SC_DFRAME(static_cast<uint32_t>(frameNotes.size()),
                      static_cast<uint32_t>(micros() - telemetryStartUs), dframeCounter);
        }
    }
#endif
    HotPathTelemetry::recordDisplayUpdate(micros() - telemetryStartUs);
}

#if defined(SESSION_CAPTURE)
namespace HitlDisplayBridge {

void confirmLoadSaveFocusedRow() { displayManager.confirmLoadSaveFocusedRow(); }

void adjustLoadSaveListSelection(int delta) { displayManager.adjustLoadSaveListSelection(delta); }

void openRevisionHistoryFromHitl(uint16_t setId) {
    displayManager.openRevisionHistoryFromHitl(setId);
}

void navigateLoadSaveOverlayBackFromHitl() {
    displayManager.navigateLoadSaveOverlayBackFromHitl();
}

}  // namespace HitlDisplayBridge
#endif
