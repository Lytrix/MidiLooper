#include <LiquidCrystal.h>
#include <SD.h> 
#include <MIDI.h>
#include "Button.h"

#define NOTE_OFF_EVENT 0x80 
#define NOTE_ON_EVENT 0x90 
#define CONTROL_CHANGE_EVENT 0xB0 
#define PITCH_BEND_EVENT 0xE0 


MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI);

const int buttonRecordPin = 9;
const int buttonStopPin = 10;

bool lastRecordState = HIGH;
bool lastStopState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;


unsigned long t=0;

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 32, d5 = 31, d6 = 30, d7 = 29;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

const int MAX_EVENTS = 512;

enum MidiType {
  NoteOn,
  NoteOff,
  ControlChange,
  PitchBend
};

struct MidiEvent {
  MidiType type;
  byte data1;
  int data2;
  uint32_t timestamp;
};

MidiEvent events[MAX_EVENTS];
int eventCount = 0;

bool isRecording = false;
bool isPlaying = false;

uint32_t recordStartTime = 0;
uint32_t loopLength = 0;
uint32_t loopStartTime = 0;
int playbackIndex = 0;

void recordEvent(MidiType type, byte d1, int d2) {
  if (eventCount >= MAX_EVENTS) return;
  events[eventCount].type = type;
  events[eventCount].data1 = d1;
  events[eventCount].data2 = d2;
  events[eventCount].timestamp = millis() - recordStartTime;
  eventCount++;
}

void handleNoteOn(byte ch, byte note, byte vel) {
  if (isRecording) recordEvent(NoteOn, note, vel);
}

void handleNoteOff(byte ch, byte note, byte vel) {
  if (isRecording) recordEvent(NoteOff, note, vel);
}

void handleCC(byte cc, byte val) {
  if (isRecording) recordEvent(ControlChange, cc, val);
}

void handlePitchBend(int bend) {
  if (isRecording) recordEvent(PitchBend, 0, bend);
}

void startRecording() {
  Serial.println("Recording...");
  isRecording = true;
  isPlaying = false;
  eventCount = 0;
  recordStartTime = millis();
}

void stopRecordingAndStartLoop() {
  isRecording = false;
  if (eventCount > 0) {
    loopLength = events[eventCount - 1].timestamp;
    loopStartTime = millis();
    playbackIndex = 0;
    isPlaying = true;
    Serial.print("Loop recorded. Length: ");
    Serial.print(loopLength);
    Serial.println(" ms");
  } else {
    Serial.println("No events recorded.");
  }
}

uint32_t lastLoopTime = 0;

void checkPlayback() {
  if (!isPlaying || eventCount == 0) return;

  uint32_t currentTime = millis();
  uint32_t loopTime = (currentTime - loopStartTime) % loopLength;

  // Detect loop restart
  if (loopTime < lastLoopTime) {
    playbackIndex = 0;  // Restart event playback
  }
  lastLoopTime = loopTime;

  // Play events up to current loop time
  while (playbackIndex < eventCount && events[playbackIndex].timestamp <= loopTime) {
    const MidiEvent& ev = events[playbackIndex];
    switch (ev.type) {
      case NoteOn:
        MIDI.sendNoteOn(ev.data1, ev.data2, 1);
        break;
      case NoteOff:
        MIDI.sendNoteOff(ev.data1, ev.data2, 1);
        break;
      case ControlChange:
        MIDI.sendControlChange(ev.data1, ev.data2, 1);
        break;
      case PitchBend:
        MIDI.sendPitchBend(ev.data2, 1);
        break;
    }
    playbackIndex++;
  }
}

void stopLoop() {
  isPlaying = false;
  playbackIndex = 0;
  Serial.println("Loop stopped.");

  // Optional: All Notes Off on channel 1
  for (int cc = 123; cc <= 127; cc++) {
    MIDI.sendControlChange(cc, 0, 1);
  }
}

void startLoop() {
  if (eventCount == 0 || loopLength == 0) return;

  isPlaying = true;
  playbackIndex = 0;
  loopStartTime = millis();
  lastLoopTime = 0;
  Serial.println("Loop playback started.");
}

