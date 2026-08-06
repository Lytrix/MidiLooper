//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "NoteEditFocus.h"
#include "Track.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/NoteUtils.h"

namespace {

#if defined(SESSION_CAPTURE)
void logGeomApplyQueue(uint8_t kind, uint32_t targetField, bool transportRunning) {
    logger.info("#CAP,%lu,GEOM_APPLY,queue,%u,%lu,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned>(kind), static_cast<unsigned long>(targetField),
                transportRunning ? 1u : 0u);
}

void logGeomApplyDequeue(uint32_t queueAgeMs, uint8_t kind) {
    logger.info("#CAP,%lu,GEOM_APPLY,dequeue,%lu,%u,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(queueAgeMs), static_cast<unsigned>(kind));
}

void logGeomApplyDone(bool applied, uint32_t displayRevision) {
    logger.info("#CAP,%lu,GEOM_APPLY,done,%u,%lu,0,0", static_cast<unsigned long>(micros()),
                applied ? 1u : 0u, static_cast<unsigned long>(displayRevision));
}

void logGeomApplySkip(uint8_t reasonCode) {
    logger.info("#CAP,%lu,GEOM_APPLY,skip,%u,0,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned>(reasonCode));
}
#endif

}  // namespace
namespace {

#if defined(SESSION_CAPTURE)
void logGeomApplyQueue(uint8_t kind, uint32_t targetField, bool transportRunning) {
    logger.info("#CAP,%lu,GEOM_APPLY,queue,%u,%lu,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned>(kind), static_cast<unsigned long>(targetField),
                transportRunning ? 1u : 0u);
}

void logGeomApplyDequeue(uint32_t queueAgeMs, uint8_t kind) {
    logger.info("#CAP,%lu,GEOM_APPLY,dequeue,%lu,%u,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(queueAgeMs), static_cast<unsigned>(kind));
}

void logGeomApplyDone(bool applied, uint32_t displayRevision) {
    logger.info("#CAP,%lu,GEOM_APPLY,done,%u,%lu,0,0", static_cast<unsigned long>(micros()),
                applied ? 1u : 0u, static_cast<unsigned long>(displayRevision));
}

void logGeomApplySkip(uint8_t reasonCode) {
    logger.info("#CAP,%lu,GEOM_APPLY,skip,%u,0,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned>(reasonCode));
}
#endif

}  // namespace
NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingMove(const NoteUtils::DisplayNote& note,
                                                                  uint32_t targetTick) {
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::Move;
    pendingPlayingGeometryNote_ = note;
    pendingPlayingGeometryTargetTick_ = targetTick;
    pendingPlayingGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingGeometryType::Move), targetTick,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingLength(const NoteUtils::DisplayNote& note,
                                                                    uint32_t targetEndTick) {
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::Length;
    pendingPlayingGeometryNote_ = note;
    pendingPlayingGeometryTargetTick_ = targetEndTick;
    pendingPlayingGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingGeometryType::Length), targetEndTick,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingPitch(const NoteUtils::DisplayNote& note,
                                                                   uint8_t currentPitch,
                                                                   uint8_t newPitch) {
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::Pitch;
    pendingPlayingGeometryNote_ = note;
    pendingPlayingGeometryPitchCurrent_ = currentPitch;
    pendingPlayingGeometryPitchNew_ = newPitch;
    pendingPlayingGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingGeometryType::Pitch), newPitch,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::finishGeometryDriverSideEffects(
    Track& track, uint32_t now, MidiMapping::FaderType driverFader) {
    clearSelectionRelatchAfterGeometry();
    clearGeometryRelatchCycleEligibility();
    refreshEditingActivity();
    currentDriverFader = driverFader;
    lastDriverFaderTime = now;

    switch (driverFader) {
        case MidiMapping::FaderType::FADER_COARSE:
        case MidiMapping::FaderType::FADER_FINE:
            syncSelectionFromGeometryEdit(track);
            if (!clockManager.isTransportRunning()) {
                publishDependentFaderLatch(track, driverFader);
            }
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            if (!clockManager.isTransportRunning()) {
                publishDependentFaderLatch(track, driverFader);
            }
            break;
        default:
            break;
    }

    if (driverFader == MidiMapping::FaderType::FADER_COARSE &&
        !clockManager.isTransportRunning()) {
        const bool geometryDriverWasIdle = !isGeometryDriverActive(now);
        if (geometryDriverWasIdle && pendingGeometryDriverMotorSyncValid_) {
            processPendingGeometryDriverMotorSync(track, true);
        }
    }
}

NOTE_EDIT_MEM bool ControlSurfaceManager::applyPlayingPitchGeometry(Track& track,
                                                                    const NoteUtils::DisplayNote& liveNote,
                                                                    uint8_t currentPitch,
                                                                    uint8_t newPitch,
                                                                    bool refreshPlaybackPreview) {
#if defined(SESSION_CAPTURE)
    const uint32_t focusStartUs = micros();
#endif
    editManager.ensureNoteEditFocusForLiveEdit(track, liveNote);
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,GEOM_APPLY,focus,%lu,%u,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - focusStartUs),
                static_cast<unsigned>(NoteEditKind::Pitch));
    const uint32_t undoStartUs = micros();
#endif
    if (!editManager.beginGeometryMutation(track, NoteEditKind::Pitch, true)) {
#if defined(SESSION_CAPTURE)
        logger.info("#CAP,%lu,GEOM_APPLY,undo,fail,%lu,%u,0", static_cast<unsigned long>(micros()),
                    static_cast<unsigned long>(micros() - undoStartUs),
                    static_cast<unsigned>(NoteEditKind::Pitch));
#endif
        logger.log(CAT_MIDI, LOG_WARNING,
                   "Note pitch change aborted: session undo snapshot unavailable (heap reserve)");
        return false;
    }
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,GEOM_APPLY,undo,ok,%lu,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - undoStartUs),
                static_cast<unsigned>(NoteEditKind::Pitch));
    const uint32_t pipelineStartUs = micros();
#endif

