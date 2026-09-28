// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include <unity.h>
#include "../../src/LoopEventStore.cpp"

static std::vector<MidiEvent> rows(size_t n, unsigned shape) {
  std::vector<MidiEvent> out; uint32_t random=183;
  for(size_t i=0;i<n;++i) {
    random^=random<<13;random^=random>>17;random^=random<<5;
    const uint32_t tick=shape==0?n-i:shape==1?(i%2?n-i:i):shape==2?7:random%31;
    auto e=MidiEvent::NoteOn(tick,1+i%16,i%128,1+i%127);
    if(shape==3 && i%3==0)e=MidiEvent::NoteOff(tick,1+i%16,i%128,i%128);
    if(shape==3 && i%3==1)e=MidiEvent::ControlChange(tick,1+i%16,i%128,i%128);
    e.noteId=i+1;out.push_back(e);
  }
  return out;
}
static void fill(LoopEventStore& store,const std::vector<MidiEvent>& input) {
  LoopEventStore::enterEphemeralSeal();
  for(const auto& e:input)TEST_ASSERT_TRUE(store.append(e));
  LoopEventStore::leaveEphemeralSeal();
}
static void same(const MidiEvent& a,const MidiEvent& b) {
  TEST_ASSERT_EQUAL(a.tick,b.tick);TEST_ASSERT_EQUAL(a.noteId,b.noteId);
  TEST_ASSERT_EQUAL(a.channel,b.channel);TEST_ASSERT_EQUAL(a.type,b.type);
  TEST_ASSERT_EQUAL(a.data.noteData.note,b.data.noteData.note);
  TEST_ASSERT_EQUAL(a.data.noteData.velocity,b.data.noteData.velocity);
}
static size_t drain(LoopEventStore& store,LoopEventStoreOrderCursor& c,size_t quantum) {
  size_t log=1;for(size_t n=store.size();n>1;n=(n+1)/2)++log;
  const size_t limit=40*(store.size()+1)*log*log+100;
  const auto ids=store.chunkIds();const auto cap=store.chunkIds().capacity();
  const auto used=LoopEventStore::usedChunkCount();size_t units=0;
  for(size_t turns=0;turns<limit;++turns) {
    const auto result=store.stepPrivateTickOrder(c,quantum);
    TEST_ASSERT_TRUE(result!=LoopEventStoreRewriteResult::Denied);
    TEST_ASSERT_LESS_OR_EQUAL(quantum,c.lastStepUnits()); units+=c.lastStepUnits();
    TEST_ASSERT_LESS_OR_EQUAL(limit,units);
    TEST_ASSERT_EQUAL(cap,store.chunkIds().capacity());TEST_ASSERT_EQUAL(used,LoopEventStore::usedChunkCount());
    TEST_ASSERT_TRUE(ids==store.chunkIds());
    for(auto id:ids) TEST_ASSERT_EQUAL(1,LoopEventStore::chunkReferenceCount(id));
    if(result==LoopEventStoreRewriteResult::Complete) {TEST_ASSERT_TRUE(c.complete());return units;}
  }
  TEST_FAIL_MESSAGE("ordering exceeded fixed work watchdog");return 0;
}
void test_stable_private_order_across_chunks_and_long_inputs() {
  for(size_t count:{0u,1u,2u,255u,256u,257u,2056u})for(unsigned shape=0;shape<4;++shape) {
    LoopEventStore::resetPoolForTests();LoopEventStore store;
    auto expected=rows(count,shape);fill(store,expected);
    std::stable_sort(expected.begin(),expected.end(),[](const MidiEvent&a,const MidiEvent&b){return a.tick<b.tick;});
    LoopEventStoreOrderCursor c;TEST_ASSERT_TRUE(store.beginPrivateTickOrder(c));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,store.stepPrivateTickOrder(c,0));
    drain(store,c,shape==0?1:7);
    for(size_t i=0;i<count;++i)same(expected[i],store.at(i));
    for(size_t ci=0;ci<store.chunkIds().size();++ci) {
      uint32_t first,last;TEST_ASSERT_TRUE(LoopEventStore::chunkTickSpan(store.chunkIds()[ci],first,last));
      TEST_ASSERT_EQUAL(expected[ci*256].tick,first);
      TEST_ASSERT_EQUAL(expected[std::min(count,(ci+1)*256)-1].tick,last);
    }
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,store.stepPrivateTickOrder(c,1));
    TEST_ASSERT_EQUAL(0,c.lastStepUnits());
    TEST_ASSERT_FALSE(store.beginPrivateTickOrder(c));
  }
}
void test_persistence_and_sharing_are_rejected_before_mutation() {
  for(unsigned mode=0;mode<4;++mode) {
    LoopEventStore::resetPoolForTests();LoopEventStore store;
    const auto input=rows(257,0);
    if(mode==1)LoopEventStore::enterSdLoadStaging();
    if(mode>=2)LoopEventStore::enterEphemeralSeal();
    for(const auto&e:input)TEST_ASSERT_TRUE(store.append(e));
    if(mode==1)LoopEventStore::leaveSdLoadStaging();
    if(mode>=2)LoopEventStore::leaveEphemeralSeal();
    if(mode==2)LoopEventStore::retainChunkReferenceForTests(store.chunkIds().front());
    if(mode==3) {
      TEST_ASSERT_TRUE(PersistenceQueue::admitSealedChunk(store.chunkIds().front()));
      uint16_t id;TEST_ASSERT_TRUE(PersistenceQueue::beginWriteQueuedChunk(id));
    }
    LoopEventStoreOrderCursor c;TEST_ASSERT_TRUE(store.beginPrivateTickOrder(c));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,store.stepPrivateTickOrder(c,1));
    TEST_ASSERT_FALSE(c.complete());
    for(size_t i=0;i<input.size();++i)same(input[i],store.at(i));
    if(mode==2) {CaptureChunkIdList borrowed{store.chunkIds().front()};LoopEventStore::releaseChunkRefs(borrowed);}
  }
}
void test_stale_owner_pool_and_append_invalidate_cursor() {
  for(unsigned mode=0;mode<3;++mode) {
    LoopEventStore::resetPoolForTests();LoopEventStore store;fill(store,rows(33,0));
    LoopEventStoreOrderCursor c;TEST_ASSERT_TRUE(store.beginPrivateTickOrder(c));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,store.stepPrivateTickOrder(c,2));
    if(mode==0) {store.clear();fill(store,rows(32,1));}
    if(mode==1) {store.clear();LoopEventStore::resetPoolForTests();}
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(90,2,70,99)));
    const size_t count=store.size();const auto ids=store.chunkIds();
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,store.stepPrivateTickOrder(c,1));
    TEST_ASSERT_FALSE(c.complete());TEST_ASSERT_EQUAL(count,store.size());TEST_ASSERT_TRUE(ids==store.chunkIds());
    c.discard();TEST_ASSERT_TRUE(store.beginPrivateTickOrder(c));drain(store,c,32);
    store.clear();TEST_ASSERT_FALSE(c.complete());
  }
}
void test_pool_reuse_rejects_old_cursor_with_identical_shape() {
  LoopEventStore::resetPoolForTests();LoopEventStore old;fill(old,rows(33,0));
  LoopEventStoreOrderCursor c;TEST_ASSERT_TRUE(old.beginPrivateTickOrder(c));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,old.stepPrivateTickOrder(c,2));
  LoopEventStore::resetPoolForTests();LoopEventStore replacement;const auto expected=rows(33,1);fill(replacement,expected);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,old.stepPrivateTickOrder(c,1));
  for(size_t i=0;i<expected.size();++i)same(expected[i],replacement.at(i));
}
void test_queue_admission_after_preflight_denies_next_access() {
  LoopEventStore::resetPoolForTests();LoopEventStore store;const auto input=rows(256,0);fill(store,input);
  LoopEventStoreOrderCursor c;TEST_ASSERT_TRUE(store.beginPrivateTickOrder(c));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,store.stepPrivateTickOrder(c,2));
  TEST_ASSERT_TRUE(PersistenceQueue::admitSealedChunk(store.chunkIds().front()));
  auto result=LoopEventStoreRewriteResult::Pending;
  for(unsigned i=0;i<5&&result==LoopEventStoreRewriteResult::Pending;++i)result=store.stepPrivateTickOrder(c,1);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,result);TEST_ASSERT_FALSE(c.complete());
  for(size_t i=0;i<input.size();++i)same(input[i],store.at(i));
}
void test_frozen_raw_survives_separate_prepared_ordering() {
  LoopEventStore::resetPoolForTests();LoopEventStore producer,prepared;
  LoopEventStore::CaptureRange raw;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());TEST_ASSERT_TRUE(raw.reserveTail());
  const auto input=rows(2056,3);for(const auto&e:input)TEST_ASSERT_TRUE(producer.append(e));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(raw));const auto generation=raw.generation();
  fill(prepared,input);LoopEventStoreOrderCursor c;TEST_ASSERT_TRUE(prepared.beginPrivateTickOrder(c));
  drain(prepared,c,32);
  for(size_t i=0;i<input.size();++i) {MidiEvent e;bool end;
    TEST_ASSERT_TRUE(raw.read(generation,i/256,i%256,e,end));TEST_ASSERT_FALSE(end);same(input[i],e);
  }
}
void setUp() {}
void tearDown() {}
int main(){UNITY_BEGIN();
 RUN_TEST(test_stable_private_order_across_chunks_and_long_inputs);
 RUN_TEST(test_persistence_and_sharing_are_rejected_before_mutation);
 RUN_TEST(test_stale_owner_pool_and_append_invalidate_cursor);
 RUN_TEST(test_pool_reuse_rejects_old_cursor_with_identical_shape);
 RUN_TEST(test_queue_admission_after_preflight_denies_next_access);
 RUN_TEST(test_frozen_raw_survives_separate_prepared_ordering);
 return UNITY_END();}
