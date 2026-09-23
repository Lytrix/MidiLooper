#pragma once
// Marks the native Arduino boundary stub. A production file that carries its own native fallback under
// `PIO_UNIT_TEST_NATIVE` must defer to this stub when a suite links both, so the two do not define the same
// inline function in one translation unit (see src/Loop/LoopPendingNoteChange.cpp).
#define HARNESS_ARDUINO_STUB 1
// Native hardware boundary only. Domain owners use their production declarations.
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <algorithm>
using byte = uint8_t;
#define FLASHMEM
#define PROGMEM
#define DMAMEM
#define F(s) s
inline uint32_t micros() { return 0; }
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}

struct NativeSerial {
  template<class... A> void printf(const char*, A...) {}
  void println() {}
  template <class T> void println(const T&) {}
  void flush() {}
};
inline NativeSerial Serial;
class File;
inline void noInterrupts() {}
inline void interrupts() {}

#define EXTMEM
inline void yield() {}
struct NativeMidiPort {
  static constexpr uint8_t Clock = 0xF8, Start = 0xFA, Continue = 0xFB, Stop = 0xFC;
  template<class... A> void begin(A...) {}
  bool read() { return false; }
  uint8_t getType() const { return 0; }
  uint8_t getChannel() const { return 0; }
  uint8_t getData1() const { return 0; }
  uint8_t getData2() const { return 0; }
  template<class... A> void sendAfterTouch(A...) {}
  template<class... A> void sendControlChange(A...) {}
  template<class... A> void sendNoteOff(A...) {}
  template<class... A> void sendNoteOn(A...) {}
  template<class... A> void sendPitchBend(A...) {}
  template<class... A> void sendProgramChange(A...) {}
  template<class... A> void sendRealTime(A...) {}
  template<class... A> void sendSongPosition(A...) {}
  template<class... A> void sendSongSelect(A...) {}
  template<class... A> void sendSysEx(A...) {}
  template<class... A> void setHandleNoteOn(A...) {}
  template<class... A> void setHandleNoteOff(A...) {}
  template<class... A> void setHandleControlChange(A...) {}
  template<class... A> void setHandleProgramChange(A...) {}
  template<class... A> void setHandlePitchChange(A...) {}
  template<class... A> void setHandleAfterTouchChannel(A...) {}
  template<class... A> void setHandleClock(A...) {}
  template<class... A> void setHandleStart(A...) {}
  template<class... A> void setHandleStop(A...) {}
  template<class... A> void setHandleContinue(A...) {}
};
inline NativeMidiPort usbMIDI, Serial8;
#define MIDI_CREATE_INSTANCE(Type, Port, Name) NativeMidiPort Name
