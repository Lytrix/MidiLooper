//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/PassReclaim.cpp"
#include "../../src/Loop/LoopPassReclaim.cpp"

#include "EditPass.h"
#include "Loop.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "MidiEvent.h"
#include "LoopEventStore.h"
#include "PassReclaim.h"
#include "Utils/MemoryMonitor.h"

namespace {

OverdubPass makeOverdubPass(PassId id, CapturePassState state) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  OverdubPass pass{};
  pass.id = id;
  pass.state = state;
  pass.committedChunkIds = std::move(committedChunkIds);
  pass.mergeSequence = id;
  return pass;
}

RecordPass makeRecordPass(PassId id) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 100)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

void consumeChunksUntilReserve() {
  LoopEventStore sink;
  while (LoopEventStore::canAllocChunkWithReserve()) {
    for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
      if (!sink.append(MidiEvent::NoteOn(i, 1, 60, 100))) {
        return;
      }
    }
  }
}

}  // namespace

void test_chunk_stats_match_pool_bitmap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  TEST_ASSERT_EQUAL(LoopEventStoreConfig::POOL_CHUNK_COUNT, LoopEventStore::freeChunkCount());
  TEST_ASSERT_EQUAL(0u, LoopEventStore::usedChunkCount());

  LoopEventStore store;
  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }
  TEST_ASSERT_EQUAL(1u, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL(LoopEventStoreConfig::POOL_CHUNK_COUNT - 1u, LoopEventStore::freeChunkCount());
}

void test_seal_succeeds_when_capture_pass_count_exceeds_25() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  MemoryMonitor::resetNativeTestFreeHeap();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPass(1);
  loop.passes.overdubPasses.reserve(26);
  for (PassId id = 2; id <= 27; ++id) {
    loop.passes.overdubPasses.push_back(makeOverdubPass(id, CapturePassState::Disabled));
  }
  TEST_ASSERT_TRUE(loop.passes.capturePassCount() > 25u);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(20, 1, 62, 90)));
  const SealOutcome seal = loop.sealCapture(100);
  TEST_ASSERT_EQUAL(static_cast<int>(SealOutcome::Ok), static_cast<int>(seal));
}

void test_seal_returns_pool_exhausted_when_reserve_violated() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  consumeChunksUntilReserve();
  TEST_ASSERT_FALSE(LoopEventStore::canAllocChunkWithReserve());

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(5, 1, 60, 100)));
  const SealOutcome seal = loop.sealCapture(50);
  TEST_ASSERT_EQUAL(static_cast<int>(SealOutcome::PoolExhausted),
                    static_cast<int>(seal));
}

void test_reclaim_disabled_overdub_when_unreferenced() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.passes.overdubPasses.push_back(makeOverdubPass(42, CapturePassState::Disabled));
  const uint16_t usedBefore = LoopEventStore::usedChunkCount();

  SlotPassReferences refs;
  loop.reclaimUnreferencedDisabledPasses(refs);

  TEST_ASSERT_EQUAL(0u, loop.passes.overdubPasses.size());
  TEST_ASSERT_TRUE(LoopEventStore::usedChunkCount() < usedBefore);
}

void test_reclaim_retains_disabled_pass_referenced_by_undo() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.passes.overdubPasses.push_back(makeOverdubPass(7, CapturePassState::Disabled));

  SlotPassReferences refs;
  refs.pinCapturePass(7);
  loop.reclaimUnreferencedDisabledPasses(refs);

  TEST_ASSERT_EQUAL(1u, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(7u, loop.passes.overdubPasses[0].id);
}

