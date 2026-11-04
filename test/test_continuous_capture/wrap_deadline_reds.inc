// C3.4 deadline off the Clock (owner rulings 2026-11-04; design packet
// `docs/Plans/2026-11-04-wrap-deadline-off-clock.md`, revision 2, section 6).
//
// The device wedged inside one overdub wrap for 123.46 s: `materializeDeferredOverdubCompletionsAtDeadline` answered 518
// owed completions with a whole-loop derive each (`captures/session_20261104_140156.log`). These cases drive the
// production MIDI, wrap, undo, stop and Snapshot-owner routes over an interval whose completions are still owed at the
// wrap, and compare the result with the **retained-path oracle**: the same gestures with the coverage worker drained
// before every wrap, so nothing is owed when the wrap seals.
//
// At the RED baseline every case failed: R1 because the wrap answered and sealed on the Clock, the parity cases because
// the forced deadline derived each completion without the interval's own pending rows (a long source note was
// re-shortened by every owed completion, the last one winning). The off-Clock obligation slice turned them green, so
// they run in the default gate. The oracle is a sequential canonical run: the source complete before each lap.

namespace wrap_deadline {

constexpr uint32_t kBars = 8;
constexpr uint32_t kLoopLen = 768 * kBars;
constexpr uint8_t kSourcePitches = 48;
constexpr uint8_t kFirstPitch = 36;

/// A playing slot whose committed source holds one long note per pitch (36..83, 0..5999), with a published root.
Track& prepareLongSourceTrack() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = kLoopLen;
  loop.beginCapture(CapturePhase::Record);
  for (uint8_t i = 0; i < kSourcePitches; ++i)
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, static_cast<uint8_t>(kFirstPitch + i), 100)));
  for (uint8_t i = 0; i < kSourcePitches; ++i)
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(6000, 1, static_cast<uint8_t>(kFirstPitch + i), 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(kLoopLen, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  track.prewarmPlaybackForSlot(0, true);
  track.forceSetState(TRACK_PLAYING);
  for (unsigned turn = 0; turn < 8192 && !loop.publishedSnapshotIsCurrent(); ++turn)
    (void)loop.stepPendingSnapshotPublish(16);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(), "the source root must be published first");
  return track;
}

/// The retained path: the coverage worker runs until no completion is owed (no tile owed, the set and the overflow
/// empty). This is what the wrap would find if the worker had kept up.
void drainRetainedPath(Loop& loop) {
  for (unsigned turn = 0; turn < 200000; ++turn) {
    const bool owed = loop.overdubCoverageDeferredCompletionsForTest() != 0 ||
                      loop.overdubCompletionOverflowForTest() != 0 || loop.overdubCoverageOwedCountForTest() != 0;
    if (!owed && !loop.hasOutstandingWrapObligation()) return;
    // Production lane order: an earlier lap's obligation (decided rows only, in the oracle) retires first (C1).
    if (!loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn))
      (void)loop.serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn);
  }
  TEST_FAIL_MESSAGE("the retained path did not drain");
}

/// The sequential canonical oracle's precondition: earlier obligations retired and the whole loop's source prepared,
/// so every decision of the coming lap is made at its own NoteOn/NoteOff against complete content.
void prepareCompleteSource(Loop& loop) {
  for (unsigned turn = 0; turn < 200000 && loop.hasOutstandingWrapObligation(); ++turn)
    (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
  (void)loop.overdubCoverageForTest().markSpanSealed(0, kLoopLen);
  for (unsigned turn = 0; turn < 200000 && loop.overdubCoverageOwedCountForTest() != 0; ++turn)
    (void)loop.serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, loop.overdubCoverageOwedCountForTest(), "oracle: the source must be complete");
}

/// `n` overdub notes over the long source notes. For the subject the committed generation is moved first, so every
/// span is unprepared and each NoteOff's completion is retained (16) or owed beyond the set (DEC-247), as on the
/// device; the oracle plays the same notes against a complete source.
void inputOwedNotes(Loop& loop, unsigned n, uint32_t lapBase, unsigned salt, bool oracle = false) {
  if (oracle) {
    prepareCompleteSource(loop);
  } else {
    loop.notifyCommittedContentChangedForTest();
  }
  for (unsigned i = 0; i < n; ++i) {
    const uint8_t pitch = static_cast<uint8_t>(kFirstPitch + ((i + salt) % kSourcePitches));
    const uint32_t on = 40 + ((i * 11u + salt * 7u) % 5800u);
    inputNote(pitch, lapBase + on, lapBase + on + 20);
  }
}

unsigned owedNow(const Loop& loop) {
  return loop.overdubCoverageDeferredCompletionsForTest() + loop.overdubCompletionOverflowForTest();
}

bool wrapAt(Track& track, uint32_t tick) {
  clockManager.assignCurrentTickSilently(tick);
  return track.commitOverdubWrap();
}

