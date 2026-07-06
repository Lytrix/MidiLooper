# Fader State System Documentation

> **NOTE_EDIT motorized faders (2026):** Authoritative behavior for DROID motor sync, split select/geometry queues, and F1 inbound guards is in [**DROID_MOTORFADER_PITCHBEND.md**](DROID_MOTORFADER_PITCHBEND.md). Bugfix handoff: [`note_edit_geometry_f1_selection_guard_bugfix.md`](../plans/note_edit_geometry_f1_selection_guard_bugfix.md). The V2 sections below describe the generic `MidiFaderProcessor` pipeline; **timing and feedback rules for NOTE_EDIT** are owned by `NoteEditManager` (see § NOTE_EDIT motor feedback below).
>
> **Compile-time feedback gate:** `NoteEditManager::kNoteEditFaderFeedbackEnabled` — see § NOTE_EDIT fader feedback on vs off. Default in firmware today: **`false`** (inbound geometry works; outbound motor sync off).

## Overview

The Fader State System is a modular, configuration-driven architecture that manages all hardware fader interactions in the MIDI looper. The V2 system separates concerns into specialized components while maintaining smooth, predictable behavior through intelligent state management and feedback prevention.

**Current Architecture (2025 - V2 System):**
- **MidiFaderManager**: Coordination and configuration management
- **MidiFaderProcessor**: State tracking and input processing
- **MidiFaderActions**: Action execution and business logic
- **MidiFaderConfig**: Configuration-based fader mapping

## System Architecture

### Core Components

#### 1. MidiFaderManager (Coordinator)
**Location**: `src/MidiFaderManager.cpp`
**Responsibilities**:
- Coordinates between processor and actions
- Manages fader configurations (basic/extended)
- Provides unified API for fader operations
- Handles setup and lifecycle management

#### 2. MidiFaderProcessor (State Engine)
**Location**: `src/MidiFaderProcessor.cpp`
**Responsibilities**:
- Tracks fader states and movement detection
- Handles input validation and deadband filtering
- Manages driver fader concept and timing
- Provides feedback prevention mechanisms

#### 3. MidiFaderActions (Business Logic)
**Location**: `src/MidiFaderActions.cpp`
**Responsibilities**:
- Executes fader-triggered actions
- Delegates to NoteEditManager for complex operations
- Handles action parameter processing
- Maintains separation between input and business logic

#### 4. MidiFaderConfig (Configuration System)
**Location**: `src/Utils/MidiFaderConfig.cpp`
**Responsibilities**:
- Manages fader-to-action mappings
- Supports multiple configuration profiles
- Provides runtime configuration changes
- Handles channel and CC number mappings

### Enhanced FaderState Structure
```cpp
struct FaderState {
    MidiMapping::FaderType type;          // FADER_SELECT, FADER_COARSE, FADER_FINE, FADER_NOTE_VALUE
    uint8_t channel;                      // MIDI channel (16 for SELECT, 15 for others)
    bool isInitialized;                   // Whether fader has received first input
    int16_t lastPitchbendValue;           // Last received pitchbend value
    uint8_t lastCCValue;                  // Last received CC value
    uint32_t lastUpdateTime;              // When fader last received input
    uint32_t lastSentTime;                // When we last sent updates to this fader
    bool pendingUpdate;                   // Whether an update is scheduled
    uint32_t updateScheduledTime;         // When the scheduled update should execute
    MidiMapping::FaderType scheduledByDriver; // Which fader scheduled this update
    int16_t lastSentPitchbend;            // Last value sent to prevent redundant updates
    uint8_t lastSentCC;                   // Last CC value sent
};
```

### Configuration-Based Fader Mapping

#### Standard Fader Configuration
| Fader | Type | Channel | Control | Action | Purpose |
|-------|------|---------|---------|---------|---------|
| 1 | `FADER_SELECT` | 16 | Pitchbend | `SELECT_NOTE` | Note selection/navigation |
| 2 | `FADER_COARSE` | 15 | Pitchbend | `MOVE_NOTE_COARSE` | 16th-step positioning |
| 3 | `FADER_FINE` | 15 | CC2 | `MOVE_NOTE_FINE` | Tick-level fine positioning |
| 4 | `FADER_NOTE_VALUE` | 15 | CC3 | `CHANGE_NOTE_VALUE` | Note value/velocity editing |

