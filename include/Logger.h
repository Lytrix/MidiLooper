#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include "MidiEvent.h"

// Log levels
enum LogLevel {
  LOG_ERROR = 0,
  LOG_WARNING = 1,
  LOG_INFO = 2,
  LOG_DEBUG = 3,
  LOG_TRACE = 4
};

// Log categories
enum LogCategory {
  CAT_GENERAL = 0,
  CAT_STATE = 1,
  CAT_MIDI = 2,
  CAT_CLOCK = 3,
  CAT_TRACK = 4,
  CAT_BUTTON = 5,
  CAT_DISPLAY = 6
};

/**
 * @class Logger
 * @brief Central logging utility with leveled and categorized message output.
 *
 * Provides static methods to log messages at various levels (ERROR, WARNING, INFO, DEBUG, TRACE)
 * and categories (general, state, midi, clock, track, button, display). Supports:
 *  - Formatted printf-style messages
 *  - Category-specific logging via log()
 *  - State transition logging
 *  - MIDI and track event logging with structured information
 *
 * The log level can be configured at runtime using setup(), and output is
 * prefixed with timestamps, levels, and categories.
 */
class Logger {
public:
  static void setup(LogLevel level = LOG_INFO);
  
  // Log methods for different levels
  static void error(const char* format, ...);
  static void warning(const char* format, ...);
  static void info(const char* format, ...);
  static void debug(const char* format, ...);
  static void trace(const char* format, ...);

  // Category-specific logging
  static void log(LogCategory category, LogLevel level, const char* format, ...);
  
  // State transition logging
  static void logStateTransition(const char* component, const char* fromState, const char* toState);
  
  // MIDI event logging
  static void logMidiEvent(const MidiEvent& evt);
  
  // Track event logging
  static void logTrackEvent(const char* event, uint32_t tick, const char* format = nullptr, ...);

private:
  static LogLevel currentLevel;
  static bool isInitialized;
  
  static void printPrefix(LogLevel level, LogCategory category = CAT_GENERAL);
  static void printTimestamp();
  static void printCategory(LogCategory category);
  static void printLevel(LogLevel level);
};

// Global logger instance
extern Logger logger;

#endif // LOGGER_H 