// S7.5b.6: real accepted Capture -> draw-source -> paint phases -> native DMA endpoint.
// Raster/SPI are hardware boundaries; source selection, geometry and publication are real.
#define HARNESS_REAL_CAPTURE_STOP
#define HARNESS_REAL_PLAYBACK_HOT_PATH
#define HARNESS_LINKS_REAL_DISPLAY_MANAGER
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <vector>
#include "../test_support/capture_storage_harness.inc"
// `Overlay.cpp` now arrives with `capture_storage_harness.inc` (the commit owner needs its catalog-change
// signal), so it is no longer included here separately.
#include "../../src/StorageManager/StorageActivitySnapshot.cpp"
#include "../../src/OverlayCatalogReadPolicy.cpp"
// Include-based harnesses merge TU namespaces. Keep display's internal owner unambiguous.
#define resolveTrackIndex resolveDisplayTrackIndex
#include "DisplayManagerInternal.h"
#include "../../src/DisplayManager/DisplayColdHelpers.cpp"
#include "../../src/DisplayManager/DisplayTickResolve.cpp"
#include "../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp"
#define findActiveSealedCompanionRow findActiveSealedCompanionDrawRow
#include "../../src/DisplayManager/DisplayNoteWindowGather.cpp"
#undef findActiveSealedCompanionRow
#include "../../src/DisplayManager/DisplayNoteResolve.cpp"
#include "../../src/DisplayManager/DisplayCacheLifecycle.cpp"
template<class T, class L, class H> T constrain(T value, L low, H high) {
  return std::max(static_cast<T>(low), std::min(value, static_cast<T>(high)));
}
#include "../../src/DisplayManager/PianoRollDraw.cpp"
#undef resolveTrackIndex
// Exercise the target's actual transfer-admission branch against a native DMA I/O boundary.
#define __IMXRT1062__ 1
#include "../../src/DisplayManager.cpp"
#undef __IMXRT1062__
#include "../../src/DisplayManager/BootDisplay.cpp"

// Presentation-only neighbours, not exercised domain alternatives.
void DisplayManager::drawTrackStatus(uint8_t, uint32_t) {}
void DisplayManager::drawSidebar(Track&, uint8_t) {}
void DisplayManager::drawInfoArea(uint32_t, Track&, uint8_t, uint32_t) {}
void DisplayManager::drawNoteInfo(uint32_t, Track&, uint8_t, const DisplayNoteVec&) {}
void DisplayManager::drawLoadSaveView(uint32_t) { TEST_FAIL_MESSAGE("load/save paint off path"); }
void DisplayManager::refreshAutoSaveBeforeLoadToast(uint32_t) {}
DisplayNoteVec EditManager::projectedNoteEditDisplayNotes(const Track&) const {
  TEST_FAIL_MESSAGE("NOTE_EDIT projection off path"); return {};
}
void EditManager::markNoteEditDisplayPainted() {}

struct DisplayCaptureTestAccess {
  static const DisplayNoteVec& notes() { return displayManager.resolvedPaintNotes(); }
  static auto phase() { return displayManager.pendingPaintPhase_; }
  static auto& api() { return displayManager._display.api; }
  static auto key() { return displayManager.liveDrawSourceKey_; }
  static uint8_t track() { return displayManager.paintTrackIndex_; }
  static uint8_t slot() { return displayManager.paintSlot_; }
  static uint32_t rectangles() { return displayManager._display.gfx.rectangles; }
  static uint32_t pianoRows() { return displayManager._display.gfx.pianoRows; }
  static size_t committedPrefixSize() { return displayManager.liveDisplayCommittedPrefix_.size(); }
};
struct FrameReceipt {
  DisplayNoteVec notes;
  DisplayWindowUtils::DrawSourceGatherKey source;
  uint8_t track = 255, slot = 255;
  uint32_t timestamp = 0;
  uint32_t previewFrontier = 0, rectangles = 0, pianoRows = 0;
};
static FrameReceipt submittedFrame, completedFrame;
static uint32_t resolvePreviewFrontier = 0;
static void onSubmit() {
  submittedFrame = {DisplayCaptureTestAccess::notes(), DisplayCaptureTestAccess::key(),
                    DisplayCaptureTestAccess::track(), DisplayCaptureTestAccess::slot(), micros()};
  submittedFrame.previewFrontier = resolvePreviewFrontier;
  submittedFrame.rectangles = DisplayCaptureTestAccess::rectangles();
  submittedFrame.pianoRows = DisplayCaptureTestAccess::pianoRows();
}
static void onComplete() { completedFrame = submittedFrame; completedFrame.timestamp = micros(); }

