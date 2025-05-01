#include "Logger.h"
#include <stdarg.h>

LogLevel Logger::currentLevel = LOG_INFO;
bool Logger::isInitialized = false;
Logger logger;

void Logger::setup(LogLevel level) {
  currentLevel = level;
  isInitialized = true;
  Serial.begin(115200);
  info("Logger initialized with level %d", level);
}

void Logger::printTimestamp() {
  unsigned long ms = millis();
  Serial.printf("[%lu.%03lu] ", ms / 1000, ms % 1000);
}

void Logger::printLevel(LogLevel level) {
  const char* levels[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
  Serial.printf("[%s] ", levels[level]);
}

void Logger::printCategory(LogCategory category) {
  const char* categories[] = {"GEN", "STATE", "MIDI", "CLOCK", "TRACK", "BTN", "DISP"};
  Serial.printf("[%s] ", categories[category]);
}

void Logger::printPrefix(LogLevel level, LogCategory category) {
  if (!isInitialized) return;
  printTimestamp();
  printLevel(level);
  if (category != CAT_GENERAL) {
    printCategory(category);
  }
}

void Logger::error(const char* format, ...) {
  if (currentLevel < LOG_ERROR) return;
  printPrefix(LOG_ERROR);
  va_list args;
  va_start(args, format);
  Serial.printf(format, args);
  va_end(args);
  Serial.println();
}

void Logger::warning(const char* format, ...) {
  if (currentLevel < LOG_WARNING) return;
  printPrefix(LOG_WARNING);
  va_list args;
  va_start(args, format);
  Serial.printf(format, args);
  va_end(args);
  Serial.println();
}

void Logger::info(const char* format, ...) {
  if (currentLevel < LOG_INFO) return;
  printPrefix(LOG_INFO);
  va_list args;
  va_start(args, format);
  Serial.printf(format, args);
  va_end(args);
  Serial.println();
}

void Logger::debug(const char* format, ...) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG);
  va_list args;
  va_start(args, format);
  Serial.printf(format, args);
  va_end(args);
  Serial.println();
}

void Logger::trace(const char* format, ...) {
  if (currentLevel < LOG_TRACE) return;
  printPrefix(LOG_TRACE);
  va_list args;
  va_start(args, format);
  Serial.printf(format, args);
  va_end(args);
  Serial.println();
}

void Logger::log(LogCategory category, LogLevel level, const char* format, ...) {
  if (currentLevel < level) return;
  printPrefix(level, category);
  va_list args;
  va_start(args, format);
  Serial.printf(format, args);
  va_end(args);
  Serial.println();
}

void Logger::logStateTransition(const char* component, const char* fromState, const char* toState) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG, CAT_STATE);
  Serial.printf("%s state transition: %s -> %s", component, fromState, toState);
  Serial.println();
}

void Logger::logMidiEvent(const char* type, uint8_t channel, uint8_t data1, uint8_t data2) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG, CAT_MIDI);
  Serial.printf("%s: ch=%d, data1=%d, data2=%d", type, channel, data1, data2);
  Serial.println();
}

void Logger::logTrackEvent(const char* event, uint32_t tick, const char* format, ...) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG, CAT_TRACK);
  Serial.printf("%s @ tick %lu", event, tick);
  if (format) {
    Serial.print(" (");
    va_list args;
    va_start(args, format);
    Serial.printf(format, args);
    va_end(args);
    Serial.print(")");
  }
  Serial.println();
} 