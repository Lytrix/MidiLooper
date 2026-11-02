// C5.3b (DEC-246 amendment 24): how much lead does the successor window really need?
//
// While window N serves, the host-only probe (`RollingSuccessorProbe.h`) opens one successor demand for
// `[N.end, N.end + span)` no earlier than `lead` ticks before `N.end`, its deadline. The demand goes through the
// production retry, qualification, admission, build and publication; the **only** rule skipped is the DEC-147 lead
// price (`leadTooShort`), so the run measures the bounded producer itself. At `N.end` the successor is handed off or is a
// named miss (no merged fallback is counted as coverage, no tick-side build).
//
// Units. The host has no wall clock: one owner turn is `laneEvery` ticks. A lead is therefore a number of owner turns
// times the advance per turn; the device's real advance per turn is not measured here.
#include "../test_revision_manifest_lane/manifest_lane_harness.h"

#include "RollingSuccessorProbe.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

namespace probe = RollingSuccessorProbe;
using PlaybackTickContext::TickRoute;

constexpr uint32_t kSpan = 2 * kBar;

struct RunConfig {
  uint32_t bars = 4;
  uint32_t notesPerBar = 1;
  bool manifest = false;           // flag-1 shape: track 0 reads RevisionManifest
  uint32_t lead = UINT32_MAX;      // the successor demand opens no earlier than this before N.end
  bool bypass = true;              // skip only the DEC-147 lead price for the successor
  uint32_t laneEvery = 1;          // ticks per owner turn
  uint32_t denyRun = 0;            // track 0's holder grains denied after each one served
  uint32_t pressureTracks = 0;     // other tracks playing the same shape on the same holder lane
  uint32_t seams = 50;
  bool hideBefore = false;         // a production Delete edit (Hide) on track 0 before playback
  uint32_t moveAtSeam = UINT32_MAX;  // a Delete edit while that seam's successor is being prepared
};

struct RunResult {
  uint32_t seams = 0;           // successor deadlines reached
  uint32_t handedOff = 0, missed = 0, priceRefused = 0, cancelled = 0, duplicates = 0, requested = 0;
  uint32_t steadyTicks = 0;     // ticks from the first handoff to the last seam
  uint32_t steadyNonView = 0;   // of those, ticks track 0 was not served by its window
  uint32_t steadyMerged = 0;    // of those, ticks served by the merged fallback
  uint32_t staleAdoptions = 0;  // a window adopted after a move that still carried the old source
  uint64_t maxPrep = 0;         // request -> ready, ticks
  uint64_t minMargin = UINT64_MAX;  // ready -> deadline, ticks
  uint64_t maxQualify = 0;      // request -> admitted, ticks
  uint32_t maxServed = 0;       // track 0 holder grains between request and ready
  uint32_t maxDenied = 0;       // owner turns in that interval that served track 0 nothing
  uint32_t maxPricedGrains = 0; // the admission's own grain price for the window (after qualification)
  uint64_t firstHandoff = 0;
  uint64_t firstView = UINT64_MAX;  // the first tick track 0 was served by a window
  uint32_t nonViewAfterFirstView = 0;
  uint32_t leakedDemandTicks = 0;  // a refused/discarded successor's demand still active (must stay 0)
  uint64_t ticks = 0;
};

/// Production's Delete edit of the first committed note of `pitch` (as NOTE_EDIT commits it); the root is republished.
void hideFirstNoteOfPitch(Loop& loop, uint8_t pitch) {
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  const MidiEvent* on = nullptr;
  for (const MidiEvent& e : rows)
    if (e.isNoteOn() && e.data.noteData.note == pitch) { on = &e; break; }
  TEST_ASSERT_NOT_NULL_MESSAGE(on, "no committed NoteOn of that pitch");
  uint32_t off = on->tick + 1;
  for (const MidiEvent& e : rows)
    if (e.isNoteOff() && e.data.noteData.note == pitch && e.tick > on->tick) { off = e.tick; break; }
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.targetNoteId = on->noteId;
  row.startTick = on->tick;
  row.endTick = off;
  row.pitch = pitch;
  row.velocity = 100;
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, row));
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
  for (uint32_t grain = 0; grain < 4096 && loop.hasPendingSnapshotPublish(); ++grain) loop.stepPendingSnapshotPublish(1);
}

