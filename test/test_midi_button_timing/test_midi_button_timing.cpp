// Production DROID gesture classification, driven by the existing native clock.
#include <unity.h>
#include "../test_support/firmware/Arduino.h"
#include "../../src/Logger.cpp"
#include "../../src/Utils/MidiButtonConfig.cpp"
#include "../../src/MidiButtonProcessor.cpp"

using Press = MidiButtonConfig::PressType;
struct Recognition { uint8_t note, channel; Press type; };
static std::vector<Recognition> recognized;
static MidiButtonProcessor processor;
static constexpr uint8_t channel = MidiConfig::Channels::SELECT;
static constexpr uint8_t loopNote = MidiConfig::Led::LOOP_SELECT_LED_BASE + 4;
static constexpr uint8_t recordNote = MidiConfig::Transport::NOTE_RECORD;

void setUp() {
    nativeMillisSet(1000);
    MidiButtonConfig::Config::loadConfiguration();
    processor.setup();
    recognized.clear();
    processor.setButtonPressCallback([](uint8_t note, uint8_t ch, Press type) {
        recognized.push_back({note, ch, type});
    });
}
void tearDown() {}
static void advance(uint32_t ms) { nativeMillisAdvance(ms); processor.update(); }
static void press(uint8_t note = loopNote) { processor.handleMidiNote(channel, note, 127, true); }
static void release(uint8_t note = loopNote) { processor.handleMidiNote(channel, note, 0, false); }
static void tap(uint8_t note = loopNote) { press(note); advance(120); release(note); }
static void expect(size_t count, Press type = Press::SHORT_PRESS) {
    TEST_ASSERT_EQUAL_UINT(count, recognized.size());
    if (count) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(type), static_cast<int>(recognized.back().type));
        TEST_ASSERT_EQUAL_UINT(channel - 1, recognized.back().channel);
    }
}
void test_production_mapping_is_not_momentary() {
    const auto* cfg = MidiButtonConfig::Config::findButtonConfig(loopNote, channel - 1);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_FALSE(cfg->isMomentary);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MidiButtonConfig::ActionType::TOGGLE_RECORD_FOR_SLOT), static_cast<int>(cfg->shortPressAction));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MidiButtonConfig::ActionType::CLEAR_TRACK_FOR_SLOT), static_cast<int>(cfg->longPressAction));
    TEST_ASSERT_EQUAL_INT(4, cfg->parameter);
}
void test_short_requires_recognition_after_release() {
    press(); expect(0); advance(120); release(); expect(0);
    advance(299); expect(0); advance(1); expect(1);
    TEST_ASSERT_EQUAL_UINT(loopNote, recognized.back().note);
    advance(1000); expect(1);
}
void test_early_followup_is_double_not_two_shorts() {
    tap(); advance(100); tap(); expect(0);
    advance(399); expect(0); advance(1); expect(1, Press::DOUBLE_PRESS);
}
void test_three_taps_recognize_one_triple() {
    tap(); advance(50); tap(); advance(50); tap(); expect(1, Press::TRIPLE_PRESS);
    advance(500); expect(1, Press::TRIPLE_PRESS);
}
void test_repeat_after_recognition_is_two_shorts() {
    tap(); advance(300); expect(1);
    tap(); advance(299); expect(1); advance(1); expect(2);
}
void test_long_press_clear_is_release_driven_once() {
    press(); advance(600); expect(0); release(); expect(1, Press::LONG_PRESS);
    advance(1000); release(); expect(1, Press::LONG_PRESS);
}
void test_just_below_long_threshold_is_short() {
    press(); advance(599); release(); expect(0); advance(300); expect(1);
}
void test_orphan_release_cannot_clear_or_record() {
    release(); advance(1000); expect(0);
}
void test_other_control_does_not_finish_pending_loop_tap() {
    tap(); advance(10); tap(recordNote); expect(0);
    advance(169); expect(0); advance(1); expect(1);
    TEST_ASSERT_EQUAL_UINT(loopNote, recognized.back().note);
    advance(130); expect(2);
    TEST_ASSERT_EQUAL_UINT(recordNote, recognized.back().note);
}
void test_third_tap_after_expiry_is_a_new_short() {
    tap(); advance(50); tap(); advance(400); expect(1, Press::DOUBLE_PRESS);
    tap(); advance(300); expect(2, Press::SHORT_PRESS);
}
void test_long_press_cancels_pending_short() {
    tap(); advance(20); press();
    // Release before servicing pending work: the long classification cancels the pending tap.
    nativeMillisAdvance(600); release(); expect(1, Press::LONG_PRESS);
    advance(1000); expect(1, Press::LONG_PRESS);
}
void test_velocity_zero_note_on_is_release() {
    press(); advance(120); processor.handleMidiNote(channel, loopNote, 0, true);
    expect(0); advance(300); expect(1);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_production_mapping_is_not_momentary);
    RUN_TEST(test_short_requires_recognition_after_release);
    RUN_TEST(test_early_followup_is_double_not_two_shorts);
    RUN_TEST(test_three_taps_recognize_one_triple);
    RUN_TEST(test_repeat_after_recognition_is_two_shorts);
    RUN_TEST(test_long_press_clear_is_release_driven_once);
    RUN_TEST(test_just_below_long_threshold_is_short);
    RUN_TEST(test_orphan_release_cannot_clear_or_record);
    RUN_TEST(test_other_control_does_not_finish_pending_loop_tap);
    RUN_TEST(test_velocity_zero_note_on_is_release);
    RUN_TEST(test_third_tap_after_expiry_is_a_new_short);
    RUN_TEST(test_long_press_cancels_pending_short);
    return UNITY_END();
}
