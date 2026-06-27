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
#include "MidiHandler.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/DisplayWindowUtils.h"
#include "TrackStateMachine.h"
#include "MidiButtonManager.h"
#include "MidiConfig.h"
#include "StorageManager.h"
#include "RtcTime.h"
#include "DeferredSaveDisplayStatus.h"
#include "LooperState.h"
#include "SavedSetCatalog.h"
#include <algorithm>
#include <string>
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>

DisplayManager displayManager;
namespace {
MidiEventVec liveDisplayEventBuffer;

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
    if (isPlayStopButtonHeld()) {
        return clockManager.getDisplayTickPhase();
    }
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        return 0.0f;
    }
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
    MidiEventVec captureFlat;
    loop.capture.store.flatten(captureFlat);
    return NoteUtils::findOpenNoteOns(captureFlat, loop.loopLengthTicks);
}

bool findPreferredWrapHeadOffTick(const MidiEventVec& midiEvents, const NoteUtils::OpenNoteOn& open,
                                  uint32_t loopLength, uint32_t& headOffTickOut) {
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
                               const MidiEventVec& captureEvents, uint32_t loopLength,
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

void applyLiveOpenTails(const std::vector<NoteUtils::OpenNoteOn>& openNotes,
                        const MidiEventVec& midiEvents, uint32_t loopLength, uint32_t closeTick,
                        DisplayNoteVec& notes, bool extendHeldNotesToPlayhead) {
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
    if (editManager.getEditSessionType() != EditSessionType::Loop) {
        return 0;
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
    const uint32_t tickInLoop = tickPhaseInLoop(displayTick, dispLoop.startLoopTick, loopLength);
    const uint32_t loopOrigin = resolveLoopOriginTick(track, displaySlot);
    return (tickInLoop - loopOrigin + loopLength) % loopLength;
}

const DisplayNoteVec& DisplayManager::resolveDisplayNotes(const Track& track, uint8_t displaySlot,
                                                          uint32_t currentTick) {
    if (track.isJamming()) {
        invalidateLiveDisplayCache();
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
                                                        : loop.liveEventCount();
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
                const_cast<Loop&>(loop).ensureVisualCacheBuilt();
                liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
                committedDisplayEnd = liveDisplayNotes.size();
                MidiEventVec captureFlat;
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
                MidiEventVec captureFlat;
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

        if (cacheCold || contextChanged || eventsShrunk || eventsAdded || loopLengthChanged ||
            captureRevisionChanged) {
            if (track.isOverdubbing()) {
                loop.mergeMaterializedPassesWithCapture(liveDisplayEventBuffer);
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
        }

        if (track.isRecording() || track.isOverdubbing()) {
            if (track.isOverdubbing()) {
                loop.mergeMaterializedPassesWithCapture(liveDisplayEventBuffer);
            }
            rebuildLiveDisplayNotes();
            const uint32_t playheadCloseTick = resolvePlayheadInLoop(track, displaySlot, currentTick);
            if (track.isOverdubbing()) {
                liveDisplayCacheOpenNotes =
                    NoteUtils::findOpenNoteOns(liveDisplayEventBuffer, liveLoopLength);
                MidiEventVec captureEvents;
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
        invalidateLiveDisplayCache();
        const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
        if (editManager.isNoteEditActive() && loopLength > 0) {
            const NoteEditFocus& focus = editManager.getEditSession().focus;
            const std::vector<DisplayNote> filtered =
                filterSelectableDisplayNotes(track.editAwareMidiEvents(), focus,
                                             track.getMidiChannel(), loopLength);
            liveDisplayNotes.assign(filtered.begin(), filtered.end());
            return liveDisplayNotes;
        }
        const auto& cachedNotes = track.getCachedNotes();
        liveDisplayNotes.assign(cachedNotes.begin(), cachedNotes.end());
        return liveDisplayNotes;
    }

    invalidateLiveDisplayCache();
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (loopLength == 0 || (!loop.hasPublishedEvents() && loop.liveEventCount() == 0)) {
        liveDisplayNotes.clear();
        return liveDisplayNotes;
    }

    Loop& mutLoop = const_cast<Loop&>(loop);
    mutLoop.ensureVisualCacheBuilt();
    mutLoop.mergeMaterializedPassesWithCapture(liveDisplayEventBuffer);

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
            // Playback / edit display: wrap-held tails only — not live note-on lengthening.
            applyLiveOpenTails(openNotes, liveDisplayEventBuffer, loopLength, playheadCloseTick,
                               liveDisplayNotes, false);
        }
    }
    return liveDisplayNotes;
}

void DisplayManager::emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot,
                                                uint32_t currentTick) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLen = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const DisplayNoteVec& frameNotes = resolveDisplayNotes(track, displaySlot, currentTick);
    const size_t bufferEvents =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size()
                                                    : loop.liveEventCount();
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

void DisplayManager::maybeEmitDisplayCaptureOnChange(const Track& track, uint8_t displaySlot,
                                                     uint32_t currentTick, size_t frameNoteCount) {
    static size_t lastFrameNotes = static_cast<size_t>(-1);
    static uint8_t lastSlot = 255;
    static TrackState lastState = NUM_TRACK_STATES;
    static uint32_t lastLoopLen = 0;
    static size_t lastTakeEvents = static_cast<size_t>(-1);

    const Loop& loop = track.getLoop(displaySlot);
    const TrackState state = track.getState();
    const uint32_t loopLen = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const size_t takeEvents =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size()
                                                    : loop.liveEventCount();

    const bool changed = frameNoteCount != lastFrameNotes || displaySlot != lastSlot ||
                         state != lastState || loopLen != lastLoopLen ||
                         takeEvents != lastTakeEvents;
    const bool regression =
        loopLen > 0 && loop.hasPublishedEvents() && frameNoteCount == 0 && takeEvents > 0;

    if (!changed && !regression) {
        return;
    }

    lastFrameNotes = frameNoteCount;
    lastSlot = displaySlot;
    lastState = state;
    lastLoopLen = loopLen;
    lastTakeEvents = takeEvents;
    emitDisplayCaptureSnapshot(track, displaySlot, currentTick);
}

void DisplayManager::invalidateLiveDisplayCache() {
    liveDisplayCacheEventCount = static_cast<size_t>(-1);
    liveDisplayCacheCaptureRevision = 0;
    liveDisplayCacheLoopLength = 0;
    liveDisplayCacheSlot = 255;
    liveDisplayCacheTrackState = NUM_TRACK_STATES;
    liveDisplayCacheOpenNotes.clear();
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
    //Serial.println("Text drawn.");
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
void DisplayManager::drawAllNotes(const Track& track, uint8_t displaySlot, uint32_t currentTick,
                                  uint32_t lengthLoop, int minPitch, int maxPitch, int pianoRollY0,
                                  int pianoRollY1, bool windowRelativeTicks,
                                  const DisplayNoteVec& notes) {
    const uint32_t loopLength = track.isJamming() ? track.getLoopLength()
                                                  : resolveDisplayLoopLength(track, displaySlot, currentTick);
    const uint32_t jamStartTick = track.isJamming() ? track.getJamStartTick()
                                                    : resolveLoopOriginTick(track, displaySlot);
    int selectedIdx = editManager.getSelectedNoteIdx();

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
void DisplayManager::drawBracket(uint32_t bracketTick, uint32_t lengthLoop, int pianoRollY1) {
    // Draw bracket when in NOTE_EDIT mode (simplified since we use dedicated faders)
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        int bracketX = TRACK_MARGIN + map(bracketTick, 0, lengthLoop, 0, pianoRollWidth());
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
                     pianoRollY1, useBoundedWindow, *detailedNotes);

        // Adjust bracket tick to be relative to jam start
        uint32_t bracketTick = editManager.getBracketTick();
        uint32_t relativeBracketTick = (bracketTick - jamStartTick + loopLength) % loopLength;
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
        _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, kSaveStatusDotY, brightness);
    }
}

void DisplayManager::refreshLoadSaveListCache() {
    loadSaveListCount_ =
        StorageManager::listSavedSetFolderEntries(loadSaveListEntries_, kLoadSaveListCapacity);
    loadSaveListCacheValid_ = true;
}

void DisplayManager::adjustLoadSaveListSelection(int delta) {
    if (delta == 0) {
        return;
    }
    const size_t totalRows = 1 + loadSaveListCount_;
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

    constexpr int kListRowsStartY = 8;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / 8;
    if (loadSaveListSelection_ < loadSaveListScrollOffset_) {
        loadSaveListScrollOffset_ = loadSaveListSelection_;
    } else if (loadSaveListSelection_ >= loadSaveListScrollOffset_ + kListVisibleRows) {
        loadSaveListScrollOffset_ =
            loadSaveListSelection_ - static_cast<uint8_t>(kListVisibleRows - 1);
    }
}

void DisplayManager::drawLoadSaveTrackFilledBar(int x, int y, uint8_t filledSlots,
                                                uint8_t maxSlots, uint8_t brightness) {
    constexpr int kSegments = 8;
    constexpr int kSegmentWidth = 3;
    constexpr int kSegmentGap = 1;
    if (maxSlots == 0) {
        maxSlots = 1;
    }
    for (int segment = 0; segment < kSegments; ++segment) {
        const bool filled = static_cast<uint32_t>(filledSlots) * kSegments >
                            static_cast<uint32_t>(segment) * maxSlots;
        const uint8_t segmentBrightness = filled ? brightness : 1;
        for (int py = 0; py < 5; ++py) {
            for (int px = 0; px < kSegmentWidth; ++px) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(),
                                        x + segment * (kSegmentWidth + kSegmentGap) + px, y + py,
                                        segmentBrightness);
            }
        }
    }
}

