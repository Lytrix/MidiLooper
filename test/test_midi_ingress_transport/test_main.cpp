// Executes pinned upstream bodies, not models of the receive/dequeue algorithms.
#define MIDI_INGRESS_MASK_QUALIFICATION 1
#define MIDI_INGRESS_RECEIPTS_ENABLED 1
#ifndef PIO_UNIT_TEST_NATIVE
#define PIO_UNIT_TEST_NATIVE 1
#endif
#include <cstdio>
#include <cstdlib>
#ifdef MIDI_INGRESS_STANDALONE
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
#define RUN_TEST(x) do { x(); std::printf("PASS %s\n", #x); } while (0)
#else
#include <unity.h>
#define CHECK(x) TEST_ASSERT_TRUE(x)
#endif
#include "Utils/MidiIngressReceipt.h"
#include "../test_support/MidiIngressMaskFixture.h"
#include "../../src/Utils/MidiIngressReceipt.cpp"
#include "platform_fakes.h"
uint32_t ingressFakeMicros = 0;
static uint64_t receiveTime = 0;
static unsigned samplerCalls = 0;
static PhysicalClockStamp sample() { CHECK(IngressMaskFixture::primask == 1); ++samplerCalls; return {receiveTime, 7}; }
void setUp() {}
void tearDown() {}

static void observeAt(uint64_t time) {
    receiveTime = time; ingressFakeMicros = uint32_t(time);
}
static void setup(din::HardwareSerialIMXRT* serial = nullptr, host::MIDIDeviceBase* device = nullptr) {
    IngressMaskFixture::reset(); samplerCalls = 0;
    MidiIngress::setup(serial, device);
    MidiIngress::setClockSampler(sample);
    observeAt(100);
    usb::reset(); host::requeues = 0;
}
static void paired(const MidiIngressReceipt& r, uint64_t time, unsigned offset) {
    CHECK(r.associated()); CHECK(r.current()); CHECK(r.independentObservation());
    CHECK(r.observedUs == time); CHECK(r.rawObservedUs == uint32_t(time));
    CHECK(r.clockEra == 7); CHECK(r.eventOffset == offset);
}
static void usbReceive(unsigned slot, std::initializer_list<uint32_t> words, unsigned extraBytes = 0, bool missing = false) {
    unsigned bytes = unsigned(words.size()) * 4;
    std::memcpy(usb::rx_buffer + slot * usb::MIDI_RX_SIZE_480, words.begin(), bytes);
    usb::transfer_t t{(usb::rx_packet_size - bytes - extraBytes) << 16, slot};
    if (missing) usb_missing::rx_event(&t); else usb::rx_event(&t);
}
static void hostReceive(host::MIDIDeviceBase& device, std::initializer_list<uint32_t> words) {
    CHECK(words.size() <= 16);
    std::memcpy(device.rx_buffer, words.begin(), words.size() * 4);
    host::Transfer_t transfer{64, {uint32_t(64 - words.size() * 4) << 16}, device.rx_buffer};
    device.rx_data(&transfer);
}
static uint32_t note(unsigned pitch) { return (90u << 24) | (pitch << 16) | 0x9009u; }

