#include <MIDI.h>
#include <Bounce2.h>
#include <LiquidCrystal.h>

// === Constants ===
#define NUM_TRACKS 4
#define MAX_EVENTS 1024

// LCD Pins (adjust as needed)
LiquidCrystal lcd(12, 11, 32, 31, 30, 29);

// Button Pins
const int trackSelectPin = 9;
const int recordControlPin = 10;
Bounce trackSelectButton = Bounce();
Bounce recordButton = Bounce();

// === Clock Timing ===
volatile uint32_t clockTicks = 0;
uint32_t loopLengthTicks = 0;
uint32_t loopStartTime = 0;
bool loopRunning = false;
bool clockRunning = false;

unsigned long lastLCDUpdate = 0;

// === Track States ===
enum TrackState { IDLE, RECORDING, OVERDUBBING, PLAYBACK };
const char* stateLabels[] = {"-", "R", "O", "P"};

// === MIDI Setup ===
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI);

// === Structures ===
struct MidiEvent {
  uint32_t timestamp;
  byte type;
  byte data1;
  byte data2;
};

struct Track {
  TrackState state = IDLE;
  MidiEvent events[MAX_EVENTS];
  int midiChannel = 1;
  int eventCount = 0;
  bool muted = false;

  // New for overdub filtering
  byte notesThisTick[32];
  int notesThisTickCount = 0;
  uint32_t lastTickChecked = -1;
};

Track tracks[NUM_TRACKS];
int currentTrack = 0;

// === LCD Update ===
void updateLCD() {
  lcd.setCursor(0, 0);
  if (!loopRunning || loopLengthTicks == 0) {
    lcd.print("Loop: <not set>   ");
  } else {
    uint32_t loopPos = (clockTicks - loopStartTime) % loopLengthTicks;
    int progressCols = (loopPos * 16) / loopLengthTicks;
    for (int i = 0; i < 16; i++) {
      if (i < progressCols) lcd.write(byte(255));
      else lcd.print(" ");
    }
  }

  lcd.setCursor(0, 1);
  for (int i = 0; i < NUM_TRACKS; i++) {
    if (i == currentTrack) lcd.print("[");
    else lcd.print(" ");

    if (tracks[i].muted) lcd.print("M");
    else lcd.print(stateLabels[tracks[i].state]);

    if (i == currentTrack) lcd.print("]");
    else lcd.print(" ");
  }
}

// === MIDI Clock Handlers ===
void onClock() {
  if (clockRunning) clockTicks++;
  loopTracks();
}

void onStart() {
  clockTicks = 0;
  loopStartTime = 0;
  clockRunning = true;
  loopRunning = true;
  updateLCD();
}

void onStop() {
  clockRunning = false;
  lcd.setCursor(0, 0);
  lcd.print("  >> STOPPED <<  ");
}

// === MIDI Input ===
void handleNoteOn(byte channel, byte note, byte velocity) {
  Track &track = tracks[currentTrack];
  if (velocity == 0) {
    handleNoteOff(channel, note, velocity);
    return;
  }

  // Overdub suppression
  if (track.state == OVERDUBBING) {
    for (int i = 0; i < track.notesThisTickCount; i++) {
      if (track.notesThisTick[i] == note) {
        return; // Suppress duplicate overdub
      }
    }
  }

  uint32_t time = (loopLengthTicks > 0) ? (clockTicks - loopStartTime) % loopLengthTicks : (clockTicks - loopStartTime);

  if ((track.state == RECORDING || track.state == OVERDUBBING) && track.eventCount < MAX_EVENTS) {
    track.events[track.eventCount++] = { time, 0x90, note, velocity };
  }
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  Track &track = tracks[currentTrack];
  uint32_t time = (loopLengthTicks > 0) ? (clockTicks - loopStartTime) % loopLengthTicks : (clockTicks - loopStartTime);

  if ((track.state == RECORDING || track.state == OVERDUBBING) && track.eventCount < MAX_EVENTS) {
    track.events[track.eventCount++] = { time, 0x80, note, velocity };
  }
}

// === Track Playback ===
void playTrack(Track &track) {
  if ((track.state != RECORDING && track.state != OVERDUBBING && track.state != PLAYBACK) || loopLengthTicks == 0 || track.muted) return;

  uint32_t loopPos = (clockTicks - loopStartTime) % loopLengthTicks;

  if (track.lastTickChecked != loopPos) {
    track.notesThisTickCount = 0;
    track.lastTickChecked = loopPos;
  }

  if (track.state == OVERDUBBING || track.state == PLAYBACK && loopLengthTicks > 0) {
    for (int i = 0; i < track.eventCount; i++) {
      if (track.events[i].timestamp == loopPos) {
        MIDI.send(track.events[i].type,
                  track.events[i].data1,
                  track.events[i].data2, track.midiChannel);

        if (track.notesThisTickCount < 32 && track.events[i].type == 0x90 && track.events[i].data2 > 0) {
          track.notesThisTick[track.notesThisTickCount++] = track.events[i].data1;
        }
      }
    }
  }
}

// === Button Logic ===
void handleTrackState(int i) {
  switch (tracks[i].state) {
    case IDLE:
      tracks[i].state = RECORDING;
      tracks[i].eventCount = 0;
      tracks[i].muted = false;
      if (!loopRunning) {
        loopStartTime = clockTicks;
        loopRunning = true;
      }
      break;
    case RECORDING:
      if (i == 0) {
        loopLengthTicks = clockTicks - loopStartTime;
      }
      tracks[i].state = OVERDUBBING;
      break;
    case OVERDUBBING:
      tracks[i].state = PLAYBACK;
      break;
    case PLAYBACK:
      tracks[i].muted = !tracks[i].muted;
      break;
  }
}

// === Track Loop Function ===
void loopTracks() {
  for (int i = 0; i < NUM_TRACKS; i++) {
    playTrack(tracks[i]);
  }
}

// === Setup ===
void setup() {
  Serial.begin(115200);

  pinMode(trackSelectPin, INPUT_PULLUP);
  pinMode(recordControlPin, INPUT_PULLUP);

  trackSelectButton.attach(trackSelectPin);
  trackSelectButton.interval(10);

  recordButton.attach(recordControlPin);
  recordButton.interval(10);

  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("MIDI Looper Ready");
  delay(1000);
  lcd.clear();

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleClock(onClock);
  MIDI.setHandleStart(onStart);
  MIDI.setHandleStop(onStop);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
}

// === Main Loop ===
void loop() {
  MIDI.read();

  trackSelectButton.update();
  recordButton.update();

  if (trackSelectButton.fell()) {
    currentTrack = (currentTrack + 1) % NUM_TRACKS;
    updateLCD();
  }

  if (recordButton.fell()) {
    handleTrackState(currentTrack);
    updateLCD();
  }

    if (millis() - lastLCDUpdate > 100) {
      updateLCD();
      lastLCDUpdate = millis();
    }
}