    NoteUtils::DisplayNote pitchTarget = liveNote;
    pitchTarget.startTick = liveNote.startTick;
    pitchTarget.endTick = liveNote.endTick;
    const bool pitchUpdated = NoteMovementUtils::applyNoteEditChange(
        track, editManager, NoteMovementUtils::NoteEditChangeKind::Pitch, pitchTarget, 0, 0, 0,
        currentPitch, newPitch, pitchTarget.startTick, pitchTarget.endTick, refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,GEOM_APPLY,pipeline,%lu,%u,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - pipelineStartUs), pitchUpdated ? 1u : 0u,
                static_cast<unsigned>(NoteEditKind::Pitch));
#endif
    if (!pitchUpdated) {
        return false;
    }

    NoteEditFocus& focus = editManager.getEditSession().focus;
    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        focus.baselineMap[focus.movingNoteId] = focus.last;
    }
    return true;
}

NOTE_EDIT_MEM void ControlSurfaceManager::processPendingPlayingGeometry(Track& track) {
    if (pendingPlayingGeometryType_ == PendingPlayingGeometryType::None) {
        return;
    }
    if (!clockManager.isTransportRunning()) {
#if defined(SESSION_CAPTURE)
        logGeomApplySkip(1);
#endif
        pendingPlayingGeometryType_ = PendingPlayingGeometryType::None;
        return;
    }
    const uint32_t queueAgeMs =
        pendingPlayingGeometryQueuedAtMs_ > 0 ? millis() - pendingPlayingGeometryQueuedAtMs_ : 0;
#if defined(SESSION_CAPTURE)
    logGeomApplyDequeue(queueAgeMs,
                        static_cast<uint8_t>(pendingPlayingGeometryType_));
#endif
    editManager.processKindBoundaryUndoWarm(track);
    const uint32_t now = millis();

    const PendingPlayingGeometryType kind = pendingPlayingGeometryType_;
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::None;
    pendingPlayingGeometryQueuedAtMs_ = 0;

    bool applied = false;
    MidiMapping::FaderType driverFader = MidiMapping::FaderType::FADER_COARSE;
    switch (kind) {
        case PendingPlayingGeometryType::Move:
            applied = editManager.moveNoteToPosition(track, pendingPlayingGeometryNote_,
                                                     pendingPlayingGeometryTargetTick_);
            driverFader = MidiMapping::FaderType::FADER_COARSE;
            break;
        case PendingPlayingGeometryType::Length:
            applied = editManager.changeNoteEndWithOverlapHandling(
                track, pendingPlayingGeometryNote_, pendingPlayingGeometryTargetTick_);
            driverFader = MidiMapping::FaderType::FADER_COARSE;
            break;
        case PendingPlayingGeometryType::Pitch:
            applied = applyPlayingPitchGeometry(track, pendingPlayingGeometryNote_,
                                                pendingPlayingGeometryPitchCurrent_,
                                                pendingPlayingGeometryPitchNew_, true);
            driverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
            break;
        case PendingPlayingGeometryType::None:
            break;
    }
    if (applied) {
        finishGeometryDriverSideEffects(track, now, driverFader);
    }
#if defined(SESSION_CAPTURE)
    logGeomApplyDone(applied, editManager.sessionPreviewRevision());
#endif
}
