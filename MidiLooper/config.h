// Event storage
#define MAX_TRACKS 4
#define MAX_EVENTS 1024

// Use these with the Teensy 3.5 & 3.6 & 4.1 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  11  // not actually used
#define SDCARD_SCK_PIN   13  // not actually used

// To store last used settings
struct LooperState {
  int activeTrack;
  byte midiChannels[MAX_TRACKS];
  bool playing[MAX_TRACKS];
};