bool sourceCurrent(const Loop& loop, const PlaybackViewSourceIdentity& s, bool manifest) {
  if (s.playbackRevision != loop.playbackRevision) return false;
  return manifest ? s.manifestIdentity != 0 && s.manifestIdentity == CommittedManifestLane::gServable.identity
                  : s.snapshotIdentity == loop.publishedSnapshotIdentity();
}

RunResult run(const RunConfig& c) {
  sharedCaptureFinalizationSuiteSetUp();
  uninstallReader();
  probe::gKnobs = {};
  probe::gCounters = {};
  if (c.manifest) installReader();
  const uint32_t trackCount = 1 + c.pressureTracks;
  for (uint32_t i = 0; i < trackCount; ++i) recordTake(trackManager.getTrack(i), c.bars, c.notesPerBar);
  Track& t0 = trackManager.getTrack(0);
  Loop& loop0 = t0.getLoop(0);
  if (c.hideBefore) hideFirstNoteOfPitch(loop0, 62);
  if (c.manifest)
    for (int i = 0; i < 4; ++i) reader().serviceOwner(loop0);  // import, then the edit index if a Hide needs one
  probe::gKnobs.enabled = true;
  probe::gKnobs.bypassLeadPrice = c.bypass;
  probe::gKnobs.requestLeadTicks = c.lead;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  for (uint32_t i = 0; i < trackCount; ++i) {
    trackManager.getTrack(i).startPlaying(0);
    TrackCaptureStopTestAccess::unmute(trackManager.getTrack(i));
  }
  MidiSendCapture::reset();

  RunResult r;
  MidiServiceDrain::PlaybackHolderCursor cursor{};
  uint32_t denyLeft = 0;
  std::vector<uint64_t> servedGrains;  // ticks of track 0's served holder grains
  std::vector<uint64_t> nonViewTicks, mergedTicks;
  uint32_t lastStart = UINT32_MAX;
  uint64_t lastStorage = 0;
  bool moved = false;
  const uint64_t cap = uint64_t(c.seams + 8) * kSpan * 6 + 4 * uint64_t(c.bars) * kBar;
  uint64_t lastTick = 0;
  for (uint64_t tick = 0; tick < cap; ++tick) {
    lastTick = tick;
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(static_cast<uint32_t>(tick));
    if (c.manifest) reader().serviceOwner(loop0);
    if (!moved && c.moveAtSeam != UINT32_MAX && probe::gCounters.recordCount > c.moveAtSeam &&
        probe::gCounters.records[c.moveAtSeam].admitTick != 0) {
      hideFirstNoteOfPitch(loop0, 63);  // the source moves while seam k's successor is being built
      moved = true;
    }
    if (tick % c.laneEvery == 0) {
      const auto clock = clockManager.getPlaybackDeadlineClock();
      MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
      for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
        Track& track = trackManager.getTrack(i);
        (void)track.serviceRollingPlaybackWindow(clock);
        track.serviceAdmittedDeadlineMisses(clock);
      }
      for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
        offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(static_cast<uint32_t>(tick), clock);
      uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
      if (MidiServiceDrain::selectPlaybackHolderRoundRobin(cursor, offers, Config::NUM_TRACKS, chosen) ==
          MidiServiceDrain::PlaybackHolderSelection::Service) {
        if (chosen == 0 && denyLeft > 0) {
          --denyLeft;
        } else {
          (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(static_cast<uint32_t>(tick), nullptr, true,
                                                                          clock);
          if (chosen == 0) {
            servedGrains.push_back(tick);
            denyLeft = c.denyRun;
          }
        }
      }
    }
    for (uint32_t i = 0; i < trackCount; ++i) {
      const auto before = PlaybackTickContext::routeCountsForTest();
      trackManager.getTrack(i).playMidiEvents(static_cast<uint32_t>(tick), true);
      if (i != 0) continue;
      const auto after = PlaybackTickContext::routeCountsForTest();
      const bool view = after.route[int(TickRoute::View)] != before.route[int(TickRoute::View)];
      if (!view) nonViewTicks.push_back(tick);
      if (view && r.firstView == UINT64_MAX) r.firstView = tick;
      if (!view && r.firstView != UINT64_MAX) ++r.nonViewAfterFirstView;
      if (after.route[int(TickRoute::Merged)] != before.route[int(TickRoute::Merged)]) mergedTicks.push_back(tick);
    }
    {
      const auto& rt = TrackCaptureStopTestAccess::runtimeFor(t0, 0);
      const auto& f = rt.rolling;
      if (f.successorState == 3 && f.phase == LoopPlaybackRuntime::RollingPhase::Serving && f.deadlineTick != 0) {
        const uint64_t end = f.deadlineTick + std::min<uint32_t>(kSpan, f.loopLength - f.startPhase);
        if ((rt.pendingViewRequest_.active && rt.pendingViewRequest_.deadlineTick == end) ||
            (rt.frozenViewRequest.active && rt.frozenViewRequest.deadlineTick == end))
          ++r.leakedDemandTicks;
      }
    }
    if (getenv("C53B_TRACE") && moved && tick % 192 == 0) {
      const auto& rt = TrackCaptureStopTestAccess::runtimeFor(t0, 0);
      const auto* ls = rt.viewSetIfPrewarmed();
      printf("TRACE t=%" PRIu64 " ph=%u ss=%u why=%u st=%u dl=%" PRIu64 " pend=%d froz=%d own=%d bld=%d jp=%d exit=%s role=%u c=%u r=%u b=%u i=%u\n",
             tick, unsigned(rt.rolling.phase), unsigned(rt.rolling.successorState), unsigned(rt.rolling.blockedBy),
             rt.rolling.startPhase, rt.rolling.deadlineTick, rt.pendingViewRequest_.active, rt.frozenViewRequest.active,
             rt.ownsCoverageSource(), rt.viewBuild.active(), rt.completedPromisePendingJudgement,
             PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-",
             unsigned(rt.coverageRoleRefusal(false)), ls->lifecycle.current, ls->lifecycle.ready, ls->lifecycle.build,
             ls->lifecycle.idle);
    }
    // An adoption: the served window changed. Its source must be the current one at that tick.
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval coverage{};
    const auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t0, 0);
    const auto* set = runtime.viewSetIfPrewarmed();
    if (t0.publishedPlaybackWindowForSlot(0, served, coverage) && set &&
        runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        set->lifecycle.current < set->views.size()) {
      const uint64_t storage = set->views[set->lifecycle.current].storageGeneration;
      if (coverage.startPhase != lastStart || storage != lastStorage) {
        if (!sourceCurrent(loop0, served, c.manifest)) ++r.staleAdoptions;
        lastStart = coverage.startPhase;
        lastStorage = storage;
      }
    }
    if (tick % 256 == 0) {
      uint32_t reached = 0;
      for (uint32_t i = 0; i < std::min(probe::gCounters.recordCount, probe::kRecords); ++i)
        reached += probe::gCounters.records[i].track == 0 && probe::gCounters.records[i].deadline <= tick;
      if (reached >= c.seams) break;
    }
  }

  const uint32_t n = std::min(probe::gCounters.recordCount, probe::kRecords);
  uint64_t lastDeadline = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const auto& rec = probe::gCounters.records[i];
    if (rec.track != 0 || rec.deadline > lastTick) continue;  // track 0, deadlines the run reached
    ++r.seams;
    ++r.requested;
    r.handedOff += rec.outcome == uint8_t(probe::Outcome::HandedOff);
    r.missed += rec.outcome == uint8_t(probe::Outcome::Missed);
    r.priceRefused += rec.outcome == uint8_t(probe::Outcome::PriceRefused);
    r.cancelled += rec.outcome == uint8_t(probe::Outcome::Cancelled);
    lastDeadline = std::max(lastDeadline, rec.deadline);
    if (rec.outcome == uint8_t(probe::Outcome::HandedOff) && r.firstHandoff == 0) r.firstHandoff = rec.deadline;
    if (rec.admitTick) r.maxQualify = std::max(r.maxQualify, rec.admitTick - rec.requestTick);
    r.maxPricedGrains = std::max<uint32_t>(r.maxPricedGrains, rec.pricedGrains);
    if (!rec.readyTick) continue;
    r.maxPrep = std::max(r.maxPrep, rec.readyTick - rec.requestTick);
    if (rec.readyTick <= rec.deadline) r.minMargin = std::min(r.minMargin, rec.deadline - rec.readyTick);
    uint32_t served = 0;
    for (uint64_t g : servedGrains) served += g >= rec.requestTick && g <= rec.readyTick;
    uint32_t turns = 0;
    for (uint64_t k = rec.requestTick; k <= rec.readyTick; ++k) turns += k % c.laneEvery == 0;
    r.maxServed = std::max(r.maxServed, served);
    r.maxDenied = std::max(r.maxDenied, turns - served);
  }
  r.duplicates = probe::gCounters.duplicateStarts;  // every track: one successor per served window, anywhere
  if (r.firstHandoff != 0 && lastDeadline > r.firstHandoff) {
    r.steadyTicks = static_cast<uint32_t>(lastDeadline - r.firstHandoff);
    for (uint64_t k : nonViewTicks) r.steadyNonView += k >= r.firstHandoff && k < lastDeadline;
    for (uint64_t k : mergedTicks) r.steadyMerged += k >= r.firstHandoff && k < lastDeadline;
  }
  r.ticks = lastTick + 1;
  probe::gKnobs = {};
  uninstallReader();
  return r;
}

