// S7.5b.6: real accepted Capture -> draw-source -> paint phases -> native DMA endpoint.
// Raster/SPI are hardware boundaries; source selection, geometry and publication are real.
#define PLAYBACK_WINDOW_ROLLING_ENABLED 1 // Match the shipping capture build's owner interleavings.
#define HARNESS_REAL_CAPTURE_STOP
#define HARNESS_REAL_PLAYBACK_HOT_PATH
#define HARNESS_LINKS_REAL_DISPLAY_MANAGER
// DEC-210: drive the production `TrackManager::setSelectedTrack` path; the edit-session
// neighbours are presentation-only here and must not fail the focus-change fixture.
#define HARNESS_OBSERVABLE_TRACK_SELECT
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
  static const DisplayNoteVec& resolveCommitted(const Track& track, uint8_t slot, uint32_t tick) {
    return displayManager.resolveDisplayNotesByDrawSource(track, slot, tick, 0, 127);
  }
  static const DisplayNoteVec& liveCapture(const Track& track, uint8_t slot, uint32_t tick) {
    return displayManager.resolveDisplayNotesLiveCapture(track, slot, tick);
  }
  /// Build/qualify the display's committed base directly, for the ownership contract test.
  static bool buildCommittedBase(const Track& track, uint8_t slot, uint32_t tick) {
    Loop& mutLoop = const_cast<Loop&>(track.getLoop(slot));
    const Loop& loop = track.getLoop(slot);
    const uint32_t loopLen = displayManager.resolveDisplayLoopLength(track, slot, tick);
    uint32_t start = 0;
    uint32_t len = 0;
    uint8_t bars = 0;
    displayManager.syncDetailedPaintWindow(track, slot, tick, loopLen, start, len, bars);
    displayManager.buildCommittedDisplayBase(loop, mutLoop, slot, loopLen, tick, true, start, len);
    return displayManager.committedDisplayBaseMatches(loop, slot, loopLen, true, start, len);
  }
  static const DisplayNoteVec& committedBaseNotes() { return displayManager.liveDisplayNotes; }
  static uint8_t track() { return displayManager.paintTrackIndex_; }
  static uint8_t slot() { return displayManager.paintSlot_; }
  static uint32_t rectangles() { return displayManager._display.gfx.rectangles; }
  static uint32_t pianoRows() { return displayManager._display.gfx.pianoRows; }
  static size_t committedPrefixSize() { return displayManager.liveDisplayCommittedPrefix_.size(); }
  // The split's two facts (`DIAG,disp_turn`): what the playhead asked for this turn, and the interval the frame
  // is **drawn** in. Under a content hold they must differ from the content key; outside one they coincide.
  static uint32_t requestedViewStart() { return displayManager.detailedWindowRequestedStartTick_; }
  static uint32_t requestedViewLength() { return displayManager.detailedWindowRequestedLengthTicks_; }
  static uint32_t drawnViewStart() { return displayManager.detailedWindowViewportStartTick_; }
  static uint32_t drawnViewLength() { return displayManager.detailedWindowViewportLengthTicks_; }
  static uint8_t drawnViewSlot() { return displayManager.detailedWindowViewportSlot_; }
  /// The piano-roll zoom: `detailedWindowBars_` is the requested window width in bars (0 = max).
  static void setDetailedWindowBars(uint8_t slot, uint8_t bars) {
    displayManager.detailedWindowBars_[slot] = bars;
  }
  static const char* qualifiedSourceName(const Loop& loop) {
    switch (loop.lastQualifiedDisplaySource()) {
      case Loop::QualifiedDisplaySource::BoundedContinuation: return "bound";
      case Loop::QualifiedDisplaySource::RetainedEnvelope: return "retained";
      case Loop::QualifiedDisplaySource::PreparedWindow: return "prepared";
      case Loop::QualifiedDisplaySource::VisualCache: return "cache";
      case Loop::QualifiedDisplaySource::None: break;
    }
    return "none";
  }
  static const VisualBarVec* overview(const Track& t, const Loop& loop, uint8_t slot) {
    return displayManager.updateOverviewCaptureDensity(t, loop, slot, loop.loopLengthTicks);
  }
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
  LoopContentResolution::boundedDisplayCancel();
  LoopContentResolution::deviceGateReset();
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
  static size_t jobs(const Track& t) { return t.captureStopJobs_ ? t.captureStopJobs_->count : 0; }
  static auto* runtime(Track& t) { return t.playbackRuntime.slotIfAllocated(t.getActiveLoopIndex()); }
  static bool retiredPreviewsEmpty(const Track& t) {
    if (!t.captureStopJobs_ || t.captureStopJobs_->count != 0) return false;
    for (const auto& job : t.captureStopJobs_->entries)
      if (!job.preview.notes.empty() || !job.preview.openNoteIndices.empty()) return false;
    return true;
  }
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
  // Content and viewport are two facts now (disp_turn split): the content key keeps the retained interval and the
  // retained rows are never re-gathered, while the *drawn* interval follows the playhead so the cursor cannot be
  // left behind. The drawn rows are therefore the retained rows that fall inside the requested viewport.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(8*bar-16,submittedFrame.source.windowStart,
      "the content key must stay the retained interval while the viewport follows the request");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,DisplayCaptureTestAccess::drawnViewStart(),
      "the drawn viewport must be the interval the playhead requested");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(16*bar,DisplayCaptureTestAccess::drawnViewLength(),
      "the drawn viewport must carry the requested length");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(DisplayCaptureTestAccess::requestedViewStart(),
      DisplayCaptureTestAccess::drawnViewStart(),
      "the drawn viewport must equal the requested interval, not the retained one");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2,submittedFrame.notes.size(),
      "outside-window live overlay cannot mix with held committed interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1,submittedFrame.pianoRows,
      "rasterized rows must be the retained rows inside the requested viewport (b; z is outside it)");
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

// Seed committed storage without a Snapshot or stop obligation, then open an
// empty overdub so ordinary Record-stop frame retention cannot mask fallback
// invalidation. Track's deferred idle owner calls the same rebuild entry on hardware.
void test_partial_visual_cache_publication_refreshes_stationary_draw_source() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  Loop& loop = t.getActiveLoop();
  LoopEventStore record;
  for (uint32_t i = 0; i < 32; ++i) {
    MidiEvent on = MidiEvent::NoteOn(i * bar + 12, 1, 60, 100);
    on.noteId = 100 + i;
    TEST_ASSERT_TRUE(record.append(on));
    TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(i * bar + 80, 1, 60, 0)));
  }
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = 32 * bar;
  loop.markDisplayCachesStale();
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.captureActive());
  TEST_ASSERT_FALSE(loop.isPlaybackSessionExitPublicationGap());
  TEST_ASSERT_FALSE(t.hasStopInFlightForSlot(0));

  loop.rebuildVisualCacheIdleSlice(1, 0);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(1, loop.visualCache.notes.size());
  submitFrame(0);
  TEST_ASSERT_EQUAL_UINT32(1, submittedFrame.notes.size());
  assertNote(submittedFrame.notes, 100, 60, 12, 80);
  const auto firstKey = submittedFrame.source;
  TEST_ASSERT_EQUAL_UINT32(0, firstKey.windowStart);
  TEST_ASSERT_EQUAL_UINT32(16 * bar, firstKey.windowLength);
  completeFrame();

  const uint32_t revision = loop.visualCache.revision;
  loop.rebuildVisualCacheIdleSlice(0, 1); // no budget: no publication
  loop.rebuildVisualCacheIdleSlice(1, 0, 0); // clean neighborhood: no publication
  TEST_ASSERT_EQUAL_UINT32(revision, loop.visualCache.revision);
  loop.rebuildVisualCacheIdleSlice(15, 1);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT8(0, loop.visualCache.dirtyBars[15]);
  TEST_ASSERT_EQUAL_UINT8(1, loop.visualCache.dirtyBars[16]);
  TEST_ASSERT_EQUAL_UINT32(16, loop.visualCache.notes.size());
  submitFrame(0); // identical playhead and viewport; no movement to force a gather
  TEST_ASSERT_EQUAL_UINT32(firstKey.windowStart, submittedFrame.source.windowStart);
  TEST_ASSERT_EQUAL_UINT32(firstKey.windowLength, submittedFrame.source.windowLength);
  TEST_ASSERT_EQUAL_PTR(firstKey.publishedSnapshotIdentity,
                        submittedFrame.source.publishedSnapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(16, submittedFrame.notes.size(),
      "published visible slices must refresh the stationary committed prefix before all bars clean");
  for (uint32_t i = 0; i < 16; ++i)
    assertNote(submittedFrame.notes, 100 + i, 60, i * bar + 12, i * bar + 80);
  TEST_ASSERT_EQUAL_UINT32(revision + 1, loop.visualCache.revision);
  TEST_ASSERT_EQUAL_UINT32(loop.visualCache.revision, submittedFrame.source.visualCacheRevision);
  completeFrame();

  loop.rebuildVisualCacheIdleSlice(16, 16); // final publication increments exactly once too
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(revision + 2, loop.visualCache.revision);
  loop.rebuildVisualCacheIdleSlice(1, 0); // already clean: no publication
  TEST_ASSERT_EQUAL_UINT32(revision + 2, loop.visualCache.revision);
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

void test_sealed_and_live_overdub_notes_paint_while_record_remains_private() {
  const GapTake take = seedGapOverdub(true);
  Track& t = *take.track;
  Loop& loop = t.getActiveLoop();
  submitFrame(1792); assertGapFrame(take); completeFrame();
  t.stopOverdubbing();
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_FALSE(loop.hasCommittedPasses());
  clockManager.assignCurrentTickSilently(1824);
  t.startOverdubbing(1824);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const NoteId d = acceptPair(t, 74, 1856, 1904);
  for (unsigned frame = 0; frame < 3; ++frame) {
    submitFrame(1920 + frame);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, submittedFrame.notes.size(),
        "private Record, sealed B and live C must compose without publication");
    assertNote(submittedFrame.notes, take.a, 60, 12, 80);
    assertNote(submittedFrame.notes, take.b, 62, 1328, 1408);
    assertNote(submittedFrame.notes, take.c, 70, 192, 240);
    assertNote(submittedFrame.notes, d, 74, 320, 368);
    completeFrame();
  }
  t.stopOverdubbing();
  TEST_ASSERT_TRUE(t.isPlaying());
  for (unsigned frame = 0; frame < 3; ++frame) {
    submitFrame(1924 + frame);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, submittedFrame.notes.size(),
        "stopping C must not erase unpublished B's job-owned preview");
    assertNote(submittedFrame.notes, take.a, 60, 12, 80);
    assertNote(submittedFrame.notes, take.b, 62, 1328, 1408);
    assertNote(submittedFrame.notes, take.c, 70, 192, 240);
    assertNote(submittedFrame.notes, d, 74, 320, 368);
    completeFrame();
  }
  TEST_ASSERT_FALSE(loop.hasCommittedPasses());
}

void test_retired_predecessor_notes_survive_third_overdub_start() {
  const GapTake take = seedGapOverdub(true);
  Track& t = *take.track;
  Loop& loop = t.getActiveLoop();
  submitFrame(1792); assertGapFrame(take); completeFrame();
  t.stopOverdubbing();
  clockManager.assignCurrentTickSilently(1824);
  t.startOverdubbing(1824);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const NoteId c = acceptPair(t, 74, 1856, 1904);
  submitFrame(1920); completeFrame();
  for (unsigned grain = 0; grain < 20000 && t.captureStopPreviewCount() != 0; ++grain)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, t.captureStopPreviewCount(),
      "older Record/overdub jobs must retire through production workers while C stays live");
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_TRUE(loop.captureActive());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL_UINT32(2, loop.capture.store.size());
  submitFrame(1920);
  TEST_MESSAGE("checking predecessor frame after cleanup with C live");
  assertNote(submittedFrame.notes, take.c, 70, 192, 240);
  assertNote(submittedFrame.notes, c, 74, 320, 368);
  completeFrame();

  t.stopOverdubbing();
  TEST_ASSERT_TRUE(t.isPlaying());
  clockManager.assignCurrentTickSilently(1952);
  t.startOverdubbing(1952);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const NoteId d = acceptPair(t, 76, 1984, 2032);
  for (unsigned frame = 0; frame < 3; ++frame) {
    submitFrame(2048 + frame);
    TEST_MESSAGE("checking predecessor identities after third overdub Start");
    assertNote(submittedFrame.notes, take.a, 60, 12, 80);
    assertNote(submittedFrame.notes, take.b, 62, 1328, 1408);
    assertNote(submittedFrame.notes, take.c, 70, 192, 240);
    assertNote(submittedFrame.notes, c, 74, 320, 368);
    assertNote(submittedFrame.notes, d, 76, 448, 496);
    TEST_ASSERT_EQUAL_UINT32(5, submittedFrame.notes.size());
    completeFrame();
    assertNote(completedFrame.notes, take.c, 70, 192, 240);
    assertNote(completedFrame.notes, c, 74, 320, 368);
  }
  for (unsigned grain = 0; grain < 20000 && t.captureStopPreviewCount() != 0; ++grain)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_EQUAL_UINT32(0, t.captureStopPreviewCount());
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL_UINT32(2, loop.capture.store.size());
  t.stopOverdubbing();
  TEST_ASSERT_TRUE(t.isPlaying());
  clockManager.assignCurrentTickSilently(2208);
  t.startOverdubbing(2208);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const NoteId e = acceptPair(t, 78, 2240, 2288);
  submitFrame(2304);
  TEST_MESSAGE("checking both retired overdubs after fourth overdub Start");
  assertNote(submittedFrame.notes, take.a, 60, 12, 80);
  assertNote(submittedFrame.notes, take.b, 62, 1328, 1408);
  assertNote(submittedFrame.notes, take.c, 70, 192, 240);
  assertNote(submittedFrame.notes, c, 74, 320, 368);
  assertNote(submittedFrame.notes, d, 76, 448, 496);
  assertNote(submittedFrame.notes, e, 78, 704, 752);
  TEST_ASSERT_EQUAL_UINT32(6, submittedFrame.notes.size());
  completeFrame();
  assertNote(completedFrame.notes, take.c, 70, 192, 240);
  assertNote(completedFrame.notes, c, 74, 320, 368);
}

void test_overview_preserves_finalizing_and_live_preview_bands() {
  const GapTake take = seedGapOverdub(true);
  Track& t = *take.track;
  Loop& loop = t.getActiveLoop();
  t.stopOverdubbing();
  clockManager.assignCurrentTickSilently(1824);
  t.startOverdubbing(1824);
  const auto* before = DisplayCaptureTestAccess::overview(t, loop, 0);
  TEST_ASSERT_NOT_NULL(before);
  const uint8_t predecessorBands = (*before)[0];
  TEST_ASSERT_NOT_EQUAL_UINT8(0, predecessorBands);
  (void)acceptPair(t, 12, 2320, 2360); // a different bar and pitch band
  const auto* after = DisplayCaptureTestAccess::overview(t, loop, 0);
  TEST_ASSERT_NOT_NULL(after);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(predecessorBands, (*after)[0],
      "live preview replacement must not erase finalizing predecessor density");
  TEST_ASSERT_NOT_EQUAL_UINT8(0, (*after)[1]);
}

