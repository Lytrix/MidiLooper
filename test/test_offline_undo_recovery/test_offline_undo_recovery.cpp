#define RECOVERY_UNDO_SUITE 1
#include "../test_offline_workspace_recovery/test_offline_workspace_recovery.cpp"

#ifdef RECOVERY_TEENSY_ABI
#include "undo_harness_boundaries.h"
static std::vector<uint8_t> effectiveBytes(const Loop& loop) {
  LoopEventStore events;
  loop.passes.materialize(events,loop.loopLengthTicks);
  std::vector<uint8_t> bytes;
  for (size_t n=0;n<events.size();++n) {
    const auto& event=events.at(n);
    const auto* p=reinterpret_cast<const uint8_t*>(&event);
    bytes.insert(bytes.end(),p,p+sizeof(event));
  }
  return bytes;
}

static std::vector<uint32_t> snapshotNotes(const Loop& loop) {
  std::map<NoteId,std::vector<uint32_t>> notes;
  auto root=loop.publishedSnapshot();
  if (root) for (unsigned bar=0;bar<(loop.loopLengthTicks+Config::TICKS_PER_BAR-1)/Config::TICKS_PER_BAR;++bar)
    for (unsigned pitch=0;pitch<128;++pitch)
      forEachSnapshotMembershipReference(root->noteMembership(pitch,bar).get(),
        [&](const LoopSnapshotNoteRef& note) {
          notes[note->noteId]={note->noteId,note->pitch,note->velocity,note->startTick,note->endTick};
        });
  std::vector<uint32_t> flat;
  for (const auto& entry:notes) flat.insert(flat.end(),entry.second.begin(),entry.second.end());
  return flat;
}

static void drainPublication(Loop& loop) {
  for (unsigned turn=0;turn<100000 && loop.pendingSnapshotSessionExitQueued();++turn)
    loop.stepPendingSnapshotPublish(64,true);
  if (loop.pendingSnapshotSessionExitQueued())
    std::printf("UNDO_STALL,length=%u,committed=%u,fromLoaded=%u,queued=%zu,applied=%zu,current=%u\n",loop.loopLengthTicks,loop.hasCommittedPasses(),loop.pendingSnapshotFromLoadedPasses(),loop.pendingSnapshotQueuedPassCount(),loop.pendingSnapshotAppliedPassCount(),loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE_MESSAGE(loop.pendingSnapshotSessionExitQueued(),"Snapshot publication did not settle");
}

void test_background_slot_commit_preserves_loaded_slot_redo() {
  trackManager.loadTransportSlotIndices(0,0,0);
  auto& track=trackManager.getTrack(0);
  repositionGlobalUndoStackTipForSlot(track.getGlobalUndoStack(),0);
  auto& focus=track.getLoop(0);
  auto& stack=track.getGlobalUndoStack();
  TEST_ASSERT_TRUE(TrackUndo::canUndoForLoop(track,focus));
  const auto before=effectiveBytes(focus);
  const auto beforeNotes=snapshotNotes(focus);
  const auto depth=stack.entries.size();
  TrackUndo::undoForLoop(track,focus);
  drainPublication(focus);
  TEST_ASSERT_EQUAL_UINT32(1,countRedoEntriesForSlot(stack,0));
  const auto redoId=stack.entries[stack.cursor].id;
  const auto undone=effectiveBytes(focus);
  const auto source=readHost(std::string(std::getenv("MIDILOOPER_RECOVERY_CURRENT"))+"/slots/loop_00_01.bin");
  for (unsigned repeat=0;repeat<2;++repeat) {
    LoopSnapshotBodyFormatV1ReadResult parsed{};
    TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(source.data()+10,source.size()-14,parsed));
    const auto loadedRoot=parsed.loopSnapshot;
    applyLoadedLoopSlotBodyCommit(track.getLoop(1),track,1,true,parsed.persistedOracle,
                                  std::move(parsed.loopSnapshot));
    TEST_ASSERT_TRUE(track.getLoop(1).publishedSnapshot()==loadedRoot);
    TEST_ASSERT_EQUAL_UINT32(depth,stack.entries.size());
    TEST_ASSERT_EQUAL_UINT32(redoId,stack.entries[stack.cursor].id);
    TEST_ASSERT_EQUAL_UINT32(1,countRedoEntriesForSlot(stack,0));
    TEST_ASSERT_EQUAL_UINT32(0,trackManager.getSelectedSlotIndex(0));
    TEST_ASSERT_TRUE(undone==effectiveBytes(focus));
  }
  TrackUndo::redoForLoop(track,focus);
  drainPublication(focus);
  TEST_ASSERT_EQUAL_UINT32(0,countRedoEntriesForSlot(stack,0));
  TEST_ASSERT_TRUE(before==effectiveBytes(focus));
  TEST_ASSERT_TRUE(beforeNotes==snapshotNotes(focus));
}