void test_collect_referenced_passes_pins_overdub_pass_ids() {
  PassReferenceSet refs{};
  GlobalUndoStack stack;
  UndoEntry entry;
  entry.kind = UndoEntryKind::OverdubPassAdded;
  entry.slotIndex = 1;
  entry.passId = 44;
  entry.passIds.push_back(42);
  entry.passIds.push_back(43);
  entry.passIds.push_back(44);
  entry.editPassIds.push_back(200);
  stack.entries.push_back(entry);

  collectReferencedPasses(stack, refs);
  TEST_ASSERT_TRUE(refs.slots[1].referencesCapturePass(42));
  TEST_ASSERT_TRUE(refs.slots[1].referencesCapturePass(43));
  TEST_ASSERT_TRUE(refs.slots[1].referencesCapturePass(44));
  TEST_ASSERT_TRUE(refs.slots[1].referencesEditPass(200));
}

void test_collect_referenced_passes_pins_clear_slot_snapshots() {
  PassReferenceSet refs{};
  GlobalUndoStack stack;
  UndoEntry entry;
  entry.kind = UndoEntryKind::ClearSlot;
  entry.slotIndex = 2;
  auto snapshot = std::make_shared<PersistedLoopSnapshot>();
  snapshot->passes.overdubPasses.push_back(makeOverdubPass(99, CapturePassState::Disabled));
  entry.beforeSnapshot = snapshot;
  stack.entries.push_back(entry);

  collectReferencedPasses(stack, refs);
  TEST_ASSERT_TRUE(refs.slots[2].referencesCapturePass(99));
}

void test_ninety_undo_entries_not_under_pressure_by_default() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  MemoryMonitor::resetNativeTestFreeHeap();

  GlobalUndoStack stack;
  for (uint16_t i = 0; i < 90; ++i) {
    UndoEntry entry;
    entry.kind = UndoEntryKind::OverdubPassAdded;
    entry.passId = i + 1;
    stack.entries.push_back(entry);
  }
  TEST_ASSERT_EQUAL(90u, stack.entries.size());
  TEST_ASSERT_FALSE(overUndoMemoryPressure(stack));
}

void test_save_note_edit_pass_rejected_when_heap_below_reserve() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  MemoryMonitor::setNativeTestFreeHeap(1024);

  Loop loop;
  EditPass del{};
  del.actionType = EditActionType::Delete;
  del.targetNoteId = 1;
  const size_t before = loop.passes.editPasses.size();
  const EditPassId id = loop.saveNoteEditPass(0, del);
  TEST_ASSERT_EQUAL(kInvalidEditPassId, id);
  TEST_ASSERT_EQUAL(before, loop.passes.editPasses.size());

  MemoryMonitor::resetNativeTestFreeHeap();
}

void test_save_note_edit_pass_succeeds_when_heap_headroom() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  MemoryMonitor::resetNativeTestFreeHeap();

  Loop loop;
  EditPass del{};
  del.actionType = EditActionType::Delete;
  del.targetNoteId = 1;
  const EditPassId id = loop.saveNoteEditPass(0, del);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, id);
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
}

void test_noncritical_work_deferred_when_heap_below_floor() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const uint32_t floor = LoopEventStore::internalHeapSafetyFloorBytes();
  TEST_ASSERT_TRUE(floor > 0u);

  MemoryMonitor::setNativeTestFreeHeap(floor - 1u);
  TEST_ASSERT_FALSE(
      LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
          MemoryMonitor::getInternalHeapFreeBytes()));

  MemoryMonitor::setNativeTestFreeHeap(floor);
  TEST_ASSERT_TRUE(
      LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
          MemoryMonitor::getInternalHeapFreeBytes()));
  MemoryMonitor::resetNativeTestFreeHeap();
}

void test_noncritical_work_admits_when_heap_recovers_after_stop_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const uint32_t floor = LoopEventStore::internalHeapSafetyFloorBytes();
  const uint32_t stopSnapshotHeap = floor - 4096u;
  TEST_ASSERT_TRUE(stopSnapshotHeap < floor);

  MemoryMonitor::setNativeTestFreeHeap(stopSnapshotHeap);
  TEST_ASSERT_FALSE(
      LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(stopSnapshotHeap));

  MemoryMonitor::setNativeTestFreeHeap(floor + 1024u);
  TEST_ASSERT_TRUE(
      LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
          MemoryMonitor::getInternalHeapFreeBytes()));
  MemoryMonitor::resetNativeTestFreeHeap();
}