void print(const char* label, const RunConfig& c, const RunResult& r) {
  printf("C53B %-14s src=%s bars=%u npb=%u A=%u deny=%u press=%u lead=%u | seams=%u req=%u ok=%u miss=%u "
         "price=%u canc=%u dup=%u stale=%u | steady=%u nonview=%u merged=%u | prep<=%" PRIu64 " qual<=%" PRIu64
         " margin>=%" PRId64 " served<=%u denied<=%u priced=%u\n",
         label, c.manifest ? "manifest" : "snapshot", c.bars, c.notesPerBar, c.laneEvery, c.denyRun, c.pressureTracks,
         c.lead, r.seams, r.requested, r.handedOff, r.missed, r.priceRefused, r.cancelled, r.duplicates,
         r.staleAdoptions, r.steadyTicks, r.steadyNonView, r.steadyMerged, r.maxPrep, r.maxQualify,
         r.minMargin == UINT64_MAX ? int64_t(-1) : int64_t(r.minMargin), r.maxServed, r.maxDenied,
         r.maxPricedGrains);
}

bool allHandedOff(const RunResult& r) {
  return r.seams != 0 && r.handedOff == r.seams && r.missed == 0 && r.cancelled == 0;
}

/// The smallest lead at which every successor of the run is handed off. Coarse: one span halved down to 48 ticks (a
/// sixteenth); then a bisection below the last passing point, down to one tick. 0: not even one span works. The
/// bisection assumes a passing lead stays passing when raised; the coarse points above it are all checked.
uint32_t sweepMinimumLead(const char* label, RunConfig c) {
  uint32_t found = 0;
  uint32_t failing = 0;
  for (uint32_t lead = kSpan; lead >= 48; lead /= 2) {
    c.lead = lead;
    const RunResult r = run(c);
    print(label, c, r);
    if (!allHandedOff(r)) {
      failing = lead;
      break;
    }
    found = lead;
  }
  if (found == 0) return 0;
  uint32_t lo = failing;  // largest known failing lead (0: none known)
  uint32_t hi = found;    // smallest known passing lead
  while (hi - lo > 1) {
    c.lead = lo + (hi - lo) / 2;
    const RunResult r = run(c);
    if (allHandedOff(r)) hi = c.lead;
    else {
      lo = c.lead;
      print(label, c, r);
    }
  }
  c.lead = hi;
  print(label, c, run(c));
  return hi;
}

}  // namespace

