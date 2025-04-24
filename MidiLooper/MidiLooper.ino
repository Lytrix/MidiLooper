#include <LiquidCrystal.h>
#include <Bounce2.h>
#include <SD.h> 
#include <MIDI.h>

#define NOTE_ON  0x90
#define NOTE_OFF 0x80
#define CC       0xB0
#define PB       0xE0

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 32, d5 = 31, d6 = 30, d7 = 29;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

const int recButtonPin = 9;
const int playButtonPin = 10;

Bounce recButton = Bounce();
Bounce playButton = Bounce();

volatile bool recTrigger = false;
volatile bool playTrigger = false;
bool firstEventCaptured = false;

const unsigned long debounceDelay = 50;
volatile unsigned long lastRecInterrupt = 0;
volatile unsigned long lastPlayInterrupt = 0;


MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI);

// Event storage
const int MAX_EVENTS = 512;

struct MidiEvent {
  uint32_t pulseOffset; // Clock pulse offset from loop start
  byte type, data1, data2;
};

MidiEvent loopEvents[MAX_EVENTS];
int eventCount = 0;

bool recording = false;
bool recordingArmed = false;
bool playing = false;
bool loopRecorded = false;

// Clock tracking
volatile uint32_t clockPulses = 0;
uint32_t loopStartPulse = 0;
uint32_t loopLengthPulses = 0;
uint32_t recordStartPulse = 0;
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

    if (recordingArmed) {
      recordingArmed = false;
      recording = true;
      recordStartPulse = clockPulses;
      //loopStartPulse = clockPulses;
      eventCount = 0;
      Serial.println("Recording started cleanly at bar");
    }
  }

  if (playing && loopRecorded && loopLengthPulses > 0) {
    uint32_t relPulse = (clockPulses - loopStartPulse) % loopLengthPulses;
    for (int i = 0; i < eventCount; i++) {
      if (loopEvents[i].pulseOffset == relPulse) {
        MIDI.send(loopEvents[i].type, loopEvents[i].data1, loopEvents[i].data2, 1);
      }
    }
  }
}

void onStart() {
  clockPulses = 0;
  loopStartPulse = 0;
  Serial.println("Clock Start");
}

void onStop() {
  playing = false;
  recording = false;
  Serial.println("Clock Stop");
}