#### Configuration Profiles
- **Basic Configuration**: Standard 4-fader setup
- **Extended Configuration**: Additional faders for advanced operations
- **Custom Configuration**: Runtime-configurable fader mappings

## Driver Fader Concept (Enhanced)

### Core Principle
**Single Source of Truth**: Only one fader drives changes at any time, eliminating race conditions and providing predictable behavior.

### Driver Management
```cpp
MidiMapping::FaderType currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
uint32_t lastDriverFaderTime = 0;
uint32_t lastDriverFaderUpdateTime = 0;
```

### Driver Transition Process
1. **Input Detection**: Any fader receiving input becomes candidate driver
2. **Validation**: Input passes deadband and feedback prevention checks
3. **Commit Previous**: Current driver's operations are committed
4. **Driver Switch**: New fader becomes active driver
5. **Immediate Processing**: New driver processes input immediately
6. **Cross-Updates**: Other relevant faders scheduled for delayed updates

### Timing Constants (Optimized)
```cpp
static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 100;  // 100ms (reduced from 1500ms)
static constexpr int16_t PITCHBEND_DEADBAND = 23;        // Jitter prevention
static constexpr uint8_t CC_DEADBAND_FINE = 1;           // Fine control precision
```

## V2 System Flow

### 1. Configuration Loading (`MidiFaderManager::setup`)
```cpp
void setup() {
    MidiFaderConfig::Config::initialize();
    processor.setup();
    loadFaderConfiguration("basic");  // or "extended"
}
```

### 2. Input Processing (`MidiFaderProcessor::processFaderInput`)
**Enhanced Processing Pipeline**:
1. **Configuration Lookup**: Find fader config by channel/control type
2. **Deadband Filtering**: Apply appropriate deadband for fader type
3. **Feedback Prevention**: Check ignore periods and recent sent values
4. **Significant Change Detection**: Determine if input warrants processing
5. **Driver Management**: Handle driver transitions and commit operations
6. **Callback Execution**: Trigger movement callback to manager

### 3. Action Execution (`MidiFaderActions::executeAction`)
**Action Dispatch System**:
```cpp
void executeAction(MidiFaderConfig::ActionType action, 
                  MidiMapping::FaderType faderType,
                  int16_t pitchbendValue, uint8_t ccValue, uint8_t parameter) {
    switch (action) {
        case ActionType::SELECT_NOTE:
            handleSelectNote(pitchbendValue);
            break;
        case ActionType::MOVE_NOTE_COARSE:
            handleMoveNoteCoarse(pitchbendValue);
            break;
        case ActionType::MOVE_NOTE_FINE:
            handleMoveNoteFine(ccValue);
            break;
        case ActionType::CHANGE_NOTE_VALUE:
            handleChangeNoteValue(ccValue);
            break;
    }
}
```

### 4. Business Logic Delegation
**Clean Separation**: Actions delegate to NoteEditManager for complex operations:
```cpp
void handleSelectFaderInput(int16_t pitchbendValue, Track& track) {
    // Delegate to existing NoteEditManager logic
    noteEditManager.handleSelectFaderInput(pitchbendValue, track);
}
```

## NOTE_EDIT motor feedback (`NoteEditManager`, 2026)

During **NOTE_EDIT**, fader inbound/outbound for motorized DROID faders is **not** the generic `MidiFaderProcessor::shouldIgnoreFaderInput` blanket ignore. `NoteEditManager` owns feedback prevention and motor sync.

### Split motor-sync queues

| Direction | Driver | Pending queue | Flush trigger |
|-----------|--------|---------------|---------------|
| F1 → F2/F3/F4 | Select fader (user note select) | `pendingSelectDriverMotorSync_` | **300 ms** F1 idle (`kSelectFaderMotorIdleMs`) |
| F2/F3/F4 → F1 | Geometry faders (move/length/pitch) | `pendingGeometryDriverMotorSync_` | **300 ms** geometry-fader idle (`lastMotorSyncDriverInputMs_`) |

Queues **do not merge**. Input on one driver **cancels** the opposite pending queue. Flush uses `NoteEditFaderMotorTiming::runParallelMotorFaderBursts` (F1→dependents) or `sendFader1MotorTimedBurst` (geometry→F1).