void test_external_memory_first_buffers_report_storage_region() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  // Playback order vector was re-targeted to ExternalMemoryFirstAllocator.
  Loop loop;
  auto& playbackOrder = loop.getPlaybackOrder();
  playbackOrder.push_back(0u);
  TEST_ASSERT_NOT_NULL(playbackOrder.data());
#if EXTMEM_AVAILABLE
  TEST_ASSERT_TRUE(isInExternalMemoryPool(playbackOrder.data()));
#else
  TEST_ASSERT_FALSE(isInExternalMemoryPool(playbackOrder.data()));
#endif

  // Per-loop cached note list was re-targeted to ExternalMemoryFirstAllocator.
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
  events.push_back(MidiEvent::NoteOff(48, 1, 60, 0));
  NoteUtils::CachedNoteList cache;
  const auto& cachedNotes = cache.getNotes(events, 768);
  TEST_ASSERT_EQUAL(1u, cachedNotes.size());
  TEST_ASSERT_NOT_NULL(cachedNotes.data());
#if EXTMEM_AVAILABLE
  TEST_ASSERT_TRUE(isInExternalMemoryPool(cachedNotes.data()));
#else
  TEST_ASSERT_FALSE(isInExternalMemoryPool(cachedNotes.data()));
#endif

  loop.visualCache.notes.push_back(NoteUtils::DisplayNote{60, 100, 0, 48});
  TEST_ASSERT_NOT_NULL(loop.visualCache.notes.data());
  loop.capturePreview.notes.push_back(NoteUtils::DisplayNote{62, 100, 96, 144});
  TEST_ASSERT_NOT_NULL(loop.capturePreview.notes.data());
#if EXTMEM_AVAILABLE
  TEST_ASSERT_TRUE(isInExternalMemoryPool(loop.visualCache.notes.data()));
  TEST_ASSERT_TRUE(isInExternalMemoryPool(loop.capturePreview.notes.data()));
#else
  TEST_ASSERT_FALSE(isInExternalMemoryPool(loop.visualCache.notes.data()));
  TEST_ASSERT_FALSE(isInExternalMemoryPool(loop.capturePreview.notes.data()));
#endif
}

void test_trim_pressure_when_chunk_reserve_violated() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  consumeChunksUntilReserve();

  GlobalUndoStack stack;
  for (uint8_t i = 0; i < Config::MIN_UNDO_DEPTH + 4; ++i) {
    UndoEntry entry;
    entry.kind = UndoEntryKind::RecordPassAdded;
    entry.passId = i + 1;
    stack.entries.push_back(entry);
  }
  stack.cursor = stack.entries.size();
  TEST_ASSERT_TRUE(overUndoMemoryPressure(stack));
}

void test_trim_preserves_redo_branch_when_cursor_zero_no_pressure() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  MemoryMonitor::resetNativeTestFreeHeap();

  GlobalUndoStack stack;
  for (uint8_t i = 0; i < 3; ++i) {
    UndoEntry entry;
    entry.kind = UndoEntryKind::OverdubPassAdded;
    entry.passId = i + 1;
    stack.entries.push_back(entry);
  }
  stack.cursor = 0;
  TEST_ASSERT_EQUAL(3u, stack.redoCount());
  TEST_ASSERT_FALSE(overUndoMemoryPressure(stack));

  const size_t trimmed = trimGlobalUndoStackForMemory(stack);
  TEST_ASSERT_EQUAL(0u, trimmed);
  TEST_ASSERT_EQUAL(3u, stack.entries.size());
  TEST_ASSERT_EQUAL(0u, stack.cursor);
  TEST_ASSERT_EQUAL(3u, stack.redoCount());
}

