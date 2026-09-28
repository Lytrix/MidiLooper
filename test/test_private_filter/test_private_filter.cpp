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
using F = LoopEventStoreFilterResult;
using namespace CaptureIncrementalSanity;
using Rows = std::vector<MidiEvent>;
static Rows rows(size_t n) {
  Rows result;
  for(size_t i=0;i<n;++i) {
    auto e=MidiEvent::ControlChange(i,1+i%16,i%128,(i*3)%128);
    e.noteId=i+1;result.push_back(e);
  }
  return result;
}
static void fill(LoopEventStore& store,const Rows& input) {
  LoopEventStore::enterEphemeralSeal();
  for(const auto& e:input)TEST_ASSERT_TRUE(store.append(e));
  LoopEventStore::leaveEphemeralSeal();
}
static void same(const MidiEvent& a,const MidiEvent& b) {
  TEST_ASSERT_EQUAL(a.tick,b.tick);TEST_ASSERT_EQUAL(a.type,b.type);
  TEST_ASSERT_EQUAL(a.channel,b.channel);TEST_ASSERT_EQUAL(a.noteId,b.noteId);
  TEST_ASSERT_EQUAL(a.data.noteData.note,b.data.noteData.note);
  TEST_ASSERT_EQUAL(a.data.noteData.velocity,b.data.noteData.velocity);
}
static void inventory(const LoopEventStore& store,const Rows& expected) {
  TEST_ASSERT_EQUAL(expected.size(),store.size());
  for(size_t i=0;i<expected.size();++i)same(expected[i],store.at(i));
}
static void ready(LoopEventStore& store,LoopEventStoreFilterCursor& c) {
  TEST_ASSERT_TRUE(store.beginPrivateFilter(c));
  TEST_ASSERT_FALSE(store.beginPrivateFilter(c));
  TEST_ASSERT_FALSE(store.markPrivateFilterPair(c,0,0));
  TEST_ASSERT_FALSE(store.finishPrivateFilter(c));
  TEST_ASSERT_EQUAL(F::Denied,store.stepPrivateFilter(c,0));
  size_t units=0;
  for(size_t i=0;i<store.size()+store.chunkIds().size()+4;++i) {
    auto r=store.stepPrivateFilter(c,1);units+=c.lastStepUnits();
    TEST_ASSERT_LESS_OR_EQUAL(1,c.lastStepUnits());TEST_ASSERT_FALSE(c.complete());
    TEST_ASSERT_TRUE(r==F::Pending || r==F::InputReady);
    if(r==F::InputReady) {
      TEST_ASSERT_EQUAL(store.size()+store.chunkIds().size()+2,units);
      return;
    }
  }
  TEST_FAIL_MESSAGE("filter preflight exceeded work bound");
}
static void compact(LoopEventStore& store,LoopEventStoreFilterCursor& c,const Rows& expected,size_t quantum) {
  const auto ids=store.chunkIds();const auto capacity=ids.size();
  const auto directoryCapacity=store.chunkIds().capacity();const size_t original=store.size();
  TEST_ASSERT_TRUE(store.finishPrivateFilter(c));TEST_ASSERT_FALSE(store.finishPrivateFilter(c));
  TEST_ASSERT_FALSE(store.markPrivateFilterPair(c,0,0));
  size_t units=0;
  for(size_t i=0;i<original+capacity+5;++i) {
    const auto r=store.stepPrivateFilter(c,quantum);units+=c.lastStepUnits();
    TEST_ASSERT_LESS_OR_EQUAL(quantum,c.lastStepUnits());
    TEST_ASSERT_TRUE(r==F::Pending || r==F::Complete);
    TEST_ASSERT_EQUAL(directoryCapacity,store.chunkIds().capacity());
    // Every partial retirement retains the same prefix; no middle erase.
    for(size_t j=0;j<ids.size();++j) {
      TEST_ASSERT_EQUAL(j<store.chunkIds().size()?1:0,LoopEventStore::chunkReferenceCount(ids[j]));
      if(j<store.chunkIds().size())TEST_ASSERT_EQUAL(ids[j],store.chunkIds()[j]);
    }
    if(r==F::Complete) {
      TEST_ASSERT_TRUE(c.complete());TEST_ASSERT_EQUAL(original+capacity+3,units);
      TEST_ASSERT_EQUAL((expected.size()+255)/256,store.chunkIds().size());
      inventory(store,expected);
      for(size_t j=0;j<store.chunkIds().size();++j) {
        uint32_t first,last;const size_t end=std::min(expected.size(),(j+1)*256);
        TEST_ASSERT_EQUAL(end-j*256,LoopEventStore::chunkUsedCount(ids[j]));
        TEST_ASSERT_TRUE(LoopEventStore::chunkTickSpan(ids[j],first,last));
        TEST_ASSERT_EQUAL(expected[j*256].tick,first);TEST_ASSERT_EQUAL(expected[end-1].tick,last);
      }
      TEST_ASSERT_EQUAL(F::Complete,store.stepPrivateFilter(c,1));TEST_ASSERT_EQUAL(0,c.lastStepUnits());
      return;
    }
    TEST_ASSERT_FALSE(c.complete());
  }
  TEST_FAIL_MESSAGE("filter compaction exceeded work bound");
}
void test_stable_compaction_sizes_retries_and_tail_retirement() {
  for(size_t n:{0u,1u,255u,256u,257u,4097u})for(unsigned shape=0;shape<3;++shape) {
    LoopEventStore::resetPoolForTests();LoopEventStore store;const auto input=rows(n);fill(store,input);
    LoopEventStoreFilterCursor c;ready(store,c);Rows expected;
    for(size_t i=0;i<n;++i) {
      MidiEvent e;TEST_ASSERT_TRUE(store.readPrivateFilterRow(c,i,e));same(input[i],e);
      const bool remove=shape==1 || (shape==2 && i%3==1);
      if(remove) {
        TEST_ASSERT_TRUE(store.markPrivateFilterPair(c,i,i));
        TEST_ASSERT_TRUE(store.markPrivateFilterPair(c,i,i));
        e.tick=123;TEST_ASSERT_FALSE(store.readPrivateFilterRow(c,i,e));TEST_ASSERT_EQUAL(123,e.tick);
      } else expected.push_back(input[i]);
    }
    compact(store,c,expected,shape==0?7:1);
  }
}
void test_pair_mark_validates_both_targets_before_writing() {
  LoopEventStore::resetPoolForTests();LoopEventStore store;const auto input=rows(257);fill(store,input);
  LoopEventStoreFilterCursor c;ready(store,c);
  TEST_ASSERT_FALSE(store.markPrivateFilterPair(c,0,257));inventory(store,input);
  TEST_ASSERT_FALSE(store.markPrivateFilterPair(c,SIZE_MAX,0));inventory(store,input);
  TEST_ASSERT_TRUE(store.markPrivateFilterPair(c,0,256));
  TEST_ASSERT_TRUE(store.markPrivateFilterPair(c,0,256));
  compact(store,c,Rows(input.begin()+1,input.end()-1),7);
}
void test_minimum_decisions_compact_against_real_legacy_policy() {
  uint32_t random=0x8da573;
  auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
  for(unsigned trial=0;trial<32;++trial) {
    LoopEventStore::resetPoolForTests();LoopEventStore prepared,legacy;Rows input;
    for(size_t i=0;i<2056;++i) {
      const auto ch=1+next()%4,note=60+next()%4;
      auto e=next()%3?MidiEvent::NoteOn(i,ch,note,90):MidiEvent::NoteOff(i,ch,note,0);
      if(i%11==0)e=MidiEvent::ControlChange(i,ch,7,next()%128);
      e.noteId=i+1;input.push_back(e);
    }
    fill(prepared,input);fill(legacy,input);
    removePairsShorterThanNoteMinLength(legacy,4096,12,trial!=0);
    Rows expected;for(size_t i=0;i<legacy.size();++i)expected.push_back(legacy.at(i));
    LoopEventStoreFilterCursor c;ready(prepared,c);
    MinimumLengthWorkspace workspace;MinimumLengthCursor policy;uint64_t generation=1;
    TEST_ASSERT_TRUE(policy.begin(workspace,generation,4096,12,trial!=0));
    while(policy.stepReset(7)==MinimumLengthStep::Pending){}
    for(size_t i=0;i<input.size();++i) {
      MidiEvent e;TEST_ASSERT_TRUE(prepared.readPrivateFilterRow(c,i,e));
      const auto result=policy.consume(e);
      TEST_ASSERT_TRUE(result==MinimumLengthStep::Accepted || result==MinimumLengthStep::PairReady);
      if(result==MinimumLengthStep::PairReady) {
        size_t on,off;TEST_ASSERT_TRUE(policy.pendingPair(on,off));
        TEST_ASSERT_TRUE(prepared.markPrivateFilterPair(c,on,off));
        TEST_ASSERT_TRUE(policy.acknowledgePair());
      }
    }
    TEST_ASSERT_TRUE(policy.finishInput());compact(prepared,c,expected,7);
  }
}
void test_persistence_shared_and_corrupt_input_deny_before_marks() {
  for(unsigned mode=0;mode<5;++mode) {
    LoopEventStore::resetPoolForTests();LoopEventStore store;auto input=rows(257);fill(store,input);
    const auto id=store.chunkIds().front();
    if(mode<3)TEST_ASSERT_TRUE(PersistenceQueue::admitSealedChunk(id));
    if(mode==1){uint16_t writing;TEST_ASSERT_TRUE(PersistenceQueue::beginWriteQueuedChunk(writing));}
    if(mode==2)PersistenceQueue::markChunkPersisted(id);
    if(mode==3)LoopEventStore::retainChunkReferenceForTests(id);
    if(mode==4){input.back().tick=UINT32_MAX;const_cast<MidiEvent&>(store.at(256)).tick=UINT32_MAX;}
    LoopEventStoreFilterCursor c;TEST_ASSERT_TRUE(store.beginPrivateFilter(c));F result=F::Pending;
    for(unsigned i=0;i<300 && result==F::Pending;++i)result=store.stepPrivateFilter(c,1);
    TEST_ASSERT_EQUAL(F::Denied,result);TEST_ASSERT_FALSE(c.complete());
    TEST_ASSERT_FALSE(store.markPrivateFilterPair(c,0,1));inventory(store,input);
    if(mode==3){CaptureChunkIdList refs{id};LoopEventStore::releaseChunkRefs(refs);}
  }
}
void test_stale_owner_pool_append_and_foreign_receipts() {
  for(unsigned mode=0;mode<4;++mode) {
    LoopEventStore::resetPoolForTests();LoopEventStore store,other;const auto input=rows(257);fill(store,input);
    LoopEventStoreFilterCursor c;ready(store,c);
    if(mode==0){store.clear();fill(store,input);}
    if(mode==1){LoopEventStore::resetPoolForTests();fill(other,input);}
    if(mode==2)TEST_ASSERT_TRUE(store.append(MidiEvent::ControlChange(999,1,7,99)));
    LoopEventStore& target=mode==3?other:store;MidiEvent out;out.tick=123;
    TEST_ASSERT_FALSE(target.readPrivateFilterRow(c,0,out));TEST_ASSERT_EQUAL(123,out.tick);
    TEST_ASSERT_FALSE(target.markPrivateFilterPair(c,0,1));TEST_ASSERT_FALSE(target.finishPrivateFilter(c));
    TEST_ASSERT_EQUAL(F::Denied,target.stepPrivateFilter(c,1));TEST_ASSERT_FALSE(c.complete());
    if(mode==0)inventory(store,input);
    if(mode==1)inventory(other,input);
    if(mode==3)inventory(store,input);
    if(mode==0){c.discard();ready(store,c);compact(store,c,input,7);store.clear();TEST_ASSERT_FALSE(c.complete());}
  }
}
void test_late_admission_denies_pair_and_compaction_without_next_write() {
  LoopEventStore::resetPoolForTests();LoopEventStore store;const auto input=rows(512);fill(store,input);
  LoopEventStoreFilterCursor c;ready(store,c);
  TEST_ASSERT_TRUE(PersistenceQueue::admitSealedChunk(store.chunkIds()[1]));
  TEST_ASSERT_FALSE(store.markPrivateFilterPair(c,0,256));inventory(store,input);
  // The first chunk may be processed, but the newly admitted chunk cannot be touched.
  TEST_ASSERT_TRUE(store.finishPrivateFilter(c));
  TEST_ASSERT_EQUAL(F::Pending,store.stepPrivateFilter(c,256));
  TEST_ASSERT_EQUAL(F::Denied,store.stepPrivateFilter(c,1));inventory(store,input);TEST_ASSERT_FALSE(c.complete());
}
void test_all_midi_lanes_removed_preserving_frozen_source() {
  LoopEventStore::resetPoolForTests();LoopEventStore producer,prepared;LoopEventStore::CaptureRange raw;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());TEST_ASSERT_TRUE(raw.reserveTail());Rows input;
  for(unsigned ch=1;ch<=16;++ch)for(unsigned note=0;note<128;++note)input.push_back(MidiEvent::NoteOn(10,ch,note,100));
  for(unsigned ch=1;ch<=16;++ch)for(unsigned note=0;note<128;++note)input.push_back(MidiEvent::NoteOff(11,ch,note,0));
  for(const auto&e:input)TEST_ASSERT_TRUE(producer.append(e));TEST_ASSERT_TRUE(producer.sealCaptureRange(raw));
  const auto gen=raw.generation();fill(prepared,input);LoopEventStoreFilterCursor c;ready(prepared,c);
  MinimumLengthWorkspace scratch;MinimumLengthCursor policy;uint64_t generation=2;
  TEST_ASSERT_TRUE(policy.begin(scratch,generation,2000,12,true));
  while(policy.stepReset(7)==MinimumLengthStep::Pending){}
  for(size_t i=0;i<input.size();++i){MidiEvent e;TEST_ASSERT_TRUE(prepared.readPrivateFilterRow(c,i,e));
    const auto result=policy.consume(e);
    if(i<2048)TEST_ASSERT_EQUAL(MinimumLengthStep::Accepted,result);
    else {TEST_ASSERT_EQUAL(MinimumLengthStep::PairReady,result);size_t on,off;
      TEST_ASSERT_TRUE(policy.pendingPair(on,off));TEST_ASSERT_TRUE(prepared.markPrivateFilterPair(c,on,off));
      TEST_ASSERT_TRUE(policy.acknowledgePair());}
  }
  TEST_ASSERT_TRUE(policy.finishInput());compact(prepared,c,{},1);
  for(size_t i=0;i<input.size();++i){MidiEvent e;bool end;
    TEST_ASSERT_TRUE(raw.read(gen,i/256,i%256,e,end));TEST_ASSERT_FALSE(end);same(input[i],e);}
}
void test_late_destination_borrow_and_tail_admission_preserve_owners() {
  for(unsigned mode=0;mode<2;++mode) {
    LoopEventStore::resetPoolForTests();LoopEventStore store;const auto input=rows(512);fill(store,input);
    LoopEventStoreFilterCursor c;ready(store,c);const auto ids=store.chunkIds();
    const size_t begin=mode?256:0;
    for(size_t i=begin;i<begin+256;++i)TEST_ASSERT_TRUE(store.markPrivateFilterPair(c,i,i));
    TEST_ASSERT_TRUE(store.finishPrivateFilter(c));
    TEST_ASSERT_EQUAL(F::Pending,store.stepPrivateFilter(c,mode?512:256));
    if(mode) {
      TEST_ASSERT_TRUE(PersistenceQueue::admitSealedChunk(ids[1]));
      TEST_ASSERT_EQUAL(F::Pending,store.stepPrivateFilter(c,1)); // scan -> trim
    } else LoopEventStore::retainChunkReferenceForTests(ids[0]);
    const auto before=store.at(0).tick;
    TEST_ASSERT_EQUAL(F::Denied,store.stepPrivateFilter(c,1));
    TEST_ASSERT_EQUAL(before,store.at(0).tick);TEST_ASSERT_TRUE(ids==store.chunkIds());
    TEST_ASSERT_EQUAL(512,store.size());TEST_ASSERT_FALSE(c.complete());
    TEST_ASSERT_EQUAL(mode?1:2,LoopEventStore::chunkReferenceCount(ids[0]));
    TEST_ASSERT_EQUAL(1,LoopEventStore::chunkReferenceCount(ids[1]));
    if(!mode){CaptureChunkIdList refs{ids[0]};LoopEventStore::releaseChunkRefs(refs);}
  }
}
void test_empty_reserved_tail_is_retired_without_new_allocation() {
  LoopEventStore::resetPoolForTests();LoopEventStore store;
  TEST_ASSERT_TRUE(store.reserveCaptureTail());LoopEventStoreFilterCursor c;ready(store,c);
  compact(store,c,{},1);TEST_ASSERT_TRUE(store.empty());
}
void test_admission_after_metadata_refresh_denies_completion() {
  LoopEventStore::resetPoolForTests();LoopEventStore store;fill(store,rows(257));
  LoopEventStoreFilterCursor c;ready(store,c);
  TEST_ASSERT_TRUE(store.markPrivateFilterPair(c,0,0));
  TEST_ASSERT_TRUE(store.finishPrivateFilter(c));
  // N+K+2 units stop after the last metadata refresh, one unit before Complete.
  TEST_ASSERT_EQUAL(F::Pending,store.stepPrivateFilter(c,261));
  TEST_ASSERT_FALSE(c.complete());TEST_ASSERT_EQUAL(257,store.size());
  TEST_ASSERT_TRUE(PersistenceQueue::admitSealedChunk(store.chunkIds().back()));
  TEST_ASSERT_EQUAL(F::Denied,store.stepPrivateFilter(c,1));
  TEST_ASSERT_FALSE(c.complete());TEST_ASSERT_EQUAL(257,store.size());
}
void setUp(){}
void tearDown(){}
int main(){UNITY_BEGIN();
 RUN_TEST(test_stable_compaction_sizes_retries_and_tail_retirement);
 RUN_TEST(test_pair_mark_validates_both_targets_before_writing);
 RUN_TEST(test_minimum_decisions_compact_against_real_legacy_policy);
 RUN_TEST(test_persistence_shared_and_corrupt_input_deny_before_marks);
 RUN_TEST(test_stale_owner_pool_append_and_foreign_receipts);
 RUN_TEST(test_late_admission_denies_pair_and_compaction_without_next_write);
 RUN_TEST(test_all_midi_lanes_removed_preserving_frozen_source);
 RUN_TEST(test_late_destination_borrow_and_tail_admission_preserve_owners);
 RUN_TEST(test_empty_reserved_tail_is_retired_without_new_allocation);
 RUN_TEST(test_admission_after_metadata_refresh_denies_completion);
 return UNITY_END();}
