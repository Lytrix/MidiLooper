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
#include "NoteEditGeometryApply.h"
#if defined(SESSION_CAPTURE)
#include "EditManagerInternal.h"
#endif
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

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingEditMove(const NoteUtils::DisplayNote& note,
                                                                    uint32_t targetTick) {
    pendingPlayingEditGeometryType_ = PendingPlayingEditGeometryType::Move;
    pendingPlayingEditGeometryNote_ = note;
    pendingPlayingEditGeometryTargetTick_ = targetTick;
    pendingPlayingEditGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingEditGeometryType::Move), targetTick,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingEditLength(const NoteUtils::DisplayNote& note,
                                                                      uint32_t targetEndTick) {
    pendingPlayingEditGeometryType_ = PendingPlayingEditGeometryType::Length;
    pendingPlayingEditGeometryNote_ = note;
    pendingPlayingEditGeometryTargetTick_ = targetEndTick;
    pendingPlayingEditGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingEditGeometryType::Length), targetEndTick,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingEditPitch(const NoteUtils::DisplayNote& note,
                                                                     uint8_t currentPitch,
                                                                     uint8_t newPitch) {
    pendingPlayingEditGeometryType_ = PendingPlayingEditGeometryType::Pitch;
    pendingPlayingEditGeometryNote_ = note;
    pendingPlayingEditGeometryPitchCurrent_ = currentPitch;
    pendingPlayingEditGeometryPitchNew_ = newPitch;
    pendingPlayingEditGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingEditGeometryType::Pitch), newPitch,
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

NOTE_EDIT_MEM bool ControlSurfaceManager::applyPlayingEditPitchGeometry(Track& track,
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
    const uint32_t resolveStartUs = micros();
#endif

    NoteUtils::DisplayNote pitchTarget = liveNote;
    pitchTarget.startTick = liveNote.startTick;
    pitchTarget.endTick = liveNote.endTick;
    const bool pitchUpdated = NoteEditGeometryApply::applyNoteEditChange(
        track, editManager, NoteEditGeometryApply::NoteEditChangeKind::Pitch, pitchTarget, 0, 0, 0,
        currentPitch, newPitch, pitchTarget.startTick, pitchTarget.endTick, refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
    logGeomApplyResolve(micros() - resolveStartUs, pitchUpdated, NoteEditKind::Pitch);
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

NOTE_EDIT_MEM void ControlSurfaceManager::processPendingPlayingEditGeometry(Track& track) {
    if (pendingPlayingEditGeometryType_ == PendingPlayingEditGeometryType::None) {
        return;
    }
    if (!clockManager.isTransportRunning()) {
#if defined(SESSION_CAPTURE)
        logGeomApplySkip(1);
#endif
        pendingPlayingEditGeometryType_ = PendingPlayingEditGeometryType::None;
        return;
    }
    const uint32_t queueAgeMs =
        pendingPlayingEditGeometryQueuedAtMs_ > 0 ? millis() - pendingPlayingEditGeometryQueuedAtMs_ : 0;
#if defined(SESSION_CAPTURE)
    logGeomApplyDequeue(queueAgeMs,
                        static_cast<uint8_t>(pendingPlayingEditGeometryType_));
#endif
    editManager.processKindBoundaryUndoWarm(track);
    const uint32_t now = millis();

    const PendingPlayingEditGeometryType kind = pendingPlayingEditGeometryType_;
    pendingPlayingEditGeometryType_ = PendingPlayingEditGeometryType::None;
    pendingPlayingEditGeometryQueuedAtMs_ = 0;

    bool applied = false;
    MidiMapping::FaderType driverFader = MidiMapping::FaderType::FADER_COARSE;
    switch (kind) {
        case PendingPlayingEditGeometryType::Move:
            applied = editManager.moveNoteToPosition(track, pendingPlayingEditGeometryNote_,
                                                     pendingPlayingEditGeometryTargetTick_);
            driverFader = MidiMapping::FaderType::FADER_COARSE;
            break;
        case PendingPlayingEditGeometryType::Length:
            applied = editManager.changeNoteEndWithOverlapHandling(
                track, pendingPlayingEditGeometryNote_, pendingPlayingEditGeometryTargetTick_);
            driverFader = MidiMapping::FaderType::FADER_COARSE;
            break;
        case PendingPlayingEditGeometryType::Pitch:
            applied = applyPlayingEditPitchGeometry(track, pendingPlayingEditGeometryNote_,
                                                    pendingPlayingEditGeometryPitchCurrent_,
                                                    pendingPlayingEditGeometryPitchNew_, true);
            driverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
            break;
        case PendingPlayingEditGeometryType::None:
            break;
    }
    if (applied) {
        finishGeometryDriverSideEffects(track, now, driverFader);
    }
#if defined(SESSION_CAPTURE)
    logGeomApplyDone(applied, editManager.sessionPreviewRevision());
#endif
}
