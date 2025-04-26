#include <MIDI.h>
#include <LiquidCrystal.h>
#include <Bounce2.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"
#include "storage.h"
// ---------------------------------------------
// Variables
// ---------------------------------------------

// Display variables
const int rs = 12; // reset       (pin 4)
const int en = 11; // enbable     (pin 6)
const int d4 = 32; // data line 4
const int d5 = 31; // data line 5
const int d6 = 30; // data line 6
const int d7 = 29; // data line 7 (pin 16)
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

static unsigned long lastScreenUpdate = 0;

// Button variables
const int recButtonPin = 9;
const int playButtonPin = 10;
const unsigned long holdThreshold = 1000; // 1 second
unsigned long playButtonPressTime = 0;
unsigned long recButtonPressTime = 0;
bool playButtonHeld = false;
bool recButtonHeld = false;

Bounce recButton = Bounce();
Bounce playButton = Bounce();

// Clock tracking variables
volatile uint32_t currentPulse = 0;
uint32_t lastBarPulse = 0;

const int pulsesPerBeat = 24;
const int beatsPerBar = 4;
const int pulsesPerBar = pulsesPerBeat * beatsPerBar;

volatile bool recTrigger = false;
volatile bool playTrigger = false;
bool firstEventCaptured = false;

// Global Array to store notes which are still in Note on state
bool activeNotes[128] = {false};

// Midi variables
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI);

enum TrackState { IDLE, RECORDING, OVERDUBBING, PLAYBACK };

struct MidiEvent {
  uint32_t pulseOffset; // Clock pulse offset from loop start
  byte type, data1, data2;
};

struct MidiTrack {
  TrackState state = IDLE;
  MidiEvent loopEvents[MAX_EVENTS];
  MidiEvent backupEvents[MAX_EVENTS]; // ← backup
  int eventCount = 0;
  int backupEventCount = 0;
  bool playing = false;
  
  bool loopRecorded = false;
  bool loopRecording = false;
  bool loopRecordingArmed = false;
  bool loopOverdubbing = false;
  
  uint32_t loopStartPulse = 0;
  uint32_t loopLengthPulses = 0;
  uint32_t recordStartPulse = 0;
  uint32_t lastEventPulse = 0;
  byte midiChannel = 1;  // Default to MIDI channel 1
};

// Initiate tracks with struct
MidiTrack tracks[MAX_TRACKS];
int activeTrack = 0; // Currently selected track for loopRecording


// ---------------------------------------------------------------------
// Handle clock-based actions per pulse (loop playback, bar sync, etc.)
// ---------------------------------------------------------------------

void onClock() {
  currentPulse++; //speed of the pulses are set by the external clock at 24 pulses per quarter note
  
 // Detect bar boundary (only once per bar)
  if (currentPulse % pulsesPerBar == 0 && currentPulse != lastBarPulse) {
    lastBarPulse = currentPulse;
    for (int i = 0; i < MAX_TRACKS; i++) {
      MidiTrack& t = tracks[i];
      if (t.loopRecordingArmed) {
        t.loopRecordingArmed = false;
        t.loopRecording = true;
        t.recordStartPulse = currentPulse;
        t.loopStartPulse = currentPulse;
        t.eventCount = 0;
        Serial.println("Recording started cleanly at bar");
      }
    }
  }
  // Send Midi data per track if pulse has an loop event.
  for (int i = 0; i < MAX_TRACKS; i++) {
    MidiTrack& t = tracks[i];
    if (t.playing && t.loopRecorded && t.loopLengthPulses > 0) {
      uint32_t relPulse = (currentPulse - t.loopStartPulse) % t.loopLengthPulses;
      for (int j = 0; j < t.eventCount; j++) {
        if (t.loopEvents[j].pulseOffset == relPulse) {
          MIDI.send(t.loopEvents[j].type, t.loopEvents[j].data1, t.loopEvents[j].data2, t.midiChannel);
        }
      }
    }
  }
}


// -------------------------------------------
// Midi States
// -------------------------------------------