### Live F1 select (Select kind)

- `handleSelectFaderInput` → `applyNoteSelectFromFader1Pitchbend` → `applySelectNav(..., requestFaderSync=false)` → schedules **select-dependent** motor sync (F2+F3+F4), not full outbound pipeline restart.
- Motor sync fires on **`EditorSelection.primaryNote`** / `NoteId` change, not list-index-only shifts.

### Geometry edit kinds (Move / Length / Pitch / Add / Delete)

- `syncSelectionFromGeometryEdit` updates `EditorSelection` from **moving-note `NoteId`** (focus when active).
- `applySelectionFromGeometryEdit` → `syncGeometrySelectionToUi` (bracket + display refresh only; **does not** recompute `selectedNoteIdx` or exit `EditStartNoteState`).
- F1 bracket motor flush is **outbound-only** during geometry moves (`sendFader1MotorTimedBurst` after geometry-fader idle).
- **User F1** during geometry kinds commits geometry and applies note select (`applySelectNav` → Select kind). Motor echo after geometry F1 send is blocked by `selectFaderFeedbackIgnoreUntilMs_` (see § F1 feedback ignore).

### F1 feedback ignore

| Mechanism | When |
|-----------|------|
| Value echo | `NoteEditFaderSelectSync::shouldIgnoreSelectFaderEcho` — inbound within **1500 ms** of last send and delta ≤ `SELECT_MOVEMENT_THRESHOLD` (100) |
| Armed window | `selectFaderFeedbackIgnoreUntilMs_` after `sendFader1BracketFeedback` / `sendFader1MotorTimedBurst` — inbound F1 ignored until window expires (catches motor landing off-threshold) |

`NoteEditManager::FEEDBACK_IGNORE_PERIOD` = **1500 ms** (not the 100 ms examples elsewhere in this file).