void test_trim_drops_undo_side_before_redo_branch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  MemoryMonitor::resetNativeTestFreeHeap();

  GlobalUndoStack stack;
  for (uint8_t i = 0; i < Config::MIN_UNDO_DEPTH + 2; ++i) {
    UndoEntry entry;
    entry.kind = UndoEntryKind::RecordPassAdded;
    entry.passId = i + 1;
    stack.entries.push_back(entry);
  }
  stack.cursor = 2;
  const size_t redoBefore = stack.redoCount();
  TEST_ASSERT_TRUE(redoBefore > 0);

  consumeChunksUntilReserve();
  TEST_ASSERT_TRUE(overUndoMemoryPressure(stack));

  trimGlobalUndoStackForMemory(stack);
  TEST_ASSERT_TRUE(stack.entries.size() < Config::MIN_UNDO_DEPTH + 2);
  TEST_ASSERT_EQUAL(redoBefore, stack.redoCount());
}

void test_retained_undo_root_changes_feedback_based_trim_survivors() {
  for (bool retainRetiredRoot : {false, true}) {
    LoopEventStore::resetPoolForTests();
    LoopEventStore::initPool();
    MemoryMonitor::resetNativeTestFreeHeap();
    bool released = false;
    GlobalUndoStack stack;
    const size_t initialCount = Config::MIN_UNDO_DEPTH + 3;
    for (size_t i = 0; i < initialCount; ++i) {
      UndoEntry entry;
      entry.id = i + 1; entry.passId = i + 1;
      if (i == 0) {
        // Controlled pressure input models an oldest root whose destruction
        // recovers enough internal heap to clear pressure. No device byte-cost claim.
        entry.beforePublishedSnapshot = ImmutableLoopSnapshotRef(new LoopSnapshot(),
            [&released](const LoopSnapshot* root) {
              delete root;
              released = true;
              MemoryMonitor::setNativeTestFreeHeap(Config::HEAP_RESERVE_BYTES + 1024);
            });
      }
      stack.entries.push_back(std::move(entry));
    }
    stack.cursor = stack.entries.size();
    ImmutableLoopSnapshotRef retired;
    if (retainRetiredRoot) retired = stack.entries.front().beforePublishedSnapshot;
    MemoryMonitor::setNativeTestFreeHeap(Config::HEAP_RESERVE_BYTES - 1);
    TEST_ASSERT_TRUE(overUndoMemoryPressure(stack));
    const auto trimmed = trimGlobalUndoStackForMemory(stack);
    TEST_ASSERT_EQUAL_UINT32(retainRetiredRoot ? 3 : 1, trimmed);
    TEST_ASSERT_EQUAL_UINT32(retainRetiredRoot ? Config::MIN_UNDO_DEPTH : initialCount - 1,
                             stack.entries.size());
    TEST_ASSERT_EQUAL_UINT32(retainRetiredRoot ? 4 : 2, stack.entries.front().id);
    TEST_ASSERT_EQUAL(!retainRetiredRoot, released);
    TEST_ASSERT_EQUAL(retainRetiredRoot, overUndoMemoryPressure(stack));
    retired.reset();
    TEST_ASSERT_TRUE(released);
    MemoryMonitor::resetNativeTestFreeHeap();
  }
}

void test_new_push_clears_redo_branch() {
  GlobalUndoStack stack;
  for (uint8_t i = 0; i < 3; ++i) {
    UndoEntry entry;
    entry.kind = UndoEntryKind::OverdubPassAdded;
    entry.passId = i + 1;
    stack.entries.push_back(entry);
  }
  stack.cursor = 1;
  TEST_ASSERT_EQUAL(2u, stack.redoCount());

  if (stack.cursor < stack.entries.size()) {
    stack.entries.erase(stack.entries.begin() + static_cast<std::ptrdiff_t>(stack.cursor),
                        stack.entries.end());
  }
  UndoEntry fresh;
  fresh.kind = UndoEntryKind::OverdubPassAdded;
  fresh.passId = 99;
  stack.entries.push_back(std::move(fresh));
  stack.cursor = stack.entries.size();

  TEST_ASSERT_EQUAL(0u, stack.redoCount());
  TEST_ASSERT_EQUAL(2u, stack.entries.size());
  TEST_ASSERT_EQUAL(99u, stack.entries.back().passId);
}

