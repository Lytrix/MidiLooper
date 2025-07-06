/*
 * Example: Button Configuration for MIDI Looper
 * 
 * This example demonstrates the MIDI button configuration for the looper.
 * 
 * MIDI Configuration:
 * - Button A (C2, note 36): Toggle play/stop
 * - Button B (C#2, note 37): Toggle record/overdub
 * - Encoder Button (D2, note 38): Cycle between main edit modes
 * 
 * Main Edit Modes:
 * 1. NOTE_EDIT Mode (Program 1, Note 0 trigger on channel 16):
 *    - All 4 faders work for note editing
 *    - Display shows note highlighting and bracket
 *    - Fader 1: Note Selection (Pitchbend channel 16)
 *    - Fader 2: Coarse Position (Pitchbend channel 15)
 *    - Fader 3: Fine Position (CC2 channel 15)
 *    - Fader 4: Note Value/Pitch (CC3 channel 15)
 * 
 * 2. LOOP_EDIT Mode (Program 0, Note 100 trigger on channel 16):
 *    - All 4 faders are disabled for note editing
 *    - Display shows notes without highlighting
 *    - Loop Length Control: CC 101 on channel 16 (1-128 bars)
 *    - CC value 0 = 1 bar, CC value 127 = 128 bars
 * 
 * Loop Length Editing:
 * - Only works when in LOOP_EDIT mode
 * - Uses CC 101 on channel 16
 * - Maps CC values (0-127) to bars (1-128)
 * - Preserves all MIDI events when changing loop length
 * - Updates display automatically
 * 
 * Usage:
 * 1. Press encoder button to switch to LOOP_EDIT mode
 * 2. Send CC 101 on channel 16 with value 0-127
 * 3. Loop length changes from 1-128 bars accordingly
 * 4. All notes remain intact
 * 5. Display updates to show new loop length
 * 
 */

#include "Arduino.h"

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {
        // Wait up to 2 seconds for serial connection
    }
    
    Serial.println("MIDI Looper Example: Button Configuration");
    Serial.println("=========================================");
    Serial.println();
    Serial.println("MIDI Button Mappings:");
    Serial.println("- Button A (C2/36): Play/Stop toggle");
    Serial.println("- Button B (C#2/37): Record/Overdub toggle");  
    Serial.println("- Encoder Button (D2/38): Cycle edit modes");
    Serial.println();
    Serial.println("Main Edit Modes:");
    Serial.println("1. NOTE_EDIT: Faders control note editing");
    Serial.println("2. LOOP_EDIT: CC 101 controls loop length (1-128 bars)");
    Serial.println();
    Serial.println("Loop Length Control:");
    Serial.println("- CC 101 on channel 16");
    Serial.println("- Value 0 = 1 bar, Value 127 = 128 bars");
    Serial.println("- Only works in LOOP_EDIT mode");
    Serial.println("- Preserves all MIDI events");
    Serial.println();
}

void loop() {
    // Main loop - actual MIDI handling is done by the NoteEditManager
    delay(100);
} 