# Fader State System Documentation

## Overview

The Fader State System is a unified state machine that manages all hardware fader interactions in the MIDI looper. It prevents race conditions between multiple faders while ensuring smooth, predictable behavior through a "driver fader" concept and intelligent scheduling.

## System Architecture

### Core Components

#### FaderState Structure
```cpp
struct FaderState {
    FaderType type;                  // FADER_SELECT, FADER_COARSE, FADER_FINE, FADER_NOTE_VALUE
    uint8_t channel;                 // MIDI channel (16 for SELECT, 15 for others)
    bool isInitialized;              // Whether fader has received first input
    int16_t lastPitchbendValue;      // Last received pitchbend value
    uint8_t lastCCValue;             // Last received CC value
    uint32_t lastUpdateTime;         // When fader last received input
    uint32_t lastSentTime;           // When we last sent updates to this fader
    bool pendingUpdate;              // Whether an update is scheduled
    uint32_t updateScheduledTime;    // When the scheduled update should execute
    FaderType scheduledByDriver;     // Which fader scheduled this update
    int16_t lastSentPitchbend;       // Last value sent to prevent redundant updates
    uint8_t lastSentCC;              // Last CC value sent
};
```

#### Fader Types and Mappings

| Fader | Type | Channel | Control | Purpose |
|-------|------|---------|---------|---------|
| 1 | `FADER_SELECT` | 16 | Pitchbend | Note selection/navigation |
| 2 | `FADER_COARSE` | 15 | Pitchbend | 16th-step positioning |
| 3 | `FADER_FINE` | 15 | CC2 | Tick-level fine positioning |
| 4 | `FADER_NOTE_VALUE` | 15 | CC3 | Note value/velocity editing |

### Driver Fader Concept

#### Key Principle
**Only one fader can be the "driver" at any given time.** The driver fader:
- Processes input immediately without delay
- Controls the current edit operation
- Schedules other faders to update after a delay
- Never updates itself during its own operation

#### Driver Selection
```cpp
FaderType currentDriverFader = FADER_SELECT;  // Current driver
uint32_t lastDriverFaderTime = 0;            // When driver was last active
```

When any fader receives input:
1. It automatically becomes the new driver fader
2. Commits any active note movement from the previous driver
3. Processes its input immediately
4. Schedules other relevant faders for delayed updates

### Timing Constants

```cpp
static constexpr uint32_t FADER_UPDATE_DELAY = 1500;      // 1.5s delay for other faders
static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 1500;  // 1.5s to ignore feedback
static constexpr uint32_t SELECTNOTE_PROTECTION_PERIOD = 2000; // 2s protection for select fader
```

## System Flow

### 1. Input Processing (`handleFaderInput`)

```cpp
void handleFaderInput(FaderType faderType, int16_t pitchbendValue, uint8_t ccValue)
```

**Steps:**
1. **Feedback Check**: Skip if input should be ignored (feedback prevention)
2. **Initialization**: Initialize fader state on first input
3. **Deadband Filtering**: Ignore small changes to prevent jitter
4. **Driver Transition**: Commit previous driver's operations if switching
5. **Set New Driver**: Update `currentDriverFader` and timestamps
6. **Process Input**: Execute fader-specific logic immediately
7. **Schedule Others**: Queue other faders for delayed updates

#### Deadband Filtering
- **Pitchbend faders**: 23-unit deadband to prevent jitter
- **CC faders**: 1-unit deadband for precise control

### 2. Scheduling System (`scheduleOtherFaderUpdates`)

**Channel-Aware Scheduling:**
- **Fader 1 (SELECT)**: No scheduling needed (separate channel 16)
- **Faders 2,3,4**: Schedule updates for non-driver faders on same channel

**Conflict Prevention:**
- Cancels pending updates for faders that would conflict with new driver
- Only schedules faders that don't share the driver's channel
- Stores which fader was the driver when scheduling occurred

### 3. Update Execution (`updateFaderStates`)

**Validation Checks:**
- Fader wasn't the driver when update was scheduled
- Current driver is still the same as when update was scheduled
- No channel conflicts exist
- Driver isn't still actively moving
- SELECT driver special handling (prevents motorized feedback)

**Execution Logic:**
```cpp
if (state.type != state.scheduledByDriver && 
    currentDriverFader == state.scheduledByDriver && 
    !hasChannelConflict &&
    !driverStillActive &&
    !skipForSelectDriver) {
    sendFaderUpdate(state.type, track);
}
```

### 4. Update Transmission (`sendFaderUpdate`)

**Smart Program Change Logic:**
- Skip program changes for CC-only faders (3,4)
- Skip program changes when sharing channel with current driver
- Prevents unnecessary hardware movement

