#ifndef MIDIHANDLER_H
#define MIDIHANDLER_H

#include <Arduino.h>

enum InputSource {
  SOURCE_USB,
  SOURCE_SERIAL
};

class MidiHandler {
public:
  MidiHandler();

  void setup();
  void handleInput();

  void handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source);

  void sendNoteOn(byte channel, byte note, byte velocity);
  void sendNoteOff(byte channel, byte note, byte velocity);
  void sendControlChange(byte channel, byte control, byte value);
  void sendPitchBend(byte channel, int value);
  void sendAfterTouch(byte channel, byte pressure);

  void sendClock();
  void sendStart();
  void sendStop();
  void sendContinueMIDI();

  void setOutputUSB(bool enable);
  void setOutputSerial(bool enable);

private:
  bool outputUSB;
  bool outputSerial;

  // --- missing private declarations (now added) ---
  void handleNoteOn(byte channel, byte note, byte velocity, uint32_t currentTick);
  void handleNoteOff(byte channel, byte note, byte velocity, uint32_t currentTick);
  void handleControlChange(byte channel, byte control, byte value, uint32_t currentTick);
  void handlePitchBend(byte channel, int pitchValue);
  void handleAfterTouch(byte channel, byte pressure);
  void handleMidiStart();
  void handleMidiStop();
  void handleMidiContinue();
};

extern MidiHandler midiHandler;

#endif
