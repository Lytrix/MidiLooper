//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiFaderManager.h"
#include "Logger.h"
#include "MidiConfig.h"
#include <functional>

#if defined(__IMXRT1062__)
DMAMEM MidiFaderManager midiFaderManager;
#else
MidiFaderManager midiFaderManager;
#endif

MidiFaderManager::MidiFaderManager() {
    // Set up the callback from processor to this manager
    processor.setFaderMovementCallback(
        std::bind(&MidiFaderManager::onFaderMovement, this, 
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
}

void MidiFaderManager::setup() {
    // Initialize the configuration system
    MidiFaderConfig::Config::initialize();
    
    // Load default configuration BEFORE setting up the processor
    loadFaderConfiguration("basic");
    
    // Setup the processor (now that config is loaded)
    processor.setup();
    
    logger.info("MidiFaderManager setup complete with %d configured faders", 
                getConfiguredFaderCount());
}

void MidiFaderManager::update() {
    // Update the processor to handle pending fader updates
    processor.update();
}

void MidiFaderManager::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
    // Validate input
    if (!isValidChannel(channel) || !isValidPitchbend(pitchValue)) {
        return;
    }
    
    // Delegate to processor
    processor.handlePitchbend(channel, pitchValue);
}

void MidiFaderManager::handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    // Validate input
    if (!isValidChannel(channel) || !isValidCC(value)) {
        return;
    }
    
    // Delegate to processor
    processor.handleCC(channel, ccNumber, value);
}

void MidiFaderManager::onFaderMovement(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
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

void MidiFaderManager::loadFaderConfiguration(const char* configName) {
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

void MidiFaderManager::addCustomFader(MidiMapping::FaderType faderType, uint8_t channel, 
                                       const char* description, MidiFaderConfig::ActionType action) {
    MidiFaderConfig::FaderConfig config(faderType, channel, description);
    config.withAction(action);
    
    MidiFaderConfig::Config::addFader(config);
    logger.info("Added custom fader: %s (type %d, channel %d)", description, (int)faderType, channel);
}

MidiMapping::FaderType MidiFaderManager::getCurrentDriverFader() const {
    return processor.getCurrentDriverFader();
}

const MidiFaderProcessor::FaderState& MidiFaderManager::getFaderState(MidiMapping::FaderType faderType) const {
    return processor.getFaderState(faderType);
}

MidiFaderProcessor::FaderState& MidiFaderManager::getFaderStateMutable(MidiMapping::FaderType faderType) {
    return processor.getFaderStateMutable(faderType);
}

void MidiFaderManager::markFaderSent(MidiMapping::FaderType faderType) {
    processor.markFaderSent(faderType);
}

void MidiFaderManager::printFaderConfiguration() const {
    MidiFaderConfig::Config::printConfiguration();
}

uint32_t MidiFaderManager::getConfiguredFaderCount() const {
    return MidiFaderConfig::Config::getFaderConfigs().size();
}

bool MidiFaderManager::isValidChannel(uint8_t channel) const {
    return channel >= 1 && channel <= 16;
}

bool MidiFaderManager::isValidPitchbend(int16_t pitchValue) const {
    return MidiConfig::Pitchbend::isValidLogical(pitchValue);
}

bool MidiFaderManager::isValidCC(uint8_t ccValue) const {
    return ccValue <= 127;
}
