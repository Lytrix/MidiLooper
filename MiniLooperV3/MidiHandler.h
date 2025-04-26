// MidiHandler.h
#ifndef MIDIHANDLER_H
#define MIDIHANDLER_H

#include <Arduino.h>

// Midi Input
enum InputSource {
  SOURCE_USB,
  SOURCE_SERIAL
};

void setupMidi();
void handleMidiInput();

// Midi Output
extern bool outputUSB;
extern bool outputSerial;

void sendNoteOn(byte channel, byte note, byte velocity);
void sendNoteOff(byte channel, byte note, byte velocity);
void sendControlChange(byte channel, byte control, byte value);
void sendPitchBend(byte channel, int value);
void sendAfterTouch(byte channel, byte pressure);

void sendClock();
void sendStart();
void sendStop();
void sendContinueMIDI();

#endif