void setUp() {
  displayManager.~DisplayManager();
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  new (&displayManager) DisplayManager();
  A34aBoundary::resetSlotFlags();
  nativeMillisSet(1000);
  nativeTransportRunning = true;
  clockManager.assignCurrentTickSilently(0);
  submittedFrame = {}; completedFrame = {};
  nativeDisplayOnSubmit = onSubmit;
  nativeDisplayOnComplete = onComplete;
}
void tearDown() {}

static Track& beginRecord() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  t.startRecording(0);
  TEST_ASSERT_TRUE(t.isRecording());
  return t;
}
static NoteId acceptPair(Track& t, uint8_t pitch, uint32_t on, uint32_t off) {
  Loop& loop = t.getActiveLoop();
  const size_t before = loop.capture.store.size();
  t.noteOn(1, pitch, 100, on);
  t.noteOff(1, pitch, 0, off);
  TEST_ASSERT_EQUAL_UINT32(before + 2, loop.capture.store.size());
  const MidiEvent& accepted = loop.capture.store.at(before);
  TEST_ASSERT_EQUAL_UINT8(pitch, accepted.data.noteData.note);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, accepted.noteId);
  return accepted.noteId;
}
static void serviceDisplay(uint32_t tick) {
  clockManager.assignCurrentTickSilently(tick);
  if (DisplayCaptureTestAccess::phase() == DisplayWindowUtils::DisplayFramePhase::Idle) {
    resolvePreviewFrontier = trackManager.getSelectedTrack().getActiveLoop().capturePreviewView().revision;
  }
  nativeMillisAdvance(1);
  displayManager.update();
}
static void submitFrame(uint32_t tick) {
  TEST_ASSERT_FALSE(displayManager.hasPendingPaintPhase());
  serviceDisplay(tick); serviceDisplay(tick); serviceDisplay(tick);
  TEST_ASSERT_FALSE(displayManager.hasPendingPaintPhase());
}
static void completeFrame() {
  auto& api = DisplayCaptureTestAccess::api();
  TEST_ASSERT_TRUE(api.active);
  api.completionRequested = true;
  api.pollDisplayTransfer();
  TEST_ASSERT_FALSE(api.active);
}
static void assertNote(const DisplayNoteVec& notes, NoteId id, uint8_t pitch,
                       uint32_t start, uint32_t end) {
  size_t found = 0;
  for (const auto& note : notes) {
    if (note.noteId != id) continue;
    ++found;
    TEST_ASSERT_EQUAL_UINT8(pitch, note.note);
    TEST_ASSERT_EQUAL_UINT32(start, note.startTick);
    TEST_ASSERT_EQUAL_UINT32(end, note.endTick);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, found, "accepted NoteId must occur exactly once in the frame");
}
static void growRecordGeometry(Track& t, uint32_t tick) {
  clockManager.assignCurrentTickSilently(tick);
  t.updateGrowingRecordLoopLength(tick);
}

