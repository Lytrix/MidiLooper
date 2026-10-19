#pragma once
// Suite-local physical-port boundary; retain common time/allocator/serial fakes.
#define NativeMidiPort SharedInboxMidiPort
#define usbMIDI sharedBoundaryUsbMIDI
#define Serial8 sharedBoundarySerial8
#include "../../test_support/firmware/Arduino.h"
#undef NativeMidiPort
#undef usbMIDI
#undef Serial8
#undef MIDI_CREATE_INSTANCE

#include "Utils/MidiIngressReceipt.h"
#if MIDI_INGRESS_RECEIPTS_ENABLED
inline uint64_t inputReceiveTime = 1000;
inline PhysicalClockStamp inputReceiveStamp() { return {inputReceiveTime, 17}; }
#endif

struct NativeMidiPort : SharedInboxMidiPort {
  static std::vector<NativeMidiMessage>& routeInbox(uint8_t route) {
    static std::vector<NativeMidiMessage> queues[3];
    return queues[route];
  }
#if MIDI_INGRESS_RECEIPTS_ENABLED
  static std::vector<uint32_t>& receiptSlots(uint8_t route) {
    static std::vector<uint32_t> slots[3];
    return slots[route];
  }
  static void receive(uint8_t route, NativeMidiMessage message) {
    static uint32_t next[3]{};
    const uint32_t capacity = route == 0 ? 6 : route == 1 ? 64 : 400;
    const auto slot = next[route]++ % capacity;
    const auto* owner = MidiIngress::registeredOwner(route);
    midi_ingress_packet(route, owner, capacity, 0);
    midi_ingress_publish(route, owner, slot, 0, 1);
    receiptSlots(route).push_back(slot);
    routeInbox(route).push_back(message);
  }
#endif
  explicit NativeMidiPort(uint8_t route = 2) : inbox(routeInbox(route)), route_(route) {}
  bool read() {
#if MIDI_INGRESS_RECEIPTS_ENABLED
    const auto* owner = MidiIngress::registeredOwner(route_);
    midi_ingress_clear_last(route_, owner);
#endif
    if (inbox.empty()) return false;
#if MIDI_INGRESS_RECEIPTS_ENABLED
    auto& slots = receiptSlots(route_);
    if (!slots.empty()) {
      midi_ingress_consume(route_, owner, slots.front(), 0);
      slots.erase(slots.begin());
    }
#endif
    const auto message = inbox.front();
    inbox.erase(inbox.begin());
    lastType = message.type; lastChannel = message.channel;
    lastData1 = message.data1; lastData2 = message.data2;
    return true;
  }
  void pushMessage(uint8_t type, uint8_t channel, uint8_t data1, uint8_t data2) {
#if MIDI_INGRESS_RECEIPTS_ENABLED
    receive(route_, {type, channel, data1, data2});
#else
    inbox.push_back({type, channel, data1, data2});
#endif
  }
  void clearInbox() { inbox.clear(); }
  std::vector<NativeMidiMessage>& inbox;
  uint8_t route_;
};
inline NativeMidiPort usbMIDI(0), Serial8(1);
#define MIDI_CREATE_INSTANCE(Type, Port, Name) NativeMidiPort Name(1)
