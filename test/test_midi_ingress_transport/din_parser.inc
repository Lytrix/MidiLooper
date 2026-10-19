// Actual pinned MIDI parser over the actual instrumented UART dequeue body.
inline uint32_t millis() { return ingressFakeMicros / 1000; }
#include <MIDI.h>
struct ParserTransport {
    static constexpr bool thruActivated = false;
    din::HardwareSerialIMXRT& serial;
    void begin() {}
    unsigned available() const {
        const unsigned ring = (serial.rx_buffer_head_ + serial.rx_buffer_total_size_ - serial.rx_buffer_tail_) % serial.rx_buffer_total_size_;
        return ring + unsigned(serial.port.DATA.fifo.size());
    }
    uint8_t read() { return uint8_t(serial.read()); }
    bool beginTransmission(midi::MidiType) { std::abort(); }
    void write(uint8_t) { std::abort(); }
    void endTransmission() { std::abort(); }
};
using DinParser = midi::MidiInterface<ParserTransport>;
static bool nextParsed(DinParser& parser, ParserTransport& transport) {
    for (unsigned i = 0; i < 32 && transport.available(); ++i) if (parser.read()) return true;
    return false;
}
static void test_din_parser_partial_running_status_and_interleaved_realtime() {
    din::HardwareSerialIMXRT serial; setup(&serial);
    ParserTransport transport{serial}; DinParser parser(transport); parser.begin(MIDI_CHANNEL_OMNI);
    observeAt(111); serial.port.receive({0x90, 60}); serial.IRQHandler();
    CHECK(!nextParsed(parser, transport)); // Partial frame produces no message.
    observeAt(222); serial.port.receive({0xf8}); serial.IRQHandler();
    CHECK(nextParsed(parser, transport)); CHECK(parser.getType() == midi::Clock);
    paired(MidiIngress::lastReceipt(1), 222, 6);
    observeAt(333); serial.port.receive({100, 61, 0xf8, 101}); serial.IRQHandler();
    observeAt(999); // Completing-byte observation, not first-byte or read time.
    CHECK(nextParsed(parser, transport)); CHECK(parser.getType() == midi::NoteOn);
    CHECK(parser.getData1() == 60 && parser.getData2() == 100);
    paired(MidiIngress::lastReceipt(1), 333, 3);
    CHECK(nextParsed(parser, transport)); CHECK(parser.getType() == midi::Clock);
    paired(MidiIngress::lastReceipt(1), 333, 5);
    CHECK(nextParsed(parser, transport)); CHECK(parser.getType() == midi::NoteOn);
    CHECK(parser.getData1() == 61 && parser.getData2() == 101);
    paired(MidiIngress::lastReceipt(1), 333, 6);
    CHECK(!nextParsed(parser, transport));
}
static void test_din_parser_partial_sysex_and_interleaved_realtime() {
    din::HardwareSerialIMXRT serial; setup(&serial);
    ParserTransport transport{serial}; DinParser parser(transport); parser.begin(MIDI_CHANNEL_OMNI);
    observeAt(111); serial.port.receive({0xf0, 1}); serial.IRQHandler();
    CHECK(!nextParsed(parser, transport));
    observeAt(222); serial.port.receive({0xf8}); serial.IRQHandler();
    CHECK(nextParsed(parser, transport)); CHECK(parser.getType() == midi::Clock);
    paired(MidiIngress::lastReceipt(1), 222, 6);
    observeAt(333); serial.port.receive({2, 0xf7}); serial.IRQHandler();
    CHECK(nextParsed(parser, transport)); CHECK(parser.getType() == midi::SystemExclusive);
    CHECK(parser.getSysExArrayLength() == 4);
    const auto* bytes = parser.getSysExArray();
    CHECK(bytes[0] == 0xf0 && bytes[1] == 1 && bytes[2] == 2 && bytes[3] == 0xf7);
    paired(MidiIngress::lastReceipt(1), 333, 6);
}