/// Every turn the off-Clock design may need (coverage/discharge lane and the Snapshot owner), until both are idle.
void drainMaintenance(Track& track) {
  Loop& loop = track.getActiveLoop();
  for (unsigned turn = 0; turn < 200000; ++turn) {
    // The production lane order (`TrackDeferredMaintenance`): the oldest wrap obligation, then the coverage worker.
    const bool obligation = loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
    const bool coverage = obligation ||
                          loop.serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn) !=
                              Loop::OverdubCoverageTurn::Idle;
    const bool publish = loop.hasPendingSnapshotPublish();
    if (publish) TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
    if (!coverage && !publish && owedNow(loop) == 0 && !loop.hasOutstandingWrapObligation()) return;
  }
  TEST_FAIL_MESSAGE("maintenance did not converge");
}

struct Outcome {
  std::vector<std::tuple<uint32_t, NoteId, uint8_t, uint8_t>> content;  // tick, id, status, pitch
  std::vector<std::vector<std::tuple<NoteId, uint32_t, uint32_t, int>>> recordCompanions;  // per session record
  bool rootCurrent = false;
};

Outcome observe(Track& track) {
  Loop& loop = track.getActiveLoop();
  Outcome out;
  SessionMidiEventVec events;
  loop.copyEffectiveCommittedEventsInRange(events, 0, kLoopLen, true);
  for (const MidiEvent& e : events)
    out.content.emplace_back(e.tick, e.noteId, static_cast<uint8_t>(e.type), e.data.noteData.note);
  std::sort(out.content.begin(), out.content.end());
  const size_t records = loop.overdubSessionUndoDepth() + loop.overdubSessionRedoDepth();
  for (size_t r = 0; r < records; ++r) {
    std::vector<std::tuple<NoteId, uint32_t, uint32_t, int>> rows;
    for (const EditPassId id : loop.overdubSessionCompanionIdsAt(r))
      for (const EditPass& row : loop.passes.editPasses)
        if (row.id == id)
          rows.emplace_back(row.targetNoteId, row.startTick, row.endTick, static_cast<int>(row.state));
    std::sort(rows.begin(), rows.end());
    out.recordCompanions.push_back(std::move(rows));
  }
  out.rootCurrent = loop.publishedSnapshotIsCurrent();
  return out;
}