void onStart() {
  currentPulse = 0;
  for (int i = 0; i < MAX_TRACKS; i++) {
    MidiTrack& t = tracks[i];
    t.loopStartPulse = 0;
  }
  Serial.println("Clock Start");
}

void onStop() {
  for (int i = 0; i < MAX_TRACKS; i++) {
    MidiTrack& t = tracks[i];
    //t.playing = false;
    t.loopRecording = false;
  }
  
  // Send all Notes off
  for (int ch = 1; ch <= 16; ch++) {
    MIDI.sendControlChange(123, 0, ch); // 123 = All Notes Off
  }
  
  Serial.println("Clock Stop");
}

void onNoteOn(byte ch, byte note, byte vel) {
  MidiTrack& t = tracks[activeTrack];

  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
    uint32_t pulse = currentPulse;

    // Start of new recording
    if (t.eventCount == 0) {
      t.loopStartPulse = pulse;
      t.recordStartPulse = pulse;
      firstEventCaptured = true;
    }

    // Allow recording if not overdubbing, or if overdubbing and inside loop range
    if (!t.loopOverdubbing || (t.loopOverdubbing && (pulse - t.loopStartPulse) < t.loopLengthPulses)) {
      t.loopEvents[t.eventCount++] = {pulse - t.loopStartPulse, midi::NoteOn, note, vel};
      t.lastEventPulse = pulse;

      // Track note on for proper note off management
      activeNotes[note] = true;
    }
  } else {
    Serial.print("Event buffer full from track: ");
    Serial.println(activeTrack + 1);
  }
}

void onNoteOff(byte ch, byte note, byte vel) {
  MidiTrack& t = tracks[activeTrack];

  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
    uint32_t pulse = currentPulse;

    // Start of new recording
    if (t.eventCount == 0) {
      t.loopStartPulse = pulse;
      t.recordStartPulse = pulse;
      firstEventCaptured = true;
    }

    // Allow recording if not overdubbing, or if overdubbing and within loop
    if (!t.loopOverdubbing || (t.loopOverdubbing && (pulse - t.loopStartPulse) < t.loopLengthPulses)) {
      t.loopEvents[t.eventCount++] = {pulse - t.loopStartPulse, midi::NoteOff, note, vel};
      t.lastEventPulse = pulse;

      // Mark note as off to avoid hanging notes
      activeNotes[note] = false;
    }
  } else {
    Serial.println("Event buffer full!");
  }
}


void onControlChange(byte ch, byte num, byte val) {
  MidiTrack& t = tracks[activeTrack];
  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
    uint32_t pulse = currentPulse;
      if (t.eventCount == 0) {
      t.loopStartPulse = currentPulse;
      t.recordStartPulse = currentPulse;
      firstEventCaptured = true;
    }
    t.loopEvents[t.eventCount++] = {currentPulse - t.loopStartPulse, midi::ControlChange, num, val};
    t.lastEventPulse = pulse;
  }
  else {
    Serial.println("Event buffer full!");
  }
}

void onPitchBend(byte ch, int bend) {
  MidiTrack& t = tracks[activeTrack];
  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
    uint32_t pulse = currentPulse;
    if (t.eventCount == 0) {
      t.loopStartPulse = currentPulse;
      t.recordStartPulse = currentPulse;
      firstEventCaptured = true;
    }
    byte lsb = bend & 0x7F;
    byte msb = (bend >> 7) & 0x7F;
    t.loopEvents[t.eventCount++] = {currentPulse - t.loopStartPulse, midi::PitchBend, lsb, msb};
    t.lastEventPulse = pulse;
  }
  else {
    Serial.println("Event buffer pitchbend full track: ");
    Serial.println(activeTrack);
  }
}

// -------------------------------------------
// Looper States
// -------------------------------------------

void switchTrack() {
  activeTrack = (activeTrack + 1) % MAX_TRACKS;
  lcd.setCursor(0, 1);
  lcd.print("Switched to T");
  lcd.print(activeTrack + 1);
  lcd.print("       "); // clear leftover chars
  Serial.print("Switched to Track ");
  Serial.println(activeTrack + 1);
  delay(500); // Give user visual confirmation
}