void onNoteOn(byte ch, byte note, byte vel) {
  if (recording && eventCount < MAX_EVENTS) {
    if (!firstEventCaptured) {
      loopStartPulse = clockPulses;
      recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    loopEvents[eventCount++] = {clockPulses - loopStartPulse, midi::NoteOn, note, vel};
  }
  else {
    Serial.println("Event buffer full!");
  }
}

void onNoteOff(byte ch, byte note, byte vel) {
  if (recording && eventCount < MAX_EVENTS) {
     if (!firstEventCaptured) {
      loopStartPulse = clockPulses;
      recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    loopEvents[eventCount++] = {clockPulses - loopStartPulse, midi::NoteOff, note, vel};
  }
  else {
    Serial.println("Event buffer full!");
  }
}

void onControlChange(byte ch, byte num, byte val) {
  if (recording && eventCount < MAX_EVENTS) {
     if (!firstEventCaptured) {
      loopStartPulse = clockPulses;
      recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    loopEvents[eventCount++] = {clockPulses - loopStartPulse, midi::ControlChange, num, val};
  }
  else {
    Serial.println("Event buffer full!");
  }
}

void onPitchBend(byte ch, int bend) {
  if (recording && eventCount < MAX_EVENTS) {
     if (!firstEventCaptured) {
      loopStartPulse = clockPulses;
      recordStartPulse = clockPulses;
      firstEventCaptured = true;
    }
    byte lsb = bend & 0x7F;
    byte msb = (bend >> 7) & 0x7F;
    loopEvents[eventCount++] = {clockPulses - loopStartPulse, midi::PitchBend, lsb, msb};
  }
  else {
    Serial.println("Event buffer full!");
  }
}


void onRecordButtonPressed() {
  if (!recording) {
    recordingArmed = true;
    Serial.println("Recording armed");
  }
}

void startRecording() {
  recording = true;
  playing = false;
  eventCount = 0;
  loopStartPulse = (clockPulses / pulsesPerBar) * pulsesPerBar; // quantize start
  lcd.setCursor(0, 1);
  lcd.print("Recording started");
  Serial.println("Recording started");
}

void stopRecording() {
   if (!recording) return;

  recording = false;
  // Quantize loop length to nearest bar from record start
  loopLengthPulses = ((clockPulses - recordStartPulse + pulsesPerBar - 1) / pulsesPerBar) * pulsesPerBar;

  // Set playback to begin from the start of the recording
  loopStartPulse = recordStartPulse;
  playing = true;
  loopRecorded = (eventCount > 0);
  Serial.print("Recording stopped. Loop length: ");
  Serial.println(loopLengthPulses);
}

void startPlayback() {
  if (loopRecorded && loopLengthPulses > 0) {
    playing = true;
    loopStartPulse = (clockPulses / pulsesPerBar) * pulsesPerBar; // quantize
    lcd.setCursor(0, 1);
    lcd.print("Playback started");
    Serial.println("Playback started");
  }
}

void stopPlayback() {
  playing = false;
  Serial.println("Playback stopped");
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
  memset(barGrid, '-', BAR_STEPS);
  barGrid[BAR_STEPS] = '\0';

  uint32_t startPulse = barIndex * pulsesPerBar;
  uint32_t endPulse = startPulse + pulsesPerBar;

  for (int i = 0; i < eventCount; i++) {
    uint32_t pulse = loopEvents[i].pulseOffset;
    if (pulse < startPulse || pulse >= endPulse) continue;

    uint32_t step = ((pulse - startPulse) * BAR_STEPS) / pulsesPerBar;
    if (step >= BAR_STEPS) step = BAR_STEPS - 1;

    switch (loopEvents[i].type) {
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
  uint32_t playPulse = (clockPulses - loopStartPulse) % loopLengthPulses;
  uint32_t playStep = ((playPulse % pulsesPerBar) * BAR_STEPS) / pulsesPerBar;
  if (playStep >= BAR_STEPS) playStep = BAR_STEPS - 1;
  barGrid[playStep] = '>';
}

void updateDisplay() {
  currentBar = ((clockPulses - loopStartPulse) / pulsesPerBar) % (loopLengthPulses / pulsesPerBar);
  buildBarGrid(currentBar);
  
  lcd.setCursor(0, 0);
  
  if (recording) lcd.print("Rec ");
  else if (recordingArmed) lcd.print("Arm ");
  else if (playing) lcd.print("Play");
  else lcd.print("Idle");
  
  overlayPlayhead();
  lcd.setCursor(0, 1);
  lcd.print(barGrid);

  // Show current bar
  uint32_t pulses = (clockPulses - loopStartPulse) % loopLengthPulses;
  int bar = pulses / pulsesPerBar;
  int beat = (pulses % pulsesPerBar) / (pulsesPerBar / 4);

  lcd.setCursor(6, 0);
  lcd.print(bar + 1);
  lcd.print(":");
  lcd.print(beat + 1);

  // Show event count
  lcd.setCursor(12, 0);
  lcd.print("*");
  lcd.print(eventCount);
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
  recButton.interval(10);  // 10 ms debounce

  playButton.attach(playButtonPin);
  playButton.interval(10); // 10 ms debounce

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // Print a message to the LCD.
  lcd.setCursor(0, 0);
  lcd.print("MIDI Looper Ready");
  delay(1000);
  lcd.clear();

  Serial.println("Teensy Real-Time MIDI Looper Ready.");
}

static unsigned long lastUpdate = 0;

void loop() {
  MIDI.read();

  recButton.update();
  playButton.update();

  if (recButton.fell()) {
    if (!recording) startRecording();
    else stopRecording();
  }

  if (playButton.fell()) {
    if (!playing) startPlayback();
    else stopPlayback();
  }
  
  if (millis() - lastUpdate > 100) {
    updateDisplay();
    lastUpdate = millis();
  }
}