void assertSameOutcome(const Outcome& oracle, const Outcome& subject, const char* what) {
  char message[200];
  if (oracle.content != subject.content) {
    // The first differences, so a failure names the events (tick, id, type, pitch) rather than only a count.
    unsigned shown = 0;
    for (size_t i = 0; i < oracle.content.size() && i < subject.content.size() && shown < 6; ++i) {
      if (oracle.content[i] == subject.content[i]) continue;
      const auto& o = oracle.content[i];
      const auto& s = subject.content[i];
      std::printf("%s diff[%u]: oracle tick=%u id=%u type=%u pitch=%u | subject tick=%u id=%u type=%u pitch=%u\n",
                  what, static_cast<unsigned>(i), std::get<0>(o), std::get<1>(o), std::get<2>(o), std::get<3>(o),
                  std::get<0>(s), std::get<1>(s), std::get<2>(s), std::get<3>(s));
      ++shown;
    }
  }
#if defined(WRAP_DEADLINE_TRACE_PITCH)
  for (const auto* run : {&oracle, &subject}) {
    std::printf("%s %s pitch %d:", what, run == &oracle ? "oracle" : "subject", WRAP_DEADLINE_TRACE_PITCH);
    for (const auto& e : run->content)
      if (std::get<3>(e) == WRAP_DEADLINE_TRACE_PITCH)
        std::printf(" %u%s#%u", std::get<0>(e), std::get<2>(e) == 0x90 ? "on" : "off", std::get<1>(e));
    std::printf("\n");
    for (size_t r = 0; r < run->recordCompanions.size(); ++r) {
      std::printf("%s %s record %u:", what, run == &oracle ? "oracle" : "subject", static_cast<unsigned>(r));
      for (const auto& c : run->recordCompanions[r])
        std::printf(" n%u[%u..%u]s%d", std::get<0>(c), std::get<1>(c), std::get<2>(c), std::get<3>(c));
      std::printf("\n");
    }
  }
#endif
  std::snprintf(message, sizeof(message), "%s: content %u vs oracle %u events", what,
                static_cast<unsigned>(subject.content.size()), static_cast<unsigned>(oracle.content.size()));
  TEST_ASSERT_TRUE_MESSAGE(oracle.content == subject.content, message);
  std::snprintf(message, sizeof(message), "%s: %u records vs oracle %u", what,
                static_cast<unsigned>(subject.recordCompanions.size()),
                static_cast<unsigned>(oracle.recordCompanions.size()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(oracle.recordCompanions.size(), subject.recordCompanions.size(), message);
  for (size_t r = 0; r < oracle.recordCompanions.size(); ++r) {
    std::snprintf(message, sizeof(message), "%s: record %u companions %u vs oracle %u (owner, geometry, state)",
                  what, static_cast<unsigned>(r), static_cast<unsigned>(subject.recordCompanions[r].size()),
                  static_cast<unsigned>(oracle.recordCompanions[r].size()));
    TEST_ASSERT_TRUE_MESSAGE(oracle.recordCompanions[r] == subject.recordCompanions[r], message);
  }
}

void freshHarness() {
  setUp();
}

/// One overdub session: `laps` wraps of `owedPerLap` notes each, the oracle draining the retained path before each
/// wrap. `between` runs after wrap `undoAfterLap` (0 = none) and may undo/redo or stop.
template <typename Between>
Outcome runSession(bool oracle, unsigned laps, unsigned owedPerLap, unsigned betweenAfterLap, Between between) {
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  for (unsigned lap = 0; lap < laps; ++lap) {
    const uint32_t base = lap * kLoopLen;
    inputOwedNotes(loop, owedPerLap, base, lap, oracle);
    if (oracle) drainRetainedPath(loop);
    if (!wrapAt(track, base + kLoopLen - 1)) {
      // R4: both obligation slots owned -> the wrap defers (DEC-069) and is retried once the oldest retires.
      TEST_ASSERT_FALSE_MESSAGE(loop.wrapObligationCapacityAvailable(), "a wrap may only defer for capacity");
      for (unsigned turn = 0; turn < 200000 && !loop.wrapObligationCapacityAvailable(); ++turn)
        (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
      TEST_ASSERT_TRUE_MESSAGE(wrapAt(track, base + kLoopLen - 1), "the deferred wrap is retried and commits");
    }
    if (lap + 1 == betweenAfterLap) between(track);
  }
  drainMaintenance(track);
  return observe(track);
}
Outcome runSession(bool oracle, unsigned laps, unsigned owedPerLap) {
  return runSession(oracle, laps, owedPerLap, 0, [](Track&) {});
}

}  // namespace wrap_deadline

/// R1 -- per-wrap Clock work is constant. The wrap's transferred phases (the C3.4 deadline, the companion seal, the
/// source-view apply) do no derive and save no edit pass on the Clock, for 0, 50 and 518 owed completions alike.
/// Today the wrap answers every owed completion with a derive and seals every companion inside the Clock dispatch.
void test_wrap_deadline_red_clock_work_is_constant_in_owed_completions() {
  using namespace wrap_deadline;
  const unsigned oweds[] = {0, 50, 518};
  char message[200];
  for (const unsigned owed : oweds) {
    freshHarness();
    Track& track = prepareLongSourceTrack();
    Loop& loop = track.getActiveLoop();
    clockManager.assignCurrentTickSilently(0);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_TRUE(track.isOverdubbing());
    inputOwedNotes(loop, owed == 0 ? 50 : owed, 0, 0);
    if (owed == 0) drainRetainedPath(loop);  // same notes, nothing owed: only decided companions reach the wrap
    const unsigned owedAtWrap = owedNow(loop);
    const uint32_t derivesBefore = LoopContentResolution::resolveDisplayNotesInWindowCountForTest();
    const size_t editPassesBefore = loop.passes.editPasses.size();
    TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));
    const uint32_t derives = LoopContentResolution::resolveDisplayNotesInWindowCountForTest() - derivesBefore;
    const size_t saved = loop.passes.editPasses.size() - editPassesBefore;
    std::snprintf(message, sizeof(message), "owed at wrap %u: wrap Clock ran %u derives and saved %u edit passes",
                  owedAtWrap, static_cast<unsigned>(derives), static_cast<unsigned>(saved));
    std::printf("wrap-deadline R1: %s\n", message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, derives, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, saved, message);
  }
}

/// R2 -- exact source-cut parity, one wrap: what the off-Clock obligation answers equals the retained path.
void test_wrap_deadline_parity_single_wrap_matches_retained_path() {
  using namespace wrap_deadline;
  const Outcome oracle = runSession(true, 1, 518);
  const Outcome subject = runSession(false, 1, 518);
  assertSameOutcome(oracle, subject, "R2 single wrap");
}

/// R3/R4 -- multi-wrap backlog and later edits to older NoteIds: three laps of 518 owed each retire in record order,
/// each record keeps its own companions, and a later record's companions on older NoteIds never move an earlier
/// record's answers.
void test_wrap_deadline_parity_three_wrap_backlog_matches_retained_path() {
  using namespace wrap_deadline;
  const Outcome oracle = runSession(true, 3, 518);
  const Outcome subject = runSession(false, 3, 518);
  assertSameOutcome(oracle, subject, "R3/R4 three-wrap backlog");
}