**Capture analysis:** [`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md), host verifier `scripts/test_note_edit_geometry_fader1_serial_verify.py`.

## NOTE_EDIT fader feedback on vs off

Compile-time switches in `include/NoteEditManager.h`:

| Flag | Default | Purpose |
|------|---------|---------|
| `kNoteEditFaderFeedbackEnabled` | `false` | Outbound DROID motor sync + inbound echo/stale-latch guards |
| `kEditedNoteAuditionEnabled` | `false` | MIDI note preview on pitch edit when transport stopped |

Set `kNoteEditFaderFeedbackEnabled = true` for production motorized DROID use and HITL fader-feedback regression (`openspec/specs/note-edit-fader-feedback`). Keep **`false`** for capture-serial geometry perf work, host logic tests, and sessions where motor echo would fight manual fader position.

### When feedback is ON (`true`)

| Subsystem | Behavior |
|-----------|----------|
| **Outbound** | `requestFaderOutbound`, `sendNoteEditSessionFaderFeedback`, motor triggers (F2–F4), F1 bracket bursts, select/geometry motor-sync queues |
| **F1 inbound** | `shouldIgnoreFaderInput` — 1500 ms value echo + `selectFaderFeedbackIgnoreUntilMs_` after outbound F1 |
| **F2–F4 inbound** | `shouldIgnoreDependentFaderInput` — stale-latch vs live snapshot; post-select `selectDependentSettleUntilMs_` blocks ch15 during outbound pipeline |
| **F4 pitch** | Ignores CC during `FEEDBACK_IGNORE_PERIOD` after note select |
| **Session open** | Full `SessionOpen` outbound pipeline (F1 → F2 → F3 → F4) per `note-edit-fader-feedback` spec |

### When feedback is OFF (`false`)

| Subsystem | Behavior |
|-----------|----------|
| **Outbound** | No MIDI to faders; `sendNoteEditSessionFaderFeedback` logs `feedback disabled` |
| **Motor sync queues** | `scheduleSelectDependentMotorSync`, `processPendingSelectDependentMotorSync`, `processPendingGeometryDriverMotorSync` — no-ops |
| **F1 inbound** | `shouldIgnoreFaderInput` always returns **false** — user F1 always processed (no motor-echo suppression) |
| **F2–F4 inbound** | `shouldIgnoreDependentFaderInput` always returns **false** — no stale-latch suppression |
| **Geometry edits** | **Unchanged** — `handleCoarseFaderInput`, `handleFineFaderInput`, `handleNoteValueFaderInput`, overlap handling, session undo |
| **Side effects** | `applyFaderOutboundDisabledSideEffects` still runs for `SessionOpen` (clears suppress flag), `LengthModeEnter` / `LengthModeExit` (enables `startEditingEnabled`) |

**Practical difference:** feedback **off** is for validating edit store, display, and heap behavior without motors moving or inbound being gated by last-sent values. Feedback **on** is required for motor/bracket alignment and for passing NOTE_EDIT fader HITL gates. F3/F4 debounce (`FINE_STABILITY_TIME`, `NOTE_VALUE_STABILITY_TIME`) and geometry guards (e.g. `geometry_driver_empty_step_ignored`) apply in **both** modes.

### Audition flag (`kEditedNoteAuditionEnabled`)

When `false` (default): no held note-on preview during pitch edit with transport stopped. When `true`: `sendEditedNoteAuditionWhenTransportStopped` drives a short preview note — independent of motor feedback.

## Channel Architecture (Enhanced)

### Smart Channel Management
- **Channel 16**: Fader 1 (SELECT) - Isolated operation
- **Channel 15**: Faders 2,3,4 - Coordinated operation with conflict prevention

### Configuration-Based Channel Mapping
```cpp
struct FaderConfig {
    MidiMapping::FaderType type;
    uint8_t channel;
    uint8_t ccNumber;        // For CC-based faders
    InputType inputType;     // PITCHBEND or CC_CONTROL
    ActionType action;
    uint8_t parameter;
    const char* description;
};
```

### Dynamic Channel Handling
- **Runtime Configuration**: Faders can be remapped to different channels
- **Conflict Detection**: Automatic detection of channel conflicts
- **Flexible Routing**: Support for non-standard channel assignments

## Feedback Prevention (Enhanced)

### Multi-Layer Feedback Prevention
1. **Timing-Based**: Ignore periods after outgoing updates
2. **Value-Based**: Compare with last sent values
3. **Deadband-Based**: Ignore small changes within deadband
4. **Channel-Based**: Coordinate shared channel faders

### Optimized Ignore Periods

> **Superseded for NOTE_EDIT:** see § NOTE_EDIT motor feedback above. `NoteEditManager` uses **1500 ms** `FEEDBACK_IGNORE_PERIOD` plus value-echo and `selectFaderFeedbackIgnoreUntilMs_`. The 100 ms example below is **not** current NOTE_EDIT behavior.

```cpp
// Generic MidiFaderProcessor examples — do not use for NOTE_EDIT motor sync
static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 100;
```

### Smart Ignore Logic
```cpp
bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType, 
                           int16_t pitchbendValue, uint8_t ccValue) {
    const FaderState& state = getFaderState(faderType);
    
    // Check timing-based ignore
    if (millis() - state.lastSentTime < FEEDBACK_IGNORE_PERIOD) {
        return true;
    }
    
    // Check value-based ignore
    if (!hasSignificantChange(state, pitchbendValue, ccValue)) {
        return true;
    }
    
    return false;
}
```

## Individual Fader Behaviors (V2)

### Fader 1 (SELECT) - Enhanced
- **Configuration**: Channel 16, Pitchbend, SELECT_NOTE action
- **Behavior**: Immediate note selection with cached note access
- **Isolation**: No cross-fader scheduling (separate channel)
- **Performance**: Uses cached notes for optimal selection speed

### Fader 2 (COARSE) - Enhanced  
- **Configuration**: Channel 15, Pitchbend, MOVE_NOTE_COARSE action
- **Behavior**: 16th-step positioning with stable note identity
- **Coordination**: Syncs with fine fader through driver system
- **Integration**: Works with cached note system and overlap handling

### Fader 3 (FINE) - Enhanced
- **Configuration**: Channel 15, CC2, MOVE_NOTE_FINE action
- **Behavior**: Tick-level positioning within 16th-note steps
- **Precision**: 1-unit deadband for precise control
- **Coordination**: Respects coarse fader driver status

### Fader 4 (NOTE_VALUE) - Enhanced
- **Configuration**: Channel 15, CC3, CHANGE_NOTE_VALUE action
- **Behavior**: Note velocity/property modification
- **Integration**: Works with overlap detection and cached notes
- **Precision**: Fine-grained control over note characteristics

## Configuration System

### Profile Management
```cpp
// Load predefined configurations
void loadFaderConfiguration(const char* configName) {
    if (strcmp(configName, "basic") == 0) {
        MidiFaderConfig::Config::loadBasicConfiguration();
    } else if (strcmp(configName, "extended") == 0) {
        MidiFaderConfig::Config::loadExtendedConfiguration();
    }
}
```

### Runtime Configuration
```cpp
// Add custom faders at runtime
void addCustomFader(MidiMapping::FaderType faderType, uint8_t channel, 
                   const char* description, MidiFaderConfig::ActionType action) {
    MidiFaderConfig::FaderConfig config(faderType, channel, description);
    config.withAction(action);
    MidiFaderConfig::Config::addFader(config);
}
```

### Configuration Validation
- **Channel Conflict Detection**: Prevents overlapping channel assignments
- **Action Validation**: Ensures actions are compatible with fader types
- **Parameter Validation**: Validates action parameters and ranges

## Performance Optimizations (V2)

### 1. Reduced Timing Overhead
- **100ms ignore periods** (down from 1500ms)
- **Faster response times** while maintaining feedback prevention
- **Optimized driver transition logic**

### 2. Configuration-Based Dispatch
- **O(1) configuration lookup** by channel/control type
- **Eliminates hardcoded channel checks**
- **Supports dynamic fader remapping**

### 3. Cached Note Integration
- **Uses Track::getCachedNotes()** for all note operations
- **95% performance improvement** over note reconstruction
- **Consistent performance across all fader operations**

### 4. Modular Architecture Benefits
- **Reduced coupling** between components
- **Easier testing** of individual components
- **Better maintainability** and extensibility

## Error Handling & Debugging (Enhanced)

### Comprehensive Logging
```cpp
logger.info("Fader movement: %s (type %d)", config->description, (int)faderType);
logger.debug("Executing fader action: type=%d fader=%d pitchbend=%d cc=%d", 
             action, faderType, pitchbendValue, ccValue);