void onRecordButtonPressed() {
  MidiTrack& t = tracks[activeTrack];
  if (!t.loopRecording) {
    t.loopRecordingArmed = true;
    Serial.println("Recording armed");
  }
}

uint32_t computeLoopLengthFromEvents(MidiEvent* events, int eventCount, uint32_t pulsesPerBar) {
  if (eventCount == 0) return 0;

  // Sort events in-place by pulseOffset
  std::sort(events, events + eventCount, [](const MidiEvent& a, const MidiEvent& b) {
    return a.pulseOffset < b.pulseOffset;
  });

  // Determine the last meaningful event offset
  uint32_t lastEventOffset = 0;
  for (int i = 0; i < eventCount; i++) {
    byte type = events[i].type & 0xF0;
    if (type == 0x90 || type == 0x80 || type == 0xB0) { // NoteOn, NoteOff, ControlChange
      if (events[i].pulseOffset > lastEventOffset) {
        lastEventOffset = events[i].pulseOffset;
      }
    }
  }

  // Round up to the next full bar
  uint32_t loopLength = ((lastEventOffset / pulsesPerBar) + 1) * pulsesPerBar;
  return loopLength;
}


void startRecording() {
  MidiTrack& t = tracks[activeTrack];

  // Backup current loop
  t.backupEventCount = t.eventCount;
  memcpy(t.backupEvents, t.loopEvents, sizeof(MidiEvent) * t.eventCount);
  
  t.loopRecording = true;
  t.playing = false;
  t.eventCount = 0;
  t.loopStartPulse = (currentPulse / pulsesPerBar) * pulsesPerBar; // quantize start
  t.loopLengthPulses = 0;
  lcd.setCursor(0, 0);
  lcd.print("Rec ");
  Serial.println("Recording started");
}

void stopRecording() {
  MidiTrack& t = tracks[activeTrack];
  if (!t.loopRecording) return;

  // Add note off for all notes which still are in note on state on endPulse
  uint32_t endPulse = currentPulse - t.loopStartPulse;

  for (int note = 0; note < 128; note++) {
    if (activeNotes[note]) {
      // Insert a Note Off at the end of the loop
      if (t.eventCount < MAX_EVENTS) {
        t.loopEvents[t.eventCount++] = {
          endPulse, midi::NoteOff, note, 0
        };
      }
      activeNotes[note] = false;
    }
  }

  t.loopRecording = false;
  // Quantize loop length to nearest bar from record start
  // t.loopLengthPulses = ((t.lastEventPulse - t.loopStartPulse + pulsesPerBar - 1) / pulsesPerBar) * pulsesPerBar;
  t.loopLengthPulses = computeLoopLengthFromEvents(t.loopEvents, t.eventCount, pulsesPerBar);

  // Set playback to begin from the start of the loopRecording
  t.loopStartPulse = t.recordStartPulse;
  t.playing = true;
  lcd.setCursor(0, 0);
  lcd.print("Play");
  t.loopRecorded = (t.eventCount > 0);
  Serial.print("Recording stopped. Loop length: ");

  int loopBars = t.loopLengthPulses / pulsesPerBar + 1;
  Serial.println(loopBars);

  // Auto-save after recording
  // char filename[16];
  // snprintf(filename, sizeof(filename), "track%d.mid", activeTrack + 1);
  // saveTracksAsMidi(filename);
  saveAllTracksRaw();
}

void startPlayback() {
   MidiTrack& t = tracks[activeTrack];
  if (t.loopRecorded && t.loopLengthPulses > 0) {
    t.playing = true;
    t.loopStartPulse = (currentPulse / pulsesPerBar) * pulsesPerBar; // quantize
    lcd.setCursor(0, 0);
    lcd.print("Play");
    Serial.println("Playback started");
  }
}

void stopPlayback() {
  MidiTrack& t = tracks[activeTrack];
  t.playing = false;
  lcd.setCursor(0, 0);
  lcd.print("Stop");
  Serial.println("Playback stopped track: ");
  Serial.println(activeTrack);
}

void showUndoMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Undo performed!");
  lcd.setCursor(0, 1);
  lcd.print("Track ");
  lcd.print(activeTrack + 1);

  delay(1000);  // Show for 1 second
  lcd.clear();
}

void undoTrack() {
  MidiTrack& t = tracks[activeTrack];
  if (t.backupEventCount > 0) {
    t.eventCount = t.backupEventCount;
    memcpy(t.loopEvents, t.backupEvents, sizeof(MidiEvent) * t.eventCount);
    t.loopRecorded = true;
    t.playing = true;
    Serial.println("Undo performed");

    showUndoMessage();
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Nothing to undo    ");
    delay(1000);
    lcd.clear();
    Serial.println("No backup found for undo");
    
  }
}

// -------------------------------------------
// Buttons
// -------------------------------------------



void buttonState() {
  MidiTrack& t = tracks[activeTrack];

  recButton.update();
  playButton.update();

  // Handle rec button press
  if (recButton.fell()) {
    recButtonPressTime = millis();
    recButtonHeld = false;

    // Double-tap or toggle logic
    if (!recButtonHeld) {
      if (!t.loopRecording) {
        startRecording();
      } else if (!t.loopOverdubbing) {
        // Start overdubbing mode
        t.loopOverdubbing = true;
        t.loopLengthPulses = currentPulse - t.loopStartPulse; // Set loop length now
        startPlayback(); // Ensure playback is on when overdubbing
        Serial.println("Entered overdub mode.");
      } else {
        stopRecording(); // If already overdubbing, stop recording
      }
    }
  }

  // Long hold to undo recording
  if (recButton.read() == LOW && !recButtonHeld) {
    if (millis() - recButtonPressTime > holdThreshold) {
      recButtonHeld = true;
      undoTrack();  // Long press action
    }
  }

  // Handle play button press
  if (playButton.fell()) {
    playButtonPressTime = millis();
    playButtonHeld = false;
  }

  // Long hold to switch track
  if (playButton.read() == LOW && !playButtonHeld) {
    if (millis() - playButtonPressTime > holdThreshold) {
      playButtonHeld = true;
      switchTrack(); // Long press action
    }
  }

  // Short press to toggle playback
  if (playButton.rose()) {
    if (!playButtonHeld) {
      if (!t.playing)
        startPlayback();
      else
        stopPlayback();
    }
  }
}




// ---------------------------------------------------
// Display functions
// ---------------------------------------------------
#define GRID_STEPS 16
char grid[GRID_STEPS + 1];  // +1 for null-terminator

#define BAR_STEPS 16
char barGrid[BAR_STEPS + 1];
uint32_t currentBar = 0;

void buildBarGrid(uint32_t barIndex) {
  MidiTrack& t = tracks[activeTrack];
  memset(barGrid, '-', BAR_STEPS);
  barGrid[BAR_STEPS] = '\0';

  uint32_t startPulse = barIndex * pulsesPerBar;
  uint32_t endPulse = startPulse + pulsesPerBar;

  for (int i = 0; i < t.eventCount; i++) {
    uint32_t pulse = t.loopEvents[i].pulseOffset;
    if (pulse < startPulse || pulse >= endPulse) continue;

    uint32_t step = ((pulse - startPulse) * BAR_STEPS) / pulsesPerBar;
    if (step >= BAR_STEPS) step = BAR_STEPS - 1;

    switch (t.loopEvents[i].type) {
      case midi::NoteOn:
        barGrid[step] = '*'; break;
      case midi::ControlChange:
        barGrid[step] = 'C'; break;
      case midi::PitchBend:
        barGrid[step] = 'B'; break;
      default:
        barGrid[step] = '+';
    }
    // Overlay cursor
    overlayPlayhead();

  }
}

void overlayPlayhead() {
  MidiTrack& t = tracks[activeTrack];
  uint32_t playPulse = (currentPulse - t.loopStartPulse) % t.loopLengthPulses;
  uint32_t playStep = ((playPulse % pulsesPerBar) * BAR_STEPS) / pulsesPerBar;
  if (playStep >= BAR_STEPS) playStep = BAR_STEPS - 1;
  barGrid[playStep] = '>';
}

