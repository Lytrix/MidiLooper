#define RECOVERY_BOOT_SUITE 1
#include "../test_offline_undo_recovery/test_offline_undo_recovery.cpp"
namespace RtcTime { void init() {} } // physical RTC only
#include "../../src/BootRecoveryPolicy.cpp"
#include "../../src/SetBrowserOverlayPolicy.cpp"
#include "../../src/StorageManager/BootRecovery.cpp"
#ifdef RECOVERY_TEENSY_ABI
using RecoveryCard = std::map<std::string,std::vector<uint8_t>>;
static RecoveryCard recoverySourceCard() {
  RecoveryCard result;
  const std::string root=std::getenv("MIDILOOPER_RECOVERY_CURRENT");
  result[CurrentWorkspaceStorage::kWorkspaceMetaPath]=readHost(root+"/workspace.bin");
  for (unsigned t=0;t<8;++t) for (unsigned slot=0;slot<8;++slot) {
    char tail[64];std::snprintf(tail,sizeof(tail),"/slots/loop_%02u_%02u.bin",t,slot);
    result[std::string(CurrentSetStorage::kCurrentSetDir)+tail]=readHost(root+tail);
  }
  result[CurrentSetStorage::kCurrentRuntimeBundleTempPath]={1,9,8,2,7};
  result["/MidiLooper/current/slots/loop_00_00.bin.tmp"]={4,3,2};
  result["/unrelated/user-data.bin"]={11,12,13};
  return result;
}
static void resetRecoveryBoot(const RecoveryCard& card) {
  nativeSdAfterMutation={};nativeSdWriteBudget=-1;nativeSdRenameBudget=-1;
  nativeSdPartialWrite=false;nativeSdFailWriteOpen=false;
  SD.files=card;
  StorageManagerInternal::resetStorageSessionJobs();
}
static void assertRecoverySources(const RecoveryCard& sources) {
  for (const auto& entry:sources) {
    TEST_ASSERT_TRUE_MESSAGE(SD.files.count(entry.first)==1,entry.first.c_str());
    TEST_ASSERT_TRUE_MESSAGE(SD.files.at(entry.first)==entry.second,entry.first.c_str());
  }
}
static void assertRecoverySaveBlocked() {
  TEST_ASSERT_TRUE(StorageManager::bootRecoveryBlocked());
  const auto before=SD.files;
  StorageManager::requestDeferredSaveState(LOOPER_IDLE);
  TEST_ASSERT_FALSE(StorageManagerInternal::storageSession.currentWorkspaceSave.pending);
  StorageManager::processDeferredSaveState(LOOPER_IDLE);
  TEST_ASSERT_FALSE(StorageManager::saveState(LOOPER_IDLE));
  TEST_ASSERT_TRUE(before==SD.files);
}
void test_boot_repair_refuses_bad_sources_before_any_write() {
  const auto original=recoverySourceCard();
  for (unsigned failure=0;failure<7;++failure) {
    auto bad=original;
    const char* slot="/MidiLooper/current/slots/loop_00_00.bin";
    if (failure==0) bad.erase(slot);
    if (failure==1) bad[slot][20]^=0x80;
    if (failure==2) bad[CurrentWorkspaceStorage::kWorkspaceMetaPath][4]^=0x80;
    if (failure==3) bad.erase(CurrentWorkspaceStorage::kWorkspaceMetaPath);
    resetRecoveryBoot(bad);
    if (failure==4) {
      // Unsupported body with a VALID outer CRC must still be refused by the parser.
      SD.files[slot][14]=99;
      TEST_ASSERT_TRUE(CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(slot));
      bad=SD.files;
    }
    if (failure==5) {
      CurrentWorkspaceStorage::WorkspaceMetaRecord meta{};
      TEST_ASSERT_TRUE(CurrentWorkspaceStorage::readWorkspaceMetaFile(meta));
      const uint32_t newer=meta.currentEpoch+1;
      std::memcpy(SD.files[slot].data(),&newer,4);
      bad=SD.files;
    }
    if (failure==6) {
      SD.files[slot][35]=7; // LSV1 metadata identity contradicts the filename
      TEST_ASSERT_TRUE(CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(slot));
      bad=SD.files;
    }
    LooperState state=LOOPER_IDLE;
    TEST_ASSERT_FALSE(StorageManager::loadState(state));
    TEST_ASSERT_TRUE(bad==SD.files); // includes evidence temp and unrelated files
    assertRecoverySaveBlocked();
  }
}
void test_boot_repair_io_faults_and_restart() {
  const auto original=recoverySourceCard();
  using Outcome=StorageManagerInternal::BootBundleRecovery;
  for (int budget: {0,1,12,80,220}) {
    for (bool partial: {false,true}) {
      resetRecoveryBoot(original);
      nativeSdWriteBudget=budget;nativeSdPartialWrite=partial;
      TEST_ASSERT_EQUAL(Outcome::Blocked,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
      assertRecoverySources(original);assertRecoverySaveBlocked();
      const auto interrupted=SD.files;
      resetRecoveryBoot(interrupted);
      TEST_ASSERT_EQUAL(Outcome::Repaired,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
      assertRecoverySources(original);
    }
  }
  resetRecoveryBoot(original);nativeSdFailWriteOpen=true;
  TEST_ASSERT_EQUAL(Outcome::Blocked,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
  TEST_ASSERT_TRUE(original==SD.files);assertRecoverySaveBlocked();
  for (int budget: {0,1}) {
    auto corrupt=original;
    corrupt[CurrentSetStorage::kCurrentRuntimeBundlePath]={9,9,9};
    resetRecoveryBoot(corrupt);nativeSdRenameBudget=budget;
    TEST_ASSERT_EQUAL(Outcome::Blocked,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
    assertRecoverySources(original);assertRecoverySaveBlocked();
    bool oldFound=false;for (const auto& f:SD.files) if(f.second==std::vector<uint8_t>{9,9,9}) oldFound=true;
    TEST_ASSERT_TRUE(oldFound);
    const auto interrupted=SD.files;resetRecoveryBoot(interrupted);
    TEST_ASSERT_EQUAL(Outcome::Repaired,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
    assertRecoverySources(original);
  }
}
void test_boot_repair_empty_card_and_exhausted_candidate_names() {
  using Outcome=StorageManagerInternal::BootBundleRecovery;
  RecoveryCard empty{{"/unrelated/user-data.bin",{1,2,3}}};
  resetRecoveryBoot(empty);
  TEST_ASSERT_EQUAL(Outcome::NoWorkspace,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
  TEST_ASSERT_FALSE(StorageManager::bootRecoveryBlocked());
  TEST_ASSERT_TRUE(empty==SD.files);
  auto exhausted=recoverySourceCard();
  for (unsigned i=0;i<64;++i) {
    char path[100]; std::snprintf(path,sizeof(path),"/MidiLooper/current/temp/runtime.repair.%02u.bin",i);
    exhausted[path]={1,2,3};
  }
  resetRecoveryBoot(exhausted);
  TEST_ASSERT_EQUAL(Outcome::Blocked,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
  TEST_ASSERT_TRUE(exhausted==SD.files);
  assertRecoverySaveBlocked();
}
void test_reboot_after_every_repair_mutation_preserves_sources() {
  using Outcome=StorageManagerInternal::BootBundleRecovery;
  const auto original=recoverySourceCard();
  auto corrupt=original;corrupt[CurrentSetStorage::kCurrentRuntimeBundlePath]={9,9,9};
  resetRecoveryBoot(corrupt);
  std::vector<RecoveryCard> cuts;
  nativeSdAfterMutation=[&] {cuts.push_back(SD.files);};
  TEST_ASSERT_EQUAL(Outcome::Repaired,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
  nativeSdAfterMutation={};
  TEST_ASSERT_TRUE(cuts.size()>200);
  for (const auto& card:cuts) {
    resetRecoveryBoot(card);
    const auto result=StorageManagerInternal::prepareCurrentWorkspaceForBoot();
    TEST_ASSERT_TRUE(result==Outcome::Repaired || result==Outcome::Ready);
    TEST_ASSERT_TRUE(StorageManagerInternal::validateRuntimeBundleForBoot(CurrentSetStorage::kCurrentRuntimeBundlePath));
    assertRecoverySources(original);
    bool oldFound=false;for (const auto& f:SD.files) if(f.second==std::vector<uint8_t>{9,9,9}) oldFound=true;
    TEST_ASSERT_TRUE(oldFound);
  }
  const auto healthy=SD.files;
  resetRecoveryBoot(healthy);
  TEST_ASSERT_EQUAL(Outcome::Ready,StorageManagerInternal::prepareCurrentWorkspaceForBoot());
  TEST_ASSERT_TRUE(healthy==SD.files);
  std::printf("BOOT_REPAIR_CUTS,verified=%zu\n",cuts.size());
}
#endif
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reconstructed_workspace_loads);
  RUN_TEST(test_reconstructed_history_roundtrips);
#ifdef RECOVERY_TEENSY_ABI
  if (!std::getenv("MIDILOOPER_UNDO_TARGET") || std::string(std::getenv("MIDILOOPER_UNDO_TARGET"))=="0:0") {
    RUN_TEST(test_boot_repair_refuses_bad_sources_before_any_write);
    RUN_TEST(test_boot_repair_io_faults_and_restart);
    RUN_TEST(test_boot_repair_empty_card_and_exhausted_candidate_names);
    RUN_TEST(test_reboot_after_every_repair_mutation_preserves_sources);
  }
#endif
  return UNITY_END();
}
