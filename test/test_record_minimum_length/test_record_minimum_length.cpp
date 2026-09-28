// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include <unity.h>
#include <tuple>
#include "../../src/Logger.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/CaptureIncrementalSanity.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
using namespace CaptureIncrementalSanity;
using Pairs = std::vector<std::pair<size_t,size_t>>;
static void reset(MinimumLengthCursor& cursor) {
  size_t visits=0; MinimumLengthStep state;
  for(size_t steps=0;steps<300;++steps) {
    state=cursor.stepReset(7); visits+=cursor.lastStepUnits();
    TEST_ASSERT_LESS_OR_EQUAL(7,cursor.lastStepUnits());
    if(state!=MinimumLengthStep::Pending) break;
  }
  TEST_ASSERT_TRUE(state==MinimumLengthStep::InputReady);
  TEST_ASSERT_EQUAL(2048,visits);
}
static Pairs run(const std::vector<MidiEvent>& rows, uint32_t min=12, bool enabled=true) {
  MinimumLengthWorkspace workspace; MinimumLengthCursor cursor; uint64_t generation=1;
  TEST_ASSERT_TRUE(cursor.begin(workspace,generation,2000,min,enabled)); reset(cursor);
  Pairs pairs;
  for(const auto& row:rows) {
    auto state=cursor.consume(row);
    TEST_ASSERT_EQUAL(1,cursor.lastStepUnits());
    TEST_ASSERT_TRUE(state==MinimumLengthStep::Accepted || state==MinimumLengthStep::PairReady);
    if(state==MinimumLengthStep::PairReady) {
      size_t on=9999,off=9999;
      TEST_ASSERT_TRUE(cursor.pendingPair(on,off));
      pairs.emplace_back(on,off);
      TEST_ASSERT_FALSE(cursor.finishInput());
      TEST_ASSERT_TRUE(cursor.consume(row)==MinimumLengthStep::Denied);
      TEST_ASSERT_EQUAL(0,cursor.lastStepUnits());
      size_t againOn=9999,againOff=9999;
      TEST_ASSERT_TRUE(cursor.pendingPair(againOn,againOff));
      TEST_ASSERT_EQUAL(on,againOn); TEST_ASSERT_EQUAL(off,againOff);
      TEST_ASSERT_TRUE(cursor.acknowledgePair());
      TEST_ASSERT_FALSE(cursor.acknowledgePair());
    }
  }
  TEST_ASSERT_TRUE(cursor.finishInput()); TEST_ASSERT_TRUE(cursor.finishInput());
  TEST_ASSERT_TRUE(cursor.consume(MidiEvent::NoteOff(2000,1,60,0))==MinimumLengthStep::Denied);
  size_t on=77,off=88; TEST_ASSERT_FALSE(cursor.pendingPair(on,off));
  TEST_ASSERT_EQUAL(77,on); TEST_ASSERT_EQUAL(88,off);
  return pairs;
}
void test_strict_threshold_and_disable() {
  std::vector<MidiEvent> rows={MidiEvent::NoteOn(10,1,60,100),MidiEvent::NoteOff(21,1,60,0),
      MidiEvent::NoteOn(30,1,60,100),MidiEvent::NoteOff(42,1,60,0)};
  const Pairs expected={{0,1}}; TEST_ASSERT_TRUE(run(rows)==expected);
  TEST_ASSERT_TRUE(run(rows,12,false).empty()); TEST_ASSERT_TRUE(run(rows,0).empty());
  TEST_ASSERT_TRUE(run({}).empty());
}
void test_retrigger_pairs_latest_ordinal_not_note_id() {
  auto a=MidiEvent::NoteOn(10,1,60,100), b=MidiEvent::NoteOn(20,1,60,100), off=MidiEvent::NoteOff(25,1,60,0);
  a.noteId=1;b.noteId=2;off.noteId=1;
  const Pairs expected={{1,2}}; TEST_ASSERT_TRUE(run({a,b,off})==expected);
}
void test_long_off_clears_candidate_and_channels_are_independent() {
  TEST_ASSERT_TRUE(run({MidiEvent::NoteOn(10,1,60,100),MidiEvent::NoteOff(30,1,60,0),
      MidiEvent::NoteOff(31,1,60,0)}).empty());
  const Pairs expected={{0,3}};
  TEST_ASSERT_TRUE(run({MidiEvent::NoteOn(10,1,60,100),MidiEvent::NoteOn(11,2,60,100),
      MidiEvent::NoteOff(12,3,60,0),MidiEvent::NoteOff(13,1,60,0)})==expected);
}
void test_head_equal_tick_and_velocity_zero() {
  const Pairs expected={{1,2}};
  TEST_ASSERT_TRUE(run({MidiEvent::NoteOff(10,1,60,0),MidiEvent::NoteOn(10,1,60,100),
      MidiEvent::NoteOn(11,1,60,0),MidiEvent::ControlChange(11,1,7,90),
      MidiEvent::NoteOn(1900,1,60,100)})==expected);
}
void test_order_and_lane_denial_do_not_consume_ordinal() {
  MinimumLengthWorkspace scratch; MinimumLengthCursor cursor; uint64_t gen=3;
  TEST_ASSERT_TRUE(cursor.begin(scratch,gen,2000,12,true)); reset(cursor);
  TEST_ASSERT_TRUE(cursor.consume(MidiEvent::NoteOn(10,1,60,100))==MinimumLengthStep::Accepted);
  TEST_ASSERT_TRUE(cursor.consume(MidiEvent::NoteOff(9,1,60,0))==MinimumLengthStep::Denied);
  TEST_ASSERT_TRUE(cursor.consume(MidiEvent::NoteOff(10,1,60,0))==MinimumLengthStep::Denied);
  auto invalid=MidiEvent::NoteOn(11,1,60,100);invalid.channel=0;
  TEST_ASSERT_TRUE(cursor.consume(invalid)==MinimumLengthStep::Denied);
  invalid.channel=17; TEST_ASSERT_TRUE(cursor.consume(invalid)==MinimumLengthStep::Denied);
  invalid.channel=1;invalid.data.noteData.note=128;
  TEST_ASSERT_TRUE(cursor.consume(invalid)==MinimumLengthStep::Denied);
  TEST_ASSERT_TRUE(cursor.consume(MidiEvent::NoteOff(11,1,60,0))==MinimumLengthStep::PairReady);
  size_t on,off;TEST_ASSERT_TRUE(cursor.pendingPair(on,off));TEST_ASSERT_EQUAL(0,on);TEST_ASSERT_EQUAL(1,off);
}
void test_stale_receipts_restart_and_independent_cursors() {
  MinimumLengthWorkspace a,b;MinimumLengthCursor x,y;uint64_t gx=1,gy=2;
  TEST_ASSERT_TRUE(x.begin(a,gx,2000,12,true));
  TEST_ASSERT_FALSE(x.begin(b,gy,2000,12,true));
  TEST_ASSERT_TRUE(x.stepReset(0)==MinimumLengthStep::Denied);TEST_ASSERT_FALSE(x.finishInput());
  ++gx;TEST_ASSERT_TRUE(x.stepReset(1)==MinimumLengthStep::Denied);
  x.discard();TEST_ASSERT_TRUE(x.begin(a,gx,2000,12,true));reset(x);
  TEST_ASSERT_TRUE(y.begin(b,gy,2000,12,true));reset(y);
  const auto on=MidiEvent::NoteOn(10,1,60,100),off=MidiEvent::NoteOff(11,1,60,0);
  TEST_ASSERT_TRUE(x.consume(on)==MinimumLengthStep::Accepted);
  TEST_ASSERT_TRUE(y.consume(on)==MinimumLengthStep::Accepted);
  ++gx;TEST_ASSERT_TRUE(x.consume(off)==MinimumLengthStep::Denied);
  TEST_ASSERT_TRUE(y.consume(off)==MinimumLengthStep::PairReady);
  ++gy;size_t oi=77,fi=88;TEST_ASSERT_FALSE(y.pendingPair(oi,fi));
  TEST_ASSERT_EQUAL(77,oi);TEST_ASSERT_EQUAL(88,fi);TEST_ASSERT_FALSE(y.acknowledgePair());
  TEST_ASSERT_FALSE(y.finishInput());
  y.discard();TEST_ASSERT_TRUE(y.begin(b,gy,0,12,true));reset(y);
  TEST_ASSERT_TRUE(y.finishInput());++gy;TEST_ASSERT_FALSE(y.finishInput());
}
void test_every_midi_lane_without_2048_row_limit() {
  std::vector<MidiEvent> rows;Pairs expected;
  for(unsigned ch=1;ch<=16;++ch)for(unsigned note=0;note<128;++note)
    rows.push_back(MidiEvent::NoteOn(10,ch,note,100));
  for(unsigned ch=1;ch<=16;++ch)for(unsigned note=0;note<128;++note) {
    expected.emplace_back((ch-1)*128+note,rows.size());
    rows.push_back(MidiEvent::NoteOff(11,ch,note,0));
  }
  TEST_ASSERT_TRUE(run(rows)==expected);
}
using Row=std::tuple<uint32_t,uint8_t,uint8_t,uint8_t,uint8_t,NoteId>;
static Row signature(const MidiEvent& e) {
  return {e.tick,e.channel,static_cast<uint8_t>(e.isNoteOff()?0:e.isNoteOn()?1:2),
          e.data.noteData.note,e.data.noteData.velocity,e.noteId};
}
void test_differential_retained_inventory_against_real_helper() {
  uint32_t seed=0x31df74;
  auto next=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
  for(unsigned trial=0;trial<64;++trial) {
    std::vector<MidiEvent> rows;
    for(unsigned i=0;i<320;++i) {
      const uint32_t tick=next()%100, channel=1+next()%4, note=60+next()%4;
      auto e=(next()%2)?MidiEvent::NoteOn(tick,channel,note,100):MidiEvent::NoteOff(tick,channel,note,0);
      e.noteId=i+1;rows.push_back(e);
    }
    // Match the actual comparator's equal-key ordering, not a new NoteId policy.
    std::sort(rows.begin(),rows.end(),LoopStopFinalize::noteEventLess);
    // Give the legacy helper unique ordered ticks so its additional unstable sort
    // cannot select a different equal-key On identity for this differential oracle.
    for(size_t i=0;i<rows.size();++i) rows[i].tick=i;
    const auto pairs=run(rows);
    std::vector<bool> keep(rows.size(),true);
    for(const auto& p:pairs) {keep[p.first]=false;keep[p.second]=false;}
    std::vector<Row> expected,actual;
    for(size_t i=0;i<rows.size();++i)if(keep[i])expected.push_back(signature(rows[i]));
    LoopEventStore::resetPoolForTests();LoopEventStore store;
    for(const auto& e:rows)TEST_ASSERT_TRUE(store.append(e));
    TEST_ASSERT_EQUAL(pairs.size(),removePairsShorterThanNoteMinLength(store,2000,12,true));
    for(size_t i=0;i<store.size();++i)actual.push_back(signature(store.at(i)));
    TEST_ASSERT_TRUE(expected==actual);
  }
}
void setUp() {}
void tearDown() {}
int main() { UNITY_BEGIN();
 RUN_TEST(test_strict_threshold_and_disable);
 RUN_TEST(test_retrigger_pairs_latest_ordinal_not_note_id);
 RUN_TEST(test_long_off_clears_candidate_and_channels_are_independent);
 RUN_TEST(test_head_equal_tick_and_velocity_zero);
 RUN_TEST(test_order_and_lane_denial_do_not_consume_ordinal);
 RUN_TEST(test_stale_receipts_restart_and_independent_cursors);
 RUN_TEST(test_every_midi_lane_without_2048_row_limit);
 RUN_TEST(test_differential_retained_inventory_against_real_helper);
 return UNITY_END(); }