// The user can request overdub before even the first Record closure grain. The draw source must
// preserve the complete base and compose accepted notes without relying on a borrowed source setup.
void test_immediate_overdub_paints_before_any_record_finalization_turn() {
  Track& t = beginRecord();
  const NoteId a = acceptPair(t, 60, 12, 80), b = acceptPair(t, 62, 1328, 1408);
  growRecordGeometry(t, 1536);
  submitFrame(1536); completeFrame();
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecording(1536);
  TEST_ASSERT_FALSE(t.getActiveLoop().hasCommittedPasses());
  clockManager.assignCurrentTickSilently(1664);
  t.startOverdubbing(1664);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const NoteId c = acceptPair(t, 70, 1728, 1776);
  submitFrame(1792);
  assertGapFrame({&t, a, b, c});
  completeFrame();
  assertNote(completedFrame.notes, c, 70, 192, 240);
  TEST_ASSERT_FALSE(t.getActiveLoop().hasCommittedPasses());
}

static std::array<NoteId, 24> stopPartialLongRecordForDisplay() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  Track& t = beginRecord();
  std::array<NoteId, 24> ids{};
  for (uint32_t b = 0; b < ids.size(); ++b)
    ids[b] = acceptPair(t, static_cast<uint8_t>(60 + b % 8), b * bar + 12, b * bar + 80);
  (void)acceptPair(t, 95, length + 12, length + 80); // the trailing partial bar is trimmed
  growRecordGeometry(t, length + 184);
  submitFrame(length + 184); completeFrame();
  clockManager.assignCurrentTickSilently(length + 200);
  t.stopRecording(length + 200);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT32(length, t.getActiveLoop().loopLengthTicks);
  TEST_ASSERT_FALSE(t.getActiveLoop().hasCommittedPasses());
  return ids;
}

void test_truncated_record_stop_reprojects_complete_first_sixteen_bars() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const auto ids = stopPartialLongRecordForDisplay();
  submitFrame(clockManager.getCurrentTick());
  TEST_ASSERT_EQUAL_UINT32(0, submittedFrame.source.windowStart);
  TEST_ASSERT_EQUAL_UINT32(16 * bar, submittedFrame.source.windowLength);
  for (uint32_t b = 0; b < 16; ++b)
    assertNote(submittedFrame.notes, ids[b], static_cast<uint8_t>(60 + b % 8),
               b * bar + 12, b * bar + 80);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(16, submittedFrame.notes.size(),
      "a rewound viewport must not retain only the tail of the old Record window");
  completeFrame();
  for (uint32_t b = 0; b < 16; ++b)
    assertNote(completedFrame.notes, ids[b], static_cast<uint8_t>(60 + b % 8),
               b * bar + 12, b * bar + 80);
}

void test_closed_overdub_note_survives_truncated_record_publication_gap() {
  (void)stopPartialLongRecordForDisplay();
  Track& t = trackManager.getSelectedTrack();
  clockManager.assignCurrentTickSilently(256);
  t.startOverdubbing(256);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  t.noteOn(1, 100, 100, 320);
  const NoteId id = t.getActiveLoop().capture.store.at(0).noteId;
  submitFrame(344);
  TEST_ASSERT_TRUE_MESSAGE(std::any_of(submittedFrame.notes.begin(), submittedFrame.notes.end(),
      [id](const auto& note) { return note.noteId == id && note.note == 100; }),
      "fixture must first paint the incoming open hold");
  completeFrame();
  t.noteOff(1, 100, 0, 360);
  for (unsigned frame = 0; frame < 3; ++frame) {
    submitFrame(384 + frame);
    assertNote(submittedFrame.notes, id, 100, 320, 360);
    completeFrame();
    assertNote(completedFrame.notes, id, 100, 320, 360);
  }
  TEST_ASSERT_FALSE(t.getActiveLoop().hasCommittedPasses());
}

static void exerciseLateRecordWindowBeforeRootPublication(bool dense, bool contended = false) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  Track& t = beginRecord();
  std::array<NoteId, 24> ids{};
  for (uint32_t b = 0; b < 24; ++b) {
    const uint32_t count = dense ? 16 : 1;
    for (uint32_t n = 0; n < count; ++n) {
      const NoteId id = acceptPair(t, dense ? 84 - n : 60 + b % 8,
          b * bar + n * 48 + 12, b * bar + n * 48 + 40);
      if (n == 0) ids[b] = id;
    }
  }
  growRecordGeometry(t, length + 200);
  submitFrame(length + 184); completeFrame();
  (void)acceptPair(t, 95, length + 12, length + 80); // pin 24 complete bars; trailing partial bar is trimmed.
  clockManager.assignCurrentTickSilently(length + 200);
  t.stopRecording(length + 200);
  Loop& loop = t.getActiveLoop();
  submitFrame(clockManager.getCurrentTick()); completeFrame();
  clockManager.assignCurrentTickSilently(256);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  t.startOverdubbing(256);
  for (unsigned turn = 0; turn < 4096 && !loop.hasCommittedPasses(); ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_NULL(t.recordStopPreviewForSlot(0));
  const uint32_t on = 17 * bar + 32, off = on + 64;
  clockManager.assignCurrentTickSilently(on);
  t.noteOn(1, 100, 100, on);
  Track& background = trackManager.getTrack(1);
  if (contended) {
    background.ensureLoopsAllocated();
    Loop& other = background.getActiveLoop();
    LoopEventStore record;
    for (uint32_t b = 0; b < 24; ++b) {
      for (uint32_t n = 0; n < 16; ++n) {
        MidiEvent event = MidiEvent::NoteOn(b * bar + n * 48, 2, 40 + n, 100);
        event.noteId = 1000 + b * 16 + n;
        TEST_ASSERT_TRUE(record.append(event));
        TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(event.tick + 40, 2, 40 + n, 0)));
      }
    }
    other.seedRecordPassFromStore(record);
    other.loopLengthTicks = length;
    other.markDisplayCachesStale();
    background.forceSetState(TRACK_PLAYING);
    TEST_ASSERT_TRUE(background.isAudiblePlaybackActive());
  }
  const NoteId live = loop.capture.store.at(0).noteId;
  submitFrame(on + 16); completeFrame();
  clockManager.assignCurrentTickSilently(off);
  t.noteOff(1, 100, 0, off);
  // This is the existing production producer, not a manually prepared index.
  for (unsigned turn = 0; turn < 2048; ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    if (contended)
      background.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  submitFrame(off + 16);
  for (uint32_t b = 16; b < 24; ++b)
    assertNote(submittedFrame.notes, ids[b], dense ? 84 : 60 + b % 8,
               b * bar + 12, b * bar + 40);
  assertNote(submittedFrame.notes, live, 100, on, off);
  completeFrame();
  assertNote(completedFrame.notes, live, 100, on, off);
}

void test_late_record_window_and_closed_overdub_progress_before_root_publication() {
  exerciseLateRecordWindowBeforeRootPublication(false);
}
void test_dense_late_record_window_and_closed_overdub_progress_before_root_publication() {
  exerciseLateRecordWindowBeforeRootPublication(true);
}
void test_selected_gap_display_source_survives_competing_audible_track() {
  exerciseLateRecordWindowBeforeRootPublication(true, true);
}
void test_display_source_progresses_with_pending_successor_stop_and_across_wrap() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  const auto ids = stopPartialLongRecordForDisplay();
  Track& t = trackManager.getSelectedTrack();
  Loop& loop = t.getActiveLoop();
  submitFrame(clockManager.getCurrentTick()); completeFrame();
  clockManager.assignCurrentTickSilently(256);
  t.startOverdubbing(256);
  for (unsigned turn = 0; turn < 4096 && !loop.hasCommittedPasses(); ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  (void)acceptPair(t, 100, 320, 400);
  clockManager.assignCurrentTickSilently(512);
  t.stopOverdubbing();
  clockManager.assignCurrentTickSilently(640);
  t.startOverdubbing(640);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(loop.privateSourceOwedForSeal());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  const auto before = LoopContentResolution::boundedDisplayLastSlice();
  t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  const auto after = LoopContentResolution::boundedDisplayLastSlice();
  TEST_ASSERT_TRUE_MESSAGE(std::strcmp(before.step, after.step) != 0 ||
      before.cursor != after.cursor || before.workUnits != after.workUnits,
      "a pending successor Stop must not make the display-source wakeup unreachable");

  const uint32_t on = 23 * bar + 32, off = on + 80;
  const NoteId late = acceptPair(t, 101, on, off);
  for (unsigned turn = 0; turn < 4096; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  submitFrame(off + 16);
  assertNote(submittedFrame.notes, ids[23], 67, 23 * bar + 12, 23 * bar + 80);
  assertNote(submittedFrame.notes, late, 101, on, off);
  completeFrame();
  LoopContentResolution::BoundedDisplayRequest completedSource;
  completedSource.owner = &loop;
  completedSource.loopId = loop.loopId;
  completedSource.loopLengthTicks = loop.loopLengthTicks;
  completedSource.playbackRevision = loop.playbackRevision;
  TEST_ASSERT_TRUE_MESSAGE(LoopContentResolution::boundedDisplayReady(completedSource, loop.passes),
      "fixture must have a complete qualified source before the real wrap commit");
  clockManager.assignCurrentTickSilently(length);
  TEST_ASSERT_TRUE_MESSAGE(t.maybeCommitOverdubWrap(length - 8, 0), "fixture must execute the production wrap commit");
  TEST_ASSERT_TRUE_MESSAGE(std::any_of(LoopContentResolution::boundedDisplayNotes().begin(),
      LoopContentResolution::boundedDisplayNotes().end(), [&](const auto& n) { return n.noteId == ids[0]; }),
      "the completed prior display source still contains the original Record identity");
  submitFrame(length + 256);
  TEST_ASSERT_TRUE_MESSAGE(std::any_of(submittedFrame.notes.begin(), submittedFrame.notes.end(),
      [&](const auto& n) { return n.noteId == ids[0]; }),
      "first wrapped frame must retain Record content while its replacement source is not ready");
  for (uint32_t b = 0; b < 16; ++b)
    assertNote(submittedFrame.notes, ids[b], 60 + b % 8, b * bar + 12, b * bar + 80);
  completeFrame();
  for (unsigned turn = 0; turn < 4096; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  submitFrame(length + 256);
  for (uint32_t b = 0; b < 16; ++b)
    assertNote(submittedFrame.notes, ids[b], 60 + b % 8, b * bar + 12, b * bar + 80);
  completeFrame();
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}


void test_closed_overdub_note_survives_painted_stop_and_gradual_finalization() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  Track& recording = beginRecord();
  for (uint32_t b = 0; b < 24; ++b) {
    for (uint32_t n = 0; n < 16; ++n)
      (void)acceptPair(recording, 84 - n, b * bar + n * 48, b * bar + n * 48 + 40);
    growRecordGeometry(recording, (b + 1) * bar);
    submitFrame((b + 1) * bar - 8); completeFrame();
  }
  growRecordGeometry(recording, length + 200);
  clockManager.assignCurrentTickSilently(length + 200);
  recording.stopRecording(length + 200);
  Track& t = trackManager.getSelectedTrack();
  submitFrame(clockManager.getCurrentTick()); completeFrame();
  clockManager.assignCurrentTickSilently(256);
  t.startOverdubbing(256);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  clockManager.assignCurrentTickSilently(320);
  t.noteOn(1, 74, 100, 320);
  const NoteId id = t.getActiveLoop().capture.store.at(0).noteId;
  submitFrame(344); completeFrame();
  clockManager.assignCurrentTickSilently(600);
  t.noteOff(1, 74, 0, 600);
  for (unsigned turn = 0; turn < 192; ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    if (turn % 16 == 0) {
      submitFrame(624 + turn);
      assertNote(submittedFrame.notes, id, 74, 320, 600);
      completeFrame();
      assertNote(completedFrame.notes, id, 74, 320, 600);
    }
  }
}

void test_apply_capture_playhead_tails_honours_the_frozen_close_tick() {
  // The paint copy grows an open preview row to the tick the caller supplies. An accepted stop must
  // supply its closure tick, never the advanced playhead (`midifile-chain-43` owner report).
  CapturePreview preview;
  NoteUtils::DisplayNote row{};
  row.noteId = 7;
  row.note = 74;
  row.velocity = 100;
  row.startTick = 320;
  row.endTick = 320;
  preview.notes.push_back(row);
  CapturePreviewNoteState state;
  state.pitch = 74;
  state.open = true;
  preview.noteStates.push_back(state);
  preview.openNoteIndices.push_back(0);

  const auto paintWithCloseTick = [&](uint32_t closeTick) {
    NoteUtils::DisplayNoteVec notes;
    notes.push_back(row);
    DisplayManagerInternal::applyCapturePlayheadTails(preview, 3072, closeTick, 0, notes, false);
    TEST_ASSERT_EQUAL_UINT32(1, notes.size());
    return notes[0].endTick;
  };
  // Live interval: the caller passes the playhead and the row follows it.
  TEST_ASSERT_EQUAL_UINT32(344, paintWithCloseTick(344));
  // Accepted stop: the caller passes the frozen closure tick; the advanced playhead (560) is not it.
  TEST_ASSERT_EQUAL_UINT32(400, paintWithCloseTick(400));
}

void test_record_preview_is_cleared_before_stop_descriptor_reuse() {
  Track& t = beginRecord();
  for (unsigned take = 0; take < 5; ++take) {
    if (take != 0) {
      t.clear();
      clockManager.assignCurrentTickSilently(0);
      TEST_ASSERT_TRUE(t.startRecording(0));
    }
    (void)acceptPair(t, static_cast<uint8_t>(60 + take), 12, 80);
    (void)acceptPair(t, static_cast<uint8_t>(72 + take), 800, 880);
    clockManager.assignCurrentTickSilently(1536);
    t.stopRecording(1536);
    TEST_ASSERT_TRUE_MESSAGE(t.getActiveLoop().capturePreview.notes.empty(),
        "reusing a stop descriptor must not swap retired rows into the producer");
    for (unsigned turn = 0; turn < 20000 && !t.getActiveLoop().hasCommittedPasses(); ++turn)
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    TEST_ASSERT_TRUE(t.getActiveLoop().hasCommittedPasses());
    TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::retiredPreviewsEmpty(t),
        "Record publication must retire its preview before the descriptor is reusable");
  }
}