void test_record_note_ids_reach_distinct_submission_and_completion() {
  Track& t = beginRecord();
  const NoteId a = acceptPair(t,60,12,80), b = acceptPair(t,62,800,880);
  growRecordGeometry(t,960);
  submitFrame(960);
  auto& api = DisplayCaptureTestAccess::api();
  TEST_ASSERT_EQUAL_UINT32(1,api.submitted);
  TEST_ASSERT_EQUAL_UINT32(0,api.completions);
  TEST_ASSERT_EQUAL_UINT32(2,submittedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT32(2,submittedFrame.rectangles);
  TEST_ASSERT_EQUAL_UINT32(t.getActiveLoop().capturePreviewView().revision,submittedFrame.previewFrontier);
  TEST_ASSERT_EQUAL_UINT8(0,submittedFrame.track);
  TEST_ASSERT_EQUAL_UINT8(0,submittedFrame.slot);
  assertNote(submittedFrame.notes,a,60,12,80);
  assertNote(submittedFrame.notes,b,62,800,880);
  const auto immutableBytes = api.inFlight;
  completeFrame();
  TEST_ASSERT_EQUAL_UINT32(1,api.completions);
  TEST_ASSERT_TRUE(api.completed == immutableBytes);
  assertNote(completedFrame.notes,a,60,12,80);
  assertNote(completedFrame.notes,b,62,800,880);
  TEST_ASSERT_FALSE(t.getActiveLoop().publishedSnapshotIsCurrent());
}

void test_input_after_resolve_waits_for_next_frame_without_mutating_in_flight() {
  Track& t = beginRecord();
  const NoteId a = acceptPair(t,60,12,80);
  growRecordGeometry(t,160);
  const uint32_t oldFrontier=t.getActiveLoop().capturePreviewView().revision;
  serviceDisplay(160); // real Resolve
  const NoteId b = acceptPair(t,64,192,240);
  serviceDisplay(256); serviceDisplay(256);
  TEST_ASSERT_EQUAL_UINT32(1,submittedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT32(oldFrontier,submittedFrame.previewFrontier);
  assertNote(submittedFrame.notes,a,60,12,80);
  completeFrame();
  TEST_ASSERT_EQUAL_UINT32(oldFrontier,completedFrame.previewFrontier);
  submitFrame(256);
  TEST_ASSERT_EQUAL_UINT32(2,submittedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT32(t.getActiveLoop().capturePreviewView().revision,submittedFrame.previewFrontier);
  assertNote(submittedFrame.notes,b,64,192,240);
  completeFrame();
  assertNote(completedFrame.notes,b,64,192,240);
}

void test_busy_transfer_keeps_complete_frame_pending_and_receipts_separate() {
  Track& t = beginRecord();
  const NoteId a = acceptPair(t,60,12,80);
  growRecordGeometry(t,160);
  submitFrame(160);
  const auto firstBytes = DisplayCaptureTestAccess::api().inFlight;
  const NoteId b = acceptPair(t,64,192,240);
  serviceDisplay(256); serviceDisplay(256); serviceDisplay(256);
  TEST_ASSERT_TRUE(displayManager.hasPendingPaintPhase());
  TEST_ASSERT_EQUAL(DisplayWindowUtils::DisplayFramePhase::Transfer,DisplayCaptureTestAccess::phase());
  TEST_ASSERT_EQUAL_UINT32(1,DisplayCaptureTestAccess::api().submitted);
  TEST_ASSERT_EQUAL_UINT32(0,DisplayCaptureTestAccess::api().completions);
  TEST_ASSERT_TRUE(DisplayCaptureTestAccess::api().inFlight == firstBytes);
  completeFrame();
  assertNote(completedFrame.notes,a,60,12,80);
  serviceDisplay(256);
  TEST_ASSERT_FALSE(displayManager.hasPendingPaintPhase());
  TEST_ASSERT_EQUAL_UINT32(2,DisplayCaptureTestAccess::api().submitted);
  assertNote(submittedFrame.notes,b,64,192,240);
  completeFrame();
  TEST_ASSERT_EQUAL_UINT32(2,DisplayCaptureTestAccess::api().completions);
  assertNote(completedFrame.notes,b,64,192,240);
}

struct TrackCaptureStopTestAccess {
  static auto* runtime(Track& t) { return t.playbackRuntime.slotIfAllocated(t.getActiveLoopIndex()); }
};
struct GapTake { Track* track; NoteId a,b,c; };
static GapTake seedGapOverdub(bool early=false, bool withLivePair=true) {
  Track& t = beginRecord();
  const NoteId a = acceptPair(t,60,12,80), b = acceptPair(t,62,1328,1408);
  growRecordGeometry(t,1536);
  submitFrame(1536); completeFrame();
  TEST_ASSERT_EQUAL_UINT32(4,t.getActiveLoop().capture.store.size());
  t.stopRecording(1536);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1536,t.getActiveLoop().loopLengthTicks,"fixture must stop a two-bar take");
  TEST_ASSERT_TRUE(t.isPlaying());
  Loop& loop=t.getActiveLoop();
  if (early) {
    // Display's early-overdub contract is pre-publication. Closure leaves the sealed Record source
    // borrowed by the Loop; a presentation-owned MIDI successor is a later, post-publication role.
    t.processDeferredIdleMaintenance(0,false,MidiServiceDrain::DeferredIdleOwner::VisualCache,1);
    auto* runtime=TrackCaptureStopTestAccess::runtime(t);
    TEST_ASSERT_NOT_NULL(runtime);
    TEST_ASSERT_TRUE(loop.hasSealedCaptureBorrow());
    TEST_ASSERT_TRUE(runtime->hasBoundRecordReturnWindowForPendingJob());
    TEST_ASSERT_FALSE(loop.hasCommittedPasses());
    TEST_ASSERT_FALSE(loop.pendingSnapshotSessionExitQueued());
    TEST_ASSERT_TRUE(t.hasStopInFlightForSlot(0));
    TEST_ASSERT_FALSE(t.hasStopInFlightForSlot(1));
  } else {
    for (unsigned i=0;i<4096 && !loop.hasCommittedPasses();++i)
      t.processDeferredIdleMaintenance(0,false,MidiServiceDrain::DeferredIdleOwner::VisualCache,1);
    TEST_ASSERT_TRUE(loop.hasCommittedPasses());
    TEST_ASSERT_TRUE(loop.isPlaybackSessionExitPublicationGap());
  }
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  clockManager.assignCurrentTickSilently(1664);
  t.startOverdubbing(1664);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const NoteId c=withLivePair?acceptPair(t,70,1728,1776):kInvalidNoteId;
  return {&t,a,b,c};
}
static void assertGapFrame(const GapTake& take) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(3,submittedFrame.notes.size(),
                                 "fresh accepted overlay must not wait for whole Record Snapshot");
  assertNote(submittedFrame.notes,take.a,60,12,80);
  assertNote(submittedFrame.notes,take.b,62,1328,1408);
  assertNote(submittedFrame.notes,take.c,70,192,240);
  TEST_ASSERT_EQUAL_UINT32(2,DisplayCaptureTestAccess::committedPrefixSize());
}
void test_live_overdub_note_reaches_frame_before_record_snapshot_publication() {
  const GapTake take=seedGapOverdub();
  submitFrame(1792); assertGapFrame(take);
  completeFrame();
  assertNote(completedFrame.notes,take.c,70,192,240);
  TEST_ASSERT_TRUE(take.track->getActiveLoop().isPlaybackSessionExitPublicationGap());
  TEST_ASSERT_FALSE(take.track->getActiveLoop().publishedSnapshotIsCurrent());
}
void test_repeated_gap_frames_keep_overlay_out_of_committed_prefix() {
  const GapTake take=seedGapOverdub();
  for (unsigned turn=0;turn<4;++turn) {
    submitFrame(1792+turn); assertGapFrame(take); completeFrame();
  }
  const NoteId d=acceptPair(*take.track,71,1800,1848);
  submitFrame(1856);
  TEST_ASSERT_EQUAL_UINT32(4,submittedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT32(2,DisplayCaptureTestAccess::committedPrefixSize());
  assertNote(submittedFrame.notes,take.a,60,12,80);
  assertNote(submittedFrame.notes,take.b,62,1328,1408);
  assertNote(submittedFrame.notes,take.c,70,192,240);
  assertNote(submittedFrame.notes,d,71,264,312);
  completeFrame(); assertNote(completedFrame.notes,d,71,264,312);
}
void test_current_root_handoff_preserves_base_and_live_identity_once() {
  const GapTake take=seedGapOverdub();
  submitFrame(1792); assertGapFrame(take); completeFrame();
  Loop& loop=take.track->getActiveLoop();
  for (unsigned turn=0;turn<4096 && !loop.publishedSnapshotIsCurrent();++turn)
    loop.stepPendingSnapshotPublish(4,true,0);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loop.isPlaybackSessionExitPublicationGap());
  DisplayNoteVec canonical, published;
  LoopContentResolution::resolveDisplayNotes(loop.passes,loop.loopLengthTicks,canonical);
  LoopContentResolution::resolveDisplayNotes(*loop.publishedSnapshot(),published);
  char rootState[180];
  std::snprintf(rootState,sizeof(rootState),"canonical=%zu root=%zu a=%lu b=%lu c=%lu root0=%lu/%u root1=%lu/%u",
      canonical.size(),published.size(),static_cast<unsigned long>(take.a),
      static_cast<unsigned long>(take.b),static_cast<unsigned long>(take.c),
      published.empty()?0ul:static_cast<unsigned long>(published[0].noteId),
      published.empty()?0u:static_cast<unsigned>(published[0].note),
      published.size()<2?0ul:static_cast<unsigned long>(published[1].noteId),
      published.size()<2?0u:static_cast<unsigned>(published[1].note));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(canonical.size(),published.size(),rootState);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2,canonical.size(),rootState);
  TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(loop.publishedSnapshot()->findNote(take.a,60)), "current root must contain first accepted Record NoteId");
  TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(loop.publishedSnapshot()->findNote(take.b,62)),rootState);
  TEST_ASSERT_TRUE_MESSAGE(loop.captureActive(), "Record root publication cannot close live overdub Capture");
  assertNote(loop.capturePreviewView().notes,take.c,70,192,240);
  submitFrame(1792);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2,DisplayCaptureTestAccess::committedPrefixSize(), "published draw source must retain both Record notes");
  assertGapFrame(take);
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),submittedFrame.source.publishedSnapshotIdentity);
  completeFrame(); assertNote(completedFrame.notes,take.c,70,192,240);
}

