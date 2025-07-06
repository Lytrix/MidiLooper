# Loop Start Editing Documentation

## Overview

The Loop Start Editing system allows users to dynamically adjust the starting point of a loop while maintaining all existing MIDI data. This feature provides powerful flexibility for live performance and loop arrangement by allowing loops to begin at different positions without losing any recorded content.

## Key Concepts

### Loop Start Point
The **loop start point** is the tick position where the loop begins playback and display. It can be set to any position within the loop:
- **Default**: 0 (start of the loop)
- **Range**: 0 to loop length - 1
- **Resolution**: 16th-note steps AND note start positions for precise control

### Relative Display
When a loop start point is set, all visual elements are displayed **relative to the loop start point**:
- **MIDI notes**: Positioned relative to the new start point
- **Piano roll**: Events wrap around visually at the new start position
- **Playhead cursor**: Shows position relative to loop start
- **Bracket cursor**: Displays selection relative to loop start
- **Note information**: Shows timings relative to loop start

### Grace Period and Endpoint Management
The system uses a **grace period** to prevent accidental loop endpoint changes:
- **Grace period**: 1000ms after loop start changes
- **Endpoint update**: After grace period, loop endpoint is updated to maintain bar-based length
- **Bar preservation**: Loop length is maintained in complete bars relative to the new start point

## User Interface

### Fader Control
**Fader 1 (Pitchbend Channel 16)** controls loop start editing:
- **Mode**: Available only in **LOOP_EDIT** mode
- **Same logic as note selection**: Uses identical positioning algorithm
- **Positions**: Both note start positions AND 16th-note grid positions
- **Movement filtering**: 12-tick threshold to prevent jitter

### Mode Switching
The system automatically routes Fader 1 based on the current mode:
- **LOOP_EDIT mode**: Fader 1 → Loop start editing
- **NOTE_EDIT mode**: Fader 1 → Note selection
- **Seamless switching**: No configuration needed

### Visual Feedback
- **Immediate display update**: All visual elements update in real-time
- **Playhead adjustment**: Cursor shows correct position relative to new start
- **Note positioning**: All notes display at their correct relative positions
- **Bracket positioning**: Selection bracket moves with the relative coordinate system

## Technical Implementation

### Core Components

#### Track Class Extensions
```cpp
class Track {
    uint32_t loopStartTick = 0;        // Current loop start position
    
    // Getters and setters
    uint32_t getLoopStartTick() const;
    void setLoopStartTick(uint32_t tick);
    
    // Combined operations
    void setLoopStartAndEnd(uint32_t startTick, uint32_t endTick);
    uint32_t getLoopEndTick() const;
};
```

#### Undo System Integration
```cpp
class Track {
    // Loop start undo/redo history
    std::vector<uint32_t> loopStartHistory;
    std::vector<uint32_t> loopStartRedoHistory;
    
    // Undo operations
    void TrackUndo::pushLoopStartSnapshot(Track& track);
    bool TrackUndo::undoLoopStart(Track& track);
    bool TrackUndo::redoLoopStart(Track& track);
};
```

### Position Calculation System

#### Selection Algorithm
The loop start fader uses the **same positioning logic as note selection**:
1. **Collect all note start positions** (relative to loop start)
2. **Add 16th-note grid positions** for empty steps
3. **Sort all positions** for consistent navigation
4. **Map fader position** to sorted position list
5. **Apply movement filtering** to prevent jitter

#### Relative Position Conversion
```cpp
// Convert absolute position to relative
uint32_t absolutePos = notes[i].startTick;
uint32_t relativePos = (absolutePos >= loopStartTick) ? 
    (absolutePos - loopStartTick) : (absolutePos + loopLength - loopStartTick);
relativePos = relativePos % loopLength;
```

### Grace Period Management

#### Grace Period State
```cpp
class NoteEditManager {
    uint32_t loopStartEditingTime = 0;           // When editing started
    bool loopStartEditingEnabled = true;         // Whether editing is active
    uint32_t lastLoopStartEditingActivityTime = 0; // Last activity timestamp
    
    static constexpr uint32_t LOOP_START_GRACE_PERIOD = 1000; // 1 second
};
```

#### Grace Period Logic
1. **Start editing**: Grace period begins when loop start changes
2. **Activity tracking**: Updates timestamp on each change
3. **Endpoint update**: After 1000ms of inactivity, update loop endpoint
4. **Bar preservation**: Calculate loop length in bars and maintain it

#### Endpoint Update Algorithm
```cpp
void updateLoopEndpointAfterGracePeriod(Track& track) {
    // Calculate loop length in complete bars
    uint32_t loopLengthBars = (loopLength + (Config::TICKS_PER_BAR / 2)) / Config::TICKS_PER_BAR;
    if (loopLengthBars == 0) loopLengthBars = 1;
    
    // Calculate new loop end based on start + bars
    uint32_t newLoopEndTick = loopStartTick + (loopLengthBars * Config::TICKS_PER_BAR);
    uint32_t newLoopLength = loopLengthBars * Config::TICKS_PER_BAR;
    
    // Update if needed
    if (newLoopLength != loopLength) {
        track.setLoopLength(newLoopLength);
    }
}
```

### Display System Integration

