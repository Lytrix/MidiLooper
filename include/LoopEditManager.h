//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef LOOP_EDIT_MANAGER_H
#define LOOP_EDIT_MANAGER_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "Track.h"
#include "MidiHandler.h"
#include "StorageManager.h"
#include "TrackUndo.h"
#include "Logger.h"
#include "Globals.h"
#include "MidiConfig.h"
#include "Utils/NoteUtils.h"
#include "Utils/LoopEditDepartGeometry.h"

#include "EditSession.h"

/**
 * @class LoopEditManager
 * @brief Manages loop editing functionality including loop start and length editing.
 *
 * This class handles all loop editing operations:
 * - Loop start point editing via fader input
 * - Loop length editing via CC input
 * - Grace period management for loop start editing
 * - Undo/redo support for loop changes
 * - State saving after loop modifications
 */
class LoopEditManager {
public:
    LoopEditManager(MidiHandler& midiHandler);
    
    // Loop start point editing
    void handleLoopStartFaderInput(int16_t pitchValue, Track& track);
    void refreshLoopStartEditingActivity();
    void updateLoopEndpointAfterGracePeriod(Track& track);
    
    // Loop length editing
    void handleLoopLengthInput(uint8_t ccValue, Track& track);
    void sendCurrentLoopLengthCC(Track& track);
    void sendCurrentLoopStartPitchbend(Track& track);

    /** Cancel pending loop-start grace work and ignore motor feedback briefly. */
    void onLeaveLoopEditSession();
    /** Push loop fader positions to DROID without applying input. */
    void onEnterLoopEditSession(Track& track);
    /// Rebind loop edit session to the selected slot after focus change.
    void reopenLoopEditSession(Track& track);
    /// Flush grace/debounced save and commit loop geometry on slot/track exit.
    void commitLoopEditOnDepart(Track& track);
    /// Commit any in-flight loop start/length preview before global undo/redo.
    void flushAllPendingGeometry(Track& track);
    /// Drop unsettled preview and restore session baseline (before global redo).
    void cancelPendingGeometryPreview(Track& track);
    /// True while loop start or length preview is waiting to settle.
    bool hasPendingGeometry() const;
    /// Realign session baseline and motor feedback after global geometry undo/redo.
    void onGlobalGeometryRestored(Track& track);
    /// Align LOOP_EDIT baseline to live loop geometry (no fader I/O). Used after transport reanchor.
    void syncSessionBaselineFromLiveLoop(Track& track);

    // Track change handling
    void onTrackChanged(Track& newTrack);
    
    // Update method for grace period checking and debounced SD flush (call every frame).
    void update();
    
    bool isLoopEditMode() const;

private:
    MidiHandler& midiHandler;

    // Loop start editing grace period and state
    static constexpr uint32_t LOOP_START_GRACE_PERIOD = 1000; // ms
    uint32_t loopStartEditingTime = 0;
    bool loopStartEditingEnabled = true;
    uint32_t lastLoopStartEditingActivityTime = 0;
    
    // Helper methods
    uint32_t calculateLoopStartTick(int16_t pitchValue, Track& track);
    uint32_t calculateLoopLengthFromCC(uint8_t ccValue);
    uint8_t calculateCCFromLoopLength(uint32_t loopLength);
    
    // Movement filtering
    bool isSignificantMovement(uint32_t currentStart, uint32_t newStart);

    /// Coalesce SD writes during rapid loop start/length tweaks so the main loop
    /// (and USB host pumping) is not blocked on every fader step.
    void scheduleDebouncedLoopEditSave();

    static constexpr uint32_t LOOP_EDIT_SAVE_DEBOUNCE_MS = 400;
    static constexpr uint32_t LOOP_GEOMETRY_SETTLE_MS = 600;
    uint32_t pendingLoopEditSaveAtMs = 0;

    bool hasPendingLoopStartTick_ = false;
    uint32_t pendingLoopStartTick_ = 0;
    uint32_t loopStartSettleUntilMs_ = 0;

    uint32_t pendingLoopLengthTicks_ = 0;
    uint32_t loopLengthSettleUntilMs_ = 0;

    static constexpr uint32_t LOOP_EDIT_FEEDBACK_IGNORE_MS = 1500;
    uint32_t feedbackIgnoreUntilMs_ = 0;

    uint8_t sessionSlot_ = 255;
    uint32_t sessionBaselineLoopStart_ = 0;
    uint32_t sessionBaselineLoopLength_ = 0;

    void captureSessionBaseline(Track& track);
    void flushPendingLoopEditWork(Track& track);
    uint8_t selectedSlotForTrack(const Track& track) const;
    void applyLoopStartTick(Track& track, uint32_t startTick);
    void applyLoopStartPreview(Track& track, uint32_t startTick);
    void scheduleLoopStartSettle(uint32_t startTick);
    void commitSettledLoopStart(Track& track);
    void flushPendingLoopStartSettle(Track& track);
    void applyLoopLength(Track& track, uint32_t loopLengthTicks);
    void applyLoopLengthWithWrapping(Track& track, uint32_t loopLengthTicks);
    void applyLoopLengthPreview(Track& track, uint32_t loopLengthTicks);
    void scheduleLoopLengthSettle(uint32_t loopLengthTicks);
    void commitSettledLoopLength(Track& track);
    void flushPendingLoopLengthSettle(Track& track);
    void commitPendingLoopGeometry(Track& track);

    bool shouldIgnoreLoopFaderInput() const;
    std::vector<uint32_t> buildLoopStartFaderPositions(const Track& track) const;
};

#endif // LOOP_EDIT_MANAGER_H 