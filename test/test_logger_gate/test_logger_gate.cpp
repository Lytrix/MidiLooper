//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Timing-path review, items 1 and 4: the two things the tick edge and the playback hot path now *ask* before
// they pay for anything.
//
//   * `Logger::isEnabled` is the gate `Track::sendMidiEvent` uses so the per-note trace line's note-name
//     lookup and octave arithmetic are not built for a message the logger would discard. A gate like that is
//     only correct if it is the **same** filter the message path applies, so the fixture asserts the
//     agreement rather than the predicate's shape: exactly the cases `log()` admits are the cases the gate
//     says yes to -- including the boundary where the level is equal, which is where an off-by-one would live.
//   * `MidiNoteName` owns the arithmetic the trace line prints, so the gate cannot buy its saving by printing
//     something else: the pair is pinned for the bottom, middle and top of the note range.
//
// The suite links the real `Logger.cpp`, whose native branch records the *(category, level)* the filter
// *admitted* (`LoggerNativeProbe`) instead of printing, so "the gate agrees with the message path" is an
// assertion about behaviour and not a reading of two code paths.

#include <unity.h>

#include <cstring>

#include "../../src/Logger.cpp"
#include "Utils/MidiNoteName.h"

namespace {

/// The agreement, stated once so every case reads the same way: the gate and the message path must answer
/// identically, and the message path's answer is what the native seam recorded.
void assertGateAgreesWithMessagePath(LogCategory category, LogLevel level) {
  LoggerNativeProbe::reset();
  logger.log(category, level, "probe");
  const bool messagePathAdmitted = LoggerNativeProbe::lastAdmittedLevel != -1;
  const bool gateAdmits = Logger::isEnabled(category, level);
  TEST_ASSERT_EQUAL_MESSAGE(messagePathAdmitted, gateAdmits,
                            "the gate must admit exactly what the message path admits");
}

}  // namespace

void test_gate_agrees_at_the_level_boundary_and_above() {
  Logger::setCategoryEnabled(CAT_MIDI, true);
  logger.setup(LOG_TRACE);
  // Equal is admitted (`log()` fails only when `currentLevel < level`), and the gate must not be stricter.
  assertGateAgreesWithMessagePath(CAT_MIDI, LOG_TRACE);
  TEST_ASSERT_TRUE(Logger::isEnabled(CAT_MIDI, LOG_TRACE));
  assertGateAgreesWithMessagePath(CAT_MIDI, LOG_INFO);
  TEST_ASSERT_TRUE(Logger::isEnabled(CAT_MIDI, LOG_INFO));
  logger.setup(LOG_INFO);
}

void test_gate_refuses_what_the_level_filters_out() {
  Logger::setCategoryEnabled(CAT_MIDI, true);
  logger.setup(LOG_INFO);
  // The production level (`main.cpp` sets INFO/DEBUG, never TRACE): the trace line is built and discarded.
  assertGateAgreesWithMessagePath(CAT_MIDI, LOG_TRACE);
  TEST_ASSERT_FALSE(Logger::isEnabled(CAT_MIDI, LOG_TRACE));
  // A level the logger does emit stays admitted -- the gate is not a blanket "no".
  assertGateAgreesWithMessagePath(CAT_MIDI, LOG_INFO);
  TEST_ASSERT_TRUE(Logger::isEnabled(CAT_MIDI, LOG_INFO));
}

void test_gate_honours_the_category_flag_at_an_admitted_level() {
  logger.setup(LOG_TRACE);
  Logger::setCategoryEnabled(CAT_MIDI, false);
  assertGateAgreesWithMessagePath(CAT_MIDI, LOG_TRACE);
  TEST_ASSERT_FALSE_MESSAGE(Logger::isEnabled(CAT_MIDI, LOG_TRACE),
                            "an admitted level must not admit a disabled category");
  Logger::setCategoryEnabled(CAT_MIDI, true);
  assertGateAgreesWithMessagePath(CAT_MIDI, LOG_TRACE);
  TEST_ASSERT_TRUE(Logger::isEnabled(CAT_MIDI, LOG_TRACE));
  logger.setup(LOG_INFO);
}

void test_note_name_pair_matches_the_printed_trace_fields() {
  // Pitch classes, in the order the trace table printed them.
  TEST_ASSERT_EQUAL_STRING("C", MidiNoteName::pitchClassName(0));
  TEST_ASSERT_EQUAL_STRING("C#", MidiNoteName::pitchClassName(1));
  TEST_ASSERT_EQUAL_STRING("A#", MidiNoteName::pitchClassName(10));
  TEST_ASSERT_EQUAL_STRING("B", MidiNoteName::pitchClassName(11));
  TEST_ASSERT_EQUAL_STRING("C", MidiNoteName::pitchClassName(12));
  // The octave convention: note 60 is C4, and the bottom of the range is C-1.
  TEST_ASSERT_EQUAL_INT(-1, MidiNoteName::octave(0));
  TEST_ASSERT_EQUAL_INT(4, MidiNoteName::octave(60));
  TEST_ASSERT_EQUAL_STRING("C", MidiNoteName::pitchClassName(60));
  TEST_ASSERT_EQUAL_INT_MESSAGE(9, MidiNoteName::octave(127), "the top of the MIDI range is G9");
  TEST_ASSERT_EQUAL_STRING("G", MidiNoteName::pitchClassName(127));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_gate_agrees_at_the_level_boundary_and_above);
  RUN_TEST(test_gate_refuses_what_the_level_filters_out);
  RUN_TEST(test_gate_honours_the_category_flag_at_an_admitted_level);
  RUN_TEST(test_note_name_pair_matches_the_printed_trace_fields);
  return UNITY_END();
}
