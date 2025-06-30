//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiFaderManagerV2.h"
#include "Logger.h"
#include <functional>

MidiFaderManagerV2 midiFaderManagerV2;

MidiFaderManagerV2::MidiFaderManagerV2() {
    // Set up the callback from processor to this manager
    processor.setFaderMovementCallback(
        std::bind(&MidiFaderManagerV2::onFaderMovement, this, 
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
}

void MidiFaderManagerV2::setup() {
    // Initialize the configuration system
    MidiFaderConfig::Config::initialize();
    
    // Setup the processor
    processor.setup();
    
    // Load default configuration
    loadFaderConfiguration("basic");
    
    logger.info("MidiFaderManagerV2 setup complete with %d configured faders", 
                getConfiguredFaderCount());
}

void MidiFaderManagerV2::update() {
    // Update the processor to handle pending fader updates
    processor.update();
}

void MidiFaderManagerV2::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
    // Validate input
    if (!isValidChannel(channel) || !isValidPitchbend(pitchValue)) {
        return;
    }
    
    // Delegate to processor
    processor.handlePitchbend(channel, pitchValue);
}

void MidiFaderManagerV2::handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    // Validate input
    if (!isValidChannel(channel) || !isValidCC(value)) {
        return;
    }
    
    // Delegate to processor
    processor.handleCC(channel, ccNumber, value);
}

void MidiFaderManagerV2::onFaderMovement(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    // Find the fader configuration
    const MidiFaderConfig::FaderConfig* config = 
        MidiFaderConfig::Config::findFaderConfig(faderType);
    
    if (config == nullptr) {
        logger.debug("No configuration found for fader type: %d", (int)faderType);
        return;
    }
    
    logger.info("Fader movement: %s (type %d)", config->description, (int)faderType);
    
    // Execute the action with the configured parameter
    actions.executeAction(config->action, faderType, pitchbendValue, ccValue, config->parameter);
}

void MidiFaderManagerV2::loadFaderConfiguration(const char* configName) {
    if (strcmp(configName, "basic") == 0) {
        MidiFaderConfig::Config::loadBasicConfiguration();
    } else if (strcmp(configName, "extended") == 0) {
        MidiFaderConfig::Config::loadExtendedConfiguration();
    } else {
        logger.warning("Unknown fader configuration: %s, loading basic", configName);
        MidiFaderConfig::Config::loadBasicConfiguration();
    }
    
    logger.info("Loaded fader configuration: %s (%d faders)", 
                configName, getConfiguredFaderCount());
}

void MidiFaderManagerV2::addCustomFader(MidiMapping::FaderType faderType, uint8_t channel, 
                                       const char* description, MidiFaderConfig::ActionType action) {
    MidiFaderConfig::FaderConfig config(faderType, channel, description);
    config.withAction(action);
    
    MidiFaderConfig::Config::addFader(config);
    logger.info("Added custom fader: %s (type %d, channel %d)", description, (int)faderType, channel);
}

MidiMapping::FaderType MidiFaderManagerV2::getCurrentDriverFader() const {
    return processor.getCurrentDriverFader();
}

const MidiFaderProcessor::FaderState& MidiFaderManagerV2::getFaderState(MidiMapping::FaderType faderType) const {
    return processor.getFaderState(faderType);
}

MidiFaderProcessor::FaderState& MidiFaderManagerV2::getFaderStateMutable(MidiMapping::FaderType faderType) {
    return processor.getFaderStateMutable(faderType);
}

void MidiFaderManagerV2::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    processor.scheduleOtherFaderUpdates(driverFader);
}

void MidiFaderManagerV2::markFaderSent(MidiMapping::FaderType faderType) {
    processor.markFaderSent(faderType);
}

void MidiFaderManagerV2::printFaderConfiguration() const {
    MidiFaderConfig::Config::printConfiguration();
}

uint32_t MidiFaderManagerV2::getConfiguredFaderCount() const {
    return MidiFaderConfig::Config::getFaderConfigs().size();
}

bool MidiFaderManagerV2::isValidChannel(uint8_t channel) const {
    return channel >= 1 && channel <= 16;
}

bool MidiFaderManagerV2::isValidPitchbend(int16_t pitchValue) const {
    return pitchValue >= -8192 && pitchValue <= 8191;
}

bool MidiFaderManagerV2::isValidCC(uint8_t ccValue) const {
    return ccValue <= 127;
} 