static void test_usb_partial_packet_and_packet_reuse() {
    setup(); observeAt(111);
    usbReceive(0, {note(60), 0x0000f80f, note(61)}, 2);
    CHECK(usb::rx_available == 12); CHECK(usb::rx_count[0] == 12);
    observeAt(999); // service must not restamp the receive observation
    CHECK(usb::usb_midi_read_message() == note(60));
    auto first = MidiIngress::lastReceipt(0); paired(first, 111, 0);
    CHECK(usb::requeues == 0); CHECK(usb::rx_index[0] == 4);
    CHECK(usb::usb_midi_read_message() == 0x0000f80f);
    auto second = MidiIngress::lastReceipt(0); paired(second, 111, 1);
    CHECK(second.receiveSequence == first.receiveSequence);
    CHECK(usb::usb_midi_read_message() == note(61)); paired(MidiIngress::lastReceipt(0), 111, 2);
    CHECK(usb::requeues == 1); CHECK(usb::rx_available == 0);
    CHECK(usb::usb_midi_read_message() == 0); CHECK(!MidiIngress::lastReceipt(0).associated());
    observeAt(222); usbReceive(0, {note(60)});
    CHECK(usb::usb_midi_read_message() == note(60));
    auto reused = MidiIngress::lastReceipt(0); paired(reused, 222, 0);
    CHECK(reused.receiveSequence > first.receiveSequence);
}
static void test_usb_transfer_fifo_wrap_and_zero_length() {
    setup();
    for (unsigned round = 0; round < 4; ++round) {
        for (unsigned slot = 0; slot < 6; ++slot) {
            observeAt(1000 + round * 10 + slot); usbReceive(slot, {note(60 + slot)});
        }
        for (unsigned slot = 0; slot < 6; ++slot) {
            CHECK(usb::usb_midi_read_message() == note(60 + slot));
            paired(MidiIngress::lastReceipt(0), 1000 + round * 10 + slot, 0);
        }
    }
    auto requeues = usb::requeues;
    usbReceive(0, {}); CHECK(usb::requeues == requeues + 1);
    CHECK(usb::usb_midi_read_message() == 0); CHECK(!MidiIngress::lastReceipt(0).associated());
}
static void test_usb_reset_keeps_payload_but_invalidates_receipt() {
    setup(); usbReceive(0, {note(60), note(61)});
    CHECK(usb::usb_midi_read_message() == note(60));
    auto first = MidiIngress::lastReceipt(0);
    midi_ingress_reset(0, nullptr, 6);
    CHECK(!MidiIngress::currentReceipt(0, first));
    CHECK(usb::usb_midi_read_message() == note(61));
    auto old = MidiIngress::lastReceipt(0); CHECK(old.associated()); CHECK(!old.current());
}
static void test_usb_missing_hook_preserves_payload_refuses_pairing() {
    setup(); observeAt(111); usbReceive(0, {note(60)}, 0, true);
    // Original receive body + hooked dequeue: payload still works; metadata must not.
    CHECK(usb::usb_midi_read_message() == note(60));
    CHECK(!MidiIngress::lastReceipt(0).associated());
    CHECK(!MidiIngress::lastReceipt(0).independentObservation());
    observeAt(222); usbReceive(0, {note(60)});
    CHECK(usb::usb_midi_read_message() == note(60)); paired(MidiIngress::lastReceipt(0), 222, 0);
}
static void test_host_fifo_zero_words_and_empty_clear() {
    host::MIDIDeviceBase device; setup(nullptr, &device);
    observeAt(111); hostReceive(device, {note(60), 0, 0x0000f80f, note(61)});
    observeAt(222); hostReceive(device, {note(62)});
    observeAt(999);
    CHECK(device.read()); CHECK(device.msg_type == 0x90); CHECK(device.msg_data1 == 60);
    auto first = MidiIngress::lastReceipt(2); paired(first, 111, 0);
    CHECK(device.read()); CHECK(device.msg_type == 0xf8); paired(MidiIngress::lastReceipt(2), 111, 2);
    CHECK(MidiIngress::lastReceipt(2).receiveSequence == first.receiveSequence);
    CHECK(device.read()); CHECK(device.msg_data1 == 61); paired(MidiIngress::lastReceipt(2), 111, 3);
    CHECK(device.read()); CHECK(device.msg_data1 == 62); paired(MidiIngress::lastReceipt(2), 222, 0);
    CHECK(MidiIngress::lastReceipt(2).receiveSequence > first.receiveSequence);
    CHECK(!device.read()); CHECK(!MidiIngress::lastReceipt(2).associated());
}
static void test_host_backpressure_resumes_and_ring_wraps() {
    host::MIDIDeviceBase device; setup(nullptr, &device);
    // 24 legal packets fill 384 of 399 usable slots; no artificial queue writes.
    for (unsigned packet = 0; packet < 24; ++packet) {
        observeAt(1000 + packet);
        hostReceive(device, {note(0), note(1), note(2), note(3), note(4), note(5), note(6), note(7),
                            note(8), note(9), note(10), note(11), note(12), note(13), note(14), note(15)});
    }
    CHECK(!device.rx_packet_queued); CHECK(host::requeues == 23);
    observeAt(9999);
    for (unsigned index = 0; index < 384; ++index) {
        CHECK(device.read()); CHECK(device.msg_data1 == index % 16);
        paired(MidiIngress::lastReceipt(2), 1000 + index / 16, index % 16);
        if (index == 0) { CHECK(device.rx_packet_queued); CHECK(host::requeues == 24); }
    }
    CHECK(!device.read());
    observeAt(2000);
    hostReceive(device, {note(0), note(1), note(2), note(3), note(4), note(5), note(6), note(7),
                        note(8), note(9), note(10), note(11), note(12), note(13), note(14), note(15)});
    CHECK(device.rx_head == 0);
    for (unsigned index = 0; index < 16; ++index) {
        CHECK(device.read()); CHECK(device.msg_data1 == index); paired(MidiIngress::lastReceipt(2), 2000, index);
    }
}
static void test_host_partial_sysex_realtime_metadata_per_word() {
    host::MIDIDeviceBase device; setup(nullptr, &device);
    observeAt(111); hostReceive(device, {0x017df004, 0x0000f80f});
    observeAt(222); hostReceive(device, {0x0000f705});
    observeAt(999);
    CHECK(!device.read()); CHECK(device.msg_sysex_len == 3);
    paired(MidiIngress::lastReceipt(2), 111, 0);
    CHECK(device.read()); CHECK(device.msg_type == 0xf8);
    paired(MidiIngress::lastReceipt(2), 111, 1);
    CHECK(device.read()); CHECK(device.msg_type == 0xf0); CHECK(device.msg_data1 == 4);
    CHECK(device.msg_sysex[0] == 0xf0); CHECK(device.msg_sysex[1] == 0x7d);
    CHECK(device.msg_sysex[2] == 1); CHECK(device.msg_sysex[3] == 0xf7);
    paired(MidiIngress::lastReceipt(2), 222, 0); // completion word, not invented first-byte time
}
static void test_host_disconnect_and_filtered_read() {
    host::MIDIDeviceBase device; setup(nullptr, &device);
    hostReceive(device, {note(60), note(61)});
    CHECK(!device.read(2)); // consumed channel-1 payload, even though no dispatch
    auto first = MidiIngress::lastReceipt(2); paired(first, 100, 0);
    device.disconnect(); CHECK(device.rxpipe == nullptr); CHECK(device.txpipe == nullptr);
    CHECK(!MidiIngress::currentReceipt(2, first));
    CHECK(device.read()); CHECK(device.msg_data1 == 61);
    auto old = MidiIngress::lastReceipt(2); CHECK(old.associated()); CHECK(!old.current());
    CHECK(!device.read()); CHECK(!MidiIngress::lastReceipt(2).associated());
    // Reconnect/claim reset metadata using its exact hook capacity, no old association.
    midi_ingress_reset(2, &device, device.rx_queue_size); device.rxpipe = &device;
    observeAt(222); hostReceive(device, {note(61)});
    CHECK(device.read()); paired(MidiIngress::lastReceipt(2), 222, 0);
}
static void test_din_irq_fifo_peek_and_ring_wrap() {
    din::HardwareSerialIMXRT serial; setup(&serial);
    for (unsigned round = 0; round < 24; ++round) {
        observeAt(1000 + round); serial.port.receive({0x90, 60, 0xf8}); serial.IRQHandler();
        CHECK(serial.port.DATA.fifo.empty());
        observeAt(9999);
        CHECK(serial.peek() == 0x90); CHECK(serial.peek() == 0x90);
        CHECK(serial.read() == 0x90); auto first = MidiIngress::lastReceipt(1); paired(first, 1000 + round, 4);
        CHECK(serial.read() == 60); paired(MidiIngress::lastReceipt(1), 1000 + round, 5);
        CHECK(MidiIngress::lastReceipt(1).receiveSequence == first.receiveSequence);
        CHECK(serial.read() == 0xf8); paired(MidiIngress::lastReceipt(1), 1000 + round, 6);
        CHECK(serial.read() == -1); CHECK(!MidiIngress::lastReceipt(1).associated());
    }
    CHECK(serial.rx_buffer_head_ == 8); CHECK(serial.rx_buffer_tail_ == 8);
}
static void test_din_full_drop_invalidates_without_reordering() {
    din::HardwareSerialIMXRT serial; setup(&serial);
    for (unsigned packet = 0; packet < 9; ++packet) {
        serial.port.receive({0, 1, 2, 3, 4, 5, 6}); serial.IRQHandler();
    }
    CHECK(serial.rx_buffer_head_ == 63); CHECK(serial.rx_buffer_tail_ == 0);
    serial.port.receive({99, 100}); serial.IRQHandler(); // original IRQ drops both
    CHECK(serial.port.DATA.fifo.empty()); CHECK(serial.rx_buffer_head_ == 63);
    for (unsigned index = 0; index < 63; ++index) {
        CHECK(serial.read() == int(index % 7));
        auto r = MidiIngress::lastReceipt(1); CHECK(r.associated()); CHECK(!r.current());
    }
    CHECK(serial.read() == -1);
    observeAt(222); serial.port.receive({42}); serial.IRQHandler();
    CHECK(serial.read() == 42); paired(MidiIngress::lastReceipt(1), 222, 6);
}
static void test_din_clear_direct_fifo_and_late_peek() {
    din::HardwareSerialIMXRT serial; setup(&serial);
    serial.port.receive({1, 2}); serial.IRQHandler();
    CHECK(serial.read() == 1); auto prior = MidiIngress::lastReceipt(1);
    serial.clear(); CHECK(!MidiIngress::currentReceipt(1, prior));
    CHECK(serial.read() == -1); CHECK(!MidiIngress::lastReceipt(1).associated());
    observeAt(333); serial.port.receive({0xf8});
    CHECK(serial.read() == 0xf8); auto direct = MidiIngress::lastReceipt(1); paired(direct, 333, 0);
    CHECK(direct.flags & MidiIngressReceipt::LateHardwareRead);
    observeAt(444); serial.port.receive({0x90});
    CHECK(serial.peek() == 0x90); CHECK(serial.port.DATA.fifo.empty());
    observeAt(999); CHECK(serial.peek() == 0x90); CHECK(serial.read() == 0x90);
    auto peeked = MidiIngress::lastReceipt(1); paired(peeked, 444, 0);
    CHECK(peeked.flags & MidiIngressReceipt::LateHardwareRead);
    CHECK(peeked.receiveSequence > direct.receiveSequence);
    CHECK(serial.read() == -1); CHECK(!MidiIngress::lastReceipt(1).associated());
}
static void test_raw_clock_capacity_mismatch_and_wrong_owner() {
    din::HardwareSerialIMXRT serial; host::MIDIDeviceBase device; setup(&serial, &device);
    MidiIngress::setClockSampler(nullptr); observeAt(0xfffffff0);
    usbReceive(0, {note(60)}); observeAt(12);
    CHECK(usb::usb_midi_read_message() == note(60));
    auto raw = MidiIngress::lastReceipt(0); CHECK(raw.associated()); CHECK(raw.current());
    CHECK(raw.clock == MidiIngressReceipt::Clock::RawMicros32); CHECK(raw.rawObservedUs == 0xfffffff0);
    CHECK(raw.observedUs == 0); CHECK(!raw.independentObservation());
    // Real receive/dequeue still succeeds under unsupported metadata geometry.
    serial.rx_buffer_total_size_ = 32; serial.rx_buffer_size_ = 32;
    serial.port.receive({42}); serial.IRQHandler(); CHECK(serial.read() == 42);
    CHECK(!MidiIngress::lastReceipt(1).associated());
    host::MIDIDeviceBase unregistered;
    hostReceive(unregistered, {note(62)}); CHECK(unregistered.read()); CHECK(unregistered.msg_data1 == 62);
    CHECK(!MidiIngress::lastReceipt(2).associated());
}
#include "din_parser.inc"
static void expectHookMask(uint32_t incoming) {
    using namespace IngressMaskFixture;
    CHECK(events.size() == 2);
    CHECK(events[0].kind == Kind::Ingress && events[0].enter);
    CHECK(events[0].before == incoming && events[0].after == 1);
    CHECK(events[1].kind == Kind::Ingress && !events[1].enter);
    CHECK(events[1].before == 1 && events[1].after == incoming);
    CHECK(primask == incoming);
}
static void test_unprovisioned_packet_and_direct_preserve_mask_without_sampling() {
    CHECK(!MidiIngress::ready());
    for (uint32_t incoming : {0u, 1u}) {
        IngressMaskFixture::reset(incoming);
        midi_ingress_packet(0, nullptr, 6, 0); expectHookMask(incoming);
        IngressMaskFixture::reset(incoming);
        midi_ingress_direct(1, nullptr); expectHookMask(incoming);
    }
    CHECK(samplerCalls == 0);
}
static void test_complete_hook_route_mask_refusal_matrix() {
    din::HardwareSerialIMXRT serial; host::MIDIDeviceBase device;
    const void* owners[] = {nullptr, &serial, &device};
    const unsigned capacities[] = {6, 64, 400};
    for (uint8_t route = 0; route < 3; ++route) for (uint32_t incoming : {0u, 1u}) {
        setup(&serial, &device);
        MidiIngress::setCountersForTests(route, 5, 10);
        IngressMaskFixture::reset(incoming);
        midi_ingress_packet(route, owners[route], capacities[route], 0);
        expectHookMask(incoming); CHECK(samplerCalls == 1);
        midi_ingress_publish(route, owners[route], 0, 3, 1);
        midi_ingress_consume(route, owners[route], 0, 0);
        auto r = MidiIngress::lastReceipt(route); paired(r, 100, 3);
        CHECK(r.queueGeneration == 5 && r.receiveSequence == 11);
        MidiIngress::noteDispatch(route, r);
        CHECK(MidiIngress::lastDispatchedReceipt(route).receiveSequence == 11);
        CHECK(IngressMaskFixture::primask == incoming && samplerCalls == 1);
        for (unsigned refusal = 0; refusal < 4; ++refusal) {
            MidiIngress::setCountersForTests(route, refusal == 3 ? 0 : 5,
                                             refusal == 2 ? UINT64_MAX : 10);
            midi_ingress_reset(route, owners[route], capacities[route]);
            samplerCalls = 0; IngressMaskFixture::reset(incoming);
            midi_ingress_packet(route, refusal == 0 ? &owners : owners[route],
                                capacities[route] + (refusal == 1), 0);
            expectHookMask(incoming); CHECK(samplerCalls == 0);
        }
        MidiIngress::setCountersForTests(route, 5, 10);
        midi_ingress_reset(route, owners[route], capacities[route]);
        IngressMaskFixture::reset(incoming); samplerCalls = 0;
        midi_ingress_packet(3, nullptr, 6, 0); expectHookMask(incoming);
        CHECK(samplerCalls == 0);
    }
    setup(&serial, &device);
    for (uint32_t incoming : {0u, 1u}) {
        IngressMaskFixture::reset(incoming); samplerCalls = 0;
        midi_ingress_direct(1, &serial); expectHookMask(incoming);
        auto r = MidiIngress::lastReceipt(1); paired(r, 100, 0);
        CHECK(r.flags & MidiIngressReceipt::LateHardwareRead);
        CHECK(samplerCalls == 1 && IngressMaskFixture::primask == incoming);
    }
}
static void test_actual_receiver_packet_frequency_is_not_word_frequency() {
    setup();
    usbReceive(0, {note(60), note(61), note(62), 0x0000f80f});
    CHECK(samplerCalls == 1);
    for (auto word : {note(60), note(61), note(62), 0x0000f80fu})
        CHECK(usb::usb_midi_read_message() == word);
    CHECK(samplerCalls == 1);
    usbReceive(1, {}); CHECK(samplerCalls == 1);
    host::MIDIDeviceBase device; setup(nullptr, &device);
    hostReceive(device, {note(60), 0, note(61), 0x0000f80f}); CHECK(samplerCalls == 1);
    CHECK(device.read() && device.msg_data1 == 60);
    CHECK(device.read() && device.msg_data1 == 61);
    CHECK(device.read() && device.msg_type == 0xf8); CHECK(!device.read());
    CHECK(samplerCalls == 1);
    hostReceive(device, {}); CHECK(samplerCalls == 2); CHECK(!device.read());
    hostReceive(device, {0, 0}); CHECK(samplerCalls == 3); CHECK(!device.read());
    hostReceive(device, {note(62)}); CHECK(samplerCalls == 4);
    CHECK(!device.read(2)); CHECK(samplerCalls == 4); CHECK(!device.read());
    din::HardwareSerialIMXRT serial; setup(&serial);
    serial.port.receive({0, 1, 2, 3, 4, 5, 6}); serial.IRQHandler(); CHECK(samplerCalls == 1);
    for (unsigned i = 0; i < 7; ++i) CHECK(serial.read() == int(i));
    CHECK(samplerCalls == 1);
    serial.IRQHandler(); CHECK(samplerCalls == 1);
    serial.port.receive({42}); CHECK(serial.read() == 42); CHECK(samplerCalls == 2);
    serial.port.receive({43}); CHECK(serial.peek() == 43); CHECK(samplerCalls == 3);
    CHECK(serial.read() == 43); CHECK(samplerCalls == 3);
}
static void test_din_pressure_samples_burst_and_driver_enable_is_distinct() {
    din::HardwareSerialIMXRT serial; setup(&serial);
    for (unsigned burst = 0; burst < 9; ++burst) {
        serial.port.receive({0, 1, 2, 3, 4, 5, 6}); serial.IRQHandler();
    }
    CHECK(samplerCalls == 9);
    serial.port.receive({99, 100}); serial.IRQHandler(); CHECK(samplerCalls == 10);
    CHECK(serial.rx_buffer_head_ == 63 && serial.port.DATA.fifo.empty());
    for (unsigned i = 0; i < 63; ++i) {
        CHECK(serial.read() == int(i % 7)); CHECK(!MidiIngress::lastReceipt(1).current());
    }
    IngressMaskFixture::reset(1);
    serial.port.receive({42}); CHECK(serial.read() == 42);
    // Original driver unconditionally enables; the hook itself restores the
    // masked state it received. Never reinterpret this as whole-driver safety.
    CHECK(IngressMaskFixture::primask == 0);
    bool sawHookRestoreOne = false, sawDriverEnable = false;
    for (const auto& event : IngressMaskFixture::events) {
        if (event.kind == IngressMaskFixture::Kind::Ingress && !event.enter && event.after == 1) sawHookRestoreOne = true;
        if (event.kind == IngressMaskFixture::Kind::Driver && !event.enter && event.after == 0) sawDriverEnable = true;
    }
    CHECK(sawHookRestoreOne && sawDriverEnable);
}