/// R5 -- undone-record progress: undo record P right after its wrap, keep recording, then redo it. P's companions are
/// still produced (Disabled while undone) and the redone content equals the oracle.
void test_wrap_deadline_parity_undone_record_keeps_resolving() {
  using namespace wrap_deadline;
  const auto undoRedo = [](Track& track) {
    TEST_ASSERT_TRUE(track.getActiveLoop().undoOverdubSession());  // the session-record flip TrackUndo routes to
    drainMaintenance(track);
    TEST_ASSERT_TRUE(track.getActiveLoop().redoOverdubSession());
  };
  const Outcome oracle = runSession(true, 2, 200, 1, undoRedo);
  const Outcome subject = runSession(false, 2, 200, 1, undoRedo);
  assertSameOutcome(oracle, subject, "R5 undone record");
}

/// R6/R7 -- stopped-transport discharge and publication liveness: stop the overdub after a wrap with completions owed.
/// The stop is accepted at once; the obligation retires while stopped; publication converges to a current root holding
/// the oracle's content.
void test_wrap_deadline_parity_stop_discharges_while_stopped_and_publishes() {
  using namespace wrap_deadline;
  const auto stop = [](Track& track) {
    clockManager.assignCurrentTickSilently(kLoopLen + 40);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_FALSE_MESSAGE(track.isOverdubbing(), "stop acceptance must not wait for owed completions");
    const bool owedAtStop = track.getActiveLoop().hasOutstandingWrapObligation();
    for (unsigned grain = 0; grain < 200000 && track.hasPendingDeferredOverdubStop(); ++grain) {
      // The idle turns production runs while stopped: the discharge lane (C4), the Snapshot owner, the stop job.
      (void)track.getActiveLoop().serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
      TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
      (void)TrackCaptureStopTestAccess::step(track);
    }
    std::printf("wrap-deadline R6: obligation outstanding at stop acceptance: %u\n", owedAtStop ? 1u : 0u);
    TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(), "the deferred stop must retire");
  };
  const Outcome oracle = runSession(true, 1, 300, 1, stop);
  const Outcome subject = runSession(false, 1, 300, 1, stop);
  assertSameOutcome(oracle, subject, "R6/R7 stop");
  TEST_ASSERT_TRUE_MESSAGE(subject.rootCurrent, "R7: publication must converge to a current root");
}

/// G3 (C1 ruling: owed too, exact) -- the next interval plays notes on pitches the previous record still owes; its
/// decisions on those pitches equal the oracle, where the previous record was answered before the next interval began.
void test_wrap_deadline_parity_next_interval_on_owed_pitches_matches_oracle() {
  using namespace wrap_deadline;
  // Lap 2 uses the same pitch rotation (salt 1 shifts by one pitch), so nearly every lap-2 note lands on a pitch with
  // a lap-1 completion still owed in the subject run.
  const Outcome oracle = runSession(true, 2, 518);
  const Outcome subject = runSession(false, 2, 518);
  assertSameOutcome(oracle, subject, "G3 next interval");
}

/// C2 (owner 2026-11-04) -- a pre-session row changes state after a wrap, before that wrap's obligation answers (a
/// NOTE_EDIT replace disables it). Session 1 leaves a Shorten companion on source note 36 (0..99); session 2 wraps with
/// completions owed on pitch 36, then that companion is disabled. The obligation must answer against the state at its
/// wrap: the same record companions as the oracle, where the flip happens only after the answer.
wrap_deadline::Outcome runPreSessionFlip(bool flipBeforeAnswer) {
  using namespace wrap_deadline;
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(36, 100, 200);  // shortens source 36 (0..6000) to 0..99: a companion of session 1
  TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));
  drainMaintenance(track);
  EditPassIdList preSession = loop.overdubSessionCompanionIdsAt(0);
  TEST_ASSERT_FALSE_MESSAGE(preSession.empty(), "precondition: session 1 sealed a companion");
  clockManager.assignCurrentTickSilently(kLoopLen + 40);
  midiButtonActions.handleToggleRecord();  // stop session 1
  for (unsigned grain = 0; grain < 200000 && track.hasPendingDeferredOverdubStop(); ++grain) {
    TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  const uint32_t base = 2 * kLoopLen;
  clockManager.assignCurrentTickSilently(base);
  midiButtonActions.handleToggleRecord();  // session 2
  TEST_ASSERT_TRUE(track.isOverdubbing());
  loop.notifyCommittedContentChangedForTest();
  for (unsigned i = 0; i < 40; ++i) inputNote(36, base + 300 + i * 120, base + 300 + i * 120 + 40);
  TEST_ASSERT_TRUE(wrapAt(track, base + kLoopLen - 1));
  TEST_ASSERT_TRUE_MESSAGE(loop.hasOutstandingWrapObligation(), "precondition: session 2 owes at its wrap");
  // The flip is a direct row-state change (what NOTE_EDIT's replace does to the row); this harness has no owner that
  // re-queues the stale root after it, so only the answering lanes are drained: the cut is what is under test.
  const auto drainLanes = [&loop]() {
    for (unsigned turn = 0; turn < 200000; ++turn) {
      const bool obligation = loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
      const bool coverage = obligation || loop.serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn) !=
                                              Loop::OverdubCoverageTurn::Idle;
      if (!coverage && owedNow(loop) == 0 && !loop.hasOutstandingWrapObligation()) return;
    }
    TEST_FAIL_MESSAGE("the answering lanes did not converge");
  };
  if (flipBeforeAnswer) loop.disableEditPasses(preSession);
  drainLanes();
  if (!flipBeforeAnswer) loop.disableEditPasses(preSession);
  drainLanes();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, loop.wrapCutJournalOverflows(), "the flip journal must hold the flip");
  return observe(track);
}