void DisplayManager::drawLoadSaveSetDetail(int detailX, const SavedSetCatalog::SavedSetMetadata& metadata,
                                           bool isCurrentRow, const char* folderName,
                                           uint32_t nowMs) {
    _display.gfx.select_font(&Font5x7FixedMono);
    int y = 0;

    char line[40];
    if (metadata.userLabel[0] != '\0') {
        std::snprintf(line, sizeof(line), "UID: %s", metadata.userLabel);
    } else if (metadata.sequence != 0) {
        std::snprintf(line, sizeof(line), "UID: %05lu",
                      static_cast<unsigned long>(metadata.sequence));
    } else {
        std::snprintf(line, sizeof(line), "UID: --");
    }
    _display.gfx.draw_text(_display.api.getFrameBuffer(), line, detailX, y, 15);
    y += 8;

    char dateTime[40];
    RtcTime::formatDetailDateTime(metadata.createdAtUnix, dateTime, sizeof(dateTime));
    if (dateTime[0] == '\0') {
        std::snprintf(dateTime, sizeof(dateTime), "--");
    }
    _display.gfx.draw_text(_display.api.getFrameBuffer(), dateTime, detailX, y, 5);
    y += 8;

    std::snprintf(line, sizeof(line), "Bars: %u", metadata.masterLoopBars);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), line, detailX, y, 5);
    y += 8;

    std::snprintf(line, sizeof(line), "Tracks: %u", metadata.trackCount);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), line, detailX, y, 5);
    y += 8;

    std::snprintf(line, sizeof(line), "Loops: %u", metadata.filledSlotCount);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), line, detailX, y, 5);
    y += 10;

    const uint8_t trackRows = metadata.trackCount > 0 ? metadata.trackCount : 0;
    for (uint8_t trackIndex = 0; trackIndex < trackRows; ++trackIndex) {
        std::snprintf(line, sizeof(line), "T%u", static_cast<unsigned>(trackIndex + 1));
        _display.gfx.draw_text(_display.api.getFrameBuffer(), line, detailX, y, 5);
        drawLoadSaveTrackFilledBar(detailX + 18, y + 1, metadata.perTrackFilledSlots[trackIndex],
                                   Config::MAX_LOOPS_PER_TRACK, 8);
        y += 8;
    }

    if (isCurrentRow && folderName != nullptr && folderName[0] != '\0' &&
        (autoSaveBeforeLoadToastText_[0] == '\0' ||
         nowMs >= autoSaveBeforeLoadToastExpiresAtMs_)) {
        char fromLine[32];
        std::snprintf(fromLine, sizeof(fromLine), "From: %s", folderName);
        _display.gfx.draw_text(_display.api.getFrameBuffer(), fromLine, detailX,
                               DISPLAY_HEIGHT - 8, 5);
    }
}

