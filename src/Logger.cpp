//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Logger.h"
#include <stdarg.h>
#include "MidiEvent.h"

LogLevel Logger::currentLevel = LOG_INFO;
bool Logger::isInitialized = false;
// By default, all categories enabled except MOVE_NOTES
bool Logger::categoryEnabled[] = { true, true, true, true, true, true, true, false };
Logger logger;

static char logBuffer[128];  // Shared buffer for formatted log output

void Logger::setup(LogLevel level) {
  currentLevel = level;
  isInitialized = true;
  Serial.begin(115200);
  Serial.print("Logger initialized with level: ");
  Serial.println(level);
}

// Enable or disable logging for a given category
void Logger::setCategoryEnabled(LogCategory category, bool enabled) {
  if (category >= CAT_GENERAL && category <= CAT_MOVE_NOTES) {
    categoryEnabled[category] = enabled;
  }
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
  const char* categories[] = {"GEN", "STATE", "MIDI", "CLOCK", "TRACK", "BTN", "DISP", "MOVE"};
  if (category < (int)(sizeof(categories)/sizeof(categories[0]))) {
    Serial.printf("[%s] ", categories[category]);
  }
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
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
  Serial.println(logBuffer);
}

void Logger::warning(const char* format, ...) {
  if (currentLevel < LOG_WARNING) return;
  printPrefix(LOG_WARNING);
  va_list args;
  va_start(args, format);
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
  Serial.println(logBuffer);
}

void Logger::info(const char* format, ...) {
  if (currentLevel < LOG_INFO) return;
  printPrefix(LOG_INFO);
  va_list args;
  va_start(args, format);
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
  Serial.println(logBuffer);
}

void Logger::debug(const char* format, ...) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG);
  va_list args;
  va_start(args, format);
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
  Serial.println(logBuffer);
}

void Logger::trace(const char* format, ...) {
  if (currentLevel < LOG_TRACE) return;
  printPrefix(LOG_TRACE);
  va_list args;
  va_start(args, format);
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
  Serial.println(logBuffer);
}

void Logger::log(LogCategory category, LogLevel level, const char* format, ...) {
  if (currentLevel < level) return;
  if (!categoryEnabled[category]) return;
  printPrefix(level, category);
  va_list args;
  va_start(args, format);
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
  Serial.println(logBuffer);
}

void Logger::logStateTransition(const char* component, const char* fromState, const char* toState) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG, CAT_STATE);
  Serial.printf("%s state transition: %s -> %s", component, fromState, toState);
  Serial.println();
}

void Logger::logMidiEvent(const MidiEvent& evt) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG, CAT_MIDI);
  switch (evt.type) {
    case midi::NoteOn:
      Serial.printf("NoteOn: ch=%d, note=%d, vel=%d", evt.channel, evt.data.noteData.note, evt.data.noteData.velocity);
      break;
    case midi::NoteOff:
      Serial.printf("NoteOff: ch=%d, note=%d, vel=%d", evt.channel, evt.data.noteData.note, evt.data.noteData.velocity);
      break;
    case midi::ControlChange:
      Serial.printf("ControlChange: ch=%d, cc=%d, val=%d", evt.channel, evt.data.ccData.cc, evt.data.ccData.value);
      break;
    case midi::ProgramChange:
      Serial.printf("ProgramChange: ch=%d, program=%d", evt.channel, evt.data.program);
      break;
    case midi::PitchBend:
      Serial.printf("PitchBend: ch=%d, value=%d", evt.channel, evt.data.pitchBend);
      break;
    case midi::AfterTouchChannel:
      Serial.printf("AfterTouch: ch=%d, pressure=%d", evt.channel, evt.data.channelPressure);
      break;
    default:
      Serial.printf("MIDI type=%d, ch=%d", evt.type, evt.channel);
      break;
  }
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
    vsnprintf(logBuffer, sizeof(logBuffer), format, args);
    va_end(args);
    Serial.print(logBuffer);
    Serial.print(")");
  }
  Serial.println();
}
