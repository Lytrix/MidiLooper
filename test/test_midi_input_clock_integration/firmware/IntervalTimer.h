#pragma once
#include <cstdint>
// Timer hardware boundary only: no clock-source or tick state machine here.
namespace NativeIntervalTimer {
inline bool beginAllowed = true;
inline unsigned beginCalls = 0;
inline bool (*beginHook)() = nullptr;
}
class IntervalTimer {
 public:
  bool begin(void (*callback)(), uint32_t period) {
    ++NativeIntervalTimer::beginCalls;
    if (!NativeIntervalTimer::beginAllowed || (NativeIntervalTimer::beginHook && !NativeIntervalTimer::beginHook())) return false;
    callback_ = callback; period_ = period; return true;
  }
  void update(uint32_t period) { period_ = period; }
  void fire() const { if (callback_) callback_(); }
  uint32_t period() const { return period_; }
 private:
  void (*callback_)() = nullptr;
  uint32_t period_ = 0;
};