void updateDisplay() {
    MidiTrack& t = tracks[activeTrack];
    
    if (t.loopLengthPulses > 0) {
      currentBar = ((currentPulse - t.loopStartPulse) / pulsesPerBar) % (t.loopLengthPulses / pulsesPerBar);
    } else {
      currentBar = 0; // prevent dived by 0
    }

    buildBarGrid(currentBar);
    
    lcd.setCursor(0, 0);
    
    if (t.loopRecording) lcd.print("Rec ");
    else if (t.loopOverdubbing) lcd.print("Over");
    else if (t.loopRecordingArmed) lcd.print("Arm ");
    else if (t.playing) lcd.print("Play");
    else lcd.print("Idle");
    
    overlayPlayhead();
    lcd.setCursor(0, 1);
    lcd.print(barGrid);

    lcd.setCursor(10, 0);
    if (t.midiChannel < 10) {
      lcd.print(" ");
    }
    lcd.print(t.midiChannel);

    // Show current bar
    uint32_t pulses = (currentPulse - t.loopStartPulse) % t.loopLengthPulses;
    int bar = pulses / pulsesPerBar;
    int beat = (pulses % pulsesPerBar) / (pulsesPerBar / 4);

    lcd.setCursor(5, 0);
    lcd.print(bar + 1);
    lcd.print(":");
    lcd.print(beat + 1);

    // Show event count
    lcd.setCursor(12, 0);
    lcd.print("*");
    //lcd.print(t.eventCount);
    //lcd.print("        "); // Clear remaining  
}

// ---------------------------------------------------
// Setup
// ---------------------------------------------------

void setup() {
  Serial.begin(115200);             // Fastest speed to quickly process debug messages

  // Initialize the SD card
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("SD card initialization failed!");
    while (1);
  }
  Serial.println("SD card initialized.");
  
  delay(500);                       // Give SD card some time to initialize fully

  loadLooperStateFromSD();          // Auto load last used Looper state

  MIDI.begin(MIDI_CHANNEL_OMNI);    // Open Midi ports at 31,250 bps are set in this function
  
  MIDI.setHandleClock(onClock);     // This handle is run at 24 PPQN set by the external clock
  MIDI.setHandleStart(onStart);
  MIDI.setHandleStop(onStop);
  
  MIDI.setHandleNoteOn(onNoteOn);
  MIDI.setHandleNoteOff(onNoteOff);
  MIDI.setHandleControlChange(onControlChange);
  MIDI.setHandlePitchBend(onPitchBend);
  
  // USB MIDI (Teensy built-in)
  // usbMIDI.setHandleClock(onClock);
  // usbMIDI.setHandleStart(onStart);
  // usbMIDI.setHandleStop(onStop);
  // usbMIDI.setHandleNoteOn(onNoteOn);
  // usbMIDI.setHandleNoteOff(onNoteOff);
  // usbMIDI.setHandleControlChange(onControlChange);
  // usbMIDI.setHandlePitchChange(onPitchBend);

  pinMode(recButtonPin, INPUT_PULLUP);
  pinMode(playButtonPin, INPUT_PULLUP);

  recButton.attach(recButtonPin);
  recButton.interval(5);  // 5 ms debounce

  playButton.attach(playButtonPin);
  playButton.interval(5); // 5 ms debounce

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // Print a message to the LCD.
  lcd.setCursor(0, 0);
  lcd.print("MIDI Looper v0.1");
  delay(1000);
  lcd.clear();

  // Auto load midi tracks  
  loadAllTracksFromRaw();
  Serial.println("Teensy Real-Time MIDI Looper Ready.");
}

// ---------------------------------------------------
// Main Loop
// ---------------------------------------------------

void loop() {
  //usbMIDI.read();  // USB MIDI has priority — runs first
  MIDI.read();     // Fallback to DIN MIDI
  buttonState();
  if (millis() - lastScreenUpdate > 100) { // Do not send the data too often to ensure Midi has priority
    updateDisplay();
    lastScreenUpdate = millis();
  }
}
