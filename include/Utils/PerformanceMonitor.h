//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <deque>
#include <string>
#include <algorithm>
#include <Arduino.h>

namespace PerformanceMonitor {

/**
 * @struct PerformanceMetrics
 * @brief Real-time performance metrics for the MIDI looper system
 */
struct PerformanceMetrics {
    // CPU Usage
    uint32_t cpuUsagePercent;           // CPU usage percentage
    uint32_t loopTimeMicros;            // Main loop execution time in microseconds
    uint32_t maxLoopTimeMicros;         // Maximum loop time observed
    
    // Memory Usage
    uint32_t freeRAMBytes;              // Available RAM in bytes
    uint32_t usedRAMBytes;              // Used RAM in bytes
    uint32_t heapFragmentationPercent;  // Heap fragmentation percentage
    
    // MIDI Performance
    uint32_t midiEventsProcessed;       // MIDI events processed per loop
    uint32_t cacheHitRate;              // Note cache hit rate (0-100)
    uint32_t displayUpdateTimeMicros;   // Display update time in microseconds
    
    // Audio Performance
    uint32_t audioLatencyMicros;        // Audio processing latency
    uint32_t bufferUnderruns;           // Number of buffer underruns
    uint32_t bufferOverruns;            // Number of buffer overruns
    
    // System Health
    uint32_t temperatureCelsius;        // System temperature
    uint32_t voltageMillivolts;         // System voltage
    uint32_t uptimeSeconds;             // System uptime in seconds
};

/**
 * @class PerformanceMonitor
 * @brief Real-time performance monitoring and optimization
 * 
 * Tracks various performance metrics and provides optimization suggestions.
 * Can be used to identify bottlenecks and optimize the system.
 */
class PerformanceMonitor {
private:
    static constexpr size_t HISTORY_SIZE = 100;  // Keep last 100 samples
    
    PerformanceMetrics currentMetrics;
    std::deque<PerformanceMetrics> history;
    
    // Timing variables
    uint32_t loopStartTime;
    uint32_t lastUpdateTime;
    uint32_t frameCount;
    
    // Performance counters
    uint32_t totalMidiEventsProcessed;
    uint32_t cacheHits;
    uint32_t cacheMisses;
    
public:
    PerformanceMonitor() : loopStartTime(0), lastUpdateTime(0), frameCount(0),
                          totalMidiEventsProcessed(0), cacheHits(0), cacheMisses(0) {
        // history.reserve(HISTORY_SIZE); // std::deque does not support reserve
    }
    
    /**
     * @brief Start timing a loop iteration
     */
    void beginLoop() {
        loopStartTime = micros();
    }
    
    /**
     * @brief End timing a loop iteration and update metrics
     */
    void endLoop() {
        uint32_t loopTime = micros() - loopStartTime;
        currentMetrics.loopTimeMicros = loopTime;
        
        if (loopTime > currentMetrics.maxLoopTimeMicros) {
            currentMetrics.maxLoopTimeMicros = loopTime;
        }
        
        frameCount++;
        
        // Update metrics every 100 frames (approximately 2 seconds at 50Hz)
        if (frameCount % 100 == 0) {
            updateMetrics();
        }
    }
    
    /**
     * @brief Record MIDI event processing
     */
    void recordMidiEvent() {
        totalMidiEventsProcessed++;
        currentMetrics.midiEventsProcessed++;
    }
    
    /**
     * @brief Record cache hit/miss
     */
    void recordCacheHit() { cacheHits++; }
    void recordCacheMiss() { cacheMisses++; }
    
    /**
     * @brief Get current performance metrics
     */
    const PerformanceMetrics& getCurrentMetrics() const {
        return currentMetrics;
    }
    
    /**
     * @brief Get performance history
     */
    const std::deque<PerformanceMetrics>& getHistory() const {
        return history;
    }
    
