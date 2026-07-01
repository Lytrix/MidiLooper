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
#include "Utils/NoteEditFaderOutboundPlan.h"

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
    static std::vector<SelectNavigation::SelectNavSlot> buildSelectNavigationSlots(
        const Track& track, uint32_t bracketTick, bool includeBracketIfMissing = true);
    /// NOTE_EDIT UI list: filtered when **NoteEditSession** active, else **getCachedNotes()** copy.
    static std::vector<NoteUtils::DisplayNote> selectableDisplayNotesForEditUi(const Track& track);

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
    
    void sendStartNotePitchbend(Track& track);
    /** NOTE_EDIT session entry: grace period + deferred selectnote fader sync. */
    void sendNoteEditSessionFaderFeedback(Track& track);
    /** GPIO / bar-step note select: fader1 bracket + dependent refresh. */
    void scheduleNoteSelectFaderSync(Track& track);
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
    uint32_t noteSelectionTime = 0;
    bool startEditingEnabled = true;
    uint32_t lastEditingActivityTime = 0;
    
    int16_t lastUserSelectFaderValue = MidiConfig::Pitchbend::CENTER;
    uint32_t lastSelectFaderTime = 0;
    static constexpr int16_t SELECT_MOVEMENT_THRESHOLD = 100;
    
    int16_t lastUserCoarseFaderValue = MidiConfig::Pitchbend::CENTER;
    uint32_t lastCoarseFaderTime = 0;
    static constexpr int16_t COARSE_MOVEMENT_THRESHOLD = 150;
    static constexpr uint32_t COARSE_STABILITY_TIME = 1000;
    
    uint8_t lastFineCCValue = 64;
    bool fineCCInitialized = false;
    uint32_t referenceStep = 0;

    uint32_t lastPitchbendSentTime = 0;
    uint32_t lastSelectnoteSentTime = 0;
    static constexpr uint32_t PITCHBEND_IGNORE_PERIOD = 1500;
    static constexpr uint32_t SELECTNOTE_UPDATE_DELAY = 1600;
    uint32_t selectFaderFeedbackIgnoreUntilMs_ = 0;

    NoteEditFaderOutbound::SelectPhase faderSelectPhase_ =
        NoteEditFaderOutbound::SelectPhase::Idle;
    uint32_t fader1LastUserInputMs_ = 0;
    int lastAppliedSelectSlotIndex_ = -1;

    NoteEditFaderOutbound::Trigger activeOutboundTrigger_ =
        NoteEditFaderOutbound::Trigger::None;
    NoteEditFaderOutbound::Trigger pendingOutboundTrigger_ =
        NoteEditFaderOutbound::Trigger::None;
    NoteEditFaderOutbound::Step outboundStep_ = NoteEditFaderOutbound::Step::Idle;
    NoteEditFaderOutbound::PlanFlags outboundPlan_{};
    uint32_t outboundStepStartedMs_ = 0;
    int16_t outboundSentFader1Pitchbend_ = 0;
    uint32_t lastGeometryFader1BracketSentMs_ = 0;

    static constexpr uint32_t kFeedbackAnchorRelTickUnset = UINT32_MAX;
    static constexpr int8_t kFeedbackNotePitchUnset = -1;
    uint32_t lastFeedbackAnchorRelTick_ = kFeedbackAnchorRelTickUnset;
    int8_t lastFeedbackNotePitch_ = kFeedbackNotePitchUnset;

    static constexpr uint32_t F2_OUTBOUND_SELECT_IGNORE_TAIL_MS = 400;
    static constexpr uint32_t GEOMETRY_F1_BRACKET_MIN_GAP_MS = 150;

    bool isGeometryDriverActive(uint32_t now) const;
    void armSelectFaderFeedbackIgnore(uint32_t sentAt, uint32_t durationMs);
    void resetFeedbackGeometrySnapshot();
    void stampFeedbackPositionFromSelection(Track& track);
    void stampFeedbackPitchFromSelection(Track& track);
    void stampFeedbackGeometrySnapshotAtDone(Track& track,
                                           const NoteEditFaderOutbound::PlanFlags& plan);
    void evaluateDependentFaderRefreshDirty(Track& track, bool& needsPositionRefresh,
                                          bool& needsPitchRefresh);
    bool requestDependentFaderRefreshFromSelection(Track& track);
    void requestFaderOutbound(NoteEditFaderOutbound::Trigger trigger,
                              const NoteEditFaderOutbound::PlanFlags* planOverride = nullptr);
    void cancelActiveFaderOutbound();
    void processFaderOutbound();
    void completeOutboundPipelineAtDone(Track& track, uint32_t now);
    void processFaderSelectQuiet();
    void sendFader1BracketFeedback(Track& track, bool updateNavStateFromOutbound = true);
    void logOutboundStep(const char* label);
    void logSelectSlot(int slotIndex, int16_t pitchValue, bool ignored);
    void logSelectApplyDecision(uint32_t targetBracketTick, int targetNoteIdx, int slotIndex,
                                bool apply, const char* reason);
    void sendSelectnoteFaderUpdate(Track& track);
    void performSelectnoteFaderUpdate(Track& track);
    void armNoteEditDroidMotorBank();
    int selectNavSlotIndexForPitchbend(Track& track, int16_t pitchValue);
    bool applyNoteSelectFromFader1Pitchbend(Track& track, int16_t pitchValue,
                                            int16_t priorPitchValue, uint32_t now,
                                            bool enforceStaleEchoLockout,
                                            bool sendDependentFeedback);
    struct Fader1SelectTarget {
        uint32_t absoluteTargetTick = 0;
        int noteIdx = -1;
        int slotIndex = -1;
        bool valid = false;
    };
    Fader1SelectTarget resolveFader1SelectTarget(Track& track, int16_t pitchValue);
    void enableStartEditing();
    void armChannel15FaderFeedbackIgnore(uint32_t sentAt);
    void armCoarseFaderFeedbackIgnore(uint32_t sentAt);
    void armChannel15CcFaderFeedbackIgnore(uint32_t sentAt);
    
    static constexpr uint32_t FADER2_PROTECTION_PERIOD = 2000;
    
    MidiFaderProcessor* faderProcessor = nullptr;
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
    void handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue = 0, uint8_t ccValue = 0);
    void scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader);
    void sendFaderUpdate(MidiMapping::FaderType faderType, Track& track);
    void sendFaderPosition(MidiMapping::FaderType faderType, Track& track);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    
    bool sendCoarseFaderPosition(Track& track);
    bool sendFineFaderPosition(Track& track);
    void sendCoarseFaderMotorTrigger();
    void sendFineFaderMotorTrigger();
    bool sendNoteValueFaderPosition(Track& track);
    void sendNoteValueFaderMotorTrigger();

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