void test_moving_window_gap_never_mixes_old_prefix_with_new_viewport_overlay() {
  constexpr uint32_t bar=Config::TICKS_PER_BAR, length=24*bar;
  Track& t=beginRecord();
  const NoteId a=acceptPair(t,60,12,80);
  const NoteId b=acceptPair(t,62,8*bar+20,8*bar+80);
  const NoteId z=acceptPair(t,65,23*bar+500,23*bar+640);
  growRecordGeometry(t,length);
  submitFrame(length-16);
  TEST_ASSERT_EQUAL_UINT32(8*bar-16,submittedFrame.source.windowStart);
  TEST_ASSERT_EQUAL_UINT32(16*bar,submittedFrame.source.windowLength);
  TEST_ASSERT_EQUAL_UINT32(2,submittedFrame.notes.size());
  assertNote(submittedFrame.notes,b,62,8*bar+20,8*bar+80);
  assertNote(submittedFrame.notes,z,65,23*bar+500,23*bar+640);
  completeFrame();
  t.stopRecording(length);
  TEST_ASSERT_EQUAL_UINT32(length,t.getActiveLoop().loopLengthTicks);
  for (unsigned turn=0;turn<4096 && !t.getActiveLoop().hasCommittedPasses();++turn)
    t.processDeferredIdleMaintenance(0,false,MidiServiceDrain::DeferredIdleOwner::VisualCache,1);
  Loop& loop=t.getActiveLoop();
  TEST_ASSERT_TRUE(loop.isPlaybackSessionExitPublicationGap());
  clockManager.assignCurrentTickSilently(length+128);
  t.startOverdubbing(length+128);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const NoteId c=acceptPair(t,70,length+192,length+240);
  submitFrame(length+256);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(8*bar-16,submittedFrame.source.windowStart,
      "held content must retain its qualified interval, not paint a new viewport");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2,submittedFrame.notes.size(),
      "outside-window live overlay cannot mix with held committed interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2,submittedFrame.pianoRows,
      "rasterized held interval must include both committed notes");
  assertNote(submittedFrame.notes,b,62,8*bar+20,8*bar+80);
  assertNote(submittedFrame.notes,z,65,23*bar+500,23*bar+640);
  completeFrame();
  TEST_ASSERT_TRUE(loop.isPlaybackSessionExitPublicationGap());
  for (unsigned turn=0;turn<8192 && !loop.publishedSnapshotIsCurrent();++turn)
    loop.stepPendingSnapshotPublish(4,true,0);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  submitFrame(length+256);
  TEST_ASSERT_EQUAL_UINT32(0,submittedFrame.source.windowStart);
  TEST_ASSERT_EQUAL_UINT32(3,submittedFrame.notes.size());
  assertNote(submittedFrame.notes,a,60,12,80);
  assertNote(submittedFrame.notes,b,62,8*bar+20,8*bar+80);
  assertNote(submittedFrame.notes,c,70,192,240);
  completeFrame();
  assertNote(completedFrame.notes,c,70,192,240);
}

