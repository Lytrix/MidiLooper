// Host-only reconstruction using existing persistence writers and the real loader.
#define main existing_drain_suite_main
#include "../test_storage_drain_restore/test_storage_drain_restore.cpp"
#undef main
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <set>

#ifdef RECOVERY_TEENSY_ABI
static std::vector<uint8_t> readHost(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  TEST_ASSERT_TRUE_MESSAGE(file.good(), path.c_str());
  return {std::istreambuf_iterator<char>(file), {}};
}

#include "../../tools/midilooper-reader/RecoveryRuntimeBundle.h"

static void makeRecoveryBundle(const std::array<uint8_t,8>& selected, uint32_t epoch) {
  const char* path=CurrentSetStorage::kCurrentRuntimeBundlePath;
  File file=CurrentSetStorage::openForRewrite(path);
  TEST_ASSERT_TRUE(bool(file));
  OfflineRecovery::RuntimeBundleDefaults defaults;
  defaults.epoch=epoch; defaults.selected=selected;
  TEST_ASSERT_TRUE(OfflineRecovery::writeRuntimeBundle(file,defaults));
  file.close();
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(path));
}

static void assertCaptureBytesEqual(const CommittedChunkIdList& expected, const CommittedChunkIdList& actual) {
  MidiEventVec a,b;
  for (auto id:expected) LoopEventStore::appendChunkRefEvent(id,a);
  for (auto id:actual) LoopEventStore::appendChunkRefEvent(id,b);
  TEST_ASSERT_EQUAL_UINT32(a.size(),b.size());
  if (!a.empty()) TEST_ASSERT_EQUAL_MEMORY(a.data(),b.data(),a.size()*sizeof(MidiEvent));
}

static void assertRestoredCaptureContent(const std::vector<uint8_t>& file, const Loop& loop) {
  PersistedLoopSnapshot raw;
  LoopSnapshotBodyFormatV1ReadResult lsv;
  PersistedLoopSnapshot* expected=&raw;
  const uint8_t* body=file.data()+10;
  size_t size=file.size()-14;
  if (size>=4 && std::memcmp(body,"LSV1",4)==0) {
    TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(body,size,lsv));
    TEST_ASSERT_TRUE(lsv.hasPersistedOracle);
    expected=&lsv.persistedOracle;
  } else {
    size_t pos=0;
    StorageIo io{nullptr,[&](void* dest,size_t n) {
      if (n>size-pos) return false;
      std::memcpy(dest,body+pos,n);pos+=n;return true;
    }};
    TEST_ASSERT_TRUE(readPersistedLoopSnapshot(io,raw));
    TEST_ASSERT_EQUAL_UINT32(size,pos);
  }
  TEST_ASSERT_EQUAL_UINT32(expected->passes.recordPass.id,loop.passes.recordPass.id);
  assertCaptureBytesEqual(expected->passes.recordPass.committedChunkIds,loop.passes.recordPass.committedChunkIds);
  TEST_ASSERT_EQUAL_UINT32(expected->passes.overdubPasses.size(),loop.passes.overdubPasses.size());
  for (size_t p=0;p<expected->passes.overdubPasses.size();++p) {
    TEST_ASSERT_EQUAL_UINT32(expected->passes.overdubPasses[p].id,loop.passes.overdubPasses[p].id);
    assertCaptureBytesEqual(expected->passes.overdubPasses[p].committedChunkIds,loop.passes.overdubPasses[p].committedChunkIds);
  }
}

void test_writer_refuses_invalid_or_unwritable_output() {
  SD.reset();
  File file=CurrentSetStorage::openForRewrite("/candidate.bin");
  OfflineRecovery::RuntimeBundleDefaults defaults;
  defaults.selected[0]=8;
  TEST_ASSERT_FALSE(OfflineRecovery::writeRuntimeBundle(file,defaults));
  TEST_ASSERT_EQUAL_UINT32(0,file.size());
  file.close();
  defaults.selected[0]=0;
  File closed;
  TEST_ASSERT_FALSE(OfflineRecovery::writeRuntimeBundle(closed,defaults));
}