void test_held_record_preview_uses_frozen_stop_endpoint_before_and_after_publication() {
  Track& t = beginRecord();
  (void)acceptPair(t, 60, 12, 80);
  t.noteOn(1, 100, 100, 1200);
  const NoteId held = t.getActiveLoop().capture.store.at(2).noteId;
  growRecordGeometry(t, 1400);
  clockManager.assignCurrentTickSilently(1400);
  t.stopRecording(1400);
  TEST_ASSERT_EQUAL_UINT32(1536, t.getActiveLoop().loopLengthTicks);
  submitFrame(1400);
  assertNote(submittedFrame.notes, held, 100, 1200, 1400);
  completeFrame();
  for (unsigned turn = 0; turn < 20000 && !t.getActiveLoop().hasCommittedPasses(); ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_TRUE(t.getActiveLoop().hasCommittedPasses());
  Loop& loop = t.getActiveLoop();
  for (unsigned turn = 0; turn < 8192 && !loop.publishedSnapshotIsCurrent(); ++turn)
    loop.stepPendingSnapshotPublish(4, true, 0);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  submitFrame(1400);
  assertNote(submittedFrame.notes, held, 100, 1200, 1400);
  completeFrame();
}

void test_rounded_record_stop_follows_wrapped_overdub_before_publication() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  Track& t = beginRecord();
  std::array<NoteId, 24> ids{};
  for (uint32_t b = 0; b < ids.size(); ++b)
    ids[b] = acceptPair(t, static_cast<uint8_t>(60 + b % 8), b * bar + 12, b * bar + 80);
  (void)acceptPair(t, 95, length - 168, length - 128); // pin rounded-up last-bar content
  growRecordGeometry(t, length - 120);
  submitFrame(length - 120); completeFrame();
  clockManager.assignCurrentTickSilently(length - 104);
  t.stopRecording(length - 104);
  TEST_ASSERT_EQUAL_UINT32(length, t.getActiveLoop().loopLengthTicks);
  submitFrame(length - 104); completeFrame();
  clockManager.assignCurrentTickSilently(length + 152);
  t.startOverdubbing(length + 152);
  const NoteId live = acceptPair(t, 100, length + 200, length + 240);
  for (unsigned frame = 0; frame < 3; ++frame) {
    submitFrame(length + 256 + frame);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, submittedFrame.source.windowStart,
        "a complete stopped Record preview permits coherent follow across wrap");
    for (uint32_t b = 0; b < 16; ++b)
      assertNote(submittedFrame.notes, ids[b], static_cast<uint8_t>(60 + b % 8),
                 b * bar + 12, b * bar + 80);
    assertNote(submittedFrame.notes, live, 100, 200, 240);
    TEST_ASSERT_EQUAL_UINT32(17, submittedFrame.notes.size());
    completeFrame();
    assertNote(completedFrame.notes, live, 100, 200, 240);
  }
  TEST_ASSERT_FALSE(t.getActiveLoop().hasCommittedPasses());
}

void test_empty_overdub_start_cannot_grow_frozen_record_tails() {
  Track& t = beginRecord();
  const NoteId base = acceptPair(t, 60, 12, 80);
  t.noteOn(1, 100, 100, 1200);
  const NoteId held = t.getActiveLoop().capture.store.at(2).noteId;
  growRecordGeometry(t, 1400);
  clockManager.assignCurrentTickSilently(1400);
  t.stopRecording(1400);
  submitFrame(1400); completeFrame();
  clockManager.assignCurrentTickSilently(1420);
  t.startOverdubbing(1420);
  for (uint32_t tick : {1440u, 1480u, 1580u}) {
    submitFrame(tick);
    assertNote(submittedFrame.notes, base, 60, 12, 80);
    assertNote(submittedFrame.notes, held, 100, 1200, 1400);
    TEST_ASSERT_EQUAL_UINT32(2, submittedFrame.notes.size());
    completeFrame();
  }
}

void test_empty_successor_start_preserves_stopped_overdub_hold_endpoint() {
  const GapTake take = seedGapOverdub(true, false);
  Track& t = *take.track;
  clockManager.assignCurrentTickSilently(1728);
  t.noteOn(1, 100, 100, 1728);
  const NoteId held = t.getActiveLoop().capture.store.at(0).noteId;
  submitFrame(1744); completeFrame();
  clockManager.assignCurrentTickSilently(1760);
  t.stopOverdubbing();
  clockManager.assignCurrentTickSilently(1792);
  t.startOverdubbing(1792);
  for (uint32_t tick : {1824u, 1856u}) {
    submitFrame(tick);
    assertNote(submittedFrame.notes, held, 100, 192, 224);
    TEST_ASSERT_EQUAL_UINT32(3, submittedFrame.notes.size());
    completeFrame();
  }
}

// ---------------------------------------------------------------------------------------------
// Task 4.19 — the retained display source envelope. Contract: `docs/Plans/display_source_retention_handoff.md`
// (settled round 3). Fixtures are in that file's order: 1–3 state changes inside the retained prefix,
// 4 a pass appended after it, 5–6 the capacity boundary, 7 the frame-level immediate-wrap regression.
// Serving fixtures fail before the envelope exists; the refusal fixtures are controls that fail if
// retention ever over-serves.

static LoopContentResolution::BoundedDisplayRequest displayRequestFor(const Loop& loop) {
  LoopContentResolution::BoundedDisplayRequest request;
  request.owner = &loop;
  request.loopId = loop.loopId;
  request.loopLengthTicks = loop.loopLengthTicks;
  request.playbackRevision = loop.playbackRevision;
  request.canonicalStorageGeneration = loop.canonicalStorageGeneration;
  return request;
}

/// Run the bounded producer for the current request until it publishes (or the grain budget is spent).
static bool driveDisplaySourceToPublish(Track& t, unsigned budget = 4096) {
  Loop& loop = t.getActiveLoop();
  const auto request = displayRequestFor(loop);
  if (!LoopContentResolution::boundedDisplayRequest(request, loop.passes)) {
    return false;
  }
  for (unsigned turn = 0; turn < budget; ++turn) {
    if (LoopContentResolution::boundedDisplayReady(request, loop.passes)) {
      return true;
    }
    LoopContentResolution::boundedDisplayStep(loop.passes);
  }
  return LoopContentResolution::boundedDisplayReady(request, loop.passes);
}

/// One committed Record pass with two accepted pairs — the smallest take a retained envelope can own.
struct CommittedRecordTake {
  Track* track;
  NoteId a;
  NoteId b;
};
static CommittedRecordTake seedCommittedRecordTake() {
  Track& t = beginRecord();
  const NoteId a = acceptPair(t, 60, 12, 80);
  const NoteId b = acceptPair(t, 62, 1328, 1408);
  growRecordGeometry(t, 1536);
  submitFrame(1536);
  completeFrame();
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecording(1536);
  Loop& loop = t.getActiveLoop();
  for (unsigned turn = 0; turn < 4096 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, loop.passes.capturePassCount(),
                                   "fixture must have exactly one committed Record pass");
  DisplayNoteVec committedPass;
  LoopContentResolution::resolveDisplayNotes(loop.passes, loop.loopLengthTicks, committedPass);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, committedPass.size(),
                                   "the committed Record pass must carry both accepted pairs");
  return {&t, a, b};
}

/// Note-for-note equality by `NoteId` and geometry against an independently rebuilt frame.
static void assertRetainedEqualsRebuild(const Loop& loop, const char* why) {
  DisplayNoteVec rebuilt;
  LoopContentResolution::resolveDisplayNotes(loop.passes, loop.loopLengthTicks, rebuilt);
  const DisplayNoteVec& retained = LoopContentResolution::boundedDisplayRetainedNotes();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rebuilt.size(), retained.size(), why);
  for (const auto& expected : rebuilt) {
    size_t found = 0;
    for (const auto& served : retained) {
      if (served.noteId != expected.noteId) continue;
      ++found;
      TEST_ASSERT_EQUAL_UINT8(expected.note, served.note);
      TEST_ASSERT_EQUAL_UINT32(expected.startTick, served.startTick);
      TEST_ASSERT_EQUAL_UINT32(expected.endTick, served.endTick);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, found, why);
  }
}

// Fixture 1 — a capture undo (Disable) inside the prefix must refuse the retained rows. Same pass id,
// same committed chunk ids, same count: only the state moved, so only an exact predicate can see it.
void test_capture_undo_refuses_the_retained_display_source() {
  const CommittedRecordTake take = seedCommittedRecordTake();
  Track& t = *take.track;
  Loop& loop = t.getActiveLoop();
  TEST_ASSERT_TRUE_MESSAGE(driveDisplaySourceToPublish(t),
                           "fixture must publish a retained display source before the undo");
  auto request = displayRequestFor(loop);
  TEST_ASSERT_TRUE_MESSAGE(LoopContentResolution::boundedDisplayRetainedReady(request, loop.passes),
                           "a promoted generation must serve its own live prefix");
  const size_t retainedRows = LoopContentResolution::boundedDisplayRetainedNotes().size();
  TEST_ASSERT_TRUE(retainedRows > 0);

  TEST_ASSERT_TRUE(loop.setCapturePassState(loop.passes.recordPass.id, CapturePassState::Disabled));
  request = displayRequestFor(loop);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, loop.passes.capturePassCount(),
                                   "the undo keeps the pass and its count; only its state flips");
  TEST_ASSERT_FALSE_MESSAGE(LoopContentResolution::boundedDisplayRetainedReady(request, loop.passes),
                            "a Disabled pass inside the retained prefix must refuse the retained rows");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedRows,
                                   LoopContentResolution::boundedDisplayRetainedNotes().size(),
                                   "a refusal keeps the envelope; it never rewrites it");
}

// Fixture 2 — capture redo requalifies, and the served rows are the deterministic output of the current
// passes: equality against an independent rebuild, not an assertion.
void test_capture_redo_requalifies_the_retained_display_source() {
  const CommittedRecordTake take = seedCommittedRecordTake();
  Track& t = *take.track;
  Loop& loop = t.getActiveLoop();
  TEST_ASSERT_TRUE(driveDisplaySourceToPublish(t));
  const PassId recordPass = loop.passes.recordPass.id;
  TEST_ASSERT_TRUE(loop.setCapturePassState(recordPass, CapturePassState::Disabled));
  TEST_ASSERT_FALSE(LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(loop),
                                                                      loop.passes));
  TEST_ASSERT_TRUE(loop.setCapturePassState(recordPass, CapturePassState::Active));
  const auto request = displayRequestFor(loop);
  TEST_ASSERT_TRUE_MESSAGE(LoopContentResolution::boundedDisplayRetainedReady(request, loop.passes),
                           "a redo that restores every exact field must requalify the retained rows");
  assertRetainedEqualsRebuild(loop, "requalified retained rows must equal an independent rebuild");
  const auto& retained = LoopContentResolution::boundedDisplayRetainedNotes();
  char receipt[240];
  std::snprintf(receipt, sizeof(receipt),
                "retained=%zu,a=%lu,b=%lu,priced=%lu,passes=%zu,chunks=%zu,rev=%lu",
                retained.size(), static_cast<unsigned long>(take.a),
                static_cast<unsigned long>(take.b),
                static_cast<unsigned long>(LoopContentResolution::boundedDisplayRetainedCapacity()),
                loop.passes.capturePassCount(),
                loop.passes.hasRecordPass()
                    ? loop.passes.recordPass.committedChunkIds.size()
                    : static_cast<size_t>(0),
                static_cast<unsigned long>(loop.playbackRevision));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, retained.size(), receipt);
  assertNote(retained, take.a, 60, 12, 80);
  assertNote(retained, take.b, 62, 1328, 1408);
}

// Fixture 3 — an edit undo inside the prefix refuses the retained rows (same edit id, same count).
void test_edit_undo_refuses_the_retained_display_source() {
  const CommittedRecordTake take = seedCommittedRecordTake();
  Track& t = *take.track;
  Loop& loop = t.getActiveLoop();
  EditPass deleteRow{};
  deleteRow.targetNoteId = take.b;
  deleteRow.actionType = EditActionType::Delete;
  deleteRow.startTick = 800;
  deleteRow.endTick = 880;
  deleteRow.pitch = 62;
  const EditPassId editId = loop.saveNoteEditPass(0, deleteRow);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, editId);
  TEST_ASSERT_EQUAL_UINT32(1, loop.passes.editPasses.size());
  TEST_ASSERT_TRUE_MESSAGE(driveDisplaySourceToPublish(t),
                           "fixture must publish a retained display source under the active edit row");
  TEST_ASSERT_TRUE(LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(loop),
                                                                     loop.passes));
  loop.disableEditPasses({editId});
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, loop.passes.editPasses.size(),
                                   "the edit undo keeps the row and the count; only its state flips");
  TEST_ASSERT_EQUAL(static_cast<int>(EditPassState::Disabled),
                    static_cast<int>(loop.passes.editPasses[0].state));
  TEST_ASSERT_FALSE_MESSAGE(
      LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(loop), loop.passes),
      "a Disabled edit row inside the retained identity must refuse the retained rows");
  loop.enableEditPasses({editId});
  TEST_ASSERT_TRUE_MESSAGE(
      LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(loop), loop.passes),
      "an edit redo that restores every exact field must requalify the retained rows");
  assertRetainedEqualsRebuild(loop, "requalified retained rows must equal an independent rebuild");
}

// Fixture 4 — a pass appended after the retained prefix (the production wrap commit) must not refuse it.
// The prelude is the enforced regression fixture's own, so the wrap commit executes in the state the frame
// path reaches it in (a pending successor Stop, a committed Record source, no publication gap).
void test_retained_display_source_survives_the_wrap_that_appends_a_pass() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  const auto ids = stopPartialLongRecordForDisplay();
  Track& t = trackManager.getSelectedTrack();
  Loop& loop = t.getActiveLoop();
  submitFrame(clockManager.getCurrentTick());
  completeFrame();
  clockManager.assignCurrentTickSilently(256);
  t.startOverdubbing(256);
  for (unsigned turn = 0; turn < 4096 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  (void)acceptPair(t, 100, 320, 400);
  clockManager.assignCurrentTickSilently(512);
  t.stopOverdubbing();
  clockManager.assignCurrentTickSilently(640);
  t.startOverdubbing(640);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  const uint32_t on = 23 * bar + 32, off = on + 80;
  (void)acceptPair(t, 101, on, off);
  for (unsigned turn = 0; turn < 4096; ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  submitFrame(off + 16);
  completeFrame();
  TEST_ASSERT_TRUE_MESSAGE(driveDisplaySourceToPublish(t, 8192),
                           "fixture must publish a retained display source before the wrap");
  const auto request = displayRequestFor(loop);
  TEST_ASSERT_TRUE(LoopContentResolution::boundedDisplayRetainedReady(request, loop.passes));
  const uint32_t retainedPasses = LoopContentResolution::boundedDisplayRetainedPassCount();
  TEST_ASSERT_TRUE_MESSAGE(retainedPasses >= 1, "the promoted generation must name its prefix");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedPasses, loop.passes.capturePassCount(),
                                   "the promotion happened on this pass set, so the prefix is all of it");
  const size_t retainedRows = LoopContentResolution::boundedDisplayRetainedNotes().size();
  TEST_ASSERT_TRUE(retainedRows > 0);

  clockManager.assignCurrentTickSilently(length);
  TEST_ASSERT_TRUE_MESSAGE(t.maybeCommitOverdubWrap(length - 8, 0),
                           "fixture must execute the production wrap commit");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedPasses + 1, loop.passes.capturePassCount(),
                                   "the wrap must append exactly one committed pass after the prefix");
  TEST_ASSERT_TRUE_MESSAGE(LoopContentResolution::boundedDisplayRetainedReady(request, loop.passes),
                           "a pass appended after the retained prefix must not refuse the retained rows");
  TEST_ASSERT_EQUAL_UINT32(retainedRows, LoopContentResolution::boundedDisplayRetainedNotes().size());
  for (uint32_t b = 0; b < 16; ++b) {
    assertNote(LoopContentResolution::boundedDisplayRetainedNotes(), ids[b],
               static_cast<uint8_t>(60 + b % 8), b * bar + 12, b * bar + 80);
  }
}