void test_early_overdub_entry_keeps_record_base_before_first_note() {
  GapTake take=seedGapOverdub(true,false);
  submitFrame(1664);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2,submittedFrame.notes.size(),
      "early overdub must not blank the complete recorded base before pass publication");
  assertNote(submittedFrame.notes,take.a,60,12,80);
  assertNote(submittedFrame.notes,take.b,62,1328,1408);
  completeFrame();
  take.c=acceptPair(*take.track,70,1728,1776);
  submitFrame(1792); assertGapFrame(take); completeFrame();
  TEST_ASSERT_FALSE(take.track->getActiveLoop().hasCommittedPasses());
  TEST_ASSERT_FALSE(take.track->getActiveLoop().pendingSnapshotSessionExitQueued());
}
void test_early_overdub_overlay_survives_later_record_pass_publication() {
  const GapTake take=seedGapOverdub(true);
  submitFrame(1792); assertGapFrame(take); completeFrame();
  Loop& loop=take.track->getActiveLoop();
  for (unsigned turn=0;turn<4096 && !loop.hasCommittedPasses();++turn)
    take.track->processDeferredIdleMaintenance(0,false,MidiServiceDrain::DeferredIdleOwner::VisualCache,1);
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  submitFrame(1792); assertGapFrame(take); completeFrame();
  assertNote(completedFrame.notes,take.c,70,192,240);
}