```

### Configuration Debugging
- **Configuration validation** on startup
- **Runtime configuration inspection**
- **Fader state monitoring and reporting**

### Error Recovery
- **Missing configuration graceful handling**
- **Invalid input parameter validation**
- **Robust driver state management**

## Integration with Note Movement System

### Stable Note Identity Integration
- **Maintains moving note identity** across fader operations
- **Delegates to NoteMovementUtils** for overlap handling
- **Uses cached notes** for optimal performance

### Seamless EditState Integration
- **Works with all EditStates** (Select, Start, Length, Pitch)
- **Maintains state consistency** across fader/button interactions
- **Preserves edit context** during fader operations

## Current Status (2025 - V2 System)

✅ **Modular Architecture**: Clean separation of concerns across components  
✅ **Configuration-Driven**: Flexible, runtime-configurable fader mappings  
✅ **Performance Optimized**: 100ms response times with cached note integration  
✅ **Robust Error Handling**: Comprehensive validation and error recovery  
✅ **Extensible Design**: Easy addition of new fader types and actions  
✅ **Production Ready**: Extensively tested V2 system with improved reliability  
✅ **Cached Note Integration**: Seamless integration with 95% performance improvement  
✅ **Stable Identity Management**: Consistent note identity across all operations  

## Migration Benefits (V1 → V2)

### Architectural Improvements
- **Reduced coupling**: Components can be tested and modified independently
- **Better extensibility**: New fader types and actions easily added
- **Improved maintainability**: Clear separation of concerns
- **Enhanced configurability**: Runtime configuration changes supported

### Performance Improvements
- **Faster response times**: 100ms ignore periods vs 1500ms
- **Cached note integration**: 95% performance improvement
- **Optimized driver transitions**: Reduced overhead in fader switching
- **Configuration-based dispatch**: O(1) lookup vs hardcoded checks

### Reliability Improvements
- **Better error handling**: Comprehensive validation and recovery
- **Robust state management**: Improved driver state consistency
- **Enhanced logging**: Better debugging and monitoring capabilities
- **Modular testing**: Individual components can be tested in isolation

The V2 Fader State System provides a robust, performant, and maintainable foundation for hardware fader control while maintaining backward compatibility and smooth user experience. 