// Fixture 4b (the former F6 item) — the appended-row **tier rule**. A wrap's appended pass is not in the
// envelope; its closed rows reach paint through the sealed-capture-preview tier. Retention may therefore serve
// only while that tier is still owed (`!sealedCapturePreviewIsCovered()`). Once the tier is released those rows
// live only in the cache/gather path, so the envelope must stand down even though its own exact predicate still
// qualifies — that asymmetry is what this fixture pins. Both halves are production gestures: the wrap commit
// appends the pass, and a capture undo→redo inside the prefix is what clears the sealed preview.
void test_retained_display_source_stands_down_once_the_sealed_tier_is_released() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  const auto ids = stopPartialLongRecordForDisplay();
  Track& t = trackManager.getSelectedTrack();
  Loop& loop = t.getActiveLoop();
  submitFrame(clockManager.getCurrentTick());
  completeFrame();
  clockManager.assignCurrentTickSilently(256);
  t.startOverdubbing(256);
  for (unsigned turn = 0; turn < 4096 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  (void)acceptPair(t, 100, 320, 400);
  clockManager.assignCurrentTickSilently(512);
  t.stopOverdubbing();
  clockManager.assignCurrentTickSilently(640);
  t.startOverdubbing(640);
  const uint32_t on = 23 * bar + 32, off = on + 80;
  const NoteId late = acceptPair(t, 101, on, off);
  for (unsigned turn = 0; turn < 4096; ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  submitFrame(off + 16);
  completeFrame();
  TEST_ASSERT_TRUE_MESSAGE(driveDisplaySourceToPublish(t, 8192),
                           "fixture must publish a retained display source before the wrap");
  auto request = displayRequestFor(loop);
  const uint32_t retainedPasses = LoopContentResolution::boundedDisplayRetainedPassCount();
  TEST_ASSERT_TRUE(retainedPasses >= 1);
  char receipt[240];

  clockManager.assignCurrentTickSilently(length);
  TEST_ASSERT_TRUE_MESSAGE(t.maybeCommitOverdubWrap(length - 8, 0),
                           "fixture must execute the production wrap commit");
  std::snprintf(receipt, sizeof(receipt), "covered=%d,sealed=%zu,passes=%zu,retained=%lu",
                loop.sealedCapturePreviewIsCovered() ? 1 : 0,
                loop.sealedCapturePreviewNotes().size(), loop.passes.capturePassCount(),
                static_cast<unsigned long>(retainedPasses));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedPasses + 1, loop.passes.capturePassCount(), receipt);
  TEST_ASSERT_FALSE_MESSAGE(loop.sealedCapturePreviewIsCovered(),
                            "a fresh wrap's appended rows must still be owed by the sealed tier");
  DisplayNoteVec servedBefore;
  TEST_ASSERT_TRUE_MESSAGE(loop.collectQualifiedCommittedDisplayNotes(0, length, servedBefore), receipt);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "retained", DisplayCaptureTestAccess::qualifiedSourceName(loop),
      "while the tier is owed the envelope serves the prefix and the overlay supplies the appended rows");
  TEST_ASSERT_TRUE_MESSAGE(std::none_of(servedBefore.begin(), servedBefore.end(),
                                        [late](const auto& n) { return n.noteId == late; }),
                           "the envelope must not claim the appended pass' rows as its own");

  // Release the tier the production way: a capture undo→redo inside the prefix drops the sealed preview
  // (`setCapturePassState` → `clearSealedCapturePreview`) and restores every exact field of the prefix.
  const PassId recordPass = loop.passes.recordPass.id;
  TEST_ASSERT_TRUE(loop.setCapturePassState(recordPass, CapturePassState::Disabled));
  TEST_ASSERT_TRUE(loop.setCapturePassState(recordPass, CapturePassState::Active));
  request = displayRequestFor(loop);
  std::snprintf(receipt, sizeof(receipt), "covered=%d,sealed=%zu,passes=%zu,retained=%lu",
                loop.sealedCapturePreviewIsCovered() ? 1 : 0,
                loop.sealedCapturePreviewNotes().size(), loop.passes.capturePassCount(),
                static_cast<unsigned long>(LoopContentResolution::boundedDisplayRetainedPassCount()));
  TEST_ASSERT_TRUE_MESSAGE(loop.sealedCapturePreviewIsCovered(),
                           "the capture undo must release the sealed tier");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(retainedPasses + 1, loop.passes.capturePassCount(), receipt);
  TEST_ASSERT_TRUE_MESSAGE(
      LoopContentResolution::boundedDisplayRetainedReady(request, loop.passes),
      "the envelope's own exact predicate must still qualify after the redo");
  DisplayNoteVec servedAfter;
  loop.collectQualifiedCommittedDisplayNotes(0, length, servedAfter);
  TEST_ASSERT_TRUE_MESSAGE(
      std::strcmp(DisplayCaptureTestAccess::qualifiedSourceName(loop), "retained") != 0,
      "with the tier released and an appended pass present, retention must stand down");
}

// Fixture 5 — at the declared cap retention is accepted and priced by the capacity it keeps, not by the
// row count it serves.
void test_retained_display_source_accepts_the_declared_capacity_and_prices_it() {
  constexpr uint32_t kNotes = 2048;  // two events per pair → exactly the declared 4096 reserved rows
  Track& t = beginRecord();
  for (uint32_t n = 0; n < kNotes; ++n) {
    const uint32_t on = n * 24;
    (void)acceptPair(t, static_cast<uint8_t>(60 + n % 8), on, on + 16);
    if (n % 96 == 95) growRecordGeometry(t, on + 24);
  }
  growRecordGeometry(t, kNotes * 24 + 8);
  submitFrame(kNotes * 24 + 8);
  completeFrame();
  clockManager.assignCurrentTickSilently(kNotes * 24 + 24);
  t.stopRecording(kNotes * 24 + 24);
  Loop& loop = t.getActiveLoop();
  for (unsigned turn = 0; turn < 8192 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_TRUE_MESSAGE(driveDisplaySourceToPublish(t, 32768),
                           "the capped take must still publish a retained display source");
  TEST_ASSERT_TRUE_MESSAGE(LoopContentResolution::boundedDisplayRetainedRefusal() == nullptr,
                           "the declared cap itself must be accepted");
  const uint32_t priced = LoopContentResolution::boundedDisplayRetainedCapacity();
  const size_t served = LoopContentResolution::boundedDisplayRetainedNotes().size();
  char receipt[160];
  std::snprintf(receipt, sizeof(receipt), "priced=%lu,served=%zu,cap=%lu",
                static_cast<unsigned long>(priced), served,
                static_cast<unsigned long>(LoopContentResolution::kRetainedDisplaySourceNoteCapacity));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(LoopContentResolution::kRetainedDisplaySourceNoteCapacity, priced,
                                   receipt);
  TEST_ASSERT_GREATER_THAN_MESSAGE(served, static_cast<size_t>(priced),
                                   "pricing is the reserved capacity, not the served row count");
  TEST_ASSERT_TRUE_MESSAGE(LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(loop),
                                                                             loop.passes),
                           receipt);
}

// Fixture 6 — one reserved row past the cap is a named refusal, and the previously qualified retained
// content stays intact.
void test_retained_display_source_refuses_one_row_past_the_cap_and_keeps_the_previous_envelope() {
  const CommittedRecordTake small = seedCommittedRecordTake();
  Track& t = *small.track;
  Loop& loop = t.getActiveLoop();
  TEST_ASSERT_TRUE(driveDisplaySourceToPublish(t));
  const uint32_t pricedBefore = LoopContentResolution::boundedDisplayRetainedCapacity();
  const size_t rowsBefore = LoopContentResolution::boundedDisplayRetainedNotes().size();
  TEST_ASSERT_TRUE(pricedBefore > 0);
  TEST_ASSERT_TRUE(rowsBefore > 0);

  t.clear();
  clockManager.assignCurrentTickSilently(0);
  TEST_ASSERT_TRUE(t.startRecording(0));
  constexpr uint32_t kNotes = 2050;  // 4100 committed events: past the 4096 declared rows
  for (uint32_t n = 0; n < kNotes; ++n) {
    const uint32_t on = n * 24;
    (void)acceptPair(t, static_cast<uint8_t>(60 + n % 8), on, on + 16);
    if (n % 96 == 95) growRecordGeometry(t, on + 24);
  }
  growRecordGeometry(t, kNotes * 24 + 8);
  submitFrame(kNotes * 24 + 8);
  completeFrame();
  clockManager.assignCurrentTickSilently(kNotes * 24 + 24);
  t.stopRecording(kNotes * 24 + 24);
  for (unsigned turn = 0; turn < 8192 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_TRUE_MESSAGE(driveDisplaySourceToPublish(t, 32768),
                           "an over-cap take still publishes its own candidate result");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("retained_capacity",
                                   LoopContentResolution::boundedDisplayRetainedRefusal(),
                                   "one row past the declared cap must be a named refusal");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(pricedBefore, LoopContentResolution::boundedDisplayRetainedCapacity(),
                                   "a refusal keeps the previous envelope");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rowsBefore, LoopContentResolution::boundedDisplayRetainedNotes().size(),
                                   "a refusal keeps the previous envelope's rows");
}

// Destructive set (owner level) — a different loop length is a different request identity, and a clear
// takes the pass set the retained prefix describes away. Neither may keep serving.
void test_retained_display_source_stands_down_on_geometry_change_and_clear() {
  const CommittedRecordTake take = seedCommittedRecordTake();
  Track& t = *take.track;
  Loop& loop = t.getActiveLoop();
  TEST_ASSERT_TRUE(driveDisplaySourceToPublish(t));
  TEST_ASSERT_TRUE(LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(loop),
                                                                     loop.passes));
  auto otherGeometry = displayRequestFor(loop);
  otherGeometry.loopLengthTicks += Config::TICKS_PER_BAR;
  TEST_ASSERT_FALSE_MESSAGE(
      LoopContentResolution::boundedDisplayRetainedReady(otherGeometry, loop.passes),
      "a geometry change must refuse the retained rows: another length is another identity");
  t.clear();
  TEST_ASSERT_EQUAL_UINT32(0, loop.passes.capturePassCount());
  TEST_ASSERT_FALSE_MESSAGE(
      LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(loop), loop.passes),
      "a cleared loop must refuse the retained rows");
}