extern "C" void setUp() {
  sharedCaptureFinalizationSuiteSetUp();
  uninstallReader();
  RollingSuccessorProbe::gKnobs = {};
}
extern "C" void tearDown() {
  RollingSuccessorProbe::gKnobs = {};
  uninstallReader();
  sharedCaptureFinalizationSuiteTearDown();
}

// The structural fact behind amendment 24: with the DEC-147 price in force, no successor at N's end is ever admitted.
void test_c5_3b_the_declared_price_refuses_every_successor_at_n_end() {
  for (bool manifest : {false, true}) {
    RunConfig c;
    c.manifest = manifest;
    c.bypass = false;
    c.seams = 12;
    const RunResult r = run(c);
    print("price_in_force", c, r);
    TEST_ASSERT_GREATER_THAN_UINT32(0, r.requested);
    TEST_ASSERT_EQUAL_UINT32(r.requested, r.priceRefused);
    TEST_ASSERT_EQUAL_UINT32(0, r.handedOff);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.leakedDemandTicks, "a refused successor's demand outlived its refusal");
    TEST_ASSERT_GREATER_THAN_UINT32(1, r.requested);  // the lane kept requesting a successor for later windows
  }
}

// The sweep: the smallest lead at which every successor of a run is ready and current at N.end, per shape, source,
// owner cadence and denial pattern. Recorded in `evidence/2026-11-02-c5-3b-successor-lead-measurement.md`. The one
// assertion is the decision the owner asked for: is the measured bound below one span?
void test_c5_3b_minimum_successor_lead_sweep() {
  struct Case { const char* label; uint32_t bars, npb, laneEvery, deny, press; bool hide, manifest; };
  const Case cases[] = {
      // physical: one owner, no denial, one owner turn per tick
      {"1bar_sparse", 1, 1, 1, 0, 0, false, false},
      {"4bar_sparse", 4, 1, 1, 0, 0, false, false},
      {"4bar_sparse", 4, 1, 1, 0, 0, false, true},
      {"20bar_sparse", 20, 1, 1, 0, 0, false, true},
      {"4bar_dense", 4, 32, 1, 0, 0, false, false},
      {"8bar_dense", 8, 32, 1, 0, 0, false, false},
      {"20bar_dense", 20, 32, 1, 0, 0, false, false},
      {"4bar_hide", 4, 32, 1, 0, 0, true, false},
      {"4bar_hide", 4, 1, 1, 0, 0, true, true},
      {"4bar_dense", 4, 32, 1, 0, 0, false, true},
      {"20bar_dense", 20, 32, 1, 0, 0, false, true},
      {"4bar_hide", 4, 32, 1, 0, 0, true, true},
      // qualified: the declared seven denials per grain, and eight real tracks on the round-robin lane
      {"4bar_deny7", 4, 32, 1, 7, 0, false, false},
      {"20bar_deny7", 20, 32, 1, 7, 0, false, false},
      {"4bar_deny7", 4, 1, 1, 7, 0, false, true},
      {"4bar_deny7", 4, 32, 1, 7, 0, false, true},
      {"4bar_press7", 4, 32, 1, 0, 7, false, false},
      {"4bar_press7", 4, 1, 1, 0, 7, false, true},
      {"4bar_press7", 4, 32, 1, 0, 7, false, true},
      {"20bar_press7", 20, 32, 1, 0, 7, false, false},
      // the owner cadence scales the lead: eight ticks per owner turn
      {"4bar_dense_A8", 4, 32, 8, 0, 0, false, false},
      {"4bar_deny7_A8", 4, 32, 8, 7, 0, false, false},
  };
  for (const Case& k : cases) {
    RunConfig c;
    c.bars = k.bars;
    c.notesPerBar = k.npb;
    c.laneEvery = k.laneEvery;
    c.denyRun = k.deny;
    c.pressureTracks = k.press;
    c.hideBefore = k.hide;
    c.manifest = k.manifest;
    c.seams = 20;
    const uint32_t lmin = sweepMinimumLead(k.label, c);
    printf("C53B_LMIN %-14s src=%s A=%u lmin=%u ticks (%u owner turns)\n", k.label,
           k.manifest ? "manifest" : "snapshot", k.laneEvery, lmin, lmin / k.laneEvery);
    if (k.bars == 1) continue;  // the one-bar loop is served by its cold full-loop window: no positioned successor
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, lmin, k.label);
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(kSpan, lmin, k.label);
  }
}

