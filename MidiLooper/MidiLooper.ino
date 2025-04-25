#include <MIDI.h>
#include <LiquidCrystal.h>
#include <Bounce2.h>
#include <SPI.h>
#include <SD.h>

// ---------------------------------------------
// Variables
// ---------------------------------------------

// Event storage
#define MAX_TRACKS 4
#define MAX_EVENTS 1024

// Use these with the Teensy 3.5 & 3.6 & 4.1 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  11  // not actually used
#define SDCARD_SCK_PIN   13  // not actually used

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
volatile uint32_t clockPulses = 0;
uint32_t lastBarPulse = 0;

const int pulsesPerBeat = 24;
const int beatsPerBar = 4;
const int pulsesPerBar = pulsesPerBeat * beatsPerBar;

volatile bool recTrigger = false;
volatile bool playTrigger = false;
bool firstEventCaptured = false;

const unsigned long debounceDelay = 50;
volatile unsigned long lastRecInterrupt = 0;
volatile unsigned long lastPlayInterrupt = 0;

// Midi variables
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI);

struct MidiEvent {
  uint32_t pulseOffset; // Clock pulse offset from loop start
  byte type, data1, data2;
};

struct MidiTrack {
  MidiEvent loopEvents[MAX_EVENTS];
  MidiEvent backupEvents[MAX_EVENTS]; // ← backup
  int eventCount = 0;
  int backupEventCount = 0;
  bool playing = false;
  bool loopRecorded = false;
  bool loopRecording = false;
  bool loopRecordingArmed = false;
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
  clockPulses++; //speed of the pulses are set by the external clock at 24 pulses per quarter note
  
 // Detect bar boundary (only once per bar)
  if (clockPulses % pulsesPerBar == 0 && clockPulses != lastBarPulse) {
    lastBarPulse = clockPulses;
    for (int i = 0; i < MAX_TRACKS; i++) {
      MidiTrack& t = tracks[i];
      if (t.loopRecordingArmed) {
        t.loopRecordingArmed = false;
        t.loopRecording = true;
        t.recordStartPulse = clockPulses;
        t.loopStartPulse = clockPulses;
        t.eventCount = 0;
        Serial.println("Recording started cleanly at bar");
      }
    }
  }
  // Send Midi data per track if pulse has an loop event.
  for (int i = 0; i < MAX_TRACKS; i++) {
    MidiTrack& t = tracks[i];
    if (t.playing && t.loopRecorded && t.loopLengthPulses > 0) {
      uint32_t relPulse = (clockPulses - t.loopStartPulse) % t.loopLengthPulses;
      for (int j = 0; j < t.eventCount; j++) {
        if (t.loopEvents[j].pulseOffset == relPulse) {
          MIDI.send(t.loopEvents[j].type, t.loopEvents[j].data1, t.loopEvents[j].data2, t.midiChannel);
        }
      }
    }
  }
}

// ---------------------------------------------------- 
// Save Looper State to SD
// ----------------------------------------------------

// To store last used settings
struct LooperState {
  int activeTrack;
  byte midiChannels[MAX_TRACKS];
  bool playing[MAX_TRACKS];
};