// Destructive set, second case — a **slot/focus change**. The envelope belongs to the loop it was promoted
// from and to no other, so another slot may never be served its rows; and the change must not *destroy* the
// envelope either, because returning to the slot it belongs to must serve again (the contract's
// `clearRetained()` list does not include a focus change).
void test_retained_display_source_never_serves_another_slot_and_survives_the_focus_change() {
  const CommittedRecordTake take = seedCommittedRecordTake();
  Track& t = *take.track;
  Loop& slot0 = t.getActiveLoop();
  TEST_ASSERT_EQUAL_UINT8(0, t.getActiveLoopIndex());
  TEST_ASSERT_TRUE(driveDisplaySourceToPublish(t));
  const auto slot0Request = displayRequestFor(slot0);
  TEST_ASSERT_TRUE(LoopContentResolution::boundedDisplayRetainedReady(slot0Request, slot0.passes));
  DisplayNoteVec servedSlot0;
  TEST_ASSERT_TRUE_MESSAGE(slot0.collectQualifiedCommittedDisplayNotes(0, slot0.loopLengthTicks, servedSlot0),
                           "fixture must have a retained-served interval on slot 0");
  // Slot 0's own request may still be served by the candidate's publish edge (`bound`) while the request
  // matches; what matters here is that the rows are its own and that any *retained* serve is only for slot 0.
  assertNote(servedSlot0, take.a, 60, 12, 80);
  assertNote(servedSlot0, take.b, 62, 1328, 1408);

  // A different take, recorded into slot 1 through the same production gestures.
  // Focus change at the track level (`Track::setActiveLoopIndex`) — the loop the display asks content for. The
  // display-side slot authority that moves with a switch is `invalidateForSlotChange`, covered by the working-set
  // suite's slot-change fixture; this fixture owns the *content identity* half.
  t.setActiveLoopIndex(1);
  TEST_ASSERT_NOT_EQUAL(&slot0, &t.getActiveLoop());
  Loop& slot1 = t.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  TEST_ASSERT_TRUE(t.startRecording(0));
  const NoteId otherA = acceptPair(t, 80, 40, 120);
  const NoteId otherB = acceptPair(t, 83, 1300, 1400);
  growRecordGeometry(t, 1536);
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecording(1536);
  for (unsigned turn = 0; turn < 4096 && !slot1.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(slot1.hasCommittedPasses(), "fixture must commit slot 1's own take");

  char receipt[240];
  std::snprintf(receipt, sizeof(receipt), "slot1 len=%lu,rev=%lu,passes=%zu,retained=%lu",
                static_cast<unsigned long>(slot1.loopLengthTicks),
                static_cast<unsigned long>(slot1.playbackRevision), slot1.passes.capturePassCount(),
                static_cast<unsigned long>(LoopContentResolution::boundedDisplayRetainedPassCount()));
  TEST_ASSERT_FALSE_MESSAGE(
      LoopContentResolution::boundedDisplayRetainedReady(displayRequestFor(slot1), slot1.passes),
      "the envelope names the loop it was promoted from; another slot must not match it");
  DisplayNoteVec servedSlot1;
  const bool served = slot1.collectQualifiedCommittedDisplayNotes(0, slot1.loopLengthTicks, servedSlot1);
  std::snprintf(receipt, sizeof(receipt), "served=%d,rows=%zu,src=%s,slot1len=%lu",
                served ? 1 : 0, servedSlot1.size(),
                DisplayCaptureTestAccess::qualifiedSourceName(slot1),
                static_cast<unsigned long>(slot1.loopLengthTicks));
  TEST_ASSERT_TRUE_MESSAGE(std::none_of(servedSlot1.begin(), servedSlot1.end(),
                                        [&](const auto& n) {
                                          return n.noteId == take.a || n.noteId == take.b;
                                        }),
                           receipt);
  if (served) {
    TEST_ASSERT_TRUE_MESSAGE(std::any_of(servedSlot1.begin(), servedSlot1.end(),
                                         [&](const auto& n) {
                                           return n.noteId == otherA || n.noteId == otherB;
                                         }),
                             receipt);
  }
  TEST_ASSERT_TRUE_MESSAGE(
      std::strcmp(DisplayCaptureTestAccess::qualifiedSourceName(slot1), "retained") != 0,
      "slot 1 may be served by its own sources, never by slot 0's retained envelope");

  // Focus back: the envelope was not destroyed by the change, so it serves again for its own loop.
  // Focus back to the slot the envelope belongs to.
  t.setActiveLoopIndex(0);
  TEST_ASSERT_EQUAL_PTR(&slot0, &t.getActiveLoop());
  TEST_ASSERT_TRUE_MESSAGE(
      LoopContentResolution::boundedDisplayRetainedReady(slot0Request, slot0.passes),
      "a focus change must not destroy the envelope; only another slot may not be served it");
  DisplayNoteVec servedBack;
  TEST_ASSERT_TRUE(slot0.collectQualifiedCommittedDisplayNotes(0, slot0.loopLengthTicks, servedBack));
  // Slot 0's own request is served again — by the retained envelope while it still qualifies, or by the
  // candidate's publish edge when that request matches too; the rows are the property under test here.
  assertNote(servedBack, take.a, 60, 12, 80);
  assertNote(servedBack, take.b, 62, 1328, 1408);
}

// Display-chain packet A: real owner selection/admission and real Track/paint workers.
// This harness deliberately has no target interval timer, SD traffic or USB input cost;
// it establishes logical ordering, not main.cpp's physical scheduling deadline.
struct DisplayChainOwnerTurns {
  struct ExpectedNote { NoteId id; uint8_t pitch; uint32_t on, off; };
  static bool phaseProgressed(DisplayWindowUtils::DisplayFramePhase before,
                              DisplayWindowUtils::DisplayFramePhase after,
                              unsigned submissionsBefore, unsigned submissionsAfter) {
    return before != after || submissionsBefore != submissionsAfter;
  }
  static bool frameMatches(const FrameReceipt& frame, const std::vector<ExpectedNote>& expected) {
    for (const auto& wanted : expected) {
      unsigned count = 0;
      for (const auto& note : frame.notes) {
        if (note.noteId != wanted.id) continue;
        ++count;
        if (note.note != wanted.pitch || note.startTick != wanted.on || note.endTick != wanted.off) return false;
      }
      if (count != 1) return false;
    }
    return true;
  }
  std::vector<ExpectedNote> expected, expectedAtResolve;
  MidiServiceDrain::DeferredIdleOwnerCursor cursor{};
  MidiServiceDrain::ActiveLoadDisplayContinuation displayChild{};
  uint8_t prefixCursor = 0;
  uint32_t lastDisplayUpdate = 0;
  unsigned paints = 0;
  unsigned maintenanceTurns = 0;
  unsigned deniedDisplayTurns = 0;

  void complete() {
    completeFrame();
    TEST_ASSERT_TRUE_MESSAGE(frameMatches(completedFrame, expectedAtResolve),
        "completed frame must preserve each expected NoteId and its geometry exactly once");
    for (const auto& note : expectedAtResolve)
      assertNote(completedFrame.notes, note.id, note.pitch, note.on, note.off);
  }

  void step(Track& track) {
    using namespace MidiServiceDrain;
    nativeMillisAdvance(50);
    Loop& loop = track.getActiveLoop();
    const auto owner = selectDeferredIdleOwner(cursor,
        track.hasSnapshotPublicationBacklogOnAnySlot(),
        trackManager.anyPlayingMidiDrainAfterOverdubStop() &&
            track.hasPendingSessionExitSnapshotOnAnySlot());
    // Same wrap exclusion as the main-loop display/deferred admission.
    const bool allowed = allowDeferredIdleOwner(owner, loop.hasDeferredVisualCacheWrapAppend());
    if (!allowed) {
      if (owner == DeferredIdleOwner::LoadAndDisplay) ++deniedDisplayTurns;
      return;
    }
    if (owner == DeferredIdleOwner::LoadAndDisplay) {
      const bool due = DisplayWindowUtils::admitDisplayFrameTurn(
          displayManager.hasPendingPaintPhase(),
          millis() - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL, false);
      const auto child = displayChild.select(true, due, false); // no load requested in this fixture
      if (child == ActiveLoadDisplayChild::Display) {
        const auto before = DisplayCaptureTestAccess::phase();
        const unsigned submitted = DisplayCaptureTestAccess::api().submitted;
        // Physical DMA completion is controlled at this I/O boundary, separately
        // from the Resolve/Draw/Transfer worker and its note-identity observations.
        if (DisplayCaptureTestAccess::api().active) complete();
        if (before == DisplayWindowUtils::DisplayFramePhase::Idle) expectedAtResolve = expected;
        serviceDisplay(clockManager.getCurrentTick());
        ++paints;
        TEST_ASSERT_TRUE_MESSAGE(phaseProgressed(before, DisplayCaptureTestAccess::phase(),
            submitted, DisplayCaptureTestAccess::api().submitted),
            "an admitted healthy display phase must progress or submit");
        displayChild.finish(child, displayManager.hasPendingPaintPhase());
        if (!displayManager.hasPendingPaintPhase()) lastDisplayUpdate = millis();
      }
      return;
    }
    // SlotProvision/Persistence own different workers in main; they do not
    // accidentally become extra stop opportunities in this trace.
    if (owner == DeferredIdleOwner::SlotProvision || owner == DeferredIdleOwner::Persistence) return;
    if (owner == DeferredIdleOwner::PlaybackViewMaintenance) {
      const auto clock = clockManager.getPlaybackDeadlineClock();
      if (track.isPlaying() && !track.isRecording() && !track.isOverdubbing())
        (void)track.requestActiveCommittedPlaybackWindow(static_cast<uint32_t>(clock.tick), clock);
      (void)track.serviceRollingPlaybackWindow(clock);
      track.servicePreviewWindowForNoteEditSession();
      (void)track.serviceOverdubOverlayWindow();
      track.serviceAdmittedDeadlineMisses(clock);
      (void)track.stepPlaybackViewMaintenance(static_cast<uint32_t>(clock.tick), nullptr, false, clock);
      return;
    }
    ++maintenanceTurns;
    // Production priority policy and workers, rather than assuming every
    // VisualCache opportunity reaches the Track maintenance branch.
    if (serviceCanonicalPrefixTurn(owner, trackManager.getTrackCount(), prefixCursor,
        [](uint8_t index) {
          const Track& candidate = trackManager.getTrack(index);
          if (candidate.hasPendingDeferredOverdubStop() || candidate.isRecording()) return true;
          if ((!candidate.isAudiblePlaybackActive() && !candidate.isStoppedRecording()) ||
              !candidate.loopsAllocated()) return false;
          const Loop& source = candidate.getActiveLoop();
          return canonicalPreparationHasPriorityWork(candidate.isOverdubbing(), source.visualCacheDirty,
              source.hasDeferredVisualCacheWrapAppend(), source.overdubSourceSpanCacheReady());
        }, [](uint8_t index) { return trackManager.getTrack(index).processDeferredCanonicalPrefix(); })) return;
    if (!track.hasRealtimeDeferredIdleWork()) return;
    track.processDeferredIdleMaintenance(millis(), false, owner);
  }
};

/// Sealed-pass immutability oracle (committed-pass-identity-corruption): snapshot the **entire semantic
/// Record-pass row vector** immediately after Record seal, then require exact equality after every overdub
/// commit. NoteId 24 is a witness, not the oracle: any row may be the first to change, and a fix that only
/// moves the corruption elsewhere still fails. Physical chunk identity is deliberately excluded — storage may
/// relocate while semantic content stays identical.
struct SealedRecordRow {
  uint8_t type = 0;
  uint32_t tick = 0;
  NoteId noteId = kInvalidNoteId;
  uint8_t channel = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
};

static bool sealedRowLess(const SealedRecordRow& a, const SealedRecordRow& b) {
  if (a.tick != b.tick) return a.tick < b.tick;
  if (a.type != b.type) return a.type < b.type;
  if (a.noteId != b.noteId) return a.noteId < b.noteId;
  if (a.channel != b.channel) return a.channel < b.channel;
  if (a.pitch != b.pitch) return a.pitch < b.pitch;
  return a.velocity < b.velocity;
}

static bool sealedRowsEqual(const SealedRecordRow& a, const SealedRecordRow& b) {
  return a.type == b.type && a.tick == b.tick && a.noteId == b.noteId && a.channel == b.channel &&
         a.pitch == b.pitch && a.velocity == b.velocity;
}

static std::vector<SealedRecordRow> sealedRecordRows(const Loop& loop) {
  LoopPasses recordOnly = loop.passes;
  recordOnly.overdubPasses.clear();
  recordOnly.editPasses.clear();
  SessionMidiEventVec flat;
  copyEffectiveCommittedEventsFromPasses(recordOnly, loop.loopLengthTicks, 0, loop.loopLengthTicks,
                                         flat, false);
  std::vector<SealedRecordRow> rows;
  rows.reserve(flat.size());
  for (const MidiEvent& event : flat) {
    const bool note = event.type == midi::NoteOn || event.type == midi::NoteOff;
    rows.push_back({static_cast<uint8_t>(event.type), event.tick, event.noteId, event.channel,
                    note ? event.data.noteData.note : static_cast<uint8_t>(0),
                    note ? event.data.noteData.velocity : static_cast<uint8_t>(0)});
  }
  std::sort(rows.begin(), rows.end(), sealedRowLess);
  return rows;
}

static size_t appendSealedRow(char* out, size_t cap, size_t used, const char* prefix,
                              const SealedRecordRow& row) {
  if (used >= cap) return used;
  const int written = std::snprintf(out + used, cap - used, "%s@t%lu %s id=%lu p=%u v=%u ",
      prefix, static_cast<unsigned long>(row.tick),
      row.type == midi::NoteOn ? "N" : (row.type == midi::NoteOff ? "F" : "?"),
      static_cast<unsigned long>(row.noteId), static_cast<unsigned>(row.pitch),
      static_cast<unsigned>(row.velocity));
  return written < 0 ? used : used + static_cast<size_t>(written);
}

/// Compact multiset delta for the sealed-row oracle; both vectors are sorted by `sealedRowLess`.
static void describeSealedDelta(const std::vector<SealedRecordRow>& expected,
                                const std::vector<SealedRecordRow>& actual, char* out, size_t cap) {
  out[0] = '\0';
  size_t used = 0;
  const int head = std::snprintf(out, cap, "expected=%zu actual=%zu ", expected.size(), actual.size());
  if (head > 0) used = static_cast<size_t>(head);
  size_t i = 0, j = 0;
  unsigned missing = 0, extra = 0;
  while (i < expected.size() || j < actual.size()) {
    if (i < expected.size() && j < actual.size() && sealedRowsEqual(expected[i], actual[j])) {
      ++i;
      ++j;
      continue;
    }
    if (j >= actual.size() ||
        (i < expected.size() && sealedRowLess(expected[i], actual[j]))) {
      if (missing < 3) used = appendSealedRow(out, cap, used, "missing ", expected[i]);
      ++i;
      ++missing;
    } else {
      if (extra < 3) used = appendSealedRow(out, cap, used, "extra ", actual[j]);
      ++j;
      ++extra;
    }
  }
}

/// Live-capture boundary probe: the Record store must hold the witness pair before growth and before stop.
static void assertCaptureStoreRecordPair(Track& track, NoteId id, uint32_t expectedOn,
                                         uint32_t expectedOff, const char* stage) {
  const Loop& loop = track.getActiveLoop();
  unsigned ons = 0, offs = 0;
  uint32_t onTick = 0, offTick = 0;
  for (size_t i = 0; i < loop.capture.store.size(); ++i) {
    const MidiEvent& event = loop.capture.store.at(i);
    if (event.noteId != id) continue;
    if (event.isNoteOn()) {
      ++ons;
      onTick = event.tick;
    } else if (event.isNoteOff()) {
      ++offs;
      offTick = event.tick;
    }
  }
  char detail[300];
  std::snprintf(detail, sizeof(detail),
      "capture store %s id=%lu ons=%u@%lu offs=%u@%lu rows=%zu expected=%lu/%lu", stage,
      static_cast<unsigned long>(id), ons, static_cast<unsigned long>(onTick), offs,
      static_cast<unsigned long>(offTick), loop.capture.store.size(),
      static_cast<unsigned long>(expectedOn), static_cast<unsigned long>(expectedOff));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, ons, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, offs, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedOn, onTick, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedOff, offTick, detail);
}

static void assertSealedRecordRowsUnchanged(const Loop& loop,
                                            const std::vector<SealedRecordRow>& baseline,
                                            unsigned overdub, const char* context) {
  const std::vector<SealedRecordRow> current = sealedRecordRows(loop);
  bool same = current.size() == baseline.size();
  for (size_t i = 0; same && i < baseline.size(); ++i) same = sealedRowsEqual(baseline[i], current[i]);
  if (same) return;
  char delta[900];
  describeSealedDelta(baseline, current, delta, sizeof(delta));
  char detail[1100];
  std::snprintf(detail, sizeof(detail), "%s overdub=%u %s", context, overdub, delta);
  TEST_FAIL_MESSAGE(detail);
}