void test_reconstructed_history_roundtrips() {
  if (!std::getenv("MIDILOOPER_RECOVERY_CURRENT")) TEST_IGNORE_MESSAGE("Copied workspace required");
  StorageManager::restoreDeferredUndoSnapshotsBeforeUse();
  // Pinned independently by the preserved 122-action offline audit.
  const unsigned expectedDepth[64]={3,2,2,2,2,2,2,2,3,4,4,2,2,2,2,2,1,0,2,2,2,2,2,2,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,2,2,2,2,2,0,2,2,2,2,2,2,2,3,2,2,0,1,2,1,0};
  unsigned baseline=0;
  for (unsigned t=0;t<8;++t) {
    const auto& loadedStack=trackManager.getTrack(t).getGlobalUndoStack();
    size_t n=loadedStack.entries.size(); baseline+=n;
    TEST_ASSERT_EQUAL_UINT32(n,loadedStack.cursor);
    for (unsigned slot=0;slot<8;++slot) {
      TEST_ASSERT_EQUAL_UINT32(expectedDepth[t*8+slot],countAppliedUndoEntriesForSlot(loadedStack,slot));
      TEST_ASSERT_EQUAL_UINT32(0,countRedoEntriesForSlot(loadedStack,slot));
    }
    std::printf("UNDO_BASELINE,track=%u,entries=%zu\n",t,n);
  }
  if (std::getenv("MIDILOOPER_UNDO_RELOAD_ONLY")) {
    std::printf("UNDO_RELOAD,total=%u\n",baseline);
  }
  std::array<const GlobalUndoStack*,8> history{};
  OfflineRecovery::RuntimeBundleDefaults defaults;
  const auto& workspace=SD.files["/MidiLooper/current/workspace.bin"];
  std::memcpy(&defaults.epoch,workspace.data()+2,4);
  for (unsigned t=0;t<8;++t) {
    auto& track=trackManager.getTrack(t);
    defaults.selected[t]=trackManager.getSelectedSlotIndex(t);
    history[t]=&track.getGlobalUndoStack();
    for (const auto& e:history[t]->entries)
      std::printf("RECOVERED_UNDO,track=%u,slot=%u,kind=%u,pass=%u,group_passes=%zu,companion_edits=%zu\n",t,e.slotIndex,unsigned(e.kind),e.passId,e.passIds.size(),e.editPassIds.size());
  }
  const char* recovered=std::getenv("MIDILOOPER_UNDO_BUNDLE_OUTPUT");
  if (recovered) {
    const char* path="/recovery-history.bundle.bin";
    File file=CurrentSetStorage::openForRewrite(path);
    TEST_ASSERT_TRUE(OfflineRecovery::writeRuntimeBundle(file,defaults)); file.close();
    TEST_ASSERT_TRUE(CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(path));
    std::ofstream out(recovered,std::ios::binary|std::ios::trunc);
    TEST_ASSERT_TRUE(out.good());
    const auto& bytes=SD.files[path];out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();
    TEST_ASSERT_TRUE(out.good());
    std::printf("UNDO_BUNDLE,bytes=%zu\n",bytes.size());
  }
  unsigned total=0,withHistory=0;
  for (unsigned t=0;t<8;++t) {
    auto& track=trackManager.getTrack(t);

    for (unsigned s=0;s<8;++s) {
      unsigned targetTrack=99,targetSlot=99;
      const char* target=std::getenv("MIDILOOPER_UNDO_TARGET");
      if (target && std::sscanf(target,"%u:%u",&targetTrack,&targetSlot)==2 &&
          (targetTrack!=t || targetSlot!=s)) continue;
      trackManager.loadTransportSlotIndices(t,s,s);
      auto& loop=track.getLoop(s);
      auto& stack=track.getGlobalUndoStack();
      repositionGlobalUndoStackTipForSlot(stack,s);
      unsigned entries=0;
      for (const auto& entry:stack.entries) if (entry.slotIndex==s) ++entries;
      std::printf("UNDO_SLOT,track=%u,slot=%u,entries=%u,passes=%zu,edits=%zu,geometry=%zu\n",t,s,entries,loop.passes.capturePassCount(),loop.passes.editPasses.size(),loop.passes.loopGeometries.size());
      if (!entries) continue;
      ++withHistory;
      // Raw-format slots have no persisted root. Build a baseline explicitly:
      // comparing a null root with the first redo-built root is not a parity test.
      if (!loop.publishedSnapshot() && loop.hasCommittedPasses()) {
        std::printf("UNDO_BASELINE_PUBLICATION,track=%u,slot=%u\n",t,s);
        TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
      }
      drainPublication(loop);
      const auto before=effectiveBytes(loop);
      const auto beforeNotes=snapshotNotes(loop);
      const auto length=loop.loopLengthTicks;
      const auto cursor=stack.cursor;
      for (unsigned e=0;e<entries;++e) {
        std::printf("UNDO_TRY,track=%u,slot=%u,index=%u,kind=%u,pass=%u\n",t,s,e,unsigned(stack.entries[stack.cursor-1].kind),stack.entries[stack.cursor-1].passId);
        const auto oldCursor=stack.cursor;
        TrackUndo::undoForLoop(track,loop);
        TEST_ASSERT_EQUAL_UINT32(oldCursor-1,stack.cursor);
        drainPublication(loop);
      }
      for (unsigned e=0;e<entries;++e) {
        const auto oldCursor=stack.cursor;
        std::printf("REDO_TRY,track=%u,slot=%u,index=%u,kind=%u,pass=%u\n",t,s,e,unsigned(stack.entries[stack.cursor].kind),stack.entries[stack.cursor].passId);
        TrackUndo::redoForLoop(track,loop);
        TEST_ASSERT_EQUAL_UINT32(oldCursor+1,stack.cursor);
        drainPublication(loop);
      }
      TEST_ASSERT_EQUAL_UINT32(cursor,stack.cursor);
      TEST_ASSERT_EQUAL_UINT32(length,loop.loopLengthTicks);
      TEST_ASSERT_TRUE_MESSAGE(before==effectiveBytes(loop),"Redo did not restore materialized content");
      const auto afterNotes=snapshotNotes(loop);
      if (beforeNotes!=afterNotes) {
        std::printf("UNDO_NOTE_DIFFERENCE,before=%zu,after=%zu\n",beforeNotes.size()/5,afterNotes.size()/5);
        for (size_t n=0;n<std::min(beforeNotes.size(),afterNotes.size());++n)
          if (beforeNotes[n]!=afterNotes[n]) {
            std::printf("UNDO_NOTE_FIRST_DIFFERENCE,note=%zu,field=%zu,before=%u,after=%u\n",n/5,n%5,beforeNotes[n],afterNotes[n]);break;
          }
      }
      TEST_ASSERT_TRUE_MESSAGE(beforeNotes==afterNotes,"Redo changed published note identities or content");
      total+=entries;
    }
  }
  std::printf("UNDO_ROUNDTRIP,slots=%u,entries=%u\n",withHistory,total);
}
#else
void test_reconstructed_history_roundtrips() { TEST_IGNORE_MESSAGE("Isolated recovery ABI build required"); }
#endif

#ifndef RECOVERY_BOOT_SUITE
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reconstructed_workspace_loads);
  RUN_TEST(test_reconstructed_history_roundtrips);
#ifdef RECOVERY_TEENSY_ABI
  RUN_TEST(test_background_slot_commit_preserves_loaded_slot_redo);
#endif
  return UNITY_END();
}

#endif