void setup() {
  Serial.begin(57600);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleCC);
  MIDI.setHandlePitchBend(handlePitchBend);

  Serial.println("Teensy Real-Time MIDI Looper Ready.");
  Serial.println("Commands: 'r' to record, 's' to stop+loop");
	
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // Print a message to the LCD.
 // lcd.print("hello, world!");
  
  pinMode(buttonRecordPin, INPUT_PULLUP);
  pinMode(buttonStopPin, INPUT_PULLUP);

}
int type, note, velocity, channel, d1, d2;
void loop() {
  MIDI.read();
  checkPlayback();

  // Debounced read
  bool currentRecord = digitalRead(buttonRecordPin);
  bool currentPlayStop = digitalRead(buttonStopPin); // shared button now

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentRecord != lastRecordState) {
      lastDebounceTime = millis();
      if (currentRecord == LOW) {
        if (!isRecording && !isPlaying) {
          startRecording();
        } else if (isRecording) {
          stopRecordingAndStartLoop();
        }
      }
    }

    if (currentPlayStop != lastStopState) {
      lastDebounceTime = millis();
      if (currentPlayStop == LOW) {
        if (isPlaying) {
          stopLoop();
        } else if (!isRecording && eventCount > 0 && loopLength > 0) {
          startLoop();
        }
      }
    }
  }

  lastRecordState = currentRecord;
  lastStopState = currentPlayStop;


  // if (Serial.available()) {
  //   char cmd = Serial.read();
  //   if (cmd == 'r') {
  //     startRecording();
  //   } else if (cmd == 's') {
  //     stopRecordingAndStartLoop();
  //   } else if (cmd == 'x') {
  //     stopLoop();
  //   }
  // }
  if (MIDI.read()){
    byte type = MIDI.getType();
    switch (type) {
      case midi::NoteOn:
        note = MIDI.getData1();
        velocity = MIDI.getData2();
        channel = MIDI.getChannel();
        if (velocity > 0) {
          lcd.setCursor(0, 0);
          lcd.print("CHN");
          lcd.setCursor(0, 1);
          if (channel < 10) {
              lcd.print(' ');
          }
          lcd.print(' ');
          lcd.print(channel);
          lcd.setCursor(4, 0);
          lcd.print("KEY");
          lcd.setCursor(4, 1);
          if (note < 100) {
              lcd.print(' ');
          }
          if (note < 10) {
              lcd.print(' ');
          }
          lcd.print( note);

          lcd.setCursor(8, 0);
          lcd.print("VEL");
          lcd.setCursor(8, 1);
          if (velocity < 100) {
             lcd.print(' ');
          }
          if (velocity < 10) {
              lcd.print(' ');
          }
          lcd.print(velocity);
          //Serial.println(String("Note On:  ch=") + channel + ", note=" + note + ", velocity=" + velocity);
        } else {
          lcd.setCursor(0, 1);
          lcd.print("            ");
          //Serial.println(String("Note Off: ch=") + channel + ", note=" + note);
        }
        break;
      case midi::NoteOff:
        note = MIDI.getData1();
        velocity = MIDI.getData2();
        channel = MIDI.getChannel();
        lcd.setCursor(0, 1);
        lcd.print("            ");
        //Serial.println(String("Note Off: ch=") + channel + ", note=" + note + ", velocity=" + velocity);
        break;
      default:
        d1 = MIDI.getData1();
        d2 = MIDI.getData2();
        
       // Serial.println(String("Message, type=") + type + ", data = " + d1 + " " + d2);
    }
    t = millis();
  }

  // if (millis() - t > 10000) {
  //   t += 10000;
  //  // Serial.println("(inactivity)");
  // }
  // set the cursor to column 0, line 1
  // (note: line 1 is the second row, since counting begins with 0):
  //  if (button1.released()) {
  //   lcd.setCursor(12, 0);
  //   lcd.print(" ");
  //   //Serial.println("FUNC Button is not pressed...");
  // }
  // if (button1.pressed()) {
  //    lcd.setCursor(12, 0);
  //    lcd.print("F");
  //    startRecording();
  //    //Serial.println("FUNC Button pressed!!!");
  // }
 
  // if (button2.released()) {
  //   lcd.setCursor(12, 1);
  //   lcd.print(" ");
  //   //Serial.println("REC Button is not pressed...");
  // } 
  // if (button2.pressed()) {
  //   lcd.setCursor(12, 1);
  //   lcd.print("R");
  //   stopRecordingAndStartLoop();
  //    //Serial.println("REC Button pressed!!!");
  // }

  //lcd.setCursor(0, 1);
  // print the number of seconds since reset:
  //lcd.print(millis() / 1000);
}

