// Native hardware boundary only: PRIMASK events, not receiver/clock algorithms.
#pragma once
#include <cstdint>
#include <vector>
namespace IngressMaskFixture {
enum class Kind : uint8_t { Ingress, Physical, Driver };
struct Event { Kind kind; bool enter; uint32_t before, after; };
inline uint32_t primask = 0;
inline std::vector<Event> events;
inline uint32_t enter(Kind kind) {
  const uint32_t saved = primask;
  primask = 1;
  events.push_back({kind, true, saved, primask});
  return saved;
}
inline void restore(Kind kind, uint32_t saved) {
  events.push_back({kind, false, primask, saved});
  primask = saved;
}
inline void reset(uint32_t incoming = 0) { primask = incoming; events.clear(); }
}
namespace MidiIngress { namespace NativeMask {
inline uint32_t enter() { return IngressMaskFixture::enter(IngressMaskFixture::Kind::Ingress); }
inline void restore(uint32_t saved) { IngressMaskFixture::restore(IngressMaskFixture::Kind::Ingress, saved); }
} }
