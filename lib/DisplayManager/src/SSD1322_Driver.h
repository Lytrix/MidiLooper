#ifndef SSD1322_DRIVER_H
#define SSD1322_DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include <DMAChannel.h>

// Display dimensions
#define DISPLAY_WIDTH   256
#define DISPLAY_HEIGHT  64
#define PIXEL_BITS      4       // 4-bit grayscale (16 levels)
#define PIXELS_PER_BYTE 2       // Each byte stores 2 pixels (4-bits each)
#define FRAMEBUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT / PIXELS_PER_BYTE)  // 8192 bytes

// MIDI timing constant
#define MIDI_CLOCK_THRESHOLD 41.6  // MIDI timing threshold in microseconds (24kHz)

// Chunking for interruptible transfers
#define TRANSFER_CHUNK_SIZE 120
#define NUM_CHUNKS (FRAMEBUFFER_SIZE/TRANSFER_CHUNK_SIZE + 1)

// DMA segments for circular buffer
#define DMA_SEGMENTS    3
#define SEGMENT_SIZE    (FRAMEBUFFER_SIZE/2 / DMA_SEGMENTS)

class SSD1322_Driver {
public:
    // SSD1322 commands namespace
    struct CMD {
        static const uint8_t SET_COLUMN         = 0x15;
        static const uint8_t SET_ROW            = 0x75;
        static const uint8_t WRITE_RAM          = 0x5C;
        static const uint8_t READ_RAM           = 0x5D;
        static const uint8_t SET_REMAP          = 0xA0;
        static const uint8_t DISPLAY_ON         = 0xAF;
        static const uint8_t DISPLAY_OFF        = 0xAE;
        
        // Display dimensions and settings
        static const uint16_t WIDTH = 256;
        static const uint16_t HEIGHT = 64;
        static const uint8_t COLUMN_START = 0x1C;
        static const uint8_t COLUMN_END = 0x5B;
        static const uint8_t ROW_START = 0x00;
        static const uint8_t ROW_END = 0x3F;
    };

    // Constructor
    SSD1322_Driver(int csPin, int dcPin, int rstPin, int spiClock = 10000000);
    
    // Initialization
    void begin();
    
    // Display control
    void displayOn();
    void displayOff();
    void clearDisplay();
    
    // Drawing methods
    void initializeTransfer();
    bool transferChunk();  // Returns true when complete
    bool draw(uint8_t *pixelBuffer, bool waitForCompletion = false);
    
    // DMA setup and control
    void setupDMA();
    void startDMATransfer(uint8_t *pixelBuffer);
    void stopDMA();
    
    // MIDI-friendly drawing - call this regularly from main loop
    void updateDisplay(uint8_t *pixelBuffer);
    
    // Status checking
    bool isTransferInProgress() const { return transferInProgress; }
    unsigned long getTransferTime() const { return lastTransferTime; }
    bool isDMAActive() const { return dmaActive; }
    
    // Buffer management for circular DMA
    void updateDMABufferSegment(uint8_t *pixelBuffer, int segment);
    void beginPixelBufferUpdate(int segment);
    void endPixelBufferUpdate();
    
    // DMA interrupt handler - must be public for C-style callback
    void dmaInterruptHandler();

private:
    int _csPin;
    int _dcPin;
    int _rstPin;
    int _spiClock;
    
    // Chunked transfer variables
    static uint8_t _packedBuffer[FRAMEBUFFER_SIZE/2];
    volatile bool transferInProgress;
    volatile int currentChunk;
    unsigned long lastTransferTime;
    
    // DMA variables
    DMAChannel _dma;
    DMASetting _dmaSettings[DMA_SEGMENTS];
    volatile bool dmaActive;
    volatile uint32_t dmaTriggerCount;
    volatile int activeSegment;
    volatile int updatableSegment;
    
    // Helper methods
    void sendCommand(uint8_t cmd);
    void sendData(uint8_t data);
    void packPixels(uint8_t *srcBuffer);
    void packPixelsToSegment(uint8_t *srcBuffer, int segment);
    void setupDisplayWindow();
    void endDisplayTransfer();
    void configureDMAMux();
    void configureFIFOWatermarks(uint8_t txWatermark = 1, uint8_t rxWatermark = 3);
    
    // Extended init sequences
    void sendExtendedCommands();
    
    // Friend function to handle DMA interrupt
    static void dmaCompleteCallback();
    friend void dmaCompleteCallback();
};

#endif // SSD1322_DRIVER_H