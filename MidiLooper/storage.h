#include "config.h"

// ---------------------------------------------------- 
// Save Looper State to SD
// ----------------------------------------------------



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