void test_wrap_deadline_cut_ignores_a_pre_session_flip_after_the_wrap() {
  using namespace wrap_deadline;
  const Outcome oracle = runPreSessionFlip(false);
  const Outcome subject = runPreSessionFlip(true);
  assertSameOutcome(oracle, subject, "C2 pre-session flip");
}

/// Review B2 -- the wrap runs in the PIT ISR on internal clock. A wrap that lands inside a lane turn (every phase,
/// including retirement) must defer through the owed wrap commit and never cancel, reuse or lose an obligation; once
/// the turn ends the deferred wrap commits, and the outcome equals the oracle.
namespace wrap_deadline {
struct IsrWrapProbe {
  Track* track = nullptr;
  uint32_t tick = 0;
  unsigned fired = 0;
  unsigned deferred = 0;
  bool armed = false;
};
void isrWrapProbe(void* ctx) {
  auto& probe = *static_cast<IsrWrapProbe*>(ctx);
  if (!probe.armed) return;
  ++probe.fired;
  clockManager.assignCurrentTickSilently(probe.tick);
  if (!probe.track->commitOverdubWrap()) ++probe.deferred;
  probe.armed = false;  // one interrupt-shaped wrap per arming
}
}  // namespace wrap_deadline

void test_wrap_deadline_isr_wrap_inside_a_lane_turn_defers_and_loses_nothing() {
  using namespace wrap_deadline;
  const Outcome oracle = runSession(true, 2, 200);
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  inputOwedNotes(loop, 200, 0, 0);
  TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));
  inputOwedNotes(loop, 200, kLoopLen, 1);
  IsrWrapProbe probe;
  probe.track = &track;
  probe.tick = 2 * kLoopLen - 1;
  Loop::wrapObligationTurnProbeForTest = isrWrapProbe;
  Loop::wrapObligationTurnProbeContextForTest = &probe;
  // Arm the interrupt on every turn until lap 2's wrap has committed: it lands in each phase of lap 1's obligation.
  for (unsigned turn = 0; turn < 200000 && loop.overdubSessionUndoDepth() < 2; ++turn) {
    probe.armed = true;
    (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
    if (!loop.hasOutstandingWrapObligation()) {
      probe.armed = false;
      TEST_ASSERT_TRUE_MESSAGE(wrapAt(track, 2 * kLoopLen - 1), "the deferred wrap commits once no turn is running");
    }
  }
  Loop::wrapObligationTurnProbeForTest = nullptr;
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, probe.fired, "the interrupt-shaped wrap must have fired inside a turn");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(probe.fired, probe.deferred, "every wrap inside a lane turn must defer");
  drainMaintenance(track);
  assertSameOutcome(oracle, observe(track), "B2 ISR wrap inside a lane turn");
}