#### Coordinate System Transformation
All display functions work with **relative coordinates**:

```cpp
// DrawAllNotes - adjust note positions
uint32_t adjustedStartTick = (n.startTick >= loopStartTick) ? 
    (n.startTick - loopStartTick) : (n.startTick + lengthLoop - loopStartTick);
adjustedStartTick = adjustedStartTick % lengthLoop;
```

#### Display Functions Updated
- **`drawAllNotes()`**: Notes display at relative positions
- **`drawBracket()`**: Uses relative bracket position
- **`drawNoteInfo()`**: Shows timings relative to loop start
- **`drawPianoRoll()`**: Playhead cursor adjusted for relative position

### Fader Integration

#### Input Handling
```cpp
void handleLoopStartFaderInput(int16_t pitchValue, Track& track) {
    // Only in LOOP_EDIT mode
    if (currentMainEditMode != MAIN_MODE_LOOP_EDIT) return;
    
    // Use same logic as select fader
    // Calculate target position from fader value
    // Apply movement filtering
    // Update loop start position
}
```

#### Mode-Based Routing
```cpp
void handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
    if (channel == 16) {
        if (currentMainEditMode == MAIN_MODE_LOOP_EDIT) {
            handleLoopStartFaderInput(pitchValue, track);
        } else if (currentMainEditMode == MAIN_MODE_NOTE_EDIT) {
            handleSelectFaderInput(pitchValue, track);
        }
    }
}
```

### Undo/Redo System

#### Snapshot Management
- **Automatic snapshots**: Created before loop start changes
- **Separate history**: Independent from other undo operations
- **Integration**: Works with clear track undo system

#### Undo Operations
```cpp
bool TrackUndo::undoLoopStart(Track& track) {
    if (track.loopStartHistory.empty()) return false;
    
    // Move current state to redo history
    track.loopStartRedoHistory.push_back(track.getLoopStartTick());
    
    // Restore previous state
    uint32_t previousLoopStart = track.loopStartHistory.back();
    track.loopStartHistory.pop_back();
    track.setLoopStartTick(previousLoopStart);
    
    return true;
}
```

## System Integration

### Note Editing Compatibility
The loop start editing system is **fully compatible** with existing note editing:
- **Coordinates**: All note editing works with relative positions
- **Selection**: Note selection uses relative positioning
- **Movement**: Note movement calculations use relative coordinates
- **Display**: All visual elements work with the relative coordinate system

### Fader System Integration
- **Unified routing**: Same fader hardware for different modes
- **Consistent behavior**: Same positioning algorithm for both features
- **Smooth transitions**: Seamless switching between modes
- **No conflicts**: Clear separation between LOOP_EDIT and NOTE_EDIT modes

### State Management
- **Track-specific**: Each track has its own loop start point
- **Persistent**: Loop start points are saved/loaded with tracks
- **Undo integration**: Full undo/redo support for loop start changes
- **Clear track**: Loop start is reset when tracks are cleared

## Performance Considerations

### Movement Filtering
- **12-tick threshold**: Prevents jitter from small fader movements
- **Stable positioning**: Reduces CPU load from excessive updates
- **Smooth operation**: Eliminates noise from motorized faders

### Display Optimization
- **Coordinate caching**: Relative positions calculated once per display cycle
- **Efficient updates**: Only affected display elements are redrawn
- **Minimal overhead**: Coordinate transformation is lightweight

### Grace Period Benefits
- **Reduced calculations**: Endpoint updates happen only after settling
- **Stable operation**: Prevents rapid endpoint changes during editing
- **User-friendly**: Natural feel for loop start adjustments

## Error Handling

### Position Validation
- **Range checking**: Loop start position is always within loop bounds
- **Wrap-around**: Positions that exceed loop length are wrapped correctly
- **Zero-length protection**: Prevents invalid loop configurations

### Recovery Mechanisms
- **Invalid state detection**: System detects and corrects invalid configurations
- **Graceful degradation**: Falls back to default behavior on errors
- **Logging**: Comprehensive logging for troubleshooting

## Use Cases

### Live Performance
- **Loop arrangement**: Change loop start points during performance
- **Rhythmic variation**: Create different feels from the same recorded content
- **Quick adjustments**: Rapid loop start changes for musical effect

### Studio Work
- **Loop alignment**: Align loops with different musical material
- **Arrangement**: Position loops to match song structure
- **Experimentation**: Try different loop start points for creative ideas

### Sound Design
- **Texture variation**: Create different textures from the same loop
- **Rhythmic displacement**: Shift rhythmic emphasis
- **Creative manipulation**: Use loop start changes as a musical tool

## Future Enhancements

### Potential Improvements
- **Quantization options**: Snap to bar/beat boundaries
- **Multiple loop start points**: Save/recall different start positions
- **Automation**: MIDI CC control for loop start position
- **Visual indicators**: Enhanced display of loop start position

### Integration Possibilities
- **Sync with other tracks**: Coordinated loop start changes
- **Pattern variation**: Different loop start points for different patterns
- **Performance macros**: Preset loop start configurations

This system provides a powerful and intuitive way to manipulate loop start points while maintaining full compatibility with existing functionality and ensuring smooth, stable operation. 