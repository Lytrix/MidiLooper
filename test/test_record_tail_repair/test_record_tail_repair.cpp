#include <unity.h>
#include <tuple>
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/LoopStopFinalize.cpp"
using namespace LoopStopFinalize;
using Inventory = std::vector<std::tuple<uint32_t, uint8_t, uint8_t>>;
static Inventory run(const std::vector<MidiEvent>& input, uint32_t length=2000,
                     uint32_t close=UINT32_MAX, uint32_t window=768) {
  TailRepairWorkspace scratch;
  TailRepairCursor cursor;
  uint64_t generation=1;
  TEST_ASSERT_TRUE(cursor.begin(scratch,generation,length,close,window));
  size_t visits=0;
  TailRepairStep state;
  do { state=cursor.stepReset(7); visits+=cursor.lastStepUnits();
    TEST_ASSERT_LESS_OR_EQUAL(7,cursor.lastStepUnits());
  } while(state==TailRepairStep::Pending);
  TEST_ASSERT_EQUAL(2048,visits);
  TEST_ASSERT_TRUE(state==TailRepairStep::InputReady);
  for(const auto& event:input) { TEST_ASSERT_TRUE(cursor.consume(event)); TEST_ASSERT_EQUAL(1,cursor.lastStepUnits()); }
  TEST_ASSERT_TRUE(cursor.finishInput());
  TEST_ASSERT_FALSE(cursor.finishInput());
  Inventory result; visits=0;
  for(size_t turns=0;turns<5000;++turns) {
    MidiEvent event=MidiEvent::NoteOn(123,1,1,1);
    state=cursor.stepOutput(7,event); visits+=cursor.lastStepUnits();
    TEST_ASSERT_LESS_OR_EQUAL(7,cursor.lastStepUnits());
    if(state==TailRepairStep::EventReady) {
      TEST_ASSERT_TRUE(event.isNoteOff());
      result.emplace_back(event.tick,event.channel,event.data.noteData.note);
      MidiEvent retry;
      TEST_ASSERT_TRUE(cursor.stepOutput(7,retry)==TailRepairStep::EventReady);
      TEST_ASSERT_EQUAL(0,cursor.lastStepUnits());
      TEST_ASSERT_EQUAL(event.tick,retry.tick);
      TEST_ASSERT_EQUAL(event.channel,retry.channel);
      TEST_ASSERT_EQUAL(event.data.noteData.note,retry.data.noteData.note);
      TEST_ASSERT_TRUE(cursor.acknowledgeOutput());
      TEST_ASSERT_FALSE(cursor.acknowledgeOutput());
    } else { TEST_ASSERT_EQUAL(123,event.tick); }
    if(state==TailRepairStep::Complete) break;
  }
  TEST_ASSERT_TRUE(state==TailRepairStep::Complete);
  TEST_ASSERT_EQUAL(2048,visits);
  std::sort(result.begin(),result.end()); return result;
}
static void expect(const std::vector<MidiEvent>& input, Inventory expected,
                   uint32_t length=2000,uint32_t close=UINT32_MAX,uint32_t window=768) {
  TEST_ASSERT_TRUE(run(input,length,close,window)==expected);
}
void test_tail_phase_and_source_order() {
  const auto on=MidiEvent::NoteOn(1500,1,60,100), off=MidiEvent::NoteOff(1500,1,60,0);
  expect({on},{{1800,1,60}},2000,1800);
  expect({on},{{1999,1,60}},2000,1400);
  expect({off,on},{{1999,1,60}});
  expect({on,off},{});
  expect({on,MidiEvent::NoteOn(1300,1,60,100)},{{1400,1,60}},2000,1400);
  expect({on,MidiEvent::NoteOff(1600,2,60,0)},{{1999,1,60}});
}
void test_wrap_strict_half_and_later_off() {
  expect({MidiEvent::NoteOff(500,1,60,0),MidiEvent::NoteOn(1500,1,60,100)},{{1999,1,60}});
  expect({MidiEvent::NoteOff(499,1,60,0),MidiEvent::NoteOn(1500,1,60,100)},{});
  expect({MidiEvent::NoteOff(2200,1,60,0),MidiEvent::NoteOn(1500,1,60,100)},{});
  expect({MidiEvent::NoteOn(1500,1,60,100),MidiEvent::NoteOn(1600,1,60,0)},{});
}
void test_small_empty_and_zero_window() {
  expect({},{});
  expect({MidiEvent::NoteOn(0,1,0,100)},{{0,1,0}},1);
  expect({MidiEvent::NoteOn(99,16,127,100)},{{99,16,127}},100);
  expect({MidiEvent::NoteOn(1500,1,60,100)}, {},2000,UINT32_MAX,0);
}
void test_stale_and_invalid_transitions() {
  TailRepairWorkspace scratch; TailRepairCursor cursor; uint64_t gen=1;
  MidiEvent out=MidiEvent::NoteOn(77,1,1,100);
  TEST_ASSERT_FALSE(cursor.begin(scratch,gen,0));
  TEST_ASSERT_TRUE(cursor.begin(scratch,gen,2000));
  TEST_ASSERT_FALSE(cursor.finishInput());
  TEST_ASSERT_TRUE(cursor.stepReset(0)==TailRepairStep::Denied);
  ++gen; TEST_ASSERT_TRUE(cursor.stepReset(1)==TailRepairStep::Denied);
  cursor.discard(); TEST_ASSERT_TRUE(cursor.begin(scratch,gen,2000));
  TEST_ASSERT_TRUE(cursor.stepReset(2048)==TailRepairStep::InputReady);
  auto invalid=MidiEvent::NoteOn(1500,1,60,100); invalid.channel=0;
  TEST_ASSERT_FALSE(cursor.consume(invalid)); invalid.channel=17;
  TEST_ASSERT_FALSE(cursor.consume(invalid)); invalid.channel=1; invalid.data.noteData.note=128;
  TEST_ASSERT_FALSE(cursor.consume(invalid));
  TEST_ASSERT_TRUE(cursor.consume(MidiEvent::NoteOn(1500,1,60,100)));
  ++gen; TEST_ASSERT_FALSE(cursor.finishInput()); --gen;
  TEST_ASSERT_TRUE(cursor.finishInput());
  TEST_ASSERT_TRUE(cursor.stepOutput(2048,out)==TailRepairStep::EventReady);
  ++gen; TEST_ASSERT_FALSE(cursor.acknowledgeOutput());
  out.tick=77; TEST_ASSERT_TRUE(cursor.stepOutput(1,out)==TailRepairStep::Denied);
  TEST_ASSERT_EQUAL(77,out.tick);
}
void test_all_keys_and_more_than_2048_rows() {
  std::vector<MidiEvent> input; Inventory expected;
  for(unsigned ch=1;ch<=16;++ch) for(unsigned note=0;note<128;++note) {
    input.push_back(MidiEvent::NoteOn(1400,ch,note,100));
    input.push_back(MidiEvent::NoteOn(1500,ch,note,100));
    expected.emplace_back(1999,ch,note);
  }
  expect(input,expected);
}
void test_differential_against_real_store_policy() {
  uint32_t random=13;
  auto next=[&](){random=random*1664525u+1013904223u; return random;};
  for(unsigned trial=0;trial<80;++trial) {
    LoopEventStore::resetPoolForTests(); LoopEventStore store;
    std::vector<MidiEvent> input;
    const uint32_t length=1+next()%3000, window=next()%1000, close=next()%(length+10);
    for(unsigned i=0;i<300;++i) {
      const auto tick=next()%(length+100); const auto channel=1+next()%16; const auto note=next()%128;
      auto event=(next()&0x100)?MidiEvent::NoteOn(tick,channel,note,100):MidiEvent::NoteOff(tick,channel,note,0);
      input.push_back(event); TEST_ASSERT_TRUE(store.append(event));
    }
    const size_t before=store.size();
    finalizeWrapWindowOnStore(store,length,close,window);
    Inventory expected;
    for(size_t i=before;i<store.size();++i) { const auto& e=store.at(i); expected.emplace_back(e.tick,e.channel,e.data.noteData.note); }
    std::sort(expected.begin(),expected.end());
    expect(input,expected,length,close,window);
  }
}
void setUp() {}
void tearDown() {}
int main() { UNITY_BEGIN();
 RUN_TEST(test_tail_phase_and_source_order);
 RUN_TEST(test_wrap_strict_half_and_later_off);
 RUN_TEST(test_small_empty_and_zero_window);
 RUN_TEST(test_stale_and_invalid_transitions);
 RUN_TEST(test_all_keys_and_more_than_2048_rows);
 RUN_TEST(test_differential_against_real_store_policy);
 return UNITY_END(); }