/// Review B1 -- the stop's undo entry and companion history read the session's companion ids. With an obligation
/// still owed at the stop, the job must not reach those phases until the obligation has sealed its late companions,
/// so the OverdubPassAdded entry carries every companion of the session (a later global undo then removes them too).
void test_wrap_deadline_stop_undo_entry_waits_for_late_companions() {
  using namespace wrap_deadline;
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  inputOwedNotes(loop, 150, 0, 0);
  TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));
  const PassId wrapPass = loop.lastCommittedPassId();
  TEST_ASSERT_TRUE(loop.hasOutstandingWrapObligation());
  clockManager.assignCurrentTickSilently(kLoopLen + 40);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_FALSE_MESSAGE(track.isOverdubbing(), "the stop is accepted at once");
  // The job alone: it must park before companion history while the obligation is owed.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::stepJobOnly(track);
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(), "the job waits for its session's obligation");
  // Parked at the gate: a route may already have *set* a companion-reading phase (some jump straight to the transfer),
  // but the gate holds the grain that would execute it. The undo entry below is the oracle for "nothing read early".
  std::printf("wrap-deadline B1: parked at phase %u with the obligation owed\n",
              static_cast<unsigned>(TrackCaptureStopTestAccess::job(track).admission.phase));
  for (unsigned grain = 0; grain < 200000 && track.hasPendingDeferredOverdubStop(); ++grain) {
    TrackCaptureStopTestAccess::snapshotOwnerTurn(track);
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(loop.hasOutstandingWrapObligation());
  const UndoEntry* entry = nullptr;
  for (const UndoEntry& e : TrackCaptureStopTestAccess::undoStack(track).entries)
    if (e.kind == UndoEntryKind::OverdubPassAdded) entry = &e;
  TEST_ASSERT_NOT_NULL(entry);
  unsigned late = 0;
  unsigned missing = 0;
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.id <= wrapPass) continue;  // the wrap's late companions and the stop's own are newer than the wrap pass
    ++late;
    if (std::find(entry->editPassIds.begin(), entry->editPassIds.end(), row.id) == entry->editPassIds.end()) ++missing;
  }
  char message[160];
  std::snprintf(message, sizeof(message), "%u companion rows after the wrap, %u missing from the undo entry", late,
                missing);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, late, message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, missing, message);
}

/// Review B4 -- a predecessor stop waits only for its own session's obligations. Session A wraps and stops to PLAYING;
/// a successor session B opens at once and wraps. Once A's obligation has retired, A's job retires while B's
/// obligation is still owed (a Loop-wide wait would hold A, and with it B's own later stop).
void test_wrap_deadline_predecessor_stop_does_not_wait_for_the_successor() {
  using namespace wrap_deadline;
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  inputOwedNotes(loop, 80, 0, 0);
  TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));
  clockManager.assignCurrentTickSilently(kLoopLen + 40);
  midiButtonActions.handleToggleRecord();  // stop A (to PLAYING)
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(kLoopLen + 80);
  midiButtonActions.handleToggleRecord();  // successor B
  if (!track.isOverdubbing()) {
    TEST_IGNORE_MESSAGE("this harness refuses a successor while the predecessor job is pending");
  }
  inputOwedNotes(loop, 80, kLoopLen, 1);
  TEST_ASSERT_TRUE(wrapAt(track, 2 * kLoopLen - 1));
  // Retire A's obligation only (FIFO head), then step A's job alone.
  for (unsigned turn = 0; turn < 200000; ++turn) {
    if (!loop.hasOutstandingWrapObligation()) break;
    if (!loop.wrapObligationOwedByStoppingSession(false)) break;  // nothing of A's left
    (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.hasOutstandingWrapObligation(), "precondition: B's obligation is still owed");
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain) {
    TrackCaptureStopTestAccess::snapshotOwnerTurnOnly(track);
    (void)TrackCaptureStopTestAccess::stepJobOnly(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "the predecessor's stop must retire while only the successor still owes");
  TEST_ASSERT_TRUE(loop.hasOutstandingWrapObligation());
}

/// Review B3 -- C1 defers a held note's NoteOn occupancy on a pitch an earlier obligation owes. If that obligation
/// retires while the key is held, the decision must survive to the NoteOff (occupancy and span answered together):
/// the sounding source note is shortened to the hold, exactly as in the sequential oracle.
wrap_deadline::Outcome runHeldAcrossRetire(bool oracle) {
  using namespace wrap_deadline;
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  // Lap 1: late notes on 40..44, so source 40 still sounds over 0..4999 and the obligation owes pitch 40.
  if (oracle) {
    prepareCompleteSource(loop);
  } else {
    loop.notifyCommittedContentChangedForTest();
  }
  for (uint8_t i = 0; i < 5; ++i) inputNote(static_cast<uint8_t>(40 + i), 5000 + i * 100, 5020 + i * 100);
  if (oracle) drainRetainedPath(loop);
  TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));
  if (oracle) {
    prepareCompleteSource(loop);
  } else {
    TEST_ASSERT_TRUE_MESSAGE(loop.pitchOwedByOutstandingWrapObligation(40), "precondition: pitch 40 owed");
  }
  // Lap 2: hold 40 over the sounding source, let the obligation retire mid-hold, then release.
  clockManager.assignCurrentTickSilently(kLoopLen + 3000);
  midiHandler.handleMidiMessage(midi::NoteOn, 1, 40, 100, SOURCE_USB);
  // Mid-hold: the lanes run until the obligation has retired and the coverage worker is idle. The held note's own
  // occupancy hold stays owed by design (answered with its span at the NoteOff), so "nothing owed" is not the stop.
  for (unsigned turn = 0; turn < 200000; ++turn) {
    const bool obligation = loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
    const bool coverage = obligation || loop.serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn) !=
                                            Loop::OverdubCoverageTurn::Idle;
    if (loop.hasPendingSnapshotPublish()) TrackCaptureStopTestAccess::snapshotOwnerTurnOnly(track);
    if (!coverage && !loop.hasOutstandingWrapObligation()) break;
  }
  TEST_ASSERT_FALSE(loop.hasOutstandingWrapObligation());
  clockManager.assignCurrentTickSilently(kLoopLen + 3100);
  midiHandler.handleMidiMessage(midi::NoteOff, 1, 40, 0, SOURCE_USB);
  drainMaintenance(track);
  TEST_ASSERT_TRUE(wrapAt(track, 2 * kLoopLen - 1));
  drainMaintenance(track);
  return observe(track);
}