void test_bounded_slot_reclaim_matches_reference_pins_and_preserves_audible_revision() {
  for (size_t quantum : {size_t(1), size_t(7), size_t(32)}) {
    LoopEventStore::resetPoolForTests();
    Loop reference, bounded;
    for (Loop* loop : {&reference, &bounded}) {
      loop->passes.recordPass = makeRecordPass(90);
      loop->passes.recordPass.state = CapturePassState::Disabled;
      for (PassId id : {9u,2u,12u,7u,4u,1u,6u,2u,0u}) {
        loop->passes.overdubPasses.push_back(makeOverdubPass(id,
            id % 2 == 0 ? CapturePassState::Disabled : CapturePassState::Active));
      }
      for (PassId id : {31u,5u,20u,11u,17u,42u}) {
        EditPass row; row.id = id;
        row.state = id == 20 ? EditPassState::Active : EditPassState::Disabled;
        row.addedEvents.resize(id == 5 ? 45 : 1);
        loop->passes.editPasses.push_back(std::move(row));
      }
    }
    GlobalUndoStack stack;
    UndoEntry pass; pass.kind = UndoEntryKind::OverdubPassAdded; pass.passIds = {12}; pass.editPassIds = {17};
    stack.entries.push_back(std::move(pass));
    UndoEntry edits; edits.kind = UndoEntryKind::NoteEditPassClosed; edits.editPassIds = {31,31};
    stack.entries.push_back(std::move(edits));
    UndoEntry clear; clear.kind = UndoEntryKind::ClearSlot;
    clear.beforeSnapshot = std::make_shared<PersistedLoopSnapshot>();
    clear.beforeSnapshot->passes.recordPass.id = 2;
    EditPass beforeEdit; beforeEdit.id = 11; clear.beforeSnapshot->passes.editPasses.push_back(beforeEdit);
    clear.afterSnapshot = std::make_shared<PersistedLoopSnapshot>();
    OverdubPass afterPass; afterPass.id = 6; clear.afterSnapshot->passes.overdubPasses.push_back(std::move(afterPass));
    EditPass afterEdit; afterEdit.id = 42; clear.afterSnapshot->passes.editPasses.push_back(afterEdit);
    stack.entries.push_back(std::move(clear));
    UndoEntry foreign; foreign.kind = UndoEntryKind::RecordPassAdded; foreign.passId = 4; foreign.slotIndex = 1;
    stack.entries.push_back(std::move(foreign));
    stack.cursor = 0; // Redo entries still pin resources.
    PassReferenceSet references;
    collectReferencedPasses(stack, references);
    const size_t expected = reference.reclaimUnreferencedDisabledCapturePasses(references.slots[0]) +
        reference.reclaimUnreferencedDisabledEditPasses(references.slots[0]);
    Loop::DisabledPassReclaim work;
    TEST_ASSERT_FALSE(work.begin(bounded, stack, 0));
    TEST_ASSERT_TRUE(work.prewarm(32));
    TEST_ASSERT_TRUE(work.begin(bounded, stack, 0));
    const auto* captureRows = bounded.passes.overdubPasses.data();
    const auto* editRows = bounded.passes.editPasses.data();
    const uint32_t revision = bounded.playbackRevision;
    const uint64_t generation = bounded.canonicalStorageGeneration;
    TEST_ASSERT_EQUAL(Loop::DisabledPassReclaim::Result::Pending, work.step(0));
    size_t turns = 0;
    while (!work.complete() && turns++ < 10000) {
      const auto before = work.completedUnits();
      TEST_ASSERT_NOT_EQUAL(Loop::DisabledPassReclaim::Result::Denied, work.step(quantum));
      TEST_ASSERT_TRUE(work.completedUnits() - before <= quantum);
      TEST_ASSERT_EQUAL_PTR(captureRows, bounded.passes.overdubPasses.data());
      TEST_ASSERT_EQUAL_PTR(editRows, bounded.passes.editPasses.data());
    }
    TEST_ASSERT_TRUE(work.complete());
    TEST_ASSERT_EQUAL(expected, work.reclaimedPasses());
    TEST_ASSERT_EQUAL(revision, bounded.playbackRevision); // Disabled content does not change the audible source.
    TEST_ASSERT_EQUAL(generation + 1, bounded.canonicalStorageGeneration);
    TEST_ASSERT_EQUAL(reference.passes.recordPass.id, bounded.passes.recordPass.id);
    TEST_ASSERT_EQUAL(reference.passes.overdubPasses.size(), bounded.passes.overdubPasses.size());
    TEST_ASSERT_EQUAL(reference.passes.editPasses.size(), bounded.passes.editPasses.size());
    for (size_t i = 0; i < reference.passes.overdubPasses.size(); ++i)
      TEST_ASSERT_EQUAL(reference.passes.overdubPasses[i].id, bounded.passes.overdubPasses[i].id);
    for (size_t i = 0; i < reference.passes.editPasses.size(); ++i)
      TEST_ASSERT_EQUAL(reference.passes.editPasses[i].id, bounded.passes.editPasses[i].id);
  }
}

