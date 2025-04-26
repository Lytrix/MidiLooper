#include <MIDI.h>
#include <Bounce2.h>
#include <LiquidCrystal.h>

#define NUM_TRACKS 4
#define MAX_EVENTS 1024

#define TICKS_PER_QUARTER_NOTE 24
#define BEATS_PER_BAR 4
#define TICKS_PER_BAR (TICKS_PER_QUARTER_NOTE * BEATS_PER_BAR)

LiquidCrystal lcd(12, 11, 32, 31, 30, 29);

// Button Pins
const int trackSelectPin = 9;
const int recordControlPin = 10;
Bounce trackSelectButton = Bounce();
Bounce recordButton = Bounce();

// Clock
volatile uint32_t clockTicks = 0;
uint32_t loopLengthTicks = 0;
uint32_t loopStartTick = 0;
bool loopRunning = false;
bool clockRunning = false;
unsigned long lastLCDUpdate = 0;

// Track states
enum TrackState { IDLE, RECORDING, OVERDUBBING, PLAYBACK };
const char* stateLabels[] = {"-", "R", "O", "P"};

// Scheduled transition
bool pendingRecordChange = false;
TrackState nextTrackState = IDLE;
int pendingTrackIndex = -1;

bool pendingStopRecording = false;
int trackToStop = -1;

MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI);

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
    uint32_t loopPos = (clockTicks - loopStartTick) % loopLengthTicks;
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

  // Bar:Beat indicator
  uint32_t pos = clockTicks - loopStartTick;
  int bar = pos / TICKS_PER_BAR;
  int beat = (pos % TICKS_PER_BAR) / (TICKS_PER_BAR / BEATS_PER_BAR);

  lcd.setCursor(13, 1);
  lcd.print(bar + 1);
  lcd.print(":");
  lcd.print(beat + 1);
}

// === MIDI Clock Callbacks ===
void onClock() {
  if (clockRunning) clockTicks++;

  // Quantized recording state changes
  if (pendingRecordChange) {
    uint32_t pos = clockTicks - loopStartTick;
    // Start immediately if we're at the beginning of a loop and track 0 is starting the loop
    bool firstLoopStart = (loopLengthTicks == 0 && clockTicks == 0 && pendingTrackIndex == 0);

    if (firstLoopStart || pos % TICKS_PER_BAR == 0) {
      Track &track = tracks[pendingTrackIndex];

      switch (nextTrackState) {
        case RECORDING:
          track.state = RECORDING;
          track.eventCount = 0;
          track.muted = false;
          if (pendingTrackIndex == 0) {
            loopStartTick = clockTicks;
          }
          break;
        case OVERDUBBING:
          track.state = OVERDUBBING;
          break;
        case PLAYBACK:
          track.state = PLAYBACK;
          if (pendingTrackIndex == 0 && loopLengthTicks == 0) {
            loopLengthTicks = clockTicks - loopStartTick;
          }
          break;
        default:
          break;
      }

      updateLCD();
      pendingRecordChange = false;
    }
  }

  // Quantized stop of recording/overdubbing
  if (pendingStopRecording) {
    uint32_t pos = clockTicks - loopStartTick;
    if (pos % TICKS_PER_BAR == 0) {
      Track &track = tracks[trackToStop];
      if (track.state == RECORDING) {
        track.state = OVERDUBBING;
      } else if (track.state == OVERDUBBING) {
        track.state = PLAYBACK;
        if (trackToStop == 0 && loopLengthTicks == 0) {
          loopLengthTicks = clockTicks - loopStartTick;
        }
      }

      updateLCD();
      pendingStopRecording = false;
      trackToStop = -1;
    }
  }

  loopTracks();
}

void onStart() {
  clockTicks = 0;
  loopStartTick = 0;
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

  uint32_t time = loopLengthTicks > 0 ? (clockTicks - loopStartTick) % loopLengthTicks : (clockTicks - loopStartTick);

  if ((track.state == RECORDING || track.state == OVERDUBBING) && track.eventCount < MAX_EVENTS) {
    track.events[track.eventCount++] = { time, 0x90, note, velocity };
  }
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  Track &track = tracks[currentTrack];
  uint32_t time = loopLengthTicks > 0 ? (clockTicks - loopStartTick) % loopLengthTicks : (clockTicks - loopStartTick);

  if ((track.state == RECORDING || track.state == OVERDUBBING) && track.eventCount < MAX_EVENTS) {
    track.events[track.eventCount++] = { time, 0x80, note, velocity };
  }
}

// === Track Playback ===
void playTrack(Track &track) {
  if ((track.state != RECORDING && track.state != OVERDUBBING && track.state != PLAYBACK)
      || loopLengthTicks == 0 || track.muted) return;

  uint32_t loopPos = (clockTicks - loopStartTick) % loopLengthTicks;

  if (track.lastTickChecked != loopPos) {
    track.lastTickChecked = loopPos;

    if (track.state != RECORDING) {
      for (int i = 0; i < track.eventCount; i++) {
        if (track.events[i].timestamp == loopPos) {
          MIDI.send(track.events[i].type,
                    track.events[i].data1,
                    track.events[i].data2, track.midiChannel);
        }
      }
    }
  }
}

// === Button Actions ===
void handleTrackState(int i) {
  Track &track = tracks[i];

  // If muted, just toggle mute
  if (track.state == PLAYBACK && track.muted) {
    track.muted = !track.muted;
    updateLCD();
    return;
  }

  switch (track.state) {
    case IDLE:
      // Starting fresh recording: quantize at bar
      nextTrackState = RECORDING;
      pendingTrackIndex = i;
      pendingRecordChange = true;
      break;

    case RECORDING:
    case OVERDUBBING:
      // Stop recording or overdubbing (still quantized at bar)
      pendingStopRecording = true;
      trackToStop = i;
      break;

    case PLAYBACK:
      if (loopLengthTicks > 0) {
        // Immediate start of overdubbing without quantization
        track.state = OVERDUBBING;
        track.muted = false;
        track.lastTickChecked = (clockTicks - loopStartTick) - 1; // <--- Helps play function to  "catch up" and start from the next tick (that's why -1 is set).
        updateLCD();
      } else {
        // No loop length yet: treat it like first recording
        nextTrackState = RECORDING;
        pendingTrackIndex = i;
        pendingRecordChange = true;
      }
  break;
    break;
  }

  // Start internal clock if not running
  if (!clockRunning) {
    clockTicks = 0;
    loopStartTick = 0;
    clockRunning = true;
    loopRunning = true;
  }
}

// === Track Loop ===
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

  // Button actions
  if (trackSelectButton.fell()) {
    currentTrack = (currentTrack + 1) % NUM_TRACKS;
    updateLCD();
  }

  if (recordButton.fell()) {
    handleTrackState(currentTrack);
  }

  // LCD update
  if (millis() - lastLCDUpdate > 100) {
    updateLCD();
    lastLCDUpdate = millis();
  }
}
