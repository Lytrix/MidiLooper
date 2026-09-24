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
#include <vector>
using byte = uint8_t;
#define FLASHMEM
#define PROGMEM
#define DMAMEM
#define F(s) s
// Controllable native clock. The device's `millis()` advances on its own; on the host it must be driven
// by the fixture, or no bounded wall-clock wait (A3.5's post-load commit holdoff) can ever expire and
// every timing policy would be untestable. `nativeMillisNow()` is never reset between cases, so a
// monotonic step is always forward; a suite that needs a specific instant calls `nativeMillisSet`.
inline uint32_t& nativeMillisNow() {
  static uint32_t now = 1000u;
  return now;
}
inline uint32_t millis() { return nativeMillisNow(); }
/// `micros()` is derived from the same clock, so the drain's wall-clock cap and its holdoff wait are
/// driven by one fixture-controlled instant rather than a frozen zero.
inline uint32_t micros() { return nativeMillisNow() * 1000u; }
/// Advance the native clock. Returns the new value.
inline uint32_t nativeMillisAdvance(uint32_t deltaMs) {
  nativeMillisNow() += deltaMs;
  return nativeMillisNow();
}
inline void nativeMillisSet(uint32_t nowMs) { nativeMillisNow() = nowMs; }
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
/// `yield()` is the same host boundary: it lets the (mocked) clock advance by the smallest step, which is
/// what the device's scheduler does during a bounded yield and what makes a `do { …; yield(); } while
/// (millis() < deadline)` service loop terminate on the host instead of spinning forever. A suite that needs a frozen clock keeps one by not yielding.
inline void yield() { nativeMillisAdvance(1u); }
/// One inbound MIDI message a fixture can queue for `MidiHandler::handleMidiInput()` to consume. Before
/// A3.5 the stub's `read()` always returned false, so no native suite could observe that MIDI input was
/// serviced at all -- which is exactly the claim A3.5's holdoff wait has to prove.
struct NativeMidiMessage {
  uint8_t type = 0;
  uint8_t channel = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
};

struct NativeMidiPort {
  static constexpr uint8_t Clock = 0xF8, Start = 0xFA, Continue = 0xFB, Stop = 0xFC;
  template<class... A> void begin(A...) {}
  bool read() {
    if (inbox.empty()) {
      return false;
    }
    const NativeMidiMessage message = inbox.front();
    inbox.erase(inbox.begin());
    lastType = message.type;
    lastChannel = message.channel;
    lastData1 = message.data1;
    lastData2 = message.data2;
    return true;
  }
  uint8_t getType() const { return lastType; }
  uint8_t getChannel() const { return lastChannel; }
  uint8_t getData1() const { return lastData1; }
  uint8_t getData2() const { return lastData2; }

  /// Fixture injection: queue one inbound message, consumed in FIFO order by the next `read()`.
  void pushMessage(uint8_t type, uint8_t channel, uint8_t data1, uint8_t data2) {
    inbox.push_back(NativeMidiMessage{type, channel, data1, data2});
  }
  void clearInbox() { inbox.clear(); }
  /// The inbound queue is **shared by every instance**. `MIDI_CREATE_INSTANCE` expands to a *definition*
  /// (`NativeMidiPort Name;`), not an extern reference, so a TU gets its own copy -- and on hardware those
  /// copies are all the same physical port. A per-instance queue would let a fixture push to one object
  /// while `MidiHandler` reads another, which is exactly the false negative this seam exists to avoid.
  /// Do not clear it wholesale; a fixture clears it explicitly via `clearInbox()`.
  static std::vector<NativeMidiMessage>& sharedInbox() {
    static std::vector<NativeMidiMessage> inbox;
    return inbox;
  }
  std::vector<NativeMidiMessage>& inbox = sharedInbox();
  uint8_t lastType = 0;
  uint8_t lastChannel = 0;
  uint8_t lastData1 = 0;
  uint8_t lastData2 = 0;
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
