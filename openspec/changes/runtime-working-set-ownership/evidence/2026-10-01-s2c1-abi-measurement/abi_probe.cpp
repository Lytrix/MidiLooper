#include <cstdint>
#include <cstddef>
struct ProbeSerial {
  void print(const char*) {} void print(int) {} void print(unsigned) {} void print(long) {}
  void print(unsigned long) {} void println(const char*) {} void println(int) {} void println(unsigned) {}
  void println(long) {} void println(unsigned long) {}
};
static ProbeSerial Serial;
#include "MidiEvent.h"
#include "RuntimePlaybackView.h"
template<unsigned long N> struct Reveal;
Reveal<sizeof(MidiEvent)> sizeof_a;
Reveal<sizeof(PlaybackPassEventOrigin)> sizeof_b;
Reveal<sizeof(PlaybackBridgeRecord)> sizeof_c;
Reveal<sizeof(FixedPlaybackEventStorage)> sizeof_d;
Reveal<sizeof(RuntimePlaybackView)> sizeof_e;
Reveal<sizeof(RuntimePlaybackViewSet)> sizeof_f;