// The continuity gate (amendment 24) at the natural request point, N's own start: 50 consecutive seams, no lane-owned
// merged tick, no gap, no duplicate start, no stale serve -- in both source configurations.
void test_c5_3b_fifty_seams_are_continuous_in_both_source_configurations() {
  struct Case { const char* label; uint32_t bars, npb, press; bool manifest; };
  const Case cases[] = {
      {"4bar_sparse", 4, 1, 0, false}, {"4bar_sparse", 4, 1, 0, true},   {"4bar_dense", 4, 32, 0, false},
      {"8bar_dense", 8, 32, 0, false}, {"20bar_dense", 20, 32, 0, false}, {"20bar_sparse", 20, 1, 0, true},
      {"4bar_press7", 4, 32, 7, false}, {"4bar_press7", 4, 1, 7, true},
      {"4bar_dense", 4, 32, 0, true},   {"20bar_dense", 20, 32, 0, true}, {"4bar_press7", 4, 32, 7, true},
  };
  for (const Case& k : cases) {
    RunConfig c;
    c.bars = k.bars;
    c.notesPerBar = k.npb;
    c.pressureTracks = k.press;
    c.manifest = k.manifest;
    c.lead = kSpan;
    c.seams = 50;
    const RunResult r = run(c);
    print(k.label, c, r);
    char detail[96];
    snprintf(detail, sizeof(detail), "%s src=%s", k.label, k.manifest ? "manifest" : "snapshot");
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(50, r.seams, detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(r.seams, r.handedOff, detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.missed, detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.duplicates, detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.staleAdoptions, detail);
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, r.steadyTicks, detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.steadyNonView, detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.steadyMerged, detail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.leakedDemandTicks, detail);
  }
}

