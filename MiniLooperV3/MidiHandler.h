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

  // --- Initialization ---
  void setup();

  // --- Input Handling ---
  void handleMidiInput();
  void handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source);

  // --- MIDI Output ---
  void sendNoteOn(byte channel, byte note, byte velocity);
  void sendNoteOff(byte channel, byte note, byte velocity);
  void sendControlChange(byte channel, byte control, byte value);
  void sendPitchBend(byte channel, int value);
  void sendAfterTouch(byte channel, byte pressure);
  void sendProgramChange(byte channel, byte program);

  // --- Clock / Transport Output ---
  void sendClock();
  void sendStart();
  void sendStop();
  void sendContinueMIDI();

  // --- Output Routing ---
  void setOutputUSB(bool enable);
  void setOutputSerial(bool enable);

private:
  bool outputUSB = true;
  bool outputSerial = true;

  // --- Message Handlers ---
  void handleNoteOn(byte channel, byte note, byte velocity, uint32_t tickNow);
  void handleNoteOff(byte channel, byte note, byte velocity, uint32_t tickNow);
  void handleControlChange(byte channel, byte control, byte value, uint32_t tickNow);
  void handlePitchBend(byte channel, int pitchValue, uint32_t tickNow);
  void handleAfterTouch(byte channel, byte pressure, uint32_t tickNow);
  void handleProgramChange(byte channel, byte program, uint32_t tickNow);
  void handleMidiStart();
  void handleMidiStop();
  void handleMidiContinue();
};

extern MidiHandler midiHandler;

#endif // MIDIHANDLER_H