**Protection Periods:**
- CC faders: 1-second protection when they were recently the driver
- Channel 15 faders: Shared ignore periods to prevent feedback loops

## Channel Architecture

### Channel Separation
- **Channel 16**: Fader 1 (SELECT) - Isolated from others
- **Channel 15**: Faders 2,3,4 (positioning/editing) - Shared channel

### Conflict Prevention
The shared channel 15 creates potential conflicts:
- Program changes affect all faders on the channel
- Updates must be coordinated to prevent feedback
- Driver fader gets priority, others wait for delay period

### Smart Channel Handling
```cpp
bool currentDriverOnChannel15 = (currentDriverFader == FADER_COARSE || 
                                currentDriverFader == FADER_FINE || 
                                currentDriverFader == FADER_NOTE_VALUE);
bool faderOnChannel15 = (faderType == FADER_COARSE || 
                        faderType == FADER_FINE || 
                        faderType == FADER_NOTE_VALUE);
```

## Feedback Prevention

### Input Feedback Prevention
```cpp
bool shouldIgnoreFaderInput(FaderType faderType, int16_t pitchbendValue, uint8_t ccValue)
```

**Criteria for ignoring input:**
- Recent outgoing update to this fader (within `FEEDBACK_IGNORE_PERIOD`)
- Value hasn't changed significantly since last sent value
- Fader is within ignore period after being updated

### Output Feedback Prevention
- Set ignore periods after sending updates
- Skip redundant updates when values haven't changed
- Channel-wide ignore periods for shared channel faders

## Individual Fader Behaviors

### Fader 1 (SELECT)
- **Purpose**: Navigate between notes and grid positions
- **Channel**: 16 (isolated)
- **Behavior**: Immediate selection, no delayed updates to others
- **Special**: No scheduling of other faders (separate channel)

### Fader 2 (COARSE)
- **Purpose**: Position notes on 16th-note grid
- **Channel**: 15 (shared)
- **Behavior**: Updates position, syncs with fine fader
- **Protection**: Respects fine fader driver status

### Fader 3 (FINE)
- **Purpose**: Precise tick-level positioning
- **Channel**: 15 (shared) 
- **Behavior**: Fine-tunes position within 16th-note steps
- **Protection**: Maintains position when recently active

### Fader 4 (NOTE_VALUE)
- **Purpose**: Edit note velocity/properties
- **Channel**: 15 (shared)
- **Behavior**: Modifies note characteristics
- **Protection**: Single-source-of-truth for note values

## Error Handling & Edge Cases

### Missing Note Recovery
- Detects when moving note MIDI events are missing
- Searches deleted notes list for accidental removal
- Restores missing moving note before proceeding

### Driver Transition Handling
- Commits active note movements when switching drivers
- Prevents data loss during rapid fader switching
- Maintains edit state consistency

### Channel Conflict Resolution
- Prioritizes driver fader over scheduled updates
- Prevents simultaneous updates on shared channels
- Uses stored driver information for validation

### Timing Edge Cases
- Handles rapid driver changes during scheduled updates
- Prevents updates when driver is still actively moving
- Uses multiple timing windows for different validation needs

## Performance Optimizations

### Efficient State Management
- Single vector storage for all fader states
- O(1) access by fader type
- Minimal memory overhead per fader

### Smart Update Scheduling
- Only schedules necessary updates
- Cancels conflicting pending updates
- Prevents redundant MIDI traffic

### Deadband Filtering
- Reduces CPU load from minor position changes
- Prevents oscillation around target values
- Tuned per fader type for optimal response

## Integration Points

### EditManager Integration
- Tracks moving note state during fader operations
- Commits note movements on driver transitions
- Maintains selected note indices

### TrackManager Integration
- Provides MIDI event access for note manipulation
- Supplies loop length for position calculations
- Handles track-specific operations

### MidiHandler Integration
- Sends position updates via MIDI
- Handles program changes and CC messages
- Manages channel-specific communications

## System Benefits

1. **Race Condition Elimination**: Single driver ensures no conflicts
2. **Predictable Behavior**: Consistent 1.5s delay for all updates
3. **Hardware Awareness**: Channel-based conflict prevention
4. **Smooth Operation**: Deadband filtering prevents jitter
5. **Feedback Prevention**: Multiple layers of feedback protection
6. **Efficient Updates**: Smart scheduling reduces unnecessary MIDI traffic
7. **Error Recovery**: Robust handling of edge cases and missing data

The Fader State System provides a reliable, efficient foundation for hardware fader control while maintaining the responsiveness and precision required for musical performance and editing tasks. 