void test_reconstructed_workspace_loads() {
  const char* input=std::getenv("MIDILOOPER_RECOVERY_CURRENT");
  const char* output=std::getenv("MIDILOOPER_RECOVERY_BUNDLE_OUTPUT");
  if (!input || !output) TEST_IGNORE_MESSAGE("Host copy and candidate output required");
  TEST_ASSERT_EQUAL_UINT32(20,sizeof(MidiEvent));
  resetLoadSubsystemForS2b7();
  sequencerRunning=false;
  RestoreHarness::setRuntimeTransportIdle();
  std::array<uint8_t,8> selected{};
  std::array<uint32_t,64> lengths{};
  auto workspace=readHost(std::string(input)+"/workspace.bin");
  SD.writeFile("/MidiLooper/current/workspace.bin",workspace);
  uint32_t epoch=0; std::memcpy(&epoch,workspace.data()+2,4);
  for (unsigned t=0;t<8;++t) {
    bool chosen=false;
    for (unsigned s=0;s<8;++s) {
      char relative[80]; std::snprintf(relative,sizeof(relative),"/slots/loop_%02u_%02u.bin",t,s);
      auto bytes=readHost(std::string(input)+relative);
      SD.writeFile((std::string("/MidiLooper/current")+relative).c_str(),bytes);
      size_t offset=10;
      if (bytes.size()>18 && std::memcmp(bytes.data()+10,"LSV1",4)==0) offset=10+8+1+12+4;
      std::memcpy(&lengths[t*8+s],bytes.data()+offset+8,4);
      if (!chosen && lengths[t*8+s]) {selected[t]=s;chosen=true;}
    }
  }
  LooperState state=LOOPER_IDLE;
  TEST_ASSERT_FALSE(StorageManager::recoveryLoadDirectoryForTest("/MidiLooper/current",state));
  std::vector<uint8_t> candidate;
  if (std::getenv("MIDILOOPER_AUTOMATIC_RECOVERY")) {
    const char* existing=std::getenv("MIDILOOPER_RECOVERY_EXISTING_BUNDLE");
    if (existing) SD.writeFile(CurrentSetStorage::kCurrentRuntimeBundlePath,readHost(existing));
    const auto beforeBoot=SD.files;
    TEST_ASSERT_TRUE_MESSAGE(StorageManager::loadState(state),"Automatic boot reconstruction refused");
    if (existing) {
      for (const auto& entry:beforeBoot)
        TEST_ASSERT_TRUE_MESSAGE(SD.files.count(entry.first) && SD.files.at(entry.first)==entry.second,
                                 "Healthy boot changed an original workspace file");
      // Normal boot may reconcile sets/index.bin; it must not reconstruct current metadata.
      for (const auto& entry:SD.files)
        if (entry.first.find("/MidiLooper/current/")==0)
          TEST_ASSERT_TRUE_MESSAGE(beforeBoot.count(entry.first),"Healthy boot created a repair file");
    }
    candidate=SD.files[CurrentSetStorage::kCurrentRuntimeBundlePath];
  } else {
  const char* existing=std::getenv("MIDILOOPER_RECOVERY_EXISTING_BUNDLE");
  if (existing) SD.writeFile(CurrentSetStorage::kCurrentRuntimeBundlePath,readHost(existing));
  else makeRecoveryBundle(selected,epoch);
  candidate=SD.files[CurrentSetStorage::kCurrentRuntimeBundlePath];
  auto broken=candidate; broken.pop_back();
  SD.writeFile(CurrentSetStorage::kCurrentRuntimeBundlePath,broken);
  TEST_ASSERT_FALSE(StorageManager::recoveryLoadDirectoryForTest("/MidiLooper/current",state));
  SD.writeFile(CurrentSetStorage::kCurrentRuntimeBundlePath,candidate);
  TEST_ASSERT_TRUE(StorageManager::recoveryLoadDirectoryForTest("/MidiLooper/current",state));
  }
  TEST_ASSERT_EQUAL(LOOPER_IDLE,state);
  TEST_ASSERT_EQUAL_FLOAT(120.0f,bpm);
  for (unsigned t=0;t<8;++t) {
    TEST_ASSERT_FALSE(trackManager.getTrack(t).isRecording());
    TEST_ASSERT_FALSE(trackManager.getTrack(t).isAudiblePlaybackActive());
    for (unsigned s=0;s<8;++s) {
      TEST_ASSERT_EQUAL(s==selected[t],trackManager.isSlotEnabled(t,s));
      TEST_ASSERT_FALSE(trackManager.isSlotMuted(t,s));
    }
  }
  const char* hydration=std::getenv("MIDILOOPER_UNDO_HYDRATION");
  const bool hydrateBefore=hydration && std::string(hydration)=="before";
  const bool hydrateAfter=hydration && std::string(hydration)=="after";
  if (hydrateBefore) StorageManager::restoreDeferredUndoSnapshotsBeforeUse();
  unsigned turns=0;
  for (;turns<100000;++turns) {
    nativeMillisAdvance(1);
    StorageManager::enqueueRemainingLoopSlotRestoresFromSd();
    RestoreHarness::runLoadServiceTurn();
    if (!hydrateBefore && !hydrateAfter && !SlotLoadSession::isActive()) StorageManager::processDeferredUndoSnapshots();
    if (StorageManagerInternal::bootBackgroundFillAdmissionComplete() && RestoreHarness::restoreWorkIdle()) break;
  }
  StorageManager::restoreDeferredUndoSnapshotsBeforeUse();
  TEST_ASSERT_TRUE(StorageManagerInternal::bootBackgroundFillAdmissionComplete());
  std::printf("RECOVERY_RESTORE,turns=%u,pending=%u\n",turns,StorageManagerInternal::pendingLoopSlotRestoreCount());
  TEST_ASSERT_TRUE_MESSAGE(RestoreHarness::restoreWorkIdle(),"Restore did not settle within bounded native turns");
  unsigned mismatches=0;
  for (unsigned t=0;t<8;++t) {
    TEST_ASSERT_EQUAL(selected[t],trackManager.getTrack(t).getActiveLoopIndex());
    for (unsigned s=0;s<8;++s) {
      const auto& loop=trackManager.getTrack(t).getLoop(s);
      size_t events=LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.committedChunkIds);
      for (const auto& pass:loop.passes.overdubPasses) events+=LoopEventStore::countEventsInChunkIds(pass.committedChunkIds);
      std::set<NoteId> notes;
      auto root=loop.publishedSnapshot();
      if (root) {
        for (unsigned bar=0;bar<(loop.loopLengthTicks+Config::TICKS_PER_BAR-1)/Config::TICKS_PER_BAR;++bar)
          for (unsigned pitch=0;pitch<128;++pitch)
            forEachSnapshotMembershipReference(root->noteMembership(pitch,bar).get(),
              [&](const LoopSnapshotNoteRef& note) { notes.insert(note->noteId); });
      }
      std::printf("RESTORED_SLOT,track=%u,slot=%u,expected=%u,length=%u,events=%zu,snapshot=%u,notes=%zu\n",t,s,lengths[t*8+s],loop.loopLengthTicks,events,root?1:0,notes.size());
      char relative[80]; std::snprintf(relative,sizeof(relative),"/slots/loop_%02u_%02u.bin",t,s);
      auto original=readHost(std::string(input)+relative);
      TEST_ASSERT_TRUE(SD.files[std::string("/MidiLooper/current")+relative]==original);
      assertRestoredCaptureContent(original,loop);
      if (lengths[t*8+s]!=loop.loopLengthTicks) ++mismatches;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(0,mismatches);
  TEST_ASSERT_TRUE(SD.files["/MidiLooper/current/workspace.bin"]==workspace);
  std::ofstream out(output,std::ios::binary|std::ios::trunc);
  TEST_ASSERT_TRUE(out.good());
  out.write(reinterpret_cast<const char*>(candidate.data()),candidate.size());
  out.close(); TEST_ASSERT_TRUE(out.good());
  std::printf("RECOVERY_BUNDLE,bytes=%zu,epoch=%u,load=accepted\n",candidate.size(),epoch);
}

#else
void test_reconstructed_workspace_loads() { TEST_IGNORE_MESSAGE("Isolated recovery ABI build required"); }
#endif

#ifndef RECOVERY_UNDO_SUITE
int main() {
  UNITY_BEGIN();
#ifdef RECOVERY_TEENSY_ABI
  RUN_TEST(test_writer_refuses_invalid_or_unwritable_output);
#endif
  RUN_TEST(test_reconstructed_workspace_loads);
  return UNITY_END();
}

#endif
