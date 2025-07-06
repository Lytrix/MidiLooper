//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include "MidiEvent.h"

// Forward declarations
class Logger;
extern Logger logger;

namespace MemoryPool {

/**
 * @class MidiEventPool
 * @brief Memory pool for MidiEvent objects to reduce allocation overhead
 * 
 * Pre-allocates a pool of MidiEvent objects and provides fast allocation/deallocation.
 * Reduces memory fragmentation and improves performance for frequent MIDI event creation.
 */
class MidiEventPool {
private:
    static constexpr size_t INITIAL_POOL_SIZE = 1024;  // Start with 1K events
    static constexpr size_t GROWTH_FACTOR = 2;         // Double size when growing
    
    std::vector<MidiEvent> pool;
    std::vector<bool> used;
    size_t nextFreeIndex;
    
public:
    MidiEventPool() : nextFreeIndex(0) {
        pool.reserve(INITIAL_POOL_SIZE);
        used.resize(INITIAL_POOL_SIZE, false);
    }
    
    /**
     * @brief Allocate a MidiEvent from the pool
     * @return Pointer to an unused MidiEvent, or nullptr if pool is full
     */
    MidiEvent* allocate() {
        // Find next free slot
        while (nextFreeIndex < used.size() && used[nextFreeIndex]) {
            nextFreeIndex++;
        }
        
        if (nextFreeIndex >= used.size()) {
            // Pool is full, grow it
            growPool();
        }
        
        if (nextFreeIndex < used.size()) {
            used[nextFreeIndex] = true;
            return &pool[nextFreeIndex++];
        }
        
        return nullptr; // Should never happen after growPool()
    }
    
    /**
     * @brief Return a MidiEvent to the pool
     * @param event Pointer to the MidiEvent to deallocate
     */
    void deallocate(MidiEvent* event) {
        if (!event) return;
        
        // Find the index of this event in the pool
        size_t index = event - &pool[0];
        if (index < pool.size() && used[index]) {
            used[index] = false;
            // Reset the event to default state
            *event = MidiEvent();
            
            // Update nextFreeIndex if this slot is before it
            if (index < nextFreeIndex) {
                nextFreeIndex = index;
            }
        }
    }
    
    /**
     * @brief Get current pool statistics
     * @return Pair of (total capacity, used count)
     */
    std::pair<size_t, size_t> getStats() const {
        size_t usedCount = 0;
        for (bool isUsed : used) {
            if (isUsed) usedCount++;
        }
        return {pool.size(), usedCount};
    }
    
    /**
     * @brief Clear all allocations and reset pool
     */
    void reset() {
        std::fill(used.begin(), used.end(), false);
        nextFreeIndex = 0;
    }
    
private:
    void growPool() {
        size_t newSize = pool.size() * GROWTH_FACTOR;
        if (newSize == 0) newSize = INITIAL_POOL_SIZE;
        
        pool.resize(newSize);
        used.resize(newSize, false);
        
        // Use Serial for logging instead of logger to avoid circular dependencies
        Serial.printf("MidiEventPool grown to %zu events\n", newSize);
    }
};

/**
 * @class PooledMidiEventVector
 * @brief Vector-like container that uses the MidiEventPool for allocations
 * 
 * Provides std::vector-like interface but uses the memory pool for better performance.
 * Automatically manages pool allocations and deallocations.
 */
class PooledMidiEventVector {
private:
    std::vector<MidiEvent*> events;
    MidiEventPool& pool;
    
public:
    explicit PooledMidiEventVector(MidiEventPool& eventPool) : pool(eventPool) {}
    
    ~PooledMidiEventVector() {
        clear();
    }
    
    /**
     * @brief Add a MidiEvent to the vector
     * @param event The MidiEvent to add (will be copied)
     */
    void push_back(const MidiEvent& event) {
        MidiEvent* pooledEvent = pool.allocate();
        if (pooledEvent) {
            *pooledEvent = event;
            events.push_back(pooledEvent);
        }
    }
    
    /**
     * @brief Remove the last event
     */
    void pop_back() {
        if (!events.empty()) {
            pool.deallocate(events.back());
            events.pop_back();
        }
    }
    
    /**
     * @brief Clear all events
     */
    void clear() {
        for (MidiEvent* event : events) {
            pool.deallocate(event);
        }
        events.clear();
    }
    
    /**
     * @brief Get number of events
     */
    size_t size() const { return events.size(); }
    
    /**
     * @brief Check if empty
     */
    bool empty() const { return events.empty(); }
    
    /**
     * @brief Access event by index
     */
    MidiEvent& operator[](size_t index) { return *events[index]; }
    const MidiEvent& operator[](size_t index) const { return *events[index]; }
    
    /**
     * @brief Get iterator to first event
     */
    auto begin() { return events.begin(); }
    auto end() { return events.end(); }
    auto begin() const { return events.begin(); }
    auto end() const { return events.end(); }
    
    /**
     * @brief Convert to std::vector<MidiEvent> (for compatibility)
     */
    std::vector<MidiEvent> toVector() const {
        std::vector<MidiEvent> result;
        result.reserve(events.size());
        for (const MidiEvent* event : events) {
            result.push_back(*event);
        }
        return result;
    }
};

// Global pool instance
extern MidiEventPool globalMidiEventPool;

} // namespace MemoryPool 