static void exerciseDisplayOverdubChain(unsigned bars) {
  Track& track = beginRecord();
  std::vector<NoteId> recordIds;
  const uint32_t length = bars * Config::TICKS_PER_BAR;
  for (unsigned bar = 0; bar < bars; ++bar)
    recordIds.push_back(acceptPair(track, 48, bar * Config::TICKS_PER_BAR + 12,
                                  bar * Config::TICKS_PER_BAR + 48));
  // First boundary: the live Record capture store, before geometry growth and stop.
  assertCaptureStoreRecordPair(track, recordIds.back(),
                               (bars - 1u) * Config::TICKS_PER_BAR + 12u,
                               (bars - 1u) * Config::TICKS_PER_BAR + 48u, "pre-growth");
  growRecordGeometry(track, length);
  assertCaptureStoreRecordPair(track, recordIds.back(),
                               (bars - 1u) * Config::TICKS_PER_BAR + 12u,
                               (bars - 1u) * Config::TICKS_PER_BAR + 48u, "post-growth");
  std::vector<SealedRecordRow> expectedRecordRows;
  expectedRecordRows.reserve(static_cast<size_t>(bars) * 2u);
  for (unsigned bar = 0; bar < bars; ++bar) {
    const uint32_t onTick = bar * Config::TICKS_PER_BAR + 12;
    const uint32_t offTick = bar * Config::TICKS_PER_BAR + 48;
    expectedRecordRows.push_back({static_cast<uint8_t>(midi::NoteOn), onTick, recordIds[bar], 1, 48, 100});
    expectedRecordRows.push_back({static_cast<uint8_t>(midi::NoteOff), offTick, recordIds[bar], 1, 48, 0});
  }
  std::sort(expectedRecordRows.begin(), expectedRecordRows.end(), sealedRowLess);
  clockManager.assignCurrentTickSilently(length);
  track.stopRecording(length);
  Loop& loop = track.getActiveLoop();
  // Setup only: a real committed/current Record, never an empty fabricated root. Probe the first readable
  // pass state: if it already differs, the transfer wrote it; if it starts equal and later differs, a
  // post-transfer owner mutated the sealed rows.
  std::vector<SealedRecordRow> firstReadableRows;
  unsigned firstReadableTurn = 0;
  bool haveFirstReadable = false;
  for (unsigned turn = 0; turn < 20000 &&
       (track.hasPendingDeferredOverdubStop() || !loop.publishedSnapshotIsCurrent()); ++turn) {
    track.processDeferredIdleMaintenance(millis(), false,
        (turn & 1) ? MidiServiceDrain::DeferredIdleOwner::Snapshot
                   : MidiServiceDrain::DeferredIdleOwner::VisualCache);
    if (!haveFirstReadable && loop.passes.hasRecordPass()) {
      std::vector<SealedRecordRow> probe = sealedRecordRows(loop);
      if (!probe.empty()) {
        firstReadableRows = std::move(probe);
        firstReadableTurn = turn;
        haveFirstReadable = true;
      }
    }
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  if (haveFirstReadable) {
    char delta[900];
    describeSealedDelta(expectedRecordRows, firstReadableRows, delta, sizeof(delta));
    char detail[1100];
    std::snprintf(detail, sizeof(detail), "first readable Record pass turn=%u length=%lu rows=%zu %s",
                  firstReadableTurn, static_cast<unsigned long>(loop.loopLengthTicks),
                  firstReadableRows.size(), delta);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expectedRecordRows.size()),
                                     static_cast<uint32_t>(firstReadableRows.size()), detail);
    TEST_ASSERT_TRUE_MESSAGE(
        std::equal(expectedRecordRows.begin(), expectedRecordRows.end(), firstReadableRows.begin(),
                   sealedRowsEqual),
        detail);
  }
  clockManager.assignCurrentTickSilently(96);
  submitFrame(96); completeFrame();
  assertNote(completedFrame.notes, recordIds.front(), 48, 12, 48);
  // The immutable source baseline: the whole semantic Record-pass row vector after seal.
  const std::vector<SealedRecordRow> sealedRecordBaseline = sealedRecordRows(loop);
  {
    char delta[900];
    describeSealedDelta(expectedRecordRows, sealedRecordBaseline, delta, sizeof(delta));
    char detail[1100];
    std::snprintf(detail, sizeof(detail), "sealed Record baseline bars=%u %s", bars, delta);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expectedRecordRows.size()),
                                     static_cast<uint32_t>(sealedRecordBaseline.size()), detail);
    TEST_ASSERT_TRUE_MESSAGE(
        std::equal(expectedRecordRows.begin(), expectedRecordRows.end(), sealedRecordBaseline.begin(),
                   sealedRowsEqual),
        detail);
  }

  DisplayChainOwnerTurns owners;
  for (unsigned bar = 0; bar < std::min(bars, 16u); ++bar)
    owners.expected.push_back({recordIds[bar], 48, bar * Config::TICKS_PER_BAR + 12,
                              bar * Config::TICKS_PER_BAR + 48});
  size_t maxQueue = 0;
  unsigned maxCompletionWait = 0, waiting = 0;
  unsigned completions = DisplayCaptureTestAccess::api().completions;
  unsigned accepted = 0, retainedStarts = 0, retainedStops = 0;
  const unsigned framesAtChainStart = DisplayCaptureTestAccess::api().completions;
  for (unsigned overdub = 0; overdub < 22; ++overdub) {
    // A real player presses the next Start only after the previous interval ended;
    // a retained obligation is served within the press interval before the next
    // press. Failing to serve it here would be a liveness defect, not a fixture shape.
    unsigned servedBeforePress = 0;
    while ((track.hasAcceptedUnexecutedInteraction() || track.hasPendingOverdubStart() ||
            track.hasPendingDeferredOverdubStop()) && servedBeforePress < 4000) {
      owners.step(track);
      ++servedBeforePress;
      maxQueue = std::max(maxQueue, TrackCaptureStopTestAccess::jobs(track));
    }
    char context[220];
    std::snprintf(context, sizeof(context), "overdub=%u jobs=%zu paints=%u submitted=%u completed=%u denied=%u",
        overdub, TrackCaptureStopTestAccess::jobs(track), owners.paints,
        DisplayCaptureTestAccess::api().submitted, DisplayCaptureTestAccess::api().completions,
        owners.deniedDisplayTurns);
    TEST_ASSERT_LESS_THAN_MESSAGE(4000u, servedBeforePress,
        "accepted obligations must be served before the next press");
    const uint32_t tick = 96 + (overdub * 96) % (length - 192);
    clockManager.assignCurrentTickSilently(tick);
    track.startOverdubbing(tick);
    std::snprintf(context, sizeof(context), "overdub=%u jobs=%zu paints=%u submitted=%u completed=%u denied=%u",
        overdub, TrackCaptureStopTestAccess::jobs(track), owners.paints,
        DisplayCaptureTestAccess::api().submitted, DisplayCaptureTestAccess::api().completions,
        owners.deniedDisplayTurns);
    // DEC-078: a start met by a still-live predecessor obligation is retained on the
    // accepted-interaction carrier, not refused. It must be served exactly once later.
    if (!track.isOverdubbing()) {
      TEST_ASSERT_TRUE_MESSAGE(track.hasPendingOverdubStart() ||
          track.hasAcceptedUnexecutedInteraction(), context);
      ++retainedStarts;
    } else {
      ++accepted;
      // Distinct pitches avoid overlap-resolution ambiguity in the content oracle.
      const NoteId added = acceptPair(track, 60 + overdub, tick + 12, tick + 60);
      owners.expected.push_back({added, static_cast<uint8_t>(60 + overdub), tick + 12, tick + 60});
      clockManager.assignCurrentTickSilently(tick + 72);
      track.stopOverdubbing();
      if (track.hasAcceptedUnexecutedInteraction()) ++retainedStops;
    }
    maxQueue = std::max(maxQueue, TrackCaptureStopTestAccess::jobs(track));
    // The measured 20 turns are Track maintenance opportunities (as in the
    // original throughput fixture), not 20 iterations of the seven-owner loop.
    // Keep the production owner rotation; its display opportunities are interleaved,
    // never a direct-call paint rescue. The fixed outer cap diagnoses lost service.
    const unsigned maintenanceTarget = owners.maintenanceTurns + 20;
    for (unsigned turn = 0; turn < 120 && owners.maintenanceTurns < maintenanceTarget; ++turn) {
      owners.step(track);
      maxQueue = std::max(maxQueue, TrackCaptureStopTestAccess::jobs(track));
      if (DisplayCaptureTestAccess::api().completions != completions) {
        completions = DisplayCaptureTestAccess::api().completions;
        maxCompletionWait = std::max(maxCompletionWait, waiting);
        waiting = 0;
      } else ++waiting;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(maintenanceTarget, owners.maintenanceTurns, context);
    // Retain a final in-flight frame, but do not grant the domain extra turns.
    if (DisplayCaptureTestAccess::api().active) owners.complete();
    std::printf("display chain bars=%u %s maxQueue=%zu maxCompletionWait=%u now_paints=%u submissions=%u completions=%u jobs=%zu expected=%lu\n",
        bars, context, maxQueue, maxCompletionWait, owners.paints,
        DisplayCaptureTestAccess::api().submitted, DisplayCaptureTestAccess::api().completions,
        TrackCaptureStopTestAccess::jobs(track), static_cast<unsigned long>(overdub));
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(4u, maxQueue, context);
    assertNote(completedFrame.notes, recordIds.front(), 48, 12, 48);
    assertSealedRecordRowsUnchanged(loop, sealedRecordBaseline, overdub, context);
  }
  // Display progress is asserted on the measured window, before any teardown drain.
  const unsigned framesDuringChain =
      DisplayCaptureTestAccess::api().completions - framesAtChainStart;
  std::printf("display chain measured bars=%u frames=%u accepted=%u retainedStarts=%u retainedStops=%u\n",
      bars, framesDuringChain, accepted, retainedStarts, retainedStops);
  TEST_ASSERT_GREATER_THAN_MESSAGE(0u, framesDuringChain,
      "display frames must complete during the chain, not only after it");
  // Every accepted press is served exactly once (DEC-078) within a bounded drain.
  // The final interval is closed by its own Stop press, as a real chain ends.
  if (track.isOverdubbing()) {
    clockManager.assignCurrentTickSilently(96u + (21u * 96u) % (length - 192));
    track.stopOverdubbing();
  }
  for (unsigned turn = 0; turn < 60000 &&
       (track.hasPendingDeferredOverdubStop() || track.hasAcceptedUnexecutedInteraction() ||
        track.hasPendingOverdubStart() || track.isOverdubbing()); ++turn) {
    owners.step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_FALSE(track.hasPendingOverdubStart());
  assertSealedRecordRowsUnchanged(loop, sealedRecordBaseline, 22, "final-drain");
  TEST_ASSERT_EQUAL_UINT32(1 + accepted, loop.passes.capturePassCount());
  TEST_ASSERT_GREATER_THAN(0u, owners.paints);
}

void test_overdub_chain_preserves_frame_progress_one_bar() { exerciseDisplayOverdubChain(1); }
void test_overdub_chain_preserves_frame_progress_long_loop() { exerciseDisplayOverdubChain(24); }

void test_stationary_viewport_counts_real_completed_frames_without_transition_receipts() {
  Track& track = beginRecord();
  const NoteId id = acceptPair(track, 60, 12, 80);
  growRecordGeometry(track, 192);
  uint32_t windowStart = 0, windowLength = 0;
  for (unsigned frame = 0; frame < 4; ++frame) {
    const auto submitted = DisplayCaptureTestAccess::api().submitted;
    const auto completed = DisplayCaptureTestAccess::api().completions;
    submitFrame(192);
    TEST_ASSERT_EQUAL_UINT32(submitted + 1, DisplayCaptureTestAccess::api().submitted);
    TEST_ASSERT_EQUAL_UINT32(completed, DisplayCaptureTestAccess::api().completions);
    if (frame == 0) {
      windowStart = submittedFrame.source.windowStart;
      windowLength = submittedFrame.source.windowLength;
    }
    TEST_ASSERT_EQUAL_UINT32(windowStart, submittedFrame.source.windowStart);
    TEST_ASSERT_EQUAL_UINT32(windowLength, submittedFrame.source.windowLength);
    completeFrame();
    TEST_ASSERT_EQUAL_UINT32(completed + 1, DisplayCaptureTestAccess::api().completions);
    assertNote(completedFrame.notes, id, 60, 12, 80);
  }
}

void test_chain_oracle_separates_missing_service_from_missing_content() {
  Track& track = beginRecord();
  const NoteId id = acceptPair(track, 60, 12, 80);
  growRecordGeometry(track, 192);
  submitFrame(192); completeFrame();
  const std::vector<DisplayChainOwnerTurns::ExpectedNote> expected{{id, 60, 12, 80}};
  TEST_ASSERT_TRUE(DisplayChainOwnerTurns::frameMatches(completedFrame, expected));
  auto missing = completedFrame;
  missing.notes.clear();
  TEST_ASSERT_FALSE(DisplayChainOwnerTurns::frameMatches(missing, expected));
  auto stale = completedFrame;
  stale.notes.front().endTick = 79;
  TEST_ASSERT_FALSE(DisplayChainOwnerTurns::frameMatches(stale, expected));
  using Phase = DisplayWindowUtils::DisplayFramePhase;
  TEST_ASSERT_FALSE(DisplayChainOwnerTurns::phaseProgressed(Phase::Draw, Phase::Draw, 1, 1));
  TEST_ASSERT_TRUE(DisplayChainOwnerTurns::phaseProgressed(Phase::Draw, Phase::Transfer, 1, 1));
  TEST_ASSERT_TRUE(DisplayChainOwnerTurns::phaseProgressed(Phase::Transfer, Phase::Idle, 1, 2));
}

/// Slice C oracle: **painted notes must track committed passes with a stationary viewport and no publication
/// gap.** The device symptom was a flat painted-note count while passes grew. This drives the production commit
/// path (real `commitCapturePass`), keeps the paint window fixed, and asserts the painted count rises with each
/// committed pass and keeps the earlier identities — so a cache that was left clean cannot keep serving
/// pre-commit content.
void test_painted_notes_follow_committed_passes_on_a_stationary_viewport() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  Loop& loop = t.getActiveLoop();
  loop.loopLengthTicks = 4u * Config::TICKS_PER_BAR;
  t.forceSetState(TRACK_PLAYING);
  t.setActiveLoopIndex(0);
  submitFrame(0);
  const uint32_t baseline = static_cast<uint32_t>(submittedFrame.notes.size());
  completeFrame();
  for (unsigned pass = 0; pass < 3; ++pass) {
    loop.beginCapture(CapturePhase::Overdub);
    const uint32_t on = pass * 96u + 12u;
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(on, 1, static_cast<uint8_t>(60 + pass), 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(on + 40, 1, static_cast<uint8_t>(60 + pass), 0)));
    TEST_ASSERT_EQUAL(CommitResult::Committed,
                      loop.commitCapturePass(CommitReason::OverdubStop, on + 48));
    // Production runs the idle VisualCache lane between presses; the paint reads what that lane published.
    for (unsigned turn = 0; turn < 4096 && loop.visualCacheDirty; ++turn) {
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    }
    TEST_ASSERT_FALSE_MESSAGE(loop.visualCacheDirty,
        "the idle slice must settle the cache instead of clearing the flag without rebuilding");
    // Same viewport, same tick: only committed content changed.
    submitFrame(0);
    std::printf("slice C pass=%u painted=%u baseline=%u\n", pass,
                static_cast<uint32_t>(submittedFrame.notes.size()), baseline);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(baseline + pass + 1, static_cast<uint32_t>(submittedFrame.notes.size()),
        "each committed pass must reach the painted frame; a flat count is stale content");
    for (unsigned seen = 0; seen <= pass; ++seen) {
      bool found = false;
      for (const auto& note : submittedFrame.notes) {
        if (note.note == static_cast<uint8_t>(60 + seen)) found = true;
      }
      TEST_ASSERT_TRUE_MESSAGE(found, "an earlier committed pass must stay painted");
    }
    completeFrame();
  }
}