static void test_boot_title_is_first_transfer_and_survives_completion_and_hold() {
  auto& api = DisplayCaptureTestAccess::api();
  displayManager.beginBootOled();
  TEST_ASSERT_EQUAL_UINT32(0, api.submitted);
  TEST_ASSERT_FALSE(api.active);
  displayManager.drawBootScreen();
  TEST_ASSERT_EQUAL_UINT32(1, api.submitted);
  TEST_ASSERT_TRUE(api.active);
  TEST_ASSERT_EQUAL_UINT32(0, api.completions);
  const auto title = api.buffer;
  TEST_ASSERT_TRUE(std::any_of(title.begin(), title.end(), [](uint8_t byte) {
    return (byte >> 4) == 12 || (byte & 15) == 12;
  }));
  TEST_ASSERT_EQUAL_MEMORY(title.data(), api.inFlight.data(), title.size());
  api.completionRequested = true;
  displayManager.update();
  TEST_ASSERT_EQUAL_UINT32(1, api.completions);
  TEST_ASSERT_FALSE(api.active);
  TEST_ASSERT_EQUAL_MEMORY(title.data(), api.completed.data(), title.size());
  displayManager.finishBootSetup();
  nativeMillisAdvance(100);
  displayManager.update();
  TEST_ASSERT_EQUAL_UINT32(1, api.submitted);
  TEST_ASSERT_FALSE(displayManager.hasPendingPaintPhase());
  TEST_ASSERT_EQUAL_MEMORY(title.data(), api.buffer.data(), title.size());
  LoopEventStore::initPool();
  trackManager.getSelectedTrack().ensureLoopsAllocated();
  nativeMillisAdvance(2000);
  displayManager.update();
  TEST_ASSERT_TRUE(displayManager.hasPendingPaintPhase());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_title_is_first_transfer_and_survives_completion_and_hold);
  RUN_TEST(test_record_note_ids_reach_distinct_submission_and_completion);
  RUN_TEST(test_input_after_resolve_waits_for_next_frame_without_mutating_in_flight);
  RUN_TEST(test_busy_transfer_keeps_complete_frame_pending_and_receipts_separate);
  RUN_TEST(test_live_overdub_note_reaches_frame_before_record_snapshot_publication);
  RUN_TEST(test_repeated_gap_frames_keep_overlay_out_of_committed_prefix);
  RUN_TEST(test_current_root_handoff_preserves_base_and_live_identity_once);
  RUN_TEST(test_moving_window_gap_never_mixes_old_prefix_with_new_viewport_overlay);
  RUN_TEST(test_early_overdub_entry_keeps_record_base_before_first_note);
  RUN_TEST(test_early_overdub_overlay_survives_later_record_pass_publication);
  return UNITY_END();
}

void DisplayManager::refreshLoadSaveListCache() { TEST_FAIL_MESSAGE("load/save catalog off path"); }
void DisplayManager::adjustLoadSaveListSelection(int) { TEST_FAIL_MESSAGE("load/save navigation off path"); }
void DisplayManager::invalidateLoadSaveDetailCache() { TEST_FAIL_MESSAGE("load/save invalidation off path"); }
void DisplayManager::invalidateLoadSaveRevisionListCache() { TEST_FAIL_MESSAGE("load/save invalidation off path"); }
void EditManager::rematerializeNoteEditSessionAfterWorkspaceReload(Track&) {
  TEST_FAIL_MESSAGE("workspace reload off path");
}
bool EditManager::isLengthBracketEditActive() const {
  TEST_ASSERT_FALSE(isNoteEditActive()); return false;
}
void EditManager::invalidateProjectedNoteEditDisplayCache() const { TEST_ASSERT_FALSE(isNoteEditActive()); }
float ClockManager::getDisplayTickPhase() const { return static_cast<float>(getCurrentTick()); }
