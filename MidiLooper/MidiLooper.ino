#include <LiquidCrystal.h>
#include <Bounce2.h>
#include <SD.h> 
#include <MIDI.h>

// Event storage
#define MAX_TRACKS 4
#define MAX_EVENTS 512

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 32, d5 = 31, d6 = 30, d7 = 29;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

static unsigned long lastScreenUpdate = 0;

const int recButtonPin = 9;
const int playButtonPin = 10;
const unsigned long holdThreshold = 1000; // 1 second
bool playButtonHeld = false;
unsigned long playButtonPressTime = 0;

Bounce recButton = Bounce();
Bounce playButton = Bounce();

volatile bool recTrigger = false;
volatile bool playTrigger = false;
bool firstEventCaptured = false;

const unsigned long debounceDelay = 50;
volatile unsigned long lastRecInterrupt = 0;
volatile unsigned long lastPlayInterrupt = 0;

MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI);

struct MidiEvent {
  uint32_t pulseOffset; // Clock pulse offset from loop start
  byte type, data1, data2;
};

struct MidiTrack {
  MidiEvent loopEvents[MAX_EVENTS];
  int eventCount = 0;
  bool playing = false;
  bool loopRecorded = false;
  bool loopRecording = false;
  bool loopRecordingArmed = false;
  uint32_t loopStartPulse = 0;
  uint32_t loopLengthPulses = 0;
  uint32_t recordStartPulse = 0;
  uint32_t lastEventPulse = 0;
  byte midiChannel = 1;  // New: default to MIDI channel 1
};

MidiTrack tracks[MAX_TRACKS];
int activeTrack = 0; // Currently selected track for loopRecording

// Clock tracking
volatile uint32_t clockPulses = 0;
uint32_t lastBarPulse = 0;

const int pulsesPerBeat = 24;
const int beatsPerBar = 4;
const int pulsesPerBar = pulsesPerBeat * beatsPerBar;

// Incoming MIDI Clock Handlers
void onClock() {
  clockPulses++;
  
 // Detect bar boundary (only once per bar)
  if (clockPulses % pulsesPerBar == 0 && clockPulses != lastBarPulse) {
    lastBarPulse = clockPulses;
    for (int i = 0; i < MAX_TRACKS; i++) {
      MidiTrack& t = tracks[i];
      if (t.loopRecordingArmed) {
        t.loopRecordingArmed = false;
        t.loopRecording = true;
        t.recordStartPulse = clockPulses;
        //loopStartPulse = clockPulses;
        t.eventCount = 0;
        Serial.println("Recording started cleanly at bar");
      }
    }
  }
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
    t.playing = false;
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
  }
  else {
    Serial.println("Event buffer full from track: ");
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


void onRecordButtonPressed() {
  MidiTrack& t = tracks[activeTrack];
  if (!t.loopRecording) {
    t.loopRecordingArmed = true;
    Serial.println("Recording armed");
  }
}

void startRecording() {
  MidiTrack& t = tracks[activeTrack];
  t.loopRecording = true;
  t.playing = false;
  t.eventCount = 0;
  t.loopStartPulse = (clockPulses / pulsesPerBar) * pulsesPerBar; // quantize start
  lcd.setCursor(0, 1);
  lcd.print("Recording started");
  Serial.println("Recording started");
}

void stopRecording() {
  MidiTrack& t = tracks[activeTrack];
  if (!t.loopRecording) return;

  t.loopRecording = false;
  // Quantize loop length to nearest bar from record start
  //loopLengthPulses = ((clockPulses - recordStartPulse + pulsesPerBar - 1) / pulsesPerBar) * pulsesPerBar;
  t.loopLengthPulses = ((t.lastEventPulse - t.loopStartPulse + pulsesPerBar - 1) / pulsesPerBar) * pulsesPerBar;

  // Set playback to begin from the start of the loopRecording
  t.loopStartPulse = t.recordStartPulse;
  t.playing = true;
  t.loopRecorded = (t.eventCount > 0);
  Serial.print("Recording stopped. Loop length: ");

  int loopBars = t.loopLengthPulses / pulsesPerBar + 1;
  Serial.println(loopBars);
}

void startPlayback() {
   MidiTrack& t = tracks[activeTrack];
  if (t.loopRecorded && t.loopLengthPulses > 0) {
    t.playing = true;
    t.loopStartPulse = (clockPulses / pulsesPerBar) * pulsesPerBar; // quantize
    lcd.setCursor(0, 1);
    lcd.print("Playback started");
    Serial.println("Playback started");
  }
}

void stopPlayback() {
  MidiTrack& t = tracks[activeTrack];
  t.playing = false;
  Serial.println("Playback stopped track: ");
  Serial.println(activeTrack);
}

void onRecButton() {
  unsigned long now = millis();
  if (now - lastRecInterrupt > debounceDelay) {
    recTrigger = true;
    lastRecInterrupt = now;
  }
}

void onPlayButton() {
  unsigned long now = millis();
  if (now - lastPlayInterrupt > debounceDelay) {
    playTrigger = true;
    lastPlayInterrupt = now;
  }
}

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

void updateDisplay() {
  MidiTrack& t = tracks[activeTrack];
  currentBar = ((clockPulses - t.loopStartPulse) / pulsesPerBar) % (t.loopLengthPulses / pulsesPerBar);
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


void setup() {
  Serial.begin(57600);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  
  MIDI.setHandleClock(onClock);
  MIDI.setHandleStart(onStart);
  MIDI.setHandleStop(onStop);
  MIDI.setHandleNoteOn(onNoteOn);
  MIDI.setHandleNoteOff(onNoteOff);
  MIDI.setHandleControlChange(onControlChange);
  MIDI.setHandlePitchBend(onPitchBend);
  
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
  Serial.println("Teensy Real-Time MIDI Looper Ready.");
}

void buttonState() {
  MidiTrack& t = tracks[activeTrack];

  recButton.update();
  playButton.update();

  // Record button toggle
  if (recButton.fell()) {
    if (!t.loopRecording)
      startRecording();
    else
      stopRecording();
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

void loop() {
  MIDI.read();
  buttonState();

  if (millis() - lastScreenUpdate > 100) {
    updateDisplay();
    lastUpdate = millis();
  }
}

