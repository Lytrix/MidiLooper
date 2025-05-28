#pragma once
#include "EditState.h"
#include "NoteUtils.h"
#include <vector>
#include <utility>

class EditStartNoteState : public EditState {
public:
    void onEnter(EditManager& manager, Track& track, uint32_t startTick) override;
    void onExit(EditManager& manager, Track& track) override;
    void onEncoderTurn(EditManager& manager, Track& track, int delta) override;
    void onButtonPress(EditManager& manager, Track& track) override;
    const char* getName() const override { return "EditStartNote"; }

    /**
     * @brief Get the initial MIDI-event hash before editing started
     */
    uint32_t getInitialHash() const { return initialHash; }

private:
    using DisplayNote = NoteUtils::DisplayNote;
    /**
     * @brief Identify overlapping notes and decide which to shorten or delete.
     * @param currentNotes Reconstructed display notes list
     * @param movingPitch Pitch of the moving note
     * @param currentStart Original start tick of the moving note
     * @param newStart New start tick after movement
     * @param newEnd Raw new end tick after movement
     * @param delta Encoder delta (direction)
     * @param loopLength Loop length in ticks
     * @param notesToShorten Out list of notes to shorten (with new end tick)
     * @param notesToDelete Out list of notes to delete entirely
     */
    static void findOverlaps(const std::vector<DisplayNote>& currentNotes,
                              uint8_t movingPitch,
                              uint32_t currentStart,
                              uint32_t newStart,
                              uint32_t newEnd,
                              int delta,
                              uint32_t loopLength,
                              std::vector<std::pair<DisplayNote, uint32_t>>& notesToShorten,
                              std::vector<DisplayNote>& notesToDelete);

    /**
     * @brief Apply shortening or deletion to MIDI events based on overlap decisions.
     * @param midiEvents List of MIDI events to modify.
     * @param notesToShorten Notes to shorten (pair of DisplayNote, new end tick).
     * @param notesToDelete Notes to delete entirely.
     * @param manager EditManager for recording deleted originals in undo list.
     * @param loopLength Loop length in ticks.
     */
    static void applyShortenOrDelete(std::vector<MidiEvent>& midiEvents,
                                     const std::vector<std::pair<DisplayNote, uint32_t>>& notesToShorten,
                                     const std::vector<DisplayNote>& notesToDelete,
                                     EditManager& manager,
                                     uint32_t loopLength);

    uint32_t initialHash = 0; // hash of midiEvents at onEnter for undo commit-on-exit
}; 