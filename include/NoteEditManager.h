//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef NOTE_EDIT_MANAGER_H
#define NOTE_EDIT_MANAGER_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "EditManager.h"
#include "Utils/MidiMapping.h"
#include "MidiButtonManager.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"
#include "Track.h"
#include "Logger.h"
#include "LoopEditManager.h"
#include "MidiConfig.h"
#include "Utils/SelectNavigation.h"
#include "Utils/NoteEditFaderMotorTiming.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditFaderSelectSync.h"
#include "Utils/NoteEditDependentFaderSnapshot.h"
#include "NoteEditSessionState.h"

class DisplayManager;

/**
 * @class NoteEditManager
 * @brief Manages MIDI note-based button logic and fader control.
 *
 * This class serves as the main interface for MIDI control, delegating to specialized handlers:
 * - MidiButtonHandler for button press/release logic
 * - MidiFaderHandler for fader control
 */
class NoteEditManager {
public:
    NoteEditManager();
    
    // Specialized handlers
    MidiButtonManager buttonHandler;
    MidiFaderManager faderHandler;
    
    //void setup();
    void update();
    
    // MIDI input handlers
    void handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn);
    void handleMidiPitchbend(uint8_t channel, int16_t pitchValue);
    void handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value);
    
    // Fader handler methods (must be public for MidiFaderActions)
    void handleSelectFaderInput(int16_t pitchValue, Track& track);
    void handleCoarseFaderInput(int16_t pitchValue, Track& track);
    void handleFineFaderInput(uint8_t ccValue, Track& track);
    void handleNoteValueFaderInput(uint8_t ccValue, Track& track);

    /** One nav slot per note (multiple per 16th when notes share a step) or empty grid step. */
    std::vector<SelectNavigation::SelectNavSlot> buildSelectNavigationSlots(
        const Track& track, uint32_t bracketTick, bool includeBracketIfMissing = true) const;
    /// NOTE_EDIT UI list: filtered (+ window when long loop) when session active, else cached copy.
    std::vector<NoteUtils::DisplayNote> selectableDisplayNotesForEditUi(const Track& track) const;

    // Loop editing is now handled by LoopEditManager
    LoopEditManager loopEditManager;

    // Edit mode methods (must be public for MidiButtonActions)
    void cycleEditMode(Track& track);
    void processEncoderMovement(int rawDelta);
    void deleteSelectedNote(Track& track);
    void toggleLengthEditingMode();
    /** Force position-edit routing when opening or closing a note-edit session. */
    void resetLengthEditingModeOnSessionBoundary();
    /** Return fader 2/3 to position edit after fader-1 note select (leaves length mode). */
    void resetLengthEditingModeOnNoteSelect();
    
    /** Arm SessionOpen outbound; suppress duplicate SELECT_SYNC until pipeline Done. */
    void prepareNoteEditSessionOpen();
    /** NOTE_EDIT session entry: grace period + deferred selectnote fader sync. */
    void sendNoteEditSessionFaderFeedback(Track& track);
    void syncReferenceStepFromBracketTick(uint32_t bracketTick);
    /** GPIO / bar-step note select: fader1 bracket + dependent refresh. */
    void scheduleNoteSelectFaderSync(Track& track);
    /** Queue F2/F3/F4 motor sync after F1 idle, or F1 bracket sync after geometry fader idle. */
    void scheduleSelectDependentMotorSync(Track& track, const EditorSelection& priorSelection,
                                          const EditorSelection& nextSelection,
                                          bool geometryIsDriver = false);
    bool isFaderOutboundActive() const;
    void moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick);
    void changeNoteEndWithOverlapHandling(Track& track, const NoteUtils::DisplayNote& currentNote,
                                          std::uint32_t targetEndTick);
    void refreshEditingActivity();

    // Main edit session switching (for mode button functionality)
    void cycleEditSession(Track& track);
    void onTrackChanged(Track& newTrack);