void DisplayManager::drawAutoSaveBeforeLoadToast(int detailX, uint32_t nowMs) {
    if (autoSaveBeforeLoadToastText_[0] == '\0' ||
        nowMs >= autoSaveBeforeLoadToastExpiresAtMs_) {
        return;
    }
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), autoSaveBeforeLoadToastText_, detailX,
                           DISPLAY_HEIGHT - 8, 15);
}

void DisplayManager::drawLoadSaveView(uint32_t nowMs) {
    constexpr int kLoadSaveDividerX = DISPLAY_WIDTH / 2;
    constexpr int kLoadSaveRightX = kLoadSaveDividerX + 1;
    constexpr int kLoadSaveLeftMargin = 2;
    constexpr int kLoadSaveDetailMargin = kLoadSaveRightX + 2;
    constexpr int kListRowsStartY = 8;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / 8;

    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Sets", kLoadSaveLeftMargin, 0, 5);

    for (int row = 0; row < DISPLAY_HEIGHT; ++row) {
        _display.gfx.draw_pixel(_display.api.getFrameBuffer(), kLoadSaveDividerX, row, 2);
    }

    const size_t totalRows = 1 + loadSaveListCount_;
    for (int visibleRow = 0; visibleRow < kListVisibleRows; ++visibleRow) {
        const size_t listIndex = static_cast<size_t>(loadSaveListScrollOffset_) +
                                 static_cast<size_t>(visibleRow);
        if (listIndex >= totalRows) {
            break;
        }
        const int rowY = kListRowsStartY + visibleRow * 8;
        const bool selected = listIndex == loadSaveListSelection_;
        const uint8_t brightness = selected ? 15 : 5;

        if (listIndex == 0) {
            _display.gfx.draw_text(_display.api.getFrameBuffer(), "CURRENT", kLoadSaveLeftMargin,
                                   rowY, brightness);
            continue;
        }

        const size_t savedIndex = listIndex - 1;
        if (savedIndex < loadSaveListCount_) {
            _display.gfx.draw_text(_display.api.getFrameBuffer(),
                                   loadSaveListEntries_[savedIndex].folderName, kLoadSaveLeftMargin,
                                   rowY, brightness);
        }
    }

    SavedSetCatalog::SavedSetMetadata detailMetadata{};
    bool detailOk = false;
    bool isCurrentRow = loadSaveListSelection_ == 0;
    const char* folderName = nullptr;
    char loadedFromFolder[16];

    if (isCurrentRow) {
        detailOk = StorageManager::readCurrentSetBrowserMetadata(detailMetadata);
        if (StorageManager::copyCurrentSetLoadedFromFolder(loadedFromFolder,
                                                           sizeof(loadedFromFolder))) {
            folderName = loadedFromFolder;
        }
    } else {
        const size_t savedIndex = static_cast<size_t>(loadSaveListSelection_) - 1;
        if (savedIndex < loadSaveListCount_) {
            folderName = loadSaveListEntries_[savedIndex].folderName;
            detailOk = StorageManager::readSavedSetMetadataForFolder(folderName, detailMetadata);
        }
    }

    drawAutoSaveBeforeLoadToast(kLoadSaveDetailMargin, nowMs);
    if (detailOk) {
        drawLoadSaveSetDetail(kLoadSaveDetailMargin, detailMetadata, isCurrentRow, folderName, nowMs);
    }
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

void DisplayManager::update() {
    const uint32_t telemetryStartUs = micros();
    uint32_t currentTick = clockManager.getCurrentTick();
    Track& selTrack = trackManager.getSelectedTrack();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    uint32_t displayTick = selTrack.getEffectivePlaybackTick(currentTick);
    uint32_t now = millis();
    refreshAutoSaveBeforeLoadToast(now);

    const bool loadSaveActive = looperState.isLoadSaveModeActive();
    if (loadSaveActive && !loadSaveModeWasActive_) {
        refreshLoadSaveListCache();
        loadSaveListSelection_ = 0;
        loadSaveListScrollOffset_ = 0;
    }
    loadSaveModeWasActive_ = loadSaveActive;

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
    maybeEmitDisplayCaptureOnChange(selTrack, displaySlot, displayTick, frameNotes.size());
#endif
    drawPianoRoll(displayTick, selTrack, displaySlot, frameNotes);
    drawSidebar(selTrack, displaySlot);
    drawInfoArea(displayTick, selTrack, displaySlot, now);
    drawNoteInfo(displayTick, selTrack, displaySlot, frameNotes);

   _display.api.display();
    HotPathTelemetry::recordDisplayUpdate(micros() - telemetryStartUs);
}
