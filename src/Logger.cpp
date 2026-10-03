//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Logger.h"
#include <stdarg.h>
#include <cstdio>
#include <cstring>
#include "MidiEvent.h"
#include "Utils/DebugSessionCapture.h"

#if defined(PIO_UNIT_TEST_NATIVE)

LogLevel Logger::currentLevel = LOG_INFO;
bool Logger::isInitialized = false;
bool Logger::categoryEnabled[] = { true, true, true, true, true, true, true, false, false, false, false };
Logger logger;

/// Native-only seam for `test_logger_gate`: the *(category, level)* pair the filter last **admitted** into
/// `log()`. Without it "the gate agrees with the message path" would be a reading of two code paths rather
/// than an assertion -- the native build prints nothing, so admission is the only observable the pair shares.
/// `lastAdmittedLevel == -1` means "nothing admitted yet" (`LogLevel` has no such value, which is why these
/// are `int`s). The firmware build has neither this state nor this seam.
namespace LoggerNativeProbe {
int lastAdmittedCategory = -1;
int lastAdmittedLevel = -1;
inline void reset() {
  lastAdmittedCategory = -1;
  lastAdmittedLevel = -1;
}
inline void recordAdmitted(LogCategory category, LogLevel level) {
  lastAdmittedCategory = static_cast<int>(category);
  lastAdmittedLevel = static_cast<int>(level);
}
}  // namespace LoggerNativeProbe

/// Second native-only seam: the **text** of the last `info()` line. The tick edge's rows (the slot-switch
/// commit and its two cancels) are evidence the host tooling greps for by exact text, and moving the *call*
/// off the edge (timing-path review, item 1) must not move the *text*: this is what makes that an assertion.
namespace LoggerNativeInfoProbe {
inline char lastLine[256] = {};
inline uint32_t lineCount = 0;
inline void reset() {
  lastLine[0] = '\0';
  lineCount = 0;
}
}  // namespace LoggerNativeInfoProbe

void Logger::setup(LogLevel level) {
  // Native builds store the filter state even though nothing is printed: `test_logger_gate` asserts the
  // gate's agreement with the message path, which is only meaningful if the state it reads is real.
  currentLevel = level;
  isInitialized = false;
}
void Logger::setCategoryEnabled(LogCategory category, bool enabled) {
  if (category >= CAT_GENERAL && category <= CAT_STORAGE) {
    categoryEnabled[category] = enabled;
  }
}
bool Logger::isEnabled(LogCategory category, LogLevel level) {
  if (currentLevel < level) return false;
  return categoryEnabled[category];
}

void Logger::printTimestamp() {}
void Logger::printLevel(LogLevel) {}
void Logger::printCategory(LogCategory) {}
void Logger::printPrefix(LogLevel, LogCategory) {}

void Logger::error(const char*, ...) {}
void Logger::warning(const char*, ...) {}
void Logger::info(const char* format, ...) {
  // Same filter as the firmware (an `info` line is emitted only when the level admits it); the *text* is
  // captured instead of printed, which is the observable the edge-effect fixtures read.
  if (currentLevel < LOG_INFO) return;
  va_list args;
  va_start(args, format);
  vsnprintf(LoggerNativeInfoProbe::lastLine, sizeof(LoggerNativeInfoProbe::lastLine), format, args);
  va_end(args);
  ++LoggerNativeInfoProbe::lineCount;
}
void Logger::debug(const char*, ...) {}
void Logger::trace(const char*, ...) {}

void Logger::log(LogCategory category, LogLevel level, const char*, ...) {
  // Same filter, no output: the native build keeps the *decision* observable (the seam below) and silences
  // the sink, so the gate above can be held to it.
  if (!isEnabled(category, level)) return;
  LoggerNativeProbe::recordAdmitted(category, level);
}
void Logger::logStateTransition(const char*, const char*, const char*) {}
void Logger::dumpMidiEvents(const MidiEventVec&, int) {}
void Logger::logMidiEvent(const MidiEvent&) {}
void Logger::logTrackEvent(const char*, uint32_t, const char*, ...) {}

#else

LogLevel Logger::currentLevel = LOG_INFO;
bool Logger::isInitialized = false;
// By default, all categories enabled except MOVE_NOTES, MIDI_LED, BAR_STEP, STORAGE (verbose)
bool Logger::categoryEnabled[] = { true, true, true, true, true, true, true, false, false, false, false };
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
  if (category >= CAT_GENERAL && category <= CAT_STORAGE) {
    categoryEnabled[category] = enabled;
  }
}

/// The message gate: `log()`'s own filter, asked in advance so a hot path can skip building a message it
/// would throw away. It is the *only* definition of that filter -- `log()` below routes through it, so the
/// two cannot drift.
bool Logger::isEnabled(LogCategory category, LogLevel level) {
  if (currentLevel < level) return false;
  return categoryEnabled[category];
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
  const char* categories[] = {"GEN", "STATE", "MIDI", "CLOCK", "TRACK", "BTN", "DISP", "MOVE", "LED", "BARSTEP", "STOR"};
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
  va_list args;
  va_start(args, format);
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
#if defined(SESSION_CAPTURE)
  if (strncmp(logBuffer, "#DBG", 4) == 0) {
    DebugSessionCapture::appendCaptureTextLine(logBuffer);
    return;
  }
#endif
  printPrefix(LOG_INFO);
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
  if (!isEnabled(category, level)) return;
  printPrefix(level, category);
  va_list args;
  va_start(args, format);
  vsnprintf(logBuffer, sizeof(logBuffer), format, args);
  va_end(args);
  Serial.println(logBuffer);
}

void Logger::logStateTransition(const char* component, const char* fromState, const char* toState) {
  SC_STATE(component, fromState, toState);
#if !defined(SESSION_CAPTURE)
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG, CAT_STATE);
  Serial.printf("%s state transition: %s -> %s", component, fromState, toState);
  Serial.println();
#endif
}

void Logger::dumpMidiEvents(const MidiEventVec& events, int trackIndex) {
  if (currentLevel < LOG_DEBUG) return;
  printPrefix(LOG_DEBUG, CAT_TRACK);
  Serial.printf("--- MIDI events dump (track=%d, count=%zu) ---\n", trackIndex, events.size());
  for (size_t i = 0; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    printPrefix(LOG_DEBUG, CAT_TRACK);
    switch (evt.type) {
      case midi::NoteOn:
        Serial.printf("  [%zu] tick=%lu NoteOn  ch=%d note=%d vel=%d\n", i, evt.tick, evt.channel, evt.data.noteData.note, evt.data.noteData.velocity);
        break;
      case midi::NoteOff:
        Serial.printf("  [%zu] tick=%lu NoteOff ch=%d note=%d vel=%d\n", i, evt.tick, evt.channel, evt.data.noteData.note, evt.data.noteData.velocity);
        break;
      case midi::ControlChange:
        Serial.printf("  [%zu] tick=%lu CC      ch=%d cc=%d val=%d\n", i, evt.tick, evt.channel, evt.data.ccData.cc, evt.data.ccData.value);
        break;
      default:
        Serial.printf("  [%zu] tick=%lu type=%d ch=%d\n", i, evt.tick, evt.type, evt.channel);
        break;
    }
  }
  printPrefix(LOG_DEBUG, CAT_TRACK);
  Serial.println("--- end dump ---");
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

#endif // !PIO_UNIT_TEST_NATIVE