    /**
     * @brief Get average CPU usage over the last N samples
     */
    uint32_t getAverageCPUUsage(size_t samples = 10) const {
        if (history.empty()) return 0;
        
        size_t count = std::min(samples, history.size());
        uint32_t total = 0;
        
        auto it = history.rbegin();
        for (size_t i = 0; i < count; ++i) {
            total += it->cpuUsagePercent;
            ++it;
        }
        
        return total / count;
    }
    
    /**
     * @brief Check if system is under stress
     */
    bool isSystemStressed() const {
        return currentMetrics.cpuUsagePercent > 80 ||
               currentMetrics.loopTimeMicros > 20000 ||  // 20ms threshold
               currentMetrics.freeRAMBytes < 10000;      // Less than 10KB free
    }
    
    /**
     * @brief Get optimization suggestions based on current metrics
     */
    std::vector<std::string> getOptimizationSuggestions() const {
        std::vector<std::string> suggestions;
        
        if (currentMetrics.cpuUsagePercent > 80) {
            suggestions.push_back("High CPU usage detected - consider reducing display update frequency");
        }
        
        if (currentMetrics.loopTimeMicros > 20000) {
            suggestions.push_back("Long loop time detected - optimize main loop processing");
        }
        
        if (currentMetrics.cacheHitRate < 50) {
            suggestions.push_back("Low cache hit rate - review caching strategy");
        }
        
        if (currentMetrics.freeRAMBytes < 10000) {
            suggestions.push_back("Low memory - consider reducing undo history or note cache size");
        }
        
        if (currentMetrics.heapFragmentationPercent > 30) {
            suggestions.push_back("High heap fragmentation - consider using memory pools");
        }
        
        return suggestions;
    }
    
private:
    void updateMetrics() {
        // Calculate cache hit rate
        uint32_t totalCacheAccesses = cacheHits + cacheMisses;
        currentMetrics.cacheHitRate = (totalCacheAccesses > 0) ? 
            (cacheHits * 100) / totalCacheAccesses : 0;
        
        // Calculate CPU usage (simplified - based on loop time vs target)
        uint32_t targetLoopTime = 20000;  // 20ms target (50Hz)
        currentMetrics.cpuUsagePercent = (currentMetrics.loopTimeMicros * 100) / targetLoopTime;
        if (currentMetrics.cpuUsagePercent > 100) currentMetrics.cpuUsagePercent = 100;
        
        // Update memory metrics
        updateMemoryMetrics();
        
        // Update system metrics
        updateSystemMetrics();
        
        // Store in history
        history.push_back(currentMetrics);
        if (history.size() > HISTORY_SIZE) {
            history.pop_front();
        }
        
        // Reset counters
        currentMetrics.midiEventsProcessed = 0;
        cacheHits = 0;
        cacheMisses = 0;
    }
    
    void updateMemoryMetrics() {
        // Teensy 4.1: Use a static value for demonstration (from compile output)
        uint32_t freeMemory = 192416;  // From compilation output - approximate
        uint32_t totalRAM = 1024 * 512; // 512KB RAM
        currentMetrics.freeRAMBytes = freeMemory;
        currentMetrics.usedRAMBytes = totalRAM - freeMemory;
        currentMetrics.heapFragmentationPercent = 0;  // Would need heap analysis
    }
    
    void updateSystemMetrics() {
        // Update uptime
        currentMetrics.uptimeSeconds = millis() / 1000;
        
        // Temperature and voltage would require hardware-specific code
        currentMetrics.temperatureCelsius = 0;  // Would need temperature sensor
        currentMetrics.voltageMillivolts = 0;   // Would need voltage monitoring
        
        // Audio metrics (if applicable)
        currentMetrics.audioLatencyMicros = 0;
        currentMetrics.bufferUnderruns = 0;
        currentMetrics.bufferOverruns = 0;
        
        // Display update time (would be set by display system)
        currentMetrics.displayUpdateTimeMicros = 0;
    }
};

// Global performance monitor instance
extern PerformanceMonitor globalPerformanceMonitor;

} // namespace PerformanceMonitor 