void test_bounded_slot_reclaim_denies_stale_source_or_stack_before_mutation() {
  for (unsigned change = 0; change < 4; ++change) {
    Loop loop; loop.passes.recordPass.id = 9;
    loop.passes.recordPass.state = CapturePassState::Disabled;
    GlobalUndoStack stack;
    Loop::DisabledPassReclaim work;
    TEST_ASSERT_TRUE(work.prewarm(1));
    TEST_ASSERT_TRUE(work.begin(loop, stack, 0));
    TEST_ASSERT_EQUAL(Loop::DisabledPassReclaim::Result::Pending, work.step(1));
    if (change == 0) ++loop.canonicalStorageGeneration;
    if (change == 1) ++loop.playbackRevision;
    if (change == 2) ++stack.nextEntryId;
    if (change == 3) stack.entries.emplace_back();
    TEST_ASSERT_EQUAL(Loop::DisabledPassReclaim::Result::Denied, work.step(32));
    TEST_ASSERT_EQUAL(9u, loop.passes.recordPass.id);
  }
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_bounded_slot_reclaim_matches_reference_pins_and_preserves_audible_revision);
  RUN_TEST(test_bounded_slot_reclaim_denies_stale_source_or_stack_before_mutation);
  RUN_TEST(test_chunk_stats_match_pool_bitmap);
  RUN_TEST(test_seal_succeeds_when_capture_pass_count_exceeds_25);
  RUN_TEST(test_seal_returns_pool_exhausted_when_reserve_violated);
  RUN_TEST(test_reclaim_disabled_overdub_when_unreferenced);
  RUN_TEST(test_reclaim_retains_disabled_pass_referenced_by_undo);
  RUN_TEST(test_collect_referenced_passes_pins_overdub_pass_ids);
  RUN_TEST(test_collect_referenced_passes_pins_clear_slot_snapshots);
  RUN_TEST(test_ninety_undo_entries_not_under_pressure_by_default);
  RUN_TEST(test_save_note_edit_pass_rejected_when_heap_below_reserve);
  RUN_TEST(test_save_note_edit_pass_succeeds_when_heap_headroom);
  RUN_TEST(test_noncritical_work_deferred_when_heap_below_floor);
  RUN_TEST(test_noncritical_work_admits_when_heap_recovers_after_stop_snapshot);
  RUN_TEST(test_external_memory_first_buffers_report_storage_region);
  RUN_TEST(test_trim_pressure_when_chunk_reserve_violated);
  RUN_TEST(test_trim_preserves_redo_branch_when_cursor_zero_no_pressure);
  RUN_TEST(test_trim_drops_undo_side_before_redo_branch);
  RUN_TEST(test_retained_undo_root_changes_feedback_based_trim_survivors);
  RUN_TEST(test_new_push_clears_redo_branch);
  return UNITY_END();
}
