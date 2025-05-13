#include "SSD1322_Driver.h"

// Static initialization
uint8_t SSD1322_Driver::_packedBuffer[FRAMEBUFFER_SIZE/2];

SSD1322_Driver::SSD1322_Driver(int csPin, int dcPin, int rstPin, int spiClock)
    : _csPin(csPin), _dcPin(dcPin), _rstPin(rstPin), _spiClock(spiClock),
      transferInProgress(false), currentChunk(0), lastTransferTime(0) {
}

void SSD1322_Driver::begin() {
    // Configure pins
    pinMode(_csPin, OUTPUT);
    pinMode(_dcPin, OUTPUT);
    pinMode(_rstPin, OUTPUT);
    
    digitalWrite(_csPin, HIGH);
    digitalWrite(_dcPin, HIGH);

    // Hardware reset
    digitalWrite(_rstPin, LOW);
    delay(150);  
    digitalWrite(_rstPin, HIGH);
    delay(150);

    // Initialize display
    displayOff();
    sendExtendedCommands();
    clearDisplay();
    displayOn();
}

void SSD1322_Driver::sendExtendedCommands() {
    // Send all initialization commands
    sendCommand(0xFD); sendData(0x12);          // Unlock controller
    sendCommand(0xB3); sendData(0x91);          // Set clock (medium freq)
    sendCommand(0xCA); sendData(CMD::ROW_END);  // Multiplex ratio
    sendCommand(0xA2); sendData(0x00);          // Display offset
    sendCommand(0xA1); sendData(0x00);          // Display start line
    
    // Set remap format - vertical addressing with nibble remap for 4-bit pixels
    sendCommand(CMD::SET_REMAP);
    sendData(0x06);    // Vertical addressing + nibble remap
    sendData(0x11);    // Dual COM mode for better uniformity
    
    sendCommand(0xAB); sendData(0x01);          // Function select (internal VDD)
    sendCommand(0xB4); sendData(0xA0); sendData(0xB5); // Display enhancement
    sendCommand(0xC1); sendData(0x9F);          // Contrast
    sendCommand(0xC7); sendData(0x0F);          // Master current
    sendCommand(0xB9);                          // Linear grayscale
    sendCommand(0xB1); sendData(0xE2);          // Phase length
    sendCommand(0xD1); sendData(0x82); sendData(0x20); // Display enhancement B
    sendCommand(0xBB); sendData(0x1F);          // Precharge2
    sendCommand(0xB6); sendData(0x08);          // Precharge
    sendCommand(0xBE); sendData(0x07);          // VCOMH
    sendCommand(0xA6);                          // Normal display
}

void SSD1322_Driver::displayOn() {
    sendCommand(CMD::DISPLAY_ON);
}

void SSD1322_Driver::displayOff() {
    sendCommand(CMD::DISPLAY_OFF);
}

void SSD1322_Driver::clearDisplay() {
    // Set window to full display
    setupDisplayWindow();
    
    // Send black pixels
    digitalWrite(_csPin, LOW);
    digitalWrite(_dcPin, HIGH); // Data mode
    
    for(int i = 0; i < FRAMEBUFFER_SIZE; i++) {
        SPI.transfer(0x00);  // Clear to black
    }
    
    digitalWrite(_csPin, HIGH);
}

void SSD1322_Driver::sendCommand(uint8_t cmd) {
    digitalWrite(_dcPin, LOW);  // Command mode
    digitalWrite(_csPin, LOW);   
    SPI.transfer(cmd);
    digitalWrite(_csPin, HIGH);
}

void SSD1322_Driver::sendData(uint8_t data) {
    digitalWrite(_dcPin, HIGH); // Data mode
    digitalWrite(_csPin, LOW);   
    SPI.transfer(data);
    digitalWrite(_csPin, HIGH);
}

void SSD1322_Driver::packPixels(uint8_t *srcBuffer) {
    uint8_t *src = srcBuffer;
    uint8_t *dst = _packedBuffer;
    
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i += 2) {
        uint8_t a = *src++;
        uint8_t b = *src++;
        *dst++ = (min(a, (uint8_t)15) << 4) | min(b, (uint8_t)15);
    }
}

void SSD1322_Driver::setupDisplayWindow() {
    // Set column and row in one efficient operation
    digitalWrite(_csPin, LOW);
    digitalWrite(_dcPin, LOW); // Command mode
    SPI.transfer(CMD::SET_COLUMN);
    digitalWrite(_dcPin, HIGH); // Data mode
    SPI.transfer(CMD::COLUMN_START);
    SPI.transfer(CMD::COLUMN_END);
    digitalWrite(_csPin, HIGH);
    delayNanoseconds(20);
    
    digitalWrite(_csPin, LOW);
    digitalWrite(_dcPin, LOW); // Command mode
    SPI.transfer(CMD::SET_ROW);
    digitalWrite(_dcPin, HIGH); // Data mode
    SPI.transfer(CMD::ROW_START);
    SPI.transfer(CMD::ROW_END);
    digitalWrite(_csPin, HIGH);
    delayNanoseconds(20);
    
    // Write RAM command
    digitalWrite(_csPin, LOW);
    digitalWrite(_dcPin, LOW); // Command mode
    SPI.transfer(CMD::WRITE_RAM);
    digitalWrite(_dcPin, HIGH); // Data mode
}

void SSD1322_Driver::endDisplayTransfer() {
    digitalWrite(_csPin, HIGH);
}

void SSD1322_Driver::initializeTransfer() {
    transferInProgress = true;
    currentChunk = 0;
    setupDisplayWindow();
}

bool SSD1322_Driver::transferChunk() {
    if (currentChunk < NUM_CHUNKS) {
        int startOffset = currentChunk * TRANSFER_CHUNK_SIZE;
        int bytesToTransfer = min(TRANSFER_CHUNK_SIZE, 
                              FRAMEBUFFER_SIZE/2 - startOffset);
        
        // Send this chunk via SPI
        SPI.transfer(&_packedBuffer[startOffset], NULL, bytesToTransfer);
        
        currentChunk++;
        return false; // Not complete
    } else {
        // Complete the transfer
        endDisplayTransfer();
        transferInProgress = false;
        return true; // Complete
    }
}

bool SSD1322_Driver::draw(uint8_t *pixelBuffer, bool waitForCompletion) {
    unsigned long startTime = micros();
    
    // Pack the pixels
    packPixels(pixelBuffer);
    
    // Start the transfer
    SPI.beginTransaction(SPISettings(_spiClock, MSBFIRST, SPI_MODE0));
    setupDisplayWindow();
    
    // Do the transfer
    digitalWrite(_csPin, LOW);
    SPI.transfer(_packedBuffer, NULL, FRAMEBUFFER_SIZE/2);
    digitalWrite(_csPin, HIGH);
    
    SPI.endTransaction();
    
    lastTransferTime = micros() - startTime;
    return true;
}

void SSD1322_Driver::updateDisplay(uint8_t *pixelBuffer) {
    // If no transfer is in progress, start a new one
    if (!transferInProgress) {
        packPixels(pixelBuffer);
        initializeTransfer();
    }
    
    // Transfer one chunk
    transferChunk();
}

void SSD1322_Driver::setupDMA() {
    // This would implement the circular DMA buffer setup
    // Left out for now as it adds complexity and may not be
    // needed with the interruptible approach
}

void SSD1322_Driver::stopDMA() {
    // Stop any active DMA operations
    transferInProgress = false;
    digitalWrite(_csPin, HIGH);
}