// A source move while a successor is being prepared: the candidate is never restamped. It is discarded by name, or it
// finishes for the old source and is refused at N.end; no window adopted after the move carries the old source; the
// lane continues with successors of the new source.
void test_c5_3b_a_source_move_during_preparation_is_named_and_never_served_stale() {
  for (bool manifest : {false, true}) {
    RunConfig c;
    c.manifest = manifest;
    c.notesPerBar = 32;
    c.lead = kSpan;
    c.seams = 20;
    c.moveAtSeam = 4;
    const RunResult r = run(c);
    print("move_mid_prep", c, r);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.staleAdoptions, manifest ? "manifest" : "snapshot");
    TEST_ASSERT_EQUAL_UINT32(0, r.duplicates);
    TEST_ASSERT_GREATER_THAN_UINT32(0, r.missed + r.cancelled);  // the moved candidate was named, not served
    TEST_ASSERT_GREATER_THAN_UINT32(4, r.handedOff);             // and the lane carried on afterwards
    TEST_ASSERT_EQUAL_UINT32(0, r.leakedDemandTicks);
  }
}

// The one-bar loop: once its first window is served, every traversal is served. (The snapshot lane serves it from its
// cold whole-loop window; the routed slot cancels that snapshot candidate (C5.3a case 9) and is served by successive
// positioned windows instead.) Checked over the
// same run length as fifty seams, in both source configurations.
void test_c5_3b_the_one_bar_loop_is_continuous_without_a_successor() {
  for (bool manifest : {false, true}) {
    RunConfig c;
    c.bars = 1;
    c.manifest = manifest;
    c.lead = kSpan;
    c.seams = 50;
    const RunResult r = run(c);
    printf("C53B 1bar_cold src=%s ticks=%" PRIu64 " firstView=%" PRIu64 " nonViewAfter=%u successors=%u\n",
           manifest ? "manifest" : "snapshot", r.ticks, r.firstView, r.nonViewAfterFirstView, r.requested);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(UINT64_MAX, r.firstView, manifest ? "manifest" : "snapshot");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.nonViewAfterFirstView, manifest ? "manifest" : "snapshot");
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  // `C53B_ONLY=price|move` runs one case (mutation checks); `C53B_TRACE` adds the post-move state trace.
  if (const char* only = getenv("C53B_ONLY")) {
    if (std::string(only) == "price") RUN_TEST(test_c5_3b_the_declared_price_refuses_every_successor_at_n_end);
    if (std::string(only) == "move") RUN_TEST(test_c5_3b_a_source_move_during_preparation_is_named_and_never_served_stale);
    return UNITY_END();
  }
  RUN_TEST(test_c5_3b_the_declared_price_refuses_every_successor_at_n_end);
  RUN_TEST(test_c5_3b_minimum_successor_lead_sweep);
  RUN_TEST(test_c5_3b_fifty_seams_are_continuous_in_both_source_configurations);
  RUN_TEST(test_c5_3b_a_source_move_during_preparation_is_named_and_never_served_stale);
  RUN_TEST(test_c5_3b_the_one_bar_loop_is_continuous_without_a_successor);
  return UNITY_END();
}
