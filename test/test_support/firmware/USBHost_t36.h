#pragma once
#include "Arduino.h"
struct USBHost { void begin() {} void Task() {} };
struct USBHub { explicit USBHub(USBHost&) {} };
struct MIDIDevice_BigBuffer : NativeMidiPort {
  explicit MIDIDevice_BigBuffer(USBHost&) {}
  operator bool() const { return false; }
};