int main() {
#ifndef MIDI_INGRESS_STANDALONE
    UNITY_BEGIN();
#endif
    RUN_TEST(test_unprovisioned_packet_and_direct_preserve_mask_without_sampling);
    RUN_TEST(test_complete_hook_route_mask_refusal_matrix);
    RUN_TEST(test_actual_receiver_packet_frequency_is_not_word_frequency);
    RUN_TEST(test_din_pressure_samples_burst_and_driver_enable_is_distinct);
    RUN_TEST(test_usb_partial_packet_and_packet_reuse);
    RUN_TEST(test_usb_transfer_fifo_wrap_and_zero_length);
    RUN_TEST(test_usb_reset_keeps_payload_but_invalidates_receipt);
    RUN_TEST(test_usb_missing_hook_preserves_payload_refuses_pairing);
    RUN_TEST(test_host_fifo_zero_words_and_empty_clear);
    RUN_TEST(test_host_backpressure_resumes_and_ring_wraps);
    RUN_TEST(test_host_partial_sysex_realtime_metadata_per_word);
    RUN_TEST(test_host_disconnect_and_filtered_read);
    RUN_TEST(test_din_irq_fifo_peek_and_ring_wrap);
    RUN_TEST(test_din_full_drop_invalidates_without_reordering);
    RUN_TEST(test_din_clear_direct_fifo_and_late_peek);
    RUN_TEST(test_raw_clock_capacity_mismatch_and_wrong_owner);
    RUN_TEST(test_din_parser_partial_running_status_and_interleaved_realtime);
    RUN_TEST(test_din_parser_partial_sysex_and_interleaved_realtime);
#ifndef MIDI_INGRESS_STANDALONE
    return UNITY_END();
#else
    return 0;
#endif
}
