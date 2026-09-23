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

/// The device's PROGMEM string macro. Native has no flash/RAM split, so it is a no-op, but the
/// production code that routes strings through flash (the RAM1-bank-boundary work) must still compile.
#ifndef PSTR
#define PSTR(s) s
#endif

/// The device's `Serial.print(value, HEX/DEC/BIN)` base selector. Native has no such enum, but
/// production diagnostics pass it, so the boundary supplies the same names.
enum NativePrintBase { DEC = 10, HEX = 16, OCT = 8, BIN = 2 };

struct NativeSerial {
  template<class... A> void printf(const char*, A...) {}
  /// Flash-string variant of `printf`. Native has one address space, so the `_P` form is the same call;
  /// production code that keeps a literal out of RAM1 uses it, and stubbing it here is what lets a
  /// native suite link that code at all.
  template<class... A> void printf_P(const char*, A...) {}
  void print() {}
  template <class T> void print(const T&) {}
  /// Device form `print(value, base)`: diagnostics print raw bytes as HEX.
  template <class T> void print(const T&, int) {}
  void println() {}
  template <class T> void println(const T&) {}
  template <class T> void println(const T&, int) {}
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