/// Slice F oracle: **a restored slot's cache must fill through the real idle lane without a published root.**
/// This is the loop-level half of the boot blank panel. The device showed the focused slot with the right
/// length at 10.97 s and *no* painted notes until 21.8 s, so the question is whether the lane can build such a
/// slot at all or whether admission starves it. The main-loop clock gate is not reachable from here; if this
/// test is green, any remaining starvation is main.cpp admission, not the rebuild.
void test_restored_slot_cache_fills_through_the_idle_lane_without_a_root() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  Loop& loop = t.getActiveLoop();
  LoopEventStore record;
  for (unsigned n = 0; n < 4; ++n) {
    MidiEvent on = MidiEvent::NoteOn(n * 96 + 12, 1, static_cast<uint8_t>(60 + n), 100);
    on.noteId = 700 + n;
    TEST_ASSERT_TRUE(record.append(on));
    TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(n * 96 + 52, 1, static_cast<uint8_t>(60 + n), 0)));
  }
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = 4 * bar;
  loop.markDisplayCachesStale();
  // Exactly the restored shape: content in the passes, no root, cache dirty and empty.
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_NULL_MESSAGE(loop.publishedSnapshotIdentity(), "the restored slot has no current root");
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(loop.visualCache.notes.empty());

  unsigned turns = 0;
  for (; turns < 64 && loop.visualCache.notes.empty(); ++turns) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  std::printf("slice F cache turns=%u notes=%zu dirty=%u\n", turns, loop.visualCache.notes.size(),
              static_cast<unsigned>(loop.visualCacheDirty));
  TEST_ASSERT_FALSE_MESSAGE(loop.visualCache.notes.empty(),
      "the idle lane must build a restored slot's cache; a refused build is the blank first frame");
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(loop.visualCache.notes.size()));

  // And the built cache must reach the panel.
  submitFrame(0);
  for (unsigned n = 0; n < 4; ++n) {
    bool found = false;
    for (const auto& note : submittedFrame.notes) {
      if (note.note == static_cast<uint8_t>(60 + n)) found = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "a restored slot's note must be painted once its cache is built");
  }
  completeFrame();
}

// DEC-210: a boot playback-set slot is prewarmed for its first paint window. The switch path must
// not throw that cache away: before this rule a PLAYING track switch ran `invalidateDisplayCaches()`
// on the target loop, so the panel painted `n=0` until the idle lane rebuilt it
// (`session_20261029_174940`: the owner cycled tracks and saw notes only after stopping).
static void seedRootlessRestoredVisualCache(Track& track, uint8_t basePitch, NoteId baseId) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  track.ensureLoopsAllocated();
  Loop& loop = track.getActiveLoop();
  LoopEventStore record;
  for (unsigned n = 0; n < 2; ++n) {
    MidiEvent on = MidiEvent::NoteOn(n * 96 + 12, 1, static_cast<uint8_t>(basePitch + n), 100);
    on.noteId = baseId + n;
    TEST_ASSERT_TRUE(record.append(on));
    TEST_ASSERT_TRUE(record.append(
        MidiEvent::NoteOff(n * 96 + 52, 1, static_cast<uint8_t>(basePitch + n), 0)));
  }
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = bar;
  loop.markDisplayCachesStale();
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  loop.ensureVisualCacheBuilt();
  TEST_ASSERT_FALSE_MESSAGE(loop.visualCacheDirty, "the fixture must start from a clean prewarmed cache");
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
}

void test_dec210_a_playing_track_focus_change_preserves_the_target_visual_cache() {
  Track& target = trackManager.getTrack(1);
  seedRootlessRestoredVisualCache(target, 70, 2000);
  target.forceSetState(TRACK_PLAYING);
  Loop& targetLoop = target.getActiveLoop();
  const size_t cachedNotes = targetLoop.visualCache.notes.size();
  const uint32_t cacheRevision = targetLoop.visualCache.revision;

  trackManager.setSelectedTrack(0);
  trackManager.setSelectedTrack(1);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(cachedNotes, targetLoop.visualCache.notes.size(),
                                   "a track switch must not clear the target's prewarmed visual cache");
  TEST_ASSERT_EQUAL_UINT32(cacheRevision, targetLoop.visualCache.revision);
  TEST_ASSERT_FALSE(targetLoop.visualCacheDirty);

  const DisplayNoteVec& resolved =
      DisplayCaptureTestAccess::resolveCommitted(target, target.getActiveLoopIndex(), 0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(cachedNotes, resolved.size(),
                                   "the switched-to PLAYING track must paint from its prewarmed cache");
}

void test_dec210_playback_set_visual_cache_readiness_waits_for_every_active_slot() {
  TEST_ASSERT_TRUE_MESSAGE(trackManager.playbackSetVisualCachesReady(),
                           "tracks with no loops/content cannot hold the boot set");
  Track& first = trackManager.getTrack(0);
  seedRootlessRestoredVisualCache(first, 60, 1000);
  TEST_ASSERT_TRUE(trackManager.playbackSetVisualCachesReady());

  Track& second = trackManager.getTrack(1);
  second.ensureLoopsAllocated();
  Loop& secondLoop = second.getActiveLoop();
  LoopEventStore record;
  MidiEvent on = MidiEvent::NoteOn(12, 1, 80, 100);
  on.noteId = 3000;
  TEST_ASSERT_TRUE(record.append(on));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(52, 1, 80, 0)));
  secondLoop.seedRecordPassFromStore(record);
  secondLoop.loopLengthTicks = Config::TICKS_PER_BAR;
  secondLoop.markDisplayCachesStale();
  TEST_ASSERT_FALSE_MESSAGE(trackManager.playbackSetVisualCachesReady(),
                            "one unready playback slot must hold the visual set");
  secondLoop.ensureVisualCacheBuilt();
  TEST_ASSERT_TRUE(trackManager.playbackSetVisualCachesReady());
}

/// Slice G oracle: **a rootless restored slot must obtain a published root on the publication lane**, because
/// without one the panel cannot paint until the visual cache is rebuilt (device: length correct at 11 s, first
/// content only after the restore sweep ended at 21-28 s). The fast first boot after the restore fix is the
/// pointer: that boot still had the writer's coherent root, so paint needed no cache build.
void test_rootless_restored_slot_gains_a_published_root_on_the_publication_lane() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  Loop& loop = t.getActiveLoop();
  LoopEventStore record;
  for (unsigned n = 0; n < 4; ++n) {
    MidiEvent on = MidiEvent::NoteOn(n * 96 + 12, 1, static_cast<uint8_t>(60 + n), 100);
    on.noteId = 800 + n;
    TEST_ASSERT_TRUE(record.append(on));
    TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(n * 96 + 52, 1, static_cast<uint8_t>(60 + n), 0)));
  }
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = 4 * bar;
  loop.markDisplayCachesStale();
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());

  // Negative control: the publication lane owes nothing until someone asks, which is why the display lane
  // must issue the request for a rootless restored slot.
  for (unsigned turn = 0; turn < 64 && !loop.publishedSnapshotIsCurrent(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(),
      "control: an unrequested root is not fabricated by the lane");

  // The production request the display lane issues when the selected slot has content but no root.
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
  unsigned turns = 0;
  for (; turns < 512 && !loop.publishedSnapshotIsCurrent(); ++turns) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  std::printf("slice G snapshot turns=%u current=%u\n", turns,
              static_cast<unsigned>(loop.publishedSnapshotIsCurrent()));
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
      "a restored rootless slot must publish a root; without it paint waits for a cache rebuild");

  // The paint half is deliberately NOT asserted here: with a current root the display still needs its first
  // frame *request* served by the idle producer (this fixture reports `source=none`), and that producer's boot
  // admission is the next slice. Recorded rather than asserted so the suite stays honest.
  submitFrame(0);
  std::printf("slice G paint after root: notes=%zu source=%s (idle producer owed)\n",
              submittedFrame.notes.size(), DisplayCaptureTestAccess::qualifiedSourceName(loop));
  completeFrame();
}


// Tasks 4.7/4.8 prerequisite repair (CommittedDisplayBase): a tile-scoped overlap view must not change
// what the live-capture frame draws. This is the RED that fixes the ownership split: before the split the
// committed base was the overlap view, so shrinking the view lost committed notes from the frame.
void test_committed_display_base_is_independent_of_the_overlap_view() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 24 * bar;
  Track& t = beginRecord();
  for (uint32_t b = 0; b < 24; ++b) {
    (void)acceptPair(t, static_cast<uint8_t>(60 + b % 8), b * bar + 12, b * bar + 40);
  }
  growRecordGeometry(t, length + 200);
  submitFrame(length + 184);
  completeFrame();
  (void)acceptPair(t, 95, length + 12, length + 80);  // pin 24 complete bars
  clockManager.assignCurrentTickSilently(length + 200);
  t.stopRecording(length + 200);
  Loop& loop = t.getActiveLoop();
  submitFrame(clockManager.getCurrentTick());
  completeFrame();
  clockManager.assignCurrentTickSilently(256);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  t.startOverdubbing(256);
  for (unsigned turn = 0; turn < 4096 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());

  const uint32_t tick = 17 * bar + 32;
  clockManager.assignCurrentTickSilently(tick);
  loop.markDisplayCachesStale();
  loop.visualCache.notes.clear();
  const NoteUtils::DisplayNoteVec baseline =
      DisplayCaptureTestAccess::liveCapture(t, 0, tick);
  TEST_ASSERT_FALSE(baseline.empty());

  // The overlap domain becomes one row (a tile-scoped view). A full committed-layer rebuild is forced by
  // dropping the live display cache: the split base re-gathers from the committed content, while a
  // source-view base would rebuild from the one row.
  NoteUtils::DisplayNoteVec oneTile;
  if (!loop.overdubSourceViewNotes().empty()) {
    oneTile.push_back(loop.overdubSourceViewNotes().front());
  }
  loop.replaceOverdubSourceViewNotesForTest(std::move(oneTile));
  displayManager.invalidateLiveDisplayCache();
  const NoteUtils::DisplayNoteVec after =
      DisplayCaptureTestAccess::liveCapture(t, 0, tick);

  const uint32_t winStart = DisplayCaptureTestAccess::drawnViewStart();
  const uint32_t winLength = DisplayCaptureTestAccess::drawnViewLength();
  size_t compared = 0;
  for (const auto& note : baseline) {
    if (!DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, winStart, winLength,
                                                  loop.loopLengthTicks)) {
      continue;
    }
    bool overlay = false;
    for (const auto& preview : loop.capturePreviewView().notes) {
      if (preview.noteId == note.noteId) {
        overlay = true;
        break;
      }
    }
    if (overlay) {
      continue;
    }
    ++compared;
    bool found = false;
    for (const auto& painted : after) {
      if (painted.noteId == note.noteId) {
        found = true;
        break;
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(found,
                             "a committed note must remain drawable without the overlap view");
  }
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(compared));

  // Direct contract of the new abstraction: a miss gathers from the committed content, never from the
  // overlap view. The view is still one row here, so a view-backed base cannot carry the record rows.
  displayManager.invalidateLiveDisplayCache();
  TEST_ASSERT_TRUE(DisplayCaptureTestAccess::buildCommittedBase(t, 0, tick));
  TEST_ASSERT_FALSE(DisplayCaptureTestAccess::committedBaseNotes().empty());
  bool directRecordRow = false;
  for (const auto& note : DisplayCaptureTestAccess::committedBaseNotes()) {
    if (note.note == 60) {
      directRecordRow = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(directRecordRow,
                           "the committed base must gather committed content, not the overlap view");
}

// ---------------------------------------------------------------------------
// Stale published root vs authoritative committed passes (chain-48 shape).
// ---------------------------------------------------------------------------

namespace {
constexpr uint32_t kStaleRootBar = Config::TICKS_PER_BAR;
constexpr uint32_t kStaleRootBars = 20;
constexpr uint32_t kStaleRootLength = kStaleRootBars * kStaleRootBar;

/// Commit one overdub pass through the production stop owner. The note's **phase** is explicit: the tick
/// passed to the input owner is `kStaleRootLength + phase`, which is the cycle the projection is anchored on.
void commitOverdubPassAt(Track& t, Loop& loop, uint32_t startTick, uint8_t pitch, uint32_t phaseOn,
                         uint32_t phaseOff) {
  clockManager.assignCurrentTickSilently(startTick);
  t.startOverdubbing(startTick);
  TEST_ASSERT_TRUE_MESSAGE(t.isOverdubbing(), "the fixture must open its overdub interval");
  const uint32_t absOn = kStaleRootLength + phaseOn;
  const uint32_t absOff = kStaleRootLength + phaseOff;
  clockManager.assignCurrentTickSilently(absOn);
  t.noteOn(1, pitch, 100, absOn);
  clockManager.assignCurrentTickSilently(absOff);
  t.noteOff(1, pitch, 0, absOff);
  clockManager.assignCurrentTickSilently(startTick + 800);
  t.stopOverdubbing();
  for (unsigned turn = 0; turn < 8192 && t.hasStopInFlight(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight(), "the fixture's overdub stop must retire");
  (void)loop;
}

const NoteUtils::DisplayNote* displayNoteById(const DisplayNoteVec& notes, NoteId id) {
  for (const auto& note : notes) {
    if (note.noteId == id) return &note;
  }
  return nullptr;
}

void gatherCommittedForTest(Track& t, Loop& loop, uint32_t startBar, uint32_t windowBars,
                            DisplayNoteVec& out) {
  size_t committedEnd = 0;
  gatherCommittedDrawSourceBars(t, 0, loop, startBar, startBar + windowBars - 1, 0, 127, out,
                                committedEnd);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(out.size()),
                           static_cast<uint32_t>(committedEnd));
}
}  // namespace

/// RED (chain-48): committed passes advance while the published root stays stale. `visualCacheDirty`
/// made the draw source prefer that stale root, so committed overdub rows vanished except where a live
/// preview temporarily reintroduced them. The committed draw source itself must serve the passes for the
/// requested window, and the same rows/geometry must appear whether or not the window crosses the wrap.
void test_stale_root_draw_source_serves_committed_overdub_passes_across_the_sliding_window() {
  Track& t = beginRecord();
  Loop& loop = t.getActiveLoop();
  const NoteId recA = acceptPair(t, 60, 12, 80);
  // Anchor the take's length at 20 bars: the committed length follows the maintained maximum tick.
  (void)acceptPair(t, 65, (kStaleRootBars - 1) * kStaleRootBar + 100,
                   (kStaleRootBars - 1) * kStaleRootBar + 200);
  growRecordGeometry(t, kStaleRootLength);
  clockManager.assignCurrentTickSilently(kStaleRootLength);
  t.stopRecording(kStaleRootLength);
  for (unsigned turn = 0; turn < 8192 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.hasCommittedPasses(), "the record pass must commit first");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kStaleRootLength, loop.loopLengthTicks,
                                   "the fixture needs a 20-bar loop for the sliding window");
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());

  // Overdub A: bar 5 (0-based bar 4) and bar 12 (0-based 11).
  commitOverdubPassAt(t, loop, kStaleRootLength, 72, 4 * kStaleRootBar + 100, 4 * kStaleRootBar + 220);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  NoteId a5 = kInvalidNoteId;
  {
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
    if (!rows.empty()) a5 = rows.front().noteId;
  }
  TEST_ASSERT_NOT_EQUAL_MESSAGE(kInvalidNoteId, a5, "overdub A must commit a note");

  // Overdub B: bar 2 (0-based 1) and bar 18 (0-based 17).
  commitOverdubPassAt(t, loop, kStaleRootLength + 1000, 76, kStaleRootBar + 100, kStaleRootBar + 220);
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  const NoteId b2 = [&] {
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
    return rows.empty() ? kInvalidNoteId : rows.front().noteId;
  }();
  TEST_ASSERT_NOT_EQUAL_MESSAGE(kInvalidNoteId, b2, "overdub B must commit a note");

  // The chain-48 shape: a published but **stale** root (an empty snapshot at an older revision), with a
  // dirty visual cache. The committed passes are the only current authority.
  auto stale = LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.startLoopTick,
                                         loop.peekNextNoteId(), 1);
  loop.storePublishedSnapshotForTest(stale, 1);
  TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                            "the fixture root must be stale against the committed passes");
  loop.visualCacheDirty = true;

  for (uint32_t startBar : {0u, 1u, 4u, 8u}) {
    DisplayNoteVec out;
    gatherCommittedForTest(t, loop, startBar, 16, out);
    TEST_ASSERT_FALSE_MESSAGE(out.empty(), "a stale root must not blank the committed draw source");
    const uint32_t last = startBar + 15;
    if (startBar == 0) {
      TEST_ASSERT_NOT_NULL_MESSAGE(displayNoteById(out, recA),
                                   "the record note must be served from the passes");
      TEST_ASSERT_NOT_NULL_MESSAGE(displayNoteById(out, b2),
                                   "overdub B (bar 2) must be served from the passes");
    }
    if (startBar <= 4 && 4 <= last) {
      TEST_ASSERT_NOT_NULL_MESSAGE(displayNoteById(out, a5),
                                   "overdub A (bar 5) must stay visible while the window slides");
    }
  }

  // Wrap-crossing window: bars 18..33 covers 18, 19, then 0..13. The same rows and geometry must result.
  DisplayNoteVec wrapped;
  DisplayNoteVec plain;
  gatherCommittedForTest(t, loop, 18, 16, wrapped);
  gatherCommittedForTest(t, loop, 4, 16, plain);
  const auto* wrapA5 = displayNoteById(wrapped, a5);
  const auto* plainA5 = displayNoteById(plain, a5);
  TEST_ASSERT_NOT_NULL_MESSAGE(wrapA5, "a wrap-crossing window must still serve the committed rows");
  TEST_ASSERT_NOT_NULL(plainA5);
  TEST_ASSERT_EQUAL_UINT8(plainA5->note, wrapA5->note);
  TEST_ASSERT_EQUAL_UINT32(plainA5->startTick, wrapA5->startTick);
  TEST_ASSERT_EQUAL_UINT32(plainA5->endTick, wrapA5->endTick);
  TEST_ASSERT_NOT_NULL_MESSAGE(displayNoteById(wrapped, recA),
                               "the wrap-crossing window must include bar 0");
  assertNote(wrapped, a5, 72, 4 * kStaleRootBar + 100, 4 * kStaleRootBar + 220);
  assertNote(wrapped, b2, 76, kStaleRootBar + 100, kStaleRootBar + 220);
}

