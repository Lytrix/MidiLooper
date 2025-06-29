//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <iostream>
#include <vector>
#include <cassert>
#include "../include/Track.h"
#include "../include/TrackUndo.h"
#include "../include/MidiEvent.h"

// Simple test to verify redo functionality
void testRedoFunctionality() {
    std::cout << "Testing redo functionality..." << std::endl;
    
    // Create a track
    Track track;
    
    // Create some test MIDI events
    std::vector<MidiEvent> events1 = {
        {midi::NoteOn, 0, 0, {60, 100}},  // Note C4, velocity 100
        {midi::NoteOff, 480, 0, {60, 0}}  // Note off after 1 beat
    };
    
    std::vector<MidiEvent> events2 = {
        {midi::NoteOn, 0, 0, {60, 100}},   // Note C4, velocity 100
        {midi::NoteOff, 480, 0, {60, 0}},  // Note off after 1 beat
        {midi::NoteOn, 240, 0, {64, 80}},  // Note E4, velocity 80
        {midi::NoteOff, 720, 0, {64, 0}}   // Note off after 1.5 beats
    };
    
    // Test 1: Basic redo functionality
    std::cout << "Test 1: Basic redo functionality" << std::endl;
    
    // Set initial state
    track.getMidiEvents() = events1;
    assert(track.getMidiEvents().size() == 2);
    assert(TrackUndo::getUndoCount(track) == 0);
    assert(TrackUndo::getRedoCount(track) == 0);
    
    // Push undo snapshot
    TrackUndo::pushUndoSnapshot(track);
    assert(TrackUndo::getUndoCount(track) == 1);
    assert(TrackUndo::getRedoCount(track) == 0);
    
    // Modify track (simulate overdub)
    track.getMidiEvents() = events2;
    assert(track.getMidiEvents().size() == 4);
    
    // Undo the change
    TrackUndo::undoOverdub(track);
    assert(track.getMidiEvents().size() == 2);
    assert(TrackUndo::getUndoCount(track) == 0);
    assert(TrackUndo::getRedoCount(track) == 1);
    
    // Redo the change
    TrackUndo::redoOverdub(track);
    assert(track.getMidiEvents().size() == 4);
    assert(TrackUndo::getUndoCount(track) == 1);
    assert(TrackUndo::getRedoCount(track) == 0);
    
    std::cout << "✓ Test 1 passed" << std::endl;
    
    // Test 2: Redo availability
    std::cout << "Test 2: Redo availability" << std::endl;
    
    assert(TrackUndo::canUndo(track) == true);
    assert(TrackUndo::canRedo(track) == false);
    
    // Undo again
    TrackUndo::undoOverdub(track);
    assert(TrackUndo::canUndo(track) == false);
    assert(TrackUndo::canRedo(track) == true);
    
    std::cout << "✓ Test 2 passed" << std::endl;
    
    // Test 3: Clear redo functionality
    std::cout << "Test 3: Clear redo functionality" << std::endl;
    
    // Set up clear undo
    track.getMidiEvents() = events1;
    TrackUndo::pushClearTrackSnapshot(track);
    track.clear();
    assert(track.getMidiEvents().size() == 0);
    
    // Undo clear
    TrackUndo::undoClearTrack(track);
    assert(track.getMidiEvents().size() == 2);
    assert(TrackUndo::canRedoClearTrack(track) == true);
    
    // Redo clear
    TrackUndo::redoClearTrack(track);
    assert(track.getMidiEvents().size() == 0);
    assert(TrackUndo::canRedoClearTrack(track) == false);
    
    std::cout << "✓ Test 3 passed" << std::endl;
    
    // Test 4: Redo history clearing
    std::cout << "Test 4: Redo history clearing" << std::endl;
    
    // Set up undo/redo state
    track.getMidiEvents() = events1;
    TrackUndo::pushUndoSnapshot(track);
    track.getMidiEvents() = events2;
    TrackUndo::undoOverdub(track);
    assert(TrackUndo::getRedoCount(track) == 1);
    
    // Push new undo (should clear redo history)
    TrackUndo::pushUndoSnapshot(track);
    assert(TrackUndo::getRedoCount(track) == 0);
    
    std::cout << "✓ Test 4 passed" << std::endl;
    
    std::cout << "All redo functionality tests passed!" << std::endl;
}

int main() {
    testRedoFunctionality();
    return 0;
} 