void test_wrap_deadline_held_note_occupancy_survives_an_obligation_retiring_mid_hold() {
  const wrap_deadline::Outcome oracle = runHeldAcrossRetire(true);
  const wrap_deadline::Outcome subject = runHeldAcrossRetire(false);
  wrap_deadline::assertSameOutcome(oracle, subject, "B3 held across retire");
}

/// PSRAM-exhaustion ruling (2026-11-04) -- an external allocation refusal during tile preparation (the live coverage
/// worker and a wrap obligation's cut) must never reach the allocator's abort, corrupt a tile, advance a cursor past
/// unprocessed data, release an owed completion or retry without bound; MIDI input keeps being served; once memory is
/// reclaimed the same work completes with the unpressured result. RED today: the growth reaches the fatal branch.
#include "Utils/CheckedCapacity.h"
namespace wrap_deadline {
struct PressureResult {
  Outcome outcome;
  unsigned abortReached = 0;
  unsigned probeRefusals = 0;
  size_t captureEventsUnderPressure = 0;
};
PressureResult runUnderExternalRefusal(bool pressure) {
  PressureResult r;
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  inputOwedNotes(loop, 200, 0, 0);
  TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));       // an obligation whose cut must be prepared
  inputOwedNotes(loop, 60, kLoopLen, 1);               // and live tiles the coverage worker must prepare
  nativeExternalPoolPressure() = NativeExternalPoolPressure{};
  if (pressure) nativeExternalPoolPressure().refuseAtOrAboveBytes = 4096;
  const size_t captureBefore = loop.capture.store.size();
  for (unsigned turn = 0; turn < 2000; ++turn) {
    (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
    (void)loop.serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn);
    if (turn == 1000) inputNote(70, kLoopLen + 5000, kLoopLen + 5010);  // control/MIDI keeps being served
  }
  r.captureEventsUnderPressure = loop.capture.store.size() - captureBefore;
  r.abortReached = nativeExternalPoolPressure().abortReached;
  r.probeRefusals = nativeExternalPoolPressure().probeRefusals;
  nativeExternalPoolPressure() = NativeExternalPoolPressure{};  // memory reclaimed
  drainMaintenance(track);
  TEST_ASSERT_TRUE(wrapAt(track, 2 * kLoopLen - 1));
  drainMaintenance(track);
  r.outcome = observe(track);
  return r;
}
}  // namespace wrap_deadline

void test_tile_preparation_external_refusal_is_named_bounded_and_recoverable() {
  using namespace wrap_deadline;
  const PressureResult control = runUnderExternalRefusal(false);
  const PressureResult pressured = runUnderExternalRefusal(true);
  char message[200];
  std::snprintf(message, sizeof(message), "abort reached %u times, probe refusals %u over 2000 lane turns",
                pressured.abortReached, pressured.probeRefusals);
  std::printf("alloc-refusal: %s\n", message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, pressured.abortReached, message);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, pressured.probeRefusals, "the refusal must be seen and named");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(64u, pressured.probeRefusals, "a refused tile must not busy-retry");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, pressured.captureEventsUnderPressure, "MIDI input is served under refusal");
  assertSameOutcome(control.outcome, pressured.outcome, "alloc refusal then reclaim");
}

/// PSRAM-exhaustion ruling -- a request no pool could ever serve is permanently insufficient: the obligation stays
/// owed (never released), the refusal is named, and the lane parks instead of retrying (no busy retry).
void test_tile_preparation_permanent_insufficiency_parks_and_keeps_the_obligation_owed() {
  using namespace wrap_deadline;
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  inputOwedNotes(loop, 200, 0, 0);
  TEST_ASSERT_TRUE(wrapAt(track, kLoopLen - 1));
  nativeExternalPoolPressure() = NativeExternalPoolPressure{};
  nativeExternalPoolPressure().refuseAtOrAboveBytes = 4096;
  nativeExternalPoolPressure().totalCapacityBytes = 2048;  // nothing at or above 4 KB can ever be served
  for (unsigned turn = 0; turn < 4000; ++turn) (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
  const unsigned refusals = nativeExternalPoolPressure().probeRefusals;
  const unsigned aborts = nativeExternalPoolPressure().abortReached;
  nativeExternalPoolPressure() = NativeExternalPoolPressure{};
  std::printf("alloc-refusal permanent: %u probe refusals over 4000 turns, cut refusals %u\n", refusals,
              static_cast<unsigned>(loop.wrapCutRefusals()));
  TEST_ASSERT_EQUAL_UINT32(0u, aborts);
  TEST_ASSERT_TRUE_MESSAGE(loop.hasOutstandingWrapObligation(), "the obligation stays owed, never released");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(2u, refusals, "a permanently insufficient request parks, it is not retried");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, loop.wrapCutRefusals(), "named once");
}