/// Owner capture 151347: while an overdub is open and the published root lags, every accepted live note
/// advanced `captureDisplayRevision`, which the committed reuse key compared, so each frame rebuilt the
/// committed base -- a whole-window walk over every committed pass (display frames 50-155 ms, MIDI
/// timing degraded). The pass gather reads only committed passes; live rows are an overlay re-applied on
/// every compose. Frames at one window with only live input must reuse the committed base, and the
/// committed rows must stay visible on every frame.
void test_stale_root_committed_base_is_reused_while_live_overdub_input_arrives() {
  Track& t = beginRecord();
  Loop& loop = t.getActiveLoop();
  const NoteId recA = acceptPair(t, 60, 12, 80);
  (void)acceptPair(t, 65, (kStaleRootBars - 1) * kStaleRootBar + 100,
                   (kStaleRootBars - 1) * kStaleRootBar + 200);
  growRecordGeometry(t, kStaleRootLength);
  clockManager.assignCurrentTickSilently(kStaleRootLength);
  t.stopRecording(kStaleRootLength);
  for (unsigned turn = 0; turn < 8192 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  commitOverdubPassAt(t, loop, kStaleRootLength, 72, 4 * kStaleRootBar + 100, 4 * kStaleRootBar + 220);
  NoteId a5 = kInvalidNoteId;
  {
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
    if (!rows.empty()) a5 = rows.front().noteId;
  }
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, a5);
  auto stale = LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.startLoopTick,
                                         loop.peekNextNoteId(), 1);
  loop.storePublishedSnapshotForTest(stale, 1);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  loop.visualCacheDirty = true;

  // Open a second overdub and feed it live notes; the playhead stays inside one window.
  const uint32_t odStart = 2 * kStaleRootLength;
  clockManager.assignCurrentTickSilently(odStart);
  t.startOverdubbing(odStart);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const uint32_t frameTick = 2 * kStaleRootLength + 2 * kStaleRootBar + 10;
  (void)DisplayCaptureTestAccess::resolveCommitted(t, 0, frameTick);
  DisplayManagerInternal::resetDrawSourceCommittedPassGatherCountForTest();
  const uint32_t frames = 12;
  for (uint32_t i = 0; i < frames; ++i) {
    const uint32_t on = frameTick + 1 + 4 * i;
    clockManager.assignCurrentTickSilently(on);
    t.noteOn(1, static_cast<uint8_t>(40 + i), 100, on);
    t.noteOff(1, static_cast<uint8_t>(40 + i), 0, on + 2);
    const DisplayNoteVec& notes = DisplayCaptureTestAccess::resolveCommitted(t, 0, frameTick);
    TEST_ASSERT_NOT_NULL_MESSAGE(displayNoteById(notes, recA), "the record row must stay visible");
    TEST_ASSERT_NOT_NULL_MESSAGE(displayNoteById(notes, a5), "the committed overdub row must stay visible");
  }
  char msg[96];
  snprintf(msg, sizeof(msg), "committed pass walks across %u live-input frames at one window",
           static_cast<unsigned>(frames));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, DisplayManagerInternal::drawSourceCommittedPassGatherCountForTest(), msg);
}

/// Negative control: a **current** root is the authority. The committed pass walk must not run, and the
/// root's content (here an empty root) is what the draw source serves.
void test_current_root_draw_source_stays_root_backed_and_never_walks_the_passes() {
  Track& t = beginRecord();
  Loop& loop = t.getActiveLoop();
  const NoteId recA = acceptPair(t, 60, 12, 80);
  growRecordGeometry(t, kStaleRootLength);
  clockManager.assignCurrentTickSilently(kStaleRootLength);
  t.stopRecording(kStaleRootLength);
  for (unsigned turn = 0; turn < 8192 && !loop.hasCommittedPasses(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());

  auto current = LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.startLoopTick,
                                           loop.peekNextNoteId(), 1);
  loop.storePublishedSnapshotForTest(current, loop.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the control requires a current root");
  loop.visualCacheDirty = true;

  DisplayManagerInternal::resetDrawSourceCommittedPassGatherCountForTest();
  DisplayNoteVec out;
  gatherCommittedForTest(t, loop, 0, 16, out);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
                                   DisplayManagerInternal::drawSourceCommittedPassGatherCountForTest(),
                                   "a current root must not be replaced by a redundant pass walk");
  TEST_ASSERT_NULL_MESSAGE(displayNoteById(out, recA),
                           "the current root is the committed authority, not the passes");
}

static void exerciseCommittedWindowGatherAcrossWrap(bool publishSnapshot) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 20 * bar;
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  Loop& loop = track.getActiveLoop();
  LoopEventStore record;
  const uint32_t starts[] = {12, 10 * bar + 12, 17 * bar + 12, 19 * bar + 12};
  const uint32_t ends[] = {80, 10 * bar + 80, 18 * bar + 120, 19 * bar + 80};
  for (unsigned n = 0; n < 4; ++n) {
    MidiEvent on = MidiEvent::NoteOn(starts[n], 1, 60 + n, 100);
    MidiEvent off = MidiEvent::NoteOff(ends[n], 1, 60 + n, 0);
    on.noteId = off.noteId = 900 + n;
    TEST_ASSERT_TRUE(record.append(on));
    TEST_ASSERT_TRUE(record.append(off));
  }
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = length;
  if (publishSnapshot) {
    TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
    for (unsigned turn = 0; turn < 4096 && !loop.publishedSnapshotIsCurrent(); ++turn)
      loop.stepPendingSnapshotPublish(4, true, 0);
    TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                             "the current-root control must publish through production construction");
  } else {
    TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  }
  SessionMidiEventVec events;
  DisplayNoteVec notes;
  rebuildDisplayNotesInWindow(loop, loop, length, 18 * bar, 4 * bar, events, notes, false);
  assertNote(notes, 900, 60, starts[0], ends[0]);
  assertNote(notes, 902, 62, starts[2], ends[2]);
  assertNote(notes, 903, 63, starts[3], ends[3]);
  TEST_ASSERT_NULL_MESSAGE(displayNoteById(notes, 901), "the excluded middle must not leak into paint");
  TEST_ASSERT_EQUAL_UINT32(3, notes.size());

  rebuildDisplayNotesInWindow(loop, loop, length, 18 * bar, 2 * bar, events, notes, false);
  assertNote(notes, 902, 62, starts[2], ends[2]);
  assertNote(notes, 903, 63, starts[3], ends[3]);
  TEST_ASSERT_EQUAL_UINT32(2, notes.size());

  rebuildDisplayNotesInWindow(loop, loop, length, 0, length, events, notes, false);
  TEST_ASSERT_EQUAL_UINT32(4, notes.size());
  for (unsigned n = 0; n < 4; ++n)
    assertNote(notes, 900 + n, 60 + n, starts[n], ends[n]);
}

void test_rootless_committed_window_gather_keeps_both_wrap_spans_and_complete_notes() {
  exerciseCommittedWindowGatherAcrossWrap(false);
}

void test_current_snapshot_window_gather_keeps_both_wrap_spans_and_complete_notes() {
  exerciseCommittedWindowGatherAcrossWrap(true);
}

void test_rootless_boundary_pair_does_not_appear_in_an_unrelated_window() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, length = 20 * bar;
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore record;
  MidiEvent on = MidiEvent::NoteOn(length - 1, 1, 70, 100);
  MidiEvent off = MidiEvent::NoteOff(0, 1, 70, 0);
  on.noteId = off.noteId = 910;
  TEST_ASSERT_TRUE(record.append(on));
  TEST_ASSERT_TRUE(record.append(off));
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = length;
  SessionMidiEventVec events;
  DisplayNoteVec notes;
  rebuildDisplayNotesInWindow(loop, loop, length, 5 * bar, bar, events, notes, false);
  TEST_ASSERT_TRUE_MESSAGE(notes.empty(), "a boundary pair's one-tick tail must not leak into the middle");
  rebuildDisplayNotesInWindow(loop, loop, length, 19 * bar, 2 * bar, events, notes, false);
  assertNote(notes, 910, 70, length - 1, length - 1);
  TEST_ASSERT_EQUAL_UINT32(1, notes.size());
  rebuildDisplayNotesInWindow(loop, loop, length, 0, length, events, notes, false);
  assertNote(notes, 910, 70, length - 1, length - 1);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rootless_boundary_pair_does_not_appear_in_an_unrelated_window);
  RUN_TEST(test_rootless_committed_window_gather_keeps_both_wrap_spans_and_complete_notes);
  RUN_TEST(test_current_snapshot_window_gather_keeps_both_wrap_spans_and_complete_notes);
  RUN_TEST(test_overdub_chain_preserves_frame_progress_one_bar);
  RUN_TEST(test_overdub_chain_preserves_frame_progress_long_loop);
  RUN_TEST(test_stale_root_draw_source_serves_committed_overdub_passes_across_the_sliding_window);
  RUN_TEST(test_stale_root_committed_base_is_reused_while_live_overdub_input_arrives);
  RUN_TEST(test_current_root_draw_source_stays_root_backed_and_never_walks_the_passes);
  RUN_TEST(test_stationary_viewport_counts_real_completed_frames_without_transition_receipts);
  RUN_TEST(test_chain_oracle_separates_missing_service_from_missing_content);
  RUN_TEST(test_painted_notes_follow_committed_passes_on_a_stationary_viewport);
  RUN_TEST(test_restored_slot_cache_fills_through_the_idle_lane_without_a_root);
  RUN_TEST(test_dec210_a_playing_track_focus_change_preserves_the_target_visual_cache);
  RUN_TEST(test_dec210_playback_set_visual_cache_readiness_waits_for_every_active_slot);
  RUN_TEST(test_rootless_restored_slot_gains_a_published_root_on_the_publication_lane);
  RUN_TEST(test_rounded_record_stop_follows_wrapped_overdub_before_publication);
  RUN_TEST(test_empty_overdub_start_cannot_grow_frozen_record_tails);
  RUN_TEST(test_empty_successor_start_preserves_stopped_overdub_hold_endpoint);
  RUN_TEST(test_record_preview_is_cleared_before_stop_descriptor_reuse);
  RUN_TEST(test_held_record_preview_uses_frozen_stop_endpoint_before_and_after_publication);
  RUN_TEST(test_truncated_record_stop_reprojects_complete_first_sixteen_bars);
  RUN_TEST(test_closed_overdub_note_survives_truncated_record_publication_gap);
  RUN_TEST(test_apply_capture_playhead_tails_honours_the_frozen_close_tick);
  RUN_TEST(test_closed_overdub_note_survives_painted_stop_and_gradual_finalization);
  RUN_TEST(test_selected_gap_display_source_survives_competing_audible_track);
  RUN_TEST(test_committed_display_base_is_independent_of_the_overlap_view);
  RUN_TEST(test_late_record_window_and_closed_overdub_progress_before_root_publication);
  RUN_TEST(test_display_source_progresses_with_pending_successor_stop_and_across_wrap);
  RUN_TEST(test_capture_undo_refuses_the_retained_display_source);
  RUN_TEST(test_capture_redo_requalifies_the_retained_display_source);
  RUN_TEST(test_edit_undo_refuses_the_retained_display_source);
  RUN_TEST(test_retained_display_source_survives_the_wrap_that_appends_a_pass);
  RUN_TEST(test_retained_display_source_stands_down_once_the_sealed_tier_is_released);
  RUN_TEST(test_retained_display_source_accepts_the_declared_capacity_and_prices_it);
  RUN_TEST(test_retained_display_source_refuses_one_row_past_the_cap_and_keeps_the_previous_envelope);
  RUN_TEST(test_retained_display_source_stands_down_on_geometry_change_and_clear);
  RUN_TEST(test_retained_display_source_never_serves_another_slot_and_survives_the_focus_change);
  RUN_TEST(test_dense_late_record_window_and_closed_overdub_progress_before_root_publication);
  RUN_TEST(test_immediate_overdub_paints_before_any_record_finalization_turn);
  RUN_TEST(test_retired_predecessor_notes_survive_third_overdub_start);
  RUN_TEST(test_overview_preserves_finalizing_and_live_preview_bands);
  RUN_TEST(test_sealed_and_live_overdub_notes_paint_while_record_remains_private);
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
  RUN_TEST(test_partial_visual_cache_publication_refreshes_stationary_draw_source);
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