// Store Looper State
void saveLooperStateToSD() {
  File file = SD.open("looper.dat", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  LooperState state;
  state.activeTrack = activeTrack;
  for (int i = 0; i < MAX_TRACKS; i++) {
    state.midiChannels[i] = tracks[i].midiChannel;
    state.playing[i] = tracks[i].playing;
  }

  file.write((uint8_t*)&state, sizeof(LooperState));
  file.close();
  Serial.println("Looper state saved to SD.");
}

// Load Looper State
void loadLooperStateFromSD() {
  if (!SD.exists("looper.dat")) {
    Serial.println("No saved looper state found");
    return;
  }

  File file = SD.open("looper.dat", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  LooperState state;
  file.read((uint8_t*)&state, sizeof(LooperState));
  file.close();

  activeTrack = constrain(state.activeTrack, 0, MAX_TRACKS - 1);
  for (int i = 0; i < MAX_TRACKS; i++) {
    tracks[i].midiChannel = state.midiChannels[i];
    tracks[i].playing = state.playing[i];
  }

  Serial.println("Looper state loaded from SD.");
}

// ---------------------------------------------------
// Load/Save Midi Track File
// ---------------------------------------------------

// Tempo information
void writeTempoMeta(File &file, uint32_t microsecondsPerQuarter) {
  file.write((byte)0x00); // delta-time
  file.write((byte)0xFF); // meta
  file.write((byte)0x51); // tempo
  file.write((byte)0x03); // length
  file.write((byte)((microsecondsPerQuarter >> 16) & 0xFF));
  file.write((byte)((microsecondsPerQuarter >> 8) & 0xFF));
  file.write((byte)(microsecondsPerQuarter & 0xFF));
}

// Signature information
void writeTimeSignatureMeta(File &file, byte numerator, byte denominatorPower) {
  file.write((byte)0x00); // delta-time
  file.write((byte)0xFF); // meta
  file.write((byte)0x58); // time signature
  file.write((byte)0x04); // length
  file.write(numerator);
  file.write(denominatorPower); // 2 = 4 (2^2 = 4)
  file.write((byte)24); // MIDI clocks per metronome tick
  file.write((byte)8);  // 1/32 notes per quarter note
}

void writeVarLen(File &file, uint32_t value) {
  byte buffer[4];
  int count = 0;

  buffer[count++] = value & 0x7F;
  while ((value >>= 7)) {
    buffer[count++] = 0x80 | (value & 0x7F);
  }

  for (int i = count - 1; i >= 0; i--) {
    file.write(buffer[i]);
  }
}

void saveTracksAsMidi(const char* filename) {
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  // --- MIDI Header Chunk (Format 0) ---
  file.print("MThd"); // MIDI header
  file.write((byte)0x00); file.write((byte)0x00);
  file.write((byte)0x00); file.write((byte)0x06); // header length = 6
  file.write((byte)0x00); file.write((byte)0x00); // format 0 = single track
  file.write((byte)0x00); file.write((byte)0x01); // 1 track only
  file.write((byte)0x00); file.write((byte)0x18); // 24 ticks per quarter note

  // --- Track Chunk ---
  file.print("MTrk");
  int trackLenPos = file.position();
  file.write((byte)0); file.write((byte)0); file.write((byte)0); file.write((byte)0);
  int trackStart = file.position();

  // Write tempo and time signature meta events at beginning
  writeTempoMeta(file, 500000); // 120 BPM
  writeTimeSignatureMeta(file, 4, 2); // 4/4

  // --- Collect and sort all events ---
  struct TempEvent {
    uint32_t pulseOffset;
    byte status;
    byte data1;
    byte data2;
  };
  TempEvent allEvents[MAX_TRACKS * MAX_EVENTS];
  int totalEvents = 0;

  for (int t = 0; t < MAX_TRACKS; t++) {
    MidiTrack& track = tracks[t];
    if (track.eventCount == 0) continue;

    for (int i = 0; i < track.eventCount; i++) {
      if (totalEvents >= MAX_TRACKS * MAX_EVENTS) break;

      MidiEvent& e = track.loopEvents[i];
      allEvents[totalEvents++] = {
        e.pulseOffset,
        (byte)(e.type | ((track.midiChannel - 1) & 0x0F)),
        e.data1,
        e.data2
      };
    }
  }

  // Sort by pulseOffset
  std::sort(allEvents, allEvents + totalEvents, [](const TempEvent& a, const TempEvent& b) {
    return a.pulseOffset < b.pulseOffset;
  });

  // Write events with delta-time
  uint32_t lastPulse = 0;
  for (int i = 0; i < totalEvents; i++) {
    uint32_t delta = allEvents[i].pulseOffset - lastPulse;
    lastPulse = allEvents[i].pulseOffset;

    writeVarLen(file, delta);
    file.write(allEvents[i].status);
    file.write(allEvents[i].data1);
    file.write(allEvents[i].data2);
  }

  // --- End of Track ---
  file.write((byte)0x00); // delta-time
  file.write((byte)0xFF); file.write((byte)0x2F); file.write((byte)0x00); // End of track

  // --- Finalize track length ---
  int trackEnd = file.position();
  int length = trackEnd - trackStart;
  file.seek(trackLenPos);
  file.write((byte)((length >> 24) & 0xFF));
  file.write((byte)((length >> 16) & 0xFF));
  file.write((byte)((length >> 8) & 0xFF));
  file.write((byte)(length & 0xFF));
  file.seek(trackEnd);

  file.close();
  Serial.println("MIDI (Format 0) file saved!");
}

// Save Raw data
void saveAllTracksRaw() {
  for (int t = 0; t < MAX_TRACKS; t++) {
    MidiTrack& track = tracks[t];
    if (track.eventCount == 0) continue;

    char filename[16];
    sprintf(filename, "track%d.raw", t + 1);

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
      Serial.print("Failed to write ");
      Serial.println(filename);
      continue;
    }

    // --- Write metadata ---
    file.write((byte)(track.eventCount >> 8));
    file.write((byte)(track.eventCount));

    file.write((byte)(track.loopStartPulse >> 24));
    file.write((byte)(track.loopStartPulse >> 16));
    file.write((byte)(track.loopStartPulse >> 8));
    file.write((byte)(track.loopStartPulse));

    file.write((byte)(track.loopLengthPulses >> 24));
    file.write((byte)(track.loopLengthPulses >> 16));
    file.write((byte)(track.loopLengthPulses >> 8));
    file.write((byte)(track.loopLengthPulses));

    file.write((byte)(track.midiChannel));

    // --- Write events ---
    for (int i = 0; i < track.eventCount; i++) {
      MidiEvent& e = track.loopEvents[i];

      file.write((byte)(e.pulseOffset >> 24));
      file.write((byte)(e.pulseOffset >> 16));
      file.write((byte)(e.pulseOffset >> 8));
      file.write((byte)(e.pulseOffset));

      file.write(e.type);
      file.write(e.data1);
      file.write(e.data2);
    }

    file.close();
    Serial.print("Saved raw track ");
    Serial.println(t + 1);
  }
}

// Read raw data
bool loadTrackFromRaw(int trackIndex, const char* filename) {
  File file = SD.open(filename);
  if (!file) {
    Serial.print("Failed to open ");
    Serial.println(filename);
    return false;
  }

  MidiTrack& t = tracks[trackIndex];
  t.eventCount = 0;
  t.playing = false;
  t.loopRecorded = false;

  // --- Read metadata ---
  int eventCount = file.read() << 8;
  eventCount |= file.read();

  t.loopStartPulse  = (file.read() << 24);
  t.loopStartPulse |= (file.read() << 16);
  t.loopStartPulse |= (file.read() << 8);
  t.loopStartPulse |= file.read();

  t.loopLengthPulses  = (file.read() << 24);
  t.loopLengthPulses |= (file.read() << 16);
  t.loopLengthPulses |= (file.read() << 8);
  t.loopLengthPulses |= file.read();

  t.midiChannel = file.read();

  // --- Read events ---
  while (file.available() && t.eventCount < MAX_EVENTS) {
    uint32_t offset = file.read() << 24;
    offset |= file.read() << 16;
    offset |= file.read() << 8;
    offset |= file.read();

    byte type = file.read();
    byte d1 = file.read();
    byte d2 = file.read();

    t.loopEvents[t.eventCount++] = {offset, type, d1, d2};
  }

  file.close();

  if (t.eventCount > 0) {
    t.loopRecorded = true;

    Serial.print("Loaded track ");
    Serial.print(trackIndex + 1);
    Serial.print(" from ");
    Serial.println(filename);
    return true;
  }

  return false;
}


void loadAllTracksFromRaw() {
  char filename[16];
  for (int i = 0; i < MAX_TRACKS; i++) {
    sprintf(filename, "track%d.raw", i + 1);
    if (SD.exists(filename)) {
      loadTrackFromRaw(i, filename);
      lcd.setCursor(0, 0);
      lcd.print("Loaded track ");
      lcd.print(i + 1);
      delay(500);
      lcd.clear();
    } else {
      Serial.print("Raw track not found: ");
      Serial.println(filename);
    }
  }
}

// -------------------------------------------
// Midi States
// -------------------------------------------

void onStart() {
  clockPulses = 0;
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
  Serial.println("Clock Stop");
}

void onNoteOn(byte ch, byte note, byte vel) {
  MidiTrack& t = tracks[activeTrack];
  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
    uint32_t pulse = clockPulses;
    if (t.eventCount == 0) {
      t.loopStartPulse = clockPulses;
      t.recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    t.loopEvents[t.eventCount++] = {clockPulses - t.loopStartPulse, midi::NoteOn, note, vel};
    t.lastEventPulse = pulse;
  } else {
    Serial.print("Event buffer full from track: ");
    Serial.println(activeTrack+1);
  }
}

void onNoteOff(byte ch, byte note, byte vel) {
  MidiTrack& t = tracks[activeTrack];
  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
     uint32_t pulse = clockPulses;
     if (t.eventCount == 0) {
      t.loopStartPulse = clockPulses;
      t.recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    t.loopEvents[t.eventCount++] = {clockPulses - t.loopStartPulse, midi::NoteOff, note, vel};
    t.lastEventPulse = pulse;
  }
  else {
    Serial.println("Event buffer full!");
  }
}

void onControlChange(byte ch, byte num, byte val) {
  MidiTrack& t = tracks[activeTrack];
  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
    uint32_t pulse = clockPulses;
      if (t.eventCount == 0) {
      t.loopStartPulse = clockPulses;
      t.recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    t.loopEvents[t.eventCount++] = {clockPulses - t.loopStartPulse, midi::ControlChange, num, val};
    t.lastEventPulse = pulse;
  }
  else {
    Serial.println("Event buffer full!");
  }
}

void onPitchBend(byte ch, int bend) {
  MidiTrack& t = tracks[activeTrack];
  if (t.loopRecording && t.eventCount < MAX_EVENTS) {
    uint32_t pulse = clockPulses;
    if (t.eventCount == 0) {
      t.loopStartPulse = clockPulses;
      t.recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    byte lsb = bend & 0x7F;
    byte msb = (bend >> 7) & 0x7F;
    t.loopEvents[t.eventCount++] = {clockPulses - t.loopStartPulse, midi::PitchBend, lsb, msb};
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

void startRecording() {
  MidiTrack& t = tracks[activeTrack];

  // Backup current loop
  t.backupEventCount = t.eventCount;
  memcpy(t.backupEvents, t.loopEvents, sizeof(MidiEvent) * t.eventCount);

  t.loopRecording = true;
  t.playing = false;
  t.eventCount = 0;
  t.loopStartPulse = (clockPulses / pulsesPerBar) * pulsesPerBar; // quantize start
  lcd.setCursor(0, 0);
  lcd.print("Rec ");
  Serial.println("Recording started");
}

void stopRecording() {
  MidiTrack& t = tracks[activeTrack];
  if (!t.loopRecording) return;

  t.loopRecording = false;
  // Quantize loop length to nearest bar from record start
  t.loopLengthPulses = ((t.lastEventPulse - t.loopStartPulse + pulsesPerBar - 1) / pulsesPerBar) * pulsesPerBar;

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
    t.loopStartPulse = (clockPulses / pulsesPerBar) * pulsesPerBar; // quantize
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
  }

  // Long hold to undo recording
  if (recButton.read() == LOW && !recButtonHeld) {
    if (millis() - recButtonPressTime > holdThreshold) {
      recButtonHeld = true;
      undoTrack();  // Long press action
    }
  }

  // Record button toggle
  if (recButton.fell()) {
    if (!recButtonHeld) {
      if (!t.loopRecording)
        startRecording();
      else
        stopRecording();
    }
  }

  // Handle rec button press
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

  // Short press toggle playback
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
  uint32_t playPulse = (clockPulses - t.loopStartPulse) % t.loopLengthPulses;
  uint32_t playStep = ((playPulse % pulsesPerBar) * BAR_STEPS) / pulsesPerBar;
  if (playStep >= BAR_STEPS) playStep = BAR_STEPS - 1;
  barGrid[playStep] = '>';
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

void updateDisplay() {
    MidiTrack& t = tracks[activeTrack];
    
    if (t.loopLengthPulses > 0) {
      currentBar = ((clockPulses - t.loopStartPulse) / pulsesPerBar) % (t.loopLengthPulses / pulsesPerBar);
    } else {
      currentBar = 0; // prevent dived by 0
    }

    buildBarGrid(currentBar);
    
    lcd.setCursor(0, 0);
    
    if (t.loopRecording) lcd.print("Rec ");
    else if (t.loopRecordingArmed) lcd.print("Arm ");
    else if (t.playing) lcd.print("Play");
    else lcd.print("Idle");
    
    overlayPlayhead();
    lcd.setCursor(0, 1);
    lcd.print(barGrid);

    lcd.setCursor(14, 0);
    if (t.midiChannel < 10) {
      lcd.print(" ");
    }
    lcd.print(t.midiChannel);

    // Show current bar
    uint32_t pulses = (clockPulses - t.loopStartPulse) % t.loopLengthPulses;
    int bar = pulses / pulsesPerBar;
    int beat = (pulses % pulsesPerBar) / (pulsesPerBar / 4);

    lcd.setCursor(6, 0);
    lcd.print(bar + 1);
    lcd.print(":");
    lcd.print(beat + 1);

    // Show event count
    lcd.setCursor(12, 0);
    lcd.print("*");
    lcd.print(t.eventCount);
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