/// PSRAM-exhaustion ruling -- the historical cut's retention, measured by buffer ownership over a dense backlog: five
/// laps of 512 owed notes each over the long source, every obligation drained by the production lane order. Reports
/// the peak per buffer (sum of capacities at the worst turn) and asserts the bound this slice establishes.
void test_wrap_cut_memory_by_buffer() {
  using namespace wrap_deadline;
  freshHarness();
  Track& track = prepareLongSourceTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  Loop::WrapCutMemory peak;
  size_t peakTotal = 0;
  size_t answeringPeak = 0;
  const auto sample = [&]() {
    const Loop::WrapCutMemory m = loop.wrapCutMemoryForTest();
    if (m.total() > peakTotal) {
      peakTotal = m.total();
      peak = m;
    }
    if (m.cutNotes != 0 && m.total() > answeringPeak) answeringPeak = m.total();
  };
  for (unsigned lap = 0; lap < 5; ++lap) {
    inputOwedNotes(loop, 512, lap * kLoopLen, lap);
    if (!wrapAt(track, lap * kLoopLen + kLoopLen - 1)) {
      for (unsigned t = 0; t < 200000 && !loop.wrapObligationCapacityAvailable(); ++t) {
        (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
        sample();
      }
      TEST_ASSERT_TRUE(wrapAt(track, lap * kLoopLen + kLoopLen - 1));
    }
    for (unsigned t = 0; t < 64; ++t) {
      (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
      sample();
    }
  }
  for (unsigned t = 0; t < 200000 && loop.hasOutstandingWrapObligation(); ++t) {
    (void)loop.serviceWrapObligationTurn(CoveragePrerequisite::kGrainEventsPerTurn);
    sample();
  }
  std::printf("wrap-cut memory peak: total=%u layer=%u raw=%u notes=%u cutNotes=%u membership=%u pending=%u bytes; "
              "answering-phase peak=%u bytes\n",
              unsigned(peakTotal), unsigned(peak.layer), unsigned(peak.rawEvents), unsigned(peak.notes),
              unsigned(peak.cutNotes), unsigned(peak.membership), unsigned(peak.pending), unsigned(answeringPeak));
  // Bounds this slice establishes (host sizes; baseline before it: 305 KB peak, the cut copied while the cursor lived
  // and a retired slot kept its cut capacity): the preparation peak never coexists with an assembled cut, and the
  // answering phase holds only the cut notes and the interval's own pending set.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, unsigned(peak.cutNotes), "no assembled cut coexists with a preparing cursor");
  TEST_ASSERT_LESS_THAN_UINT32(unsigned(peakTotal), unsigned(answeringPeak));
}

void registerWrapDeadlineReds() {
  RUN_TEST(test_wrap_deadline_red_clock_work_is_constant_in_owed_completions);
  RUN_TEST(test_wrap_deadline_parity_single_wrap_matches_retained_path);
  RUN_TEST(test_wrap_deadline_parity_three_wrap_backlog_matches_retained_path);
  RUN_TEST(test_wrap_deadline_parity_undone_record_keeps_resolving);
  RUN_TEST(test_wrap_deadline_parity_stop_discharges_while_stopped_and_publishes);
  RUN_TEST(test_wrap_deadline_parity_next_interval_on_owed_pitches_matches_oracle);
  RUN_TEST(test_wrap_deadline_cut_ignores_a_pre_session_flip_after_the_wrap);
  RUN_TEST(test_wrap_deadline_isr_wrap_inside_a_lane_turn_defers_and_loses_nothing);
  RUN_TEST(test_wrap_deadline_stop_undo_entry_waits_for_late_companions);
  RUN_TEST(test_wrap_deadline_predecessor_stop_does_not_wait_for_the_successor);
  RUN_TEST(test_wrap_deadline_held_note_occupancy_survives_an_obligation_retiring_mid_hold);
  // PSRAM-exhaustion ruling (2026-11-04): RED at the baseline (the fatal branch reached 394 times), GREEN since the
  // checked allocation boundary.
  RUN_TEST(test_tile_preparation_external_refusal_is_named_bounded_and_recoverable);
  RUN_TEST(test_tile_preparation_permanent_insufficiency_parks_and_keeps_the_obligation_owed);
  RUN_TEST(test_wrap_cut_memory_by_buffer);
}
