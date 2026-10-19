#pragma once
#include "Arduino.h"
struct USBHost { void begin() {} void Task() {} };
struct USBHub { explicit USBHub(USBHost&) {} };
struct MIDIDevice_BigBuffer : NativeMidiPort {
  explicit MIDIDevice_BigBuffer(USBHost&) : NativeMidiPort(2) {}
  operator bool() const { return true; }
  using NoteCallback = void (*)(uint8_t, uint8_t, uint8_t);
  using TwoCallback = void (*)(uint8_t, uint8_t);
  using PitchCallback = void (*)(uint8_t, int);
  using RealtimeCallback = void (*)();
  void setHandleNoteOn(NoteCallback cb) { on_ = cb; }
  void setHandleNoteOff(NoteCallback cb) { off_ = cb; }
  void setHandleControlChange(NoteCallback cb) { cc_ = cb; }
  void setHandleProgramChange(TwoCallback cb) { program_ = cb; }
  void setHandlePitchChange(PitchCallback cb) { pitch_ = cb; }
  void setHandleAfterTouchChannel(TwoCallback cb) { pressure_ = cb; }
  void setHandleClock(RealtimeCallback cb) { clock_ = cb; }
  void setHandleStart(RealtimeCallback cb) { start_ = cb; }
  void setHandleStop(RealtimeCallback cb) { stop_ = cb; }
  void setHandleContinue(RealtimeCallback cb) { continue_ = cb; }
  bool read() {
    if (!NativeMidiPort::read()) return false;
    switch (getType()) {
      case 0x90: if (on_) on_(getChannel(), getData1(), getData2()); break;
      case 0x80: if (off_) off_(getChannel(), getData1(), getData2()); break;
      case 0xB0: if (cc_) cc_(getChannel(), getData1(), getData2()); break;
      case 0xC0: if (program_) program_(getChannel(), getData1()); break;
      case 0xD0: if (pressure_) pressure_(getChannel(), getData1()); break;
      case 0xE0: if (pitch_) pitch_(getChannel(), ((int(getData2()) << 7) | getData1()) - 8192); break;
      case 0xF8: if (clock_) clock_(); break;
      case 0xFA: if (start_) start_(); break;
      case 0xFC: if (stop_) stop_(); break;
      case 0xFB: if (continue_) continue_(); break;
    }
    return true;
  }
 private:
  NoteCallback on_ = nullptr, off_ = nullptr, cc_ = nullptr;
  TwoCallback program_ = nullptr, pressure_ = nullptr;
  PitchCallback pitch_ = nullptr;
  RealtimeCallback clock_ = nullptr, start_ = nullptr, stop_ = nullptr, continue_ = nullptr;
};
