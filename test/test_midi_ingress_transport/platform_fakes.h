#pragma once
#include "Arduino.h"
#include <cstdint>
#include <cstring>
#include <deque>
#include <cstdlib>

// Register/controller/platform boundaries only. Driver bodies are generated.
namespace usb {
constexpr unsigned RX_NUM = 6, MIDI_RX_SIZE_480 = 512;
struct transfer_t { uint32_t status = 0, callback_param = 0; };
inline uint32_t rx_packet_size = 64, rx_head = 0, rx_tail = 0, rx_available = 0;
inline uint32_t rx_count[RX_NUM]{}, rx_index[RX_NUM]{}, rx_list[RX_NUM + 1]{};
alignas(4) inline uint8_t rx_buffer[RX_NUM * MIDI_RX_SIZE_480]{};
inline unsigned requeues = 0;
inline void rx_queue_transfer(unsigned) { ++requeues; }
inline int printf(const char*, ...) { return 0; }
inline void reset() {
    rx_head = rx_tail = rx_available = requeues = 0;
    std::memset(rx_count, 0, sizeof(rx_count));
    std::memset(rx_index, 0, sizeof(rx_index));
}
#include "usb_bodies.inc"
}
namespace usb_missing {
using usb::RX_NUM; using usb::MIDI_RX_SIZE_480; using usb::transfer_t;
using usb::rx_packet_size; using usb::rx_head; using usb::rx_tail;
using usb::rx_available; using usb::rx_count; using usb::rx_index;
using usb::rx_list; using usb::rx_buffer; using usb::rx_queue_transfer;
using usb::printf;
#include "usb_missing_bodies.inc"
}

namespace host {
constexpr unsigned HEX = 16;
template <typename... Args> inline void println(Args...) {}
template <typename... Args> inline void print(Args...) {}
inline void print_hexbytes(const void*, unsigned) {}
struct Transfer_t { unsigned length; struct { uint32_t token; } qtd; void* buffer; };
inline unsigned requeues = 0;
template <typename... Args> inline void queue_Data_Transfer(Args...) { ++requeues; }
// Callback sinks fail closed if an untested parser branch reaches a callback.
struct Callback { template <typename... Args> void operator()(Args...) { std::abort(); } };
struct MIDIDeviceBase {
    uint32_t rx_head = 0, rx_tail = 0, rx_queue_size = 400;
    uint32_t rx_buffer[16]{}, rx_queue[400]{};
    int rx_size = 64;
    bool rx_packet_queued = true;
    void *rxpipe = this, *txpipe = this;
    uint8_t msg_cable = 0, msg_type = 0, msg_channel = 0, msg_data1 = 0, msg_data2 = 0;
    uint16_t msg_sysex_len = 0;
    uint8_t msg_sysex[128]{};
    Callback *handleNoteOff = nullptr, *handleNoteOn = nullptr, *handleVelocityChange = nullptr,
        *handleControlChange = nullptr, *handleProgramChange = nullptr, *handleAfterTouch = nullptr,
        *handlePitchChange = nullptr, *handleTimeCodeQuarterFrame = nullptr, *handleSongPosition = nullptr,
        *handleSongSelect = nullptr, *handleTuneRequest = nullptr, *handleClock = nullptr,
        *handleRealTimeSystem = nullptr, *handleStart = nullptr, *handleContinue = nullptr,
        *handleStop = nullptr, *handleActiveSensing = nullptr, *handleSystemReset = nullptr,
        *handleSysExPartial = nullptr, *handleSysExComplete = nullptr;
    static constexpr unsigned SYSEX_MAX_LEN = 128;
    void sysex_byte(uint8_t b);
    void rx_data(const Transfer_t*);
    bool read(uint8_t channel = 0);
    void disconnect();
};
#include "host_bodies.inc"
}

namespace din {
constexpr uint32_t LPUART_STAT_RDRF = 1, LPUART_STAT_IDLE = 2, LPUART_STAT_TDRE = 4,
    LPUART_STAT_TC = 8, LPUART_CTRL_TIE = 1, LPUART_CTRL_TCIE = 2, LPUART_CTRL_TXDIR = 4;
#define DIRECT_WRITE_LOW(a,b) ((void)0)
struct IMXRT_LPUART_t {
    uint32_t STAT = 0, WATER = 0, CTRL = 0;
    struct Data {
        IMXRT_LPUART_t* port;
        std::deque<uint16_t> fifo;
        operator uint32_t() {
            if (fifo.empty()) std::abort();
            auto value = fifo.front(); fifo.pop_front();
            port->WATER = (port->WATER & ~0x07000000u) | (uint32_t(fifo.size()) << 24);
            return value;
        }
        void operator=(uint32_t) { std::abort(); } // TX not under test
    } DATA{this, {}};
    void receive(std::initializer_list<uint16_t> values) {
        DATA.fifo = values; WATER = uint32_t(values.size()) << 24; STAT = LPUART_STAT_RDRF;
    }
};
struct HardwareSerialIMXRT {
    IMXRT_LPUART_t port;
    void* port_addr = &port;
    uint32_t rx_buffer_head_ = 0, rx_buffer_tail_ = 0, rx_buffer_total_size_ = 64, rx_buffer_size_ = 64;
    uint16_t rx_buffer_[64]{}, storage[64]{};
    uint16_t* rx_buffer_storage_ = storage;
    uint32_t tx_buffer_head_ = 0, tx_buffer_tail_ = 0, tx_buffer_total_size_ = 64, tx_buffer_size_ = 64;
    uint16_t tx_buffer_[64]{}, *tx_buffer_storage_ = storage;
    void *rts_pin_baseReg_ = nullptr, *transmit_pin_baseReg_ = nullptr;
    uint32_t rts_high_watermark_ = 60, rts_low_watermark_ = 4, transmit_pin_bitmask_ = 0;
    bool transmitting_ = false, half_duplex_mode_ = false;
    void rts_assert() {} void rts_deassert() {}
    void IRQHandler(); int read(); int peek(); void clear();
};
#include "din_bodies.inc"
}