private:
    static int16_t loopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength);

    // MIDI constants (from MidiConfig)
    static constexpr uint8_t PITCHBEND_SELECT_CHANNEL = MidiConfig::Fader::SELECT_MOTOR_CHANNEL;
    static constexpr uint8_t PITCHBEND_START_CHANNEL = MidiConfig::Fader::COARSE_MOTOR_CHANNEL;
    static constexpr uint8_t PROGRAM_CHANGE_CHANNEL = MidiConfig::PROGRAM_CHANGE_CHANNEL;
    static constexpr uint8_t FINE_CC_CHANNEL = MidiConfig::Fader::FINE_CHANNEL;
    static constexpr uint8_t FINE_CC_NUMBER = MidiConfig::Fader::FINE_CC;
    static constexpr uint8_t NOTE_VALUE_CC_CHANNEL = MidiConfig::Fader::NOTE_VALUE_CHANNEL;
    static constexpr uint8_t NOTE_VALUE_CC_NUMBER = MidiConfig::Fader::NOTE_VALUE_CC;

    static constexpr uint32_t NOTE_SELECTION_GRACE_PERIOD = 750;
    static constexpr uint32_t SELECT_DEPENDENT_SETTLE_MS = 450;
    static constexpr uint32_t kSelectFaderMotorIdleMs =
        NoteEditFaderMotorTiming::kSelectFaderMotorIdleMs;
    struct Fader1SelectTarget {
        uint32_t absoluteTargetTick = 0;
        int noteIdx = -1;
        int slotIndex = -1;
        bool valid = false;
    };
    uint32_t noteSelectionTime = 0;
    uint32_t selectDependentSettleUntilMs_ = 0;
    bool startEditingEnabled = true;
    uint32_t lastEditingActivityTime = 0;
    
    int16_t lastUserSelectFaderValue = MidiConfig::Pitchbend::CENTER;
    uint32_t lastSelectFaderTime = 0;
    uint32_t lastMotorSyncDriverInputMs_ = 0;
    uint32_t lastSelectMotorSyncMs_ = 0;
    int16_t lastMotorSyncF1Pitch_ = MidiConfig::Pitchbend::CENTER;
    bool selectDependentSettleBlockLogged_ = false;
    bool suppressSelectDependentMotorSync_ = false;
    bool pendingSelectDriverMotorSyncValid_ = false;
    bool pendingGeometryDriverMotorSyncValid_ = false;
    Fader1SelectTarget pendingSelectMotorTarget_{};
    NoteEditFaderOutbound::PlanFlags pendingSelectMotorPlan_{};
    EditorSelection pendingSelectMotorPriorSelection_{};
    static constexpr int16_t SELECT_MOVEMENT_THRESHOLD = 100;
    
    int16_t lastUserCoarseFaderValue = MidiConfig::Pitchbend::CENTER;
    uint32_t lastCoarseFaderTime = 0;
    static constexpr int16_t COARSE_MOVEMENT_THRESHOLD = 150;
    static constexpr uint32_t COARSE_STABILITY_TIME = 1000;
    static constexpr uint32_t DRIVER_FADER_ACTIVE_MS = 2500;
    
    uint8_t lastFineCCValue = 64;
    bool fineCCInitialized = false;
    uint32_t referenceStep = 0;

    uint32_t lastPitchbendSentTime = 0;
    uint32_t lastSelectnoteSentTime = 0;
    static constexpr uint32_t PITCHBEND_IGNORE_PERIOD = 1500;
    static constexpr uint32_t SELECTNOTE_UPDATE_DELAY = 1600;
    uint32_t selectFaderFeedbackIgnoreUntilMs_ = 0;

    NoteEditFaderOutbound::Trigger activeOutboundTrigger_ =
        NoteEditFaderOutbound::Trigger::None;
    NoteEditFaderOutbound::Trigger pendingOutboundTrigger_ =
        NoteEditFaderOutbound::Trigger::None;
    NoteEditFaderOutbound::PlanFlags pendingOutboundPlan_{};
    bool pendingOutboundPlanValid_ = false;
    NoteEditFaderOutbound::Step outboundStep_ = NoteEditFaderOutbound::Step::Idle;
    NoteEditFaderOutbound::PlanFlags outboundPlan_{};
    uint32_t outboundStepStartedMs_ = 0;
    int16_t outboundSentFader1Pitchbend_ = 0;
    bool isGeometryDriverActive(uint32_t now) const;
    void armSelectFaderFeedbackIgnore(uint32_t sentAt, uint32_t durationMs);
    void requestFaderOutbound(NoteEditFaderOutbound::Trigger trigger,
                              const NoteEditFaderOutbound::PlanFlags* planOverride = nullptr);
    void queuePendingOutbound(NoteEditFaderOutbound::Trigger trigger,
                              const NoteEditFaderOutbound::PlanFlags* planOverride = nullptr);
    void cancelActiveFaderOutbound();
    void processFaderOutbound();
    void completeOutboundPipelineAtDone(Track& track, uint32_t now);
    void sendFader1BracketFeedback(Track& track, bool updateNavStateFromOutbound = true);
    bool sendFader1MotorTimedBurst(Track& track);
    void logOutboundStep(const char* label);
    void logSelectSlot(int slotIndex, int16_t pitchValue, bool ignored, const char* reason = nullptr);
    void logSelectApplyDecision(uint32_t targetBracketTick, int targetNoteIdx, int slotIndex,
                                int priorSlotIndex, bool apply, const char* reason);
    void logSelectMotorSyncDecision(int noteIdx, int priorNoteIdx, bool sent, const char* reason,
                                    int16_t f1Pitch, int16_t priorMotorSyncF1Pitch,
                                    uint32_t sinceSyncMs, int16_t f2Pb, int f4Cc,
                                    int16_t priorF2Pb, int priorF4Cc, bool motorValueChanged);
    void resetSelectNavSlotApplyState();
    int selectNavSlotIndexForPitchbend(Track& track, int16_t pitchValue);
    bool applyNoteSelectFromFader1Pitchbend(Track& track, int16_t pitchValue, int posIndex);
    Fader1SelectTarget resolveFader1SelectTarget(Track& track, int16_t pitchValue);
    NoteEditDependentFaderBuildInput makeDependentFaderBuildInput(
        const Track& track, const Fader1SelectTarget* selectTarget) const;
    NoteEditDependentFaderSnapshot buildDependentFaderSnapshotForTrack(
        const Track& track, const Fader1SelectTarget* selectTarget) const;
    bool sendDependentFaderSnapshot(Track& track, const NoteEditFaderOutbound::PlanFlags& plan,
                                    const NoteEditDependentFaderSnapshot& snapshot,
                                    DependentFaderSendMode mode);
    bool shouldIgnoreDependentFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue,
                                         uint8_t ccValue, Track& track);
    void publishDependentFaderLatch(Track& track);
    void clearPendingSelectDependentMotorSync();
    void clearPendingGeometryDriverMotorSync();
    void processPendingSelectDependentMotorSync(Track& track);
    void processPendingGeometryDriverMotorSync(Track& track);
    void syncSelectionFromGeometryEdit(Track& track);
    void syncMotorsFromSelectTarget(Track& track, const Fader1SelectTarget& target,
                                    const NoteEditFaderOutbound::PlanFlags& plan);
    bool sendDependentFadersParallelTimedBurst(Track& track,
                                               const NoteEditFaderOutbound::PlanFlags& plan,
                                               const Fader1SelectTarget* selectTarget);
    bool sendCoarseFaderPosition(Track& track);
    bool sendFineFaderPosition(Track& track);
    bool sendNoteValueFaderPosition(Track& track);
    void armSelectDependentSettle(uint32_t sentAt);
    void recordFaderInputForValidation(MidiMapping::FaderType faderType, int16_t pitchbendValue,
                                       uint8_t ccValue);
    void enableStartEditing();
    void armChannel15FaderFeedbackIgnore(uint32_t sentAt);
    void armCoarseFaderFeedbackIgnore(uint32_t sentAt);
    void armChannel15CcFaderFeedbackIgnore(uint32_t sentAt);
    
    static constexpr uint32_t FADER2_PROTECTION_PERIOD = 2000;
    
    MidiFaderProcessor* faderProcessor = nullptr;
    DisplayManager* displayManager_ = nullptr;
    uint32_t lastDriverFaderUpdateTime = 0;
    MidiMapping::FaderType currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    uint32_t lastDriverFaderTime = 0;
    static constexpr uint32_t FADER_UPDATE_DELAY = 1500;
    static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 1500;
    
    bool lengthEditingMode = false;
    uint32_t lengthFineAnchorEndTick = 0;
    uint32_t lastLengthModeToggleTime = 0;
    static constexpr uint32_t LENGTH_MODE_DEBOUNCE_TIME = 100;

public:
    void setFaderProcessor(MidiFaderProcessor* processor) { faderProcessor = processor; }
    void setDisplayManager(DisplayManager* manager) { displayManager_ = manager; }
    void handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue = 0, uint8_t ccValue = 0);
    void scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    void sendCoarseFaderMotorNoteOn();
    void sendCoarseFaderMotorNoteOff();
    void sendFineFaderMotorNoteOn();
    void sendFineFaderMotorNoteOff();
    void sendNoteValueFaderMotorNoteOn();
    void sendNoteValueFaderMotorNoteOff();

    enum EditModeState {
        EDIT_MODE_NONE = 0,
        EDIT_MODE_SELECT = 1,
        EDIT_MODE_START = 2,
        EDIT_MODE_LENGTH = 3,
        EDIT_MODE_PITCH = 4
    };
    EditModeState currentEditMode = EDIT_MODE_NONE;

};

extern NoteEditManager noteEditManager;

#endif // NOTE_EDIT_MANAGER_H
